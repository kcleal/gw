//
// Created by kez on 01/08/22.
//
#include "themes.h"
#include "glfw_keys.h"

namespace Themes {

    BaseTheme::BaseTheme() {
        fcCoverage.setColor(SkColorSetRGB(220, 220, 220));

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
            p.setColor(SkColorSetRGB(tmp[i][0], tmp[i][1], tmp[i][2]));
            mate_fc.push_back(p);
        }

        ecMateUnmapped.setColor(SkColorSetRGB(255, 0, 0));
        ecSplit.setColor(SkColorSetRGB(0, 0, 255));

        lwMateUnmapped = 0.5;
        lwSplit = 0.5;
        lwCoverage = 1;

        lcCoverage.setColor(SkColorSetRGB(192, 192, 192));

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

            fcIns.setColor(SkColorSetRGB(186, 85, 211));

            insF = fcIns;
            insF.setStyle(SkPaint::kFill_Style);

            insS = fcIns;
            insS.setStyle(SkPaint::kStroke_Style);
            insS.setStrokeWidth(3);

            for (size_t i=0; i < mate_fc.size(); ++i) {
                SkPaint p = mate_fc[i];
                mate_fc[i].setAlpha(alpha);
                p.setAlpha(mapq0_alpha);
                mate_fc0.push_back(p);
            }
            SkPaint p;
            for (int i=0; i==1; ++i) {
                p = fcA;
                p.setAlpha(base_qual_alpha[i]);
                APaint.push_back(p);
                p = fcT;
                p.setAlpha(base_qual_alpha[i]);
                TPaint.push_back(p);
                p = fcC;
                p.setAlpha(base_qual_alpha[i]);
                CPaint.push_back(p);
                p = fcG;
                p.setAlpha(base_qual_alpha[i]);
                GPaint.push_back(p);
                p = fcN;
                p.setAlpha(base_qual_alpha[i]);
                NPaint.push_back(p);
            }

    }

    IgvTheme::IgvTheme() {
        name = "igv";
        bgPaint.setColor(SkColorSetRGB(255, 255, 255));
        fcNormal.setColor(SkColorSetRGB(192, 192, 192));
        fcDel.setColor(SkColorSetRGB(220, 20, 60));
        fcDup.setColor(SkColorSetRGB(30, 144, 255));
        fcInvF.setColor(SkColorSetRGB(46, 139, 0));
        fcInvR.setColor(SkColorSetRGB(46, 139, 7));
        fcTra.setColor(SkColorSetRGB(255, 105, 180));
        fcSoftClip.setColor(SkColorSetRGB(0, 128, 128));
        fcA.setColor(SkColorSetRGB(0, 255, 127));
        fcT.setColor(SkColorSetRGB(255, 0, 0));
        fcC.setColor(SkColorSetRGB(0, 0, 255));
        fcG.setColor(SkColorSetRGB(205, 133, 63));
        fcN.setColor(SkColorSetRGB(128, 128, 128));
        lcJoins.setColor(SkColorSetRGB(0, 0, 0));
        tcDel.setColor(SkColorSetRGB(0, 0, 0));
        tcLabels.setColor(SkColorSetRGB(0, 0, 0));
        tcIns.setColor(SkColorSetRGB(255, 255, 255));
        marker_paint.setColor(SkColorSetRGB(0, 0, 0));
    }

    DarkTheme::DarkTheme() {
        name = "dark";
        bgPaint.setColor(SkColorSetRGB(42, 42, 42));
        fcNormal.setColor(SkColorSetRGB(95, 95, 95));
        fcDel.setColor(SkColorSetRGB(185, 25, 25));
        fcDup.setColor(SkColorSetRGB(24, 100, 198));
        fcInvF.setColor(SkColorSetRGB(49, 167, 118));
        fcInvR.setColor(SkColorSetRGB(49, 167, 0));
        fcTra.setColor(SkColorSetRGB(225, 185, 185));
        fcSoftClip.setColor(SkColorSetRGB(0, 128, 128));
        fcA.setColor(SkColorSetRGB(106, 216, 79));
        fcT.setColor(SkColorSetRGB(231, 49, 14));
        fcC.setColor(SkColorSetRGB(77, 155, 255));
        fcG.setColor(SkColorSetRGB(236, 132, 19));
        fcN.setColor(SkColorSetRGB(128, 128, 128));
        lcJoins.setColor(SkColorSetRGB(142, 142, 142));
        tcDel.setColor(SkColorSetRGB(227, 227, 227));
        tcLabels.setColor(SkColorSetRGB(0, 0, 0));
        tcIns.setColor(SkColorSetRGB(227, 227, 227));
        marker_paint.setColor(SkColorSetRGB(220, 220, 220));
    }

    IniOptions::IniOptions() {

        dimensions = {2048, 1024};
        fmt = "png";
        link = "None";
        link_op = 0;
        number = {3, 3};
        labels = "PASS,FAIL";

        indel_length = 10;
        ylim = 50;
        split_view_size = 10000;
        threads = 3;
        pad = 500;

        no_show = false;
        log2_cov = false;
        tlen_yscale = false;

        scroll_speed = 0.15;
        tab_track_height = 0.05;
        scroll_right = GLFW_KEY_RIGHT;
        scroll_left = GLFW_KEY_LEFT;
        scroll_down = GLFW_KEY_PAGE_DOWN;
        scroll_up = GLFW_KEY_PAGE_UP;
        next_region_view = GLFW_KEY_SLASH;
        zoom_out = GLFW_KEY_DOWN;
        zoom_in = GLFW_KEY_UP;
        cycle_link_mode = GLFW_KEY_L;
        print_screen = GLFW_KEY_PRINT_SCREEN;
        delete_labels = GLFW_KEY_DELETE;
        enter_interactive_mode = GLFW_KEY_ENTER;

        struct passwd *pw = getpwuid(getuid());
        std::string home(pw->pw_dir);
        if (Utils::is_file_exist(home + "/.gw.ini")) {
            readIni(home + "/.gw.ini");
        } else if (Utils::is_file_exist(home + "/.config/.gw.ini")) {
            readIni(home + "/.config/.gw.ini");
        } else if (Utils::is_file_exist(Utils::getExecutableDir() + "/.gw.ini")) {
            readIni(Utils::getExecutableDir() + "/.gw.ini");
        }
    }

    void IniOptions::readIni(std::string path) {
        robin_hood::unordered_map<std::string, int> key_table;
        Keys::getKeyTable(key_table);

        std::cout << "Loading " << path << std::endl;

        mINI::INIFile file(path);
        mINI::INIStructure myIni;
        file.read(myIni);

        if (myIni["general"]["theme"] == "dark") {
            theme = Themes::DarkTheme();
        } else {
            theme = Themes::IgvTheme();
        }

        dimensions = Utils::parseDimensions(myIni["general"]["dimensions"]);
        fmt = myIni["general"]["fmt"];

        std::string lnk = myIni["general"]["link"];
        if (lnk == "None") {
            link_op = 0;
        } else if (lnk == "SV" || lnk == "sv" || lnk == "Sv") {
            link_op = 1;
        } else if (lnk == "all" || lnk == "All") {
            link_op = 2;
        } else {
            std::cerr << "Link type not known [None/SV/A/all]\n";
            std::terminate();
        }
        link = myIni["general"]["link"];

        indel_length = std::stoi(myIni["general"]["indel_length"]);
        ylim = std::stoi(myIni["general"]["ylim"]);
        split_view_size = std::stoi(myIni["general"]["split_view_size"]);
        threads = std::stoi(myIni["general"]["threads"]);
        pad = std::stoi(myIni["general"]["pad"]);
        log2_cov = myIni["general"]["log2_cov"] == "true";
        scroll_speed = std::stof(myIni["general"]["scroll_speed"]);

        scroll_right = key_table[myIni["navigation"]["scroll_right"]];
        scroll_left = key_table[myIni["navigation"]["scroll_left"]];
        scroll_up = key_table[myIni["navigation"]["scroll_up"]];
        scroll_down = key_table[myIni["navigation"]["scroll_down"]];
        zoom_out = key_table[myIni["navigation"]["zoom_out"]];
        zoom_in = key_table[myIni["navigation"]["zoom_in"]];

        cycle_link_mode = key_table[myIni["interaction"]["cycle_link_mode"]];
        print_screen = key_table[myIni["interaction"]["print_screen"]];

        number = Utils::parseDimensions(myIni["labelling"]["number"]);
        labels = myIni["labelling"]["labels"];
        delete_labels = key_table[myIni["labelling"]["delete_labels"]];
        enter_interactive_mode = key_table[myIni["labelling"]["enter_interactive_mode"]];

        for (auto const& it2 :  myIni["genomes"]) {
            references[it2.first] = it2.second;
        }
        for (auto const& it2 : myIni["tracks"]) {
            tracks[it2.first].push_back(it2.second);
        }
    }


}