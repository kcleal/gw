
#include <htslib/faidx.h>
#include <htslib/hfile.h>
#include <htslib/sam.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef __APPLE__
    #include <OpenGL/gl.h>
#endif
#include <GLFW/glfw3.h>
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

    GwPlot::GwPlot(std::string reference, std::vector<std::string>& bam_paths, unsigned int threads, Themes::IniOptions& opt) {

        this->reference = reference;
        this->bam_paths = bam_paths;
        this->threads = threads;
        this->opts = opt;
        this->redraw = true;
        htsFile* f;
        for (auto& fn: this->bam_paths) {
            f = sam_open(fn.c_str(), "r");
            bams.push_back(f);
        }

        this->fai = fai_load(reference.c_str());
    }

    GwPlot::~GwPlot() {
    }

    int GwPlot::plotToScreen(SkCanvas* canvas, GrDirectContext* sContext) {

        drawScreen(canvas, sContext);
        GLFWwindow * wind = this->window.window;
//        while (true) {
//            if (glfwWindowShouldClose(wind)) {
//                break;
//            } else if (glfwGetKey(wind, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
//                break;
//            }
//            if (redraw) {
//                drawScreen(canvas, sContext);
//            }
//            std::cout << (glfwGetKey(wind, GLFW_KEY_ESCAPE) == GLFW_PRESS) << std::endl;
//
//        }

        while (true) {
            if (glfwWindowShouldClose(wind)) {
                break;
            } else if (glfwGetKey(wind, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                break;
            }

            glfwWaitEvents();

            drawScreen(canvas, sContext);
            SkPaint paint;
//            paint.setColor(SK_ColorWHITE);
//            canvas->drawPaint(paint);
            paint.setColor(SK_ColorBLUE);
            canvas->drawRect({100, 200, 300, 500}, paint);
            sContext->flush();

            glfwSwapBuffers(wind);
        }

        return 1;
    }

    void GwPlot::drawScreen(SkCanvas* canvas, GrDirectContext* sContext) {

        glfwGetFramebufferSize(window.window, &opts.canvas_width, &opts.canvas_height);
        std::cout << opts.canvas_width << std::endl;

//        SkPaint bg = opts.theme.bgPaint;
//        canvas->drawPaint(opts.theme.bgPaint);

        SkPaint paint;
        paint.setColor(SK_ColorWHITE);
        canvas->drawPaint(paint);
//        SkPaint paint;
//        paint.setColor(SkColorSetRGB(42, 42, 42));
//        paint.setColor(SK_ColorWHITE);
//        canvas->drawPaint(paint);
//        paint.setColor(SK_ColorBLUE);
//        canvas->drawRect({100, 200, 300, 500}, paint);

//        sContext->flush();
//        glfwSwapBuffers(window.window);

        redraw = false;


    }



}

