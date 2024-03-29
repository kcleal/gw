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
#include "../include/BS_thread_pool.h"
#include "../include/unordered_dense.h"
#include "hts_funcs.h"
#include "drawing.h"


namespace Drawing {

    char indelChars[50];
    constexpr float polygonHeight = 0.85;

    struct TextItem{
        sk_sp<SkTextBlob> text;
        float x, y;
    };

    void drawCoverage(const Themes::IniOptions &opts, std::vector<Segs::ReadCollection> &collections,
                      SkCanvas *canvas, const Themes::Fonts &fonts, const float covYh, const float refSpace) {

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
        int last_bamIdx = 0;
        float yOffsetAll = refSpace;

        for (auto &cl: collections) {
            if (cl.skipDrawingReads && cl.skipDrawingCoverage) {
                continue;
            }
            cl.skipDrawingCoverage = true;
            if (cl.region->markerPos != -1) {
                float rp = refSpace + 6 + (cl.bamIdx * cl.yPixels);
                float xp = refSpace * 0.2;
                float markerP = (cl.xScaling * (float) (cl.region->markerPos - cl.region->start)) + cl.xOffset;
                if (markerP > cl.xOffset && markerP < cl.regionPixels - cl.xOffset) {
                    path.reset();
                    path.moveTo(markerP, rp);
                    path.lineTo(markerP - xp, rp);
                    path.lineTo(markerP, rp + (refSpace*0.7));
                    path.lineTo(markerP + xp, rp);
                    path.lineTo(markerP, rp);
                    canvas->drawPath(path, theme.marker_paint);
                }
                float markerP2 = (cl.xScaling * (float) (cl.region->markerPosEnd - cl.region->start)) + cl.xOffset;
                if (markerP2 > cl.xOffset && markerP2 < (cl.regionPixels + cl.xOffset)) {
                    path.reset();
                    path.moveTo(markerP2, rp);
                    path.lineTo(markerP2 - xp, rp);
                    path.lineTo(markerP2, rp + (refSpace*0.7));
                    path.lineTo(markerP2 + xp, rp);
                    path.lineTo(markerP2, rp);
                    canvas->drawPath(path, theme.marker_paint);
                }
            }

            if (cl.covArr.empty() || cl.readQueue.empty()) {
                continue;
            }
            if (cl.bamIdx != last_bamIdx) {
                yOffsetAll += cl.yPixels;
            }
            float xScaling = cl.xScaling;
            float xOffset = cl.xOffset;
            bool draw_mismatch_info = (opts.snp_threshold > (int) cl.covArr.size()) && !cl.mmVector.empty();
            bool draw_reference_info = (draw_mismatch_info && opts.soft_clip_threshold > (int) cl.covArr.size());
            std::vector<Segs::Mismatches> &mmVector = cl.mmVector;
            // find mismatches in reads out of view
            if (draw_mismatch_info && !cl.collection_processed) {
                Segs::findMismatches(opts, cl);
            }

            double tot, mean, n;
            const std::vector<int> &covArr_r = cl.covArr;
            std::vector<float> c;
            c.resize(cl.covArr.size());
            c[0] = (float) cl.covArr[0];
            int cMaxi = (c[0] > 10) ? (int) c[0] : 10;
            tot = (float) c[0];
            n = 0;
            if (tot > 0) {
                n += 1;
            }
            float cMax;
            bool processThis = draw_mismatch_info && !cl.collection_processed;
            for (size_t i = 1; i < c.size(); ++i) { // cum sum
                c[i] = ((float) covArr_r[i]) + c[i - 1];
                if (c[i] > cMaxi) {
                    cMaxi = (int) c[i];
                }
                if (c[i] > 0) {
                    tot += c[i];
                    n += 1;
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
            if (n > 0) {
                mean = tot / n;
                mean = ((float) ((int) (mean * 10))) / 10;
            } else {
                mean = 0;
            }

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
                    width = (mmVector.size() < 500000) ? ((1. > mms) ? 1. : mms) : xScaling;
                } else {
                    width = xScaling;
                }

                int i = 0;
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
                        if (mm_h < 0.001 || (cum_h / (covY - c[i]) < 0.2)) {
                            i += 1;
                            continue;
                        }
                        const SkPaint *paint_ref;
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

            std::sprintf(indelChars, "%d", cMaxi);

            sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(indelChars, fonts.overlay);
            canvas->drawTextBlob(blob, xOffset + 25, covY_f + yOffsetAll + 10, theme.tcDel);
            path.reset();
            path.moveTo(xOffset, covY_f + yOffsetAll);
            path.lineTo(xOffset + 20, covY_f + yOffsetAll);
            path.moveTo(xOffset, covY + yOffsetAll);
            path.lineTo(xOffset + 20, covY + yOffsetAll);
            canvas->drawPath(path, theme.lcJoins);

            char *ap = indelChars;
            ap += std::sprintf(indelChars, "%s", "avg. ");
            std::sprintf(ap, "%.1f", mean);

            if (((covY * 0.5) + yOffsetAll + 10 - fonts.overlayHeight) - (covY_f + yOffsetAll + 10) >
                0) { // dont overlap text
                blob = SkTextBlob::MakeFromString(indelChars, fonts.overlay);
                canvas->drawTextBlob(blob, xOffset + 25, (covY * 0.5) + yOffsetAll + 10, theme.tcDel);
            }
            last_bamIdx = cl.bamIdx;
        }
    }

    inline void chooseFacecolors(int mapq, const Segs::Align &a, SkPaint &faceColor, const Themes::BaseTheme &theme) {
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

    inline void chooseEdgeColor(int edge_type, SkPaint &edgeColor, const Themes::BaseTheme &theme) {
        if (edge_type == 2) {
            edgeColor = theme.ecSplit;
        } else if (edge_type == 4) {
            edgeColor = theme.ecSelected;
        } else {
            edgeColor = theme.ecMateUnmapped;
        }
    }

    inline void
    drawRectangle(SkCanvas *canvas, const float polygonH, const float yScaledOffset, const float start,
                  const float width, const float xScaling,
                  const float xOffset, const SkPaint &faceColor, SkRect &rect) {
        rect.setXYWH((start * xScaling) + xOffset, yScaledOffset, width * xScaling, polygonH);
        canvas->drawRect(rect, faceColor);
    }

    inline void
    drawLeftPointedRectangle(SkCanvas *canvas, const float polygonH, const float yScaledOffset, float start,
                             float width,
                             const float xScaling, const float maxX, const float xOffset, const SkPaint &faceColor,
                             SkPath &path, const float slop, bool edged, SkPaint &edgeColor) {
        start *= xScaling;
        width *= xScaling;
        path.reset();
        path.moveTo(start + xOffset, yScaledOffset);
        path.lineTo(start - slop + xOffset, yScaledOffset + (polygonH * 0.5));
        path.lineTo(start + xOffset, yScaledOffset + polygonH);
        path.lineTo(start + width + xOffset, yScaledOffset + polygonH);
        path.lineTo(start + width + xOffset, yScaledOffset);
        path.close();
        canvas->drawPath(path, faceColor);
        if (edged) {
            canvas->drawPath(path, edgeColor);
        }
    }

    inline void
    drawRightPointedRectangle(SkCanvas *canvas, const float polygonH, const float yScaledOffset, float start,
                              float width,
                              const float xScaling, const float maxX, const float xOffset, const SkPaint &faceColor,
                              SkPath &path,
                              const float slop,
                              bool edged, SkPaint &edgeColor) {
        start *= xScaling;
        width *= xScaling;
        path.reset();
        path.moveTo(start + xOffset, yScaledOffset);
        path.lineTo(start + xOffset, yScaledOffset + polygonH);
        path.lineTo(start + width + xOffset, yScaledOffset + polygonH);
        path.lineTo(start + width + slop + xOffset, yScaledOffset + (polygonH * 0.5));
        path.lineTo(start + width + xOffset, yScaledOffset);
        path.close();
        canvas->drawPath(path, faceColor);
        if (edged) {
            canvas->drawPath(path, edgeColor);
        }
    }

    inline void
    drawHLine(SkCanvas *canvas, SkPath &path, const SkPaint &lc, const float startX, const float y, const float endX) {
        path.reset();
        path.moveTo(startX, y);
        path.lineTo(endX, y);
        canvas->drawPath(path, lc);
    }

    void drawIns(SkCanvas *canvas, float y0, float start, float yScaling, float xOffset,
                 float yOffset, float textW, const SkPaint &sidesColor, const SkPaint &faceColor, SkPath &path,
                 SkRect &rect, float pH, float pH_05, float pH_95) {
        float x = start + xOffset;
        float y = y0 * yScaling;
        float overhang = textW * 0.125;
        float text_half = textW * 0.5;
        rect.setXYWH(x - text_half - 2, y + yOffset, textW + 2, pH);
        canvas->drawRect(rect, faceColor);
        path.reset();
        path.moveTo(x - text_half - overhang, yOffset + y + pH_05);
        path.lineTo(x + text_half + overhang, yOffset + y + pH_05);
        path.moveTo(x - text_half - overhang, yOffset + y + pH_95);
        path.lineTo(x + text_half + overhang, yOffset + y + pH_95);
        path.moveTo(x, yOffset + y);
        path.lineTo(x, yOffset + y + pH);
        canvas->drawPath(path, sidesColor);
    }

    void drawMismatchesNoMD(SkCanvas *canvas, SkRect &rect, const Themes::BaseTheme &theme, const Utils::Region *region,
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
                        int r_idx = (int) r_pos - rbegin;  // Casting once and reusing.
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
                        // Using a lookup might be faster if 'switch-case' doesn't optimize well in the compiler.
                        switch (current_base) {
                            case 'A':
                            case 'a':
                                ref_base = 1;
                                break;
                            case 'C':
                            case 'c':
                                ref_base = 2;
                                break;
                            case 'G':
                            case 'g':
                                ref_base = 4;
                                break;
                            case 'T':
                            case 't':
                                ref_base = 8;
                                break;
                            default:
                                ref_base = 15;
                                break;  // assuming 'N' and other characters map to 15
                        }

                        char bam_base = bam_seqi(ptr_seq, idx);
                        if (bam_base != ref_base) {
                            p = (r_pos - rbegin) * xScaling;
                            uint32_t colorIdx = (l_qseq == 0) ? 10 : (ptr_qual[idx] > 10) ? 10 : ptr_qual[idx];
                            rect.setXYWH(p + precalculated_xOffset_mmPosOffset, yScaledOffset, width, pH);
                            canvas->drawRect(rect, theme.BasePaints[bam_base][colorIdx]);
                            if (!collection_processed) {
                                auto &mismatch = mm_array[r_pos - rbegin]; // Reduce redundant calculations
                                switch (bam_base) {
                                    case 1:
                                        mismatch.A += 1;
                                        break;
                                    case 2:
                                        mismatch.C += 1;
                                        break;
                                    case 4:
                                        mismatch.G += 1;
                                        break;
                                    case 8:
                                        mismatch.T += 1;
                                        break;
                                    default:
                                        break;
                                }
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

                            auto &mismatch = mm_array[r_pos - rbegin]; // Reduce redundant calculations
                            switch (bam_base) {
                                case 1:
                                    mismatch.A += 1;
                                    break;
                                case 2:
                                    mismatch.C += 1;
                                    break;
                                case 4:
                                    mismatch.G += 1;
                                    break;
                                case 8:
                                    mismatch.T += 1;
                                    break;
                                default:
                                    break;
                            }
                        }
                        idx += 1;
                        r_pos += 1;
                    }
                    break;
                case BAM_CHARD_CLIP:
                    continue;
                default:
//                case BAM_CEQUAL:
                    idx += l;
                    r_pos += l;
//                default:
//                    std::cerr << op << " unhandle\n";
//                    break;
            }
        }
    }

