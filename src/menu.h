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
#include "ankerl_unordered_dense.h"
#include "utils.h"
#include "segments.h"
#include "themes.h"

#define SK_GL
#ifndef OLD_SKIA
    #include "include/gpu/ganesh/GrBackendSurface.h"
    #include "include/gpu/ganesh/GrDirectContext.h"
    #include "include/gpu/ganesh/gl/GrGLInterface.h"
#else
    #include "include/gpu/GrBackendSurface.h"
    #include "include/gpu/GrDirectContext.h"
    #include "include/gpu/gl/GrGLInterface.h"
#endif
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"


namespace Manager { class GwPlot; }  // forward declaration

namespace Menu {

    void drawMenu(SkCanvas *canvas, Themes::IniOptions &opts, Themes::Fonts &fonts, float monitorScale, float fb_width, float fb_height, std::string inputText, int charIndex);

    void menuMousePos(Themes::IniOptions &opts, Themes::Fonts &fonts, float xPos, float yPos, float fb_height, float fb_width, float monitorScale, bool *redraw);

    bool menuSelect(Themes::IniOptions &opts);

    bool navigateMenu(Themes::IniOptions &opts, int key, int action, std::string &inputText, int *charIndex, bool *captureText, bool *textFromSettings, bool *processText, std::string &reference_path);

    void processTextEntry(Themes::IniOptions &opts, std::string &inputText);

    std::vector<std::string> getCommandTip();

    constexpr std::array<const char*, 30> commandToolTip = {"ylim", "var", "tlen-y", "tags", "sort", "soft-clips", "save", "sam", "remove",
                                                            "refresh", "online", "mods", "mismatches", "mate", "mate add", "log2-cov", "load", "link", "line", "insertions", "indel-length",
                                                            "grid", "find", "filter", "expand-tracks", "edges", "cov",  "count", "alignments", "add"};

    constexpr std::array<const char*, 17> exec = {"alignments", "cov", "count", "edges", "expand-tracks", "insertions", "line", "log2-cov", "mate", "mate add", "mismatches", "mods", "tags", "soft-clips", "sam", "refresh", "tlen-y"};

    int getCommandSwitchValue(Themes::IniOptions &opts, std::string &cmd_s, bool &drawLine);

    // p_open is set to false when the window is closed.  The caller handles any
    // mode transitions (e.g. restoring GwPlot::mode to the previous view mode).
    // plot is used to actually load a genome when clicked in the Genomes tab.
    void drawImGuiSettings(Manager::GwPlot* plot, bool *p_open, bool &redraw, bool *drawLine = nullptr);

    // Command dialog opened by the bubble button. Provides an intuitive UI for
    // navigation, sorting, filtering and common actions.
    void drawImGuiCommandDialog(Manager::GwPlot* plot, bool* p_open, bool& redraw);

    // Top-left hamburger menu with Open, Commands, and Settings.
    void drawImGuiTopMenu(Manager::GwPlot* plot, float gap, float overlayHeight, bool& redraw);

    // File open dialog triggered by the Open button.
    void drawImGuiOpenDialog(Manager::GwPlot* plot, bool* p_open, bool& redraw);

    // Invisible click areas over Skia data labels; click to edit label text.
    void drawImGuiDataLabels(Manager::GwPlot* plot, float gap, float overlayHeight, bool& redraw);

    // Help dialog with command reference and demo data loader.
    void drawImGuiHelpDialog(Manager::GwPlot* plot, bool* p_open, bool& redraw);

    // Close-file dialog: lists open BAM/CRAM, track, and variant files for removal.
    void drawImGuiCloseDialog(Manager::GwPlot* plot, bool* p_open, bool& redraw);

    // Info popup showing ANSI-colored text from click output near the click location.
    void drawImGuiInfoPopup(Manager::GwPlot* plot);

    // Coverage popup showing per-base counts when clicking the coverage track.
    void drawImGuiCovPopup(Manager::GwPlot* plot);

    // Track popup showing feature info when clicking a genomic track.
    void drawImGuiTrackPopup(Manager::GwPlot* plot);

    // Reference sequence popup: colored, wrapped, scrollable sequence display.
    void drawImGuiRefPopup(Manager::GwPlot* plot);

    // Label progress table: one row per tile/variant with current label and date.
    // Only shown in TILED mode; auto-hides when returning to alignment view.
    void drawImGuiLabelTableDialog(Manager::GwPlot* plot, bool& redraw);

}