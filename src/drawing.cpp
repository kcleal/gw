//
// Created by Kez Cleal on 12/08/2022.
//
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <vector>
#include <utility>
#include <cstdio>
#include <sstream>
#include <string>
#include <memory>

#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkTextBlob.h"

#include "htslib/sam.h"
#include "BS_thread_pool.h"
#include "ankerl_unordered_dense.h"
#include "hts_funcs.h"
#include "drawing.h"
#include "term_out.h"


namespace Drawing {

    char indelChars[50];

    struct TextItem{
        sk_sp<SkTextBlob> text;
        float x, y;
        TextItem(sk_sp<SkTextBlob> blob, float posX, float posY)
            : text(std::move(blob)), x(posX), y(posY) {}
        TextItem() = default;
    };

    struct TextItemIns{
        sk_sp<SkTextBlob> text;
        float x, y;
        float box_y, box_w;
        TextItemIns(sk_sp<SkTextBlob> blob, float posX, float posY, float boxY, float boxW)
            : text(std::move(blob)), x(posX), y(posY), box_y{boxY}, box_w{boxW} {}
        TextItemIns() = default;
    };

    void drawCoverage(const Themes::IniOptions &opts, std::vector<Segs::ReadCollection> &collections,
                      SkCanvas * const canvas, const Themes::Fonts &fonts, const float covYh, const float refSpace,
                      const float gap, float monitorScale, std::vector<std::string> &bam_paths) {

        const Themes::BaseTheme &theme = opts.theme;
        SkPaint paint = theme.fcCoverage;
        SkPath path;
        SkRect rect{};
        std::vector<sk_sp<SkTextBlob> > text;
        std::vector<sk_sp<SkTextBlob> > text_ins;
        std::vector<float> textX, textY;
        std::vector<float> textX_ins, textY_ins;
        const float covY = covYh * 0.95;
        const float covY_f = covY * 0.3;
        float yOffsetAll;

        for (auto &cl: collections) {
            if (cl.skipDrawingCoverage) {
                continue;
            }
            cl.skipDrawingCoverage = true;
            yOffsetAll = (cl.yPixels * cl.bamIdx) + refSpace;

            float xScaling = cl.xScaling;
            float xOffset = cl.xOffset;
            bool draw_mismatch_info = (opts.snp_threshold > (int) cl.covArr.size()) && !cl.mmVector.empty();
            bool draw_reference_info = (draw_mismatch_info && opts.soft_clip_threshold > (int) cl.covArr.size());
            std::vector<Segs::Mismatches> &mmVector = cl.mmVector;
            // find mismatches in reads out of view
            if (draw_mismatch_info && !cl.collection_processed) {
                Segs::findMismatches(opts, cl);
            }

            const std::vector<int> &covArr_r = cl.covArr;
            std::vector<float> c;
            c.resize(cl.covArr.size());
            c[0] = (float) cl.covArr[0];
            int cMaxi = (c[0] > 10) ? (int) c[0] : 10;
            float cMax;
            bool processThis = draw_mismatch_info && !cl.collection_processed;
            for (size_t i = 1; i < c.size(); ++i) { // cum sum
                c[i] = ((float) covArr_r[i]) + c[i - 1];
                if (c[i] > cMaxi) {
                    cMaxi = (int) c[i];
                }
                if (c[i] > 0) {
                    // normalise mismatched bases to nearest whole percentage (avoids extra memory allocation)
                    if (processThis) {  //
                        if (mmVector[i].A || mmVector[i].T || mmVector[i].C || mmVector[i].G) {
                            mmVector[i].A = (mmVector[i].A > 1) ? (uint32_t) ((((float) mmVector[i].A) / c[i]) * 100)
                                                                : 0;
                            mmVector[i].T = (mmVector[i].T > 1) ? (uint32_t) ((((float) mmVector[i].T) / c[i]) * 100)
                                                                : 0;
                            mmVector[i].C = (mmVector[i].C > 1) ? (uint32_t) ((((float) mmVector[i].C) / c[i]) * 100)
                                                                : 0;
                            mmVector[i].G = (mmVector[i].G > 1) ? (uint32_t) ((((float) mmVector[i].G) / c[i]) * 100)
                                                                : 0;
                        }
                    }
                }
            }
            cl.collection_processed = true;
            cl.maxCoverage = cMaxi;

            if (opts.log2_cov) {
                for (size_t i = 0; i < c.size(); ++i) {
                    if (c[i] > 0) { c[i] = std::log2(c[i]); }
                }
                cMax = std::log2(cMaxi);
            } else if (cMaxi < opts.max_coverage) {
                cMax = cMaxi;
            } else {
                cMax = (float) opts.max_coverage;
                cMaxi = (int) cMax;
            }
            // normalize to space available
            for (auto &i: c) {
                if (i > cMax) {
                    i = 0;
                } else {
                    i = ((1 - (i / cMax)) * covY) * 0.7;
                }
                i += yOffsetAll + covY_f;
            }
            int step;
            if (c.size() > 2000) {
                step = std::max(1, (int) (c.size() / 2000));
            } else {
                step = 1;
            }
            float startY = yOffsetAll + covY;
            float lastY = startY;
            double x = xOffset;

            path.reset();
            path.moveTo(x, lastY);
            for (size_t i = 0; i < c.size(); ++i) {
                if (i % step == 0 || i == c.size() - 1) {
                    path.lineTo(x, lastY);
                    path.lineTo(x, c[i]);
                }
                lastY = c[i];
                x += xScaling;
            }
            path.lineTo(x - xScaling, yOffsetAll + covY);
            path.lineTo(xOffset, yOffsetAll + covY);
            path.close();
            canvas->drawPath(path, paint);

            if (draw_mismatch_info) {
                const char *refSeq = cl.region->refSeq;
                float mmPosOffset, mmScaling;
                if ((int) mmVector.size() < 500) {
                    mmPosOffset = 0.05;
                    mmScaling = 0.9;
                } else {
                    mmPosOffset = 0;
                    mmScaling = 1;
                }
                float width;
                if ((int) mmVector.size() <= opts.snp_threshold) {
                    float mms = xScaling * mmScaling;
                    width = ((int)mmVector.size() < opts.snp_threshold) ? ((1. > mms) ? 1. : mms) : xScaling;
                } else {
                    width = xScaling;
                }

                int i = 0;
                int refSeqLen = cl.region->refSeqLen;
                for (const auto &mm: mmVector) {
                    float cum_h = 0;
                    float mm_h;
                    bool any_mm = false;
                    if (mm.A > 2) {
                        mm_h = (float) mm.A * 0.01 * (yOffsetAll + covY - c[i]);
                        rect.setXYWH(xOffset + (i * xScaling) + mmPosOffset, startY - cum_h - mm_h, width, mm_h);
                        canvas->drawRect(rect, theme.fcA);
                        cum_h += mm_h;
                        any_mm = true;
                    }
                    if (mm.C > 2) {
                        mm_h = (float) mm.C * 0.01 * (yOffsetAll + covY - c[i]);
                        rect.setXYWH(xOffset + (i * xScaling) + mmPosOffset, startY - cum_h - mm_h, width, mm_h);
                        canvas->drawRect(rect, theme.fcC);
                        cum_h += mm_h;
                        any_mm = true;
                    }
                    if (mm.T > 2) {
                        mm_h = (float) mm.T * 0.01 * (yOffsetAll + covY - c[i]);
                        rect.setXYWH(xOffset + (i * xScaling) + mmPosOffset, startY - cum_h - mm_h, width, mm_h);
                        canvas->drawRect(rect, theme.fcT);
                        cum_h += mm_h;
                        any_mm = true;
                    }
                    if (mm.G > 2) {
                        mm_h = (float) mm.G * 0.01 * (yOffsetAll + covY - c[i]);
                        rect.setXYWH(xOffset + (i * xScaling) + mmPosOffset, startY - cum_h - mm_h, width, mm_h);
                        canvas->drawRect(rect, theme.fcG);
                        cum_h += mm_h;
                        any_mm = true;
                    }
                    if (draw_reference_info && any_mm) {
                        mm_h = yOffsetAll + covY - c[i] - cum_h;
                        if (mm_h < 0.001 || (cum_h / (yOffsetAll + covY - c[i]) < 0.2)) {
                            i += 1;
                            continue;
                        }
                        const SkPaint *paint_ref;
                        if (i >= refSeqLen) {
                            break;
                        }
                        switch (refSeq[i]) {
                            case 'A':
                                paint_ref = &theme.fcA;
                                break;
                            case 'C':
                                paint_ref = &theme.fcC;
                                break;
                            case 'G':
                                paint_ref = &theme.fcG;
                                break;
                            case 'T':
                                paint_ref = &theme.fcT;
                                break;
                            case 'N':
                                paint_ref = &theme.fcN;
                                break;
                            case 'a':
                                paint_ref = &theme.fcA;
                                break;
                            case 'c':
                                paint_ref = &theme.fcC;
                                break;
                            case 'g':
                                paint_ref = &theme.fcG;
                                break;
                            case 't':
                                paint_ref = &theme.fcT;
                                break;
                            default:
                                paint_ref = &theme.fcCoverage;
                                break;
                        }
                        rect.setXYWH(xOffset + (i * xScaling) + mmPosOffset, startY - cum_h - mm_h, width, mm_h);
                        canvas->drawRect(rect, *paint_ref);
                    }
                    i += 1;
                }
            }

            if (cl.region->markerPos != -1) {
                float rp;

                if (opts.scale_bar) {
                    rp = gap + fonts.overlayHeight + gap + fonts.overlayHeight + gap + (cl.bamIdx * cl.yPixels);
                } else {
                    rp = gap + fonts.overlayHeight + gap + (cl.bamIdx * cl.yPixels);
                }
                float xp = fonts.overlayHeight * 0.5;
                float markerP = (cl.xScaling * (float) (cl.region->markerPos - cl.region->start)) + cl.xOffset;
                if (markerP > cl.xOffset && markerP < cl.regionPixels - cl.xOffset) {
                    path.reset();
                    path.moveTo(markerP, rp);
                    path.lineTo(markerP - xp, rp);
                    path.lineTo(markerP, rp + (fonts.overlayHeight));
                    path.lineTo(markerP + xp, rp);
                    path.lineTo(markerP, rp);
                    canvas->drawPath(path, theme.fcMarkers);
                }
                float markerP2 = (cl.xScaling * (float) (cl.region->markerPosEnd - cl.region->start)) + cl.xOffset;
                if (markerP2 > cl.xOffset && markerP2 < (cl.regionPixels + cl.xOffset)) {
                    path.reset();
                    path.moveTo(markerP2, rp);
                    path.lineTo(markerP2 - xp, rp);
                    path.lineTo(markerP2, rp + (fonts.overlayHeight));
                    path.lineTo(markerP2 + xp, rp);
                    path.lineTo(markerP2, rp);
                    canvas->drawPath(path, theme.fcMarkers);
                }
            }

            std::sprintf(indelChars, "%d", cMaxi);

            sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(indelChars, fonts.overlay);
            canvas->drawTextBlob(blob, xOffset + 8 * monitorScale, covY_f + yOffsetAll + fonts.overlayHeight, theme.tcDel);
            path.reset();
            path.moveTo(xOffset, covY_f + yOffsetAll);
            path.lineTo(xOffset + 6 * monitorScale, covY_f + yOffsetAll);
            path.moveTo(xOffset, covY + yOffsetAll);
            path.lineTo(xOffset + 6 * monitorScale, covY + yOffsetAll);
            canvas->drawPath(path, theme.lcJoins);

            // Draw data labels when alignments are not shown
            if (opts.data_labels && !opts.alignments && cl.regionIdx == 0) {
                if (cl.name.empty()) {
                    std::filesystem::path fsp(bam_paths[cl.bamIdx]);
#if defined(_WIN32) || defined(_WIN64)
                    const wchar_t* pc = fsp.filename().c_str();
                    std::wstring ws(pc);
                    std::string p(ws.begin(), ws.end());
                    cl.name = p;
#else
                    cl.name = fsp.filename();
#endif
                }
                if (!cl.name.empty()) {
                    const char * name_s = cl.name.c_str();
                    sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(name_s, fonts.overlay);
                    float text_width = fonts.overlay.measureText(name_s, cl.name.size(), SkTextEncoding::kUTF8);
                    rect.setXYWH(cl.xOffset - 20, cl.yOffset - fonts.overlayHeight * 3, text_width + 20 + 8 * monitorScale + 8 * monitorScale, fonts.overlayHeight * 2);
                    canvas->drawRect(rect, theme.bgPaint);
                    canvas->drawTextBlob(blob, cl.xOffset + 8 * monitorScale, cl.yOffset - fonts.overlayHeight * 1.6, theme.tcDel);
                }
            }
        }
    }

