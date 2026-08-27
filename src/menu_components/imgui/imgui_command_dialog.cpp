// imgui_command_dialog.cpp
// ImGui command / tools dialog
// Extracted from menu.cpp with no functional changes.

#include <sstream>
#include <streambuf>
#include <string>
#include <vector>
#include <iostream>

#include "menu.h"
#include "plot_manager.h"
#include "plot_commands.h"

#include "imgui.h"
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

static void execCommand(Manager::GwPlot* plot,
                        const std::string& cmd_str,
                        bool& redraw)
{
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
    static char snapshot_text[256] = {};
    static char manual_filter[256] = {};
    static int mapq_min = 0;

    static bool flag_include[11] = {};
    static bool flag_exclude[11] = {};

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
    float ms = std::max(plot->monitorScale, 1.0f);

    // Anchor to the left side of the screen.
    ImGui::SetNextWindowPos(
        ImVec2(0.f, 0.f),
        ImGuiCond_Appearing,
        ImVec2(0.f, 0.f));

    ImGui::SetNextWindowSize(ImVec2(350.f, 540.f * ms),
                             ImGuiCond_Appearing);

    ImGui::SetNextWindowSizeConstraints(
        ImVec2(240.f, 300.f * ms),
        ImVec2(io.DisplaySize.x, io.DisplaySize.y));

    if (!ImGui::Begin("Tools", p_open,
                      ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
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
        }

        ImGui::SameLine();

        if (ImGui::Button("Clear Filters")) {
            execCommand(plot, "refresh", redraw);
            mapq_min = 0;
            for (int i = 0; i < 11; ++i) {
                flag_include[i] = false;
                flag_exclude[i] = false;
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
            ImGui::EndTable();
        }

        // Active filters read-out (placed after the flag table so it never
        // shifts the position of the checkboxes when it appears/disappears).
        {
            bool hasActive = (mapq_min > 0);
            for (int i = 0; i < 11 && !hasActive; ++i) {
                if (flag_include[i] || flag_exclude[i]) hasActive = true;
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
    // Actions
    // -------------------------------------------------------------------------

    if (ImGui::CollapsingHeader("Actions")) {

        ImGui::SetNextItemWidth(
            ImGui::GetContentRegionAvail().x - 60.f);

        bool find_enter =
            ImGui::InputText("##find",
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

        ImGui::SetNextItemWidth(
            ImGui::GetContentRegionAvail().x - 75.f);

        bool snap_enter =
            ImGui::InputText("##snapshot",
                             snapshot_text,
                             sizeof(snapshot_text),
                             ImGuiInputTextFlags_EnterReturnsTrue);

        ImGui::SameLine();

        if (ImGui::Button("Snapshot") || snap_enter) {
            std::string cmd = "snapshot";
            if (snapshot_text[0]) {
                cmd += " ";
                cmd += snapshot_text;
            }
            execCommand(plot, cmd, redraw);
            snapshot_text[0] = '\0';
        }
    }

    ImGui::End();
}

} // namespace Menu