    void drawBlock(bool plotPointedPolygons, bool pointLeft, bool edged, float s, float e, float width,
                   float pointSlop, float pH, float yScaledOffset, float xScaling, float xOffset, float regionPixels,
                   size_t idx, size_t nBlocks, int regionLen,
                   const Segs::Align &a, SkCanvas *canvas, SkPath &path, SkRect &rect, SkPaint &faceColor,
                   SkPaint &edgeColor) {

        if (plotPointedPolygons) {
            if (pointLeft) {
                drawLeftPointedRectangle(canvas, pH, yScaledOffset, s, width, xScaling,
                                         regionPixels, xOffset, faceColor, path, pointSlop, edged, edgeColor);

            } else {
                drawRightPointedRectangle(canvas, pH, yScaledOffset, s, width, xScaling,
                                          regionPixels, xOffset, faceColor, path, pointSlop, edged, edgeColor);

            }
        } else {
            drawRectangle(canvas, pH, yScaledOffset, s, width, xScaling, xOffset, faceColor,
                          rect);
        }
    }

    void drawDeletionLine(const Segs::Align &a, SkCanvas *canvas, SkPath &path, const Themes::IniOptions &opts,
                          const Themes::Fonts &fonts,
                          int regionBegin, size_t idx, int Y, int regionLen, int starti, int lastEndi,
                          float regionPixels, float xScaling, float yScaling, float xOffset, float yOffset,
                          float textDrop, std::vector<TextItem> &text, bool indelTextFits) {

        int isize = starti - lastEndi;
        int lastEnd = lastEndi - regionBegin;
        starti -= regionBegin;

        lastEnd = (lastEnd < 0) ? 0 : lastEnd;
        int size = starti - lastEnd;
        if (size <= 0) {
            return;
        }
        float delBegin = (float) lastEnd * xScaling;
        float delEnd = delBegin + ((float) size * xScaling);
        float yh = ((float) Y + (float) polygonHeight * (float) 0.5) * yScaling + yOffset;

        if (isize >= opts.indel_length) {
            if (regionLen < 500000 && indelTextFits) { // line and text
                std::sprintf(indelChars, "%d", isize);
                size_t sl = strlen(indelChars);
                float textW = fonts.textWidths[sl - 1];
                float textBegin = (((float) lastEnd + (float) size / 2) * xScaling) - (textW / 2);
                float textEnd = textBegin + textW;
                if (textBegin < 0) {
                    textBegin = 0;
                    textEnd = textW;
                } else if (textEnd > regionPixels) {
                    textBegin = regionPixels - textW;
                    textEnd = regionPixels;
                }
                text.emplace_back() = {SkTextBlob::MakeFromString(indelChars, fonts.overlay),
                                       textBegin + xOffset,
                                       ((float) Y + polygonHeight) * yScaling - textDrop + yOffset};

                if (textBegin > delBegin) {
                    drawHLine(canvas, path, opts.theme.lcJoins, delBegin + xOffset, yh, textBegin + xOffset);
                    drawHLine(canvas, path, opts.theme.lcJoins, textEnd + xOffset, yh, delEnd + xOffset);
                }
            } else { // dot only
                delEnd = std::min(regionPixels, delEnd);
                if (delEnd - delBegin < 2) {
                    canvas->drawPoint(delBegin + xOffset, yh, opts.theme.lcBright);
                } else {
                    drawHLine(canvas, path, opts.theme.lcJoins, delBegin + xOffset, yh, delEnd + xOffset);
                }
            }
        } else if ((float) size / (float) regionLen > 0.0005) { // (regionLen < 50000 || size > 100) { // line only
            delEnd = std::min(regionPixels, delEnd);
            drawHLine(canvas, path, opts.theme.lcJoins, delBegin + xOffset, yh, delEnd + xOffset);
        }
    }

