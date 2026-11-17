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

// Small close pill shown beside a data label while the pointer is over the label
// (or in the label's place when data labels are hidden).
// Borderless pill in the Skia label colours (bgPaint fill, tcDel glyph).
static bool drawLabelCloseButton(Manager::GwPlot* plot, const char* id, ImVec2 pos, float side, const char* tooltip) {
    auto toImCol = [](SkColor c, float alpha = 1.f) {
        return IM_COL32(SkColorGetR(c), SkColorGetG(c), SkColorGetB(c), (int)(255 * alpha));
    };
    const auto& theme = plot->opts.theme;
    // Whole-pixel position and no border, so the window clip doesn't trim one side
    pos = ImVec2(IM_ROUND(pos.x), IM_ROUND(pos.y));
    side = IM_ROUND(side);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(side, side), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoSavedSettings);
    ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(pos.x + side, pos.y + side);
    bool clicked = ImGui::InvisibleButton("##close", ImVec2(side, side));
    bool hovered = ImGui::IsItemHovered();
    dl->AddRectFilled(pos, p1, toImCol(theme.bgPaint.getColor()), 2.5f);
    if (hovered) dl->AddRectFilled(pos, p1, toImCol(theme.tcDel.getColor(), 0.12f), 2.5f);
    const ImU32 glyph = toImCol(theme.tcDel.getColor());
    const ImVec2 c(pos.x + side * 0.5f, pos.y + side * 0.5f);
    const float arm = side * 0.18f;
    strokeGlyphLine(dl, ImVec2(c.x - arm, c.y - arm), ImVec2(c.x + arm, c.y + arm), glyph, 1.5f);
    strokeGlyphLine(dl, ImVec2(c.x + arm, c.y - arm), ImVec2(c.x - arm, c.y + arm), glyph, 1.5f);
    if (hovered) ImGui::SetTooltip("%s", tooltip);
    ImGui::End();
    ImGui::PopStyleVar(3);
    return clicked;
}

// True when the pointer is over a label or the close pill to its right
static bool overLabelOrClose(float sx, float sy, float sw, float sh) {
    if (pointerOverOtherWindow()) return false;
    const ImVec2 m = ImGui::GetIO().MousePos;
    return m.x >= sx && m.x <= sx + sw + 2.0f + sh && m.y >= sy && m.y <= sy + sh;
}

void drawImGuiDataLabels(Manager::GwPlot* plot,
                         float gap,
                         float overlayHeight,
                         bool& redraw)
{
    if (plot->mode != Manager::Show::SINGLE) {
        return;
    }
    // With labels hidden only the close pills are drawn, in the label's place
    const bool labels = plot->opts.data_labels;

    float ms = plot->monitorScale;

    // Track which label is currently being edited
    static int editingIndex = -1;   // -1 = none, otherwise global index
    static bool editJustOpened = false;
    static char editBuf[256] = {};
    if (!labels) editingIndex = -1;

    static std::string confirmName;  // track awaiting close confirmation
    static int confirmIdx = -1;

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
    std::string pendingClose;  // run after all label windows are drawn

    // ---------------------------------------------------------------------
    // BAM labels (only regionIdx == 0 collections)
    // ---------------------------------------------------------------------

    for (auto& cl : plot->collections) {
        if (cl.regionIdx != 0) continue;

        if (cl.name.empty() && labels) {
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
        if (!labels) {
            if (overLabelOrClose(sx, sy, std::max(0.0f, sw - sh - 2.0f), sh)) {
                std::snprintf(winLabel, sizeof(winLabel), "##datalabel_close_bam%d", labelIdx);
                if (drawLabelCloseButton(plot, winLabel, ImVec2(sx, sy), sh, "Close file"))
                    pendingClose = "rm bam" + std::to_string(cl.bamIdx);
            }
            labelIdx++;
            continue;
        }
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
                if (cl.bamIdx >= 0 && cl.bamIdx < (int) plot->bam_paths.size())
                    plot->bam_labels[plot->bam_paths[cl.bamIdx]] = editBuf;
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
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Click to edit label");

            ImGui::End();
            ImGui::PopStyleVar();

            if (overLabelOrClose(sx, sy, sw, sh)) {
                std::snprintf(winLabel, sizeof(winLabel), "##datalabel_close_bam%d", labelIdx);
                if (drawLabelCloseButton(plot, winLabel, ImVec2(sx + sw + 2.0f, sy), sh, "Close file"))
                    pendingClose = "rm bam" + std::to_string(cl.bamIdx);
            }
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
        if (!labels) {
            if (overLabelOrClose(sx, sy, std::max(0.0f, sw - sh - 2.0f), sh)) {
                std::snprintf(winLabel, sizeof(winLabel), "##datalabel_close_trk%zu", ti);
                if (drawLabelCloseButton(plot, winLabel, ImVec2(sx, sy), sh, "Close track")) {
                    confirmName = trk.name.empty()
                        ? std::filesystem::path(trk.path).filename().string()
                        : trk.name;
                    confirmIdx = (int)ti;
                }
            }
            trackY += trk.px_height;
            continue;
        }
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
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Click to edit label");

            ImGui::End();
            ImGui::PopStyleVar();

            if (overLabelOrClose(sx, sy, sw, sh)) {
                std::snprintf(winLabel, sizeof(winLabel), "##datalabel_close_trk%zu", ti);
                if (drawLabelCloseButton(plot, winLabel, ImVec2(sx + sw + 2.0f, sy), sh, "Close track")) {
                    confirmName = trk.name.empty()
                        ? std::filesystem::path(trk.path).filename().string()
                        : trk.name;
                    confirmIdx = (int)ti;
                }
            }
        }

        trackY += trk.px_height;
    }

    if (confirmItemClose("Close track?", "the track", confirmName, redraw) && confirmIdx >= 0) {
        pendingClose = "rm track" + std::to_string(confirmIdx);
        confirmIdx = -1;
    }

    if (!pendingClose.empty()) {
        execCommand(plot, pendingClose, redraw);
        editingIndex = -1;
    }
}

} // namespace Menu