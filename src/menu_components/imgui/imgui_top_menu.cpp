#include "menu.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imfilebrowser.h"
#include "plot_commands.h"
#include "utils.h"

#include <cmath>
#include <iostream>
#include <string>

namespace Menu {

// -----------------------------------------------------------------------------
// Icon drawing helpers (local to this translation unit)
// -----------------------------------------------------------------------------

static void drawCloseIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col) {
    float pad = (max.x - min.x) * 0.30f;
    float l = min.x + pad, r = max.x - pad;
    float t = min.y + pad, b = max.y - pad;
    dl->AddLine(ImVec2(l, t), ImVec2(r, b), col, 1.0f);
    dl->AddLine(ImVec2(r, t), ImVec2(l, b), col, 1.0f);
}

static void drawPlusIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col) {
    float pad = (max.x - min.x) * 0.30f;
    float l = min.x + pad, r = max.x - pad;
    float t = min.y + pad, b = max.y - pad;
    float midX = (l + r) * 0.5f;
    float midY = (t + b) * 0.5f;
    dl->AddLine(ImVec2(midX, t), ImVec2(midX, b), col, 1.0f);
    dl->AddLine(ImVec2(l, midY), ImVec2(r, midY), col, 1.0f);
}

static void drawClipboardIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col) {
    float w = max.x - min.x;
    float h = max.y - min.y;
    float hPad = w * 0.20f;
    float tabH = h * 0.26f;
    float tabW = w * 0.38f;
    dl->AddRect(ImVec2(min.x + hPad, min.y + tabH * 0.5f),
                ImVec2(max.x - hPad, max.y - hPad * 0.5f),
                col, 0, 0, 1.0f);
    float tx = min.x + (w - tabW) * 0.5f;
    dl->AddRectFilled(ImVec2(tx, min.y + hPad * 0.4f),
                      ImVec2(tx + tabW, min.y + tabH),
                      col);
}

static void drawHamburgerIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col) {
    float w = max.x - min.x;
    float h = max.y - min.y;
    float hPad = w * 0.25f;
    float vPad = h * 0.30f;
    float l = min.x + hPad, r = max.x - hPad;
    float t = min.y + vPad, b = max.y - vPad;
    float midY = (t + b) * 0.5f;
    dl->AddLine(ImVec2(l, t),    ImVec2(r, t),    col, 1.0f);
    dl->AddLine(ImVec2(l, midY), ImVec2(r, midY), col, 1.0f);
    dl->AddLine(ImVec2(l, b),    ImVec2(r, b),    col, 1.0f);
}

static void drawSquareIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col) {
    float pad = (max.x - min.x) * 0.28f;
    dl->AddRect(ImVec2(min.x + pad, min.y + pad),
                ImVec2(max.x - pad, max.y - pad),
                col, 0.0f, 0, 1.0f);
}

static void drawRefreshIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col) {
    float w = max.x - min.x;
    float h = max.y - min.y;
    float cx = min.x + w * 0.5f;
    float cy = min.y + h * 0.5f;
    float r  = std::min(w, h) * 0.30f;
    constexpr float PI = 3.14159265f;
    float thickness = 1.0f;
    dl->PathArcTo(ImVec2(cx, cy), r, -PI * 0.4f, PI * 1.6f, 18);
    dl->PathStroke(col, false, thickness);
    // arrowhead
    float endA   = PI * 1.6f;
    float ax     = cx + r * cosf(endA);
    float ay     = cy + r * sinf(endA);
    float backAng = atan2f(-cosf(endA), sinf(endA));
    float spread  = 0.44f;
    float al      = std::min(w, h) * 0.18f;
    dl->AddLine(ImVec2(ax, ay),
                ImVec2(ax + cosf(backAng + spread) * al, ay + sinf(backAng + spread) * al),
                col, thickness);
    dl->AddLine(ImVec2(ax, ay),
                ImVec2(ax + cosf(backAng - spread) * al, ay + sinf(backAng - spread) * al),
                col, thickness);
}

