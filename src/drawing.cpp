//
// Created by Kez Cleal on 12/08/2022.
//
#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <vector>
#include <utility>
#include <string>
#include <stdio.h>

#include <GLFW/glfw3.h>

#define SK_GL
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"
#include "include/core/SkData.h"
#include "include/core/SkStream.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSize.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPoint.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFont.h"
#include "include/core/SkTextBlob.h"

#include "htslib/hts.h"
#include "htslib/sam.h"

#include "../inc/BS_thread_pool.h"
#include "drawing.h"
#include "hts_funcs.h"
#include "../inc/robin_hood.h"
#include "utils.h"
#include "segments.h"
#include "themes.h"


namespace Drawing {

    SkPoint pArr[5];
    char indelChars[10];
    constexpr float polygonHeight = 0.85;
    std::mutex mtx;

    void drawCoverage(const Themes::IniOptions &opts, const std::vector<Segs::ReadCollection> &collections,
                      SkCanvas *canvas, const Themes::Fonts &fonts, float covY) {

        const Themes::BaseTheme &theme = opts.theme;
        SkPaint paint = theme.fcCoverage;
        SkPath path;

        std::vector<sk_sp < SkTextBlob> > text;
        std::vector<sk_sp < SkTextBlob> > text_ins;
        std::vector<float> textX, textY;
        std::vector<float> textX_ins, textY_ins;

        float cOff = 0;
        float yOffsetAll = 0;
        int cMaxi = 0;
        for (auto &cl: collections) {
            float xScaling = cl.xScaling;
            float xOffset = cl.xOffset;
            std::vector<int> covArr = cl.covArr;
            std::vector<float> c;
            c.resize(covArr.size());
            c[0] = covArr[0];
            float cMax = 0;
            for (size_t i=1; i<c.size(); ++i) { // cum sum
                c[i] = covArr[i] + c[i-1];
                if (c[i] > cMaxi) {
                    cMaxi = c[i];
                }
            }
            if (opts.log2_cov) {
                for (size_t i=1; i<c.size(); ++i) {
                    if (c[i] > 0) { c[i] = std::log2(c[i]); }
                }
                cMax = std::log2(cMaxi);
            } else {
                cMax = cMaxi;
            }

            // normalize to space available
            for (auto &i : c) {
                i = ((1 - (i / cMax)) * covY) * 0.7;
                i += yOffsetAll + (covY * 0.3);
            }
            cOff += covY;
            int step;
            if (c.size() > 2000) {
                step = std::max(1, (int)(c.size() / 2000));
            } else {
                step = 1;
            }
            float lastY = cOff;
            double x = xOffset;
            path.reset();
            path.moveTo(x, cOff);
            for (size_t i=0; i<c.size(); ++i)  {
                if (i % step == 0 || i == c.size() - 1) {
                    path.lineTo(x, lastY);
                    path.lineTo(x, c[i]);
                }
                lastY = c[i];
                x += xScaling;
            }
            path.lineTo(x - xScaling, cOff);
            path.lineTo(xOffset, cOff);

            path.close();
            sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(indelChars, fonts.fonty);

            canvas->drawPath(path, paint);
            canvas->drawTextBlob(blob, xOffset + 25, (cOff * 0.3) + yOffsetAll + 10, theme.tcDel);

            yOffsetAll += cl.yPixels;
            cOff += -covY + cl.yPixels;
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
                    faceColor = theme.mate_fc0[a.delegate->core.mtid % 48];
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
                    faceColor = theme.mate_fc[a.delegate->core.mtid % 48];
                    break;
            }
        }
    }

    inline void chooseEdgeColor(int edge_type, SkPaint &edgeColor, const Themes::BaseTheme &theme) {
        if (edge_type == 2) {
            edgeColor = theme.ecSplit;
        } else {
            edgeColor = theme.ecMateUnmapped;
        }
    }

    inline void
    drawRectangle(SkCanvas *canvas, const float polygonH, const float yScaledOffset, const float start, const float width, const float xScaling,
                  const float xOffset, const SkPaint &faceColor, SkRect &rect) {
        rect.setXYWH((start * xScaling) + xOffset, yScaledOffset, width * xScaling, polygonH);
        canvas->drawRect(rect, faceColor);
    }

    inline void
    drawLeftPointedRectangle(SkCanvas *canvas, const float polygonH, const float yScaledOffset, float start, float width,
                             const float xScaling, const float maxX, const float xOffset, const SkPaint &faceColor, SkPath &path, const float slop) {
        start *= xScaling;
        width *= xScaling;
        if (start < 0) {
            width += start;
            start = 0;
        }
        if (start + width > maxX) {
            width = maxX - start;
        }
        path.reset();
        path.moveTo(start + xOffset, yScaledOffset);
        path.lineTo(start - slop + xOffset, yScaledOffset + polygonH / 2);
        path.lineTo(start + xOffset, yScaledOffset + polygonH);
        path.lineTo(start + width + xOffset, yScaledOffset + polygonH);
        path.lineTo(start + width + xOffset, yScaledOffset);
        path.close();

//        pArr[0].set(start + xOffset, yScaledOffset);
//        pArr[1].set(start - slop + xOffset, yScaledOffset + polygonH / 2);
//        pArr[2].set(start + xOffset, yScaledOffset + polygonH);
//        pArr[3].set(start + width + xOffset, yScaledOffset + polygonH);
//        pArr[4].set(start + width + xOffset, yScaledOffset);
//        path.addPoly(pArr, 5, true);
        canvas->drawPath(path, faceColor);
    }

    inline void
    drawRightPointedRectangle(SkCanvas *canvas, const float polygonH, const float yScaledOffset, float start, float width,
                              const float xScaling, const float maxX, const float xOffset, const SkPaint &faceColor, SkPath &path,
                              const float slop) {
        start *= xScaling;
        width *= xScaling;
        if (start < 0) {
            width += start;
            start = 0;
        }
        if (start + width > maxX) {
            width = maxX - start;
        }
        path.reset();
        path.moveTo(start + xOffset, yScaledOffset);
        path.lineTo(start + xOffset, yScaledOffset + polygonH);
        path.lineTo(start + width + xOffset, yScaledOffset + polygonH);
        path.lineTo(start + width + slop + xOffset, yScaledOffset + polygonH / 2);
        path.lineTo(start + width + xOffset, yScaledOffset);
        path.close();

//        pArr[0].set(start + xOffset, yScaledOffset);
//        pArr[1].set(start + xOffset, yScaledOffset + polygonH);
//        pArr[2].set(start + width + xOffset, yScaledOffset + polygonH);
//        pArr[3].set(start + width + slop + xOffset, yScaledOffset + polygonH / 2);
//        pArr[4].set(start + width + xOffset, yScaledOffset);
//        path.addPoly(pArr, 5, true);
        canvas->drawPath(path, faceColor);
    }

    inline void drawHLine(SkCanvas *canvas, SkPath &path, const SkPaint &lc, const float startX, const float y, const float endX) {
        path.reset();
        path.moveTo(startX, y);
        path.lineTo(endX, y);
        canvas->drawPath(path, lc);
    }

    void drawIns(SkCanvas *canvas, float y0, float start, float yScaling, float xOffset,
                 float yOffset, float textW, const SkPaint &sidesColor, const SkPaint &faceColor, SkPath &path,
                 SkRect &rect) {

        float x = start + xOffset;
        float y = y0 * yScaling;
        float ph = polygonHeight * yScaling;
        float overhang = textW * 0.125;
        rect.setXYWH(x - (textW / 2), y + yOffset, textW, ph);
        canvas->drawRect(rect, faceColor);

        path.reset();
        path.moveTo(x - (textW / 2) - overhang, yOffset + y + ph * 0.05);
        path.lineTo(x + (textW / 2) + overhang, yOffset + y + ph * 0.05);
        path.moveTo(x - (textW / 2) - overhang, yOffset + y + ph * 0.95);
        path.lineTo(x + (textW / 2) + overhang, yOffset + y + ph * 0.95);
        path.moveTo(x, yOffset + y);
        path.lineTo(x, yOffset + y + ph);
        canvas->drawPath(path, sidesColor);
    }

    void drawBams(const Themes::IniOptions &opts, const std::vector<Segs::ReadCollection> &collections,
                  SkCanvas *canvas, float yScaling, const Themes::Fonts &fonts) {

        SkPaint faceColor;
        SkPaint edgeColor;

        SkRect rect;
        SkPath path;

        const Themes::BaseTheme &theme = opts.theme;

        std::vector<sk_sp < SkTextBlob> > text;
        std::vector<sk_sp < SkTextBlob> > text_ins;
        std::vector<float> textX, textY;
        std::vector<float> textX_ins, textY_ins;

        for (auto &cl: collections) {

            int regionBegin = cl.region.start;
            int regionEnd = cl.region.end;
            int regionLen = regionEnd - regionBegin;

            float xScaling = cl.xScaling;
            float xOffset = cl.xOffset;
            float yOffset = cl.yOffset;
            float regionPixels = regionLen * xScaling;
            float pointSlop = ((regionLen / 100) * 0.25) * xScaling;
            float textDrop = (yScaling - fonts.fontHeight) / 2;

            bool plotSoftClipAsBlock = regionLen > opts.soft_clip_threshold;
            bool plotPointedPolygons = regionLen < 50000;

            float pH = yScaling * polygonHeight;

            for (auto &a: cl.readQueue) {

                int Y = a.y;
                if (Y == -1) {
                    continue;
                }
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

                if (regionLen < 100000 && a.edge_type != 1) {
                    edged = true;
                    chooseEdgeColor(a.edge_type, edgeColor, theme);
                } else {
                    edged = false;
                }

                float width, s, e, yh, textW;
                for (size_t idx = 0; idx < nBlocks; ++idx) {
                    s = a.block_starts[idx];
                    if (s > regionEnd) { break; }
                    e = a.block_ends[idx];
                    if (e < regionBegin) { continue; }
                    s -= regionBegin;
                    e -= regionBegin;
                    s = (s < 0) ? 0: s;
                    e = (e > regionLen) ? regionLen : e;
                    width = e - s;
                    if (plotPointedPolygons) {
                        if (pointLeft) {
                            if (s > 0 && idx == 0 && a.left_soft_clip == 0) {
                                drawLeftPointedRectangle(canvas, pH, yScaledOffset, s, width, xScaling,
                                                         regionPixels, xOffset, faceColor, path, pointSlop);
                                if (edged) {
                                    drawLeftPointedRectangle(canvas, pH, yScaledOffset, s, width, xScaling,
                                                             regionPixels, xOffset, edgeColor, path, pointSlop);
                                }
                            } else {
                                drawRectangle(canvas, pH, yScaledOffset, s, width, xScaling, xOffset,
                                              faceColor, rect);
                                if (edged) {
                                    drawRectangle(canvas, pH, yScaledOffset, s, width, xScaling, xOffset,
                                                  edgeColor, rect);
                                }
                            }
                        } else {
                            if (e < regionLen && idx == nBlocks - 1 && a.right_soft_clip == 0) {
                                drawRightPointedRectangle(canvas, pH, yScaledOffset, s, width, xScaling,
                                                          regionPixels, xOffset, faceColor, path, pointSlop);
                                if (edged) {
                                    drawRightPointedRectangle(canvas, pH, yScaledOffset, s, width, xScaling,
                                                              regionPixels, xOffset, edgeColor, path, pointSlop);
                                }
                            } else {
                                drawRectangle(canvas, pH, yScaledOffset, s, width, xScaling, xOffset,
                                              faceColor, rect);
                                if (edged) {
                                    drawRectangle(canvas, pH, yScaledOffset, s, width, xScaling, xOffset,
                                                  edgeColor, rect);
                                }
                            }
                        }
                    } else {
                        drawRectangle(canvas, pH, yScaledOffset, s, width, xScaling, xOffset, faceColor,
                                      rect);
                        if (edged) {
                            drawRectangle(canvas, pH, yScaledOffset, s, width, xScaling, xOffset,
                                          edgeColor, rect);
                        }
                    }


                    // add lines and text between gaps
                    if (idx > 0) {
                        float lastEnd = a.block_ends[idx - 1] - regionBegin;
                        lastEnd = (lastEnd < 0) ? 0 : lastEnd;
                        int size = s - lastEnd;
                        float delBegin = lastEnd * xScaling;
                        float delEnd = delBegin + (size * xScaling);
                        yh = (Y + polygonHeight * 0.5) * yScaling + yOffset;
                        if (size <= 0) { continue; }
                        if (regionLen < 500000 && size >= opts.indel_length) { // line and text
                            std::sprintf(indelChars, "%d", size);
                            size_t sl = strlen(indelChars);
                            textW = fonts.textWidths[sl - 1];
                            float textBegin = ((lastEnd + size / 2) * xScaling) - (textW / 2);
                            float textEnd = textBegin + textW;
                            if (textBegin < 0) {
                                textBegin = 0;
                                textEnd = textW;
                            } else if (textEnd > regionPixels) {
                                textBegin = regionPixels - textW;
                                textEnd = regionPixels;
                            }
                            text.push_back(SkTextBlob::MakeFromString(indelChars, fonts.fonty));
                            textX.push_back(textBegin + xOffset);
                            textY.push_back((Y + polygonHeight) * yScaling - textDrop + yOffset);
                            if (textBegin > delBegin) {
                                drawHLine(canvas, path, theme.lcJoins, delBegin + xOffset, yh, textBegin + xOffset);
                                drawHLine(canvas, path, theme.lcJoins, textEnd + xOffset, yh, delEnd + xOffset);
                            }
                        } else if (size / (float) regionLen >
                                   0.0005) { // (regionLen < 50000 || size > 100) { // line only
                            delEnd = std::min(regionPixels, delEnd);
                            drawHLine(canvas, path, theme.lcJoins, delBegin + xOffset, yh, delEnd + xOffset);
                        }
                    }
                }

                // add soft-clip blocks
                int start = a.delegate->core.pos - regionBegin;
                int end = a.reference_end - regionBegin;
                if (a.left_soft_clip > 0) {
                    width = (plotSoftClipAsBlock) ? (float) a.left_soft_clip : 0;
                    s = start - a.left_soft_clip;
                    e = start + width;
                    if (e > 0) {
                        if (pointLeft && plotPointedPolygons) {
                            drawLeftPointedRectangle(canvas, pH, yScaledOffset, s, width, xScaling,
                                                     regionPixels, xOffset,
                                                     (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip,
                                                     path, pointSlop);
                        } else {
                            drawRectangle(canvas, pH, yScaledOffset, s, width, xScaling, xOffset,
                                          (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip, rect);
                        }
                    }
                }
                if (a.right_soft_clip > 0) {
                    if (plotSoftClipAsBlock) {
                        s = end;
                        width = (float) a.right_soft_clip;
                    } else {
                        s = end + a.right_soft_clip;
                        width = 0;
                    }
                    if (s < regionPixels) {
                        if (!pointLeft && plotPointedPolygons) {
                            drawRightPointedRectangle(canvas, pH, yScaledOffset, s, width, xScaling,
                                                      regionPixels, xOffset,
                                                      (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip, path,
                                                      pointSlop);
                        } else {
                            drawRectangle(canvas, pH, yScaledOffset, s, width, xScaling,
                                          xOffset, (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip, rect);
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
                            if (ins.length > opts.indel_length) {
                                if (regionLen < 500000) {  // line and text
                                    drawIns(canvas, Y, p, yScaling, xOffset, yOffset, textW, theme.insS,
                                            theme.fcIns, path, rect);
                                    text_ins.push_back(SkTextBlob::MakeFromString(indelChars, fonts.fonty));
                                    textX_ins.push_back(p - (textW / 2) + xOffset - 2);
                                    textY_ins.push_back(((Y + polygonHeight) * yScaling) + yOffset - textDrop);
                                } else {  // line only
                                    drawIns(canvas, Y, p, yScaling, xOffset, yOffset, xScaling, theme.insS,
                                            theme.fcIns, path, rect);
                                }
                            } else if (regionLen < 100000) {  // line only
                                drawIns(canvas, Y, p, yScaling, xOffset, yOffset, xScaling, theme.insS,
                                        theme.fcIns, path, rect);
                            }
                        }
                    }
                }

                // add mismatches
                if (regionLen > opts.snp_threshold && plotSoftClipAsBlock) {
                    continue;
                }
                size_t l_seq = a.delegate->core.l_qseq;
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
                if (regionLen <= opts.snp_threshold && !a.mismatches.empty()) {
                    for (auto &m: a.mismatches) {
                        int p = (m.pos - regionBegin) * xScaling;
                        if (0 < p && p < regionPixels) {
                            colorIdx = (l_qseq == 0) ? 10 : (m.qual > 10) ? 10 : m.qual;
                            float mms = xScaling * mmScaling;
                            width = (regionLen < 500000) ? ((1. > mms) ? 1. : mms) : xScaling;
                            rect.setXYWH(p + xOffset + mmPosOffset, yScaledOffset, width, pH);
                            canvas->drawRect(rect, theme.BasePaints[m.base][colorIdx]);
                        }
                    }
                }

                // add soft-clips
                if (!plotSoftClipAsBlock) {
                    uint8_t *ptr_seq = bam_get_seq(a.delegate);
                    uint8_t *ptr_qual = bam_get_qual(a.delegate);
                    if (a.right_soft_clip > 0) {
                        int pos = a.reference_end - regionBegin;
                        if (pos < regionLen && a.cov_end > regionBegin) {
                            int opLen = a.right_soft_clip;
                            for (int idx = l_seq - opLen; idx < l_seq; ++idx) {
                                float p = pos * xScaling;
                                if (0 <= p < regionPixels) {
                                    uint8_t base = bam_seqi(ptr_seq, idx);
                                    uint8_t qual = ptr_qual[idx];
                                    colorIdx = (l_qseq == 0) ? 10 : (qual > 10) ? 10 : qual;
                                    rect.setXYWH(p + xOffset + mmPosOffset, yScaledOffset, xScaling * mmScaling, pH);
                                    canvas->drawRect(rect, theme.BasePaints[base][colorIdx]);
                                    pos += 1;
                                } else {
                                    break;
                                }
                            }
                        }
                    }
                    if (a.left_soft_clip > 0) {
                        int opLen = a.left_soft_clip;
                        int pos = a.pos - regionBegin - opLen;
                        for (int idx = 0; idx < opLen; ++idx) {
                            float p = pos * xScaling;
                            if (0 <= p && p < regionPixels) {
                                uint8_t base = bam_seqi(ptr_seq, idx);
                                uint8_t qual = ptr_qual[idx];
                                colorIdx = (l_qseq == 0) ? 10 : (qual > 10) ? 10 : qual;
                                rect.setXYWH(p + xOffset + mmPosOffset, yScaledOffset, xScaling * mmScaling, pH);
                                canvas->drawRect(rect, theme.BasePaints[base][colorIdx]);
                                pos += 1;
                            } else if (p >= regionPixels) {
                                break;
                            }
                        }
                    }
                }
            }

            // draw text last
            for (int i = 0; i < text.size(); ++i) {
                canvas->drawTextBlob(text[i].get(), textX[i], textY[i], theme.tcDel);
            }
            for (int i = 0; i < text_ins.size(); ++i) {
                canvas->drawTextBlob(text_ins[i].get(), textX_ins[i], textY_ins[i], theme.tcIns);
            }
        }
    }

    void drawRef(const Themes::IniOptions &opts, const std::vector<Segs::ReadCollection> &collections,
                  SkCanvas *canvas, const Themes::Fonts &fonts, size_t nbams) {

        SkRect rect;
        SkPaint faceColor;
        const Themes::BaseTheme &theme = opts.theme;
        float offset = 0;
        float h = (opts.dimensions.y / nbams) * 0.03;
        float textW = fonts.textWidths[0];
        float minLetterSize = opts.dimensions.x / textW;
        for (auto &cl: collections) {
            int size = cl.region.end - cl.region.start;
            float xScaling = cl.xScaling;
            const char *ref = cl.region.refSeq;
            if (ref == nullptr) {
                continue;
            }
            float i = cl.xOffset;
            if (size < minLetterSize) {
                float v = (xScaling - textW) / 2;
                while (*ref) {
                    switch ((unsigned int)*ref) {
                        case 65: faceColor = theme.fcA; break;
                        case 67: faceColor = theme.fcC; break;
                        case 71: faceColor = theme.fcG; break;
                        case 78: faceColor = theme.fcN; break;
                        case 84: faceColor = theme.fcT; break;
                        case 97: faceColor = theme.fcA; break;
                        case 99: faceColor = theme.fcC; break;
                        case 103: faceColor = theme.fcG; break;
                        case 110: faceColor = theme.fcN; break;
                        case 116: faceColor = theme.fcT; break;
                    }
                    canvas->drawTextBlob(SkTextBlob::MakeFromText(ref, 1, fonts.fonty, SkTextEncoding::kUTF8), i + v, offset = h, faceColor);
                    i += xScaling;
                    ++ref;
                }
            } else if (size < 20000) {
                while (*ref) {
                    rect.setXYWH(i, offset, xScaling, h);
                    switch ((unsigned int)*ref) {
                        case 65: canvas->drawRect(rect, theme.fcA); break;
                        case 67: canvas->drawRect(rect, theme.fcC); break;
                        case 71: canvas->drawRect(rect, theme.fcG); break;
                        case 78: canvas->drawRect(rect, theme.fcN); break;
                        case 84: canvas->drawRect(rect, theme.fcT); break;
                        case 97: canvas->drawRect(rect, theme.fcA); break;
                        case 99: canvas->drawRect(rect, theme.fcC); break;
                        case 103: canvas->drawRect(rect, theme.fcG); break;
                        case 110: canvas->drawRect(rect, theme.fcN); break;
                        case 116: canvas->drawRect(rect, theme.fcT); break;
                    }
                    i += xScaling;
                    ++ref;
                }
            }
            // todo add pixelHeight to offset
        }
    }
}