    inline void
    chooseFacecolors(const int mapq, const Segs::Align &a, SkPaint &faceColor, const Themes::BaseTheme &theme) {
        if (mapq == 0) {
            switch (a.orient_pattern) {
                case Segs::NORMAL:
                    faceColor = theme.fcNormal0;
                    break;
                case Segs::DEL:
                    faceColor = theme.fcDel0;
                    break;
                case Segs::INV_F:
                    faceColor = theme.fcInvF0;
                    break;
                case Segs::INV_R:
                    faceColor = theme.fcInvR0;
                    break;
                case Segs::DUP:
                    faceColor = theme.fcDup0;
                    break;
                case Segs::TRA:
                    faceColor = theme.mate_fc0[(a.delegate->core.tid + ((a.delegate->core.mtid >= 0) ?  a.delegate->core.mtid : 0)) % 48];
                    break;
            }
        } else {
            switch (a.orient_pattern) {
                case Segs::NORMAL:
                    faceColor = theme.fcNormal;
                    break;
                case Segs::DEL:
                    faceColor = theme.fcDel;
                    break;
                case Segs::INV_F:
                    faceColor = theme.fcInvF;
                    break;
                case Segs::INV_R:
                    faceColor = theme.fcInvR;
                    break;
                case Segs::DUP:
                    faceColor = theme.fcDup;
                    break;
                case Segs::TRA:
                    faceColor = theme.mate_fc[(a.delegate->core.tid + ((a.delegate->core.mtid >= 0) ?  a.delegate->core.mtid : 0)) % 48];
                    break;
            }
        }
    }

    inline void
    chooseEdgeColor(const int edge_type, SkPaint &edgeColor, const Themes::BaseTheme &theme) {
        if (edge_type == 2) {
            edgeColor = theme.ecSplit;
        } else if (edge_type == 4) {
            edgeColor = theme.ecSelected;
        } else {
            edgeColor = theme.ecMateUnmapped;
        }
    }

    inline void
    drawRectangle(SkCanvas *const canvas, const float polygonH, const float yScaledOffset, const float start,
                  const float width, const SkPaint &faceColor, SkRect &rect) {
        rect.setXYWH(start, yScaledOffset, width, polygonH);
        canvas->drawRect(rect, faceColor);
    }

    SkPoint points[5];

    inline void drawLeftPointedRectangleNoEdge(SkCanvas *const canvas, const float polygonH, const float yScaledOffset, const float start,
                                         const float width, const float xOffset, const SkPaint &faceColor,
                                         SkPath &path, const float slop) {
        const float startX = start + xOffset;
        const float midY = yScaledOffset + (polygonH * 0.5);
        const float endY = yScaledOffset + polygonH;
        const float endX = start + width + xOffset;
        const float slopedStartX = start - slop + xOffset;
        points[0] = SkPoint::Make(startX, yScaledOffset);
        points[1] = SkPoint::Make(slopedStartX, midY);
        points[2] = SkPoint::Make(startX, endY);
        points[3] = SkPoint::Make(endX, endY);
        points[4] = SkPoint::Make(endX, yScaledOffset);
        path.reset();
        path.addPoly(points, 5, true);
        canvas->drawPath(path, faceColor);
    }

    inline void drawRightPointedRectangleNoEdge(SkCanvas *const canvas, const float polygonH, const float yScaledOffset, const float start,
                                          const float width, const float xOffset, const SkPaint &faceColor,
                                          SkPath &path, const float slop) {
        const float startX = start + xOffset;
        const float endY = yScaledOffset + polygonH;
        const float midY = yScaledOffset + (polygonH * 0.5);
        const float endX = start + width + xOffset;
        const float slopedEndX = endX + slop;
        points[0] = SkPoint::Make(startX, yScaledOffset);
        points[1] = SkPoint::Make(startX, endY);
        points[2] = SkPoint::Make(endX, endY);
        points[3] = SkPoint::Make(slopedEndX, midY);
        points[4] = SkPoint::Make(endX, yScaledOffset);
        path.reset();
        path.addPoly(points, 5, true);
        canvas->drawPath(path, faceColor);
    }

    inline void drawLeftPointedRectangle(SkCanvas *const canvas, const float polygonH, const float yScaledOffset, const float start,
                                         const float width, const float xOffset, const SkPaint &faceColor,
                                         SkPath &path, const float slop, bool edged, const SkPaint &edgeColor) {
        const float startX = start + xOffset;
        const float midY = yScaledOffset + (polygonH * 0.5);
        const float endY = yScaledOffset + polygonH;
        const float endX = start + width + xOffset;
        const float slopedStartX = start - slop + xOffset;
        points[0] = SkPoint::Make(startX, yScaledOffset);
        points[1] = SkPoint::Make(slopedStartX, midY);
        points[2] = SkPoint::Make(startX, endY);
        points[3] = SkPoint::Make(endX, endY);
        points[4] = SkPoint::Make(endX, yScaledOffset);
        path.reset();
        path.addPoly(points, 5, true);
        canvas->drawPath(path, faceColor);
        if (edged) {
            points[0].fY += 0.5;
            points[4].fY += 0.5;
            path.reset();
            path.addPoly(points, 5, true);
            canvas->drawPath(path, edgeColor);
        }
    }

    inline void drawRightPointedRectangle(SkCanvas *const canvas, const float polygonH, const float yScaledOffset, float start,
                                          float width, const float xOffset, const SkPaint &faceColor,
                                          SkPath &path, const float slop, bool edged, const SkPaint &edgeColor) {

        const float startX = start + xOffset;
        const float endY = yScaledOffset + polygonH;
        const float midY = yScaledOffset + (polygonH * 0.5);
        const float endX = start + width + xOffset;
        const float slopedEndX = endX + slop;
        points[0] = SkPoint::Make(startX, yScaledOffset);
        points[1] = SkPoint::Make(startX, endY);
        points[2] = SkPoint::Make(endX, endY);
        points[3] = SkPoint::Make(slopedEndX, midY);
        points[4] = SkPoint::Make(endX, yScaledOffset);
        path.reset();
        path.addPoly(points, 5, true);
        canvas->drawPath(path, faceColor);
        if (edged) {
            points[0].fY += 0.5;
            points[4].fY += 0.5;
            path.reset();
            path.addPoly(points, 5, true);
            canvas->drawPath(path, edgeColor);
        }
    }

    inline void
    drawHLine(SkCanvas * const canvas, SkPath &path, const SkPaint &lc, const float startX, const float y, const float endX) {
        path.reset();
        path.moveTo(startX, y);
        path.lineTo(endX, y);
        canvas->drawPath(path, lc);
    }

    inline void
    drawIns(SkCanvas * const canvas, const float y0, const float start, const float yScaling, const float xOffset,
            const float yOffset, const SkPaint &faceColor, SkRect &rect, const float pH, const float overhang, const float width) {

        const float x = start + xOffset;
        const float y = y0 * yScaling;
        const float box_left = x - (width * 0.5);

        rect.setXYWH(box_left, y + yOffset, width, pH);
        canvas->drawRect(rect, faceColor);  // middle bar

        rect.setXYWH(box_left - overhang, yOffset + y, overhang + width + overhang, overhang);
        canvas->drawRect(rect, faceColor);  // top bar

        rect.setXYWH(box_left - overhang, yOffset + y + pH - overhang, overhang + width + overhang, overhang);
        canvas->drawRect(rect, faceColor);  // bottom bar

    }

    constexpr std::array<char, 256> make_lookup_ref_base() {
        std::array<char, 256> a{};
        for (auto& elem : a) {
            elem = 15;  // Initialize all elements to 15
        }
        a['A'] = 1; a['a'] = 1;
        a['C'] = 2; a['c'] = 2;
        a['G'] = 4; a['g'] = 4;
        a['T'] = 8; a['t'] = 8;
        a['N'] = 15; a['n'] = 15;
        return a;
    }
    constexpr std::array<char, 256> lookup_ref_base = make_lookup_ref_base();

    void update_A(Segs::Mismatches& elem) { elem.A += 1; }
    void update_C(Segs::Mismatches& elem) { elem.C += 1; }
    void update_G(Segs::Mismatches& elem) { elem.G += 1; }
    void update_T(Segs::Mismatches& elem) { elem.T += 1; }
    void update_pass(Segs::Mismatches& elem) {}  // For N bases

    // Lookup table for function pointers, initialized statically
    void (*lookup_table_mm[16])(Segs::Mismatches&) = {
            update_pass,     // 0
            update_A,        // 1
            update_C,        // 2
            update_pass,     // 3
            update_G,        // 4
            update_pass,     // 5
            update_pass,     // 6
            update_pass,     // 7
            update_T,        // 8
            update_pass,     // 9
            update_pass,     // 10
            update_pass,     // 11
            update_pass,     // 12
            update_pass,     // 13
            update_pass,     // 14
            update_pass      // 15
    };

    // Lookup table of pre-created text blobs
    std::array<sk_sp<SkTextBlob>, 16> lookup_table_bam_textblobs;

    void initializeTextBlobs(const Themes::Fonts &fonts) {
        // Nibble to text
        std::array<const char*, 16> lookup_table_bam_text = {
                "",     // 0
                "A",    // 1
                "C",    // 2
                "",     // 3
                "G",    // 4
                "",     // 5
                "",     // 6
                "",     // 7
                "T",    // 8
                "",     // 9
                "",     // 10
                "",     // 11
                "",     // 12
                "",     // 13
                "",     // 14
                "N"      // 15
        };
         for (int i = 0; i < 16; i++) {
            lookup_table_bam_textblobs[i] = SkTextBlob::MakeFromString(
                lookup_table_bam_text[i], fonts.overlay);
        }
    }

    int countDigits(uint32_t number) {
        if (number < 10) return 1;
        if (number < 100) return 2;
        if (number < 1000) return 3;
        if (number < 10000) return 4;
        if (number < 100000) return 5;
        if (number < 1000000) return 6;
        if (number < 10000000) return 7;
        return 8;
    }

