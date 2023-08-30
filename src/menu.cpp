//
// Created by Kez Cleal on 26/08/2023.
//
#include <cstdlib>
#include <string>
#include <cstdio>
#include <filesystem>
#include <vector>
#include <unordered_map>

#ifdef __APPLE__
#include <OpenGL/gl.h>
#endif

#include "htslib/faidx.h"
#include "htslib/hts.h"
#include "htslib/sam.h"

#include <GLFW/glfw3.h>
#define SK_GL
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"
#include "include/core/SkDocument.h"

#include "drawing.h"
#include "glfw_keys.h"
#include "plot_manager.h"
#include "segments.h"
#include "../include/ini.h"
#include "../include/robin_hood.h"
#include "../include/termcolor.h"
#include "themes.h"
#include "utils.h"
#include "menu.h"


namespace Menu {

    void warnRestart() {
        std::cerr << termcolor::yellow << "\nPlease click the save button and restart gw to apply changes" << termcolor::reset << std::endl;
    }

    std::string niceText(std::string s) {
        if (s.length() == 0) {
            return "";
        }
        s[0] = std::toupper(s[0]);
        if (s.find("_") != std::string::npos) {
            s.replace(s.find("_"), 1, " ");
        }
        return s;
    }

    std::vector<std::string> mainHeadings() {
        return {"genomes", "tracks", "general", "view_thresholds", "navigation", "interaction", "labelling"};
    }

    std::vector<std::string> availableButtonsStr(Themes::MenuTable t) {
        switch (t) {
            case (Themes::MenuTable::CONTROLS): return {"close", "save"};
            case (Themes::MenuTable::MAIN): return {"close", "save"};
            case (Themes::MenuTable::GENOMES): return {"back", "close", "save", "add", "delete"};
            case (Themes::MenuTable::TRACKS): return {"back", "close", "save", "add", "delete"};
            default: return {"back", "close", "save"};
        }
    }

    std::string getMenuKey(Themes::MenuTable mt) {
        switch (mt) {
            case Themes::MenuTable::MAIN: return "Main menu";
            case Themes::MenuTable::GENOMES: return "genomes";
            case Themes::MenuTable::TRACKS: return "tracks";
            case Themes::MenuTable::GENERAL: return "general";
            case Themes::MenuTable::VIEW_THRESHOLDS: return "view_thresholds";
            case Themes::MenuTable::NAVIGATION: return "navigation";
            case Themes::MenuTable::INTERACTION: return "interaction";
            case Themes::MenuTable::LABELLING: return "labelling";
            case Themes::MenuTable::CONTROLS: return "Controls";
        }
        return "";
    }

