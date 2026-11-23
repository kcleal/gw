// imgui_track_manager.cpp
// Track manager: lists alignment files, tracks and variant files with
// per-item height, order and removal. Every action runs an existing command.

#include <algorithm>
#include <cstring>
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

// Popup listing the fields a track can show as feature labels. Ticking a field appends it,
// so the label follows the order the fields were chosen in.
static void trackLabelFieldsPopup(Manager::GwPlot* plot, int trackIdx, std::string& pending) {
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(ImGui::GetFontSize() * 26.f, ImGui::GetFontSize() * 24.f));
    if (!ImGui::BeginPopup("##label_fields")) return;
    HGW::GwTrack& trk = plot->tracks[trackIdx];
    const bool gff = trk.kind == HGW::FType::GFF3_NOI || trk.kind == HGW::FType::GFF3_IDX
                  || trk.kind == HGW::FType::GTF_NOI || trk.kind == HGW::FType::GTF_IDX;
    if (gff) {  // GFF3/GTF declare no fields: offer the attributes of the features in view
        for (auto& rgn : plot->regions) {
            if (trackIdx < (int)rgn.featuresInView.size())
                for (auto& f : rgn.featuresInView[trackIdx]) trk.noteAttributeKeys(f);
        }
    }
    trk.resolveLabelFields();
    std::vector<std::string> chosen;
    for (auto& f : trk.labelFieldList)
        chosen.push_back(f.source == 1 ? "INFO:" + f.key : (f.source == 2 ? "FORMAT:" + f.key : f.key));
    auto apply = [&]() {
        std::string spec;
        for (auto& c : chosen) spec += (spec.empty() ? "" : ",") + c;
        pending = "track-labels " + std::to_string(trackIdx) + " " + (spec.empty() ? "auto" : spec);
    };

    ImGui::TextDisabled("Label shows");
    ImGui::SameLine();
    if (chosen.empty()) {
        ImGui::TextUnformatted("default");
    } else {
        std::string order;
        for (auto& c : chosen) order += (order.empty() ? "" : ", ") + c.substr(c.find(':') == std::string::npos ? 0 : c.find(':') + 1);
        ImGui::TextWrapped("%s", order.c_str());
        if (ImGui::SmallButton("Default")) {
            chosen.clear();
            apply();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Clear the chosen fields");
    }
    ImGui::Separator();

    auto fields = trk.availableLabelFields();
    for (auto& c : chosen) {  // keep fields typed by hand, or not seen in this view, in the list
        if (std::none_of(fields.begin(), fields.end(), [&](const auto& f) { return f.first == c; }))
            fields.emplace_back(c, "");
    }
    const char* group = "";
    ImGui::BeginChild("##fields", ImVec2(ImGui::GetFontSize() * 16.f,
                      std::min((float)(fields.size() + (gff ? 0.3f : 2.5f)) * ImGui::GetFrameHeightWithSpacing(), ImGui::GetFontSize() * 15.f)));
    if (fields.empty()) ImGui::TextDisabled("No fields found in view");
    for (auto& f : fields) {
        const size_t colon = f.first.find(':');
        const char* g = colon == std::string::npos ? "" : (f.first[0] == 'I' ? "INFO" : "FORMAT");
        if (std::strcmp(g, group) != 0) {
            group = g;
            ImGui::SeparatorText(g[0] == 'F' ? "FORMAT (first sample)" : g);
        }
        auto it = std::find(chosen.begin(), chosen.end(), f.first);
        bool on = it != chosen.end();
        const std::string shown = colon == std::string::npos ? f.first : f.first.substr(colon + 1);
        ImGui::PushID(f.first.c_str());
        if (ImGui::Checkbox(shown.c_str(), &on)) {
            if (on) chosen.push_back(f.first);
            else chosen.erase(it);
            apply();
        }
        ImGui::PopID();
        if (!f.second.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f.second.c_str());
    }
    ImGui::EndChild();

    if (gff) {  // any attribute name can be typed, e.g. one not present in this view
        static char custom[64] = "";
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 11.f);
        bool enter = ImGui::InputTextWithHint("##custom", "attribute name", custom, sizeof(custom), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::SmallButton("Add") || enter) && custom[0]) {
            if (std::find(chosen.begin(), chosen.end(), custom) == chosen.end()) chosen.emplace_back(custom);
            custom[0] = '\0';
            apply();
        }
    }
    ImGui::EndPopup();
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
    static std::string confirmName;  // track awaiting close confirmation
    static int confirmIdx = -1;
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
            ImGui::BeginDisabled(i == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) pending = "move-bam " + std::to_string(i) + " " + std::to_string(i - 1);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(i == (int)plot->bam_paths.size() - 1);
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) pending = "move-bam " + std::to_string(i) + " " + std::to_string(i + 1);
            ImGui::EndDisabled();
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
                if (trackManagerButton("x", "Close track")) {
                    confirmName = plot->remoteTracks[i].name;
                    confirmIdx = i;
                }
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
                if (trk.supportsLabelFields()) {
                    if (trackManagerButton("Labels", "Choose the fields shown as feature labels"))
                        ImGui::OpenPopup("##label_fields");
                    trackLabelFieldsPopup(plot, i, pending);
                } else {
                    ImGui::Dummy(ImVec2(ImGui::CalcTextSize("Labels").x + ImGui::GetStyle().FramePadding.x * 2, 1));
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(i == 0);
                if (ImGui::ArrowButton("##up", ImGuiDir_Up)) pending = "move-track " + std::to_string(i) + " " + std::to_string(i - 1);
                ImGui::EndDisabled();
                ImGui::SameLine();
                ImGui::BeginDisabled(i == n - 1);
                if (ImGui::ArrowButton("##down", ImGuiDir_Down)) pending = "move-track " + std::to_string(i) + " " + std::to_string(i + 1);
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (trackManagerButton("x", "Close track")) {
                    confirmName = trackManagerLabel(trk);
                    confirmIdx = i;
                }
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

    if (confirmItemClose("Close track?", "the track", confirmName, redraw)) {
        if (confirmIdx >= 0) pending = "rm track" + std::to_string(confirmIdx);
        confirmIdx = -1;
    }

    ImGui::End();

    if (!pending.empty()) {
        execCommand(plot, pending, redraw);
        plot->processed = false;
    }
}

} // namespace Menu
