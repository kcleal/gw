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

    constexpr std::array<const char*, 38> commandToolTip = {"ylim", "var", "track-height", "tlen-y", "tiles", "tags", "tab-track-height", "sort", "soft-clips", "save", "sam", "remove",
                                                            "refresh", "online", "name", "mods", "mismatches", "mate", "mate add", "log2-cov", "load", "link", "line", "insertions", "indel-length", "ideogram",
                                                            "grid", "font-size", "find", "filter", "expand-tracks", "edges", "cov",  "count", "alignments", "add", "min-junction-reads", "ref-complement"};

    // Every command offered by the command bar, in the order listed
    constexpr std::array<const char*, 61> commandList = {
        "add", "alignments", "clear-markers", "clear-roi", "colour", "count", "cov", "edges", "expand-tracks", "filter",
        "find", "font-size", "goto", "grid", "header", "help", "ideogram", "indel-length", "insertions", "introns",
        "labels", "line", "link", "load", "log2-cov", "marker", "mate", "mate add", "min-junction-reads", "mismatches",
        "mods", "move-bam", "move-track", "name", "online", "quit", "ref-complement", "refresh", "remove", "remove-filter", "remove-marker",
        "remove-roi", "roi", "sam", "save", "settings", "snapshot", "soft-clips", "sort", "tab-track-height", "tags",
        "theme", "tiles", "tlen-y", "track-height", "translate", "translate_code", "translate_frame", "translate_strand",
        "var", "ylim"};

    constexpr std::array<const char*, 18> exec = {"alignments", "cov", "count", "edges", "expand-tracks", "insertions", "line", "log2-cov", "mate", "mate add", "mismatches", "mods", "tags", "soft-clips", "sam", "ref-complement", "refresh", "tlen-y"};

    int getCommandSwitchValue(Themes::IniOptions &opts, std::string &cmd_s, bool &drawLine);

    // Cached settings preview uses a file-independent example and production drawing.
    sk_sp<SkImage> renderThemePreview(const Themes::IniOptions& opts, int width, int height);
    void clearThemePreview();

    // p_open is set to false when the window is closed. The caller restores the view.
    // plot also supplies the context for loading genomes in the Genomes tab.
    void drawImGuiSettings(Manager::GwPlot* plot, bool *p_open, bool &redraw, bool *drawLine = nullptr);

    // Command dialog opened by the bubble button. Provides an intuitive UI for
    // navigation, sorting, filtering and common actions.
    void drawImGuiCommandDialog(Manager::GwPlot* plot, bool* p_open, bool& redraw);

    // Top-left hamburger menu with Open, Commands, and Settings.
    void drawImGuiTopMenu(Manager::GwPlot* plot, float gap, float overlayHeight, bool& redraw);

    // File open dialog triggered by the Open button.
    void drawImGuiOpenDialog(Manager::GwPlot* plot, bool* p_open, bool& redraw);

    // Invisible click areas over Skia data labels; click to edit label text.
    void drawImGuiAlignmentScrollbars(Manager::GwPlot* plot);
    void drawImGuiYlimBox(Manager::GwPlot* plot);
    void drawImGuiCursorLine(Manager::GwPlot* plot);
    void drawImGuiTrackSeparators(Manager::GwPlot* plot);
    void drawImGuiDataLabels(Manager::GwPlot* plot, float gap, float overlayHeight, bool& redraw);

    // Help dialog with command reference and demo data loader.
    void drawImGuiHelpDialog(Manager::GwPlot* plot, bool* p_open, bool& redraw);

    // Close-file dialog: lists open BAM/CRAM, track, and variant files for removal.
    void drawImGuiCloseDialog(Manager::GwPlot* plot, bool* p_open, bool& redraw);

    // Ask-before-close confirmation for destructive track/file close actions.
    // 'pending' holds the item's display name; set it where the user triggers the
    // action and call each frame. Returns true once when the user confirms.
    bool confirmItemClose(const char* title, const char* what, std::string& pending, bool& redraw);

    void drawImGuiTrackManager(Manager::GwPlot* plot, bool* p_open, bool& redraw);

    void drawImGuiChromosomeList(Manager::GwPlot* plot, bool& redraw);

    // Show the in-window reference chooser (GW started without a reference). previousSession is
    // a session file to offer alongside the genomes, or empty.
    void openGenomeSelector(const std::string& previousSession);

    // With no reference open, open the online reference matching a BAM/CRAM/VCF file's header
    void openReferenceForData(Manager::GwPlot* plot, const std::string& path);

    // Info popup showing ANSI-colored text from click output near the click location.
    void drawImGuiInfoPopup(Manager::GwPlot* plot);

    // Coverage popup showing per-base counts when clicking the coverage track.
    void drawImGuiCovPopup(Manager::GwPlot* plot);

    // Track popup showing feature info when clicking a genomic track.
    void drawImGuiTrackPopup(Manager::GwPlot* plot);

    // Sequence popup: colored, wrapped, scrollable reference or amino-acid display.
    void drawImGuiSeqPopup(Manager::GwPlot* plot);

    // Label progress table: one row per tile/variant with current label and date.
    // Only shown in TILED mode; auto-hides when returning to alignment view.
    void drawImGuiLabelTableDialog(Manager::GwPlot* plot, bool& redraw);

    // Transient status overlay showing the most recent command output/errors
    // triggered from the ImGui UI. Terminal colors are preserved separately.
    void drawImGuiCommandStatus(Manager::GwPlot* plot);

    // Command bar along the bottom edge: text entry with a scrollable list of every command.
    // Shows the position under the pointer while empty.
    void drawImGuiCommandBar(Manager::GwPlot* plot, bool& redraw);

    // Keep Escape in the UI instead of also triggering legacy plot shortcuts.
    bool hasImGuiEscapeTarget();

}
