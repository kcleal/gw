#include <algorithm>
#include <string>
#include <unordered_map>
#include "menu.h"
#include "plot_manager.h"
#include "imgui.h"
#include "imgui_internal.h"

// Transient per-collection read scrollbars. Range estimates are deliberately
// approximate because read stacking depends on the current viewport and sorting.
namespace Menu {

void drawImGuiAlignmentScrollbars(Manager::GwPlot* plot) {
    if (plot->mode != Manager::Show::SINGLE || !plot->opts.alignments ||
            plot->opts.tlen_yscale || plot->trackY <= 0) return;
    struct DragState { int value{0}; int maximum{0}; bool active{false}; };
    static std::unordered_map<size_t, DragState> drags;
    auto& io = ImGui::GetIO();
    float ms = plot->monitorScale;
    for (size_t i = 0; i < plot->collections.size(); ++i) {
        auto& cl = plot->collections[i];
        auto& drag = drags[i];
        float right = (cl.xOffset + cl.regionPixels) / ms;
        float top = cl.yOffset / ms;
        float height = std::max(1.0f, (plot->trackY - plot->gap) / ms);
        bool nearby = io.MousePos.x >= right - 28.0f && io.MousePos.x <= right + 6.0f &&
                      io.MousePos.y >= top && io.MousePos.y <= top + height;
        if (!nearby && !drag.active) continue;
        if (!drag.active) {
            // covArr stores depth deltas. Use the peak already calculated by
            // the coverage renderer instead of scanning large regions on hover.
            int peak = cl.covArr.empty() ? 0 : cl.maxCoverage;
            // Coverage gives a useful initial estimate; allow the user to scroll
            // further as hidden stacks are reached, even without coverage data.
            int rows = std::max(plot->opts.ylim, 1);
            int estimate = peak > 0 ? peak * 2 : (int)cl.readQueue.size();
            drag.maximum = std::max(rows, std::max(estimate - rows, cl.vScroll + rows));
            drag.value = cl.vScroll;
        }
        ImGui::SetNextWindowPos(ImVec2(right - 16.0f, top), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(16.0f, height), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
        std::string id = "##alignment_scroll_" + std::to_string(i);
        ImGui::Begin(id.c_str(), nullptr, ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoBackground);
        // These controls belong to the plot background, below every dialog even
        // after a slider drag has focused their window.
        ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
        // VSlider increases upward, so invert its value to scroll downward.
        int sliderValue = drag.maximum - drag.value;
        // Keep the same blue slider in IGV, dark and slate themes.
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.16f, 0.29f, 0.48f, 0.54f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.26f, 0.59f, 0.98f, 0.40f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.26f, 0.59f, 0.98f, 0.67f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, ImVec4(0.24f, 0.52f, 0.88f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, ImVec4(0.26f, 0.59f, 0.98f, 1.00f));
        bool changed = ImGui::VSliderInt("##offset", ImVec2(16, height),
                                        &sliderValue, 0, drag.maximum, "");
        ImGui::PopStyleColor(5);
        drag.value = drag.maximum - sliderValue;
        drag.active = ImGui::IsItemActive();
        bool released = ImGui::IsItemDeactivatedAfterEdit();
        bool smallView = cl.region && cl.region->end - cl.region->start < plot->opts.low_memory &&
                         cl.readQueue.size() <= 10000;
        if ((changed && smallView) || released)
            plot->setCollectionVScroll(i, drag.value);
        if (ImGui::IsItemHovered() || drag.active)
            ImGui::SetTooltip("Offset: %d", drag.value);
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
}

} // namespace Menu
