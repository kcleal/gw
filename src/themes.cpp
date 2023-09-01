//
// Created by kez on 01/08/22.
//
#include <filesystem>
#include "menu.h"
#include "themes.h"
#include "glfw_keys.h"
#include "../include/unordered_dense.h"

namespace Themes {

    BaseTheme::BaseTheme() {

        fcCoverage.setStyle(SkPaint::kStrokeAndFill_Style);
        fcCoverage.setStrokeWidth(0);

        std::vector<std::vector<int>> tmp = {{158, 1,   66},
                                             {179, 24,  71},
                                             {203, 51,  76},
                                             {220, 73,  75},
                                             {233, 92,  71},
                                             {244, 114, 69},
                                             {248, 142, 82},
                                             {252, 167, 94},
                                             {253, 190, 110},
                                             {253, 212, 129},
                                             {254, 228, 147},
                                             {254, 242, 169},
                                             {254, 254, 190},
                                             {244, 250, 174},
                                             {233, 246, 158},
                                             {213, 238, 155},
                                             {190, 229, 160},
                                             {164, 218, 164},
                                             {134, 206, 164},
                                             {107, 196, 164},
                                             {83,  173, 173},
                                             {61,  148, 183},
                                             {57,  125, 184},
                                             {76,  101, 172},
                                             {94,  79,  162},
                                             {255, 255, 229},
                                             {252, 254, 215},
                                             {249, 253, 200},
                                             {246, 251, 184},
                                             {237, 248, 178},
                                             {227, 244, 170},
                                             {216, 239, 162},
                                             {202, 233, 156},
                                             {187, 227, 149},
                                             {172, 220, 141},
                                             {155, 213, 135},
                                             {137, 205, 127},
                                             {119, 197, 120},
                                             {101, 189, 111},
                                             {82,  179, 102},
                                             {64,  170, 92},
                                             {55,  158, 84},
                                             {44,  144, 75},
                                             {34,  131, 66},
                                             {23,  122, 62},
                                             {11,  112, 58},
                                             {0,   103, 54},
                                             {0,   92,  50},
                                             {0,   79,  45},
                                             {0,   69,  41}};
        for (size_t i = 0; i < tmp.size(); ++i) {
            SkPaint p;
            p.setARGB(255, tmp[i][0], tmp[i][1], tmp[i][2]);
            mate_fc.push_back(p);
        }

        ecMateUnmapped.setARGB(255, 255, 0, 0);
        ecMateUnmapped.setStyle(SkPaint::kStroke_Style);
        ecMateUnmapped.setStrokeWidth(1);

        ecSplit.setARGB(255, 0, 0, 255);
        ecSplit.setStyle(SkPaint::kStroke_Style);
        ecSplit.setStrokeWidth(1);

        fcIns.setARGB(255, 156, 85, 201);

        lwMateUnmapped = 0.5;
        lwSplit = 0.5;
        lwCoverage = 1;

        lcCoverage.setARGB(255, 162, 192, 192);

        alpha = 204;
        mapq0_alpha = 102;

        marker_paint.setStyle(SkPaint::kStrokeAndFill_Style);
        marker_paint.setStrokeWidth(3);
    }

