//
// Created by Kez Cleal on 12/08/2022.
//

#pragma once

#include <GLFW/glfw3.h>
#include <string>
#include <utility>
#include <vector>

#include "htslib/sam.h"

#define SK_GL
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"

#include "BS_thread_pool.h"
#include "ankerl_unordered_dense.h"
#include "hts_funcs.h"
#include "utils.h"
#include "segments.h"
#include "themes.h"


namespace Drawing {

    void drawCoverage(const Themes::IniOptions &opts, std::vector<Segs::ReadCollection> &collections,
                      SkCanvas *canvas, const Themes::Fonts &fonts, float covY, float refSpace, const float gap);

    void drawCollection(const Themes::IniOptions &opts, Segs::ReadCollection &cl, SkCanvas* canvas,
                  float trackY, float yScaling, const Themes::Fonts &fonts, int linkOp, float refSpace, float pointSlop, float textDrop, float pH, float monitorScale);

    void drawRef(const Themes::IniOptions &opts, std::vector<Utils::Region> &regions, int fb_width,
                 SkCanvas *canvas, const Themes::Fonts &fonts, float refSpace, float nRegions, float gap, float monitorScale, bool scale_bar);

    void drawBorders(const Themes::IniOptions &opts, float fb_width, float fb_height,
                     SkCanvas *canvas, size_t nregions, size_t nbams, float trackY, float covY, int nTracks,
                     float totalTabixY, float refSpace, float gap, float totalCovY);

    void drawLabel(const Themes::IniOptions &opts, SkCanvas *canvas, SkRect &rect, Utils::Label &label, Themes::Fonts &fonts,
                   const ankerl::unordered_dense::set<std::string> &seenLabels, const std::vector<std::string> &sortedLabels);

    void drawTracks(Themes::IniOptions &opts, float fb_width, float fb_height,
                    SkCanvas *canvas, float totalTabixY, float tabixY, std::vector<HGW::GwTrack> &tracks,
                    std::vector<Utils::Region> &regions, const Themes::Fonts &fonts, float gap, float monitorScale, float sliderSpace,
                    float pointSlop);

    void drawChromLocation(const Themes::IniOptions &opts,
                           const Themes::Fonts &fonts,
                           const std::vector<Utils::Region> &regions,
                           const std::unordered_map<std::string, std::vector<Ideo::Band>> &ideogram, SkCanvas* canvas,
                           const faidx_t* fai, float fb_width, float fb_height, float monitorScale, float plot_gap);

}