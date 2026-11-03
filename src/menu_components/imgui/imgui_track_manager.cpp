// imgui_track_manager.cpp
// Track manager: lists alignment files, tracks and variant files with
// per-item height, order and removal. Every action runs an existing command.

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "menu.h"
#include "plot_manager.h"
#include "plot_commands.h"
#include "imgui.h"

namespace Menu {

static std::string trackManagerLabel(const HGW::GwTrack& trk) {
    if (trk.kind == HGW::FType::ROI) return "Regions of interest";
    if (!trk.name.empty()) return trk.name;
    return std::filesystem::path(trk.path).filename().string();
}

// Square icon-sized button that keeps rows aligned
static bool trackManagerButton(const char* label, const char* tooltip) {
    bool clicked = ImGui::SmallButton(label);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tooltip);
    return clicked;
}

void drawImGuiTrackManager(Manager::GwPlot* plot, bool* p_open, bool& redraw) {
    if (!p_open || !*p_open) return;

    ImGuiIO& io = ImGui::GetIO();
    float ms = plot->remoteCommand ? 1.0f : std::max(plot->monitorScale, 1.0f);
    ImGui::SetNextWindowSize(ImVec2(std::min(460.f * ms, io.DisplaySize.x * 0.9f),
                                    std::min(380.f * ms, io.DisplaySize.y * 0.8f)),
                             ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.5f),
                            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));

    bool visible = ImGui::Begin("Tracks", p_open, ImGuiWindowFlags_NoCollapse);
    if (closeOnEscape(p_open)) redraw = true;
    if (!visible) {
        ImGui::End();
        return;
    }

    std::string pending;  // run after the widgets so indexes stay valid this frame
    const float drawingArea = (float)plot->fb_height - plot->refSpace - plot->sliderSpace;
    auto fractionOf = [&](const HGW::GwTrack& trk) -> float {
        if (trk.height_fraction > 0.0) return (float)trk.height_fraction;
        if (drawingArea > 0 && trk.px_height > 0) return (float)(trk.px_height / drawingArea);
        return plot->tracks.empty() ? 0.f : (float)(plot->opts.tab_track_height / (double)plot->tracks.size());
    };
    // Slider values are held while dragging and applied once on release
    static std::unordered_map<int, float> dragValues;
    auto heightSlider = [&](int key, float current, float lo, float hi, const std::string& target) {
        auto it = dragValues.find(key);
        float v = it != dragValues.end() ? it->second : current;
        ImGui::SetNextItemWidth(ImGui::GetFrameHeight() * 4.f);
        ImGui::SliderFloat("##h", &v, lo, hi, "%.2f");
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Fraction of the window height");
        if (ImGui::IsItemActive()) {
            dragValues[key] = v;
        } else if (it != dragValues.end()) {
            pending = "track-height " + target + " " + std::to_string(v);
            dragValues.erase(key);
        }
    };

    // Alignments ---------------------------------------------------------------
    if (ImGui::CollapsingHeader("Alignments", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("alignments");  // rows in each section share index-based IDs
        if (plot->bam_paths.empty()) {
            ImGui::TextDisabled("No alignment files");
        }
        for (int i = 0; i < (int)plot->bam_paths.size(); ++i) {
            ImGui::PushID(i);
            std::filesystem::path path(plot->bam_paths[i]);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(path.filename().string().c_str());
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", plot->bam_paths[i].c_str());
            ImGui::SameLine();
            bool hasIntrons = std::any_of(plot->tracks.begin(), plot->tracks.end(), [&](const HGW::GwTrack& t) {
                return t.kind == HGW::FType::INTRON && t.bamIndex == i;
            });
            if (ImGui::Checkbox("Introns", &hasIntrons)) {
                pending = "introns " + std::to_string(i);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show splice junctions as a track");
            ImGui::SameLine();
            if (!plot->remoteCommand && i < (int)plot->headers.size() &&
                    trackManagerButton("Header", "Show the file header in the Output window")) {
                const char* text = sam_hdr_str(plot->headers[i]);
                if (text) appendCommandOutput(plot, std::string("\n") + text);
            }
            ImGui::SameLine();
            if (trackManagerButton("x", "Close file")) {
                pending = "rm bam" + std::to_string(i);
            }
            ImGui::PopID();
        }
        if (!plot->bam_paths.empty() && !plot->tracks.empty()) {
            float used = 0.f;
            for (auto& t : plot->tracks) used += fractionOf(t);
            ImGui::PushID("align_height");
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Alignment panel height");
            ImGui::SameLine();
            heightSlider(-1, std::clamp(1.f - used, 0.1f, 0.9f), 0.1f, 0.9f, "alignments");
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    // Tracks -------------------------------------------------------------------
    if (ImGui::CollapsingHeader("Tracks", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("tracks");  // rows in each section share index-based IDs
        if (plot->remoteCommand) {
            for (int i = 0; i < (int)plot->remoteTracks.size(); ++i) {
                ImGui::PushID(i);
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(plot->remoteTracks[i].name.c_str());
                ImGui::SameLine();
                if (trackManagerButton("x", "Close track")) pending = "rm track" + std::to_string(i);
                ImGui::PopID();
            }
        }
        if (plot->tracks.empty() && (!plot->remoteCommand || plot->remoteTracks.empty())) {
            ImGui::TextDisabled("No tracks");
        } else if (!plot->tracks.empty() &&
                   ImGui::BeginTable("##tracks", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Height");
            ImGui::TableSetupColumn("");
            const int n = (int)plot->tracks.size();
            for (int i = 0; i < n; ++i) {
                const HGW::GwTrack& trk = plot->tracks[i];
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                std::string label = trackManagerLabel(trk);
                ImGui::TextUnformatted(label.c_str());
                if (ImGui::IsItemHovered() && !trk.path.empty() && trk.kind != HGW::FType::INTRON)
                    ImGui::SetTooltip("%s", trk.path.c_str());
                ImGui::TableSetColumnIndex(1);
                heightSlider(i, std::clamp(fractionOf(trk), 0.02f, 0.9f), 0.02f, 0.9f, std::to_string(i));
                ImGui::TableSetColumnIndex(2);
                ImGui::BeginDisabled(i == 0);
                if (ImGui::ArrowButton("##up", ImGuiDir_Up)) pending = "move-track " + std::to_string(i) + " " + std::to_string(i - 1);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(i == n - 1);
                if (ImGui::ArrowButton("##down", ImGuiDir_Down)) pending = "move-track " + std::to_string(i) + " " + std::to_string(i + 1);
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (trackManagerButton("x", "Close track")) pending = "rm track" + std::to_string(i);
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::PopID();
    }

    // Variant files ------------------------------------------------------------
    if (!plot->variantTracks.empty() && ImGui::CollapsingHeader("Variant files", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::PushID("variants");  // rows in each section share index-based IDs
        for (int i = 0; i < (int)plot->variantTracks.size(); ++i) {
            const auto& vt = plot->variantTracks[i];
            std::string name = std::filesystem::path(vt.fileName.empty() ? vt.path : vt.fileName).filename().string();
            ImGui::PushID(i);
            if (ImGui::RadioButton(name.c_str(), plot->variantFileSelection == i)) {
                plot->selectVariantFile(i);
                redraw = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", vt.path.c_str());
            ImGui::SameLine();
            if (trackManagerButton("x", "Close variant file")) pending = "rm var" + std::to_string(i);
            ImGui::PopID();
        }
        ImGui::PopID();
    }

    ImGui::End();

    if (!pending.empty()) {
        execCommand(plot, pending, redraw);
        plot->processed = false;
    }
}

} // namespace Menu
