// imgui_command_dialog.cpp
// ImGui command / tools dialog
// Extracted from menu.cpp with no functional changes.

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


namespace Menu {

// -----------------------------------------------------------------------------
// Helpers: execute commands from UI
// -----------------------------------------------------------------------------

static void execCommand(Manager::GwPlot* plot,
                        const std::string& cmd_str,
                        bool& redraw)
{
    std::string cmd = cmd_str;
    std::ostream& out =
        (plot->terminalOutput) ? std::cout : plot->outStr;

    Commands::run_command_map(plot, cmd, out);
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

    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f,
               io.DisplaySize.y * 0.5f),
        ImGuiCond_Appearing,
        ImVec2(0.5f, 0.5f));

    ImGui::SetNextWindowSize(ImVec2(550.f, 540.f),
                             ImGuiCond_Appearing);

    ImGui::SetNextWindowSizeConstraints(
        ImVec2(400.f, 300.f),
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