// imgui_command_dialog.cpp
// ImGui command / tools dialog
// Extracted from menu.cpp with no functional changes.

#include <sstream>
#include <streambuf>
#include <string>
#include <vector>
#include <iostream>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#include "menu.h"
#include "plot_manager.h"
#include "plot_commands.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imfilebrowser.h"

#include <GLFW/glfw3.h>


namespace Menu {

// -----------------------------------------------------------------------------
// Helpers: execute commands from UI
// -----------------------------------------------------------------------------

static void appendCommandOutput(Manager::GwPlot* plot, const std::string& text) {
    if (text.empty()) return;
    constexpr size_t maxHistory = 64 * 1024;  // cap total retained text
    if (plot->lastCommandOutputAnsi.empty()) {
        plot->lastCommandOutputAnsi = text;
    } else {
        plot->lastCommandOutputAnsi += text;
    }
    if (plot->lastCommandOutputAnsi.size() > maxHistory) {
        plot->lastCommandOutputAnsi.erase(
            0, plot->lastCommandOutputAnsi.size() - maxHistory);
    }
    plot->lastCommandOutputFrame = plot->frameId;
    plot->showCommandStatus = true;
    if (plot->window) {
        glfwPostEmptyEvent();
    }
}

// Returns the captured command output (empty in remote mode)
static std::string execCommand(Manager::GwPlot* plot,
                               const std::string& cmd_str,
                               bool& redraw)
{
    if (plot->remoteCommand) { plot->remoteCommand(cmd_str); return ""; }
    std::string cmd = cmd_str;
    std::ostringstream capture;
    Commands::run_command_map(plot, cmd, capture);

    std::string captured = capture.str();
    if (!captured.empty() && captured.front() != '\n' && captured.front() != '\r') {
        captured.insert(captured.begin(), '\n');
    }

    std::ostream& termOut = plot->terminalOutput ? std::cout : plot->outStr;
    termOut << captured;

    appendCommandOutput(plot, captured);
    redraw = true;
    return captured;
}

// Plot overlays (read scrollbars, ylim box, label close buttons) are ImGui windows
// that sit on the canvas. Hide them while the pointer is over any other window,
// such as a read info popup or dialog, so they never show through it.
static bool pointerOverOtherWindow() {
    const ImGuiWindow* w = GImGui->HoveredWindow;
    if (!w) return false;
    static const char* overlays[] = {"##alignment_scroll_", "##ylim_box", "##datalabel"};
    for (const char* prefix : overlays) {
        if (std::strncmp(w->RootWindow->Name, prefix, std::strlen(prefix)) == 0) return false;
    }
    return true;
}

// Stroke a glyph line exactly between two points. ImDrawList::AddLine shifts by half
// a pixel, which pushes small centred glyphs (x, +, -) off centre.
static void strokeGlyphLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col, float thickness) {
    dl->PathLineTo(a);
    dl->PathLineTo(b);
    dl->PathStroke(col, 0, thickness);
}

static std::string toolsStripAnsi(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            while (i < s.size() && s[i] != 'm') ++i;
            continue;
        }
        if (s[i] != '\r') out += s[i];
    }
    return out;
}

static std::string expandHome(const std::string& path) {
    if (path.size() >= 1 && path[0] == '~' && (path.size() == 1 || path[1] == '/')) {
        const char* home = std::getenv("HOME");
        if (home) return std::string(home) + path.substr(1);
    }
    return path;
}

// Parsed output of the count command: one column per read collection
struct CountTable {
    std::vector<std::string> columns;            // "file\nregion"
    std::vector<std::string> metrics;            // row names in first-seen order
    std::vector<std::vector<std::string>> cells; // [metric][column]
    std::string filter;
};

