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

#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
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

#include "../include/argparse.h"
#include "../include/glob_cpp.hpp"
#define MINI_CASE_SENSITIVE
#include "../include/ini.h"
#include "utils.h"
#include "../include/export_definitions.h"


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

    class BaseTheme {
    public:
        BaseTheme();
        ~BaseTheme() = default;

        std::string name;
        // face colours
        SkPaint bgPaint, fcNormal, fcDel, fcDup, fcInvF, fcInvR, fcTra, fcIns, fcSoftClip, \
                fcA, fcT, fcC, fcG, fcN, fcCoverage, fcTrack;
        SkPaint fcNormal0, fcDel0, fcDup0, fcInvF0, fcInvR0, fcTra0, fcSoftClip0, fcBigWig;

        std::vector<SkPaint> mate_fc;
        std::vector<SkPaint> mate_fc0;

        // edge colours
        SkPaint ecMateUnmapped, ecSplit, ecSelected;

        // line widths
        float lwMateUnmapped, lwSplit, lwCoverage;

        // line colours and Insertion paint
        SkPaint lcJoins, lcCoverage, lcLightJoins, insF, insS, lcLabel, lcBright;

        // text colours
        SkPaint tcDel, tcIns, tcLabels, tcBackground;

        // Markers
        SkPaint marker_paint;

        uint8_t alpha, mapq0_alpha;

        std::array<std::array<SkPaint, 11>, 16> BasePaints;

        void setAlphas();

    };

    class IgvTheme: public BaseTheme {
        public:
            IgvTheme();
            ~IgvTheme() = default;
    };

    class DarkTheme: public BaseTheme {
        public:
            DarkTheme();
            ~DarkTheme() = default;
    };

    class SlateTheme: public BaseTheme {
    public:
        SlateTheme();
        ~SlateTheme() = default;
    };

    class EXPORT IniOptions {
    public:
        IniOptions();
        ~IniOptions() {};

        mINI::INIStructure myIni;
        ankerl::unordered_dense::map<int, std::string> shift_keymap;
        BaseTheme theme;
        Utils::Dims dimensions, number;
        std::string genome_tag;
        std::string theme_str, font_str, parse_label, labels, link, dimensions_str, number_str, ini_path, outdir;
        std::string menu_level, control_level, previous_level;
        MenuTable menu_table;
        bool editing_underway;
        int canvas_width, canvas_height;
        int indel_length, ylim, split_view_size, threads, pad, link_op, max_coverage, max_tlen;
        bool no_show, log2_cov, tlen_yscale, expand_tracks, vcf_as_tracks, sv_arcs;
        float scroll_speed, tab_track_height;
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
        int print_screen;
        int delete_labels;
        int enter_interactive_mode;
        int repeat_command;
        int start_index;
        int soft_clip_threshold, small_indel_threshold, snp_threshold, variant_distance, low_memory;
        int edge_highlights;
        int font_size;

        bool readIni();
        static std::filesystem::path writeDefaultIni(std::filesystem::path &homedir, std::filesystem::path &home_config, std::filesystem::path &gwIni);
        void getOptionsFromIni();

    };

    class Fonts {
    public:
        Fonts();
        ~Fonts() = default;
        int fontTypefaceSize;
        float fontSize, fontHeight, fontMaxSize, overlayWidth, overlayHeight;
        SkRect rect;
        SkPath path;
        sk_sp<SkTypeface> face;
        SkFont fonty, overlay;
        float textWidths[10];  // text size is scaled between 10 values to try and fill a read

        void setTypeface(std::string &fontStr, int size);
        void setOverlayHeight(float yScale);
        void setFontSize(float yScaling, float yScale);
    };

}
