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

void drawImGuiCursorLine(Manager::GwPlot* plot) {
    const auto& io=ImGui::GetIO();
    if (!plot->drawLine || plot->mode==Manager::SETTINGS || plot->regions.empty() ||
        plot->commandBoxOpen() || io.WantCaptureMouse || !ImGui::IsMousePosValid(&io.MousePos)) return;
    const float scale=std::max(1.f,plot->monitorScale);
    const float x=io.MousePos.x;
    const float bottom=(plot->fb_height-plot->sliderSpace)/scale;
    if (x<0 || x>=plot->drawingWidth()/scale || io.MousePos.y<plot->topMenuSpace/scale || io.MousePos.y>bottom) return;
    const SkColor c=plot->opts.theme.lcJoins.getColor();
    const ImU32 color=IM_COL32(SkColorGetR(c),SkColorGetG(c),SkColorGetB(c),SkColorGetA(c));
    auto* list=ImGui::GetBackgroundDrawList();
    const float width=std::max(1.f,plot->opts.theme.lcJoins.getStrokeWidth()/scale);
    list->AddLine(ImVec2(x,plot->refSpace/scale),ImVec2(x,bottom),color,width);
    if (plot->mode==Manager::SINGLE && plot->opts.show_translation) {
        const float gap=plot->gap, font=plot->fonts.overlayHeight;
        const float height=font*4.f+gap*2.f;
        const float top=plot->refSpace-height+gap*1.5f;
        const float end=top+3.f*font*.55f+2.f*gap*.35f;
        list->AddLine(ImVec2(x,(top-gap*.5f)/scale),ImVec2(x,(end+gap*.175f)/scale),color,width);
    }
}

void drawImGuiTrackSeparators(Manager::GwPlot* plot) {
    if (!plot->remoteCommand || plot->mode!=Manager::SINGLE || plot->regions.empty()) return;
    struct Drag { float y=0,start=0; bool moved=false,cancelled=false; int width=0,height=0; };
    static std::unordered_map<size_t,Drag> drags;
    const float scale=std::max(1.f,plot->monitorScale);
    auto& io=ImGui::GetIO();
    for (size_t i=0;i<plot->remoteTracks.size();++i) {
        auto& drag=drags[i];
        const float top=plot->remoteTracks[i].top/scale;
        ImGui::SetNextWindowPos(ImVec2(0,top-4),ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(plot->drawingWidth()/scale,8),ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize,ImVec2(1,1));
        const std::string name="##track_separator_"+std::to_string(i);
        ImGui::Begin(name.c_str(),nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoNav|ImGuiWindowFlags_NoFocusOnAppearing);
        ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
        ImGui::InvisibleButton("##drag",ImVec2(plot->drawingWidth()/scale,8));
        if (ImGui::IsItemActivated()) {
            drag.start=io.MousePos.y; drag.y=top; drag.moved=false; drag.cancelled=false;
            drag.width=plot->fb_width; drag.height=plot->fb_height;
        }
        const bool active=ImGui::IsItemActive();
        if (active) {
            drag.y=std::clamp(io.MousePos.y,plot->refSpace/scale,(plot->fb_height-plot->sliderSpace)/scale);
            drag.moved=drag.moved || std::abs(io.MousePos.y-drag.start)>1;
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || io.AppFocusLost || drag.width!=plot->fb_width || drag.height!=plot->fb_height) drag.cancelled=true;
        }
        if (ImGui::IsItemHovered() || active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        if (active && !drag.cancelled) ImGui::GetBackgroundDrawList()->AddLine(ImVec2(0,drag.y),ImVec2(plot->drawingWidth()/scale,drag.y),IM_COL32(90,155,245,255),2);
        if (ImGui::IsItemDeactivated() && drag.moved && !drag.cancelled && !io.AppFocusLost)
            plot->remoteCommand("track-resize "+std::to_string(i)+" "+std::to_string(drag.y*scale));
        ImGui::End(); ImGui::PopStyleVar(2);
    }
}

void drawImGuiAlignmentScrollbars(Manager::GwPlot* plot) {
    if (plot->mode != Manager::Show::SINGLE || !plot->opts.alignments ||
            plot->opts.tlen_yscale || plot->trackY <= 0) return;
    struct DragState { int value{0}; int maximum{0}; bool active{false}; };
    static std::unordered_map<size_t, DragState> drags;
    auto& io = ImGui::GetIO();
    float ms = plot->monitorScale;
    const bool remote = static_cast<bool>(plot->remoteCommand);
    const size_t count = remote ? plot->remoteScrollbars.size() : plot->collections.size();
    for (size_t i = 0; i < count; ++i) {
        auto* cl = remote ? nullptr : &plot->collections[i];
        const auto* meta = remote ? &plot->remoteScrollbars[i] : nullptr;
        auto& drag = drags[i];
        float right = (remote ? meta->xOffset + meta->regionPixels : cl->xOffset + cl->regionPixels) / ms;
        float top = (remote ? meta->yOffset : cl->yOffset) / ms;
        float height = std::max(1.0f, (plot->trackY - plot->gap) / ms);
        bool nearby = io.MousePos.x >= right - 28.0f && io.MousePos.x <= right + 6.0f &&
                      io.MousePos.y >= top && io.MousePos.y <= top + height;
        if (!nearby && !drag.active) continue;
        if (!drag.active) {
            // covArr stores depth deltas. Use the peak already calculated by
            // the coverage renderer instead of scanning large regions on hover.
            int peak = remote ? 0 : (cl->covArr.empty() ? 0 : cl->maxCoverage);
            // Coverage gives a useful initial estimate; allow the user to scroll
            // further as hidden stacks are reached, even without coverage data.
            int rows = std::max(plot->opts.ylim, 1);
            int estimate = peak > 0 ? peak * 2 : (remote ? 0 : (int)cl->readQueue.size());
            drag.maximum = remote ? meta->maximum : std::max(rows, std::max(estimate - rows, cl->vScroll + rows));
            drag.value = (remote ? meta->value : cl->vScroll);
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
        bool smallView = !remote && (cl->region && cl->region->end - cl->region->start < plot->opts.low_memory &&
                         cl->readQueue.size() <= 10000);
        if ((changed && smallView) || released)
            plot->setCollectionVScroll(i, drag.value);
        if (ImGui::IsItemHovered() || drag.active)
            ImGui::SetTooltip("Offset: %d", drag.value);
        ImGui::End();
        ImGui::PopStyleVar(2);
    }
}

} // namespace Menu
