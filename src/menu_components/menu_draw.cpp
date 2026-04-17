// menu_draw.cpp
// Skia-based menu drawing and mouse handling
// Extracted from menu.cpp with no functional changes.

#include <algorithm>
#include <cctype>
#include <iostream>
#include <string>
#include <vector>

#include "menu.h"
#include "termcolor.h"
#include "gw_version.h"

namespace Menu {

// -----------------------------------------------------------------------------
// Utilities
// -----------------------------------------------------------------------------

void warnRestart() {
    std::cerr << termcolor::yellow
              << "\nPlease click the save button and restart gw to apply changes"
              << termcolor::reset << std::endl;
}

std::string niceText(std::string s) {
    if (s.empty()) return "";
    s[0] = std::toupper(s[0]);
    auto p = s.find("_");
    if (p != std::string::npos) {
        s.replace(p, 1, " ");
    }
    return s;
}

std::vector<std::string> mainHeadings() {
    return {
        "general",
        "genomes",
        "interaction",
        "labelling",
        "navigation",
        "tracks",
        "view_thresholds",
        "shift_keymap"
    };
}

std::vector<std::string> availableButtonsStr(Themes::MenuTable t) {
    switch (t) {
        case Themes::MenuTable::CONTROLS:
        case Themes::MenuTable::MAIN:
            return {"close", "save"};
        case Themes::MenuTable::GENOMES:
        case Themes::MenuTable::TRACKS:
            return {"back", "close", "save", "add", "delete"};
        default:
            return {"back", "close", "save"};
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
    if (s == "genomes") return Themes::MenuTable::GENOMES;
    if (s == "tracks") return Themes::MenuTable::TRACKS;
    if (s == "general") return Themes::MenuTable::GENERAL;
    if (s == "view_thresholds") return Themes::MenuTable::VIEW_THRESHOLDS;
    if (s == "navigation") return Themes::MenuTable::NAVIGATION;
    if (s == "labelling") return Themes::MenuTable::LABELLING;
    if (s == "interaction") return Themes::MenuTable::INTERACTION;
    if (s == "shift_keymap") return Themes::MenuTable::SHIFT_KEYMAP;
    return Themes::MenuTable::MAIN;
}

// -----------------------------------------------------------------------------
// Skia menu drawing
// -----------------------------------------------------------------------------

void drawMenu(SkCanvas *canvas, Themes::IniOptions &opts, Themes::Fonts &fonts, float monitorScale, float fb_width, float fb_height,
                std::string inputText, int charIndex) {

    SkRect rect;
    SkPath path;

    float baseControlBoxSize = 18.0f;  // Base control box size
    float baseVGap = 3.0f;             // Base vertical gap
    float baseStrokeWidth = 1.5f;      // Base stroke width
    float baseIconPadding = 4.0f;      // Reduced padding to make icons bigger
    float baseMenuWidth = 28.0f;       // Base menu width multiplier

    float pad = fonts.overlayHeight;
    float v_gap = baseVGap * monitorScale;
    float control_box_h = baseControlBoxSize * monitorScale;
    float stroke_width = baseStrokeWidth * monitorScale;
    float icon_padding = baseIconPadding * monitorScale;

    // Position
    float y = v_gap;
    float x = v_gap;
    float m_width = baseMenuWidth * fonts.overlayWidth;
    auto m_height = pad * 1.5f;

    // Set up paints
    SkPaint bg;
    SkPaint menuBg;
    SkPaint tcMenu;

    if (opts.theme_str != "igv") {
        bg.setARGB(255, 35, 35, 45);
        tcMenu.setARGB(255, 255, 255, 255);
        menuBg = opts.theme.fcDup;
    } else {
        bg.setARGB(255, 255, 255, 255);
        tcMenu.setARGB(255, 0, 0, 0);
        menuBg.setARGB(255, 235, 235, 235);
    }
    tcMenu.setStyle(SkPaint::kStrokeAndFill_Style);
    tcMenu.setAntiAlias(true);

    // Draw menu background
    rect.setXYWH(0, 0, 20000, m_height);
    canvas->drawRect(rect, opts.theme.bgPaint);

    rect.setXYWH(0, 0, m_width + v_gap, 20000);
    canvas->drawRect(rect, bg);

    std::vector<std::string> btns = availableButtonsStr(opts.menu_table);
    float x2 = x;

    // Calculate rounded corner radius based on scale
    float cornerRadius = 2.5f * monitorScale;
    for (auto& b : btns) {
        rect.setXYWH(x2, y, control_box_h, control_box_h);
        if (b == opts.control_level) {
            canvas->drawRoundRect(rect, cornerRadius, cornerRadius, menuBg);
        } else {
            canvas->drawRoundRect(rect, cornerRadius, cornerRadius, bg);
        }

        float iconSize = control_box_h - (2 * icon_padding);
        float iconLeft = x2 + icon_padding;
        float iconRight = x2 + control_box_h - icon_padding;
        float iconTop = y + icon_padding;
        float iconBottom = y + control_box_h - icon_padding;
        float iconMiddleX = x2 + (control_box_h / 2);
        float iconMiddleY = y + (control_box_h / 2);

        if (b == "close") {
            tcMenu.setStrokeWidth(stroke_width);
            path.reset();
            path.moveTo(iconLeft, iconTop);
            path.lineTo(iconRight, iconBottom);
            path.moveTo(iconLeft, iconBottom);
            path.lineTo(iconRight, iconTop);
            canvas->drawPath(path, tcMenu);
            tcMenu.setStrokeWidth(0);
        } else if (b == "back") {
            path.reset();
            path.moveTo(iconRight, iconTop);
            path.lineTo(iconLeft, iconMiddleY);
            path.lineTo(iconRight, iconBottom);
            canvas->drawPath(path, tcMenu);
        } else if (b == "save") {
            float cornerCut = 4.0f * monitorScale; // Size of corner cutout
            path.reset();
            path.moveTo(iconLeft, iconTop);
            path.lineTo(iconLeft, iconBottom);
            path.lineTo(iconRight, iconBottom);
            path.lineTo(iconRight, iconTop + cornerCut);
            path.lineTo(iconRight - cornerCut, iconTop);
            path.close();
            canvas->drawPath(path, tcMenu);
            float lineHeight = stroke_width * 1.2f;
            float lineGap = (iconBottom - iconTop - (3 * lineHeight)) / 3;
            rect.setXYWH(
                iconLeft + (icon_padding * 0.6f),
                iconTop + lineGap,// * 2 + lineHeight,
                iconSize - (icon_padding * 1.8f),
                lineHeight * 1.4f
            );
            canvas->drawRect(rect, ((b == opts.control_level) ? menuBg : bg));
        } else if (b == "add") {
            tcMenu.setStrokeWidth(stroke_width);
            path.reset();
            path.moveTo(iconMiddleX, iconTop);
            path.lineTo(iconMiddleX, iconBottom);
            path.moveTo(iconLeft, iconMiddleY);
            path.lineTo(iconRight, iconMiddleY);
            canvas->drawPath(path, tcMenu);
            tcMenu.setStrokeWidth(0);
        } else if (b == "delete") {
            tcMenu.setStrokeWidth(stroke_width);
            path.reset();
            path.moveTo(iconLeft, iconMiddleY);
            path.lineTo(iconRight, iconMiddleY);
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
            std::string gw_ver = GW_VERSION;
            tip = opts.ini_path + "  v" + GW_VERSION;
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
        else if (opts.menu_level == "scale_bar") { tip = "Add scale bars [true, false]"; }
        else if (opts.menu_level == "vcf_as_tracks") { tip = "Drag-and-dropped vcf/bcf files will be added as a track if true, or image-tiles otherwise [true, false]"; }
        else if (opts.menu_level == "bed_as_tracks") { tip = "Drag-and-dropped bed files will be added as a track if true, or image-tiles otherwise [true, false]"; }
        else if (opts.menu_level == "link") { tip = "Change which reads are linked [none, sv, all]"; }
        else if (opts.menu_level == "split_view_size") { tip = "Structural variants greater than this size will be drawn as two regions"; }
        else if (opts.menu_level == "threads") { tip = "The number of threads to use for file readings"; }
        else if (opts.menu_level == "pad") { tip = "The number of bases to pad a region by"; }
        else if (opts.menu_level == "scroll_speed") { tip = "The speed of scrolling, increase for faster speeds"; }
        else if (opts.menu_level == "read_y_gap") { tip = "The y-axis gap between rows of alignments"; }
        else if (opts.menu_level == "tabix_track_height") { tip = "The space taken up by tracks from tab separated files"; }
        else if (opts.menu_level == "soft_clip") { tip = "The distance in base-pairs when soft-clips become visible"; }
        else if (opts.menu_level == "small_indel") { tip = "The distance in base-pairs when small indels become visible"; }
        else if (opts.menu_level == "snp") { tip = "The distance in base-pairs when snps become visible"; }
        else if (opts.menu_level == "mod") { tip = "The distance in base-pairs when mods become visible"; }
        else if (opts.menu_level == "edge_highlights") { tip = "The distance in base-pairs when edge-highlights become visible"; }
        else if (opts.menu_level == "low_memory") { tip = "The distance in base-pairs when using low-memory mode (reads are not buffered in this mode)"; }
        else if (opts.menu_level == "mods") { tip = "Display modified bases"; }
        else if (opts.menu_level == "mods_qual_threshold") { tip = "Threshold (>) for displaying modified bases [0-255]"; }
        else if (opts.menu_level == "scroll_right") { tip = "Keyboard key to use for scrolling right"; }
        else if (opts.menu_level == "scroll_left") { tip = "Keyboard key to use for scrolling left"; }
        else if (opts.menu_level == "scroll_down") { tip = "Keyboard key to use for scrolling down"; }
        else if (opts.menu_level == "scroll_up") { tip = "Keyboard key to use for scrolling up"; }
        else if (opts.menu_level == "zoom_in") { tip = "Keyboard key to use for zooming in"; }
        else if (opts.menu_level == "zoom_out") { tip = "Keyboard key to use for zooming out"; }
        else if (opts.menu_level == "cycle_link_mode") { tip = "Keyboard key to use for cycling link mode"; }
        else if (opts.menu_level == "find_alignments") { tip = "Keyboard key to use for highlighting all alignments from template"; }
        else if (opts.menu_level == "number") { tip = "The number of images to show at one time"; }
        else if (opts.menu_level == "parse_label") { tip = "Information to parse from vcf file"; }
        else if (opts.menu_level == "labels") { tip = "Choice of labels to use"; }
        else if (opts.menu_level == "font") { tip = "Change the font"; }
        else if (opts.menu_level == "font_size") { tip = "Change the font size"; }
        else if (opts.menu_level == "track_label_parser_rules") { tip = "Format-specific label rules for GTF/GFF3 tracks, e.g. GTF:gene_name;GFF3:Name"; }
        else if (opts.menu_level == "variant_distance") { tip = "For VCF/BCF tracks, ignore variants with start and end further than this distance"; }
        else if (opts.menu_level == "sv_arcs") { tip = "Draw arcs instead of blocks for VCF/BCF track files"; }
        else if (opts.menu_level == "data_labels") { tip = "Add labels for data tracks"; }

    } else {
        if (opts.control_level == "close") { tip = "Close settings"; }
        else if (opts.control_level == "back") { tip = "Go back to"; }
        else if (opts.control_level == "save") { tip = "Save changes to .gw.ini file"; }
        else if (opts.control_level == "add") { tip = "Add a new entry"; }
        else if (opts.control_level == "delete") { tip = "Delete the selected entry"; }
    }
    if (!tip.empty()) {
        sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(tip.c_str(), fonts.overlay);
        canvas->drawTextBlob(blob.get(), m_width + v_gap + v_gap, pad + v_gap, opts.theme.tcDel);
    }
}

// -----------------------------------------------------------------------------
// Mouse hover handling
// -----------------------------------------------------------------------------

void menuMousePos(
    Themes::IniOptions &opts,
    Themes::Fonts &fonts,
    float xPos,
    float yPos,
    float fb_height,
    float fb_width,
    float monitorScale,
    bool *redraw
) {
    if (opts.editing_underway) return;

    float pad = fonts.overlayHeight;
    float v_gap = 3 * monitorScale;
    float control_box_h = 18 * monitorScale;
    float y = v_gap;
    float x = v_gap;
    float m_height = pad * 1.5f;

    if (yPos >= y && yPos <= y + control_box_h) {
        std::vector<std::string> controls =
            availableButtonsStr(opts.menu_table);

        for (const auto& btn : controls) {
            if (xPos >= x && xPos <= x + control_box_h) {
                opts.menu_level = "controls";
                opts.control_level = btn;
                *redraw = true;
                break;
            }
            x += control_box_h + v_gap;
        }
    }
    else {
        y += control_box_h + v_gap + m_height;
        y += pad + v_gap;
        opts.control_level.clear();

        if (opts.menu_table == Themes::MenuTable::MAIN) {
            opts.menu_level.clear();
            for (const auto& heading : mainHeadings()) {
                if (xPos > 0 && xPos <= fb_width &&
                    y <= yPos && yPos <= y + m_height)
                {
                    opts.menu_level = heading;
                    *redraw = true;
                    break;
                }
                y += m_height + v_gap;
                if (y > fb_height) break;
            }
        }
        else {
            opts.menu_level.clear();
            std::string key = getMenuKey(opts.menu_table);
            for (const auto& heading : opts.myIni[key]) {
                if ((xPos > 0 && xPos <= fb_width) &&
                    y <= yPos && yPos <= y + m_height)
                {
                    opts.menu_level = heading.first;
                    *redraw = true;
                    break;
                }
                y += m_height + v_gap;
                if (y > fb_height) break;
            }
        }
    }
}

} // namespace Menu