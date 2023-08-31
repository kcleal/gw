//
// Created by Kez Cleal on 26/08/2023.
//

#pragma once


#include <iostream>
#ifdef __APPLE__
#include <OpenGL/gl.h>
#endif

#include <filesystem>
#include <GLFW/glfw3.h>
#include <string>
#include <utility>
#include <vector>

#include "drawing.h"
#include "glfw_keys.h"
#include "hts_funcs.h"
#include "plot_manager.h"
#include "parser.h"
#include "../include/robin_hood.h"
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


namespace Menu {

    void drawMenu(SkCanvas *canvas, GrDirectContext *sContext, SkSurface *sSurface, Themes::IniOptions &opts, Themes::Fonts &fonts, float monitorScale, float fb_height, std::string &inputText, int *charIndex);

    void menuMousePos(Themes::IniOptions &opts, Themes::Fonts &fonts, float xPos, float yPos, float fb_height, float fb_width);

    bool menuSelect(Themes::IniOptions &opts);

    bool navigateMenu(Themes::IniOptions &opts, int key, int action, std::string &inputText, int *charIndex, bool *captureText);

    void processTextEntry(Themes::IniOptions &opts, std::string &inputText);

}