    void drawMismatchesNoMD(SkCanvas *const canvas, SkRect &rect, const Themes::BaseTheme &theme,
                       const Themes::Fonts &fonts, const Utils::Region *region,
                       const Segs::Align &align,
                       float width, float xScaling, float xOffset, float mmPosOffset, float yScaledOffset,
                       float pH, int l_qseq, std::vector<Segs::Mismatches> &mm_array,
                       bool &collection_processed, bool charFits, float textOffsetX, float textOffsetY) {
        if (!region->refSeq || align.blocks.empty() || mm_array.empty()) {
            collection_processed = true;
            return;
        }

        uint8_t *ptr_seq = bam_get_seq(align.delegate);
        if (ptr_seq == nullptr) return;

        uint8_t *ptr_qual = bam_get_qual(align.delegate);
        const char *refSeq = region->refSeq;
        int refSeqLen = region->refSeqLen;

        // Pre-calculate constants used in loops
        const float precalculated_xOffset_mmPosOffset = xOffset + mmPosOffset;
        const float text_x_offset = textOffsetX;
        const float text_y_offset = textOffsetY;

        for (const auto& blk : align.blocks) {
            uint32_t idx_start, idx_end;
            uint32_t pos_start;
            if ((int)blk.end < region->start) {
                continue;
            } else if ((int)blk.start >= region->end) {
                return;
            }
            if ((int)blk.start < region->start) {
                pos_start = region->start;
                idx_start = blk.seq_index + (pos_start - blk.start);
            } else {
                pos_start = blk.start;
                idx_start = blk.seq_index;
            }
            if ((int)blk.end < region->end) {
                idx_end = blk.seq_index + (blk.end - blk.start);
            } else {
                idx_end = (blk.seq_index + (blk.end - blk.start)) - (blk.end - region->end);
            }

            size_t ref_idx = pos_start - region->start;
            for (size_t i=idx_start; i < (size_t)idx_end; ++i) {
                if ((int)ref_idx >= refSeqLen) {
                    break;
                }
                char ref_base = lookup_ref_base[(unsigned char)refSeq[ref_idx]];
                char bam_base = bam_seqi(ptr_seq, i);
                if (bam_base != ref_base) {
                    float p = ref_idx * xScaling;
                    uint32_t colorIdx = (l_qseq == 0) ? 10 : (ptr_qual[i] > 10) ? 10 : ptr_qual[i];
                    rect.setXYWH(p + precalculated_xOffset_mmPosOffset, yScaledOffset, width, pH);
                    canvas->drawRect(rect, theme.BasePaints[bam_base][colorIdx]);
                    if (!collection_processed) {
                        lookup_table_mm[(unsigned char)bam_base](mm_array[ref_idx]);
                    }
                    if (charFits) {
                        canvas->drawTextBlob(lookup_table_bam_textblobs[(int)bam_base],
                                           p + precalculated_xOffset_mmPosOffset + text_x_offset,
                                           yScaledOffset + text_y_offset,
                                           theme.tcIns);
                    }
                }
                ref_idx += 1;
            }
        }
    }

    void drawMismatchesNoMD2(SkCanvas *const canvas, SkRect &rect, const Themes::BaseTheme &theme,
                            const Themes::Fonts &fonts, const Utils::Region *region,
                            const Segs::Align &align,
                            float width, float xScaling, float xOffset, float mmPosOffset, float yScaledOffset,
                            float pH, int l_qseq, std::vector<Segs::Mismatches> &mm_array,
                            bool &collection_processed) {
        if (mm_array.empty()) {
            collection_processed = true;
            return;
        }
        size_t mm_array_len = mm_array.size();

        uint32_t r_pos = align.pos;
        uint32_t cigar_l = align.delegate->core.n_cigar;
        uint8_t *ptr_seq = bam_get_seq(align.delegate);
        uint32_t *cigar_p = bam_get_cigar(align.delegate);
        auto *ptr_qual = bam_get_qual(align.delegate);

        if (cigar_l == 0 || ptr_seq == nullptr || cigar_p == nullptr) {
            return;
        }

        if (!region->refSeq) return;
        const char *refSeq = region->refSeq;
        uint32_t qseq_len = align.delegate->core.l_qseq;
        const uint32_t rlen = region->end - region->start;
        const uint32_t rbegin = region->start;
        const uint32_t rend = region->end;
        uint32_t idx = 0, op, l;
        float p, precalculated_xOffset_mmPosOffset = xOffset + mmPosOffset; // Precalculate this sum

        for (uint32_t k = 0; k < cigar_l; k++) {
            op = cigar_p[k] & BAM_CIGAR_MASK;
            l = cigar_p[k] >> BAM_CIGAR_SHIFT;
            if (idx >= qseq_len) {  // shouldn't happen
                break;
            }
            switch (op) {
                case BAM_CMATCH:
                    for (uint32_t i = 0; i < l; ++i) {
                        int r_idx = (int) r_pos - rbegin;
                        if (r_idx < 0) {
                            idx += 1;
                            r_pos += 1;
                            continue;
                        }
                        if (r_idx >= (int) rlen) {
                            break;
                        }
                        if (r_pos - rbegin >= mm_array_len) {
                            break;
                        }

                        char ref_base;
                        char current_base = refSeq[r_idx];
                        ref_base = lookup_ref_base[(unsigned char)current_base];

                        char bam_base = bam_seqi(ptr_seq, idx);
                        if (bam_base != ref_base) {
                            p = (r_pos - rbegin) * xScaling;
                            uint32_t colorIdx = (l_qseq == 0) ? 10 : (ptr_qual[idx] > 10) ? 10 : ptr_qual[idx];
                            rect.setXYWH(p + precalculated_xOffset_mmPosOffset, yScaledOffset, width, pH);
                            canvas->drawRect(rect, theme.BasePaints[bam_base][colorIdx]);
                            if (!collection_processed) {
                                lookup_table_mm[(size_t)bam_base](mm_array[r_pos - rbegin]);
                            }
                        }
                        idx += 1;
                        r_pos += 1;
                    }
                    break;
                case BAM_CSOFT_CLIP:
                case BAM_CINS:
                    idx += l;
                    break;
                case BAM_CDEL:
                case BAM_CREF_SKIP:
                    r_pos += l;
                    break;
                case BAM_CDIFF:
                    for (uint32_t i = 0; i < l; ++i) {
                        if (r_pos >= rbegin && r_pos < rend && r_pos - rbegin < mm_array_len) {
                            char bam_base = bam_seqi(ptr_seq, idx);
                            p = (r_pos - rbegin) * xScaling;
                            uint32_t colorIdx = (l_qseq == 0) ? 10 : (ptr_qual[idx] > 10) ? 10 : ptr_qual[idx];
                            rect.setXYWH(p + precalculated_xOffset_mmPosOffset, yScaledOffset, width, pH);
                            canvas->drawRect(rect, theme.BasePaints[bam_base][colorIdx]);

                            if (!collection_processed) {
                                lookup_table_mm[(size_t) bam_base](mm_array[r_pos - rbegin]);
                            }
                        }
                        idx += 1;
                        r_pos += 1;
                    }
                    break;
                case BAM_CHARD_CLIP:
                    continue;
                default:
                    idx += l;
                    r_pos += l;
            }
        }
    }

    void drawBlock(const bool plotPointedPolygons, const bool pointLeft, const bool edged, const float s, const float width,
                   const float pointSlop, const float pH, const float yScaledOffset, const float xOffset,
                   SkCanvas *const canvas, SkPath &path, SkRect &rect, SkPaint &faceColor, SkPaint &edgeColor) {

        if (plotPointedPolygons) {
            if (pointLeft) {
                drawLeftPointedRectangle(canvas, pH, yScaledOffset, s, width,
                                         xOffset, faceColor, path, pointSlop, edged, edgeColor);

            } else {
                drawRightPointedRectangle(canvas, pH, yScaledOffset, s, width,
                                          xOffset, faceColor, path, pointSlop, edged, edgeColor);

            }
        } else {
            drawRectangle(canvas, pH, yScaledOffset, s  + xOffset, width, faceColor,rect);
        }
    }

    void drawDeletionLine(SkCanvas *const canvas, SkPath &path, const Themes::IniOptions &opts,
                  const Themes::Fonts &fonts,
                  const int regionBegin, const int Y, const int regionLen, const int starti, int const lastEndi,
                  const float regionPixels, const float xScaling, const float yScaling, const float xOffset, const float yOffset,
                  std::vector<TextItem> &text, const bool indelTextFits, const float halfPolygonHeight) {
        const int isize = starti - lastEndi;
        const int lastEnd = lastEndi - regionBegin;
        const int startRelative = starti - regionBegin;
        const int size = startRelative - lastEnd;
        if (size <= 0) {
            return;
        }
        const float delBegin = lastEnd * xScaling;
        const float delEnd = delBegin + (size * xScaling);
        const float yh = (Y * yScaling + yOffset) + halfPolygonHeight;
        if (isize >= opts.indel_length) {
            if (regionLen < 500000 && indelTextFits) {
                const int digits = countDigits(isize);
                std::to_string(isize).copy(indelChars, digits);
                indelChars[digits] = '\0';
                const float textWidth = fonts.textWidths[digits - 1];
                const float textCenter = (lastEnd + size * 0.5f) * xScaling;
                const float textBegin = textCenter - (textWidth * 0.5f);
                const float textEnd = textBegin + textWidth;
                const float textYPosition = yh + (fonts.overlayHeight * 0.5f);
                text.emplace_back(
                    SkTextBlob::MakeFromString(indelChars, fonts.overlay),
                    textBegin + xOffset,
                    textYPosition
                );
                if (textBegin > delBegin) {
                    drawHLine(canvas, path, opts.theme.lcJoins, delBegin + xOffset, yh, textBegin + xOffset);
                    drawHLine(canvas, path, opts.theme.lcJoins, textEnd + xOffset, yh, delEnd + xOffset);
                }
            } else { // Draw dot or line without text
                if (delEnd - delBegin < 2) {
                    canvas->drawPoint(delBegin + xOffset, yh, opts.theme.lcBright);
                } else {
                    drawHLine(canvas, path, opts.theme.lcJoins, delBegin + xOffset, yh, delEnd + xOffset);
                }
            }
        } else if ((float)size * 2000.0f > (float)regionLen) { // Equivalent to size/regionLen > 0.0005, but avoids division
            drawHLine(canvas, path, opts.theme.lcJoins, delBegin + xOffset, yh, delEnd + xOffset);
        }
    }

