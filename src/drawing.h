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
#ifdef OLD_SKIA
    #include "include/gpu/GrBackendSurface.h"
    #include "include/gpu/GrDirectContext.h"
    #include "include/gpu/gl/GrGLInterface.h"
#else
    #include "include/gpu/ganesh/GrBackendSurface.h"
    #include "include/gpu/ganesh/GrDirectContext.h"
    #include "include/gpu/ganesh/gl/GrGLInterface.h"
#endif
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

    struct drawContext {
        float fb_width;
        float fb_height;
        float covYh;
        float refSpace;
        float sliderSpace;
        float gap;
        float monitorScale;
        float trackY;
        float covY;
        float yScaling;
        int linkOp;
        float totalTabixY;
        float pointSlop;
        float textDrop;
        float pH;
        size_t nRegions;
        size_t nbams;
        size_t nTracks;
        bool scale_bar;
        float totalCovY;
        float topMenuSpace;
        float overlayHeight;
        bool drawLocation;
    };

    void drawCoverage(const Themes::IniOptions &opts, std::vector<Segs::ReadCollection> &collections,
                      SkCanvas * const canvas, const Themes::Fonts &fonts,
                      std::vector<std::string> &bam_paths, const drawContext& ctx);

    void drawCollection(const Themes::IniOptions &opts, Segs::ReadCollection &cl,
                  SkCanvas *const canvas, const Themes::Fonts &fonts,
                  std::vector<std::string> &bam_paths, const drawContext& ctx);

    void drawRef(const Themes::IniOptions &opts,
                 std::vector<Utils::Region> &regions,
                 SkCanvas *const canvas, const Themes::Fonts &fonts, const drawContext& ctx);

    void drawBorders(const Themes::IniOptions &opts,
                     SkCanvas *const canvas, std::vector<HGW::GwTrack> &tracks, const drawContext& ctx);

    void drawLabel(const Themes::IniOptions &opts, SkCanvas *const canvas, SkRect &rect, Utils::Label &label, const Themes::Fonts &fonts,
              const ankerl::unordered_dense::set<std::string> &seenLabels, const std::vector<std::string> &srtLabels);


    void drawTracks(Themes::IniOptions &opts, SkCanvas *const canvas, std::vector<HGW::GwTrack> &tracks,
                    std::vector<Utils::Region> &regions, const Themes::Fonts &fonts, const drawContext& ctx,
                    std::vector<Segs::ReadCollection>* collections = nullptr);
        
    void drawChromLocation(const Themes::IniOptions &opts,
                           const Themes::Fonts &fonts,
                           std::vector<Utils::Region> &regions,
                           const std::unordered_map<std::string, std::vector<Ideo::Band>> &ideogram,
                           SkCanvas *const canvas, const drawContext& ctx);

}