static CountTable parseCountOutput(const std::string& text) {
    CountTable t;
    std::istringstream in(toolsStripAnsi(text));
    std::string line;
    int col = -1;
    while (std::getline(in, line)) {
        size_t tab = line.find('\t');
        if (tab == std::string::npos) continue;
        std::string key = line.substr(0, tab), val = line.substr(tab + 1);
        if (key == "File") {
            t.columns.push_back(std::filesystem::path(val).filename().string());
            col = (int)t.columns.size() - 1;
            for (auto& row : t.cells) row.resize(t.columns.size());
            continue;
        }
        if (col < 0) continue;
        if (key == "Region") { t.columns[col] += "\n" + val; continue; }
        if (key == "Filter") { t.filter = val; continue; }
        auto it = std::find(t.metrics.begin(), t.metrics.end(), key);
        size_t r = it - t.metrics.begin();
        if (it == t.metrics.end()) {
            t.metrics.push_back(key);
            t.cells.emplace_back(t.columns.size());
        }
        t.cells[r][col] = val;
    }
    return t;
}


// -----------------------------------------------------------------------------
// Right-hand dock shared by the Tools and Variants windows
// -----------------------------------------------------------------------------

// Title-bar button of the current window, `slot` positions to the left of the close X.
// Returns the button's screen rect so callers can anchor popups to it.
static ImRect titleBarSlot(int slot) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    float titleH = ImGui::GetFrameHeight();
    float controlW = titleH - 4.0f;
    float x = window->Pos.x + window->Size.x - titleH - (controlW + 4.0f) * (slot + 1);
    return ImRect(ImVec2(x, window->Pos.y + 2.0f), ImVec2(x + controlW, window->Pos.y + 2.0f + controlW));
}

// Runs `body` with the cursor placed in a title-bar slot, leaving the content layout untouched
template <typename F>
static void inTitleBar(int slot, F body) {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    ImVec2 content = ImGui::GetCursorScreenPos();
    ImVec2 contentMax = window->DC.CursorMaxPos;
    float titleH = ImGui::GetFrameHeight();
    ImGui::PushClipRect(window->Pos, ImVec2(window->Pos.x + window->Size.x, window->Pos.y + titleH), false);
    ImRect r = titleBarSlot(slot);
    ImGui::SetCursorScreenPos(r.Min);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
    // Match the other title-bar actions: clear at rest, themed on hover/press.
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    body(r);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
    ImGui::PopClipRect();
    ImGui::SetCursorScreenPos(content);
    window->DC.CursorMaxPos = contentMax;
}

// Slightly darker title bars mark Tools and Variants as the main headings of the dock
static void pushDockTitleColors() {
    auto darker = [](ImVec4 c) { return ImVec4(c.x * 0.72f, c.y * 0.72f, c.z * 0.72f, c.w); };
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleColor(ImGuiCol_TitleBg, darker(style.Colors[ImGuiCol_TitleBg]));
    ImGui::PushStyleColor(ImGuiCol_TitleBgActive, darker(style.Colors[ImGuiCol_TitleBgActive]));
    ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, darker(style.Colors[ImGuiCol_TitleBgCollapsed]));
}

// Where a docked Variants table starts when Tools is docked above it. Collapsed
// windows shrink to their title bar and the other window takes the rest.
static float dockSplitY(const Manager::GwPlot* plot, float displayH) {
    float titleH = ImGui::GetFrameHeight();
    if (plot->toolsCollapsed) return titleH;
    if (plot->labelTableCollapsed) return displayH - titleH;
    return displayH * 0.5f;
}

static void drawDockTitleButton(bool& docked, int slot) {
    inTitleBar(slot, [&](const ImRect& r) {
        if (ImGui::Button("D##dock_toggle", r.GetSize())) docked = !docked;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip(docked ? "Undock" : "Dock");
    });
}

// Drag the dock's left edge to resize it; the width is shared by both windows
static void drawDockResizeHandle(Manager::GwPlot* plot, float currentW) {
    ImGuiIO& io = ImGui::GetIO();
    ImGuiWindow* dockWindow = ImGui::GetCurrentWindow();
    ImVec2 content = ImGui::GetCursorScreenPos();
    ImVec2 contentMax = dockWindow->DC.CursorMaxPos;
    ImVec2 pos = ImGui::GetWindowPos();
    ImGui::SetCursorScreenPos(ImVec2(pos.x, pos.y + ImGui::GetFrameHeight()));
    ImGui::PushClipRect(pos, ImVec2(pos.x + ImGui::GetWindowWidth(), pos.y + ImGui::GetWindowHeight()), false);
    ImGui::InvisibleButton("##dock_resize", ImVec2(6.0f, std::max(1.0f, ImGui::GetWindowHeight() - ImGui::GetFrameHeight())));
    ImGui::PopClipRect();
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    if (ImGui::IsItemActive())
        plot->labelTableWidth = std::max(180.0f, std::min(io.DisplaySize.x * 0.5f, currentW - io.MouseDelta.x));
    ImGui::SetCursorScreenPos(content);
    // The resize hit area must not enlarge the scrollable contents.
    dockWindow->DC.CursorMaxPos = contentMax;
}

