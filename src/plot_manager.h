//
// Created by Kez Cleal on 25/07/2022.
//

#pragma once

#include <iostream>
#ifdef __APPLE__
    #include <OpenGL/gl.h>
#endif
#include <string>
#include <vector>

#include <GLFW/glfw3.h>
#include "utils.h"
#include "themes.h"

#define SK_GL
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"


namespace Manager {

    /*
     * Deals with window functions
     */
    class SkiaWindow {
    public:
        SkiaWindow() {};
        ~SkiaWindow();

        GLFWwindow* window;

        void init(int width, int height);

        int pollWindow(SkCanvas* canvas, GrDirectContext* sContext);

    };

    /*
     * Deals with managing genomic data
     */
    class GwPlot {
    public:
        GwPlot(std::string reference, std::vector<std::string>& bams, unsigned int threads, Themes::IniOptions& opts);
        ~GwPlot();

        bool init;
        bool redraw;
        std::string reference;
        faidx_t* fai;

        std::vector<std::string> bam_paths;
        std::vector<htsFile* > bams;
        unsigned int threads;
        Themes::IniOptions opts;
        std::vector<Utils::Region> regions;

        SkiaWindow window;

        int plotToScreen(SkCanvas* canvas, GrDirectContext* sContext);

    private:
        void drawScreen(SkCanvas* canvas, GrDirectContext* sContext);
    };


}