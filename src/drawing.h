//
// Created by Kez Cleal on 12/08/2022.
//

#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <utility>
#include <vector>


#include "../inc/BS_thread_pool.h"
#include "hts_funcs.h"
#include "../inc/robin_hood.h"
#include "utils.h"
#include "segments.h"
#include "themes.h"

#define SK_GL
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"


namespace Drawing {

    void drawCoverage(const Themes::IniOptions &opts, const std::vector<Segs::ReadCollection> &collections,
                      SkCanvas *canvas, const Themes::Fonts &fonts, const float covY, const float refSpace);

    void drawBams(const Themes::IniOptions &opts, const std::vector<Segs::ReadCollection> &collections, SkCanvas* canvas,
                  float yScaling, const Themes::Fonts &fonts, const Segs::linked_t &linked, int linkOp);

    void drawRef(const Themes::IniOptions &opts, const std::vector<Segs::ReadCollection> &collections,
                 SkCanvas *canvas, const Themes::Fonts &fonts, float refSpace, float nRegions);

    void drawBorders(const Themes::IniOptions &opts, const float fb_width, const float fb_height,
                     SkCanvas *canvas, const size_t nbams, const size_t nregions);
}