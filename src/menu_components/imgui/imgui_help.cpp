// imgui_help.cpp
// Help dialog (documentation link, demo loader, hot keys)
// Extracted from menu.cpp with no functional changes.

#include <string>
#include <iostream>

#include "menu.h"
#include "plot_manager.h"
#include "plot_commands.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imfilebrowser.h"

namespace Menu {

void drawImGuiHelpDialog(Manager::GwPlot* plot,
                         bool* p_open,
                         bool& redraw)
{
    if (!p_open || !*p_open)
        return;

    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowSize(
        ImVec2(std::min(420.f, io.DisplaySize.x * 0.9f),
               std::min(400.f, io.DisplaySize.y * 0.8f)),
        ImGuiCond_FirstUseEver);

    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f,
               io.DisplaySize.y * 0.5f),
        ImGuiCond_FirstUseEver,
        ImVec2(0.5f, 0.5f));

    if (!ImGui::Begin("Help", p_open,
                      ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    // -------------------------------------------------------------------------
    // Documentation link
    // -------------------------------------------------------------------------

    ImGui::TextWrapped(
        "For the full user guide, commands reference and tutorials visit:");

    ImGui::Spacing();

    ImGui::TextColored(
        ImVec4(0.4f, 0.7f, 1.0f, 1.0f),
        "https://kcleal.github.io/gw/docs/guide/User_guide.html");

    ImGui::SameLine();

    static float copyTimer = 0.f;

    if (copyTimer > 0.f) {
        ImGui::TextDisabled("Copied!");
        copyTimer -= ImGui::GetIO().DeltaTime;
    } else {
        if (ImGui::SmallButton("Copy")) {
            ImGui::SetClipboardText(
                "https://kcleal.github.io/gw/docs/guide/User_guide.html");
            copyTimer = 2.0f;
        }
    }

    ImGui::Spacing();
    ImGui::Separator();

    // -------------------------------------------------------------------------
    // Demo
    // -------------------------------------------------------------------------

    if (ImGui::CollapsingHeader("Demo", ImGuiTreeNodeFlags_DefaultOpen)) {

        ImGui::TextWrapped(
            "Load online BAM demo data and hg19 genome, "
            "then navigate to a deletion at "
            "chr8:37,047,270-37,055,161.");

        ImGui::Spacing();

        if (ImGui::Button("Load Demo")) {

            std::ostream& out =
                (plot->terminalOutput) ? std::cout : plot->outStr;

            if (plot->reference.empty()) {
                plot->loadGenome("hg19", out);
            }

            std::string loadCmd =
                "load https://github.com/kcleal/gw/releases/download/"
                "v1.0.0/demo1.bam";

            Commands::run_command_map(plot, loadCmd, out);

            std::string navCmd =
                "chr8:37047270-37055161";

            Commands::run_command_map(plot, navCmd, out);

            redraw = true;
            plot->processed = false;
        }
    }

    // -------------------------------------------------------------------------
    // Hot keys
    // -------------------------------------------------------------------------

    if (ImGui::CollapsingHeader("Hot keys")) {

        struct HotKey {
            const char* label;
            const char* key;
        };

        static HotKey hotkeys[] = {
            {"Scroll left", "LEFT"},
            {"Scroll right", "RIGHT"},
            {"Scroll down", "PAGE_DOWN, CTRL + ["},
            {"Scroll up", "PAGE_UP, CTRL + ]"},
            {"Zoom in", "UP"},
            {"Zoom out", "DOWN"},
            {"Zoom to cursor", "CTRL + LEFT_MOUSE"},
            {"Sort at cursor", "S"},
            {"Decrease ylim", "CTRL + KEY_MINUS"},
            {"Increase ylim", "CTRL + KEY_EQUALS"},
            {"Next region view", "]"},
            {"Previous region view", "["},
            {"Cycle link mode", "L"},
            {"Find alignments", "F"},
            {"Repeat last command", "CTRL + R"},
            {"Resize window", "SHIFT + ARROW_KEY"},
            {"Switch viewing mode", "TAB"},
        };

        for (const auto& hk : hotkeys) {
            ImGui::BulletText("%s", hk.label);
            ImGui::SameLine(200.f);
            ImGui::TextDisabled("%s", hk.key);
        }
    }

    ImGui::End();
}

} // namespace Menu