    void drawMenu(SkCanvas *canvas, GrDirectContext *sContext, SkSurface *sSurface, Themes::IniOptions &opts, Themes::Fonts &fonts, float monitorScale,
                  std::string &inputText, int *charIndex) {
        SkRect rect;
        SkPath path;
        float pad = fonts.overlayHeight;
        float v_gap = 5;
        float control_box_h = 35;
        float y = v_gap;
        float x = v_gap;
        float m_width = 26 * fonts.overlayWidth;
        auto m_height = (float)(pad * 1.5);
        SkPaint bg;
        SkPaint menuBg;
        SkPaint tcMenu;
        if (opts.theme_str == "dark") {
            bg.setARGB(255, 15, 15, 25);
            menuBg.setARGB(255, 50, 50, 55);
        } else {
            bg.setARGB(255, 60, 60, 70);
            menuBg.setARGB(255, 100, 100, 105);
        }
        tcMenu.setARGB(255, 255, 255, 255);
        tcMenu.setStyle(SkPaint::kStrokeAndFill_Style);
        tcMenu.setAntiAlias(true);

        rect.setXYWH(0, 0, m_width + v_gap, 20000);
        canvas->drawRect(rect, bg);
        std::vector<std::string> btns = availableButtonsStr(opts.menu_table);
        float x2 = x;
        for (auto& b : btns) {
            rect.setXYWH(x2, y, control_box_h, control_box_h);
            if (b == opts.control_level) {
                canvas->drawRect(rect, opts.theme.fcDup);
            } else {
                canvas->drawRect(rect, menuBg);
            }
            if (b == "close") {
                tcMenu.setStrokeWidth(3);
                path.reset();
                path.moveTo(x2 + 10, y + 10);
                path.lineTo(x2 + control_box_h - 10, y + control_box_h - 10);
                path.moveTo(x2 + 10, y + control_box_h - 10);
                path.lineTo(x2 + control_box_h - 10, y + 10);
                canvas->drawPath(path, tcMenu);
                tcMenu.setStrokeWidth(0);
            } else if (b == "back") {
                path.reset();
                path.moveTo(x2 + control_box_h - 10, y + 10);
                path.lineTo(x2 + 10, y + (control_box_h/2));
                path.lineTo(x2 + control_box_h - 10, y + control_box_h - 10);
                canvas->drawPath(path, tcMenu);
            } else if (b == "save") {
                path.reset();
                path.moveTo(x2 + 8, y + 8);
                path.lineTo(x2 + 8, y + control_box_h - 8);
                path.lineTo(x2 + control_box_h - 8, y + control_box_h - 8);
                path.lineTo(x2 + control_box_h - 8, y + 12);
                path.lineTo(x2 + control_box_h - 12, y + 8);
                canvas->drawPath(path, tcMenu);
                rect.setXYWH(x2 + 12, y + 12, control_box_h - 26, 6);
                canvas->drawRect(rect, ((b == opts.control_level) ? opts.theme.fcDup : menuBg));
            } else if (b == "add") {
                tcMenu.setStrokeWidth(3);
                path.reset();
                path.moveTo(x2 + (control_box_h / 2), y + 10);
                path.lineTo(x2 + (control_box_h / 2), y + control_box_h - 10);
                path.moveTo(x2 + 10, y + (control_box_h / 2));
                path.lineTo(x2 + control_box_h - 10, y + (control_box_h / 2));
                canvas->drawPath(path, tcMenu);
                tcMenu.setStrokeWidth(0);
            } else if (b == "delete") {
                tcMenu.setStrokeWidth(3);
                path.reset();
                path.moveTo(x2 + 10, y + (control_box_h / 2));
                path.lineTo(x2 + control_box_h - 10, y + (control_box_h / 2));
                canvas->drawPath(path, tcMenu);
                tcMenu.setStrokeWidth(0);
            }
            x2 += control_box_h + v_gap;
        }

        std::string table_name = getMenuKey(opts.menu_table);

        if (opts.menu_table == Themes::MenuTable::MAIN) {
            y += control_box_h + v_gap + m_height;
            std::string h = "    " + niceText(table_name);
            sk_sp < SkTextBlob> txt = SkTextBlob::MakeFromString(h.c_str(), fonts.overlay);
            canvas->drawTextBlob(txt.get(), x*2, y, tcMenu);
            y += pad + v_gap;
            for (auto & heading : mainHeadings()) {
                rect.setXYWH(x, y, m_width, m_height);
                if (opts.menu_level == heading) {
                    canvas->drawRect(rect, opts.theme.fcDup);
                } else {
                    canvas->drawRect(rect, menuBg);
                }
                std::string h = "    " + niceText(heading);
                sk_sp < SkTextBlob> txt = SkTextBlob::MakeFromString(h.c_str(), fonts.overlay);
                canvas->drawTextBlob(txt.get(), x*2, y + pad, tcMenu);
                y += m_height + v_gap;
            }
        } else {
            y += control_box_h + v_gap + m_height;
            std::string h = "    " + niceText(table_name);
            sk_sp < SkTextBlob> txt = SkTextBlob::MakeFromString(h.c_str(), fonts.overlay);
            canvas->drawTextBlob(txt.get(), x*2, y, tcMenu);
            y += pad + v_gap;
            for (auto & heading : opts.myIni[table_name]) {
                rect.setXYWH(x, y, m_width, m_height);
                if (opts.menu_level == heading.first) {
                    canvas->drawRect(rect, opts.theme.fcDup);
                } else {
                    canvas->drawRect(rect, menuBg);
                }
                std::string h = "    " + heading.first;
                sk_sp < SkTextBlob> txt = SkTextBlob::MakeFromString(h.c_str(), fonts.overlay);
                canvas->drawTextBlob(txt.get(), x*2, y + pad, tcMenu);
                std::string value = heading.second;
                if (!opts.editing_underway || opts.menu_level != heading.first) {
                    sk_sp < SkTextBlob> txt_v = SkTextBlob::MakeFromString(value.c_str(), fonts.overlay);
                    canvas->drawTextBlob(txt_v.get(), x*2 + x + m_width, y + pad, opts.theme.tcDel);
                } else {
                    float txt_w = fonts.overlay.measureText(inputText.c_str(), inputText.size(), SkTextEncoding::kUTF8);
                    float to_cursor_width = fonts.overlay.measureText(inputText.substr(0, *charIndex).c_str(), *charIndex, SkTextEncoding::kUTF8);
                    float txt_start = x*2 + fonts.overlayWidth + m_width;
                    SkPaint box;
                    box.setColor(SK_ColorGRAY);
                    box.setStrokeWidth(monitorScale);
                    box.setAntiAlias(true);
                    box.setStyle(SkPaint::kStroke_Style);
                    rect.setXYWH(x*2 + m_width, y, txt_w + (fonts.overlayWidth * 2), m_height);
                    canvas->drawRoundRect(rect, 5, 5, opts.theme.bgPaint);
                    canvas->drawRoundRect(rect, 5, 5, box);
                    path.reset();
                    path.moveTo(txt_start + to_cursor_width, y + (fonts.overlayHeight * 0.2));
                    path.lineTo(txt_start + to_cursor_width, y + fonts.overlayHeight * 1.1);
                    canvas->drawPath(path, opts.theme.lcBright);
                    sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(inputText.c_str(), fonts.overlay);
                    canvas->drawTextBlob(blob.get(), txt_start, y + pad, opts.theme.tcDel);
                }
                y += m_height + v_gap;
            }
        }
    }

