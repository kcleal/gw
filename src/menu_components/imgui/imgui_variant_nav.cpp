// imgui_variant_nav.cpp
// Stepping through the variants of a variant file from the plot itself: an arrow at
// each side of the plot, and a chip naming the variant shown in the alignment view.

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "menu.h"
#include "plot_manager.h"
#include "themes.h"
#include "utils.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace Menu {

namespace {

const double arrowIdleSeconds = 5.0;   // arrows are hidden after this long without pointer activity
const double arrowFadeSeconds = 0.4;
const double labelFlashSeconds = 1.5;  // the chip shows in full for this long after a label change

struct RegionKey {
    std::string chrom;
    int start, end;
    bool operator==(const RegionKey& o) const { return start == o.start && end == o.end && chrom == o.chrom; }
};

std::vector<RegionKey> regionKeys(const std::vector<Utils::Region>& regions) {
    std::vector<RegionKey> keys;
    keys.reserve(regions.size());
    for (const auto& r : regions) keys.push_back({r.chrom, r.start, r.end});
    return keys;
}

ImU32 withAlpha(ImGuiCol idx, float alpha) {
    ImVec4 c = ImGui::GetStyleColorVec4(idx);
    c.w = alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
}

const ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove
        | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing
        | ImGuiWindowFlags_NoBringToFrontOnFocus;

// One arrow; returns true when clicked. `visibility` is 0..1 from the idle timer
bool arrowButton(const char* name, ImVec2 pos, ImVec2 size, bool pointsRight, float visibility, bool& hovered) {
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::Begin(name, nullptr, overlayFlags);
    ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
    const bool clicked = ImGui::InvisibleButton("##step", size);
    hovered = ImGui::IsItemHovered();
    const bool held = ImGui::IsItemActive();
    const float alpha = (hovered ? (held ? 1.0f : 0.85f) : 0.22f) * visibility;
    const float padX = size.x * 0.22f, padY = size.y * 0.14f;
    const float tipX = pointsRight ? pos.x + size.x - padX : pos.x + padX;
    const float baseX = pointsRight ? pos.x + padX : pos.x + size.x - padX;
    const ImVec2 a(baseX, pos.y + padY), b(tipX, pos.y + size.y * 0.5f), c(baseX, pos.y + size.y - padY);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // Filled without the anti-aliasing fringe, which shows as a lighter rim on a translucent shape
    const ImDrawListFlags flags = dl->Flags;
    dl->Flags &= ~ImDrawListFlags_AntiAliasedFill;
    dl->AddTriangleFilled(a, b, c, withAlpha(ImGuiCol_Text, alpha));
    dl->Flags = flags;
    if (hovered) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetTooltip("%s", pointsRight ? "Next" : "Previous");
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    return clicked;
}

}  // namespace