static void drawChainIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col) {
    float w        = max.x - min.x;
    float h        = max.y - min.y;
    float cy       = min.y + h * 0.5f;
    float pillH    = h * 0.30f;
    float rounding = pillH * 0.5f;
    float pillW    = w * 0.42f;
    float overlap  = w * 0.09f;
    float totalW   = pillW * 2.0f - overlap;
    float x0       = min.x + (w - totalW) * 0.5f;
    float thickness = 1.0f;
    dl->AddRect(ImVec2(x0,                   cy - pillH * 0.5f),
                ImVec2(x0 + pillW,           cy + pillH * 0.5f), col, rounding, 0, thickness);
    dl->AddRect(ImVec2(x0 + pillW - overlap, cy - pillH * 0.5f),
                ImVec2(x0 + 2*pillW - overlap, cy + pillH * 0.5f), col, rounding, 0, thickness);
}

// Local exec helper (execCommand in imgui_command_dialog.cpp is file-static)
//static void execCmd(Manager::GwPlot* plot, const std::string& cmd_str, bool& redraw) {
//    std::string cmd = cmd_str;
//    std::ostream& out = plot->terminalOutput ? std::cout : plot->outStr;
//    Commands::run_command_map(plot, cmd, out);
//    redraw = true;
//}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static std::string formatRegion(const Utils::Region& r) {
    return r.chrom + ":" +
           Utils::formatNum(r.start) + "-" +
           Utils::formatNum(r.end);
}

// -----------------------------------------------------------------------------
// Top menu + region boxes
// -----------------------------------------------------------------------------