    void menuMousePos(Themes::IniOptions &opts, Themes::Fonts &fonts, float xPos, float yPos, float fb_height, float fb_width) {
        if (opts.menu_table == Themes::MenuTable::MAIN) {
            float pad = fonts.overlayHeight;
            auto m_height = (float)(pad * 2);
            float v_gap = 25;
            float y = v_gap;
            opts.menu_level = "";
            for (auto & heading : mainHeadings()) {
                if (xPos > 0 && xPos <= fb_width && y <= yPos && yPos <= y + m_height) {
                    opts.menu_level = heading;
                    break;
                }
                y += pad*2;
                if (y > fb_height) { break; }
            }
        }
    }

    bool menuSelect(Themes::IniOptions &opts) {
        if (opts.menu_level == "") {
            opts.menu_table = Themes::MenuTable::MAIN; return true;
        } else if (opts.menu_level == "genomes") {
            opts.menu_table = Themes::MenuTable::GENOMES; return true;
        } else if (opts.menu_level == "tracks") {
            opts.menu_table = Themes::MenuTable::TRACKS; return true;
        } else if (opts.menu_level == "general") {
            opts.menu_table = Themes::MenuTable::GENERAL; return true;
        } else if (opts.menu_level == "view_thresholds") {
            opts.menu_table = Themes::MenuTable::VIEW_THRESHOLDS; return true;
        } else if (opts.menu_level == "navigation") {
            opts.menu_table = Themes::MenuTable::NAVIGATION; return true;
        } else if (opts.menu_level == "interaction") {
            opts.menu_table = Themes::MenuTable::INTERACTION; return true;
        } else if (opts.menu_level == "labelling") {
            opts.menu_table = Themes::MenuTable::LABELLING; return true;
        }
        //
        else if (opts.menu_level == "controls") {
            if (opts.control_level == "close") {
                opts.menu_level = "";
                opts.menu_table = Themes::MenuTable::MAIN;
                return false;
            } else if (opts.control_level == "back") {
                opts.menu_level = "controls";
                opts.control_level = "close";
                opts.menu_table = Themes::MenuTable::MAIN;
                return true;
            } else if (opts.control_level == "save") {
                opts.menu_level = "";
                opts.control_level = "close";
                opts.menu_table = Themes::MenuTable::MAIN;
                std::cout << "Saved .gw.ini to " << opts.ini_path << std::endl;
                mINI::INIFile file(opts.ini_path);
                file.write(opts.myIni);
                return false;
            } else if (opts.control_level == "delete") {
                opts.menu_level = "";
                opts.control_level = "close";
                opts.myIni["genomes"].remove(opts.genome_tag);
                warnRestart();
                return true;
            }
        }
        //
        else if (!opts.editing_underway) {
            opts.editing_underway = true;
        } else {
            opts.editing_underway = false;
        }
        return true;
    }

