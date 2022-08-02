
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
        delete sContext;
        delete sSurface;
        if (window) {
            glfwDestroyWindow(window);
            glfwTerminate();
        }
    }

    void GwPlot::init_skia() {
        auto interface = GrGLMakeNativeInterface();
        sContext = GrDirectContext::MakeGL(interface).release();

        GrGLFramebufferInfo framebufferInfo;
        framebufferInfo.fFBOID = 0; // assume default framebuffer
//        // We are always using OpenGL and we use RGBA8 internal format for both RGBA and BGRA configs in OpenGL.
//        //(replace line below with this one to enable correct color spaces) framebufferInfo.fFormat = GL_SRGB8_ALPHA8;
        framebufferInfo.fFormat = GL_RGBA8;

        SkColorType colorType = kRGBA_8888_SkColorType;

        int width, height;
        glfwGetFramebufferSize(window, &width, &height);
        std::cout << width << " " << height << std::endl;

        GrBackendRenderTarget backendRenderTarget(width, height,
                                                  0, // sample count
                                                  0, // stencil bits
                                                  framebufferInfo);

        //(replace line below with this one to enable correct color spaces) sSurface = SkSurface::MakeFromBackendRenderTarget(sContext, backendRenderTarget, kBottomLeft_GrSurfaceOrigin, colorType, SkColorSpace::MakeSRGB(), nullptr).release();
        sSurface = SkSurface::MakeFromBackendRenderTarget(sContext, backendRenderTarget, kBottomLeft_GrSurfaceOrigin, colorType, nullptr, nullptr).release();
        if (sSurface == nullptr) {
            std::cerr << "sSurface could not be initialized\n";
            abort();
        }

    }


    void error_callback(int error, const char* description) {
        fputs(description, stderr);
    }

    void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
            glfwSetWindowShouldClose(window, GL_TRUE);
    }

    void GwPlot::createWindow (int width, int height) {

        glfwSetErrorCallback(error_callback);
        if (!glfwInit()) {
            exit(EXIT_FAILURE);
        }

//        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
//        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
//        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
//        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
//        //(uncomment to enable correct color spaces) glfwWindowHint(GLFW_SRGB_CAPABLE, GL_TRUE);
//        glfwWindowHint(GLFW_STENCIL_BITS, 0);
//        //glfwWindowHint(GLFW_ALPHA_BITS, 0);
//        glfwWindowHint(GLFW_DEPTH_BITS, 0);

        window = glfwCreateWindow(width, height, "Simple example", NULL, NULL);
        if (!window) {
            glfwTerminate();
            exit(EXIT_FAILURE);
        }
        glfwMakeContextCurrent(window);
//        glEnable(GL_FRAMEBUFFER_SRGB);

//        init_skia();

        glfwSwapInterval(1);
        glfwSetKeyCallback(window, key_callback);

        auto interface = GrGLMakeNativeInterface();
        sContext = GrDirectContext::MakeGL(interface).release();
        GrGLFramebufferInfo framebufferInfo;
        framebufferInfo.fFBOID = 0;
        framebufferInfo.fFormat = GL_RGBA8;
        SkColorType colorType = kRGBA_8888_SkColorType;

        glfwGetFramebufferSize(window, &width, &height);
        std::cout << width << " " << height << std::endl;

        GrBackendRenderTarget backendRenderTarget(width, height,
                                                  0, // sample count
                                                  0, // stencil bits
                                                  framebufferInfo);

        //(replace line below with this one to enable correct color spaces) sSurface = SkSurface::MakeFromBackendRenderTarget(sContext, backendRenderTarget, kBottomLeft_GrSurfaceOrigin, colorType, SkColorSpace::MakeSRGB(), nullptr).release();
        sSurface = SkSurface::MakeFromBackendRenderTarget(sContext, backendRenderTarget, kBottomLeft_GrSurfaceOrigin, colorType, nullptr, nullptr).release();


    }

    int GwPlot::pollWindow() {

        // Draw to the surface via its SkCanvas.
        SkCanvas* canvas = sSurface->getCanvas();

        while (!glfwWindowShouldClose(window)) {
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

