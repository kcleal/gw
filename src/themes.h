//
// Created by Kez Cleal on 25/07/2022.
//
#pragma once

#include <array>
#include <filesystem>
#include <GLFW/glfw3.h>
#include <iostream>

#if defined(_WIN32)

#else
    #include <pwd.h>
#endif

#include <string>
#include <vector>
#include <unistd.h>
#include <unordered_map>

#define SK_GL

#if !defined(OLD_SKIA) || OLD_SKIA == 0
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
#include "include/core/SkData.h"
#include "include/core/SkStream.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSize.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPoint.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFont.h"
#include "include/core/SkTextBlob.h"

#include "argparse.h"
#include "glob_cpp.hpp"
#define MINI_CASE_SENSITIVE
#include "ini.h"
#include "utils.h"
#include "export_definitions.h"
#include "ideogram.h"
#include "gw_fonts.h"


namespace Themes {

    enum MenuTable {
        MAIN,
        GENOMES,
        TRACKS,
        GENERAL,
        VIEW_THRESHOLDS,
        NAVIGATION,
        INTERACTION,
        LABELLING,
        SHIFT_KEYMAP,
        CONTROLS,
    };

    constexpr float base_qual_alpha[11] = {51, 51, 51, 51, 51, 128, 128, 128, 128, 128, 255};

    /*
    bg - background, fc - face color, ec - edge color, lc - line color, tc - text color
    if an item has 0 at the end this is the color when mapq == 0
    */
    enum GwPaint {
        bgPaint, bgPaintTiled, bgMenu, fcNormal, fcDel, fcDup, fcInvF, fcInvR, fcTra, fcIns, fcSoftClip,
        fcA, fcT, fcC, fcG, fcN, fcCoverage, fcTrack, fcNormal0, fcDel0, fcDup0, fcInvF0, fcInvR0, fcTra0,
        fcSoftClip0, fcBigWig, fcRoi, mate_fc, mate_fc0, ecMateUnmapped, ecSplit, ecSelected,
        lcJoins, lcCoverage, lcLightJoins, lcGTFJoins, lcLabel, lcBright, tcDel, tcIns, tcLabels, tcBackground,
        fcMarkers, fc5mc, fc5hmc, fcOther
    };

    class EXPORT BaseTheme {
    public:
        BaseTheme();
        ~BaseTheme() = default;

        std::string name;
        // face colours
        SkPaint bgPaint, bgPaintTiled, bgMenu, fcNormal, fcDel, fcDup, fcInvF, fcInvR, fcTra, fcIns, fcSoftClip, \
                fcA, fcT, fcC, fcG, fcN, fcCoverage, fcTrack, fcRoi;
        SkPaint fcNormal0, fcDel0, fcDup0, fcInvF0, fcInvR0, fcTra0, fcSoftClip0, fcBigWig, fc5mc, fc5hmc, fcOther;

        std::array<SkPaint, 50> mate_fc;
        std::array<SkPaint, 50> mate_fc0;

        // edge colours
        SkPaint ecMateUnmapped, ecSplit, ecSelected;

        // line widths
        float lwMateUnmapped, lwSplit, lwCoverage;

        // line colours and Insertion paint
        SkPaint lcJoins, lcCoverage, lcLightJoins, lcLabel, lcBright, lcGTFJoins;

        // text colours
        SkPaint tcDel, tcIns, tcLabels, tcBackground;

        // Markers
        SkPaint fcMarkers;

        uint8_t alpha, mapq0_alpha;

        std::array<std::array<SkPaint, 11>, 16> BasePaints;
        std::array<std::array<SkPaint, 4>, 3> ModPaints;  // Only 3 at the moment, 5mc, 5hm, Other

        void setAlphas();
        void setPaintARGB(int paint_enum, int alpha, int red, int green, int blue);
        void getPaintARGB(int paint_enum, int& alpha, int& red, int& green, int& blue);
    };

    class EXPORT IgvTheme: public BaseTheme {
        public:
            IgvTheme();
            ~IgvTheme() = default;
    };

    class EXPORT DarkTheme: public BaseTheme {
        public:
            DarkTheme();
            ~DarkTheme() = default;
    };

    class EXPORT SlateTheme: public BaseTheme {
    public:
        SlateTheme();
        ~SlateTheme() = default;
    };

    class EXPORT IniOptions {
    public:
        IniOptions();
        ~IniOptions() {};

        mINI::INIStructure myIni, seshIni;
        std::unordered_map<std::string, std::string> shift_keymap;
        BaseTheme theme;
        Utils::Dims dimensions, number;
        std::string session_file;
        std::string genome_tag;
        std::string theme_str, font_str, parse_label, labels, link, dimensions_str, number_str, ini_path, outdir;
        std::string menu_level, control_level, previous_level;
        MenuTable menu_table;
        bool editing_underway;
        int canvas_width, canvas_height;
        int indel_length, ylim, split_view_size, threads, pad, link_op, max_coverage, max_tlen, mods_qual_threshold;
        bool no_show, log2_cov, tlen_yscale, expand_tracks, vcf_as_tracks, bed_as_tracks, sv_arcs, parse_mods,
            scale_bar, alignments, data_labels;
        float scroll_speed, read_y_gap;
        double tab_track_height;
        int scroll_right;
        int scroll_left;
        int scroll_down;
        int scroll_up;
        int next_region_view;
        int previous_region_view;
        int zoom_out;
        int zoom_in;
        int cycle_link_mode;
        int find_alignments;

        int repeat_command;
        int start_index;
        int soft_clip_threshold, small_indel_threshold, snp_threshold, mod_threshold, variant_distance, low_memory;
        int edge_highlights;
        int font_size;

        bool readIni();
        static std::filesystem::path writeDefaultIni(std::filesystem::path &homedir, std::filesystem::path &home_config, std::filesystem::path &gwIni);
        void getOptionsFromIni();
        void getOptionsFromSessionIni(mINI::INIStructure& seshIni);
        void saveIniChanges();
        void setTheme(std::string &theme_str);
        void saveCurrentSession(std::string& genome_path, std::string& ideogram_path, std::vector<std::string>& bam_paths,
                                std::vector<std::string>& track_paths, std::vector<Utils::Region>& regions,
                                std::vector<std::pair<std::string, int>>& variant_paths_info,
                                std::vector<std::string>& commands, std::string output_session,
                                int mode, int window_x_pos, int window_y_pos, float monitorScale, int screen_width, int screen_height,
                                int sortReadsBy);

    };

    class EXPORT Fonts {
    public:
        Fonts();
        ~Fonts() = default;
        int fontTypefaceSize;
        float overlayWidth; // Includes white space between next character
        float overlayHeight, overlayCharWidth;  // No white space added
        SkRect rect;
        SkPath path;
        sk_sp<SkTypeface> face;
        SkFont overlay;
        float textWidths[10];  // text size is scaled between 10 values to try and fill a read

        void setTypeface(std::string &fontStr, int size);
        void setOverlayHeight(float yScale);
        float measureTextWidth(const std::string& text) const;
    };

    void readIdeogramFile(std::string file_path, std::unordered_map<std::string, std::vector<Ideo::Band>> &ideogram,
                          Themes::BaseTheme &theme);

    void readIdeogramData(const unsigned char *data, size_t size, std::unordered_map<std::string, std::vector<Ideo::Band>> &ideogram,
                          Themes::BaseTheme &theme, bool strip_chr);

}
