// imgui_chromosome_list.cpp
// Invisible button over the chromosome name in the bottom location bar. Clicking
// it lists the chromosomes in the alignment file header (in header/index order),
// falling back to the reference index when no alignment file is open.

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "menu.h"
#include "plot_manager.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace Menu {

struct ChromEntry { std::string name; int64_t length; };

static std::vector<ChromEntry> chromosomeList(Manager::GwPlot* plot) {
    std::vector<ChromEntry> list;
    if (!plot->headers.empty() && plot->headers[0]) {
        sam_hdr_t* hdr = plot->headers[0];
        for (int tid = 0; tid < sam_hdr_nref(hdr); ++tid)
            list.push_back({sam_hdr_tid2name(hdr, tid), (int64_t)sam_hdr_tid2len(hdr, tid)});
    } else if (plot->fai) {
        for (int i = 0; i < faidx_nseq(plot->fai); ++i) {
            const char* name = faidx_iseq(plot->fai, i);
            list.push_back({name, (int64_t)faidx_seq_len(plot->fai, name)});
        }
    }
    return list;
}

void drawImGuiChromosomeList(Manager::GwPlot* plot, bool& redraw) {
    pollGenomeOpen(plot);
    if (plot->mode != Manager::Show::SINGLE || plot->regions.empty() || plot->sliderSpace <= 0) return;
    static int popupRegion = -1;
    static char filter[128] = {};
    static std::vector<ChromEntry> entries;
    const float ms = plot->monitorScale;
    const float gap = plot->gap;
    const float colWidth = (float)plot->drawingWidth() / (float)plot->regions.size();
    const float top = (plot->fb_height - gap * 3) / ms;
    const float height = (gap * 2) / ms;

    // One hit area per region, matching where drawChromLocation writes the name
    for (int i = 0; i < (int)plot->regions.size(); ++i) {
        const auto& rgn = plot->regions[i];
        if (rgn.chromNameWidth <= 0 || rgn.regionLen <= 0 || rgn.chromLen <= 0) continue;
        const float left = (i * colWidth + gap * 0.5f) / ms;
        const float width = (rgn.chromNameWidth + gap) / ms;
        std::string id = "##chrom_name_" + std::to_string(i);
        ImGui::SetNextWindowPos(ImVec2(left, top), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(width, height), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin(id.c_str(), nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoBringToFrontOnFocus);
        ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
        if (ImGui::InvisibleButton("##chrom_btn", ImVec2(width, height))) {
            popupRegion = i;
            filter[0] = '\0';
            entries = chromosomeList(plot);
            ImGui::OpenPopup("##chrom_list", ImGuiPopupFlags_NoReopen);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Chromosomes");
        }
        // The popup belongs to the window of the region that opened it
        if (popupRegion == i) {
            // The hit window above pushed zero padding; the popup and its submenus
            // (Genome) get the same padding as the top-menu popups
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(gap * 0.75f, gap * 0.5f));
            // Keep a small margin so the popup's left edge never touches the screen edge
            ImGui::SetNextWindowPos(ImVec2(std::max(left, 8.0f), top - 2.0f), ImGuiCond_Appearing, ImVec2(0.0f, 1.0f));
            ImGui::SetNextWindowSizeConstraints(ImVec2(200.0f, 0.0f),
                                                ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.6f));
            if (ImGui::BeginPopup("##chrom_list")) {
                // At the top: the popup opens upwards, so the pointer arrives from the list below
                drawGenomeMenu(plot, redraw);
                ImGui::Separator();
                if (entries.empty()) {
                    ImGui::TextDisabled("No chromosomes found");
                } else {
                    if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    ImGui::InputTextWithHint("##chrom_filter", "Filter", filter, sizeof(filter));
                    std::string needle = filter;
                    std::transform(needle.begin(), needle.end(), needle.begin(), [](unsigned char c) { return std::tolower(c); });
                    std::vector<int> shown;
                    for (int k = 0; k < (int)entries.size(); ++k) {
                        if (needle.empty()) { shown.push_back(k); continue; }
                        std::string name = entries[k].name;
                        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
                        if (name.find(needle) != std::string::npos) shown.push_back(k);
                    }
                    const float rowH = ImGui::GetTextLineHeightWithSpacing();
                    const float listH = std::min((float)shown.size(), 18.0f) * rowH + ImGui::GetStyle().FramePadding.y * 2;
                    if (ImGui::BeginTable("##chroms", 2, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
                                          ImGuiTableFlags_SizingStretchProp, ImVec2(0, std::max(listH, rowH)))) {
                        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthFixed);
                        const std::string& current = plot->regions[i].chrom;
                        std::string pending;
                        ImGuiListClipper clipper;  // assemblies can have many thousands of contigs
                        clipper.Begin((int)shown.size());
                        while (clipper.Step()) {
                            for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
                                const ChromEntry& e = entries[shown[r]];
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::PushID(shown[r]);
                                if (ImGui::Selectable(e.name.c_str(), e.name == current, ImGuiSelectableFlags_SpanAllColumns))
                                    pending = e.name;
                                ImGui::PopID();
                                ImGui::TableSetColumnIndex(1);
                                ImGui::TextDisabled("%s", Utils::formatNum((int)std::min<int64_t>(e.length, INT32_MAX)).c_str());
                            }
                        }
                        ImGui::EndTable();
                        if (!pending.empty()) {
                            // Same as typing the chromosome name in the region box
                            plot->regionSelection = i;
                            execCommand(plot, "goto " + pending, redraw);
                            plot->processed = false;
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
                ImGui::EndPopup();
            } else {
                popupRegion = -1;
            }
            ImGui::PopStyleVar();
        }
        ImGui::End();
        ImGui::PopStyleVar(3);
    }
}

} // namespace Menu
