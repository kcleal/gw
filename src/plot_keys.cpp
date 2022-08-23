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
    int GwPlot::registerKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
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
        if (captureText) {
            if (key == GLFW_KEY_ENTER) {
                captureText = false;
                processText = true;
                shiftPress = false;
                std::cout << "\n";
                return 0;
            }
            if (!commandHistory.empty()) {
                if (key == GLFW_KEY_UP && commandIndex > 0) {
                    commandIndex -= 1;
                    inputText = commandHistory[commandIndex];
                    std::cout << "\r" << inputText << std::flush;
                    return 1;
                } else if (key == GLFW_KEY_DOWN && commandIndex < commandHistory.size() - 1) {
                    commandIndex += 1;
                    inputText = commandHistory[commandIndex];
                    std::cout << "\r" << inputText << std::flush;
                    return 1;
                }
            }

            if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_LEFT_SUPER) {
                if (action == GLFW_PRESS) {
                    ctrlPress = true;
                }
            }
            if (ctrlPress && key == GLFW_KEY_V) {
                std::string string = glfwGetClipboardString(window);
                if (!string.empty()) {
                    inputText.append(string);
                    std::cout << "\r" << inputText << std::flush;
                }
            } else {  // character entry
                if (key == GLFW_KEY_SEMICOLON && inputText.size() == 1) {
                    return 1;
                } else if (key == GLFW_KEY_BACKSPACE) {
                    if (inputText.size() > 1) {
                        inputText.pop_back();
                        std::string emptyS(100, ' ');
                        std::cout << "\r" << emptyS << std::flush;
                        std::cout << "\r" << inputText << std::flush;
                    }
                }
                const char *letter = glfwGetKeyName(key, scancode);
                if (letter || key == GLFW_KEY_SPACE) {
                    if (key == GLFW_KEY_SPACE) {
                        inputText.append(" ");
                    } else if (key == GLFW_KEY_SEMICOLON && mods == GLFW_MOD_SHIFT) {
                        inputText.append(":");
                    } else {
                        if (mods == GLFW_MOD_SHIFT) { // uppercase
//                            char let = toupper(*letter);
//                            std::string str = toupper(*letter);
//                            inputText.append(str);
                        } else {
                            inputText.append(letter);
                        }

                    }
                    std::cout << "\r" << inputText << std::flush;
                }
            }
            return 1;
        }
        return 1;
    }

    void GwPlot::keyPress(GLFWwindow* window, int key, int scancode, int action, int mods) {
//        std::cout << key << std::endl;
//        std::cout << regions.size() << std::endl;

        int res = registerKey(window, key, scancode, action, mods);
    }
}