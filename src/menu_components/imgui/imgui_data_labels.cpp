// imgui_data_labels.cpp
// Editable data-label overlays for BAMs and tracks
// Extracted from menu.cpp with no functional changes.

#include <cstring>
#include <string>

#include "menu.h"
#include "plot_manager.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imfilebrowser.h"

namespace Menu {

void drawImGuiDataLabels(Manager::GwPlot* plot,
                         float gap,
                         float overlayHeight,
                         bool& redraw)
{
    if (!plot->opts.data_labels ||
        plot->mode == Manager::Show::SETTINGS)
    {
        return;
    }

    float ms = plot->monitorScale;

    // Track which label is currently being edited
    static int editingIndex = -1;   // -1 = none, otherwise global index
    static bool editJustOpened = false;
    static char editBuf[256] = {};

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration |
        ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    // Opaque background for edit boxes (matches Skia theme)
    SkColor bgClr = plot->opts.theme.bgPaint.getColor();
    ImVec4 editBg(
        SkColorGetR(bgClr) / 255.f,
        SkColorGetG(bgClr) / 255.f,
        SkColorGetB(bgClr) / 255.f,
        1.0f
    );

    int labelIdx = 0;

    // ---------------------------------------------------------------------
    // BAM labels (only regionIdx == 0 collections)
    // ---------------------------------------------------------------------

    for (auto& cl : plot->collections) {
        if (cl.regionIdx != 0) continue;

        if (cl.name.empty()) {
            labelIdx++;
            continue;
        }

        float lx, ly, lw, lh;

        if (plot->opts.alignments) {
            lx = cl.xOffset + ms * 4;
            ly = cl.yOffset + ms * 4;
            lw = plot->fonts.overlay.measureText(
                     cl.name.c_str(),
                     cl.name.size(),
                     SkTextEncoding::kUTF8) + 8 * ms;
            lh = overlayHeight * 2;
        }
        else {
            float text_width =
                plot->fonts.overlay.measureText(
                    cl.name.c_str(),
                    cl.name.size(),
                    SkTextEncoding::kUTF8);

            lx = cl.xOffset - 20;
            ly = cl.yOffset - overlayHeight * 3;
            lw = text_width + 20 + 16 * ms;
            lh = overlayHeight * 2;
        }

        float sx = lx / ms;
        float sy = ly / ms;
        float sw = lw / ms;
        float sh = lh / ms;

        char winLabel[64];
        std::snprintf(winLabel, sizeof(winLabel),
                      "##datalabel_bam%d", labelIdx);

        if (editingIndex == labelIdx) {
            // ── Editing mode ─────────────────────────────────────────
            ImGui::SetNextWindowPos(ImVec2(sx, sy), ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                ImVec2(std::max(sw, 150.0f), sh + 4),
                ImGuiCond_Always);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, editBg);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, editBg);
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, editBg);

            ImGui::Begin(
                winLabel,
                nullptr,
                (flags & ~ImGuiWindowFlags_NoBringToFrontOnFocus) &
                ~ImGuiWindowFlags_NoBackground
            );

            ImGui::SetCursorPos(ImVec2(0, 0));
            ImGui::PushItemWidth(std::max(sw, 150.0f));

            if (editJustOpened) {
                ImGui::SetKeyboardFocusHere();
                editJustOpened = false;
            }

            if (ImGui::InputText("##edit",
                                 editBuf,
                                 sizeof(editBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                cl.name = editBuf;
                editingIndex = -1;
                redraw = true;
                plot->processed = false;
            }

            if (ImGui::IsItemDeactivated() &&
                editingIndex == labelIdx)
            {
                editingIndex = -1;
            }

            ImGui::PopItemWidth();
            ImGui::End();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
        }
        else {
            // ── Invisible click area ─────────────────────────────────
            ImGui::SetNextWindowPos(ImVec2(sx, sy), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin(winLabel, nullptr, flags);
            ImGui::SetCursorPos(ImVec2(0, 0));

            if (ImGui::InvisibleButton("##click", ImVec2(sw, sh))) {
                editingIndex = labelIdx;
                editJustOpened = true;
                std::strncpy(editBuf,
                             cl.name.c_str(),
                             sizeof(editBuf) - 1);
                editBuf[sizeof(editBuf) - 1] = '\0';
            }

            ImGui::End();
            ImGui::PopStyleVar();
        }

        labelIdx++;
    }

    // ---------------------------------------------------------------------
    // Track labels
    // ---------------------------------------------------------------------

    int nbams = static_cast<int>(plot->bams.size());
    float trackY =
        plot->fb_height - plot->totalTabixY - plot->sliderSpace;
    float padY = gap;

    for (size_t ti = 0; ti < plot->tracks.size(); ++ti) {
        auto& trk = plot->tracks[ti];
        if (trk.name.empty()) {
            trackY += trk.px_height;
            continue;
        }

        int thisIdx = nbams + static_cast<int>(ti);

        float text_width = plot->fonts.measureTextWidth(trk.name);

        float lx = gap + ms;
        float ly = trackY + padY + ms;
        float lw = text_width + 16 * ms;
        float lh = overlayHeight * 2;

        float sx = lx / ms;
        float sy = ly / ms;
        float sw = lw / ms;
        float sh = lh / ms;

        char winLabel[64];
        std::snprintf(winLabel, sizeof(winLabel),
                      "##datalabel_trk%zu", ti);

        if (editingIndex == thisIdx) {
            // ── Editing mode ─────────────────────────────────────────
            ImGui::SetNextWindowPos(ImVec2(sx, sy), ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                ImVec2(std::max(sw, 150.0f), sh + 4),
                ImGuiCond_Always);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, editBg);
            ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, editBg);
            ImGui::PushStyleColor(ImGuiCol_FrameBgActive, editBg);

            ImGui::Begin(
                winLabel,
                nullptr,
                (flags & ~ImGuiWindowFlags_NoBringToFrontOnFocus) &
                ~ImGuiWindowFlags_NoBackground
            );

            ImGui::SetCursorPos(ImVec2(0, 0));
            ImGui::PushItemWidth(std::max(sw, 150.0f));

            if (editJustOpened) {
                ImGui::SetKeyboardFocusHere();
                editJustOpened = false;
            }

            if (ImGui::InputText("##edit",
                                 editBuf,
                                 sizeof(editBuf),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
            {
                trk.name = editBuf;
                editingIndex = -1;
                redraw = true;
                plot->processed = false;
            }

            if (ImGui::IsItemDeactivated() &&
                editingIndex == thisIdx)
            {
                editingIndex = -1;
            }

            ImGui::PopItemWidth();
            ImGui::End();
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar();
        }
        else {
            // ── Invisible click area ─────────────────────────────────
            ImGui::SetNextWindowPos(ImVec2(sx, sy), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(sw, sh), ImGuiCond_Always);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::Begin(winLabel, nullptr, flags);
            ImGui::SetCursorPos(ImVec2(0, 0));

            if (ImGui::InvisibleButton("##click", ImVec2(sw, sh))) {
                editingIndex = thisIdx;
                editJustOpened = true;
                std::strncpy(editBuf,
                             trk.name.c_str(),
                             sizeof(editBuf) - 1);
                editBuf[sizeof(editBuf) - 1] = '\0';
            }

            ImGui::End();
            ImGui::PopStyleVar();
        }

        trackY += trk.px_height;
    }
}

} // namespace Menu