    bool navigateMenu(Themes::IniOptions &opts, int key, int action, std::string &inputText, int *charIndex, bool *captureText) {
        /*
         * Notes:
         * string variables are used to keep track of mouse-over events, or keyboard selection events
         * int enums are used to keep track of what was clicked on (mouse or keyboard), or triggering events.
         */
        if (opts.ini_path.empty()) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " .gw.ini file could not be read. Please create one in your home directory, in your .config directory, or in the same folder as the gw executable" << std::endl;
            return false;
        }
        if (key == GLFW_KEY_DOWN || key == GLFW_KEY_UP) {
            std::vector<std::string> keys;
            int current_i = 0;
            int ik = -1;
            if (opts.menu_table == Themes::MenuTable::MAIN) {
                for (auto &ini_key: mainHeadings()) {
                    keys.push_back(ini_key);
                    if (opts.menu_level == keys.back()) {
                        ik = current_i;
                    }
                    current_i += 1;
                }
            } else {
                std::string table_name = getMenuKey(opts.menu_table);
                for (auto &ini_key: opts.myIni[table_name]) {
                    keys.push_back(ini_key.first);
                    if (opts.menu_level == keys.back()) {
                        ik = current_i;
                    }
                    current_i += 1;
                }
            }
            if (key == GLFW_KEY_DOWN) {
                ik = (ik >= (int) keys.size() - 1) ? (int) keys.size() - 1 : std::max(0, ik + 1);
            } else {
                ik = (ik <= -1) ? -1 : ik -= 1;
            }
            if (ik <= -1) {
                opts.menu_level = "controls";
                opts.control_level = Menu::availableButtonsStr(opts.menu_table)[0]; //"back";
            } else {
                opts.control_level = "";
                opts.menu_level = keys[ik];
            }

        } else if (opts.menu_level == "controls") {
            std::vector<std::string> btns = Menu::availableButtonsStr(opts.menu_table);
            int current_i = 0;
            for (auto &b : btns) {
                if (b == opts.control_level) {
                    break;
                }
                current_i += 1;
            }
            if (key == GLFW_KEY_RIGHT) {
                current_i += 1;
            } else if (key == GLFW_KEY_LEFT) {
                current_i -= 1;
            }
            current_i = std::max(0, std::min(current_i, (int)btns.size() - 1));
            opts.control_level = btns[current_i];
            if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                return Menu::menuSelect(opts);
            }

        } else if (action == GLFW_PRESS) {
            if (!opts.editing_underway && Themes::MenuTable::GENOMES && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) {
                opts.genome_tag = opts.menu_level;
                std::cout << "\nSelected " << opts.genome_tag << ": " << opts.myIni["genomes"][opts.genome_tag] << std::endl;
                return true;  // force right key for editing, enter is reserved for selecting

            }
            else if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                if (opts.editing_underway) {
                    opts.editing_underway = false;
                    return true;
                } else {
                    bool keep_alive = Menu::menuSelect(opts);
                    if (opts.editing_underway) {
                        inputText = opts.myIni[getMenuKey(opts.menu_table)][opts.menu_level];
                        *charIndex = inputText.size();
                        *captureText = true;
                    }
                    return keep_alive;
                }
            } else if (opts.menu_table == Themes::MenuTable::CONTROLS && key == GLFW_KEY_ESCAPE) {
                opts.menu_table = Themes::MenuTable::MAIN;
                opts.menu_level = "";
            } else if (opts.menu_table != Themes::MenuTable::MAIN && (key == GLFW_KEY_LEFT || key == GLFW_KEY_ESCAPE)) {
                opts.menu_table = Themes::MenuTable::MAIN;
                opts.menu_level = "";
            }
        }
        return true;
    }

    enum OptionKind {
        String, ThemeOption, LinkOption, FmtOption, Bool, Int, Float, IntByInt, Path, KeyboardKey
    };

    class Option {
    public:
        std::string name, value, table;
        OptionKind kind;
        std::vector<std::string> choices;
        Option(std::string name, OptionKind kind, std::string value, Themes::MenuTable mt) {
            this->name = name;
            this->kind = kind;
            this->value = value;
            this->table = getMenuKey(mt);
            if (kind == ThemeOption) {
                choices.insert(choices.end(), {"dark", "igv"});
            } else if (kind == LinkOption) {
                choices.insert(choices.end(), {"none", "sv", "all"});
            }  else if (kind == FmtOption) {
                choices.insert(choices.end(), {"png", "pdf"});
            }
        }
    };

    Option optionFromStr(std::string &name, Themes::MenuTable mt, std::string &value) {
        std::unordered_map<std::string, OptionKind> option_map;
        for (const auto& v : {"indel_length", "ylim", "split_view_size", "threads", "pad", "soft_clip", "small_indel", "snp", "edge_highlights"}) {
            option_map[v] = Int;
        }
        for (const auto& v : {"scroll_speed", "tabix_track_height"}) {
            option_map[v] = Float;
        }
        for (const auto& v : {"coverage", "log2_cov"}) {
            option_map[v] = Bool;
        }
        for (const auto& v : {"scroll_right", "scroll_left", "zoom_out", "zoom_in", "scroll_down", "scroll_up", "cycle_link_mode", "print_screen", "find_alignments", "delete_labels", "enter_interactive_mode"}) {
            option_map[v] = KeyboardKey;
        }
        if (mt == Themes::MenuTable::GENOMES) {
            return Option(name, Path, value, mt);
        } else if (option_map.find(name) != option_map.end()) {
            return Option(name, option_map[name], value, mt);
        } else if (name == "theme") {
            return Option(name, ThemeOption, value, mt);
        } else if (name == "link") {
            return Option(name, LinkOption, value, mt);
        } else if (name == "fmt") {
            return Option(name, FmtOption, value, mt);
        } else if (name == "dimensions" || name == "number") {
            return Option(name, IntByInt, value, mt);
        } else {
            return Option(name, String, value, mt);
        }
    }

    void applyIntOption(Option &new_opt, Themes::IniOptions &opts) {
        std::string::const_iterator it = new_opt.value.begin();
        while (it != new_opt.value.end() && std::isdigit(*it)) ++it;
        if (it != new_opt.value.end()) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " expected an integer number, instead of " << new_opt.value << std::endl;
        } else {
            int v = std::stoi(new_opt.value);
            if (new_opt.name == "indel_length") { opts.indel_length = v; }
            else if (new_opt.name == "ylim") { opts.ylim = v; }
            else if (new_opt.name == "split_view_size") { opts.split_view_size = v; }
            else if (new_opt.name == "threads") { opts.threads = v; }
            else if (new_opt.name == "pad") { opts.pad = v; }
            else if (new_opt.name == "soft_clip") { opts.soft_clip_threshold = v; }
            else if (new_opt.name == "small_indel") { opts.small_indel_threshold = v; }
            else if (new_opt.name == "snp") { opts.snp_threshold = v; }
            else if (new_opt.name == "edge_highlights") { opts.edge_highlights = v; }
            else { return; }
            opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
        }
    }

    void applyFloatOption(Option &new_opt, Themes::IniOptions &opts) {
        float v;
        try {
            v = std::stof(new_opt.value);
        } catch (...) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " expected float number, instead of " << new_opt.value << std::endl;
            return;
        }
        if (new_opt.name == "scroll_spped") { opts.scroll_speed = v; }
        else if (new_opt.name == "tabix_track_height") { opts.tab_track_height = v; }
        else { return; }
        opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
    }

    void applyBoolOption(Option &new_opt, Themes::IniOptions &opts) {
        std::unordered_map<std::string, bool> bool_keys;
        bool_keys = { {"1", true}, {"true", true}, {"True", true}, {"0", false}, {"false", false}, {"False", false} };
        if (bool_keys.find(new_opt.value) == bool_keys.end()) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " expected boolean (true/1 etc), instead of " << new_opt.value << std::endl;
            return;
        }
        bool v = bool_keys[new_opt.value];
        if (new_opt.name == "scroll_spped") { opts.scroll_speed = v; }
        else if (new_opt.name == "tabix_track_height") { opts.tab_track_height = v; }
        else { return; }
        opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
    }

    void applyKeyboardKeyOption(Option &new_opt, Themes::IniOptions &opts) {
        robin_hood::unordered_map<std::string, int> keys;
        Keys::getKeyTable(keys);
        if (keys.find(new_opt.value) == keys.end()) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " key not available." << std::endl;
            return;
        }
        int v = keys[new_opt.value];
        if (new_opt.name == "scroll_right") { opts.scroll_right = v; }
        else if (new_opt.name == "scroll_left") { opts.scroll_left = v; }
        else if (new_opt.name == "zoom_out") { opts.zoom_out = v; }
        else if (new_opt.name == "zoom_in") { opts.zoom_in = v; }
        else if (new_opt.name == "scroll_down") { opts.scroll_down = v; }
        else if (new_opt.name == "scroll_up") { opts.scroll_up = v; }
        else if (new_opt.name == "cycle_link_mode") { opts.cycle_link_mode = v; }
        else if (new_opt.name == "print_screen") { opts.print_screen = v; }
        else if (new_opt.name == "find_alignments") { opts.find_alignments = v; }
        else if (new_opt.name == "delete_labels") { opts.delete_labels = v; }
        else if (new_opt.name == "enter_interactive_mode") { opts.enter_interactive_mode = v; }
        else { return; }
        opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
    }

    void applyThemeOption(Option &new_opt, Themes::IniOptions &opts) {
        if (new_opt.value == "igv") {
            opts.theme = Themes::IgvTheme();
            opts.theme_str = "igv";
        } else if (new_opt.value == "dark") {
            opts.theme = Themes::DarkTheme();
            opts.theme_str = "dark";
        } else {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " theme must be 'igv' or 'dark'" << std::endl;
            return;
        }
        opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
        opts.theme.setAlphas();
    }

    void applyLinkOption(Option &new_opt, Themes::IniOptions &opts) {
        if (new_opt.value == "none") {
            opts.link_op = 0;
        } else if (new_opt.value == "sv") {
            opts.link_op = 1;

        } else if (new_opt.value == "all") {
            opts.link_op = 2;
        } else {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " theme must be one of (none, sv, all)" << std::endl;
            return;
        }
        opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
        opts.link = new_opt.value;
    }

    void applyFmtOption(Option &new_opt, Themes::IniOptions &opts) {
        if (new_opt.value != "png" && new_opt.value != "pdf") {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " fmt must be one of (png, pdf)" << std::endl;
            return;
        }
        opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
        opts.fmt = new_opt.value;
    }

    void applyIntByIntOption(Option &new_opt, Themes::IniOptions &opts) {
        try {
            Utils::Dims dims = Utils::parseDimensions(new_opt.value);
            opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
            if (new_opt.name == "number") {
                opts.number_str = new_opt.value;
                opts.number = dims;
            } else {
                opts.dimensions_str = new_opt.value;
                opts.dimensions = dims;
                warnRestart();
            }
        } catch (std::runtime_error) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " dimensions not understood" << std::endl;
        }
    }

    void applyPathOption(Option& new_opt, Themes::IniOptions& opts) {
        if (Utils::startsWith(new_opt.value, "http") || Utils::startsWith(new_opt.value, "ftp")) {
            opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
            warnRestart();
        } else {
            std::filesystem::path path = new_opt.value;
            if (std::filesystem::exists(path)) {
                opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
                warnRestart();
            } else {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " local path does not exist: " << path << std::endl;
            }
        }
    }

    void applyStringOption(Option &new_opt, Themes::IniOptions &opts) {
        opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
        if (new_opt.name == "labels" || new_opt.name == "parse_label") {
            warnRestart();
        }
    }

    void processTextEntry(Themes::IniOptions &opts, std::string &inputText) {
        Option new_opt = optionFromStr(opts.menu_level, opts.menu_table, inputText);
        switch (new_opt.kind) {
            case (Int) : applyIntOption(new_opt, opts); break;
            case (Float) : applyFloatOption(new_opt, opts); break;
            case (Bool) : applyBoolOption(new_opt, opts); break;
            case (KeyboardKey) : applyKeyboardKeyOption(new_opt, opts); break;
            case (ThemeOption) : applyThemeOption(new_opt, opts); break;
            case (LinkOption) : applyLinkOption(new_opt, opts); break;
            case (FmtOption) : applyFmtOption(new_opt, opts); break;
            case (IntByInt) : applyIntByIntOption(new_opt, opts); break;
            case (Path) : applyPathOption(new_opt, opts); break;
            case (String) :  applyStringOption(new_opt, opts); break;
        }
    }

}
