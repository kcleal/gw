//
// Created by Kez Cleal on 25/07/2022.
//

#pragma once

#include <iostream>
#ifdef __APPLE__
    #include <OpenGL/gl.h>
#endif

#include "htslib/faidx.h"
#include "htslib/hfile.h"
#include "htslib/hts.h"
#include "htslib/sam.h"


#include <GLFW/glfw3.h>
#include <string>
#include <utility>
#include <vector>

#include "../inc/BS_thread_pool.h"
#include "drawing.h"
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


namespace Manager {

    typedef ankerl::unordered_dense::map< const char *, std::vector<int>> map_t;
    typedef std::vector< map_t > linked_t;

    enum Show {
        SINGLE,
        TILED
    };

    class HiddenWindow {
    public:
        HiddenWindow () {};
        ~HiddenWindow () = default;
        GLFWwindow *window;
        void init(int width, int height);
    };

    /*
     * Deals with managing genomic data
     */
    class GwPlot {
    public:
        GwPlot(std::string reference, std::vector<std::string> &bams, Themes::IniOptions &opts, std::vector<Utils::Region> &regions);
        ~GwPlot();

        int vScroll;
        int fb_width, fb_height;
        bool drawToBackWindow;

        std::string reference;

        std::vector<std::string> bam_paths;
        std::vector<htsFile* > bams;
        std::vector<sam_hdr_t* > headers;
        std::vector<hts_idx_t* > indexes;
        std::vector<Utils::Region> regions;
        std::vector<std::vector<Utils::Region>> multiRegions;  // used for creating tiled regions
        std::vector<Segs::ReadCollection> collections;

        ankerl::unordered_dense::map< int, sk_sp<SkImage>> imageCache;

        Themes::IniOptions opts;
        Themes::Fonts fonts;

        faidx_t* fai;
        GLFWwindow* window;
        GLFWwindow* backWindow;

        Show mode;

        void init(int width, int height);

        void initBack(int width, int height);

        void setGlfwFrameBufferSize();

        void fetchRefSeqs();

        void clearCollections();

        void processBam();

        void setScaling();

        void setVariantSite(std::string &chrom, long start, std::string &chrom2, long stop);

        void appendVariantSite(std::string &chrom, long start, std::string &chrom2, long stop);

        int startUI(GrDirectContext* sContext, SkSurface *sSurface);

        void keyPress(GLFWwindow* window, int key, int scancode, int action, int mods);

        void drawSurfaceGpu(SkCanvas *canvas);

        void runDraw(SkCanvas *canvas);

        sk_sp<SkImage> makeImage();


    private:

        bool redraw;
        bool processed;
        bool calcScaling;

        std::string inputText;
        bool captureText, shiftPress, ctrlPress, processText;
        std::vector< std::string > commandHistory;
        int commandIndex;

        float totalCovY, covY, totalTabixY, tabixY, trackY, regionWidth, bamHeight;

        int samMaxY;

        float yScaling;

        linked_t linked;

        int blockStart, blockLen;

        void drawScreen(SkCanvas* canvas, GrDirectContext* sContext);

        void tileDrawingThread(SkCanvas* canvas, SkSurface *sSurface);

        void drawTiles(SkCanvas* canvas, GrDirectContext* sContext, SkSurface *sSurface);

        int registerKey(GLFWwindow* window, int key, int scancode, int action, int mods);

    };

    void imageToPng(sk_sp<SkImage> &img, std::string &outdir);

    struct VariantJob {
        std::string chrom;
        std::string chrom2;
        long start;
        long stop;
    };

}