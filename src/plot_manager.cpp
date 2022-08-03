
#include <htslib/faidx.h>
#include <htslib/hfile.h>
#include <htslib/sam.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include <OpenGL/gl.h>
#include "GLFW/glfw3.h"
#define SK_GL
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"

#include "plot_manager.h"
#include "themes.h"


namespace Manager {

    GwPlot::GwPlot (const char* reference, std::vector<std::string>& bam_paths, unsigned int threads, Themes::IniOptions& opt) {

        this->reference_str = reference;
        this->bam_paths = bam_paths;
        this->threads = threads;
        this->opts = opt;

        htsFile* f;
        for (auto& fn: this->bam_paths) {
            f = sam_open(fn.c_str(), "r");
            bams.push_back(f);
        }

//        this->reference = fai_load(reference);
    }

    GwPlot::~GwPlot() {
    }


    SkiaWindow::~SkiaWindow() {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void SkiaWindow::init(int width, int height) {

        if (!glfwInit()) {
            std::cerr<<"ERROR: could not initialize GLFW3"<<std::endl;
            std::terminate();
        }

        glfwWindowHint(GLFW_STENCIL_BITS, 8);

        window = glfwCreateWindow(width, height, "Simple example", NULL, NULL);
        if (!window) {
            std::cerr<<"ERROR: could not create window with GLFW3"<<std::endl;
            glfwTerminate();
            std::terminate();
        }
        glfwMakeContextCurrent(window);

    }

    int SkiaWindow::pollWindow(SkCanvas* canvas, GrDirectContext* sContext) {
        while (true) {
            if (glfwWindowShouldClose(window)) {
                break;
            } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                break;
            }

            glfwWaitEvents();

            SkPaint paint;
            paint.setColor(SK_ColorWHITE);
            canvas->drawPaint(paint);
            paint.setColor(SK_ColorBLUE);
            canvas->drawRect({100, 200, 300, 500}, paint);
            sContext->flush();

            glfwSwapBuffers(window);
        }

        return 1;

    }

}

