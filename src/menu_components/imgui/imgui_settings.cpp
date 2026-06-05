#include "menu.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imfilebrowser.h"

#include <sstream>
#include <regex>

namespace Menu {

#if !defined(__EMSCRIPTEN__)
// Simple JSON entry for IGV online genomes.
struct OnlineGenome {
    std::string id;
    std::string name;
    std::string fastaURL;
};

// Flag shared between renderNamePathTable (button) and drawImGuiSettings (popup).
static bool openOnlineGenomesPopup = false;

// Parse the IGV genomes JSON.  We only need id, name and fastaURL from each
// top-level object in the array.  Objects may contain nested arrays/objects
// (e.g. "tracks"), so we split by tracking brace depth rather than regex.
static std::vector<OnlineGenome> parseIgvGenomes(const std::string& jsonText) {
    std::vector<OnlineGenome> result;

    // Find array content between [ and ]
    size_t arrStart = jsonText.find('[');
    size_t arrEnd   = jsonText.rfind(']');
    if (arrStart == std::string::npos || arrEnd == std::string::npos || arrEnd <= arrStart)
        return result;

    // Split top-level objects by tracking brace depth, respecting strings.
    std::vector<std::string> objects;
    std::string current;
    int depth = 0;
    bool inString = false;
    bool escape = false;

    for (size_t i = arrStart + 1; i < arrEnd; ++i) {
        char ch = jsonText[i];
        if (escape) {
            current += ch;
            escape = false;
            continue;
        }
        if (ch == '\\') {
            current += ch;
            escape = true;
            continue;
        }
        if (ch == '"' && !inString) {
            inString = true;
            current += ch;
            continue;
        }
        if (ch == '"' && inString) {
            inString = false;
            current += ch;
            continue;
        }
        if (inString) {
            current += ch;
            continue;
        }
        if (ch == '{') {
            ++depth;
            current += ch;
        } else if (ch == '}') {
            --depth;
            current += ch;
            if (depth == 0) {
                // trim whitespace
                size_t a = 0, b = current.size();
                while (a < b && std::isspace(static_cast<unsigned char>(current[a]))) ++a;
                while (b > a && std::isspace(static_cast<unsigned char>(current[b - 1]))) --b;
                if (a < b)
                    objects.emplace_back(current.substr(a, b - a));
                current.clear();
            }
        } else if (ch == ',' && depth == 0) {
            // skip separator between top-level objects
        } else {
            current += ch;
        }
    }

    static const std::regex idRe(R"--("id"\s*:\s*"([^"]*)")--");
    static const std::regex nameRe(R"--("name"\s*:\s*"([^"]*)")--");
    static const std::regex fastaRe(R"--("fastaURL"\s*:\s*"([^"]*)")--");

    for (const std::string& body : objects) {
        std::smatch m;
        OnlineGenome g;
        if (std::regex_search(body, m, idRe))    g.id = m[1];
        if (std::regex_search(body, m, nameRe))  g.name = m[1];
        if (std::regex_search(body, m, fastaRe)) g.fastaURL = m[1];
        if (!g.id.empty() && !g.fastaURL.empty())
            result.push_back(std::move(g));
    }
    return result;
}
#endif

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
    static std::string genomeLoadError;  // modal error message

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
            const std::string& name = pair.first;
            ImGui::PushID(name.c_str());
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            // Genome names are clickable buttons; the active one is underlined.
            bool isActiveGenome = (section == "genomes" && name == opts.genome_tag);
            if (isActiveGenome) {
                ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
                ImGui::Text("%s  *", name.c_str());
                // Draw an underline beneath the active-genome label.
                {
                    ImVec2 rMin = ImGui::GetItemRectMin();
                    ImVec2 rMax = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddLine(
                        ImVec2(rMin.x, rMax.y),
                        ImVec2(rMin.x + ImGui::CalcTextSize(name.c_str()).x, rMax.y),
                        ImGui::ColorConvertFloat4ToU32(textColor), 1.0f);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Active genome");
            } else if (section == "genomes") {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 0.2f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.4f, 0.4f, 0.4f, 0.4f));
                if (ImGui::SmallButton(name.c_str())) {
                    opts.genome_tag = name;
                    if (plot) {
                        std::ostringstream capture;
                        plot->loadGenome(name, capture);
                        std::string output = capture.str();
                        if (output.find("Error:") != std::string::npos) {
                            genomeLoadError = output;
                            ImGui::OpenPopup("Genome Load Error");
                        }
                        redraw = true;
                        plot->processed = false;
                    }
                }
                ImGui::PopStyleColor(3);
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("Load %s", name.c_str());
            } else {
                ImGui::TextUnformatted(name.c_str());
            }

            ImGui::TableSetColumnIndex(1);
            char buf[512];
            strncpy(buf, pair.second.c_str(), sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            ImGui::SetNextItemWidth(-1.f);
            if (ImGui::InputText("##path", buf, sizeof(buf),
                                 ImGuiInputTextFlags_EnterReturnsTrue)) {
                Option opt(name, Path, buf, mt);
                applyPathOption(opt, opts);
                redraw = true;
            }

            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("Delete"))
                to_delete = name;

            ImGui::PopID();
        }

        if (!to_delete.empty()) {
            opts.myIni[section].remove(to_delete);
            if (section == "genomes" && opts.genome_tag == to_delete)
                opts.genome_tag.clear();
            redraw = true;
        }

        ImGui::EndTable();
    }

    // ── Error modal for genome load failures ────────────────────────────────
    if (!genomeLoadError.empty()) {
        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Genome Load Error", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoNav)) {
            ImGui::TextUnformatted(genomeLoadError.c_str());
            ImGui::Separator();
            if (ImGui::Button("OK", ImVec2(120, 0))) {
                genomeLoadError.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

#if !defined(__EMSCRIPTEN__)
    // ── Online genomes button (Genomes tab only) ────────────────────────────
    if (section == "genomes") {
        ImGui::Separator();
        if (ImGui::Button("Online genomes"))
            openOnlineGenomesPopup = true;
    }
#endif

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
#if !defined(__EMSCRIPTEN__)
static void drawOnlineGenomesDialog(
    Themes::IniOptions& opts,
    bool& redraw,
    Manager::GwPlot* plot
);
#endif

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

#if !defined(__EMSCRIPTEN__)
    // Online genomes modal is triggered from renderNamePathTable but rendered here
    // so it isn't clipped by the tab content window.
    if (openOnlineGenomesPopup) {
        ImGui::OpenPopup("Online Genomes");
        openOnlineGenomesPopup = false;
    }
    drawOnlineGenomesDialog(opts, redraw, plot);
#endif

    ImGui::End();
}

#if !defined(__EMSCRIPTEN__)
// ----------------------------------------------------------------------------
// Online genomes dialog
// ----------------------------------------------------------------------------
static void drawOnlineGenomesDialog(
    Themes::IniOptions& opts,
    bool& redraw,
    Manager::GwPlot* plot
) {
    static std::vector<OnlineGenome> onlineGenomes;
    static bool fetchInProgress = false;
    static std::string fetchError;
    static char filterBuf[128] = {};

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 420), ImGuiCond_Appearing);

    bool popupOpen = true;
    if (!ImGui::BeginPopupModal("Online Genomes", &popupOpen,
                                ImGuiWindowFlags_NoCollapse))
        return;

    // ── Fetch on first open ─────────────────────────────────────────────────
    if (onlineGenomes.empty() && !fetchInProgress && fetchError.empty()) {
        fetchInProgress = true;
        try {
            std::string json = Utils::fetchOnlineFileContent(
                "https://s3.amazonaws.com/igv.org.genomes/genomes.json");
            onlineGenomes = parseIgvGenomes(json);
            fetchInProgress = false;
        } catch (const std::exception& e) {
            fetchError = e.what();
            fetchInProgress = false;
        }
    }

    if (fetchInProgress) {
        ImGui::Text("Loading genome list…");
        ImGui::EndPopup();
        return;
    }

    if (!fetchError.empty()) {
        ImGui::TextColored(ImVec4(1, 0, 0, 1), "Error: %s", fetchError.c_str());
        if (ImGui::Button("Retry")) {
            fetchError.clear();
            onlineGenomes.clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Close"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    // ── Filter ──────────────────────────────────────────────────────────────
    ImGui::InputTextWithHint("##filter", "Filter genomes…", filterBuf,
                             sizeof(filterBuf));

    // ── Table ───────────────────────────────────────────────────────────────
    constexpr ImGuiTableFlags tflags =
        ImGuiTableFlags_Borders |
        ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingStretchProp;

    std::string selectedId;   // genome to add this frame
    bool closePopup = false;

    if (ImGui::BeginTable("##online_genomes_tbl", 3, tflags,
                          ImVec2(0, -ImGui::GetFrameHeightWithSpacing()))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("ID",   ImGuiTableColumnFlags_WidthFixed, 90.f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("",     ImGuiTableColumnFlags_WidthFixed, 60.f);
        ImGui::TableHeadersRow();

        std::string filterLower = filterBuf;
        std::transform(filterLower.begin(), filterLower.end(),
                       filterLower.begin(), ::tolower);

        for (const auto& g : onlineGenomes) {
            // apply filter
            if (!filterLower.empty()) {
                std::string idLower = g.id;
                std::string nameLower = g.name;
                std::transform(idLower.begin(), idLower.end(), idLower.begin(), ::tolower);
                std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                if (idLower.find(filterLower) == std::string::npos &&
                    nameLower.find(filterLower) == std::string::npos)
                    continue;
            }

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(g.id.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(g.name.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", g.fastaURL.c_str());

            ImGui::TableSetColumnIndex(2);
            // If already in ini, show "Update" instead of "Open"
            bool alreadyExists = opts.myIni["genomes"].has(g.id);
            const char* btnLabel = alreadyExists ? "Update" : "Open";
            ImGui::PushID(g.id.c_str());
            if (ImGui::SmallButton(btnLabel)) {
                selectedId = g.id;
                // add / update in ini
                opts.myIni["genomes"][g.id] = g.fastaURL;
                opts.genome_tag = g.id;
                redraw = true;
                if (plot) {
                    std::ostringstream capture;
                    plot->loadGenome(g.id, capture);
                    std::string output = capture.str();
                    if (output.find("Error:") != std::string::npos) {
                        // Show error inline – the modal is about to close,
                        // so we rely on the Genome Load Error modal in
                        // renderNamePathTable to catch it.  To trigger it
                        // we would need a shared static, but for simplicity
                        // just print to the terminal output stream.
                        if (plot->terminalOutput)
                            std::cout << output << "\n";
                        else
                            plot->outStr << output << "\n";
                    } else {
                        plot->processed = false;
                    }
                }
                closePopup = true;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (closePopup || !popupOpen)
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}
#endif

} // namespace Menu
