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
#include "../include/unordered_dense.h"
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

    void drawMenu(SkCanvas *canvas, GrDirectContext *sContext, SkSurface *sSurface, Themes::IniOptions &opts, Themes::Fonts &fonts, float monitorScale, float fb_width, float fb_height, std::string inputText, int charIndex);

    void menuMousePos(Themes::IniOptions &opts, Themes::Fonts &fonts, float xPos, float yPos, float fb_height, float fb_width, bool *redraw);

    bool menuSelect(Themes::IniOptions &opts);

    bool navigateMenu(Themes::IniOptions &opts, int key, int action, std::string &inputText, int *charIndex, bool *captureText, bool *textFromSettings, bool *processText, std::string &reference_path);

    void processTextEntry(Themes::IniOptions &opts, std::string &inputText);

    std::vector<std::string> getCommandTip();

    constexpr std::array<const char*, 26> commandToolTip = {"ylim", "var", "tlen-y", "tags", "soft-clips", "snapshot", "sam", "remove",
                                                            "refresh", "online", "mismatches", "mate", "mate add", "log2-cov", "link", "line", "insertions", "indel-length",
                                                            "grid", "find", "filter", "expand-tracks", "edges", "cov",  "count", "add"};

    constexpr std::array<const char*, 15> exec = {"cov", "count", "edges", "expand-tracks", "insertions", "line", "log2-cov", "mate", "mate add", "mismatches", "tags", "soft-clips", "sam", "refresh", "tlen-y"};

    int getCommandSwitchValue(Themes::IniOptions &opts, std::string &cmd_s, bool &drawLine);

}