    void drawMods(SkCanvas *const canvas, SkRect &rect, const Themes::BaseTheme &theme, const Utils::Region *region,
                  const Segs::Align &align,
                  const float width, const float xScaling, const float xOffset, const float mmPosOffset, const float yScaledOffset,
                  const float pH, const int l_qseq, const float monitorScale, const bool as_dots) {
        if (align.any_mods.empty()) {
            return;
        }
        float precalculated_xOffset_mmPosOffset, h, top, middle, bottom, w;
        if (as_dots) {
            precalculated_xOffset_mmPosOffset = xOffset + mmPosOffset + (0.5 * xScaling);
            top = yScaledOffset + (pH * 0.3333);
            middle = yScaledOffset + pH - (pH * 0.5);
            bottom = yScaledOffset + pH - (pH * 0.3333);
        } else {
            precalculated_xOffset_mmPosOffset = xOffset + mmPosOffset;
            h = pH * 0.25;
            top = yScaledOffset + h;
            middle = top + h;
            bottom = top + h;
            w = std::fmax(monitorScale, xScaling);
        }

        auto mod_it = align.any_mods.begin();
        auto mod_end = align.any_mods.end();

        auto mc_paint = &theme.ModPaints[0];
        auto hmc_paint = &theme.ModPaints[1];
        auto other_paint = &theme.ModPaints[2];

        if (as_dots) {
            for (const auto& blk : align.blocks) {
                if ((int)blk.end < region->start) {
                    continue;
                } else if ((int)blk.start >= region->end) {
                    return;
                }
                int idx_start = blk.seq_index;
                int idx_end = blk.seq_index + (blk.end - blk.start);
                while (mod_it != mod_end && mod_it->index < idx_start) {
                    ++mod_it;
                }
                while (mod_it != mod_end && mod_it->index < idx_end) {
                    float x = ((((int)blk.start + (int)mod_it->index - idx_start) - region->start) * xScaling) + precalculated_xOffset_mmPosOffset;
                    if (x < 0) {
                        ++mod_it;
                        continue;
                    }
                    int n_mods = mod_it->n_mods;
                    for (size_t j=0; j < (size_t)n_mods; ++j) {
                        switch (mod_it->mods[j]) {
                            case 'm':  // 5mC
                                canvas->drawPoint(x, top, (*mc_paint)[ mod_it->quals[j] % 4 ]);
                                break;
                            case 'h':  // 5hmC
                                canvas->drawPoint(x, bottom, (*hmc_paint)[ mod_it->quals[j] % 4 ]);
                                break;
                            default:
                                canvas->drawPoint(x, middle, (*other_paint)[ mod_it->quals[j] % 4 ]);
                                break;
                        }
                    }
                    ++mod_it;
                }
            }
        } else {
            for (const auto& blk : align.blocks) {
                if ((int)blk.end < region->start) {
                    continue;
                } else if ((int)blk.start >= region->end) {
                    return;
                }
                int idx_start = blk.seq_index;
                int idx_end = blk.seq_index + (blk.end - blk.start);
                while (mod_it != mod_end && mod_it->index < idx_start) {
                    ++mod_it;
                }
                while (mod_it != mod_end && mod_it->index < idx_end) {
                    float x = ((((int)blk.start + (int)mod_it->index - idx_start) - region->start) * xScaling) + precalculated_xOffset_mmPosOffset;
                    if (x < 0) {
                        ++mod_it;
                        continue;
                    }
                    int n_mods = mod_it->n_mods;
                    for (size_t j=0; j < (size_t)n_mods; ++j) {
                        switch (mod_it->mods[j]) {
                            case 'm':  // 5mC
                                rect.setXYWH(x, top, w, h);
                                canvas->drawRect(rect, (*mc_paint)[ mod_it->quals[j] % 4 ]);
                                break;
                            case 'h':  // 5hmC
                                rect.setXYWH(x, bottom, w, h);
                                canvas->drawRect(rect, (*hmc_paint)[ mod_it->quals[j] % 4 ]);
                                break;
                            default:
                                rect.setXYWH(x, middle, w, h);
                                canvas->drawRect(rect, (*other_paint)[ mod_it->quals[j] % 4 ]);
                                break;
                        }
                    }
                    ++mod_it;
                }
            }
        }
    }

