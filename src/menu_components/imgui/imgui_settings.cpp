#include "menu.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imfilebrowser.h"

namespace Menu {

static const char* getOptionTooltip(const std::string& key) {
    static const std::unordered_map<std::string, const char*> tips = {
        {"theme", "Change the theme [dark, igv, slate]"},
        {"dimensions", "Starting window dimensions in pixels (WxH). Requires restart."},
        {"indel_length", "Indels >= this length will be labelled with text"},
        {"ylim", "Number of rows of reads shown"},
        {"coverage", "Show coverage track [true, false]"},
        {"log2_cov", "Log2 scale for coverage track [true, false]"},
        {"expand_tracks", "Expand overlapping track features [true, false]"},
        {"scale_bar", "Show scale bars [true, false]"},
        {"vcf_as_tracks", "Drop VCF/BCF files as tracks instead of image tiles [true, false]"},
        {"bed_as_tracks", "Drop BED files as tracks instead of image tiles [true, false]"},
        {"link", "Link reads by [none, sv, all]"},
        {"split_view_size", "SVs larger than this size are drawn as two regions"},
        {"threads", "Number of threads for file reading"},
        {"pad", "Base-pair padding around a region"},
        {"scroll_speed", "Scrolling speed (higher = faster)"},
        {"read_y_gap", "Vertical gap between alignment rows"},
        {"tabix_track_height", "Height of tabix track panel"},
        {"soft_clip", "Distance (bp) at which soft-clips become visible"},
        {"small_indel", "Distance (bp) at which small indels become visible"},
        {"snp", "Distance (bp) at which SNPs become visible"},
        {"mod", "Distance (bp) at which mods become visible"},
        {"edge_highlights", "Distance (bp) at which edge highlights become visible"},
        {"mods", "Display modified bases"},
        {"mods_qual_threshold", "Threshold for displaying modified bases [0–255]"},
        {"font", "Font name"},
        {"font_size", "Font size"},
        {"track_label_parser_rules", "Format-specific label rules for GTF/GFF3 tracks"},
        {"variant_distance", "Ignore VCF/BCF track variants spanning more than this distance"},
        {"sv_arcs", "Draw arcs instead of blocks for VCF/BCF tracks"},
        {"data_labels", "Add labels for data tracks"}
    };

    auto it = tips.find(key);
    return (it != tips.end()) ? it->second : nullptr;
}

// ----------------------------------------------------------------------------
// Render a single ini key/value widget
// ----------------------------------------------------------------------------
static bool renderIniWidget(
    const std::string& key,
    Themes::MenuTable mt,
    Themes::IniOptions& opts,
    bool& redraw,
    Manager::GwPlot* plot
) {
    std::string section = getMenuKey(mt);
    std::string val = opts.myIni[section][key];
    Option opt = optionFromStr(const_cast<std::string&>(key), mt, val);

    bool changed = false;
    ImGui::PushID(key.c_str());

    if (opt.kind == Bool) {
        bool b = (val == "true" || val == "1" || val == "on" || val == "t");
        if (ImGui::Checkbox(key.c_str(), &b)) {
            opt.value = b ? "true" : "false";
            applyBoolOption(opt, opts);
            changed = true;
        }

    } else if (opt.kind == ThemeOption) {
        static const char* themes[] = {"dark", "igv", "slate"};
        int cur = 0;
        for (int i = 0; i < 3; ++i)
            if (val == themes[i]) cur = i;

        ImGui::SetNextItemWidth(130.f);
        if (ImGui::Combo(key.c_str(), &cur, themes, 3)) {
            opt.value = themes[cur];
            applyThemeOption(opt, opts);
            changed = true;
        }

    } else if (opt.kind == LinkOption) {
        static const char* links[] = {"none", "sv", "all"};
        int cur = 0;
        for (int i = 0; i < 3; ++i)
            if (val == links[i]) cur = i;

        ImGui::SetNextItemWidth(130.f);
        if (ImGui::Combo(key.c_str(), &cur, links, 3)) {
            opt.value = links[cur];
            applyLinkOption(opt, opts);
            changed = true;
        }

    } else if (opt.kind == Int) {
        int v = 0;
        try { v = std::stoi(val); } catch (...) {}
        ImGui::SetNextItemWidth(130.f);
        if (ImGui::InputInt(key.c_str(), &v, 1, 10, ImGuiInputTextFlags_EnterReturnsTrue)) {
            opt.value = std::to_string(v);
            applyIntOption(opt, opts);
            if (key == "font_size" && plot) {
                plot->fonts.setTypeface(opts.font_str, opts.font_size);
                plot->fonts.setOverlayHeight(plot->monitorScale);
            }
            changed = true;
        }

    } else if (opt.kind == Float) {
        float v = 0.f;
        try { v = std::stof(val); } catch (...) {}
        ImGui::SetNextItemWidth(130.f);
        if (ImGui::InputFloat(key.c_str(), &v, 0.1f, 1.f, "%.2f",
                              ImGuiInputTextFlags_EnterReturnsTrue)) {
            opt.value = std::to_string(v);
            applyFloatOption(opt, opts);
            changed = true;
        }

    } else if (opt.kind == IntByInt) {
        char buf[64];
        strncpy(buf, val.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        ImGui::SetNextItemWidth(130.f);
        if (ImGui::InputText(key.c_str(), buf, sizeof(buf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            opt.value = buf;
            applyIntByIntOption(opt, opts);
            changed = true;
        }

    } else {
        char buf[512];
        strncpy(buf, val.c_str(), sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';

        ImGui::SetNextItemWidth(opt.kind == Path ? 350.f : 130.f);
        if (ImGui::InputText(key.c_str(), buf, sizeof(buf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            opt.value = buf;
            if (opt.kind == KeyboardKey) applyKeyboardKeyOption(opt, opts);
            else if (opt.kind == Path) applyPathOption(opt, opts);
            else applyStringOption(opt, opts);

            if (key == "track_label_parser_rules" && plot)
                plot->reloadPathBackedTracks();

            changed = true;
        }
    }

    const char* tip = getOptionTooltip(key);
    if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("%s", tip);

    if (changed) redraw = true;

    ImGui::PopID();
    return changed;
}

// ----------------------------------------------------------------------------
// Genomes / Tracks name→path table
// ----------------------------------------------------------------------------
static void renderNamePathTable(
    const std::string& section,
    Themes::MenuTable mt,
    Themes::IniOptions& opts,
    bool& redraw,
    Manager::GwPlot* plot = nullptr
) {
    static char add_name[128] = {};
    static char add_path[512] = {};

    constexpr ImGuiTableFlags flags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingStretchProp;

    if (ImGui::BeginTable(("##tbl_" + section).c_str(), 3, flags, ImVec2(0.f, 240.f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 140.f);
        ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 58.f);
        ImGui::TableHeadersRow();

        std::string to_delete;

        for (auto& pair : opts.myIni[section]) {
            ImGui::PushID(pair.first.c_str());
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(pair.first.c_str());

            ImGui::TableSetColumnIndex(1);
            char buf[512];
            strncpy(buf, pair.second.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::InputText("##path", buf, sizeof(buf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                Option opt(pair.first, Path, buf, mt);
                applyPathOption(opt, opts);
                redraw = true;
            }

            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("Delete"))
                to_delete = pair.first;

            ImGui::PopID();
        }

        if (!to_delete.empty()) {
            opts.myIni[section].remove(to_delete);
            redraw = true;
        }

        ImGui::EndTable();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Add");
    ImGui::InputText("Name", add_name, sizeof(add_name));
    ImGui::InputText("Path", add_path, sizeof(add_path));

    if (ImGui::Button("Add") && add_name[0] && add_path[0]) {
        Option opt(add_name, Path, add_path, mt);
        applyPathOption(opt, opts);
        add_name[0] = add_path[0] = '\0';
        redraw = true;
    }
}

// ----------------------------------------------------------------------------
// Settings window
// ----------------------------------------------------------------------------
void drawImGuiSettings(
    Manager::GwPlot* plot,
    bool* p_open,
    bool& redraw,
    bool* /*drawLine*/
) {
    if (!p_open || !*p_open) return;

    Themes::IniOptions& opts = plot->opts;
    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Appearing);
    ImGui::SetNextWindowSize(ImVec2(720, 530), ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(500, 350),
                                        ImVec2(io.DisplaySize.x, io.DisplaySize.y));

    if (!ImGui::Begin("Settings", p_open, ImGuiWindowFlags_NoCollapse)) {
        ImGui::End();
        return;
    }

    ImGui::TextDisabled("Config file: %s", opts.ini_path.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy"))
        ImGui::SetClipboardText(opts.ini_path.c_str());

    ImGui::Separator();

    if (ImGui::BeginTabBar("##settings_tabs")) {
        struct Tab {
            const char* label;
            const char* section;
            Themes::MenuTable mt;
            bool namePath;
        };

        static const Tab tabs[] = {
            {"General", "general", Themes::MenuTable::GENERAL, false},
            {"Genomes", "genomes", Themes::MenuTable::GENOMES, true},
            {"Interaction", "interaction", Themes::MenuTable::INTERACTION, false},
            {"Labelling", "labelling", Themes::MenuTable::LABELLING, false},
            {"Navigation", "navigation", Themes::MenuTable::NAVIGATION, false},
            {"Tracks", "tracks", Themes::MenuTable::TRACKS, true},
            {"View Thresholds", "view_thresholds", Themes::MenuTable::VIEW_THRESHOLDS, false},
            {"Shift Keymap", "shift_keymap", Themes::MenuTable::SHIFT_KEYMAP, false}
        };

        for (const auto& tab : tabs) {
            if (!ImGui::BeginTabItem(tab.label)) continue;

            if (tab.namePath) {
                renderNamePathTable(tab.section, tab.mt, opts, redraw, plot);
            } else {
                ImGui::BeginChild(tab.section);
                for (auto& kv : opts.myIni[tab.section]) {
                    if (kv.first == "fmt" || kv.first == "miny") continue;
                    renderIniWidget(kv.first, tab.mt, opts, redraw, plot);
                }
                ImGui::EndChild();
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace Menu