void drawImGuiTopMenu(Manager::GwPlot* plot, float gap, float overlayHeight, bool& redraw) {
    float ms = plot->monitorScale;
    float scrGap = gap / ms;
    float menuY = scrGap * 0.25f;
    ImGui::SetNextWindowPos(ImVec2(scrGap * 0.5f, menuY), ImGuiCond_Always);
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration
                            | ImGuiWindowFlags_NoBackground
                            | ImGuiWindowFlags_AlwaysAutoResize
                            | ImGuiWindowFlags_NoMove
                            | ImGuiWindowFlags_NoFocusOnAppearing
                            | ImGuiWindowFlags_NoNav;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap * 0.5f, 0));
    ImGui::Begin("##TopMenu", nullptr, flags);

    float btnSide = overlayHeight;
    ImVec2 btnSize(btnSide, btnSide);
    bool lightTheme = (plot->opts.theme_str == "igv");
    ImU32 iconCol = lightTheme ? IM_COL32(80, 80, 80, 255) : IM_COL32(200, 200, 200, 255);
    ImU32 iconHoverCol = lightTheme ? IM_COL32(0, 0, 0, 255) : IM_COL32(255, 255, 255, 255);

    // Hamburger button
    ImVec2 cursor = ImGui::GetCursorScreenPos();
    if (ImGui::Button("##hamburger", btnSize)) {
        ImGui::OpenPopup("##TopMenuPopup");
    }
    bool hovered = ImGui::IsItemHovered();
    drawHamburgerIcon(ImGui::GetWindowDrawList(), cursor,
                        ImVec2(cursor.x + btnSide, cursor.y + btnSide),
                        hovered ? iconHoverCol : iconCol);

    // View button — to the right of the burger (tight spacing like + and X buttons)
    ImGui::SameLine(0, gap * 0.25f);
    ImVec2 viewCursor = ImGui::GetCursorScreenPos();
    if (ImGui::Button("##viewbtn", btnSize)) {
        ImGui::OpenPopup("##ViewPopup");
    }
    bool viewHovered = ImGui::IsItemHovered();
    drawSquareIcon(ImGui::GetWindowDrawList(), viewCursor,
                    ImVec2(viewCursor.x + btnSide, viewCursor.y + btnSide),
                    viewHovered ? iconHoverCol : iconCol);


    // Hamburger popup — increased padding for tablet-friendly touch targets
    ImGui::SetNextWindowPos(ImVec2(cursor.x, cursor.y + btnSide), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(gap * 0.75f, gap * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(gap * 0.5f, gap * 0.4f));
    if (ImGui::BeginPopup("##TopMenuPopup")) {
        if (ImGui::MenuItem("Open file")) {
            plot->openDialogOpen = true;
        }
        bool hasFiles = !plot->bam_paths.empty() || !plot->tracks.empty() || !plot->variantTracks.empty();
        if (ImGui::MenuItem("Close file", nullptr, false, hasFiles)) {
            plot->closeDialogOpen = true;
        }
        if (ImGui::MenuItem("Tools")) {
            plot->commandDialogOpen = !plot->commandDialogOpen;
        }
        if (ImGui::MenuItem("Settings")) {
            if (plot->mode != Manager::Show::SETTINGS) {
                plot->last_mode = plot->mode;
                plot->mode = Manager::Show::SETTINGS;
                plot->opts.menu_table = Themes::MenuTable::MAIN;
                plot->opts.menu_level = "";
            } else {
                plot->mode = plot->last_mode;
                plot->opts.editing_underway = false;
            }
        }
        if (ImGui::MenuItem("Help")) {
            plot->helpDialogOpen = !plot->helpDialogOpen;
        }
        if (!plot->variantTracks.empty() && ImGui::MenuItem("Variants", nullptr, plot->labelTableDialogOpen)) {
            plot->labelTableDialogOpen = !plot->labelTableDialogOpen;
        }
        if (plot->variantTracks.size() > 1 && ImGui::BeginMenu("Variant files")) {
            for (int i = 0; i < (int)plot->variantTracks.size(); ++i) {
                const std::string& name = plot->variantTracks[i].fileName.empty()
                                            ? plot->variantTracks[i].path
                                            : plot->variantTracks[i].fileName;
                bool selected = (plot->variantFileSelection == i);
                if (ImGui::MenuItem(name.c_str(), nullptr, selected)) {
                    plot->selectVariantFile(i);
                    redraw = true;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);  // item spacing, popup padding

    // View popup — display toggles, link mode, refresh, find
    ImGui::SetNextWindowPos(ImVec2(viewCursor.x, viewCursor.y + btnSide), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(gap * 0.5f, gap * 0.5f));
    if (ImGui::BeginPopup("##ViewPopup")) {
        Themes::IniOptions &opts = plot->opts;

        // Icon toolbar: action buttons on one row at the top
        {
            float icSz = ImGui::GetFrameHeight();
            ImVec2 icBtnSz(icSz, icSz);
            float icSpacing = gap * 0.20f;
            ImDrawList* dl = ImGui::GetWindowDrawList();

            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0,0,0,0));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0,0,0,0));

            // Circular arrow (Refresh)
            {
                ImVec2 cur = ImGui::GetCursorScreenPos();
                bool clicked = ImGui::Button("##ic_refresh", icBtnSz);
                bool hov = ImGui::IsItemHovered();
                drawRefreshIcon(dl, cur, ImVec2(cur.x + icSz, cur.y + icSz), hov ? iconHoverCol : iconCol);
                if (hov) ImGui::SetTooltip("Refresh");
                if (clicked) { execCommand(plot, "refresh", redraw); plot->processed = false; }
            }
            ImGui::SameLine(0, icSpacing);

            // Chain link (Link mode — cycles none → sv → all)
            {
                const char* linkModes[] = {"none", "sv", "all"};
                int lc = opts.link_op;
                if (lc < 0 || lc > 2) lc = 0;
                ImVec2 cur = ImGui::GetCursorScreenPos();
                bool clicked = ImGui::Button("##ic_link", icBtnSz);
                bool hov = ImGui::IsItemHovered();
                drawChainIcon(dl, cur, ImVec2(cur.x + icSz, cur.y + icSz), hov ? iconHoverCol : iconCol);
                if (hov) ImGui::SetTooltip("Link: %s  (click for %s)", linkModes[lc], linkModes[(lc + 1) % 3]);
                if (clicked) {
                    execCommand(plot, std::string("link ") + linkModes[(lc + 1) % 3], redraw);
                    plot->processed = false;
                }
            }

            ImGui::PopStyleColor(3);
        }
        // ImGui::Separator();
        // ImGui::Spacing();

        // Display toggles
        bool b_alignments = opts.alignments;
        if (ImGui::Checkbox("Alignments", &b_alignments)) {
            opts.alignments = b_alignments;
            redraw = true; plot->processed = false;
        }
        bool b_cov = opts.max_coverage > 0;
        if (ImGui::Checkbox("Coverage", &b_cov)) {
            opts.max_coverage = b_cov ? 1410065408 : 0;
            redraw = true; plot->processed = false;
        }
        bool b_insertions = opts.small_indel_threshold > 0;
        if (ImGui::Checkbox("Insertions", &b_insertions)) {
            opts.small_indel_threshold = b_insertions ? std::stoi(opts.myIni["view_thresholds"]["small_indel"]) : 0;
            redraw = true; plot->processed = false;
        }
        bool b_mismatches = opts.snp_threshold > 0;
        if (ImGui::Checkbox("Mismatches", &b_mismatches)) {
            opts.snp_threshold = b_mismatches ? std::stoi(opts.myIni["view_thresholds"]["snp"]) : 0;
            redraw = true; plot->processed = false;
        }
        bool b_edges = opts.edge_highlights > 0;
        if (ImGui::Checkbox("Edges", &b_edges)) {
            opts.edge_highlights = b_edges ? std::stoi(opts.myIni["view_thresholds"]["edge_highlights"]) : 0;
            redraw = true; plot->processed = false;
        }
        bool b_softclips = opts.soft_clip_threshold > 0;
        if (ImGui::Checkbox("Soft-clips", &b_softclips)) {
            opts.soft_clip_threshold = b_softclips ? std::stoi(opts.myIni["view_thresholds"]["soft_clip"]) : 0;
            redraw = true; plot->processed = false;
        }
        bool b_mods = opts.parse_mods;
        if (ImGui::Checkbox("Mods", &b_mods)) {
            opts.parse_mods = b_mods;
            redraw = true; plot->processed = false;
        }
        if (ImGui::Checkbox("Line", &plot->drawLine)) {
            redraw = true;
        }
        if (ImGui::Checkbox("Tlen-y", &opts.tlen_yscale)) {
            redraw = true; plot->processed = false;
        }
        if (ImGui::Checkbox("Expand tracks", &opts.expand_tracks)) {
            redraw = true; plot->processed = false;
        }
        if (ImGui::Checkbox("Log2 coverage", &opts.log2_cov)) {
            redraw = true; plot->processed = false;
        }

        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();  // popup padding

    ImGui::End();
    ImGui::PopStyleVar(2);  // item spacing, window padding

    // Region text boxes — one per region, positioned according to regionWidth
    // Always show at least one box even if no regions are loaded
    if (plot->mode != Manager::Show::SETTINGS) {
        ImGuiIO& io = ImGui::GetIO();
        float ms = plot->monitorScale;
        // Convert framebuffer coords to screen coords for ImGui
        float scrGap = gap / ms;
        float scrOverlay = overlayHeight / ms;
        float rw = plot->regionWidth / ms;

        float menuY = scrGap * 0.25f;

        // Background color from Skia theme (use bgMenu to match the Skia menu bar)
        SkColor bgClr = plot->opts.theme.bgMenu.getColor();
        ImVec4 bgCol(SkColorGetR(bgClr) / 255.f, SkColorGetG(bgClr) / 255.f,
                        SkColorGetB(bgClr) / 255.f, 1.0f);

        // Persistent buffers for region text inputs (up to 64 regions)
        static char regionBufs[64][128] = {};
        static bool regionActive[64] = {};

        // Minimum width: 15 characters worth
        float minTextBoxWidth = ImGui::CalcTextSize("XXXXXXXXXXXXXXX").x + scrGap;
        size_t nRegions = plot->regions.size();
        size_t nBoxes = std::max(nRegions, (size_t)1);

        float burgerEnd = scrGap * 0.5f + overlayHeight + gap * 0.25f + overlayHeight + scrGap * 0.5f;

        bool hasTileTracks = !plot->variantTracks.empty();
        bool inTiled = (plot->mode == Manager::Show::TILED);

        // Single window spanning the full width for all region text boxes
        ImGui::SetNextWindowPos(ImVec2(0, menuY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x, scrOverlay + 4), ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGuiWindowFlags boxFlags = ImGuiWindowFlags_NoDecoration
                                    | ImGuiWindowFlags_NoBackground
                                    | ImGuiWindowFlags_NoMove
                                    | ImGuiWindowFlags_NoFocusOnAppearing
                                    | ImGuiWindowFlags_NoNav
                                    | ImGuiWindowFlags_NoSavedSettings
                                    | ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGui::Begin("##RegionBoxes", nullptr, boxFlags);

        ImGui::PushStyleColor(ImGuiCol_FrameBg, bgCol);
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, bgCol);
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, bgCol);
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.5f, 0.5f, 0.5f, 0.5f));
        // Text color matching the Skia theme's tcDel (used in command box text too)
        SkColor tcClr = plot->opts.theme.tcDel.getColor();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(SkColorGetR(tcClr) / 255.f, SkColorGetG(tcClr) / 255.f,
                                                        SkColorGetB(tcClr) / 255.f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);

        float cbSide = overlayHeight;
        float btnGap = gap * 0.25f;
        // Base space for dup + close buttons; tile button adds one more slot in last column
        float rightBtnsWidth_base = cbSide + btnGap + cbSide + gap * 0.5f;

        if (!inTiled) {
            // First pass: draw all text boxes
            for (size_t i = 0; i < nBoxes && i < 64; ++i) {
                // Build display string
                std::string display;
                bool emptyRegion = (i >= nRegions);
                if (!emptyRegion) {
                    display = formatRegion(plot->regions[i]);
                }

                float textBoxWidth = std::max(minTextBoxWidth,
                    ImGui::CalcTextSize(display.c_str()).x + scrGap);

                // Last column reserves extra space for tile button when present
                float rightBtnsWidth = rightBtnsWidth_base;
                if (hasTileTracks && i == nBoxes - 1) {
                    rightBtnsWidth += cbSide + btnGap;
                }

                // Center on the region column, then clamp width and position to avoid buttons
                float colWidth = (nRegions > 0) ? rw : io.DisplaySize.x;
                float regionLeft = colWidth * (float)i;
                float regionCenter = regionLeft + colWidth * 0.5f;
                float leftEdge = (i == 0) ? burgerEnd : regionLeft;
                float rightEdge = (!emptyRegion) ? (regionLeft + rw - rightBtnsWidth) : (regionLeft + colWidth);
                float maxTextBoxWidth = rightEdge - leftEdge;
                if (maxTextBoxWidth > 0 && textBoxWidth > maxTextBoxWidth) {
                    textBoxWidth = maxTextBoxWidth;
                }

                // Position centered on the region, then clamp to stay within bounds
                float textBoxX = regionCenter - textBoxWidth * 0.5f;
                if (textBoxX < leftEdge) {
                    textBoxX = leftEdge;
                }
                if (textBoxX + textBoxWidth > rightEdge) {
                    textBoxX = rightEdge - textBoxWidth;
                }

                // If the input is not being actively edited, update with current region
                if (!regionActive[i]) {
                    if (emptyRegion) {
                        regionBufs[i][0] = '\0';
                    } else {
                        std::strncpy(regionBufs[i], display.c_str(), sizeof(regionBufs[i]) - 1);
                        regionBufs[i][sizeof(regionBufs[i]) - 1] = '\0';
                    }
                }

                // Position each box at the top of the window using window-local coords
                ImGui::SetCursorPos(ImVec2(textBoxX, 0));
                ImGui::PushItemWidth(textBoxWidth);
                char label[32];
                std::snprintf(label, sizeof(label), "##region%zu", i);
                bool entered;
                if (emptyRegion) {
                    entered = ImGui::InputTextWithHint(label, "enter region here", regionBufs[i],
                                                        sizeof(regionBufs[i]),
                                                        ImGuiInputTextFlags_EnterReturnsTrue);
                } else {
                    entered = ImGui::InputText(label, regionBufs[i], sizeof(regionBufs[i]),
                                                ImGuiInputTextFlags_EnterReturnsTrue);
                }
                ImGui::PopItemWidth();

                regionActive[i] = ImGui::IsItemActive();

                if (entered) {
                    std::string cmd(regionBufs[i]);
                    if (!cmd.empty()) {
                        if (emptyRegion) {
                            // No regions yet — use goto to navigate
                            cmd = std::string("goto ") + cmd;
                        }
                        int prevSel = plot->regionSelection;
                        plot->regionSelection = (int)i;
                        std::ostream& out = (plot->terminalOutput) ? std::cout : plot->outStr;
                        Commands::run_command_map(plot, cmd, out);
                        plot->regionSelection = prevSel;
                        redraw = true;
                        plot->processed = false;
                    }
                }
            }

            // Second pass: draw close and duplicate buttons
            for (size_t i = 0; i < nBoxes && i < 64; ++i) {
                if (i >= nRegions) {
                    continue;
                }
                float colWidth = (nRegions > 0) ? rw : io.DisplaySize.x;
                float regionLeft = colWidth * (float)i;

                // Close button (rightmost)
                float cbX = regionLeft + rw - cbSide - gap * 0.5f;
                ImGui::SetCursorPos(ImVec2(cbX, 0));
                char closeLabel[32];
                std::snprintf(closeLabel, sizeof(closeLabel), "##close%zu", i);
                ImVec2 cbCursor = ImGui::GetCursorScreenPos();
                if (ImGui::Button(closeLabel, ImVec2(cbSide, cbSide))) {
                    std::string cmd = "rm " + std::to_string(i);
                    std::ostream& out = (plot->terminalOutput) ? std::cout : plot->outStr;
                    Commands::run_command_map(plot, cmd, out);
                    redraw = true;
                    plot->processed = false;
                }
                bool cbHovered = ImGui::IsItemHovered();
                ImU32 xCol = cbHovered ? iconHoverCol : iconCol;
                drawCloseIcon(ImGui::GetWindowDrawList(), cbCursor,
                                ImVec2(cbCursor.x + cbSide, cbCursor.y + cbSide), xCol);

                // Duplicate region button (to the left of close, with small gap)
                float dupX = cbX - cbSide - btnGap;
                ImGui::SetCursorPos(ImVec2(dupX, 0));
                char dupLabel[32];
                std::snprintf(dupLabel, sizeof(dupLabel), "##dup%zu", i);
                ImVec2 dupCursor = ImGui::GetCursorScreenPos();
                if (ImGui::Button(dupLabel, ImVec2(cbSide, cbSide))) {
                    Utils::Region &rgn = plot->regions[i];
                    std::string cmd = "add " + rgn.chrom + ":" + std::to_string(rgn.start) + "-" + std::to_string(rgn.end);
                    std::ostream& out = (plot->terminalOutput) ? std::cout : plot->outStr;
                    Commands::run_command_map(plot, cmd, out);
                    redraw = true;
                    plot->processed = false;
                }
                bool dupHovered = ImGui::IsItemHovered();
                ImU32 plusCol = dupHovered ? iconHoverCol : iconCol;
                drawPlusIcon(ImGui::GetWindowDrawList(), dupCursor,
                                ImVec2(dupCursor.x + cbSide, dupCursor.y + cbSide), plusCol);
            }
        }  // end if (!inTiled)

        ImGui::PopStyleVar();   // FrameBorderSize
        ImGui::PopStyleColor(5);

        ImGui::End();
        ImGui::PopStyleVar();  // window padding
    }
}

} // namespace Menu