    void BaseTheme::setAlphas() {
            fcNormal0 = fcNormal;
            fcDel0 = fcDel;
            fcDup0 = fcDup;
            fcInvF0 = fcInvF;
            fcInvR0 = fcInvR;
            fcTra0 = fcTra;
            fcSoftClip0 = fcSoftClip;

            bgPaint.setStyle(SkPaint::kStrokeAndFill_Style);
            bgPaint.setStrokeWidth(2);

            fcNormal.setAlpha(alpha);
            fcDel.setAlpha(alpha);
            fcDup.setAlpha(alpha);
            fcInvF.setAlpha(alpha);
            fcInvR.setAlpha(alpha);
            fcTra.setAlpha(alpha);
            fcSoftClip.setAlpha(alpha);

            fcNormal0.setAlpha(mapq0_alpha);
            fcDel0.setAlpha(mapq0_alpha);
            fcDup0.setAlpha(mapq0_alpha);
            fcInvF0.setAlpha(mapq0_alpha);
            fcInvR0.setAlpha(mapq0_alpha);
            fcTra0.setAlpha(mapq0_alpha);
            fcSoftClip0.setAlpha(mapq0_alpha);

            fcTrack.setStyle(SkPaint::kStrokeAndFill_Style);
            fcTrack.setStrokeWidth(2);
            fcTrack.setAntiAlias(true);

            lcJoins.setStyle(SkPaint::kStroke_Style);
            lcJoins.setStrokeWidth(2);

            lcLightJoins.setStyle(SkPaint::kStroke_Style);
            lcLightJoins.setStrokeWidth(1);

            lcLabel.setStyle(SkPaint::kStroke_Style);
            lcLabel.setStrokeWidth(1);
            lcLabel.setAntiAlias(true);

            lcBright.setStyle(SkPaint::kStrokeAndFill_Style);
            lcBright.setStrokeWidth(1);
            lcBright.setAntiAlias(true);

            insF = fcIns;
            insF.setStyle(SkPaint::kFill_Style);

            insS = fcIns;
            insS.setStyle(SkPaint::kStroke_Style);
            insS.setStrokeWidth(4);

            marker_paint.setStyle(SkPaint::kStrokeAndFill_Style);
            marker_paint.setAntiAlias(true);
            marker_paint.setStrokeMiter(0.1);
            marker_paint.setStrokeWidth(0.5);

            for (size_t i=0; i < mate_fc.size(); ++i) {
                SkPaint p = mate_fc[i];
                mate_fc[i].setAlpha(alpha);
                p.setAlpha(mapq0_alpha);
                mate_fc0.push_back(p);
            }
            SkPaint p;
            // A==1, C==2, G==4, T==8, N==>8
            for (size_t i=0; i<11; ++i) {
                p = fcA;
                p.setAlpha(base_qual_alpha[i]);
                BasePaints[1][i] = p;

                p = fcT;
                p.setAlpha(base_qual_alpha[i]);
                BasePaints[8][i] = p;

                p = fcC;
                p.setAlpha(base_qual_alpha[i]);
                BasePaints[2][i] = p;

                p = fcG;
                p.setAlpha(base_qual_alpha[i]);
                BasePaints[4][i] = p;

                p = fcN;
                p.setAlpha(base_qual_alpha[i]);
                BasePaints[9][i] = p;
                BasePaints[10][i] = p;
                BasePaints[11][i] = p;
                BasePaints[12][i] = p;
                BasePaints[13][i] = p;
                BasePaints[14][i] = p;
                BasePaints[15][i] = p;
            }

    }

    IgvTheme::IgvTheme() {
        name = "igv";
        fcCoverage.setARGB(255, 175, 175, 175);
        fcTrack.setARGB(200, 0, 0, 0);
        bgPaint.setARGB(255, 255, 255, 255);
        fcNormal.setARGB(255, 182, 182, 182);
        fcDel.setARGB(255, 220, 20, 60);
        fcDup.setARGB(255, 30, 144, 255);
        fcInvF.setARGB(255, 46, 139, 0);
        fcInvR.setARGB(255, 46, 139, 7);
        fcTra.setARGB(255, 255, 105, 180);
        fcSoftClip.setARGB(255, 0, 128, 128);
        fcA.setARGB(255, 0, 215, 127);
        fcT.setARGB(255, 215, 0, 0);
        fcC.setARGB(255, 20, 20, 205);
        fcG.setARGB(255, 205, 133, 63);
        fcN.setARGB(255, 128, 128, 128);
        lcJoins.setARGB(255, 30, 30, 30);
        lcLightJoins.setARGB(255, 120, 120, 120);
        lcLabel.setARGB(255, 0, 0, 0);
        lcBright.setColor(SK_ColorBLACK);
        tcDel.setARGB(255, 0, 0, 0);
        tcLabels.setARGB(255, 0, 0, 0);
        tcIns.setARGB(255, 255, 255, 255);
        tcBackground.setARGB(255, 255, 255, 255);
        marker_paint.setARGB(255, 0, 0, 0);
        ecSelected.setARGB(255, 0, 0, 0);
        ecSelected.setStyle(SkPaint::kStroke_Style);
        ecSelected.setStrokeWidth(2);
    }

