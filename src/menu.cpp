//
// Created by Kez Cleal on 26/08/2023.
//
#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <cstdio>
#include <filesystem>
#include <vector>
#include <unordered_map>

//#ifdef __APPLE__
//#include <OpenGL/gl.h>
//#endif

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
#include "../include/unordered_dense.h"
#include "../include/termcolor.h"
#include "../include/version.h"
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
        return {"general", "genomes", "interaction", "labelling", "navigation", "tracks", "view_thresholds", "shift_keymap"};
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
            case Themes::MenuTable::SHIFT_KEYMAP: return "shift_keymap";
            case Themes::MenuTable::CONTROLS: return "Controls";
        }
        return "";
    }

    Themes::MenuTable getMenuLevel(std::string &s) {
        if (s == "genomes") { return Themes::MenuTable::GENOMES; }
        else if (s == "tracks") { return Themes::MenuTable::TRACKS; }
        else if (s == "general") { return Themes::MenuTable::GENERAL; }
        else if (s == "view_thresholds") { return Themes::MenuTable::VIEW_THRESHOLDS; }
        else if (s == "navigation") { return Themes::MenuTable::NAVIGATION; }
        else if (s == "labelling") { return Themes::MenuTable::LABELLING; }
        else if (s == "interaction") { return Themes::MenuTable::INTERACTION; }
        else if (s == "shift_keymap") { return Themes::MenuTable::SHIFT_KEYMAP; }
        else { return Themes::MenuTable::MAIN; }
    }

    void drawMenu(SkCanvas *canvas, GrDirectContext *sContext, SkSurface *sSurface, Themes::IniOptions &opts, Themes::Fonts &fonts, float monitorScale, float fb_width, float fb_height,
                  std::string inputText, int charIndex) {
        SkRect rect;
        SkPath path;
        float pad = fonts.overlayHeight;
        float v_gap = 5;
        float control_box_h = 35;
        float y = v_gap;
        float x = v_gap;
        float m_width = 28 * fonts.overlayWidth;
        auto m_height = (float)(pad * 1.5);
        SkPaint bg;
        SkPaint menuBg;
        SkPaint tcMenu;
        if (opts.theme_str != "igv") {
            bg.setARGB(255, 15, 15, 25);
            tcMenu.setARGB(255, 255, 255, 255);
            menuBg = opts.theme.fcDup;
        } else {
            bg.setARGB(255, 255, 255, 255);
            tcMenu.setARGB(255, 0, 0, 0);
            menuBg.setARGB(255, 215, 215, 255);
        }
        tcMenu.setStyle(SkPaint::kStrokeAndFill_Style);
        tcMenu.setAntiAlias(true);

        rect.setXYWH(0, 0, 20000, m_height);
        canvas->drawRect(rect, opts.theme.bgPaint);

        rect.setXYWH(0, 0, m_width + v_gap, 20000);
        canvas->drawRect(rect, bg);
        std::vector<std::string> btns = availableButtonsStr(opts.menu_table);
        float x2 = x;
        for (auto& b : btns) {
            rect.setXYWH(x2, y, control_box_h, control_box_h);
            if (b == opts.control_level) {
                canvas->drawRoundRect(rect, 5, 5, menuBg);
            } else {
                canvas->drawRoundRect(rect, 5, 5, bg);
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
                canvas->drawRect(rect, ((b == opts.control_level) ? menuBg : bg));
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
        float txt_w, to_cursor_width, txt_start;
        txt_start = x*2 + fonts.overlayWidth + m_width;

        if (opts.menu_table == Themes::MenuTable::MAIN) {
            y += control_box_h + v_gap + m_height;
            std::string h = "    " + niceText(table_name);
            sk_sp < SkTextBlob> txt = SkTextBlob::MakeFromString(h.c_str(), fonts.overlay);
            canvas->drawTextBlob(txt.get(), x*2, y, tcMenu);
            y += pad + v_gap;
            for (auto & heading : mainHeadings()) {
                rect.setXYWH(x, y, m_width, m_height);
                if (opts.menu_level == heading) {
                    canvas->drawRoundRect(rect, 5, 5, menuBg);
                } else {
                    canvas->drawRoundRect(rect, 5, 5, bg);
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
            bool text_editing_underway = (opts.editing_underway && opts.control_level == "add" && opts.menu_level == "controls");
            for (auto & heading : opts.myIni[table_name]) {
                if (heading.first == "fmt" || heading.first == "miny") { continue; }  // obsolete
                rect.setXYWH(x, y, m_width, m_height);
                if (opts.menu_level == heading.first) {
                    canvas->drawRoundRect(rect, 5, 5, menuBg);
                } else {
                    canvas->drawRoundRect(rect, 5, 5, bg);
                }
                if ((table_name == "genomes" || table_name == "tracks") && heading.first == opts.genome_tag) {
                    canvas->drawCircle(x*4, y + (m_height/2), 4, tcMenu);
                }
                std::string h = "    " + heading.first;
                sk_sp < SkTextBlob> txt = SkTextBlob::MakeFromString(h.c_str(), fonts.overlay);
                canvas->drawTextBlob(txt.get(), x*2, y + pad, tcMenu);
                std::string value = heading.second;
                if (!opts.editing_underway || opts.menu_level != heading.first || text_editing_underway) {
                    sk_sp < SkTextBlob> txt_v = SkTextBlob::MakeFromString(value.c_str(), fonts.overlay);
                    canvas->drawTextBlob(txt_v.get(), x*2 + x + m_width, y + pad, opts.theme.tcDel);
                } else {
                    txt_w = fonts.overlay.measureText(inputText.c_str(), inputText.size(), SkTextEncoding::kUTF8);
                    float max_w = fb_width - m_width - (fonts.overlayWidth*5);
                    if (txt_w > max_w) {
                        std::string sub;
                        int i = 0;
                        for ( ; i < charIndex; ++i) {
                            sub = inputText.substr(i, inputText.size());
                            if (fonts.overlay.measureText(sub.c_str(), sub.size(), SkTextEncoding::kUTF8) < max_w) {
                                break;
                            }
                        }
                        inputText = inputText.substr(i, inputText.size());;
                        charIndex -= i;
                        txt_w = fonts.overlay.measureText(inputText.c_str(), inputText.size(), SkTextEncoding::kUTF8);
                    }
                    to_cursor_width = fonts.overlay.measureText(inputText.substr(0, charIndex).c_str(), charIndex, SkTextEncoding::kUTF8);
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
            if (text_editing_underway) {  // new track or genome key being added
                rect.setXYWH(x, y, m_width, m_height);
                canvas->drawRect(rect, menuBg);
                to_cursor_width = fonts.overlay.measureText(inputText.substr(0, charIndex).c_str(), charIndex, SkTextEncoding::kUTF8);
                float txt_start2 = fonts.overlay.measureText("    ", 4, SkTextEncoding::kUTF8);
                std::string h = inputText;
                path.reset();
                path.moveTo(txt_start2 + to_cursor_width + x, y + (fonts.overlayHeight * 0.2));
                path.lineTo(txt_start2 + to_cursor_width + x, y + fonts.overlayHeight * 1.1);
                canvas->drawPath(path, opts.theme.lcBright);
                sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(h.c_str(), fonts.overlay);
                canvas->drawTextBlob(blob.get(), x + txt_start2, y + pad, opts.theme.tcDel);
            }
        }
        // write tool tip
        std::string tip;
        if (opts.control_level.empty()) {
            if (opts.menu_table == Themes::MenuTable::MAIN) {
                std::string vstr = version_str;
                tip = opts.ini_path + "  v" + vstr;
            }
            else if (opts.menu_table == Themes::MenuTable::GENOMES) { tip = "Use ENTER key to select genome, or RIGHT_ARROW key to edit path"; }
            else if (opts.menu_table == Themes::MenuTable::SHIFT_KEYMAP) { tip = "Change characters selected when using shift+key"; }
            else if (opts.menu_level == "theme") { tip = "Change the theme to one of [dark, igv, slate]"; }
            else if (opts.menu_level == "dimensions") { tip = "The starting dimensions in pixels of the gw window"; }
            else if (opts.menu_level == "indel_length") { tip = "Indels with this length (or greater) will be labelled with text"; }
            else if (opts.menu_level == "ylim") { tip = "The y-limit, or number of rows of reads in the image"; }
            else if (opts.menu_level == "coverage") { tip = "Turn coverage on or off [true, false]"; }
            else if (opts.menu_level == "log2_cov") { tip = "Change the y-scale of the coverage track to log2 [true, false]"; }
            else if (opts.menu_level == "expand_tracks") { tip = "Expand overlapping track features [true, false]"; }
            else if (opts.menu_level == "vcf_as_tracks") { tip = "Drag-and-dropped vcf/bcf files will be added as track features [true, false]"; }
            else if (opts.menu_level == "link") { tip = "Change which reads are linked [none, sv, all]"; }
            else if (opts.menu_level == "split_view_size") { tip = "Structural variants greater than this size will be drawn as two regions"; }
            else if (opts.menu_level == "threads") { tip = "The number of threads to use for file readings"; }
            else if (opts.menu_level == "pad") { tip = "The number of bases to pad a region by"; }
            else if (opts.menu_level == "scroll_speed") { tip = "The speed of scrolling, increase for faster speeds"; }
            else if (opts.menu_level == "tabix_track_height") { tip = "The space taken up by tracks from tab separated files"; }
            else if (opts.menu_level == "soft_clip") { tip = "The distance in base-pairs when soft-clips become visible"; }
            else if (opts.menu_level == "small_indel") { tip = "The distance in base-pairs when small indels become visible"; }
            else if (opts.menu_level == "snp") { tip = "The distance in base-pairs when snps become visible"; }
            else if (opts.menu_level == "edge_highlights") { tip = "The distance in base-pairs when edge-highlights become visible"; }
            else if (opts.menu_level == "scroll_right") { tip = "Keyboard key to use for scrolling right"; }
            else if (opts.menu_level == "scroll_left") { tip = "Keyboard key to use for scrolling left"; }
            else if (opts.menu_level == "scroll_down") { tip = "Keyboard key to use for scrolling down"; }
            else if (opts.menu_level == "scroll_up") { tip = "Keyboard key to use for scrolling up"; }
            else if (opts.menu_level == "zoom_in") { tip = "Keyboard key to use for zooming in"; }
            else if (opts.menu_level == "zoom_out") { tip = "Keyboard key to use for zooming out"; }
            else if (opts.menu_level == "cycle_link_mode") { tip = "Keyboard key to use for cycling link mode"; }
            else if (opts.menu_level == "print_screen") { tip = "Keyboard key to use for printing screen (saves a .png file of the screen)"; }
            else if (opts.menu_level == "find_alignments") { tip = "Keyboard key to use for highlighting all alignments from template"; }
            else if (opts.menu_level == "number") { tip = "The number of images to show at one time"; }
            else if (opts.menu_level == "parse_label") { tip = "Information to parse from vcf file"; }
            else if (opts.menu_level == "labels") { tip = "Choice of labels to use"; }
            else if (opts.menu_level == "delete_labels") { tip = "Keyboard key to remove all labels on screen"; }
            else if (opts.menu_level == "delete_labels") { tip = "Keyboard key to switch to the interactive alignment-view mode"; }
            else if (opts.menu_level == "font") { tip = "Change the font"; }
            else if (opts.menu_level == "font_size") { tip = "Change the font size"; }

        } else {
            if (opts.control_level == "close") { tip = "Close settings"; }
            else if (opts.control_level == "back") { tip = "Go back to"; }
            else if (opts.control_level == "save") { tip = "Save changes to .gw.ini file"; }
            else if (opts.control_level == "add") { tip = "Add a new entry"; }
            else if (opts.control_level == "delete") { tip = "Delete the selected entry"; }
        }
        if (!tip.empty()) {
            SkPaint tip_paint;
            tip_paint.setARGB(255, 100, 100, 100);
            tip_paint.setStyle(SkPaint::kStrokeAndFill_Style);
            tcMenu.setAntiAlias(true);
            sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(tip.c_str(), fonts.overlay);
            canvas->drawTextBlob(blob.get(), m_width + v_gap + v_gap, pad + v_gap, tip_paint);
        }
    }

    void menuMousePos(Themes::IniOptions &opts, Themes::Fonts &fonts, float xPos, float yPos, float fb_height, float fb_width, bool *redraw) {
        if (opts.editing_underway) {
            return;
        }
        float pad = fonts.overlayHeight;
        float v_gap = 5;
        float control_box_h = 35;
        float y = v_gap;
        float x = v_gap;
        auto m_height = (float)(pad * 1.5);
        if (yPos >= y && yPos <= y + control_box_h) {  // mouse over controls
            std::vector<std::string> controls = availableButtonsStr(opts.menu_table);
            for (const auto& btn : controls) {
                if (xPos >= x && xPos <= x + control_box_h) {
                    opts.menu_level = "controls";
                    opts.control_level = btn;
                    *redraw = true;
                    break;
                }
                x += control_box_h + v_gap;
            }
        } else {
            y += control_box_h + v_gap + m_height;
            y += pad + v_gap;
            opts.control_level = "";
            if (opts.menu_table == Themes::MenuTable::MAIN) {
                opts.menu_level = "";
                for (const auto &heading : mainHeadings()) {
                    if (xPos > 0 && xPos <= fb_width && y <= yPos && yPos <= y + m_height) {
                        opts.menu_level = heading;
                        *redraw = true;
                        break;
                    }
                    y += m_height + v_gap;
                    if (y > fb_height) { break; }
                }
            } else {
                opts.menu_level = "";
                for (const auto &heading : opts.myIni[getMenuKey(opts.menu_table)]) {
                    if (heading.first == "fmt" || heading.first == "miny") { continue; }
                    if (xPos > 0 && xPos <= fb_width && y <= yPos && yPos <= y + m_height) {
                        opts.menu_level = heading.first;
                        *redraw = true;
                        break;
                    }
                    y += m_height + v_gap;
                    if (y > fb_height) { break; }
                }
            }
        }
    }

    bool menuSelect(Themes::IniOptions &opts) {
        if (opts.menu_level == "") {
            opts.menu_table = Themes::MenuTable::MAIN;
            opts.previous_level = opts.menu_level;
            opts.control_level = "close";
        } else if (opts.menu_level == "genomes") {
            opts.menu_table = Themes::MenuTable::GENOMES;
            opts.previous_level = opts.menu_level;
            if (opts.myIni["genomes"].size() == 0) {
                opts.menu_level = "";
            } else {
                opts.menu_level = opts.myIni["genomes"].begin()->first;
            }
        } else if (opts.menu_level == "tracks") {
            opts.menu_table = Themes::MenuTable::TRACKS;
            opts.previous_level = opts.menu_level;
            if (opts.myIni["tracks"].size() == 0) {
                opts.menu_level = "";
            } else {
                opts.menu_level = opts.myIni["tracks"].begin()->first;
            }
        } else if (opts.menu_level == "general") {
            opts.menu_table = Themes::MenuTable::GENERAL;
            opts.previous_level = opts.menu_level;
            opts.menu_level = opts.myIni["general"].begin()->first;
        } else if (opts.menu_level == "view_thresholds") {
            opts.menu_table = Themes::MenuTable::VIEW_THRESHOLDS;
            opts.previous_level = opts.menu_level;
            opts.menu_level = opts.myIni["view_thresholds"].begin()->first;
        } else if (opts.menu_level == "navigation") {
            opts.menu_table = Themes::MenuTable::NAVIGATION;
            opts.previous_level = opts.menu_level;
            opts.menu_level = opts.myIni["navigation"].begin()->first;
        } else if (opts.menu_level == "interaction") {
            opts.menu_table = Themes::MenuTable::INTERACTION;
            opts.previous_level = opts.menu_level;
            opts.menu_level = opts.myIni["interaction"].begin()->first;
        } else if (opts.menu_level == "labelling") {
            opts.menu_table = Themes::MenuTable::LABELLING;
            opts.previous_level = opts.menu_level;
            opts.menu_level = opts.myIni["labelling"].begin()->first;
        } else if (opts.menu_level == "shift_keymap") {
            opts.menu_table = Themes::MenuTable::SHIFT_KEYMAP;
            opts.previous_level = opts.menu_level;
            opts.menu_level = opts.myIni["shift_keymap"].begin()->first;
        }
        //
        else if (opts.menu_level == "controls") {
            if (opts.control_level == "close") {
                opts.menu_level = "";
                opts.menu_table = Themes::MenuTable::MAIN;
                opts.previous_level = opts.menu_level;
                return false;
            } else if (opts.control_level == "back") {
                opts.menu_level = "controls";
                opts.control_level = "close";
                opts.menu_table = Themes::MenuTable::MAIN;
                opts.previous_level = opts.menu_level;
            } else if (opts.control_level == "save") {
                opts.menu_level = "";
                opts.control_level = "close";
                opts.menu_table = Themes::MenuTable::MAIN;
                opts.previous_level = opts.menu_level;
                mINI::INIFile file(opts.ini_path);
                file.write(opts.myIni);
                std::cout << "Saved .gw.ini to " << opts.ini_path << std::endl;
                return false;
            } else if (opts.control_level == "delete") {
                opts.menu_level = "";
                opts.control_level = "close";
                opts.previous_level = opts.menu_level;
                opts.myIni[(opts.menu_table == Themes::MenuTable::GENOMES) ? "genomes" : "tracks"].remove(opts.genome_tag);
                if (opts.menu_table == Themes::MenuTable::GENOMES) {
                    warnRestart();
                }
            } else if (opts.control_level == "add") {
                opts.editing_underway = !opts.editing_underway;
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

    bool navigateMenu(Themes::IniOptions &opts, int key, int action, std::string &inputText, int *charIndex, bool *captureText, bool *textFromSettings, bool *processText, std::string &reference_path) {
        /*
         * Notes:
         * string variables are used to keep track of mouse-over events, or keyboard selection events
         * int enums are used to keep track of what was clicked on (mouse or keyboard), or triggering events.
         * bool-pointer input variables keep track of how to handle or capture text
         */
        if (opts.ini_path.empty()) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " .gw.ini file could not be read. Please create one in your home directory, in your .config directory, or in the same folder as the gw executable" << std::endl;
            return false;
        }
        // ignore empty command from command-box
        if (*processText && !*textFromSettings) {
            *processText = false;
            return true;
        }

        int ik;
        if (key == GLFW_KEY_DOWN || key == GLFW_KEY_UP) {
            std::vector<std::string> keys;
            int current_i = 0;
            ik = -1;
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
                if (opts.myIni.has(table_name)) {
                    for (auto &ini_key: opts.myIni[table_name]) {
                        if (ini_key.first == "fmt" || ini_key.first == "miny") { continue; }  // obsolete
                        keys.push_back(ini_key.first);
                        if (opts.menu_level == keys.back()) {
                            ik = current_i;
                        }
                        current_i += 1;
                    }
                }

            }
            if (key == GLFW_KEY_DOWN) {
                ik = (ik >= (int) keys.size() - 1) ? (int) keys.size() - 1 : std::max(0, ik + 1);
            } else {
                ik = std::max(-1, ik - 1);
            }
            if (ik <= -1) {
                opts.menu_level = "controls";
                opts.control_level = Menu::availableButtonsStr(opts.menu_table)[0];
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
            if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_TAB) {
                current_i += 1;
            } else if (key == GLFW_KEY_LEFT) {
                current_i -= 1;
            }
            current_i = std::max(0, std::min(current_i, (int)btns.size() - 1));
            opts.control_level = btns[current_i];
            if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                bool keep_alive = Menu::menuSelect(opts);
                if (opts.editing_underway) {
                    inputText = "";
                    *charIndex = 0;
                    *textFromSettings = true;
                    *captureText = true;
                } else if (opts.control_level == "add") {
                    std::string menu_heading = (opts.menu_table == Themes::MenuTable::GENOMES) ? "genomes" : "tracks";
                    std::replace(inputText.begin(), inputText.end(), ' ', '_');
                    for(auto &c : inputText) { c = tolower(c); }
                    opts.myIni[menu_heading][inputText] = "";
                    opts.menu_level = inputText;
                    opts.control_level = "";
                    if (opts.menu_table == Themes::MenuTable::GENOMES) {
                        bool path_in_ini = false;
                        for (const auto &ikey : opts.myIni["genomes"]) {
                            if (ikey.second == reference_path) {
                                path_in_ini = true;
                                break;
                            }
                        }
                        if (!path_in_ini) {
                            inputText = reference_path;
                        } else {
                            inputText = "";
                        }
                    } else {
                        inputText = "";
                    }
                    opts.editing_underway = true;
                    *charIndex = inputText.size();
                    *textFromSettings = true;
                    *captureText = true;
                }
                return keep_alive;
            } else if (key == GLFW_KEY_ESCAPE) {
                opts.menu_table = Themes::MenuTable::MAIN;
            }

        } else if (action == GLFW_PRESS) {
            if (!opts.editing_underway && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) && (opts.menu_table == Themes::MenuTable::GENOMES || opts.menu_table == Themes::MenuTable::TRACKS)) {
                // clicking on new item updates the genome_tag, same item triggers editing mode
                if (opts.genome_tag != opts.menu_level) {
                    opts.genome_tag = opts.menu_level;
                    return true;
                } else {
                    bool keep_alive = Menu::menuSelect(opts);
                    if (opts.editing_underway) {
                        inputText = opts.myIni[getMenuKey(opts.menu_table)][opts.menu_level];
                        *charIndex = inputText.size();
                        *textFromSettings = true;
                        *captureText = true;
                    }
                    return keep_alive;
                }
            }
            else if (key == GLFW_KEY_RIGHT || key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER || key == GLFW_KEY_TAB) {
                if (opts.editing_underway) {
                    opts.editing_underway = false;
                    *processText = false;
                    *captureText = false;
                    *textFromSettings = false;
                    inputText = "";
                    opts.editing_underway = false;
                    opts.menu_table = getMenuLevel(opts.previous_level);
                    return true;
                } else {
                    bool keep_alive = Menu::menuSelect(opts);
                    if (opts.editing_underway) {
                        inputText = opts.myIni[getMenuKey(opts.menu_table)][opts.menu_level];
                        *charIndex = inputText.size();
                        *textFromSettings = true;
                        *captureText = true;
                    }
                    return keep_alive;
                }
            } else if (opts.menu_table == Themes::MenuTable::CONTROLS && key == GLFW_KEY_ESCAPE) {
                opts.menu_table = Themes::MenuTable::MAIN;
                opts.menu_level = opts.previous_level;
            } else if (opts.menu_table != Themes::MenuTable::MAIN && (key == GLFW_KEY_LEFT || key == GLFW_KEY_ESCAPE)) {
                if (*textFromSettings) {
                    opts.menu_table = getMenuLevel(opts.previous_level);
                } else {
                    opts.menu_table = Themes::MenuTable::MAIN;
                    opts.menu_level = opts.previous_level;
                }
                *captureText = false;
                *textFromSettings = false;
                inputText = "";
                opts.editing_underway = false;
            }
        }
        return true;
    }

    enum OptionKind {
        String, ThemeOption, LinkOption, Bool, Int, Float, IntByInt, Path, KeyboardKey
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
                choices.insert(choices.end(), {"dark", "igv", "slate"});
            } else if (kind == LinkOption) {
                choices.insert(choices.end(), {"none", "sv", "all"});
            }
        }
    };

    Option optionFromStr(std::string &name, Themes::MenuTable mt, std::string &value) {
        std::unordered_map<std::string, OptionKind> option_map;
        for (const auto& v : {"indel_length", "ylim", "split_view_size", "threads", "pad", "soft_clip", "small_indel", "snp", "edge_highlights", "font_size"}) {
            option_map[v] = Int;
        }
        for (const auto& v : {"scroll_speed", "tabix_track_height"}) {
            option_map[v] = Float;
        }
        for (const auto& v : {"coverage", "log2_cov", "expand_tracks", "vcf_as_tracks"}) {
            option_map[v] = Bool;
        }
        for (const auto& v : {"scroll_right", "scroll_left", "zoom_out", "zoom_in", "scroll_down", "scroll_up", "cycle_link_mode", "print_screen", "find_alignments", "delete_labels", "enter_interactive_mode"}) {
            option_map[v] = KeyboardKey;
        }
        option_map["font"] = String;
        if (mt == Themes::MenuTable::GENOMES) {
            return Option(name, Path, value, mt);
        } else if (option_map.find(name) != option_map.end()) {
            return Option(name, option_map[name], value, mt);
        } else if (name == "theme") {
            return Option(name, ThemeOption, value, mt);
        } else if (name == "link") {
            return Option(name, LinkOption, value, mt);
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
            if (new_opt.name == "indel_length") { opts.indel_length = std::max(1, v); }
            else if (new_opt.name == "ylim") { opts.ylim = std::max(1, v); }
            else if (new_opt.name == "split_view_size") { opts.split_view_size = std::max(1, v); }
            else if (new_opt.name == "threads") { opts.threads = std::max(1, v); }
            else if (new_opt.name == "pad") { opts.pad = std::max(1, v); }
            else if (new_opt.name == "soft_clip") { opts.soft_clip_threshold = std::max(0, v); }
            else if (new_opt.name == "small_indel") { opts.small_indel_threshold = std::max(1, v); }
            else if (new_opt.name == "snp") { opts.snp_threshold = std::max(1, v); }
            else if (new_opt.name == "edge_highlights") { opts.edge_highlights = std::max(1, v); }
            else if (new_opt.name == "font_size") { opts.font_size = std::max(1, v); }
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
        if (new_opt.name == "scroll_speed") { opts.scroll_speed = v; }
        else if (new_opt.name == "tabix_track_height") { opts.tab_track_height = v; }
        else { return; }
        opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
    }

    void applyBoolOption(Option &new_opt, Themes::IniOptions &opts) {
        std::unordered_map<std::string, bool> bool_keys;
        bool_keys = { {"1", true}, {"true", true}, {"t", true}, {"True", true}, {"on", true}, {"0", false}, {"false", false}, {"f", false}, {"False", false}, {"off", false} };
        if (bool_keys.find(new_opt.value) == bool_keys.end()) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " expected boolean (true/1 etc), instead of " << new_opt.value << std::endl;
            return;
        }
        bool v = bool_keys[new_opt.value];
        if (new_opt.name == "scroll_spped") { opts.scroll_speed = v; }
        else if (new_opt.name == "tabix_track_height") { opts.tab_track_height = v; }
        else if (new_opt.name == "log2_cov") { opts.log2_cov = v; }
        else if (new_opt.name == "expand_tracks") { opts.expand_tracks = v; }
        else if (new_opt.name == "vcf_as_tracks") { opts.vcf_as_tracks = v; }
        else if (new_opt.name == "coverage") { opts.max_coverage = (v) ? 1410065408 : 0; }
        else { return; }
        opts.myIni[new_opt.table][new_opt.name] = (v) ? "true" : "false";
    }

    void applyKeyboardKeyOption(Option &new_opt, Themes::IniOptions &opts) {
        ankerl::unordered_dense::map<std::string, int> keys;
        Keys::getKeyTable(keys);
        std::string k = new_opt.value;
        for(auto &c : k) { c = toupper(c); }
        if (keys.find(k) == keys.end()) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " key " << k << " not available." << std::endl;
            return;
        }
        int v = keys[k];
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
        opts.myIni[new_opt.table][new_opt.name] = k;
    }

    void applyThemeOption(Option &new_opt, Themes::IniOptions &opts) {
        if (new_opt.value == "igv") {
            opts.theme = Themes::IgvTheme();
            opts.theme_str = "igv";
        } else if (new_opt.value == "dark") {
            opts.theme = Themes::DarkTheme();
            opts.theme_str = "dark";
        } else if (new_opt.value == "slate") {
            opts.theme = Themes::SlateTheme();
            opts.theme_str = "slate";
        } else {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " theme must be 'igv', 'dark' or 'slate'" << std::endl;
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
        } catch (...) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " dimensions not understood" << std::endl;
        }
    }

    void applyPathOption(Option& new_opt, Themes::IniOptions& opts) {
        if (Utils::startsWith(new_opt.value, "http") || Utils::startsWith(new_opt.value, "ftp")) {
            opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
        } else {
            std::filesystem::path path = new_opt.value;
            if (std::filesystem::exists(path)) {
                opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
            } else {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " local path does not exist: " << path << std::endl;
            }
        }
    }

    void applyStringOption(Option &new_opt, Themes::IniOptions &opts) {
        if (new_opt.name == "font") { opts.font_str = new_opt.value; }
        opts.myIni[new_opt.table][new_opt.name] = new_opt.value;
        if (new_opt.name == "labels" || new_opt.name == "parse_label" || new_opt.table == "shift_keymap") {
            warnRestart();
        }
    }

    void processTextEntry(Themes::IniOptions &opts, std::string &inputText) {
        if (opts.menu_level == "controls") {
            return;
        }
        Option new_opt = optionFromStr(opts.menu_level, opts.menu_table, inputText);
        switch (new_opt.kind) {
            case (Int) : applyIntOption(new_opt, opts); break;
            case (Float) : applyFloatOption(new_opt, opts); break;
            case (Bool) : applyBoolOption(new_opt, opts); break;
            case (KeyboardKey) : applyKeyboardKeyOption(new_opt, opts); break;
            case (ThemeOption) : applyThemeOption(new_opt, opts); break;
            case (LinkOption) : applyLinkOption(new_opt, opts); break;
            case (IntByInt) : applyIntByIntOption(new_opt, opts); break;
            case (Path) : applyPathOption(new_opt, opts); break;
            case (String) :  applyStringOption(new_opt, opts); break;
        }
    }

    int getCommandSwitchValue(Themes::IniOptions &opts, std::string &cmd_s, bool &drawLine) {
        if (cmd_s == "tlen-y") {
            return (int)opts.tlen_yscale;
        } else if (cmd_s == "soft-clips") {
            return (int)(opts.soft_clip_threshold > 0);
        } else if (cmd_s == "mismatches") {
            return (int)(opts.snp_threshold > 0);
        } else if (cmd_s == "log2-cov") {
            return (int)(opts.log2_cov);
        } else if (cmd_s == "expand-tracks") {
            return (int)(opts.expand_tracks);
        } else if (cmd_s == "low-mem") {
            return (int)(opts.low_mem);
        } else if (cmd_s == "line") {
            return (int)drawLine;
        } else if (cmd_s == "insertions") {
            return (int)(opts.small_indel_threshold > 0);
        } else if (cmd_s == "edges") {
            return (int)(opts.edge_highlights > 0);
        } else if (cmd_s == "cov") {
            return (int)(opts.max_coverage > 0);
        }
        return -1;
    }

}
