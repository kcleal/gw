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

    /*
     * Deals with managing genomic data
     */
    class GwPlot {
    public:
        GwPlot(std::string reference, std::vector<std::string>& bams, Themes::IniOptions& opts, std::vector<Utils::Region>& regions);
        ~GwPlot();

        int vScroll;
        bool drawToBackWindow;

        std::string reference;

        std::vector<std::string> bam_paths;
        std::vector<htsFile* > bams;
        std::vector<sam_hdr_t* > headers;
        std::vector<hts_idx_t* > indexes;
        std::vector<Utils::Region> regions;
        std::vector<Segs::ReadCollection> collections;

        Themes::IniOptions opts;
        Themes::Fonts fonts;

        faidx_t* fai;
        GLFWwindow* window;
        GLFWwindow* backWindow;

        void init(int width, int height);

        void initBack(int width, int height);

        int startUI(SkCanvas* canvas, GrDirectContext* sContext);

        void keyPress(GLFWwindow* window, int key, int scancode, int action, int mods);

        void drawSurfaceGpu(SkCanvas *canvas);

        void savePng(sk_sp<SkImage> img, std::string &outdir);


    private:

        bool redraw;
        bool processed;
        bool calcScaling;

        std::string inputText;
        bool captureText, shiftPress, ctrlPress, processText;
        std::vector< std::string > commandHistory;
        int commandIndex;

        float totalCovY, covY, totalTabixY, tabixY, trackY, regionWidth, bamHeight;
        int fb_width, fb_height;
        int samMaxY;

        float yScaling;

        linked_t linked;

        void fetchRefSeqs();

        void drawScreen(SkCanvas* canvas, GrDirectContext* sContext);

        void setGlfwFrameBufferSize();

        void setScaling();

        void processBam();

        int registerKey(GLFWwindow* window, int key, int scancode, int action, int mods);

        void processCommand();

    };


}