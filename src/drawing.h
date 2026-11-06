//
// Created by Kez Cleal on 12/08/2022.
//

#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
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

    void drawFrameImage(SkCanvas* canvas, const sk_sp<SkImage>& image);

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
        float tracksTop{0};  // canvas y of the first annotation track (GwPlot::tracksTop())
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
        const std::vector<Utils::Marker> *markers{nullptr};  // persistent markers, drawn on matching chromosomes
        std::string selectedIntronChrom;  // identity of the highlighted intron (empty = none)
        int selectedIntronStart = -1;
        int selectedIntronEnd = -1;
        int selectedIntronStrand = -2;
        std::string selectedFeatureChrom;  // identity of the highlighted gff exon/intron (empty = none)
        std::string selectedFeatureName;
        std::string selectedFeatureParent;
        int selectedFeatureStart = -1;
        int selectedFeatureEnd = -1;
        int selectedFeatureTrack = -1;
        std::string selectedBaseChrom;  // identity of the clicked reference base (empty = none)
        int selectedBasePos = -1;
        float translationTrackHeight{0};
        float translationButtonPanelWidth{0};
        bool show_translation{false};
        int translation_frame{0};
        bool translation_strand{true};
        bool translation_row_locked{false};
        bool drawLine{false};
        float mouseX{0};
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

    void drawTranslationTrack(const Themes::IniOptions &opts,
                              std::vector<Utils::Region> &regions,
                              SkCanvas *const canvas, const Themes::Fonts &fonts,
                              const drawContext& ctx,
                              int hover_frame, bool hover_active);

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

    // Vertical layout of the feature rows inside one annotation track. drawTracks() and
    // the click/hover handlers both use it, so a click resolves to the row drawn there.
    // Each row holds a block of height h and, when labels are shown, a label below it;
    // that content is centred within the row.
    struct TrackRowLayout {
        float top;        // canvas y of the first row
        float rowStep;    // height of each row
        float h;          // block height
        float contentPad; // offset from a row's top to its block's top
        bool labels;      // whether feature labels are drawn
    };

    inline TrackRowLayout trackRowLayout(float trackTop, float pxHeight, int nLevels,
                                         float overlayHeight, float gap, float monitorScale) {
        nLevels = std::max(nLevels, 1);
        const float pad = gap * 0.5f;
        TrackRowLayout L{};
        L.top = trackTop + pad;
        L.rowStep = std::fmax((pxHeight - pad - pad) / (float)nLevels, 1.0f);
        L.h = std::fmin(L.rowStep * 0.35f, 10 * monitorScale);
        L.labels = overlayHeight * (float)nLevels < pxHeight / 2;
        const float content = L.h + (L.labels ? overlayHeight + monitorScale * 2 : 0);
        L.contentPad = std::fmax((L.rowStep - content) * 0.5f, 0.0f);
        return L;
    }

    inline float trackRowBlockTop(const TrackRowLayout &L, int level) {
        return L.top + L.rowStep * (float)level + L.contentPad;
    }

    inline int trackRowAt(const TrackRowLayout &L, float y, int nLevels) {
        int level = (int)std::floor((y - L.top) / L.rowStep);
        return std::clamp(level, 0, std::max(nLevels, 1) - 1);
    }

    // Helpers for translation-track layout so the GLFW click handler and the
    // Skia renderer agree on where the AA text row is.
    inline float translationTrackHeight(const Themes::Fonts &fonts, float gap) {
        return fonts.overlayHeight * 4.0f + gap * 2.0f;
    }
    inline std::pair<float, float> aaTextRowBounds(float refSpace, const Themes::Fonts &fonts, float gap) {
        const float trackTop = refSpace - translationTrackHeight(fonts, gap);
        const float laneAreaTop = trackTop + gap * 1.5f;
        const float laneHeight = fonts.overlayHeight * 0.55f;
        const float laneSpacing = gap * 0.35f;
        const float laneAreaBottom = laneAreaTop + 3 * laneHeight + 2 * laneSpacing;
        const float textTop = laneAreaBottom + laneSpacing;
        return {textTop, textTop + fonts.overlayHeight};
    }

}