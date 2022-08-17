//
// Created by Kez Cleal on 12/08/2022.
//

#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <cstdio>
#include <cstdint>
#include <iostream>
#include <chrono>
#include <vector>
#include <utility>
#include <string>

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

    inline void drawRectangle(SkCanvas* canvas, float y, float start, float width, float lw, const SkPaint &edgeColor, float xScaling,
                       float yScaling, float maxX, float xOffset, float yOffset, const SkPaint &faceColor, SkRect &rect) {
        start *= xScaling;
        width *= xScaling;
//        if (start < 0) {
//            width += start;
//            start = 0;
//        }
//        if (start + width > maxX) {
//            width = maxX - start;
//        }
        rect.setXYWH(start + xOffset, y * yScaling + yOffset, width, polygonHeight * yScaling);
        canvas->drawRect(rect, faceColor);
    }

    // used when drawing with multiple threads
    inline void drawRectangle(SkCanvas* canvas, float y, float start, float width, float lw, const SkPaint &edgeColor, float xScaling,
                              float yScaling, float maxX, float xOffset, float yOffset, const SkPaint &faceColor, SkRect &rect,
                              std::vector<SkRect> &rects, std::vector<SkPaint> &rectPaints) {
        start *= xScaling;
        width *= xScaling;
        rect.setXYWH(start + xOffset, y * yScaling + yOffset, width, polygonHeight * yScaling);
        rects.push_back(rect);
        rectPaints.push_back(faceColor);
    }

    inline void drawLeftPointedRectangle(SkCanvas* canvas, float y0, float start, float width, float lw, const SkPaint &edgeColor, float xScaling,
                              float yScaling, float maxX, float xOffset, float yOffset, const SkPaint &faceColor, SkPath &path,
                              float slop) {
        start *= xScaling;
        width *= xScaling;
        float y = y0 * yScaling + yOffset;
        float ph = yScaling * polygonHeight;
        if (start < 0) {
            width += start;
            start = 0;
        }
        if (start + width > maxX) {
            width = maxX - start;
        }
        path.reset();
        pArr[0].set(start + xOffset, y);
        pArr[1].set(start - slop + xOffset, y + ph / 2);
        pArr[2].set(start + xOffset, y + ph);
        pArr[3].set(start + width + xOffset, y + ph);
        pArr[4].set(start + width + xOffset, y);
        path.addPoly(pArr, 5, true);
        canvas->drawPath(path, faceColor);
    }

    inline void drawRightPointedRectangle(SkCanvas* canvas, float y0, float start, float width, float lw, const SkPaint &edgeColor, float xScaling,
                                  float yScaling, float maxX, float xOffset, float yOffset, const SkPaint &faceColor, SkPath &path,
                                  float slop) {
        start *= xScaling;
        width *= xScaling;
        float y = y0 * yScaling + yOffset;
        float ph = yScaling * polygonHeight;
        if (start < 0) {
            width += start;
            start = 0;
        }
        if (start + width > maxX) {
            width = maxX - start;
        }
        path.reset();
        pArr[0].set(start + xOffset, y);
        pArr[1].set(start + xOffset, y + ph);
        pArr[2].set(start + width + xOffset, y + ph);
        pArr[3].set(start + width + slop + xOffset, y + ph / 2);
        pArr[4].set(start + width + xOffset, y);
        path.addPoly(pArr, 5, true);
        canvas->drawPath(path, faceColor);
    }

    void drawIns(SkCanvas* canvas, float y0, float start, float xScaling, float yScaling, float xOffset,
                 float yOffset, float textW, const SkPaint &sidesColor, const SkPaint &faceColor, SkPath &path, SkRect &rect) {

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
                  SkCanvas* canvas, float yScaling, const Themes::Fonts &fonts){

        SkRect rect;
        SkPath path;

        const Themes::BaseTheme &theme = opts.theme;
        std::vector< sk_sp<SkTextBlob> > text;
        std::vector< sk_sp<SkTextBlob> > text_ins;
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

            bool plotSoftClipAsBlock = regionLen > opts.soft_clip_threshold;
            bool plotPointedPolygons = regionLen < 50000;
            float pointSlop = ((regionLen / 100) * 0.25) * xScaling;

            SkPaint faceColor;
            SkPaint edgeColor;
            float lineWidth;

            float textDrop = (yScaling - fonts.fontHeight) / 2;
            std::cout << " " << textDrop << " " << fonts.fontHeight << " " << yScaling << std::endl;
            for (auto & a: cl.readQueue) {

                int Y = a.y;

                int mapq = a.delegate->core.qual;
                if (mapq == 0) {
                    switch (a.orient_pattern) {
                        case Segs::NORMAL: faceColor = theme.fcNormal0; break;
                        case Segs::DEL: faceColor = theme.fcDel0; break;
                        case Segs::INV_F: faceColor = theme.fcInvF0; break;
                        case Segs::INV_R: faceColor = theme.fcInvR0; break;
                        case Segs::DUP: faceColor = theme.fcDup0; break;
                        case Segs::TRA: faceColor = theme.mate_fc0[a.delegate->core.mtid % 49]; break;
                    }
                } else {
                    switch (a.orient_pattern) {
                        case Segs::NORMAL: faceColor = theme.fcNormal; break;
                        case Segs::DEL: faceColor = theme.fcDel; break;
                        case Segs::INV_F: faceColor = theme.fcInvF; break;
                        case Segs::INV_R: faceColor = theme.fcInvR; break;
                        case Segs::DUP: faceColor = theme.fcDup; break;
                        case Segs::TRA: faceColor = theme.mate_fc[a.delegate->core.mtid % 49]; break;
                    }
                }
                switch (a.edge_type) {
                    case 1: lineWidth = 0; edgeColor = faceColor; break;
                    case 2: lineWidth = 1; edgeColor = theme.ecSplit; break;
                    case 3: lineWidth = 1; edgeColor = theme.ecMateUnmapped; break;
                }

                bool pointLeft;
                if (plotPointedPolygons) {
                    pointLeft = (a.delegate->core.flag & 16) != 0;
                } else {
                    pointLeft = false;
                }

                size_t nBlocks = a.block_starts.size();

                float yh;
                float textW;
                for (size_t idx=0; idx < nBlocks; ++idx) {
                    int s = a.block_starts[idx];
                    if (s > regionEnd) {
                        break;
                    }
                    int e = a.block_ends[idx];
                    if (e < regionBegin) {
                        continue;
                    }
                    float width = e - s;
                    s -= regionBegin;
                    e -= regionBegin;

                    if (plotPointedPolygons) {
                        if (pointLeft) {
                            if (s > 0 && idx == 0 && a.left_soft_clip == 0) {
                                drawLeftPointedRectangle(canvas, Y, s, width, lineWidth, edgeColor, xScaling, yScaling,
                                                     regionPixels, xOffset, yOffset, faceColor, path, pointSlop);
                            } else {
                                drawRectangle(canvas, Y, s, width, lineWidth, edgeColor, xScaling, yScaling,
                                              regionPixels, xOffset, yOffset, faceColor, rect);
                            }
                        } else {
                            if (e < regionLen && idx == nBlocks - 1 && a.right_soft_clip == 0) {
                                drawRightPointedRectangle(canvas, Y, s, width, lineWidth, edgeColor, xScaling, yScaling,
                                                     regionPixels, xOffset, yOffset, faceColor, path, pointSlop);
                            } else {
                                drawRectangle(canvas, Y, s, width, lineWidth, edgeColor, xScaling, yScaling,
                                              regionPixels, xOffset, yOffset, faceColor, rect);
                            }
                        }
                    } else {
                        drawRectangle(canvas, Y, s, width, lineWidth, edgeColor, xScaling, yScaling,
                                      regionPixels, xOffset, yOffset, faceColor, rect);
                    }

                    // add lines and text between gaps
                    if (idx > 0) {
                        float lastEnd = a.block_ends[idx - 1] - regionBegin;
                        lastEnd = (lastEnd < 0) ? 0 : lastEnd;
                        int size  = s - lastEnd;
                        float delBegin = lastEnd * xScaling;
                        float delEnd = delBegin + (size * xScaling);
                        yh = (Y + polygonHeight * 0.5) * yScaling + yOffset;
                        if (size <= 0) { continue; }
                        if (regionLen < 500000 && size >= opts.indel_length ) { // line and text
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
                            textY.push_back((Y + polygonHeight) * yScaling - textDrop);
                            if (textBegin > delBegin) {
                                path.reset();
                                path.moveTo(delBegin + xOffset, yh);
                                path.lineTo(textBegin + xOffset, yh);
                                canvas->drawPath(path, theme.lcJoins);
                                path.reset();
                                path.moveTo(textEnd + xOffset, yh);
                                path.lineTo(delEnd + xOffset, yh);
                                canvas->drawPath(path, theme.lcJoins);
                            }
                        } else if (regionLen < 100000) { // line only
                            delEnd = std::min(regionPixels, delEnd);
                            path.reset();
                            path.moveTo(delBegin + xOffset, yh);
                            path.lineTo(delEnd + xOffset, yh);
                            canvas->drawPath(path, theme.lcJoins);
                        }
                    }

                }

                // add soft-clip blocks
                int start = a.delegate->core.pos - regionBegin;
                int end = a.reference_end - regionBegin;
                float width, s;

                if (a.left_soft_clip > 0) {
                    width = (plotSoftClipAsBlock) ? (float)a.left_soft_clip : 0;
                    if (pointLeft && plotPointedPolygons) {
                        drawLeftPointedRectangle(canvas, Y, start - a.left_soft_clip, width, lineWidth, edgeColor, xScaling, yScaling,
                                                 regionPixels, xOffset, yOffset,
                                                 (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip,
                                                 path, pointSlop);
                    } else {
                        drawRectangle(canvas, Y, start - a.left_soft_clip, width, lineWidth, edgeColor, xScaling, yScaling,
                                      regionPixels, xOffset, yOffset, (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip, rect);
                    }
                }
                if (a.right_soft_clip > 0) {
                    if (plotSoftClipAsBlock) {
                        s = end;
                        width = (float)a.right_soft_clip;
                    } else {
                        s = end + a.right_soft_clip;
                        width = 0;
                    }
                    if (!pointLeft && plotPointedPolygons) {
                        drawRightPointedRectangle(canvas, Y, s, width, lineWidth, edgeColor, xScaling, yScaling,
                                                 regionPixels, xOffset, yOffset, (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip, path, pointSlop);
                    } else {
                        drawRectangle(canvas, Y, s, width, lineWidth, edgeColor, xScaling, yScaling,
                                      regionPixels, xOffset, yOffset, (mapq == 0) ? theme.fcSoftClip0 : theme.fcSoftClip, rect);
                    }
                }

                // add insertions
                if (!a.any_ins.empty()) {
                    for (auto &ins : a.any_ins) {
                        float p = (ins.pos - regionBegin) * xScaling;
                        if (0 <= p < regionPixels) {
                            std::sprintf(indelChars, "%d", ins.length);
                            size_t sl = strlen(indelChars);
                            textW = fonts.textWidths[sl - 1];
                            if (ins.length > opts.indel_length) {
                                if (regionLen < 500000) {
                                    // line and text
                                    drawIns(canvas, Y, p, xScaling, yScaling, xOffset, yOffset, textW, theme.insS, theme.fcIns, path, rect);

                                    text_ins.push_back(SkTextBlob::MakeFromString(indelChars, fonts.fonty));
                                    textX_ins.push_back(p - (textW / 2) + xOffset);
                                    textY_ins.push_back(((Y + polygonHeight) * yScaling) + yOffset - textDrop);
                                } else {
                                    // line only
                                    drawIns(canvas, Y, p, xScaling, yScaling, xOffset, yOffset, xScaling, theme.insS, theme.fcIns, path, rect);

                                }
                            } else if (regionLen < 100000) {
                                // line only
                                drawIns(canvas, Y, p, xScaling, yScaling, xOffset, yOffset, xScaling, theme.insS, theme.fcIns, path, rect);
                            }

                        }
                    }
                }


            }

            // draw text last
            for (int i=0; i < text.size(); ++i) {
                canvas->drawTextBlob(text[i].get(), textX[i], textY[i], theme.tcDel);
            }
            for (int i=0; i < text_ins.size(); ++i) {
                canvas->drawTextBlob(text_ins[i].get(), textX_ins[i], textY_ins[i], theme.tcIns);
            }
        }



        return;
    }


}