    DarkTheme::DarkTheme() {
        name = "dark";
        fcCoverage.setARGB(255, 95, 95, 105);
        fcTrack.setARGB(200, 227, 232, 255);
        bgPaint.setARGB(255, 10, 10, 20);
        fcNormal.setARGB(255, 90, 90, 95);
        fcDel.setARGB(255, 185, 25, 25);
        fcDup.setARGB(255, 24, 100, 198);
        fcInvF.setARGB(255, 49, 167, 118);
        fcInvR.setARGB(255, 49, 167, 0);
        fcTra.setARGB(255, 225, 185, 185);
        fcSoftClip.setARGB(255, 0, 128, 128);
        fcA.setARGB(255, 106, 186, 79);
        fcT.setARGB(255, 201, 49, 24);
        fcC.setARGB(255, 77, 125, 245);
        fcG.setARGB(255, 226, 132, 19);
        fcN.setARGB(255, 128, 128, 128);
        lcJoins.setARGB(255, 142, 142, 142);
        lcLightJoins.setARGB(255, 82, 82, 82);
        lcLabel.setARGB(255, 182, 182, 182);
        lcBright.setColor(SK_ColorWHITE);
        tcDel.setARGB(255, 227, 227, 227);
        tcLabels.setARGB(255, 0, 0, 0);
        tcIns.setARGB(255, 227, 227, 227);
        tcBackground.setARGB(255, 10, 10, 20);
        marker_paint.setARGB(255, 220, 220, 220);
        ecSelected.setARGB(255, 255, 255, 255);
        ecSelected.setStyle(SkPaint::kStroke_Style);
        ecSelected.setStrokeWidth(2);
    }

    IniOptions::IniOptions() {
        menu_level = "";
        menu_table = MAIN;
        theme_str = "dark";
        dimensions_str = "1366x768";
        dimensions = {1366, 768};
        link = "None";
        link_op = 0;
        number_str = "3x3";
        number = {3, 3};
        labels = "PASS,FAIL";
        canvas_width = 0;
        canvas_height = 0;
        indel_length = 10;
        ylim = 50;
        split_view_size = 10000;
        threads = 3;
        pad = 500;
        start_index = 0;

        soft_clip_threshold = 20000;
        small_indel_threshold = 100000;
        snp_threshold = 1000000;
        edge_highlights = 100000;

        max_coverage = 10000000;
        max_tlen = 2000;

        editing_underway = false;
        no_show = false;
        log2_cov = false;
        tlen_yscale = false;
        low_mem = false;

        scroll_speed = 0.15;
        tab_track_height = 0.05;
        scroll_right = GLFW_KEY_RIGHT;
        scroll_left = GLFW_KEY_LEFT;
        scroll_down = GLFW_KEY_PAGE_DOWN;
        scroll_up = GLFW_KEY_PAGE_UP;
        next_region_view = GLFW_KEY_RIGHT_BRACKET;
        previous_region_view = GLFW_KEY_LEFT_BRACKET;
        zoom_out = GLFW_KEY_DOWN;
        zoom_in = GLFW_KEY_UP;
        cycle_link_mode = GLFW_KEY_L;
        print_screen = GLFW_KEY_PRINT_SCREEN;
        delete_labels = GLFW_KEY_DELETE;
        enter_interactive_mode = GLFW_KEY_ENTER;
        find_alignments=GLFW_KEY_F;
    }

    void IniOptions::getOptionsFromIni() {

        ankerl::unordered_dense::map<std::string, int> key_table;

        Keys::getKeyTable(key_table);

        theme_str = myIni["general"]["theme"];
        if (theme_str == "dark") {
            theme = Themes::DarkTheme();
        } else {
            theme = Themes::IgvTheme();
        }
        dimensions_str = myIni["general"]["dimensions"];
        dimensions = Utils::parseDimensions(dimensions_str);

        std::string lnk = myIni["general"]["link"];
        link_op = 0;
        if (lnk == "none") {
        } else if (lnk == "sv") {
            link_op = 1;
        } else if (lnk == "all") {
            link_op = 2;
        } else {
            std::cerr << "Link type not known [none/sv/all] " << lnk << std::endl;
            std::exit(-1);
        }
        link = myIni["general"]["link"];

        indel_length = std::stoi(myIni["general"]["indel_length"]);
        ylim = std::stoi(myIni["general"]["ylim"]);
        split_view_size = std::stoi(myIni["general"]["split_view_size"]);
        threads = std::stoi(myIni["general"]["threads"]);
        pad = std::stoi(myIni["general"]["pad"]);
        log2_cov = myIni["general"]["coverage"] == "true";
        log2_cov = myIni["general"]["log2_cov"] == "true";
        scroll_speed = std::stof(myIni["general"]["scroll_speed"]);

        soft_clip_threshold = std::stoi(myIni["view_thresholds"]["soft_clip"]);
        small_indel_threshold = std::stoi(myIni["view_thresholds"]["small_indel"]);
        snp_threshold = std::stoi(myIni["view_thresholds"]["snp"]);
        edge_highlights = std::stoi(myIni["view_thresholds"]["edge_highlights"]);

        scroll_right = key_table[myIni["navigation"]["scroll_right"]];
        scroll_left = key_table[myIni["navigation"]["scroll_left"]];
        scroll_up = key_table[myIni["navigation"]["scroll_up"]];
        scroll_down = key_table[myIni["navigation"]["scroll_down"]];
        zoom_out = key_table[myIni["navigation"]["zoom_out"]];
        zoom_in = key_table[myIni["navigation"]["zoom_in"]];

        cycle_link_mode = key_table[myIni["interaction"]["cycle_link_mode"]];
        print_screen = key_table[myIni["interaction"]["print_screen"]];
        if (myIni["interaction"].has("find_alignments")) {
            find_alignments = key_table[myIni["interaction"]["find_alignments"]];
        }

        number_str = myIni["labelling"]["number"];
        number = Utils::parseDimensions(number_str);
        parse_label = myIni["labelling"]["parse_label"];

        labels = myIni["labelling"]["labels"];
        delete_labels = key_table[myIni["labelling"]["delete_labels"]];
        enter_interactive_mode = key_table[myIni["labelling"]["enter_interactive_mode"]];
    }