    void drawCollection(const Themes::IniOptions &opts, Segs::ReadCollection &cl,
                  SkCanvas *const canvas, const float trackY, const float yScaling, const Themes::Fonts &fonts, const int linkOp,
                  const float refSpace, const float pointSlop, const float textDrop, const float pH, const float monitorScale,
                  std::vector<std::string> &bam_paths) {

        SkPaint faceColor;
        SkPaint edgeColor;

        SkRect rect;
        SkPath path;
        const Themes::BaseTheme &theme = opts.theme;

        static std::vector<TextItemIns> text_ins;
        static std::vector<TextItem> text_del;
        text_ins.clear();
        text_del.clear();

        const int regionBegin = cl.region->start;
        const int regionEnd = cl.region->end;
        const int regionLen = regionEnd - regionBegin;

        const float xScaling = cl.xScaling;
        const float xOffset = cl.xOffset;
        const float yOffset = cl.yOffset;
        const float regionPixels = cl.regionPixels;

        const float halfPolygonHeight = pH * 0.5f;

        const float ins_block_h = std::fmin(pH * 0.3, monitorScale * 2);
        const float ins_block_w = std::fmax(ins_block_h, xScaling * 0.5);

        const int min_gap_size = 1 + (1 / (cl.regionPixels / regionLen));

        const bool plotSoftClipAsBlock = cl.plotSoftClipAsBlock;
        const bool plotPointedPolygons = cl.plotPointedPolygons;
        const bool drawEdges = cl.drawEdges;

        std::vector<Segs::Mismatches> &mm_vector = cl.mmVector;

        // Mismatch properties
        float mmPosOffset, mmScaling;
        if (regionLen < 500) {
            mmPosOffset = 0.05;
            mmScaling = 0.9;
        } else {
            mmPosOffset = 0;
            mmScaling = 1;
        }

        const float mms = xScaling * mmScaling;
        float mm_textOffsetX = 0;
        float mm_textOffsetY = 0;
        const float mm_width = (regionLen < 500000) ? ((1. > mms) ? 1. : mms) : xScaling;
        const bool mm_charFits = ( (fonts.overlayCharWidth + (monitorScale*2)) < mm_width - mmPosOffset) &
                    ( (fonts.overlayHeight < pH - (monitorScale)) );
        if (mm_charFits) {
            initializeTextBlobs(fonts);
            mm_textOffsetX = ((mm_width - mmPosOffset) / 2) - (fonts.overlayCharWidth / 2);
            mm_textOffsetY = (pH / 2) + (fonts.overlayHeight / 2);
        }

        cl.skipDrawingReads = true;

        for (const auto &a: cl.readQueue) {
            int Y = a.y;
            assert (Y >= -2);
            if (Y < 0) {
                continue;
            }
            const bool indelTextFits = fonts.overlayHeight < yScaling;
            const int mapq = a.delegate->core.qual;
            const float yScaledOffset = (Y * yScaling) + yOffset;
            chooseFacecolors(mapq, a, faceColor, theme);
            bool pointLeft, edged;
            if (plotPointedPolygons) {
                pointLeft = (a.delegate->core.flag & 16) != 0;
            } else {
                pointLeft = false;
            }
            size_t nBlocks = a.blocks.size();
            assert (nBlocks >= 1);
            if (drawEdges && a.edge_type != 1) {
                edged = true;
                chooseEdgeColor(a.edge_type, edgeColor, theme);
            } else {
                edged = false;
            }
            double width, s, e; //, textW;
            int lastEnd = 1215752191;
            int starti = 0;
            size_t idx;

            // draw gapped
            if (nBlocks > 1) {
                idx = 1;
                size_t idx_begin = 0;
                for (; idx < nBlocks; ++idx) {
                    starti = (int) a.blocks[idx].start;
                    lastEnd = (int) a.blocks[idx - 1].end;
                    if (starti - lastEnd == 0) {
                        continue;  // insertion, draw over the top later on
                    }
                    if (starti - lastEnd >= min_gap_size) {
                        s = (double)a.blocks[idx_begin].start - regionBegin;
                        e = (double)a.blocks[idx - 1].end - regionBegin;
                        width = (e - s) * xScaling;
                        drawBlock(plotPointedPolygons, pointLeft, edged, (float) s * xScaling, (float) width,
                                  pointSlop, pH, yScaledOffset, xOffset, canvas, path, rect, faceColor, edgeColor);
                        idx_begin = idx;
                    }
                }
                // Draw final block
                s = (double)a.blocks[idx_begin].start - regionBegin;
                e = (double)a.blocks[idx - 1].end - regionBegin;
                width = (e - s) * xScaling;
                drawBlock(plotPointedPolygons, pointLeft, edged, (float) s * xScaling, (float) width,
                          pointSlop, pH, yScaledOffset, xOffset, canvas, path, rect, faceColor, edgeColor);
                idx_begin = idx;

                idx = 1;
                idx_begin = 0;
                for (; idx < nBlocks; ++idx) {
                    starti = (int) a.blocks[idx].start;
                    lastEnd = (int) a.blocks[idx - 1].end;
                    if (starti - lastEnd == 0) {
                        continue;  // insertion
                    }
                    if (lastEnd <= regionEnd && regionBegin <= starti) {
                        drawDeletionLine(canvas, path, opts, fonts, regionBegin, Y, regionLen, starti, lastEnd,
                            regionPixels, xScaling, yScaling, xOffset, yOffset, text_del, indelTextFits, halfPolygonHeight);
                    }
                }

            } else {
                s = (double)a.blocks[0].start - regionBegin;
                e = (double)a.blocks[0].end - regionBegin;
                width = (e - s) * xScaling;
                drawBlock(plotPointedPolygons, pointLeft, edged, (float) s * xScaling, (float) width,
                          pointSlop, pH, yScaledOffset, xOffset, canvas, path, rect, faceColor, edgeColor);
            }

            // add soft-clip blocks
            const int start = (int) a.pos - regionBegin;
            const int end = (int) a.reference_end - regionBegin;
            auto l_qseq = (int) a.delegate->core.l_qseq;
            if (opts.soft_clip_threshold != 0) {
                if (a.left_soft_clip > 0) {
                    width = (plotSoftClipAsBlock || l_qseq == 0) ? (float) a.left_soft_clip : 0;
                    s = start - a.left_soft_clip;
                    if (s < 0) {
                        width += s;
                        s = 0;
                    }
                    e = start + width;
                    if (start > regionLen) {
                        width = regionLen - start;
                    }
                    if (e > 0 && s < regionLen && width > 0) {
                        if (pointLeft && plotPointedPolygons) {
                            drawLeftPointedRectangle(canvas, pH, yScaledOffset, s * xScaling, width * xScaling,
                                                     xOffset, (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip,
                                                     path, pointSlop, false, edgeColor);
                        } else {
                            drawRectangle(canvas, pH, yScaledOffset, (s * xScaling) + xOffset, width * xScaling,
                                          (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip, rect);
                        }
                    }
                }
                if (a.right_soft_clip > 0) {
                    if (plotSoftClipAsBlock || l_qseq == 0) {
                        s = end;
                        width = (float) a.right_soft_clip;
                    } else {
                        s = end + a.right_soft_clip;
                        width = 0;
                    }
                    e = s + width;
                    if (s < 0) {
                        width += s;
                        s = 0;
                    }
                    if (e > regionLen) {
                        width = regionLen - s;
                        e = regionLen;
                    }
                    if (s < regionLen && e > 0) {
                        if (!pointLeft && plotPointedPolygons) {
                            drawRightPointedRectangle(canvas, pH, yScaledOffset, s * xScaling, width * xScaling,
                                                      xOffset, (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip, path,
                                                      pointSlop, false, edgeColor);
                        } else {
                            drawRectangle(canvas, pH, yScaledOffset, (s * xScaling) + xOffset, width * xScaling,
                                          (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip, rect);
                        }
                    }
                }
            }

            // add mismatches
            if (l_qseq == 0) {
                continue;
            }

            if (regionLen <= opts.snp_threshold) {
                drawMismatchesNoMD(canvas, rect, theme, fonts, cl.region, a, mm_width, xScaling, xOffset, mmPosOffset,
                                   yScaledOffset, pH, l_qseq, mm_vector, cl.collection_processed, mm_charFits, mm_textOffsetX, mm_textOffsetY);
            }

            // add insertions
            if (!a.any_ins.empty()) {
                for (auto &ins: a.any_ins) {
                    const float p = (ins.pos - regionBegin) * xScaling;
                    if (0 <= p && p < regionPixels) {
                        if (ins.length >= (uint32_t) opts.indel_length) {
                            if (regionLen < 500000 && indelTextFits) {  // line and text
                                std::sprintf(indelChars, "%d", ins.length);
                                size_t sl = strlen(indelChars);
                                text_ins.emplace_back() = {SkTextBlob::MakeFromString(indelChars, fonts.overlay),
                                                           p + mm_textOffsetX,  // x
                                                           yScaledOffset + pH - textDrop, // y
                                                           yScaledOffset,  // box_y
                                                           fonts.textWidths[sl - 1]};

                            } else {  // line only
                                drawIns(canvas, Y, p, yScaling, xOffset, yOffset, theme.fcIns, rect, pH, ins_block_h, ins_block_w);
                            }
                        } else if (regionLen < opts.small_indel_threshold) {  // line only
                            drawIns(canvas, Y, p, yScaling, xOffset, yOffset, theme.fcIns, rect, pH, ins_block_h, ins_block_w);
                        }
                    }
                }
            }
            // add soft-clips
            if (!plotSoftClipAsBlock) {
                uint8_t *ptr_seq = bam_get_seq(a.delegate);
                uint8_t *ptr_qual = bam_get_qual(a.delegate);
                if (a.right_soft_clip > 0) {
                    int pos = (int) a.reference_end - regionBegin;
                    if (pos < regionLen && a.cov_end > regionBegin) {
                        const int opLen = (int) a.right_soft_clip;
                        for (int idx = l_qseq - opLen; idx < l_qseq; ++idx) {
                            const float p = pos * xScaling;
                            const uint8_t base = bam_seqi(ptr_seq, idx);
                            const uint8_t qual = ptr_qual[idx];
                            const int colorIdx = (l_qseq == 0) ? 10 : (qual > 10) ? 10 : qual;
                            rect.setXYWH(p + xOffset + mmPosOffset, yScaledOffset, xScaling * mmScaling, pH);
                            canvas->drawRect(rect, theme.BasePaints[base][colorIdx]);
                            if (mm_charFits) {
                                canvas->drawTextBlob(lookup_table_bam_textblobs[(int)base],
                                                   p + xOffset + mmPosOffset + mm_textOffsetX,
                                                   yScaledOffset + mm_textOffsetY,
                                                   theme.tcIns);
                            }
                            pos += 1;
                        }
                    }
                }
                if (a.left_soft_clip > 0) {
                    const int opLen = (int) a.left_soft_clip;
                    int pos = (int) a.pos - regionBegin - opLen;
                    for (int idx = 0; idx < opLen; ++idx) {
                        const float p = pos * xScaling;
                        const uint8_t base = bam_seqi(ptr_seq, idx);
                        const uint8_t qual = ptr_qual[idx];
                        const int colorIdx = (l_qseq == 0) ? 10 : (qual > 10) ? 10 : qual;
                        rect.setXYWH(p + xOffset + mmPosOffset, yScaledOffset, xScaling * mmScaling, pH);
                        canvas->drawRect(rect, theme.BasePaints[base][colorIdx]);
                        if (mm_charFits) {
                            canvas->drawTextBlob(lookup_table_bam_textblobs[(int)base],
                                               p + xOffset + mmPosOffset + mm_textOffsetX,
                                               yScaledOffset + mm_textOffsetY,
                                               theme.tcIns);
                        }
                        pos += 1;
                    }
                }
            }
            // Add modifications
            if (opts.parse_mods && regionLen <= opts.mod_threshold) {
                drawMods(canvas, rect, theme, cl.region, a, (float) width, xScaling, xOffset, mmPosOffset,
                         yScaledOffset, pH, l_qseq, monitorScale, regionLen <= 2000);
            }
        }
        // draw text deletions + insertions
        for (const auto &t : text_del) {
            canvas->drawTextBlob(t.text.get(), t.x + (monitorScale * 0.5), t.y, theme.tcDel);
        }
        for (const auto &t : text_ins) {
            rect.setXYWH(t.x - monitorScale, t.box_y, t.box_w, pH);  // middle
            canvas->drawRect(rect, theme.fcIns);
            rect.setXYWH(t.x - monitorScale - ins_block_h, t.box_y, t.box_w + ins_block_h + ins_block_h, ins_block_h);  // top
            canvas->drawRect(rect, theme.fcIns);
            rect.setXYWH(t.x - monitorScale - ins_block_h, t.box_y + pH - ins_block_h, t.box_w + ins_block_h + ins_block_h, ins_block_h);  // bottom
            canvas->drawRect(rect, theme.fcIns);
            canvas->drawTextBlob(t.text.get(), t.x, t.y + pH, theme.tcIns);
        }

        // draw connecting lines between linked alignments
        if (linkOp > 0) {
            if (!cl.linked.empty()) {
                const Segs::map_t &lm = cl.linked;
                SkPaint paint;
                const float offsety = (yScaling * 0.5) + cl.yOffset;
                for (auto const &keyVal: lm) {
                    const std::vector<Segs::Align *> &ind = keyVal.second;
                    const int size = (int) ind.size();
                    if (size > 1) {
                        const float max_x = cl.xOffset + (((float) cl.region->end - (float) cl.region->start) * cl.xScaling);

                        for (int jdx = 0; jdx < size - 1; ++jdx) {

                            const Segs::Align *segA = ind[jdx];
                            const Segs::Align *segB = ind[jdx + 1];

                            if (segA->y == -1 || segB->y == -1 || segA->blocks.empty() ||
                                segB->blocks.empty() ||
                                (segA->delegate->core.tid != segB->delegate->core.tid)) { continue; }

                            const long cstart = std::min(segA->blocks.front().end, segB->blocks.front().end);
                            const long cend = std::max(segA->blocks.back().start, segB->blocks.back().start);
                            double x_a = ((double) cstart - (double) cl.region->start) * cl.xScaling;
                            double x_b = ((double) cend - (double) cl.region->start) * cl.xScaling;

                            x_a = (x_a < 0) ? 0 : x_a;
                            x_b = (x_b < 0) ? 0 : x_b;
                            x_a += cl.xOffset;
                            x_b += cl.xOffset;
                            x_a = (x_a > max_x) ? max_x : x_a;
                            x_b = (x_b > max_x) ? max_x : x_b;

                            const float y = ((float) segA->y * yScaling) + offsety;

                            switch (segA->orient_pattern) {
                                case Segs::DEL:
                                    paint = theme.fcDel;
                                    break;
                                case Segs::DUP:
                                    paint = theme.fcDup;
                                    break;
                                case Segs::INV_F:
                                    paint = theme.fcInvF;
                                    break;
                                case Segs::INV_R:
                                    paint = theme.fcInvR;
                                    break;
                                default:
                                    paint = theme.fcNormal;
                                    break;
                            }
                            paint.setStyle(SkPaint::kStroke_Style);
                            paint.setStrokeWidth(monitorScale);
                            path.reset();
                            path.moveTo(x_a, y);
                            path.lineTo(x_b, y);
                            canvas->drawPath(path, paint);
                        }
                    }
                }
            }
        }

        // Draw data labels
        if (opts.data_labels && cl.regionIdx == 0) {
            if (cl.name.empty()) {
                std::filesystem::path fsp(bam_paths[cl.bamIdx]);
#if defined(_WIN32) || defined(_WIN64)
                const wchar_t* pc = fsp.filename().c_str();
                std::wstring ws(pc);
                std::string p(ws.begin(), ws.end());
                cl.name = p;
#else
                cl.name = fsp.filename();
#endif
            }
            if (!cl.name.empty()) {
                const char * name_s = cl.name.c_str();
                sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(name_s, fonts.overlay);
                const float text_width = fonts.overlay.measureText(name_s, cl.name.size(), SkTextEncoding::kUTF8);
                float ypos = cl.yOffset + monitorScale * 4;
                rect.setXYWH(cl.xOffset + monitorScale * 4, ypos, text_width + 8 * monitorScale, fonts.overlayHeight * 2);
                float rr = 2.5*monitorScale;
                canvas->drawRoundRect(rect, rr, rr, theme.bgPaint);
                canvas->drawRoundRect(rect, rr, rr, theme.lcLabel);
                canvas->drawTextBlob(blob, cl.xOffset + 8 * monitorScale, ypos + (fonts.overlayHeight * 1.33), theme.tcDel);
            }
        }
    }

    void drawRef(const Themes::IniOptions &opts,
                 std::vector<Utils::Region> &regions, const int fb_width,
                 SkCanvas *const canvas, const Themes::Fonts &fonts, const float refSpace, const float nRegions, const float gap, const float monitorScale,
                 const bool scale_bar) {
        if (regions.empty()) {
            return;
        }
        SkRect rect;
        SkPaint faceColor;
        const Themes::BaseTheme &theme = opts.theme;
        const double regionW = (double) fb_width / (double) regions.size();
        const double xPixels = regionW - gap - gap;
        const float textW = fonts.overlayWidth;
        const float minLetterSize = (textW > 0) ? ((float) fb_width / (float) regions.size()) / textW : 0;
        int index = 0;
        const double mmPosOffset = refSpace - fonts.overlayHeight - (gap*0.25);
        const float yp = refSpace - (gap*0.25);
        const float boxHeight = fonts.overlayHeight;
        for (auto &rgn: regions) {

            const int size = rgn.end - rgn.start;
            const double xScaling = xPixels / size;
            const char *ref = rgn.refSeq;
            if (ref == nullptr) {
                continue;
            }
            double mmScaling;
            if (size < 250) {
                mmScaling = 0.9 * xScaling;
            } else {
                mmScaling = 1 * xScaling;
            }
            double i = regionW * index;
            i += gap;
            if (textW > 0 && (float) size < minLetterSize && fonts.overlayHeight <= refSpace * 1.35) {
                const double v = (xScaling - textW) * 0.5;

                while (*ref) {
                    switch ((unsigned int) *ref) {
                        case 65:
                            faceColor = theme.fcA;
                            break;
                        case 67:
                            faceColor = theme.fcC;
                            break;
                        case 71:
                            faceColor = theme.fcG;
                            break;
                        case 78:
                            faceColor = theme.fcN;
                            break;
                        case 84:
                            faceColor = theme.fcT;
                            break;
                        case 97:
                            faceColor = theme.fcA;
                            break;
                        case 99:
                            faceColor = theme.fcC;
                            break;
                        case 103:
                            faceColor = theme.fcG;
                            break;
                        case 110:
                            faceColor = theme.fcN;
                            break;
                        case 116:
                            faceColor = theme.fcT;
                            break;
                    }
                    canvas->drawTextBlob(SkTextBlob::MakeFromText(ref, 1, fonts.overlay, SkTextEncoding::kUTF8),
                                         i + v, yp, faceColor);
                    i += xScaling;
                    ++ref;
                }
            } else if (size < 20000) {
                while (*ref) {
                    rect.setXYWH(i, mmPosOffset, mmScaling, boxHeight);
                    switch ((unsigned int) *ref) {
                        case 65:
                            canvas->drawRect(rect, theme.fcA);
                            break;
                        case 67:
                            canvas->drawRect(rect, theme.fcC);
                            break;
                        case 71:
                            canvas->drawRect(rect, theme.fcG);
                            break;
                        case 78:
                            canvas->drawRect(rect, theme.fcN);
                            break;
                        case 84:
                            canvas->drawRect(rect, theme.fcT);
                            break;
                        case 97:
                            canvas->drawRect(rect, theme.fcA);
                            break;
                        case 99:
                            canvas->drawRect(rect, theme.fcC);
                            break;
                        case 103:
                            canvas->drawRect(rect, theme.fcG);
                            break;
                        case 110:
                            canvas->drawRect(rect, theme.fcN);
                            break;
                        case 116:
                            canvas->drawRect(rect, theme.fcT);
                            break;
                    }
                    i += xScaling;
                    ++ref;
                }
            }
            index += 1;
        }
    }

    void drawBorders(const Themes::IniOptions &opts, const float fb_width, const float fb_height,
                     SkCanvas *const canvas, const size_t nRegions, const size_t nbams, const float trackY, const float covY, const int nTracks,
                     const float totalTabixY, const float refSpace, const float gap, const float totalCovY) {
        SkPath path;
        if (nRegions > 1) {
            float x = fb_width / nRegions;
            float step = x;
            path.reset();
            for (int i = 0; i < (int) nRegions - 1; ++i) {
                path.moveTo(x, 0);
                path.lineTo(x, fb_height);
                x += step;
            }
            canvas->drawPath(path, opts.theme.lcLightJoins);
        }
        if (nbams > 1) {
            float y = trackY + covY;
            float step = y;
            y += refSpace;
            path.reset();
            for (int i = 0; i < (int) nbams - 1; ++i) {
                path.moveTo(gap, y);
                path.lineTo(fb_width - gap, y);
                y += step;
            }
            canvas->drawPath(path, opts.theme.lcLightJoins);
        }
        if (nTracks > 0) {
            float y = totalCovY + refSpace + (trackY*(float)nbams) + (gap * 0.5);
            float step = totalTabixY / (float) nTracks;
            for (int i = 0; i < (int) nTracks; ++i) {
                path.reset();
                path.moveTo(gap, y);
                path.lineTo(fb_width - gap, y);
                if (i == 0) {
                    canvas->drawPath(path, opts.theme.lcJoins);
                } else {
                    canvas->drawPath(path, opts.theme.lcLightJoins);
                }
                y += step;
            }

        }
    }

    void drawLabel(const Themes::IniOptions &opts, SkCanvas *const canvas, SkRect &rect, Utils::Label &label, const Themes::Fonts &fonts,
              const ankerl::unordered_dense::set<std::string> &seenLabels, const std::vector<std::string> &srtLabels) {
        float pad = 2;
        std::string cur = label.current();
        if (cur.empty()) {
            return;
        }
        sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(cur.c_str(), fonts.overlay);
        float wl = fonts.overlayWidth * (cur.size() + 1);
        auto it = std::find(srtLabels.begin(), srtLabels.end(), cur);
        int idx;
        if (it != srtLabels.end()) {
            idx = it - srtLabels.begin();
        } else {
            idx = label.i + srtLabels.size();
        }
        float step, start;
        step = -1;
        start = 1;

        float value = start;
        for (int i = 0; i < idx; ++i) {
            value += step;
            step *= -1;
            step = step * 0.5;
        }
        SkRect bg;
        float x = rect.left() + pad;
        SkPaint p;
        int v;
        if (opts.theme.name == "igv") {
            v = 255 - (int) (value * 255);
        } else {
            v = (int) (value * 255);
        }
        p.setARGB(255, v, v, v);

        if ((wl + pad) > (rect.width() * 0.5)) {
            bg.setXYWH(x + pad, rect.bottom() - fonts.overlayHeight - pad - pad - pad - pad, fonts.overlayHeight,
                       fonts.overlayHeight);
            canvas->drawRoundRect(bg, fonts.overlayHeight, fonts.overlayHeight, p);
            canvas->drawRoundRect(bg, fonts.overlayHeight, fonts.overlayHeight, opts.theme.lcLabel);
        } else {
            bg.setXYWH(x + pad, rect.bottom() - fonts.overlayHeight - pad - pad - pad - pad, wl + pad,
                       fonts.overlayHeight + pad + pad);
            canvas->drawRoundRect(bg, 5, 5, p);
            canvas->drawRoundRect(bg, 5, 5, opts.theme.lcLabel);

            if (opts.theme.name == "igv") {
                if (v == 0) {
                    canvas->drawTextBlob(blob, x + pad + pad, bg.bottom() - pad - pad, opts.theme.tcBackground);
                } else {
                    canvas->drawTextBlob(blob, x + pad + pad, bg.bottom() - pad - pad, opts.theme.tcDel);
                }
            } else {
                if (v == 255) {
                    canvas->drawTextBlob(blob, x + pad + pad, bg.bottom() - pad - pad, opts.theme.tcBackground);
                } else {
                    canvas->drawTextBlob(blob, x + pad + pad, bg.bottom() - pad - pad, opts.theme.tcDel);
                }
            }
        }

        if (label.i != label.ori_i) {

            canvas->drawRect(rect, opts.theme.lcJoins);
        }
        if (!label.comment.empty()) {
            bg.setXYWH(rect.right() - fonts.overlayHeight - (4*pad), rect.bottom() - fonts.overlayHeight - pad - pad - pad - pad, fonts.overlayHeight,
                       fonts.overlayHeight);
            canvas->drawRoundRect(bg, fonts.overlayHeight, fonts.overlayHeight, opts.theme.fcG);
        }
    }

    void drawTrackBigWig(HGW::GwTrack &trk, const Utils::Region &rgn, SkRect &rect, const float padX, const float padY,
                         const float y, const float stepX, const float stepY, const float gap, const float gap2, const float xScaling, const float t,
                         const Themes::IniOptions &opts, SkCanvas *const canvas, const Themes::Fonts &fonts, const SkPaint &faceColour) {
        if (trk.bigWig_intervals == nullptr || trk.bigWig_intervals->l == 0) {
            return;
        }
        // see chr19:5,930,464-5,931,225 in test/test_fixedStep.bigwig
        float cMax = std::numeric_limits<float>::min();
        float cMin = std::numeric_limits<float>::max();
        float v;
        int length = (int) trk.bigWig_intervals->l;
        for (int i = 0; i < length; ++i) {
            v = trk.bigWig_intervals->value[i];
            cMin = std::fmin(v, cMin);
            cMax = std::fmax(v, cMax);
        }
        float range = std::fmax(cMax, 0) - std::fmin(cMin, 0);
        float availableSPace = stepY - gap2;
        float y_negativeValueOffset;
        if (cMin < 0) {
            y_negativeValueOffset = availableSPace * 0.5;
            range = std::fmax(std::fabs(cMin), cMax) * 2;
        } else {
            y_negativeValueOffset = 0;
        }
        // normalize to space available
        for (int i = 0; i < length; ++i) {
            float &vi = trk.bigWig_intervals->value[i];
            vi = vi / range;
        }

        float startY = y + availableSPace + (gap * 0.5);
        float x = padX;
//        if (y_negativeValueOffset != 0) {
        SkPath path;
        path.moveTo(padX, startY - y_negativeValueOffset);
        path.lineTo(padX + stepX - gap2, startY - y_negativeValueOffset);
        canvas->drawPath(path, faceColour);
//        }
        int step = length / 100000;
        step = (step) ? step : 1;
        for (int i = 0; i < length; i += step) {
            if ((int)trk.bigWig_intervals->start[i] < rgn.start) {
                continue;
            } else if ((int)trk.bigWig_intervals->start[i] >= rgn.end) {
                break;
            }
            v = trk.bigWig_intervals->value[i];
            x = padX + (((float)trk.bigWig_intervals->start[i] - (float)rgn.start) * xScaling);
            rect.setXYWH(x, startY - y_negativeValueOffset, std::fmax(1, xScaling), -v * availableSPace);
            canvas->drawRect(rect, faceColour);
        }
        if (availableSPace > 2 * fonts.overlayHeight) {
            path.reset();
            path.moveTo(padX, startY - availableSPace);
            path.lineTo(padX + 20, startY - availableSPace);
            path.moveTo(padX, startY);
            path.lineTo(padX + 20, startY);
            canvas->drawPath(path, opts.theme.lcJoins);

            std::string str = std::to_string(cMax);
            str.erase(str.find_last_not_of('0') + 1, std::string::npos);
            str.erase(str.find_last_not_of('.') + 1, std::string::npos);
            sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(str.c_str(), fonts.overlay);
            canvas->drawTextBlob(blob, padX + 25, startY - availableSPace + fonts.overlayHeight, opts.theme.tcDel);
            str = std::to_string(cMin);
            str.erase(str.find_last_not_of('0') + 1, std::string::npos);
            str.erase(str.find_last_not_of('.') + 1, std::string::npos);
            blob = SkTextBlob::MakeFromString(str.c_str(), fonts.overlay);
            canvas->drawTextBlob(blob, padX + 25, startY, opts.theme.tcDel);

        }
    }

    void drawTrackBlock(int start, int stop, std::string &rid, const Utils::Region &rgn, SkRect &rect, SkPath &path,
                        const float padX, const float padY,
                        const float y, const float h, const float stepX, const float stepY, const float gap, const float gap2, const float xScaling,
                        Themes::IniOptions &opts, SkCanvas *const canvas, const Themes::Fonts &fonts,
                        const bool add_text, const bool add_rect, const bool shaded, float *const labelsEnd, std::string &vartype,
                        const float monitorScale, std::vector<TextItem> &text, bool addArc, const bool isRoi, SkPaint &faceColour, const float pointSlop, const float strand) {
        float x = 0;
        float w;

        SkPaint arcColour;
        SkPaint *faceColour2 = &faceColour;

        if (!vartype.empty()) {
            if (vartype == "DEL") {
                if (addArc) {
                    arcColour = opts.theme.fcT;
                } else {
                    faceColour2 = &opts.theme.fcT;
                    faceColour2->setAlpha(150);
                }

            } else if (vartype == "DUP") {
                if (addArc) {
                    arcColour = opts.theme.fcC;
                } else {
                    faceColour2 = &opts.theme.fcC;
                    faceColour2->setAlpha(150);
                }
            } else if (vartype == "INV") {
                if (addArc) {
                    arcColour = opts.theme.fcA;
                } else {
                    faceColour2 = &opts.theme.fcA;
                    faceColour2->setAlpha(150);
                }
            } else {
                addArc = false;
            }
        } else {
            addArc = false;
        }

        x = (float) (start - rgn.start) * xScaling;
        w = std::fmax(monitorScale, (float) (stop - start) * xScaling);
        rect.setXYWH(x + padX, (int)y + padY, w, h);

        if (addArc) {
            arcColour.setStyle(SkPaint::kStroke_Style);
            arcColour.setStrokeWidth(monitorScale);
            arcColour.setAntiAlias(true);

            int rLen = rgn.end - rgn.start;
            float leftY;
            bool closeLeft, closeRight;
            if (rgn.start - start < -rLen) {
                leftY = y + padY + stepY;
                closeLeft = false;
            } else {
                leftY = rect.top();
                closeLeft = true;
            }
            if (stop - rgn.end > rLen) {
                closeRight = false;
            } else {
                closeRight = true;
            }

            float virtual_left = ((float) (start - rgn.start) * xScaling) + padX;
            float virtual_right = ((float) (stop - rgn.start) * xScaling) + padX;
            float arcTop;
            SkPath arc;
            arc.moveTo(x + padX, y + padY);
            arc.lineTo(x + padX, y + h + padY);
            arc.moveTo(virtual_left, leftY);
            if (closeRight || closeLeft) {
                arcTop = y + h + padY + h;
                arc.quadTo(((virtual_right - virtual_left) / 2) + virtual_left, arcTop,  virtual_right, leftY);
            } else {
                arc.moveTo(virtual_left, leftY);
                arc.lineTo(virtual_right, leftY);
            }
            arc.moveTo(virtual_right, y + padY);
            arc.lineTo(virtual_right, y + h + padY);
            canvas->drawPath(arc, arcColour);

        } else if (add_rect) {
            if (!shaded) {
                if (strand == 1) {  // +
                    drawRightPointedRectangleNoEdge(canvas, h, y + padY, x + padX, w, 0, opts.theme.lcJoins, path, pointSlop);
                } else if (strand == 2) {  // -
                    drawLeftPointedRectangleNoEdge(canvas, h, y + padY, x + padX, w, 0, opts.theme.lcJoins, path, pointSlop);
                }
                if (faceColour2->getStyle() != SkPaint::kStroke_Style) {
                    canvas->drawRect(rect, *faceColour2);
                }

            } else {
                if (strand == 1) {  // +
                    drawRightPointedRectangleNoEdge(canvas, h, y + padY, x + padX, w, 0, *faceColour2, path, pointSlop);
                } else if (strand == 2) {  // -
                    drawLeftPointedRectangleNoEdge(canvas, h, y + padY, x + padX, w, 0, *faceColour2, path, pointSlop);
                }
            }
        }

        if (!add_text) {
            return;
        }
        float spaceRemaining = stepY;// - h - h;
        if (spaceRemaining < fonts.overlayHeight) {
            return;
        }
        float estimatedTextWidth = (float) rid.size() * fonts.overlayWidth;
        if (estimatedTextWidth > stepX - gap2) {
            return;
        }
        float halfInterval = estimatedTextWidth / 2;
        float midPoint = rect.right() - ((rect.right() - rect.left()) / 2);
        float leftPoint = midPoint - halfInterval;
        if (leftPoint < padX) {
            leftPoint = padX;
        }
        if (leftPoint < *labelsEnd) {
            return;
        }
        float rightPoint = midPoint + halfInterval;
        if (rightPoint > padX + stepX - gap2) {
            return;
        }

        *labelsEnd = leftPoint + estimatedTextWidth;
        rect.fLeft = leftPoint;
        text.emplace_back() = {SkTextBlob::MakeFromString(rid.c_str(), fonts.overlay),
                               rect.left(),
                               rect.bottom() + fonts.overlayHeight + monitorScale * 2};

    }

    void drawGappedTrackBlock(Themes::IniOptions &opts, const float fb_width, const float fb_height,
                              SkCanvas *const canvas, const float totalTabixY, const float tabixY, std::vector<HGW::GwTrack> &tracks,
                              const std::vector<Utils::Region> &regions, const Themes::Fonts &fonts,
                              const float gap, Utils::TrackBlock &trk, const bool any_text, const Utils::Region &rgn, SkRect &rect,
                              SkPath &path, SkPath &path2, const float padX, const float padY, const float stepX, const float stepY,
                              const float y, const float h, const float h2, const float h4, const float gap2, const float xScaling, const int nLevels,
                              float *const labelsEnd, std::vector<TextItem> &text, SkPaint &faceColour, SkPaint &shadedFaceColour,
                              const float pointSlop, const int strand, const float monitorScale) {

        int target = (int) trk.s.size();
        float screenLeftEdge = padX;
        float screenRightEdge = padX + (((float) rgn.end - (float) rgn.start) * xScaling);
        std::string empty_str;
        if (any_text) {
            drawTrackBlock(trk.start, trk.end, trk.name, rgn, rect, path, padX, padY, y, h, stepX, stepY, gap, gap2,
                           xScaling, opts, canvas, fonts, true, false, false, labelsEnd, empty_str, monitorScale, text, false, false, faceColour, pointSlop, strand);
        }
        float x, yy, w;
        int s, e;
        for (int i = 0; i < target; ++i) {  // draw lines first and thickness == 1
            s = trk.s[i];
            e = trk.e[i];
            if (e < rgn.start) {
                continue;
            }
            yy = y + padY + (h / 2);
            if (i == 0) {
                continue;
            }
            int lastEnd = (i > 0) ? trk.e[i <- 1] : trk.start;
            if (lastEnd < s) {  // add arrows
                x = std::max(((float) (lastEnd - rgn.start) * xScaling) + padX, (float) screenLeftEdge);
                w = std::min(((float) (s - rgn.start) * xScaling) + padX, (float) screenRightEdge);
                if (w > x) {
                    path2.reset();
                    path2.moveTo(x, yy);
                    path2.lineTo(w, yy);
                    canvas->drawPath(path2, opts.theme.lcGTFJoins);
//                    if (stranded != 0 && w - x > 50) {
//                        while (x + 50 < w) {
//                            x += 50;
//                            path2.reset();
//                            if (stranded == 1) {
//                                path2.moveTo(x, yy);
//                                path2.lineTo(x - 6, yy + 6);
//                                path2.moveTo(x, yy);
//                                path2.lineTo(x - 6, yy - 6);
//                            } else {
//                                path2.moveTo(x, yy);
//                                path2.lineTo(x + 6, yy + 6);
//                                path2.moveTo(x, yy);
//                                path2.lineTo(x + 6, yy - 6);
//                            }
//                            canvas->drawPath(path2, opts.theme.lcJoins);
//                        }
//                    }
                }
            }
        }
        for (int i = 0; i < target; ++i) {
            s = trk.s[i];
            e = trk.e[i];
            if (e < rgn.start) {
                continue;
            }
            assert (i < trk.drawThickness.size());
            uint8_t thickness = trk.drawThickness[i];
//            if (trk.name == "DDX11L1") {
//                std::cerr << " " << trk.name << " " << (int)thickness << " " << s << "-" << e << std::endl;
//            }

            if (s < rgn.end && e > rgn.start) {
                int left_cds = std::min(trk.coding_start, trk.coding_end);
                int right_cds = std::max(trk.coding_start, trk.coding_end);
//                if (trk.name == "DDX11L1") {
//                    std::cerr << (s < right_cds && e > right_cds) << " " << (s < left_cds && e > left_cds) << std::endl;
//                }
                if (s < right_cds && e > right_cds) { //overlaps, split into two blocks!
                    drawTrackBlock(right_cds, e, trk.name, rgn, rect, path, padX, padY, y + (h * 0.25), h * 0.5, stepX, stepY, gap,
                                   gap2, xScaling, opts, canvas, fonts, false, true, true, labelsEnd, empty_str, 0, text, false, false,  shadedFaceColour, pointSlop / 2, strand);
                    drawTrackBlock(s, right_cds, trk.name, rgn, rect, path, padX, padY, y, h, stepX, stepY, gap,
                                   gap2, xScaling, opts, canvas, fonts, false, true, false, labelsEnd, empty_str, 0, text, false, false, faceColour, pointSlop, strand);
                    continue;
                } else if (s < left_cds && e > left_cds) {
                    drawTrackBlock(s, left_cds, trk.name, rgn, rect, path, padX, padY, y + (h * 0.25), h * 0.5, stepX, stepY, gap,
                                   gap2, xScaling, opts, canvas, fonts, false, true, true, labelsEnd, empty_str, 0, text, false, false,  shadedFaceColour, pointSlop / 2, strand);
                    drawTrackBlock(left_cds, e, trk.name, rgn, rect, path, padX, padY, y, h, stepX, stepY, gap,
                                   gap2, xScaling, opts, canvas, fonts, false, true, false, labelsEnd, empty_str, 0, text, false, false, faceColour, pointSlop, strand);
                    continue;
                }

                if (thickness <= 1) {
                    drawTrackBlock(s, e, trk.name, rgn, rect, path, padX, padY, y + (h * 0.25), h * 0.5, stepX, stepY, gap, gap2, xScaling,
                                   opts, canvas, fonts, false, true, true, labelsEnd, empty_str, 0, text, false, false, shadedFaceColour, pointSlop / 2, strand);
                } else if (thickness == 2) {
                    drawTrackBlock(s, e, trk.name, rgn, rect, path, padX, padY, y, h, stepX, stepY, gap, gap2, xScaling,
                                   opts, canvas, fonts, false, true, false, labelsEnd, empty_str, 0, text, false, false, faceColour, pointSlop, strand);
                }
                else if (thickness == 3) {
                    drawTrackBlock(s, e, trk.name, rgn, rect, path, padX, padY, y, h, stepX, stepY, gap, gap2, xScaling,
                                   opts, canvas, fonts, false, true, false, labelsEnd, empty_str, 0, text, false, false, opts.theme.ecSplit, pointSlop, strand);
                }
            }

        }
    }

    void drawTracks(Themes::IniOptions &opts, const float fb_width, const float fb_height,
                    SkCanvas *const canvas, const float totalTabixY, const float tabixY, std::vector<HGW::GwTrack> &tracks,
                    std::vector<Utils::Region> &regions, const Themes::Fonts &fonts, const float gap, const float monitorScale, const float sliderSpace) {

        // All tracks are converted to TrackBlocks and then drawn
        if (tracks.empty() || regions.empty() || tabixY <= 0) {
            return;
        }
        float gap2 = 2 * gap;
        float padX = gap;

        float stepX = fb_width / (float) regions.size();
        float stepY = tabixY;
        stepY -= sliderSpace;

        float y = fb_height - totalTabixY - sliderSpace; // + gap;  // start of tracks on canvas
        float t = (float) 0.005 * fb_width;

        SkRect rect{};
        SkPath path{};
        SkPath path2{};


        bool expanded = opts.expand_tracks;

        int regionIdx = 0;
        for (auto &rgn: regions) {
            bool any_text = true;
            float xScaling = (stepX - gap2) / (float) (rgn.end - rgn.start);
            float padY = gap;
            int trackIdx = 0;

            rgn.featuresInView.clear();
            rgn.featuresInView.resize(tracks.size());
            rgn.featureLevels.clear();
            rgn.featureLevels.resize(tracks.size());
            for (auto &trk: tracks) {

                SkPaint &faceColour = trk.faceColour;
                SkPaint &shadedFaceColour = trk.shadedFaceColour;

                float right = ((float) (rgn.end - rgn.start) * xScaling) + padX;
                canvas->save();
//                canvas->clipRect({padX, y + padY, right, y + padY + stepY}, false);
                canvas->clipRect({padX, y + padY, right, fb_height}, false);

                trk.fetch(&rgn);
                if (trk.kind == HGW::BIGWIG) {
                    drawTrackBigWig(trk, rgn, rect, padX, padY, y + (stepY * trackIdx), stepX, stepY, gap, gap2,
                                    xScaling, t, opts, canvas, fonts, faceColour);
                    trackIdx += 1;
                    canvas->restore();
                    continue;
                }

                std::vector<TextItem> text;

                bool isGFF = trk.kind == HGW::GFF3_NOI || trk.kind == HGW::GFF3_IDX || trk.kind == HGW::GTF_NOI ||
                             trk.kind == HGW::GTF_IDX;

                std::vector<Utils::TrackBlock> &features = rgn.featuresInView[trackIdx];
                features.clear();
                if (isGFF) {
                    HGW::collectGFFTrackData(trk, features);
                } else {
                    HGW::collectTrackData(trk, features);
                }

                int nLevels = Segs::findTrackY(features, expanded, rgn);
                rgn.featureLevels[trackIdx] = nLevels;
                std::vector<float> labelsEndLevels(nLevels, 0);

                float blockStep = ((stepY) / (float) nLevels);
                float blockSpace = blockStep * 0.35;
                float h = std::fmin(blockSpace, 10 * monitorScale);

                float h2 = h * 0.5;
                float h4 = h2 * 0.5;

                float pointSlop = (tan(0.6) * (h2));
                float step_track = (tabixY) / ((float) nLevels);

                bool isBed12 = !trk.parts.empty() && trk.parts.size() >= 12;
                float textLevelEnd = 0;  // makes sure text doesnt overlap on same level

                for (auto &f: features) {
                    float padY_track = padY + (step_track * f.level) + (step_track * 0.5) - (fonts.overlayHeight);

                    float *fLevelEnd = (nLevels > 1) ? &labelsEndLevels[f.level] : &textLevelEnd;
                    int strand = f.strand;
                    if (isGFF || isBed12) {
                        if (!f.anyToDraw || f.start > rgn.end || f.end < rgn.start) {
                            continue;
                        }
                        drawGappedTrackBlock(opts, fb_width, fb_height, canvas, totalTabixY, tabixY, tracks, regions,
                                             fonts, gap,
                                             f, any_text, rgn, rect, path, path2, padX, padY_track, stepX, step_track,
                                             y, h, h2, h4, gap2,
                                             xScaling, nLevels, fLevelEnd, text, faceColour, shadedFaceColour, pointSlop, strand, monitorScale);

                    } else {
                        drawTrackBlock(f.start, f.end, f.name, rgn, rect, path, padX, padY_track, y, h, stepX, stepY,
                                       gap, gap2,
                                       xScaling, opts, canvas, fonts, any_text, true, false, fLevelEnd, f.vartype, monitorScale,
                                       text, opts.sv_arcs, trk.kind == HGW::FType::ROI, faceColour, pointSlop, strand);
                    }
                }

                if (fonts.overlayHeight * nLevels < stepY && features.size() < 500) {
                    for (const auto&t: text) {
                        canvas->drawTextBlob(t.text, t.x, t.y, opts.theme.tcDel);
                    }
                }

                // Draw data labels
                if (opts.data_labels && regionIdx == 0) {

                    std::filesystem::path fsp(trk.path);
#if defined(_WIN32) || defined(_WIN64)
                    const wchar_t* pc = fsp.filename().c_str();
                    std::wstring ws(pc);
                    std::string name(ws.begin(), ws.end());
#else
                    std::string name = fsp.filename();
#endif

                    const char * name_s = name.c_str();
                    sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(name_s, fonts.overlay);
                    float text_width = fonts.measureTextWidth(name_s);
                    float rr = 2.5*monitorScale;
                    rect.setXYWH(padX + monitorScale, y + padY + monitorScale,
                                 text_width + 8 * monitorScale + 8 * monitorScale, fonts.overlayHeight * 2);
                    canvas->drawRoundRect(rect, rr, rr, opts.theme.bgPaint);
                    canvas->drawRoundRect(rect, rr, rr, opts.theme.lcLabel);
                    canvas->drawTextBlob(blob, padX + 8 * monitorScale,
                                         y + padY + fonts.overlayHeight * 1.5, opts.theme.tcDel);

                }

                trackIdx += 1;
                padY += tabixY;

                canvas->restore();
            }
            padX += stepX;
            regionIdx += 1;
        }
        opts.theme.lcLightJoins.setAntiAlias(false);

    }


    std::string floatToStringCommas(double num, int precision=1) {
        std::stringstream stream;
        std::string s;
        int n;
        if ((float)((int)num) == num) {
            precision = 0;
        }
        if (precision > 0) {
            stream << std::fixed << std::setprecision(precision) << num;
            s = stream.str();
            size_t dotPos = s.find('.');
            if (dotPos != std::string::npos) {
                n = dotPos - 3;
            } else {
                n = s.size() - 3;
            }
        } else {
            s = std::to_string(int(num));
            n = s.size() - 3;
        }
        int end = (num >= 0) ? 0 : 1;
        while (n > end) {
            s.insert(n, ",");
            n -= 3;
        }
        return s;
    }

    void posToText(int num, int regionLen, std::string &a) {
        assert (num >= 0);
        if (num == 0) {
            a = "0";
            return;
        }
        int rounding = std::ceil(std::log10(regionLen));
        double d;
        switch (rounding) {
            case 0:
            case 1:
            case 2:
                a = floatToStringCommas(num, 0);
                a += " bp";
                break;
            case 3:
            case 4:
                d = (double)num / 1e3;
                d = std::floor(d * 10) / 10;
                a = floatToStringCommas((float)d, 1);
                a += " kb";
                break;
            case 5:
                d = (double)num / 1e3;
                d = std::floor(d * 10) / 10;
                a = Term::intToStringCommas((int)d - 1);
                a += " kb";
                break;
            case 6:
                d = (double)num / 1e6;
                d = std::floor(d * 10) / 10;
                a = Utils::removeZeros((float)d);
                a += " mb";
                break;
            default:
                d = (double)num / 1e6;
                d = std::ceil(d * 10) / 10;
                a = floatToStringCommas(d);
                a += " mb";
                break;
        }
    }

    double NiceNumber (const double Value, const int Round) {  // https://stackoverflow.com/questions/4947682/intelligently-calculating-chart-tick-positions
        int    Exponent;
        double Fraction;
        double NiceFraction;
        Exponent = (int) floor(log10(Value));
        Fraction = Value/pow(10, (double)Exponent);
        if (Round) {
            if (Fraction < 1.5)
                NiceFraction = 1.0;
            else if (Fraction < 3.0)
                NiceFraction = 2.0;
            else if (Fraction < 7.0)
                NiceFraction = 5.0;
            else
                NiceFraction = 10.0;
        }
        else {
            if (Fraction <= 1.0)
                NiceFraction = 1.0;
            else if (Fraction <= 2.0)
                NiceFraction = 2.0;
            else if (Fraction <= 5.0)
                NiceFraction = 5.0;
            else
                NiceFraction = 10.0;
        }

        return NiceFraction*pow(10, (double)Exponent);
    }

    int calculateInterval(int regionLen) {
        assert (regionLen > 0);
        int interval = std::pow(10, std::floor(std::log10(regionLen)) - 1);
        return interval;
    }

    // draw scale bar and ideogram
    void drawChromLocation(const Themes::IniOptions &opts,
                           const Themes::Fonts &fonts,
                           std::vector<Utils::Region> &regions,
                           const std::unordered_map<std::string, std::vector<Ideo::Band>> &ideogram,
                           SkCanvas *const canvas,
                           const float fb_width, const float fb_height, const float monitorScale,
                           const float plot_gap, const bool addLocation, const float sliderSpace) {

        SkPaint paint, light_paint, line;
//        paint.setARGB(255, 240, 32, 73);
        paint.setARGB(255, 220, 32, 73);

        paint.setStrokeWidth(monitorScale * 2);
        paint.setStyle(SkPaint::kStroke_Style);

        light_paint = opts.theme.lcLightJoins;

        line.setColor((opts.theme_str == "dark") ? SK_ColorGRAY : SK_ColorBLACK);
        line.setStrokeWidth(monitorScale);
        line.setStyle(SkPaint::kStroke_Style);
        SkRect rect{};
        SkRect clip{};
        SkPath path{};

        // Note sliderSpace = plot_gap * 4;
        // Region box
        float box_height = plot_gap * 2;
        float box_top = fb_height - (plot_gap * 3 );
        // Ideogram box
        float ideo_box_height = plot_gap;
        float ideo_box_top = fb_height - (plot_gap * 2.5);
        float horizontal_line = ideo_box_top + (ideo_box_height / 2);;

        float yh = std::fmax((float) (fb_height * 0.0175), 10 * monitorScale);

        float colWidth = (float) fb_width / (float) regions.size();


        float chrom_name_text_bottom = fb_height - plot_gap - plot_gap + (fonts.overlayHeight / 2);
        float regionIdx = 0;
        for (auto& region: regions) {

            sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(region.chrom.c_str(), fonts.overlay);
            canvas->drawTextBlob(blob, (regionIdx * colWidth) + plot_gap, chrom_name_text_bottom, opts.theme.tcDel);

            region.chromNameWidth = fonts.measureTextWidth(region.chrom);

            float h_gap = region.chromNameWidth + plot_gap + plot_gap;

            const float drawWidth = colWidth - h_gap - plot_gap - plot_gap;  // Ideogram
            const float scaleWidth = colWidth - plot_gap - plot_gap;

            if (drawWidth < 0) {
                region.ideogramStart = -1;
                region.ideogramEnd = -1;
                return;
            }

            float s = (float)region.start / (float)region.chromLen;
            float e = (float)region.end / (float)region.chromLen;
            float w = (e - s) * drawWidth;
            if (w < 3) {
                w = 3;
            }
            float xp = (regionIdx * colWidth) + h_gap;

            region.ideogramStart = xp;
            region.ideogramEnd = xp + drawWidth;

            if (addLocation || !ideogram.empty()) {
                path.reset();
                path.moveTo(region.ideogramStart, horizontal_line );
                path.lineTo(region.ideogramEnd, horizontal_line);
                canvas->drawPath(path, line);

                auto it = ideogram.find(region.chrom);
                if (it != ideogram.end()) {
                    const std::vector<Ideo::Band>& bands = it->second;
                    for (const auto& b : bands) {
                        float sb = (float) b.start / (float)region.chromLen;
                        float eb = (float) b.end / (float)region.chromLen;
                        float wb = (eb - sb) * drawWidth;
                        rect.setXYWH(xp + (sb * drawWidth),
                                     ideo_box_top,
                                     wb,
                                     ideo_box_height);
                        canvas->drawRect(rect, b.paint);
                        if (wb > 2) {
                            canvas->drawRect(rect, light_paint);
                        }
                    }
                }
            }
            if (addLocation) {
                rect.setXYWH(xp + (s * drawWidth),
                             box_top,
                             w,
                             box_height);
                canvas->drawRect(rect, paint);
            }

            // draw scale bar at top
            if (opts.scale_bar) {

                float top2 = fonts.overlayHeight + plot_gap;

                double nice_range = NiceNumber((double)region.regionLen, 0);
                double nice_tick = NiceNumber(nice_range/(10 - 1), 1) * 2;
                if (nice_tick < 1) {
                    continue;
                }

                canvas->save();
                clip.setXYWH(plot_gap + (regionIdx * colWidth), 1, scaleWidth, fonts.overlayHeight * 2 + plot_gap);
                canvas->clipRect(clip, SkClipOp::kIntersect );

                int position = (region.start / (int)nice_tick) * (int)nice_tick;
                int num_divisions = nice_range / nice_tick;

                std::string text;
                std::string last;
                SkPath path;
                double xScaling = (scaleWidth / (double)region.regionLen);

                float xOffset = plot_gap + (colWidth * regionIdx);
                float last_x = -1;

                for (int i = 0; i <= num_divisions; ++i) {

                    float x_pos = xOffset + ((position - region.start) * xScaling) - (xScaling*0.5);
                    if (position == 0) {
                        last_x = x_pos;
                        position += nice_tick;
                        continue;
                    }
                    last_x = last_x + ((x_pos - last_x) * 0.5);

                    if (last_x > 0) {
                        path.moveTo(last_x, top2 + yh*0.2);
                        path.lineTo(last_x, top2 + yh*0.70);
                        canvas->drawPath(path, opts.theme.lcJoins);
                    }

                    path.moveTo(x_pos, top2 + yh*0.2);
                    path.lineTo(x_pos, top2 + yh*0.70);
                    canvas->drawPath(path, opts.theme.lcJoins);

                    last_x = x_pos;

                    posToText(position, region.regionLen, text);

                    sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(text.c_str(), fonts.overlay);
                    float text_width = fonts.overlay.measureText(text.c_str(), text.length(), SkTextEncoding::kUTF8);
                    float t_half = text_width * 0.5;
                    canvas->drawTextBlob(blob, x_pos - t_half, top2, opts.theme.tcDel);
                    last = text;
                    position += nice_tick;
                }
                canvas->restore();

            }
            regionIdx += 1;
        }
    }

} // Drawing