void drawImGuiVariantNav(Manager::GwPlot* plot, bool& redraw) {
    if (plot->variantTracks.empty() || plot->commandBoxOpen()) return;
    const bool tiled = plot->mode == Manager::Show::TILED;
    const bool single = plot->mode == Manager::Show::SINGLE;
    if (!tiled && !single) return;
    if (plot->variantFileSelection < 0 || plot->variantFileSelection >= (int)plot->variantTracks.size()) return;
    auto& vt = plot->variantTracks[plot->variantFileSelection];
    const bool images = vt.type == HGW::TrackType::IMAGES;
    const int total = images ? (int)vt.image_glob.size() : (int)vt.multiRegions.size();
    const bool allRead = images ? true : vt.type == HGW::TrackType::VCF ? vt.vcf.done : vt.variantTrack.done;
    const int live = plot->liveVariantIndex;
    // In the alignment view the controls belong to the variant being shown; without one the
    // user is browsing freely and nothing is drawn
    if (single && (live < 0 || plot->regions.empty())) return;
    if (total == 0) return;

    auto& io = ImGui::GetIO();
    const double now = ImGui::GetTime();
    const float scale = std::max(1.f, plot->monitorScale);
    const float plotRight = plot->drawingWidth() / scale;
    const float plotTop = plot->topMenuSpace / scale;
    // The same band in both views, so the arrows do not move when switching between them
    const float plotBottom = (plot->fb_height - plot->sliderSpace) / scale;
    if (plotRight < 200 || plotBottom - plotTop < 160) return;

    // ---- Pointer activity keeps the arrows on screen ----
    static double lastActive = -1e9;
    static ImVec2 lastMouse(-FLT_MAX, -FLT_MAX);
    static std::vector<RegionKey> lastView;
    static int lastBlock = -1;
    const bool mouseValid = ImGui::IsMousePosValid(&io.MousePos);
    const bool overPlot = mouseValid && io.MousePos.x >= 0 && io.MousePos.x < plotRight &&
                          io.MousePos.y >= plotTop && io.MousePos.y < plotBottom;
    if (mouseValid && (std::fabs(io.MousePos.x - lastMouse.x) > 0.5f || std::fabs(io.MousePos.y - lastMouse.y) > 0.5f)) {
        if (overPlot) lastActive = now;
        lastMouse = io.MousePos;
    }
    if (overPlot && (io.MouseDown[0] || io.MouseWheel != 0 || io.MouseWheelH != 0)) lastActive = now;
    std::vector<RegionKey> view = single ? regionKeys(plot->regions) : std::vector<RegionKey>();
    if (!(view == lastView) || vt.blockStart != lastBlock) {  // panned, zoomed, stepped or paged
        lastActive = now;
        lastView = view;
        lastBlock = vt.blockStart;
    }

    // ---- Arrows ----
    const int perPage = std::max(1, plot->opts.number.x * plot->opts.number.y);
    const int position = tiled ? vt.blockStart : live;
    const bool hasPrevious = position > 0;
    const bool hasNext = tiled ? (vt.blockStart + perPage < total || !allRead) : (live + 1 < total || !allRead);
    const double idle = now - lastActive;
    float visibility = idle < arrowIdleSeconds ? 1.0f
                     : (float)std::max(0.0, 1.0 - (idle - arrowIdleSeconds) / arrowFadeSeconds);
    if (visibility > 0) {
        const ImVec2 size(26, 52);
        const float y = (plotTop + plotBottom) * 0.5f - size.y * 0.5f;
        // The right arrow stays clear of the read scrollbar at the edge of the alignment view
        const float edge = 8, rightEdge = 26;
        bool hovLeft = false, hovRight = false;
        int step = 0;
        if (hasPrevious && arrowButton("##variant_step_prev", ImVec2(edge, y), size, false, visibility, hovLeft)) step = -1;
        if (hasNext && arrowButton("##variant_step_next", ImVec2(plotRight - rightEdge - size.x, y), size, true, visibility, hovRight)) step = 1;
        if (hovLeft || hovRight) lastActive = now;  // never fade under the pointer
        if (step != 0) {
            plot->stepVariant(step);
            lastActive = now;
            redraw = true;
        }
        // Wake the loop to fade the arrows out; it otherwise sleeps until the next event
        const double fadeStart = lastActive + arrowIdleSeconds;
        plot->requestUiWake(now < fadeStart ? fadeStart : now + 1.0 / 30.0);
    }

    // ---- Chip: which variant the alignment view came from, and its label ----
    if (!single || live >= (int)vt.multiLabels.size()) return;
    Utils::Label& lbl = vt.multiLabels[live];
    static int chipVariant = -1, chipLabel = -1;
    static const void* chipTrack = nullptr;
    static std::vector<RegionKey> home;  // the view the variant opened with
    static double flashUntil = 0;
    if (chipTrack != &vt || chipVariant != live) {
        chipTrack = &vt;
        chipVariant = live;
        chipLabel = lbl.i;
        home = view;
        flashUntil = 0;
    } else if (chipLabel != lbl.i) {
        chipLabel = lbl.i;
        flashUntil = now + labelFlashSeconds;
    }
    const bool atVariant = view == home;
    const bool full = atVariant || now < flashUntil;
    if (now < flashUntil) plot->requestUiWake(flashUntil);

    const std::string label = lbl.labels.empty() ? std::string("no label") : lbl.current();
    std::string details;
    if (full) {
        // Type (SVTYPE, or SNP / INDEL worked out from the alleles), genotype, position
        if (!lbl.vartype.empty()) details += lbl.vartype + "   ";
        if (!lbl.genotypes.empty()) {
            // "0/1,./.,1|1": one call per sample; the first few are enough here
            int shown = 0;
            size_t from = 0;
            while (from <= lbl.genotypes.size() && shown < 3) {
                size_t to = lbl.genotypes.find(',', from);
                if (to == std::string::npos) to = lbl.genotypes.size();
                details += (shown ? " " : "") + lbl.genotypes.substr(from, to - from);
                from = to + 1;
                ++shown;
            }
            if (from <= lbl.genotypes.size()) details += " ...";
            details += "   ";
        }
        details += lbl.chrom + ":" + Utils::formatNum(lbl.pos);
    }
    const float padX = full ? 12.0f : 8.0f, padY = full ? 5.0f : 3.0f, gap = 14.0f;
    const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str());
    const ImVec2 detailSize = details.empty() ? ImVec2(0, 0) : ImGui::CalcTextSize(details.c_str());
    const ImVec2 size(labelSize.x + (details.empty() ? 0 : gap + detailSize.x) + padX * 2, labelSize.y + padY * 2);
    // The chip sits in the band setScaling() keeps free below the tracks (variantChipSpace)
    const float bandH = plot->variantChipSpace / scale;
    const ImVec2 pos((plotRight - size.x) * 0.5f,
                     bandH > 0 ? plotBottom + (bandH - size.y) * 0.5f : plotBottom - size.y - 8);
    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(1, 1));
    ImGui::Begin("##variant_chip", nullptr, overlayFlags);
    ImGui::BringWindowToDisplayBack(ImGui::GetCurrentWindow());
    const bool clicked = ImGui::InvisibleButton("##chip", size);
    const bool hovered = ImGui::IsItemHovered();
    const float alpha = full || hovered ? 0.95f : 0.6f;
    // PASS is the label that stands out against the plot: bright on dark themes, dark on light
    // themes. Light themes use fixed colours, their own popup grey is too pale to read here
    const bool pass = !lbl.labels.empty() && label == "PASS";
    const bool light = Themes::isLightTheme(plot->opts.theme_str);
    const ImVec4 navy(0.10f, 0.15f, 0.21f, 1), paleInk(0.91f, 0.94f, 0.97f, 1);
    const ImVec4 pale(0.74f, 0.80f, 0.87f, 1), darkInk(0.06f, 0.09f, 0.13f, 1);
    ImVec4 fill, ink;
    if (pass) {
        fill = light ? navy : ImGui::GetStyleColorVec4(ImGuiCol_Text);
        ink = light ? paleInk : ImGui::GetStyleColorVec4(ImGuiCol_WindowBg);
    } else {
        fill = light ? pale : ImGui::GetStyleColorVec4(ImGuiCol_PopupBg);
        ink = light ? darkInk : ImGui::GetStyleColorVec4(ImGuiCol_Text);
    }
    auto shade = [](ImVec4 c, float a) { c.w = a; return ImGui::ColorConvertFloat4ToU32(c); };
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 corner(pos.x + size.x, pos.y + size.y);
    dl->AddRectFilled(pos, corner, shade(fill, hovered ? 1.0f : alpha), size.y * 0.5f);
    dl->AddText(ImVec2(pos.x + padX, pos.y + padY), shade(ink, lbl.labels.empty() ? alpha * 0.6f : alpha), label.c_str());
    if (!details.empty()) {
        // The same ink as the label, dimmed, so it reads on every fill and while hovered
        dl->AddText(ImVec2(pos.x + padX + labelSize.x + gap, pos.y + padY), shade(ink, alpha * 0.72f), details.c_str());
    }
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (clicked) {
        if (atVariant) {
            // On the variant: a click cycles its label, as clicking a tile does
            if (!lbl.labels.empty()) {
                lbl.next();
                lbl.clicked = true;
            }
        } else {
            // Moved away: a click returns to the variant
            plot->openVariantInAlignmentView(live);
            home = regionKeys(plot->regions);
        }
        redraw = true;
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
}

}  // namespace Menu
