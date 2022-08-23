//
// Created by Kez Cleal on 23/08/2022.
//
#include <htslib/faidx.h>
#include <htslib/hfile.h>
#include <htslib/sam.h>

#include <cstdio>
#include <cstdlib>
#include <stdlib.h>
#include <string>
#include <vector>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#endif

#include "htslib/faidx.h"
#include "htslib/hfile.h"
#include "htslib/hts.h"
#include "htslib/sam.h"

#include <GLFW/glfw3.h>
#define SK_GL
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"

#include "drawing.h"
#include "hts_funcs.h"
#include "plot_manager.h"
#include "segments.h"
#include "themes.h"


namespace Manager {

    // keeps track of input commands
    int GwPlot::registerKey(GLFWwindow* window, int key, int scancode, int action) {
        if (action == GLFW_RELEASE) {
            if ((key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) && !captureText) {
                shiftPress = false;
            }
            ctrlPress = false;
            return 0;
        }

        if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) {
            shiftPress = true;
        } else if (shiftPress && GLFW_KEY_SEMICOLON && !captureText) {
            captureText = true;
            inputText.append(":");
            std::cout << "\r" << inputText << std::flush;
        } else {
            shiftPress = false;
        }

        return 1;
    }

    void GwPlot::keyPress(GLFWwindow* window, int key, int scancode, int action, int mods) {
//        std::cout << key << std::endl;
//        std::cout << regions.size() << std::endl;

        int res = registerKey(window, key, scancode, action);
    }
}