    void drawCollection(const Themes::IniOptions &opts, Segs::ReadCollection &cl,
                  SkCanvas *canvas, float trackY, float yScaling, const Themes::Fonts &fonts, int linkOp,
                  float refSpace, float pointSlop, float textDrop, float pH) {

        SkPaint faceColor;
        SkPaint edgeColor;

        SkRect rect;
        SkPath path;

        const Themes::BaseTheme &theme = opts.theme;

        std::vector<TextItem> text_ins, text_del;

        int regionBegin = cl.region->start;
        int regionEnd = cl.region->end;
        int regionLen = regionEnd - regionBegin;

        float xScaling = cl.xScaling;
        float xOffset = cl.xOffset;
        float yOffset = cl.yOffset;
        float regionPixels = cl.regionPixels; //regionLen * xScaling;

        bool plotSoftClipAsBlock = cl.plotSoftClipAsBlock; //regionLen > opts.soft_clip_threshold;
        bool plotPointedPolygons = cl.plotPointedPolygons; // regionLen < 50000;
        bool drawEdges = cl.drawEdges; //regionLen < opts.edge_highlights;

        std::vector<Segs::Mismatches> &mm_vector = cl.mmVector;

        cl.skipDrawingReads = true;

        float pH_05 = pH * 0.05;
        float pH_95 = pH * 0.95;

        for (const auto &a: cl.readQueue) {

            int Y = a.y;
            if (Y < 0) {
                continue;
            }

            bool indelTextFits = fonts.overlayHeight * 0.7 < yScaling;

            int mapq = a.delegate->core.qual;
            float yScaledOffset = (Y * yScaling) + yOffset;
            chooseFacecolors(mapq, a, faceColor, theme);
            bool pointLeft, edged;
            if (plotPointedPolygons) {
                pointLeft = (a.delegate->core.flag & 16) != 0;
            } else {
                pointLeft = false;
            }
            size_t nBlocks = a.block_starts.size();
            if (drawEdges && a.edge_type != 1) {
                edged = true;
                chooseEdgeColor(a.edge_type, edgeColor, theme);
            } else {
                edged = false;
            }
            double width, s, e, textW;
            int lastEnd = 1215752191;
            int starti;
            bool line_only;

            for (size_t idx = 0; idx < nBlocks; ++idx) {
                starti = (int) a.block_starts[idx];
                if (idx > 0) {
                    lastEnd = (int) a.block_ends[idx - 1];
                }

                if (starti > regionEnd) {
                    if (lastEnd < regionEnd) {
                        line_only = true;
                    } else {
                        break;
                    }
                } else {
                    line_only = false;
                }

                e = (double) a.block_ends[idx];
                if (e < regionBegin) { continue; }
                s = starti - regionBegin;
                e -= regionBegin;
                s = (s < 0) ? 0 : s;
                e = (e > regionLen) ? regionLen : e;
                width = e - s;
                if (!line_only) {
                    drawBlock(plotPointedPolygons, pointLeft, edged, (float) s, (float) e, (float) width,
                              pointSlop, pH, yScaledOffset, xScaling, xOffset, regionPixels,
                              idx, nBlocks, regionLen,
                              a, canvas, path, rect, faceColor, edgeColor);
                }

                // add lines and text between gaps
                if (idx > 0) {
                    drawDeletionLine(a, canvas, path, opts, fonts,
                                     regionBegin, idx, Y, regionLen, starti, lastEnd,
                                     regionPixels, xScaling, yScaling, xOffset, yOffset,
                                     textDrop, text_del, indelTextFits);
                }
            }

            // add soft-clip blocks
            int start = (int) a.pos - regionBegin;
            int end = (int) a.reference_end - regionBegin;
            auto l_seq = (int) a.delegate->core.l_qseq;
            if (opts.soft_clip_threshold != 0) {
                if (a.left_soft_clip > 0) {
                    width = (plotSoftClipAsBlock || l_seq == 0) ? (float) a.left_soft_clip : 0;
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
                            drawLeftPointedRectangle(canvas, pH, yScaledOffset, s, width, xScaling,
                                                     regionPixels, xOffset,
                                                     (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip,
                                                     path, pointSlop, false, edgeColor);
                        } else {
                            drawRectangle(canvas, pH, yScaledOffset, s, width, xScaling, xOffset,
                                          (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip, rect);
                        }
                    }
                }
                if (a.right_soft_clip > 0) {
                    if (plotSoftClipAsBlock || l_seq == 0) {
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
                            drawRightPointedRectangle(canvas, pH, yScaledOffset, s, width, xScaling,
                                                      regionPixels, xOffset,
                                                      (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip, path,
                                                      pointSlop, false, edgeColor);
                        } else {
                            drawRectangle(canvas, pH, yScaledOffset, s, width, xScaling,
                                          xOffset, (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip, rect);
                        }
                    }
                }
            }

            // add insertions
            if (!a.any_ins.empty()) {
                for (auto &ins: a.any_ins) {
                    float p = (ins.pos - regionBegin) * xScaling;
                    if (0 <= p && p < regionPixels) {
                        std::sprintf(indelChars, "%d", ins.length);
                        size_t sl = strlen(indelChars);
                        textW = fonts.textWidths[sl - 1];
                        if (ins.length > (uint32_t) opts.indel_length) {
                            if (regionLen < 500000 && indelTextFits) {  // line and text
                                drawIns(canvas, Y, p, yScaling, xOffset, yOffset, textW, theme.insS,
                                        theme.fcIns, path, rect, pH, pH_05, pH_95);
                                text_ins.emplace_back() = {SkTextBlob::MakeFromString(indelChars, fonts.overlay),
                                                           (float)(p - (textW * 0.5) + xOffset - 2),
                                                           ((Y + polygonHeight) * yScaling) + yOffset - textDrop};

                            } else {  // line only
                                drawIns(canvas, Y, p, yScaling, xOffset, yOffset, xScaling, theme.insS,
                                        theme.fcIns, path, rect, pH, pH_05, pH_95);
                            }
                        } else if (regionLen < 100000 && regionLen < opts.small_indel_threshold) {  // line only
                            drawIns(canvas, Y, p, yScaling, xOffset, yOffset, xScaling, theme.insS,
                                    theme.fcIns, path, rect, pH, pH_05, pH_95);
                        }
                    }
                }
            }

            // add mismatches
            if (regionLen > opts.snp_threshold && plotSoftClipAsBlock) {
                continue;
            }
            if (l_seq == 0) {
                continue;
            }
            float mmPosOffset, mmScaling;
            if (regionLen < 500) {
                mmPosOffset = 0.05;
                mmScaling = 0.9;
            } else {
                mmPosOffset = 0;
                mmScaling = 1;
            }
            int colorIdx;

            int32_t l_qseq = a.delegate->core.l_qseq;

            if (regionLen <= opts.snp_threshold) {
                float mms = xScaling * mmScaling;
                width = (regionLen < 500000) ? ((1. > mms) ? 1. : mms) : xScaling;
                drawMismatchesNoMD(canvas, rect, theme, cl.region, a, (float) width, xScaling, xOffset, mmPosOffset,
                                   yScaledOffset, pH, l_qseq, mm_vector, cl.collection_processed);
            }

            // add soft-clips
            if (!plotSoftClipAsBlock) {
                uint8_t *ptr_seq = bam_get_seq(a.delegate);
                uint8_t *ptr_qual = bam_get_qual(a.delegate);
                if (a.right_soft_clip > 0) {
                    int pos = (int) a.reference_end - regionBegin;
                    if (pos < regionLen && a.cov_end > regionBegin) {
                        int opLen = (int) a.right_soft_clip;
                        for (int idx = l_seq - opLen; idx < l_seq; ++idx) {
                            float p = pos * xScaling;
//                                if (0 <= p && p < regionPixels) {
                                uint8_t base = bam_seqi(ptr_seq, idx);
                                uint8_t qual = ptr_qual[idx];
                                colorIdx = (l_qseq == 0) ? 10 : (qual > 10) ? 10 : qual;
                                rect.setXYWH(p + xOffset + mmPosOffset, yScaledOffset, xScaling * mmScaling, pH);
                                canvas->drawRect(rect, theme.BasePaints[base][colorIdx]);
//                                } else if (p > regionPixels) {
//                                    break;
//                                }
                            pos += 1;
                        }
                    }
                }
                if (a.left_soft_clip > 0) {
                    int opLen = (int) a.left_soft_clip;
                    int pos = (int) a.pos - regionBegin - opLen;
                    for (int idx = 0; idx < opLen; ++idx) {
                        float p = pos * xScaling;
//                            if (0 <= p && p < regionPixels) {
                            uint8_t base = bam_seqi(ptr_seq, idx);
                            uint8_t qual = ptr_qual[idx];
                            colorIdx = (l_qseq == 0) ? 10 : (qual > 10) ? 10 : qual;
                            rect.setXYWH(p + xOffset + mmPosOffset, yScaledOffset, xScaling * mmScaling, pH);
                            canvas->drawRect(rect, theme.BasePaints[base][colorIdx]);
//                            } else if (p >= regionPixels) {
//                                break;
//                            }
                        pos += 1;
                    }
                }
            }
        }

        // draw text last
        for (const auto &t : text_del) {
            canvas->drawTextBlob(t.text.get(), t.x, t.y, theme.tcDel);
        }
        for (const auto &t : text_ins) {
            canvas->drawTextBlob(t.text.get(), t.x, t.y, theme.tcIns);
        }

//        canvas->restore();

        // draw connecting lines between linked alignments
        if (linkOp > 0) {
            if (!cl.linked.empty()) {
                const Segs::map_t &lm = cl.linked;
                SkPaint paint;
                float offsety = (yScaling * 0.5) + cl.yOffset;
                for (auto const &keyVal: lm) {
                    const std::vector<Segs::Align *> &ind = keyVal.second;
                    int size = (int) ind.size();
                    if (size > 1) {
                        float max_x = cl.xOffset + (((float) cl.region->end - (float) cl.region->start) * cl.xScaling);

                        for (int jdx = 0; jdx < size - 1; ++jdx) {

                            const Segs::Align *segA = ind[jdx];
                            const Segs::Align *segB = ind[jdx + 1];

                            if (segA->y == -1 || segB->y == -1 || segA->block_ends.empty() ||
                                segB->block_ends.empty() ||
                                (segA->delegate->core.tid != segB->delegate->core.tid)) { continue; }

                            long cstart = std::min(segA->block_ends.front(), segB->block_ends.front());
                            long cend = std::max(segA->block_starts.back(), segB->block_starts.back());
                            double x_a = ((double) cstart - (double) cl.region->start) * cl.xScaling;
                            double x_b = ((double) cend - (double) cl.region->start) * cl.xScaling;

                            x_a = (x_a < 0) ? 0 : x_a;
                            x_b = (x_b < 0) ? 0 : x_b;
                            x_a += cl.xOffset;
                            x_b += cl.xOffset;
                            x_a = (x_a > max_x) ? max_x : x_a;
                            x_b = (x_b > max_x) ? max_x : x_b;

                            float y = ((float) segA->y * yScaling) + offsety;

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
                            paint.setStrokeWidth(2);
                            path.reset();
                            path.moveTo(x_a, y);
                            path.lineTo(x_b, y);
                            canvas->drawPath(path, paint);
                        }
                    }
                }
            }
        }
    }

    void drawRef(const Themes::IniOptions &opts,
                 std::vector<Utils::Region> &regions, int fb_width,
                 SkCanvas *canvas, const Themes::Fonts &fonts, float h, float nRegions, float gap) {
        if (regions.empty()) {
            return;
        }
        SkRect rect;
        SkPaint faceColor;
        const Themes::BaseTheme &theme = opts.theme;
        double regionW = (double) fb_width / (double) regions.size();
        double xPixels = regionW - gap - gap;
        float textW = fonts.overlayWidth;
        float minLetterSize = (textW > 0) ? ((float) fb_width / (float) regions.size()) / textW : 0;
        int index = 0;
        //h *= 0.7;
//        h = (h - 6 < 4) ? 4 : h - 6;
        float yp = h + 2;
        for (auto &rgn: regions) {
            int size = rgn.end - rgn.start;
            double xScaling = xPixels / size;
            const char *ref = rgn.refSeq;
            if (ref == nullptr) {
                continue;
            }
            double mmPosOffset, mmScaling;
            if (size < 250) {
                mmPosOffset = 2;// 0.05;
                mmScaling = 0.9 * xScaling;
            } else {
                mmPosOffset = 2; //h * 0.2;
                mmScaling = 1 * xScaling;
            }
            double i = regionW * index;
            i += gap;
            if (textW > 0 && (float) size < minLetterSize && fonts.fontSize <= h * 1.35) {
                double v = (xScaling - textW) * 0.5;

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
                    rect.setXYWH(i, mmPosOffset, mmScaling, h);
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

    void drawBorders(const Themes::IniOptions &opts, float fb_width, float fb_height,
                     SkCanvas *canvas, size_t nRegions, size_t nbams, float trackY, float covY, int nTracks,
                     float totalTabixY, float refSpace, float gap) {
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
                path.moveTo(0, y);
                path.lineTo(fb_width, y);
                y += step;
            }
            canvas->drawPath(path, opts.theme.lcLightJoins);
        }
        if (nTracks > 0) {
            float y = fb_height - totalTabixY - refSpace;
            float step = totalTabixY / (float) nTracks;
            path.reset();
            for (int i = 0; i < (int) nTracks; ++i) {
                path.moveTo(0, y);
                path.lineTo(fb_width, y);
                y += step;
            }
            canvas->drawPath(path, opts.theme.lcLightJoins);
        }
    }

    void
    drawLabel(const Themes::IniOptions &opts, SkCanvas *canvas, SkRect &rect, Utils::Label &label, Themes::Fonts &fonts,
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
    }

    void drawTrackBigWig(HGW::GwTrack &trk, const Utils::Region &rgn, SkRect &rect, float padX, float padY,
                         float y, float stepX, float stepY, float gap, float gap2, float xScaling, float t,
                         Themes::IniOptions &opts, SkCanvas *canvas, const Themes::Fonts &fonts) {
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
        canvas->drawPath(path, opts.theme.fcBigWig);
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
            canvas->drawRect(rect, opts.theme.fcBigWig);
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

    int getPt( int n1 , int n2 , float perc ) {
        int diff = n2 - n1;
        return n1 + ( diff * perc );
    }

    void drawTrackBlock(int start, int stop, std::string &rid, const Utils::Region &rgn, SkRect &rect, SkPath &path,
                        float padX, float padY,
                        float y, float h, float stepX, float stepY, float gap, float gap2, float xScaling, float t,
                        Themes::IniOptions &opts, SkCanvas *canvas, const Themes::Fonts &fonts,
                        bool add_text, bool add_rect, bool v_line, bool shaded, float *labelsEnd, std::string &vartype,
                        float monitorScale, std::vector<TextItem> &text, bool addArc) {
        float x = 0;
        float w;

        SkPaint faceColour, arcColour;

        if (shaded) {
            faceColour = opts.theme.fcCoverage;
        } else {
            faceColour = opts.theme.fcTrack;
        }

        if (!vartype.empty()) {
            if (vartype == "DEL") {
                if (addArc) {
                    arcColour = opts.theme.fcT;
                } else {
                    faceColour = opts.theme.fcT;
                    faceColour.setAlpha(150);
                }

            } else if (vartype == "DUP") {
                if (addArc) {
                    arcColour = opts.theme.fcC;
                } else {
                    faceColour = opts.theme.fcC;
                    faceColour.setAlpha(150);
                }
            } else if (vartype == "INV") {
                if (addArc) {
                    arcColour = opts.theme.fcA;
                } else {
                    faceColour = opts.theme.fcA;
                    faceColour.setAlpha(150);
                }
            } else {
                addArc = false;
            }
        } else {
            addArc = false;
        }

        if (addArc) {
            v_line = false;
        }

        x = (float) (start - rgn.start) * xScaling;
        w = (float) (stop - start) * xScaling;
        rect.setXYWH(x + padX, y + padY, w, h);

        if (rect.bottom() == 0) {
            return;
        }
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

        }
        else if (add_rect) {
            canvas->drawRect(rect, faceColour);
            if (shaded) {
                canvas->drawRect(rect, opts.theme.lcLightJoins);
            }
        }
        if (v_line && x != 0) {
            path.moveTo(x + padX, y + padY);
            path.lineTo(x + padX, y + h + padY);
            canvas->drawPath(path, opts.theme.lcLightJoins);
        }

        if (!add_text) {
            return;
        }
        float spaceRemaining = stepY - h - h;
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
                               rect.bottom() + fonts.overlayHeight};

    }

    void drawGappedTrackBlock(Themes::IniOptions &opts, float fb_width, float fb_height,
                              SkCanvas *canvas, float totalTabixY, float tabixY, std::vector<HGW::GwTrack> &tracks,
                              const std::vector<Utils::Region> &regions, const Themes::Fonts &fonts,
                              float gap, Utils::TrackBlock &trk, bool any_text, const Utils::Region &rgn, SkRect &rect,
                              SkPath &path, SkPath &path2, float padX, float padY, float stepX, float stepY,
                              float y, float h, float h2, float h4, float gap2, float xScaling, float t, int nLevels,
                              float *labelsEnd, std::vector<TextItem> &text, bool vline) {

        int target = (int) trk.s.size();
        int stranded = trk.strand;
        float screenLeftEdge = padX;
        float screenRightEdge = padX + (((float) rgn.end - (float) rgn.start) * xScaling);
        std::string empty_str;
        if (any_text) {
            drawTrackBlock(trk.start, trk.end, trk.name, rgn, rect, path, padX, padY, y, h, stepX, stepY, gap, gap2,
                           xScaling, t,
                           opts, canvas, fonts, true, false, false, false, labelsEnd, empty_str, 0, text, false);
        }
        for (int i = 0; i < target; ++i) {
            int s, e;
            s = trk.s[i];
            e = trk.e[i];
            if (e < rgn.start) {
                continue;
            }
            bool add_line = (i == 0 && vline);  // vertical line at start of interval
            uint8_t thickness = trk.drawThickness[i];

            if (thickness && s < rgn.end && e > rgn.start) {

                if ((trk.coding_end != -1 && s >= trk.coding_end) ||
                    (trk.coding_start != -1 && e <= trk.coding_start)) {
                    thickness = 1;
                }

                if (s < trk.coding_end && e > trk.coding_end) { //overlaps, split in to two blocks!
                    drawTrackBlock(s, trk.coding_end, trk.name, rgn, rect, path, padX, padY, y, h, stepX, stepY, gap,
                                   gap2, xScaling, t,
                                   opts, canvas, fonts, false, true, add_line, false, labelsEnd, empty_str, 0, text, false);
                    drawTrackBlock(trk.coding_end, e, trk.name, rgn, rect, path, padX, padY, y, h, stepX, stepY, gap,
                                   gap2, xScaling, t,
                                   opts, canvas, fonts, false, true, add_line, true, labelsEnd, empty_str, 0, text, false);
                }

                else if (thickness == 1) {
                    drawTrackBlock(s, e, trk.name, rgn, rect, path, padX, padY, y, h, stepX, stepY, gap, gap2, xScaling,
                                   t,
                                   opts, canvas, fonts, false, true, add_line, true, labelsEnd, empty_str, 0, text, false);
                } else {
                    drawTrackBlock(s, e, trk.name, rgn, rect, path, padX, padY, y, h, stepX, stepY, gap, gap2, xScaling,
                                   t,
                                   opts, canvas, fonts, false, true, add_line, false, labelsEnd, empty_str, 0, text, false);
                }
            }
            float x, yy, w;
            yy = y + padY + (h / 2);
            int lastEnd = (i > 0) ? trk.e[i - 1] : trk.start;
            if (lastEnd < s) {  // add arrows
                x = std::max(((float) (lastEnd - rgn.start) * xScaling) + padX, (float) screenLeftEdge);
                w = std::min(((float) (s - rgn.start) * xScaling) + padX, (float) screenRightEdge);
                if (w > x) {
                    path2.reset();
                    path2.moveTo(x, yy);
                    path2.lineTo(w, yy);
                    canvas->drawPath(path2, opts.theme.lcLightJoins);
                    if (stranded != 0 && w - x > 50) {
                        while (x + 50 < w) {
                            x += 50;
                            path2.reset();
                            if (stranded == 1) {
                                path2.moveTo(x, yy);
                                path2.lineTo(x - 6, yy + 6);
                                path2.moveTo(x, yy);
                                path2.lineTo(x - 6, yy - 6);
                            } else {
                                path2.moveTo(x, yy);
                                path2.lineTo(x + 6, yy + 6);
                                path2.moveTo(x, yy);
                                path2.lineTo(x + 6, yy - 6);
                            }
                            canvas->drawPath(path2, opts.theme.lcJoins);
                        }
                    }
                }
            }
        }
    }

    void drawTracks(Themes::IniOptions &opts, float fb_width, float fb_height,
                    SkCanvas *canvas, float totalTabixY, float tabixY, std::vector<HGW::GwTrack> &tracks,
                    std::vector<Utils::Region> &regions, const Themes::Fonts &fonts, float gap, float monitorScale) {
        // All tracks are converted to TrackBlocks and then drawn
        if (tracks.empty() || regions.empty()) {
            return;
        }
        float gap2 = 2 * gap;
        float padX = gap;

        float stepX = fb_width / (float) regions.size();
        float refSpace = fonts.overlayHeight;
        float stepY = (totalTabixY) / (float) tracks.size();

        float y = fb_height - totalTabixY - refSpace;  // start of tracks on canvas
        float t = (float) 0.005 * fb_width;

        SkRect rect{};
        SkPath path{};
        SkPath path2{};

        opts.theme.lcLightJoins.setAntiAlias(true);
        bool expanded = opts.expand_tracks;

        for (auto &rgn: regions) {
            bool any_text = true;
            bool add_line = ((rgn.end - rgn.start) < opts.soft_clip_threshold);
            float xScaling = (stepX - gap2) / (float) (rgn.end - rgn.start);
            float padY = gap;
            int trackIdx = 0;

            rgn.featuresInView.clear();
            rgn.featuresInView.resize(tracks.size());
            rgn.featureLevels.clear();
            rgn.featureLevels.resize(tracks.size());
            for (auto &trk: tracks) {

                float right = ((float) (rgn.end - rgn.start) * xScaling) + padX;
                canvas->save();
                canvas->clipRect({padX, y + padY, right, y + padY + stepY}, false);

                trk.fetch(&rgn);
                if (trk.kind == HGW::BIGWIG) {
                    drawTrackBigWig(trk, rgn, rect, padX, padY, y + (stepY * trackIdx), stepX, stepY, gap, gap2,
                                    xScaling, t, opts, canvas, fonts);
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
                float h = std::fmin(blockSpace, 20);
                float h2 = h * 0.5;
                float h4 = h2 * 0.5;
                float step_track = (stepY - gap2) / ((float) nLevels);
                bool isBed12 = !trk.parts.empty() && trk.parts.size() >= 12;
                float textLevelEnd = 0;  // makes sure text doesnt overlap on same level

                for (auto &f: features) {
                    float padY_track = padY + (step_track * f.level);
                    float *fLevelEnd = (nLevels > 1) ? &labelsEndLevels[f.level] : &textLevelEnd;
                    if (isGFF || isBed12) {
                        if (!f.anyToDraw || f.start > rgn.end || f.end < rgn.start) {
                            continue;
                        }
                        drawGappedTrackBlock(opts, fb_width, fb_height, canvas, totalTabixY, tabixY, tracks, regions,
                                             fonts, gap,
                                             f, any_text, rgn, rect, path, path2, padX, padY_track, stepX, step_track,
                                             y, h, h2, h4, gap2,
                                             xScaling, t, nLevels, fLevelEnd, text, add_line);

                    } else {
                        drawTrackBlock(f.start, f.end, f.name, rgn, rect, path, padX, padY_track, y, h, stepX, stepY,
                                       gap, gap2,
                                       xScaling, t, opts, canvas, fonts, any_text, true, add_line, false, fLevelEnd, f.vartype, monitorScale,
                                       text, opts.sv_arcs);
                    }
                }
                trackIdx += 1;
                padY += stepY;
                if (fonts.overlayHeight * nLevels * 2 < stepY && features.size() < 500) {
                    for (const auto&t: text) {
                        canvas->drawTextBlob(t.text, t.x, t.y, opts.theme.tcDel);
                    }
                }
                canvas->restore();
            }
            padX += stepX;
        }
        opts.theme.lcLightJoins.setAntiAlias(false);

    }

    void drawChromLocation(const Themes::IniOptions &opts, const std::vector<Segs::ReadCollection> &collections,
                           SkCanvas *canvas,
                           const faidx_t *fai, std::vector<sam_hdr_t *> &headers, size_t nRegions, float fb_width,
                           float fb_height, float monitorScale) {
        SkPaint paint, line;
//        paint.setARGB(255, 110, 120, 165);

        if (opts.theme_str == "igv") {
            paint.setARGB(255, 87, 95, 107);
//            paint.setARGB(255, 160, 160, 165);
        } else {
            paint.setARGB(255, 149, 149, 163);
//            paint.setColor(SK_ColorRED);
        }
        paint.setStrokeWidth(2);
        paint.setStyle(SkPaint::kStroke_Style);
//        line.setColor((opts.theme_str == "dark") ? SK_ColorGRAY : SK_ColorBLACK);
        line.setColor((opts.theme_str == "dark") ? SK_ColorGRAY : SK_ColorBLACK);
        line.setStrokeWidth(monitorScale);
        line.setStyle(SkPaint::kStroke_Style);
        SkRect rect{};
        SkPath path{};
        auto yh = std::fmax((float) (fb_height * 0.0175), 10 * monitorScale);
        float rowHeight = (float) fb_height / (float) headers.size();
        float colWidth = (float) fb_width / (float) nRegions;
        float gap = 50;
        float gap2 = 2 * gap;
        float drawWidth = colWidth - gap2;
        if (drawWidth < 0) {
            return;
        }
        for (auto &cl: collections) {
            if (cl.bamIdx + 1 != (int) headers.size()) {
                continue;
            }
            auto length = (float) faidx_seq_len(fai, cl.region->chrom.c_str());
            float s = (float) cl.region->start / length;
            float e = (float) cl.region->end / length;
            float w = (e - s) * drawWidth;
            if (w < 3) {
                w = 3;
            }
            float yp = ((cl.bamIdx + 1) * rowHeight) - yh - (yh * 0.5);
            float xp = (cl.regionIdx * colWidth) + gap;
            rect.setXYWH(xp + (s * drawWidth),
                         yp,
                         w,
                         yh);
            path.reset();
            path.moveTo(xp, ((cl.bamIdx + 1) * rowHeight) - (yh));
            path.lineTo(xp + drawWidth, ((cl.bamIdx + 1) * rowHeight) - (yh));
            canvas->drawPath(path, line);
            canvas->drawRect(rect, paint);
        }
    }
}