// -----------------------------------------------------------------------------
// ImGui Command / Tools Dialog
// -----------------------------------------------------------------------------

void drawImGuiCommandDialog(Manager::GwPlot* plot,
                            bool* p_open,
                            bool& redraw)
{
    if (!p_open || !*p_open)
        return;

    // Persistent dialog state
    static char find_text[256] = {};
    static char manual_filter[256] = {};
    static int mapq_min = 0;

    static bool flag_include[11] = {};
    static bool flag_exclude[11] = {};

    // Read-pair orientation patterns, as understood by "pattern == <code>" filters
    static const char* pattern_labels[] = {
        "Deletion", "Duplication", "Inversion (forward)", "Inversion (reverse)", "Translocation",
    };
    static const char* pattern_codes[] = {"del", "dup", "inv_f", "inv_r", "tra"};
    constexpr int nPatterns = 5;
    static bool pattern_include[nPatterns] = {};
    static bool pattern_exclude[nPatterns] = {};

    static int sort_choice = 0;      // 0=none, 1=strand, 2=hap
    static int prev_sort_choice = 0;
    static char sort_pos[64] = {};

    static const char* flag_labels[] = {
        "Read paired",
        "Proper pair",
        "Mate unmapped",
        "Read reverse strand",
        "Mate reverse strand",
        "First in pair",
        "Second in pair",
        "Secondary alignment",
        "Fails quality checks",
        "PCR/optical duplicate",
        "Supplementary alignment",
    };

    ImGuiIO& io = ImGui::GetIO();
    // ImGui display coordinates are logical pixels in the remote canvas.
    float ms = plot->remoteCommand ? 1.0f : std::max(plot->monitorScale, 1.0f);

    // Tools shares the right-hand dock with the Variants table: same width, Tools
    // on top (collapsible), Variants below. Undocked it floats on the right.
    (void)ms;
    float topOff = plot->topMenuSpace / plot->monitorScale;
    float botOff = plot->sliderSpace / plot->monitorScale;
    bool variantsDocked = plot->labelTableDialogOpen && plot->labelTableDocked && !plot->variantTracks.empty();
    static bool wasDocked = false;
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing;
    float dockW = (plot->fb_width - plot->drawingWidth()) / plot->monitorScale;
    if (plot->toolsDocked && dockW > 0) {
        float h = variantsDocked ? dockSplitY(plot, io.DisplaySize.y) : io.DisplaySize.y;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - dockW, 0), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(dockW, h), ImGuiCond_Always);
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
    } else {
        float w = std::min(plot->labelTableWidth, io.DisplaySize.x);
        float h = std::max(100.0f, io.DisplaySize.y - topOff - botOff - 32.0f);
        ImGuiCond cond = wasDocked ? ImGuiCond_Always : ImGuiCond_Appearing;
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - w - 4.0f, topOff + 4.0f), cond);
        ImGui::SetNextWindowSize(ImVec2(w, h), cond);
        ImGui::SetNextWindowSizeConstraints(ImVec2(std::min(180.f, io.DisplaySize.x), 0.f),
                                            ImVec2(io.DisplaySize.x, io.DisplaySize.y));
    }
    bool docked = plot->toolsDocked && dockW > 0;
    wasDocked = docked;
    if (docked) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    }
    pushDockTitleColors();
    bool visible = ImGui::Begin("Tools", p_open, flags);
    ImGui::PopStyleColor(3);
    if (docked) ImGui::PopStyleVar(2);
    plot->toolsCollapsed = ImGui::IsWindowCollapsed();
    if (closeOnEscape(p_open)) redraw = true;
    drawDockTitleButton(plot->toolsDocked, 0);
    if (!visible)
    {
        ImGui::End();
        return;
    }
    if (docked) drawDockResizeHandle(plot, dockW);

    // -------------------------------------------------------------------------
    // Count
    // -------------------------------------------------------------------------

    if (ImGui::CollapsingHeader("Count")) {
        static char count_expr[256] = {};
        static bool count_with_filters = true;
        static CountTable count_table;
        static bool count_ran = false;

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60.f);
        bool count_enter = ImGui::InputTextWithHint("##count_expr", "optional filter, e.g. mapq >= 20",
                                                    count_expr, sizeof(count_expr),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        bool count_clicked = ImGui::Button("Count##run");  // "Count" alone collides with the header ID
        if (!plot->filters.empty()) {
            ImGui::Checkbox("Apply active filters", &count_with_filters);
        }
        if ((count_clicked || count_enter) && !plot->bams.empty()) {
            std::string expr;
            if (count_with_filters) {
                for (auto& f : plot->filters) {
                    if (!expr.empty()) expr += ";";
                    expr += f.filter_str;
                }
            }
            if (count_expr[0]) {
                if (!expr.empty()) expr += ";";
                expr += count_expr;
            }
            std::string cmd = expr.empty() ? "count" : "count " + expr;
            if (plot->remoteCommand) {
                plot->remoteCommand(cmd);
            } else {
                std::ostringstream capture;
                Commands::run_command_map(plot, cmd, capture);
                count_table = parseCountOutput(capture.str());
                count_ran = true;
            }
        }
        if (plot->bams.empty()) {
            ImGui::TextDisabled("Load an alignment file to count reads");
        } else if (count_ran && count_table.columns.empty()) {
            ImGui::TextDisabled("No reads in view");
        } else if (count_ran) {
            if (!count_table.filter.empty()) ImGui::TextDisabled("Filter: %s", count_table.filter.c_str());
            int ncol = (int)count_table.columns.size() + 1;
            if (ncol <= 64 && ImGui::BeginTable("##counts", ncol, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                                ImGuiTableFlags_ScrollX | ImGuiTableFlags_SizingFixedFit,
                                                ImVec2(0, (count_table.metrics.size() + 2.5f) * ImGui::GetTextLineHeightWithSpacing()))) {
                ImGui::TableSetupColumn("Reads");
                for (auto& c : count_table.columns) ImGui::TableSetupColumn(c.c_str());
                ImGui::TableHeadersRow();
                for (size_t r = 0; r < count_table.metrics.size(); ++r) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(count_table.metrics[r].c_str());
                    for (size_t c = 0; c < count_table.columns.size(); ++c) {
                        ImGui::TableSetColumnIndex((int)c + 1);
                        ImGui::TextUnformatted(count_table.cells[r][c].c_str());
                    }
                }
                ImGui::EndTable();
            }
            if (ImGui::SmallButton("Copy as TSV")) {
                std::string tsv = "reads";
                for (auto& c : count_table.columns) {
                    std::string h = c;
                    std::replace(h.begin(), h.end(), '\n', ' ');
                    tsv += "\t" + h;
                }
                tsv += "\n";
                for (size_t r = 0; r < count_table.metrics.size(); ++r) {
                    tsv += count_table.metrics[r];
                    for (auto& v : count_table.cells[r]) tsv += "\t" + v;
                    tsv += "\n";
                }
                ImGui::SetClipboardText(tsv.c_str());
            }
        }
    }

    // -------------------------------------------------------------------------
    // Filter
    // -------------------------------------------------------------------------

    if (ImGui::CollapsingHeader("Filter")) {

        static const int flag_bits[] = {
            1, 2, 8, 16, 32, 64, 128,
            256, 512, 1024, 2048
        };

        if (ImGui::Button("Apply Filters")) {

            execCommand(plot, "refresh", redraw);

            if (mapq_min > 0) {
                execCommand(
                    plot,
                    "filter mapq >= " + std::to_string(mapq_min),
                    redraw);
            }

            int include_bits = 0;
            int exclude_bits = 0;

            for (int i = 0; i < 11; ++i) {
                if (flag_include[i]) include_bits |= flag_bits[i];
                if (flag_exclude[i]) exclude_bits |= flag_bits[i];
            }

            if (include_bits)
                execCommand(plot,
                            "filter flag & " +
                            std::to_string(include_bits),
                            redraw);

            if (exclude_bits)
                execCommand(plot,
                            "filter ~flag & " +
                            std::to_string(exclude_bits),
                            redraw);

            // Kept patterns are alternatives (or); removed patterns must all be absent (and)
            std::string keep, drop;
            for (int i = 0; i < nPatterns; ++i) {
                if (pattern_include[i])
                    keep += (keep.empty() ? "" : " or ") + std::string("pattern == ") + pattern_codes[i];
                if (pattern_exclude[i])
                    drop += (drop.empty() ? "" : " and ") + std::string("pattern != ") + pattern_codes[i];
            }
            if (!keep.empty()) execCommand(plot, "filter " + keep, redraw);
            if (!drop.empty()) execCommand(plot, "filter " + drop, redraw);
        }

        ImGui::SameLine();

        if (ImGui::Button("Clear Filters")) {
            execCommand(plot, "refresh", redraw);
            mapq_min = 0;
            for (int i = 0; i < 11; ++i) {
                flag_include[i] = false;
                flag_exclude[i] = false;
            }
            for (int i = 0; i < nPatterns; ++i) {
                pattern_include[i] = false;
                pattern_exclude[i] = false;
            }
        }

        ImGui::Spacing();
        ImGui::SliderInt("Min MAPQ", &mapq_min, 0, 60);
        ImGui::Spacing();

        if (ImGui::BeginTable("##samflags", 3,
                              ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_BordersInnerV)) {

            ImGui::TableSetupColumn("Flag");
            ImGui::TableSetupColumn("Keep");
            ImGui::TableSetupColumn("Remove");
            ImGui::TableHeadersRow();

            for (int i = 0; i < 11; ++i) {
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(flag_labels[i]);

                ImGui::TableSetColumnIndex(1);
                if (ImGui::Checkbox(
                        ("##inc" + std::to_string(i)).c_str(),
                        &flag_include[i]))
                {
                    if (flag_include[i]) flag_exclude[i] = false;
                }

                ImGui::TableSetColumnIndex(2);
                if (ImGui::Checkbox(
                        ("##exc" + std::to_string(i)).c_str(),
                        &flag_exclude[i]))
                {
                    if (flag_exclude[i]) flag_include[i] = false;
                }
            }

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextDisabled("Pair pattern");
            for (int i = 0; i < nPatterns; ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(pattern_labels[i]);
                ImGui::TableSetColumnIndex(1);
                if (ImGui::Checkbox(("##pinc" + std::to_string(i)).c_str(), &pattern_include[i])) {
                    if (pattern_include[i]) pattern_exclude[i] = false;
                }
                ImGui::TableSetColumnIndex(2);
                if (ImGui::Checkbox(("##pexc" + std::to_string(i)).c_str(), &pattern_exclude[i])) {
                    if (pattern_exclude[i]) pattern_include[i] = false;
                }
            }
            ImGui::EndTable();
        }

        // Active filters read-out (placed after the flag table so it never
        // shifts the position of the checkboxes when it appears/disappears).
        {
            bool hasActive = (mapq_min > 0);
            for (int i = 0; i < 11 && !hasActive; ++i) {
                if (flag_include[i] || flag_exclude[i]) hasActive = true;
            }
            for (int i = 0; i < nPatterns && !hasActive; ++i) {
                if (pattern_include[i] || pattern_exclude[i]) hasActive = true;
            }
            if (hasActive) {
                ImGui::Spacing();
                ImGui::TextDisabled("Active filters:");
                if (mapq_min > 0)
                    ImGui::BulletText("MAPQ >= %d", mapq_min);
                for (int i = 0; i < 11; ++i) {
                    if (flag_include[i])
                        ImGui::BulletText("Keep %s", flag_labels[i]);
                    else if (flag_exclude[i])
                        ImGui::BulletText("Remove %s", flag_labels[i]);
                }
                for (int i = 0; i < nPatterns; ++i) {
                    if (pattern_include[i])
                        ImGui::BulletText("Keep %s pattern", pattern_labels[i]);
                    else if (pattern_exclude[i])
                        ImGui::BulletText("Remove %s pattern", pattern_labels[i]);
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Manual filter entry: prepend "filter " and send to the command parser.
        if (ImGui::InputTextWithHint("##manual_filter",
                                     "e.g. mapq >= 30 or flag & 1024",
                                     manual_filter,
                                     sizeof(manual_filter),
                                     ImGuiInputTextFlags_EnterReturnsTrue))
        {
            if (manual_filter[0]) {
                execCommand(plot, std::string("filter ") + manual_filter, redraw);
                manual_filter[0] = '\0';
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
            ImGui::SetTooltip("Type a filter expression and press Enter.\n"
                              "Examples:  mapq >= 30   flag & 1024   ~flag & 512");
        }
    }

    // -------------------------------------------------------------------------
    // Find
    // -------------------------------------------------------------------------

    if (ImGui::CollapsingHeader("Find read")) {

        ImGui::SetNextItemWidth(
            ImGui::GetContentRegionAvail().x - 60.f);

        bool find_enter =
            ImGui::InputTextWithHint("##find", "read name",
                             find_text,
                             sizeof(find_text),
                             ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::SameLine();

        if ((ImGui::Button("Find") || find_enter) &&
            find_text[0])
        {
            execCommand(plot,
                        "find " + std::string(find_text),
                        redraw);
            find_text[0] = '\0';
        }
    }

    // -------------------------------------------------------------------------
    // Introns
    // -------------------------------------------------------------------------

    if (ImGui::CollapsingHeader("Introns")) {
        Themes::IniOptions &opts = plot->opts;

        ImGui::TextWrapped(
            "RNA-seq splice junctions (N-op CIGAR). Toggle the intron track "
            "for a BAM, and tune how donor/acceptor positions are clustered.");
        ImGui::Spacing();

        if (ImGui::Button("Toggle intron track")) {
            execCommand(plot, "introns", redraw);
        }

        ImGui::Spacing();

        int eps = opts.splice_cluster_eps;
        if (ImGui::SliderInt("Cluster eps (bp)", &eps, 0, 20)) {
            opts.splice_cluster_eps = eps;
            redraw = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Maximum distance in bp between donor or acceptor positions "
                "that are merged into one canonical intron.");
        }

        int minR = opts.min_junction_reads;
        if (ImGui::SliderInt("Min support", &minR, 1, 50)) {
            opts.min_junction_reads = minR;
            redraw = true;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Minimum number of reads supporting a junction before it "
                "is drawn.");
        }
    }

    // -------------------------------------------------------------------------
    // Markers & regions of interest
    // -------------------------------------------------------------------------

    if (ImGui::CollapsingHeader("Markers & ROI")) {
        static int mark_kind = 0;  // 0=marker, 1=roi
        static char mark_pos[256] = {};
        static char roi_name[128] = {};
        static std::string mark_error;

        ImGui::RadioButton("Marker", &mark_kind, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Region of interest", &mark_kind, 1);

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 50.f);
        bool add_enter = ImGui::InputTextWithHint("##mark_pos",
                                                  mark_kind == 0 ? "chr1:1000 (blank = view centre)"
                                                                 : "chr1:1000-2000 (blank = view)",
                                                  mark_pos, sizeof(mark_pos),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        bool add_clicked = ImGui::Button("Add##mark");
        if (mark_kind == 1) {
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 50.f);
            add_enter |= ImGui::InputTextWithHint("##roi_name", "name (optional)", roi_name, sizeof(roi_name),
                                                  ImGuiInputTextFlags_EnterReturnsTrue);
        }
        if ((add_clicked || add_enter) && !plot->regions.empty()) {
            mark_error.clear();
            Utils::Region& view = plot->regions[plot->regionSelection];
            std::string pos = mark_pos;
            Utils::trim(pos);
            pos.erase(std::remove(pos.begin(), pos.end(), ','), pos.end());
            std::string cmd;
            if (mark_kind == 0) {
                if (pos.empty()) {
                    pos = view.chrom + ":" + std::to_string(view.start + (view.end - view.start) / 2);
                }
                cmd = "marker " + pos;
            } else {
                std::string name = roi_name;
                Utils::trim(name);
                if (pos.empty()) {
                    pos = view.toString();
                    pos = pos.substr(0, pos.find(':', pos.find(':') + 1));  // drop region markers
                } else {
                    try {
                        Utils::Region r = Utils::parseRegion(pos);
                        if (r.chrom != view.chrom) mark_error = "Region of interest must be on " + view.chrom;
                    } catch (...) {
                        mark_error = "Could not parse '" + pos + "'";
                    }
                }
                cmd = "roi " + pos + (name.empty() ? "" : " " + name);
            }
            if (mark_error.empty()) {
                execCommand(plot, cmd, redraw);
                mark_pos[0] = '\0';
                roi_name[0] = '\0';
            }
        }
        if (!mark_error.empty()) {
            ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.f), "%s", mark_error.c_str());
        }

        // Current markers and ROIs
        struct MarkRow { bool roi; std::string chrom, name; int start, end; };
        std::vector<MarkRow> rows;
        for (auto& m : plot->markers) {
            rows.push_back({false, m.chrom, "", m.start, m.end});
        }
        for (auto& t : plot->tracks) {
            if (t.kind != HGW::FType::ROI) continue;
            for (auto& kv : t.allBlocks) {
                for (size_t i = 0; i < kv.second.data.size(); ++i) {
                    std::string nm = kv.second.data[i].name;
                    Utils::trim(nm);
                    rows.push_back({true, kv.first, nm, kv.second.starts[i], kv.second.ends[i]});
                }
            }
        }
        ImGui::Spacing();
        if (rows.empty()) {
            ImGui::TextDisabled("No markers or regions of interest");
        } else if (ImGui::BeginTable("##marks", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
                                     ImGuiTableFlags_ScrollY, ImVec2(0, std::min(8, (int)rows.size() + 1) * ImGui::GetFrameHeightWithSpacing()))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Location");
            ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableHeadersRow();
            std::string pending;
            for (int i = 0; i < (int)rows.size(); ++i) {
                const auto& r = rows[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(r.roi ? "ROI" : "Marker");
                ImGui::TableSetColumnIndex(1);
                std::string loc = r.chrom + ":" + std::to_string(r.start);
                if (r.roi || r.end > r.start + 1) loc += "-" + std::to_string(r.end);
                ImGui::TextUnformatted(loc.c_str());
                if (!r.name.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", r.name.c_str());
                }
                ImGui::TableSetColumnIndex(2);
                if (ImGui::SmallButton("Go")) {
                    int width = plot->regions.empty() ? 2000
                              : std::max(100, plot->regions[plot->regionSelection].end - plot->regions[plot->regionSelection].start);
                    int mid = r.start + (r.end - r.start) / 2;
                    int s0 = std::max(1, r.roi ? std::min(r.start, mid - width / 2) : mid - width / 2);
                    int e0 = r.roi ? std::max(r.end, mid + width / 2) : mid + width / 2;
                    pending = "goto " + r.chrom + ":" + std::to_string(s0) + "-" + std::to_string(e0);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("x")) {
                    pending = r.roi ? "remove-roi " + loc : "remove-marker " + r.chrom + ":" + std::to_string(r.start);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove");
                ImGui::PopID();
            }
            ImGui::EndTable();
            if (!pending.empty()) execCommand(plot, pending, redraw);
        }
        if (!plot->markers.empty()) {
            if (ImGui::Button("Clear markers")) execCommand(plot, "clear-markers", redraw);
            ImGui::SameLine();
        }
        bool hasRoi = std::any_of(rows.begin(), rows.end(), [](const MarkRow& r) { return r.roi; });
        if (hasRoi && ImGui::Button("Clear ROIs")) execCommand(plot, "clear-roi", redraw);
    }

    // -------------------------------------------------------------------------
    // Save
    // -------------------------------------------------------------------------

    if (ImGui::CollapsingHeader("Save")) {
        struct SaveFormat { const char* label; const char* ext; bool image; };
#ifndef __EMSCRIPTEN__
        static const SaveFormat formats[] = {
            {"PNG image", ".png", true}, {"SVG image", ".svg", true}, {"PDF image", ".pdf", true},
            {"Alignments (BAM)", ".bam", false}, {"Alignments (SAM)", ".sam", false},
            {"Session (.ini)", ".ini", false},
        };
#else
        static const SaveFormat formats[] = {
            {"PNG image", ".png", true}, {"Alignments (BAM)", ".bam", false},
            {"Alignments (SAM)", ".sam", false}, {"Session (.ini)", ".ini", false},
        };
#endif
        constexpr int nFormats = (int)(sizeof(formats) / sizeof(formats[0]));
        static int save_fmt = 0;
        static char save_path[512] = {};
        static std::string save_status;
        static bool save_ok = true;

        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::BeginCombo("##save_fmt", formats[save_fmt].label)) {
            for (int i = 0; i < nFormats; ++i) {
                if (ImGui::Selectable(formats[i].label, i == save_fmt)) save_fmt = i;
            }
            ImGui::EndCombo();
        }
        const SaveFormat& fmt = formats[save_fmt];
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 50.f);
        bool save_enter = ImGui::InputTextWithHint("##save_path",
                                                   fmt.image ? "file name (blank = automatic)" : "file name",
                                                   save_path, sizeof(save_path),
                                                   ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if (ImGui::Button("Save##save") || save_enter) {
            std::string path = save_path;
            Utils::trim(path);
            path = expandHome(path);
            save_status.clear();
            save_ok = false;
            if (path.empty() && !fmt.image) {
                save_status = "Enter a file name";
            } else if (path.find(' ') != std::string::npos) {
                save_status = "File names cannot contain spaces";
            } else if (fmt.image && plot->regions.empty() && plot->mode == Manager::Show::SINGLE) {
                save_status = "Nothing to save yet";
            } else if (!fmt.image && save_fmt != nFormats - 1 && plot->bams.empty()) {
                save_status = "No alignment files loaded";
            } else {
                if (!path.empty() && !Utils::endsWith(path, fmt.ext)) path += fmt.ext;
                std::string cmd = fmt.image ? (path.empty() ? "snapshot" : "snapshot " + path) : "save " + path;
                std::string res = toolsStripAnsi(execCommand(plot, cmd, redraw));
                Utils::trim(res);
                save_ok = res.find("Error") == std::string::npos;
                save_status = res.empty() ? (plot->remoteCommand ? "Sent" : "Saved " + path) : res;
                if (save_ok) save_path[0] = '\0';
            }
        }
        if (!save_status.empty()) {
            ImGui::PushTextWrapPos(0.f);
            if (save_ok) ImGui::TextDisabled("%s", save_status.c_str());
            else ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.f), "%s", save_status.c_str());
            ImGui::PopTextWrapPos();
        }
        if (!fmt.image && save_fmt != nFormats - 1) {
            ImGui::TextDisabled("Saves the reads loaded in the current view");
        }
    }

    // -------------------------------------------------------------------------
    // Sort
    // -------------------------------------------------------------------------

    if (ImGui::CollapsingHeader("Sort")) {

        ImGui::RadioButton("None", &sort_choice, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Strand", &sort_choice, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Haplotype", &sort_choice, 2);

        if (sort_choice != prev_sort_choice) {
            prev_sort_choice = sort_choice;

            std::string cmd;
            if (sort_choice == 0) cmd = "refresh";
            else if (sort_choice == 1) cmd = "sort strand";
            else if (sort_choice == 2) cmd = "sort hap";

            if (!cmd.empty())
                execCommand(plot, cmd, redraw);
        }

        ImGui::SetNextItemWidth(
            ImGui::GetContentRegionAvail().x - 60.f);

        bool enter_pressed =
            ImGui::InputTextWithHint("##sort_pos",
                                     "enter position",
                                     sort_pos,
                                     sizeof(sort_pos),
                                     ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::SameLine();

        if ((ImGui::Button("Apply") || enter_pressed) &&
            sort_pos[0])
        {
            std::string cmd = "sort ";
            if (sort_choice == 1) cmd += "strand ";
            else if (sort_choice == 2) cmd += "hap ";
            cmd += sort_pos;

            execCommand(plot, cmd, redraw);
            sort_pos[0] = '\0';
        }
    }

    ImGui::End();
}

} // namespace Menu