    void IniOptions::readIni() {

# if defined(_WIN32) || defined(_WIN64)
    const char *homedrive_c = std::getenv("HOMEDRIVE");
	const char *homepath_c = std::getenv("HOMEPATH");
    std::string homedrive(homedrive_c ? homedrive_c : "");
	std::string homepath(homepath_c ? homepath_c : "");
	std::string home = homedrive + homepath;
#else

        struct passwd *pw = getpwuid(getuid());
        std::string home(pw->pw_dir);
#endif
        std::filesystem::path path;
        std::filesystem::path homedir(home);
        std::filesystem::path gwini(".gw.ini");
        if (std::filesystem::exists(homedir / gwini)) {
            path = homedir / gwini;
        } else {
            std::filesystem::path home_config(".config");
            if (std::filesystem::exists(homedir / home_config / gwini)) {
                path = homedir / home_config / gwini;
            } else {
                std::filesystem::path exe_path (Utils::getExecutableDir());
                if (std::filesystem::exists(exe_path / gwini)) {
                    path = exe_path / gwini;
                } else {
                    theme = Themes::DarkTheme();
                    return;
                }
            }
        }
        ini_path = path.string();

        mINI::INIFile file(ini_path);
        file.read(myIni);

        getOptionsFromIni();

    }


    Fonts::Fonts (){
        rect = SkRect::MakeEmpty();
        path = SkPath();
        char fn[20] = "arial";
        face = SkTypeface::MakeFromName(fn, SkFontStyle::Normal());
        SkScalar ts = 16;
        fonty.setSize(ts);
        fonty.setTypeface(face);
        overlay.setSize(ts);
        overlay.setTypeface(face);
        fontMaxSize = 25; // in pixels
    }

    void Fonts::setFontSize(float maxHeight, float yScale) {
        const SkGlyphID glyphs[1] = {100};
        SkRect bounds[1];
        SkPaint paint1;
        const SkPaint* pnt = &paint1;
        SkScalar height;
        int font_size = 12 * yScale;
        fonty.setSize(font_size * yScale);
        fonty.getBounds(glyphs, 1, bounds, pnt);
        fontMaxSize = bounds[0].height();
        bool was_set = false;
        while (font_size > 8 * yScale) {
            fonty.setSize(font_size);
            fonty.getBounds(glyphs, 1, bounds, pnt);
            height = bounds[0].height();
            if (height < maxHeight*0.8) {
                was_set = true;
                break;
            }
            --font_size;
        }
        if (!was_set) {
            fontSize = 14 * yScale;
            fontHeight = fontMaxSize;
            for (auto &i : textWidths) {
                i = 0;
            }
        } else {
            fontSize = (float)font_size;
            fontHeight = height;
            SkScalar w = fonty.measureText("9", 1, SkTextEncoding::kUTF8);
            for (int i = 0; i < 10; ++i) {
                textWidths[i] = (float)w * (i + 1);
            }
        }
        overlay.setSize(14 * yScale);
        overlay.getBounds(glyphs, 1, bounds, pnt);
        overlayHeight = bounds[0].height();
        overlayWidth = overlay.measureText("9", 1, SkTextEncoding::kUTF8);
    }
}
