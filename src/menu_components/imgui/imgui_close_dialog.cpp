// imgui_close_dialog.cpp
// Close-file dialog: lists open BAM/track/variant files for removal
// Extracted from menu.cpp with no functional changes.

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "menu.h"
#include "plot_manager.h"
#include "plot_commands.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imfilebrowser.h"

namespace Menu {

void drawImGuiCloseDialog(Manager::GwPlot* plot, bool* p_open, bool& redraw)
{
    if (!p_open || !*p_open)
        return;

    ImGuiIO& io = ImGui::GetIO();

    ImGui::SetNextWindowSize(
        ImVec2(std::min(480.f, io.DisplaySize.x * 0.85f),
               std::min(320.f, io.DisplaySize.y * 0.70f)),
        ImGuiCond_FirstUseEver);

    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f,
               io.DisplaySize.y * 0.5f),
        ImGuiCond_FirstUseEver,
        ImVec2(0.5f, 0.5f));

    if (!ImGui::Begin("Close File", p_open,
                      ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    // Build flat list of files
    enum class FileEntryType {
        Bam,
        Track,
        Variant
    };

    struct FileEntry {
        FileEntryType type;
        int index;
        std::string label;
    };

    std::vector<FileEntry> entries;

    for (int i = 0; i < (int)plot->bam_paths.size(); ++i) {
        std::filesystem::path p(plot->bam_paths[i]);
        entries.push_back({
            FileEntryType::Bam,
            i,
            "[BAM] " + p.filename().string()
        });
    }

    for (int i = 0; i < (int)plot->tracks.size(); ++i) {
        std::filesystem::path p(plot->tracks[i].path);
        entries.push_back({
            FileEntryType::Track,
            i,
            "[Track] " + p.filename().string()
        });
    }

    for (int i = 0; i < (int)plot->variantTracks.size(); ++i) {
        const std::string& name =
            plot->variantTracks[i].fileName.empty()
                ? plot->variantTracks[i].path
                : plot->variantTracks[i].fileName;

        std::filesystem::path p(name);
        entries.push_back({
            FileEntryType::Variant,
            i,
            "[Var] " + p.filename().string()
        });
    }

    static std::vector<char> selected;
    if (selected.size() != entries.size()) {
        selected.assign(entries.size(), 0);
    }

    ImGui::TextDisabled("Select files to close:");
    ImGui::Spacing();

    float listH =
        ImGui::GetContentRegionAvail().y -
        ImGui::GetFrameHeightWithSpacing() - 8.f;

    if (ImGui::BeginListBox("##filelist", ImVec2(-FLT_MIN, listH))) {
        for (int i = 0; i < (int)entries.size(); ++i) {
            ImGui::PushID(i);
            bool checked = selected[i] != 0;
            if (ImGui::Checkbox(entries[i].label.c_str(), &checked)) {
                selected[i] = checked ? 1 : 0;
            }
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }

    ImGui::Spacing();

    bool canClose = false;
    for (char s : selected) {
        if (s) { canClose = true; break; }
    }

    if (!canClose)
        ImGui::BeginDisabled();

    if (ImGui::Button("Close selected")) {

        std::ostream& out =
            plot->terminalOutput ? std::cout : plot->outStr;

        std::vector<int> bamIdx, trackIdx, varIdx;

        for (int i = 0; i < (int)entries.size(); ++i) {
            if (!selected[i]) continue;

            const auto& e = entries[i];
            if (e.type == FileEntryType::Bam)
                bamIdx.push_back(e.index);
            else if (e.type == FileEntryType::Track)
                trackIdx.push_back(e.index);
            else
                varIdx.push_back(e.index);
        }

        auto closeDescending =
            [&](std::vector<int>& idxs, const char* prefix)
        {
            std::sort(idxs.begin(), idxs.end(), std::greater<int>());
            for (int idx : idxs) {
                std::string cmd =
                    std::string("rm ") + prefix + std::to_string(idx);
                Commands::run_command_map(plot, cmd, out);
            }
        };

        closeDescending(varIdx, "var");
        closeDescending(trackIdx, "track");
        closeDescending(bamIdx, "bam");

        selected.clear();
        *p_open = false;
        redraw = true;
        plot->processed = false;
    }

    if (!canClose)
        ImGui::EndDisabled();

    ImGui::SameLine();

    if (ImGui::Button("Cancel")) {
        selected.clear();
        *p_open = false;
    }

    ImGui::End();
}

} // namespace Menu