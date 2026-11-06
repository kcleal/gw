// imgui_info_popups.cpp
// Info, coverage, track, and reference sequence popups
// Extracted from menu.cpp with no functional changes.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "menu.h"
#include "plot_manager.h"
#include "plot_commands.h"
#include "segments.h"
#include "utils.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imfilebrowser.h"

namespace Menu {

struct PopupTitleIconClip {
    ImDrawList* drawList=ImGui::GetWindowDrawList();
    PopupTitleIconClip(){
        const auto pos=ImGui::GetWindowPos(),size=ImGui::GetWindowSize();
        drawList->PushClipRect(pos,ImVec2(pos.x+size.x,pos.y+size.y),false);
    }
    ~PopupTitleIconClip(){drawList->PopClipRect();}
};


// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string stripAnsiCodes(const std::string& s)
{
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size();) {
        if (s[i] == '\033' && i + 1 < s.size() && s[i + 1] == '[') {
            while (i < s.size() && s[i] != 'm')
                ++i;
            if (i < s.size())
                ++i;
        }
        else {
            out += s[i++];
        }
    }
    return out;
}

struct RowData {
    std::string label;
    std::string plainValue;
    std::string ansiValue;
    bool hasColor = false;

    std::vector<char> inputBuf;

    bool navigable = false;
    std::string navChrom;
    int navPos = 0;

    bool isSection = false;
    std::string sectionKey;
};

static void execReadPopupCommand(Manager::GwPlot* plot,
                                     const Manager::GwPlot::ReadPopup& rp,
                                     const std::string& cmd_str,
                                     bool& redraw) {
        if (plot->remoteCommand) {
            const auto action = cmd_str.rfind("find", 0) == 0 ? "find" : cmd_str;
            plot->remoteCommand("read-popup " + std::to_string(rp.uid) + " " + action);
            return;
        }
        int previousRegionSelection = plot->regionSelection;
        if (!rp.sam.empty()) {
            plot->selectedAlign = rp.sam;
        }
        if (!rp.qname.empty()) {
            plot->target_qname = rp.qname;
        }
        if (rp.regionSelection >= 0 && rp.regionSelection < (int)plot->regions.size()) {
            plot->regionSelection = rp.regionSelection;
        }
        execCommand(plot, cmd_str, redraw);
        plot->regionSelection = previousRegionSelection;
    }

// Magnifying glass icon (Find)
    static void drawMagnifyIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col, float thickness) {
        float w = max.x - min.x;
        float h = max.y - min.y;
        float r  = std::min(w, h) * 0.24f;
        float cx = min.x + w * 0.40f;
        float cy = min.y + h * 0.36f;
        dl->AddCircle(ImVec2(cx, cy), r, col, 12, thickness);
        // Handle: 45° from circle centre toward bottom-right
        float hx1 = cx + r * 0.707f;
        float hy1 = cy + r * 0.707f;
        float hx2 = max.x - w * 0.2f;
        float hy2 = max.y - h * 0.22f;
        dl->AddLine(ImVec2(hx1, hy1), ImVec2(hx2, hy2), col, thickness + 0.5f);
    }

static ImVec4 ansiCodeToColor(const std::string& code, const ImVec4& defaultColor) {
    if (code == "0" || code == "00") return defaultColor;  // reset
    if (code == "1") return defaultColor;  // bold — keep current
    if (code == "3") return defaultColor;  // italic — ignore
    if (code == "4") return defaultColor;  // underline — ignore
    // Standard foreground colors
    if (code == "30") return ImVec4(0.40f, 0.40f, 0.40f, 1);  // grey/black
    if (code == "31") return ImVec4(0.90f, 0.20f, 0.20f, 1);  // red
    if (code == "32") return ImVec4(0.20f, 0.75f, 0.20f, 1);  // green
    if (code == "33") return ImVec4(0.85f, 0.75f, 0.15f, 1);  // yellow
    if (code == "34") return ImVec4(0.30f, 0.50f, 1.00f, 1);  // blue
    if (code == "35") return ImVec4(0.70f, 0.40f, 1.00f, 1);  // magenta
    if (code == "36") return ImVec4(0.20f, 0.80f, 0.80f, 1);  // cyan
    if (code == "37") return ImVec4(0.90f, 0.90f, 0.90f, 1);  // white
    // Bright foreground colors
    if (code == "90") return ImVec4(0.50f, 0.50f, 0.50f, 1);  // bright grey
    if (code == "91") return ImVec4(1.00f, 0.35f, 0.35f, 1);  // bright red
    if (code == "92") return ImVec4(0.35f, 0.90f, 0.35f, 1);  // bright green
    if (code == "93") return ImVec4(1.00f, 0.90f, 0.30f, 1);  // bright yellow
    if (code == "94") return ImVec4(0.50f, 0.70f, 1.00f, 1);  // bright blue
    if (code == "95") return ImVec4(0.90f, 0.50f, 1.00f, 1);  // bright magenta
    if (code == "96") return ImVec4(0.40f, 0.95f, 0.95f, 1);  // bright cyan
    if (code == "97") return ImVec4(1.00f, 1.00f, 1.00f, 1);  // bright white
    return defaultColor;
}

// Renders ANSI-colored text directly to a draw list at a given screen position,
// clipped to [clipMin, clipMax].  Used to paint colored text beneath a transparent
// InputText overlay so that mouse text-selection still works.
static void drawAnsiTextAtPos(ImDrawList* dl, ImVec2 origin,
                                const std::string& ansiText,
                                ImVec2 clipMin, ImVec2 clipMax) {
    ImVec4 defaultColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    ImVec4 currentColor = defaultColor;
    ImFont* font        = ImGui::GetFont();
    float   fontSize    = ImGui::GetFontSize();
    float   x           = origin.x;
    ImVec4  clip(clipMin.x, clipMin.y, clipMax.x, clipMax.y);

    size_t i = 0;
    while (i < ansiText.size()) {
        if (ansiText[i] == '\r' || ansiText[i] == '\n') { ++i; continue; }
        if (ansiText[i] == '\033' && i + 1 < ansiText.size() && ansiText[i + 1] == '[') {
            size_t j = i + 2;
            while (j < ansiText.size() && ansiText[j] != 'm') ++j;
            if (j < ansiText.size()) {
                std::string code = ansiText.substr(i + 2, j - (i + 2));
                size_t semi = code.rfind(';');
                if (semi != std::string::npos) code = code.substr(semi + 1);
                currentColor = ansiCodeToColor(code, defaultColor);
                i = j + 1;
            } else {
                ++i;
            }
        } else {
            size_t start = i;
            while (i < ansiText.size() &&
                    ansiText[i] != '\033' && ansiText[i] != '\n' && ansiText[i] != '\r') {
                ++i;
            }
            if (i > start) {
                const char* tb = ansiText.c_str() + start;
                const char* te = ansiText.c_str() + i;
                ImU32 col = ImGui::ColorConvertFloat4ToU32(currentColor);
                dl->AddText(font, fontSize, ImVec2(x, origin.y), col, tb, te, 0.0f, &clip);
                x += font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, tb, te).x;
            }
        }
    }
}

static std::vector<RowData> parseAnsiRows(const std::string& ansiText)
{
    std::vector<RowData> rows;
    size_t start = 0;

    while (start < ansiText.size()) {
        size_t end = ansiText.find('\n', start);
        if (end == std::string::npos)
            end = ansiText.size();

        std::string line = ansiText.substr(start, end - start);
        start = end + 1;

        while (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.size() < 4 ||
            line[0] != '\033' ||
            line[1] != '[' ||
            line[2] != '1' ||
            line[3] != 'm')
            continue;

        size_t resetPos = line.find("\033[", 4);
        if (resetPos == std::string::npos)
            continue;

        std::string label = line.substr(4, resetPos - 4);
        while (!label.empty() && label.back() == ' ')
            label.pop_back();

        size_t mPos = resetPos + 2;
        while (mPos < line.size() && line[mPos] != 'm')
            ++mPos;
        if (mPos < line.size())
            ++mPos;

        std::string ansiVal = line.substr(mPos);
        std::string plainVal = stripAnsiCodes(ansiVal);
        bool hasColor = (ansiVal != plainVal);

        if (!label.empty()) {
            RowData rd;
            rd.label = label;
            rd.plainValue = plainVal;
            rd.ansiValue = ansiVal;
            rd.hasColor = hasColor;

            rd.inputBuf.assign(plainVal.begin(), plainVal.end());
            rd.inputBuf.push_back('\0');

            rows.push_back(std::move(rd));
        }
    }

    return rows;
}

// Parses ANSI-coded output from Term::printTrack into display rows.
    // The track output format is a single line:
    //   \r[bold]filename[reset]    [cyan]chrom:start-end[reset][bold]    Key  [reset]value ...
    // followed optionally by a second line with tab-separated feature fields.
    // Explain a SAM flag: each set bit, plus the strand of the read and its mate
    static void drawSamFlagTooltip(const std::string& value) {
        int flag = 0;
        try { flag = std::stoi(value); } catch (...) { return; }
        static const std::pair<int, const char*> bits[] = {
            {0x1, "Paired"}, {0x2, "Proper pair"}, {0x4, "Read unmapped"}, {0x8, "Mate unmapped"},
            {0x10, "Read reverse strand"}, {0x20, "Mate reverse strand"}, {0x40, "First in pair"},
            {0x80, "Second in pair"}, {0x100, "Secondary"}, {0x200, "Fails QC"},
            {0x400, "Duplicate"}, {0x800, "Supplementary"},
        };
        ImGui::BeginTooltip();
        if (ImGui::BeginTable("##samflag", 2, ImGuiTableFlags_SizingFixedFit)) {
            auto row = [](const char* bit, const char* meaning, bool dim) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextDisabled("%s", bit);
                ImGui::TableSetColumnIndex(1);
                if (dim) ImGui::TextDisabled("%s", meaning); else ImGui::TextUnformatted(meaning);
            };
            for (const auto& b : bits) {
                if (!(flag & b.first)) continue;
                char hex[16];
                std::snprintf(hex, sizeof(hex), "%d", b.first);
                row(hex, b.second, false);
            }
            // Strands are implied by unset bits, so name them too
            if (!(flag & 0x4) && !(flag & 0x10)) row("", "Read forward strand", true);
            if ((flag & 0x1) && !(flag & 0x8) && !(flag & 0x20)) row("", "Mate forward strand", true);
            if (flag == 0) row("0", "Unpaired, forward strand", false);
            ImGui::EndTable();
        }
        ImGui::EndTooltip();
    }

    static std::vector<RowData> parseTrackRows(const std::string& ansiText) {
        std::vector<RowData> rows;

        // Split into lines, stripping \r
        std::vector<std::string> lines;
        {
            size_t pos = 0;
            while (pos < ansiText.size()) {
                size_t nl = ansiText.find('\n', pos);
                if (nl == std::string::npos) nl = ansiText.size();
                std::string line = ansiText.substr(pos, nl - pos);
                while (!line.empty() && (line.front() == '\r')) line = line.substr(1);
                while (!line.empty() && line.back() == '\r') line.pop_back();
                if (!line.empty()) lines.push_back(std::move(line));
                pos = nl + 1;
            }
        }
        if (lines.empty()) return rows;

        auto makeRow = [](const std::string& label, const std::string& plain) -> RowData {
            RowData rd{label, plain, plain, false, {}};
            rd.inputBuf.assign(plain.begin(), plain.end());
            rd.inputBuf.push_back('\0');
            return rd;
        };

        // Tokenize the first line into (ANSI-code, following-text) pairs
        struct Token { std::string code; std::string text; };
        std::vector<Token> tokens;
        {
            Token cur;
            const std::string& line = lines[0];
            size_t i = 0;
            while (i < line.size()) {
                if (line[i] == '\033' && i + 1 < line.size() && line[i + 1] == '[') {
                    if (!cur.text.empty() || !tokens.empty()) {
                        tokens.push_back(std::move(cur));
                        cur = {};
                    }
                    size_t j = i + 2;
                    while (j < line.size() && line[j] != 'm') ++j;
                    cur.code = line.substr(i + 2, j - (i + 2));
                    i = (j < line.size()) ? j + 1 : j;
                } else {
                    cur.text += line[i++];
                }
            }
            if (!cur.text.empty()) tokens.push_back(std::move(cur));
        }

        // Interpret tokens: first bold → filename (skipped), first cyan → region,
        // then alternating bold/reset pairs → label/value rows.
        bool foundFile = false, foundRegion = false;
        std::string pendingLabel;

        for (auto& tok : tokens) {
            if (tok.code == "1") {
                if (!foundFile) {
                    // First bold segment is the filename — skip, don't display
                    foundFile = true;
                } else {
                    // Subsequent bold segments are labels (Parent, ID, Type, …)
                    pendingLabel = tok.text;
                    while (!pendingLabel.empty() && pendingLabel.front() == ' ')
                        pendingLabel = pendingLabel.substr(1);
                    while (!pendingLabel.empty() && pendingLabel.back() == ' ')
                        pendingLabel.pop_back();
                }
            } else if (tok.code == "36") {
                // Cyan segment is the genomic region — show as plain text
                if (!foundRegion && !tok.text.empty()) {
                    rows.push_back(makeRow("region", tok.text));
                    foundRegion = true;
                }
            } else if (tok.code == "0" || tok.code == "00") {
                // Reset: text after reset is the value for the pending label
                if (!pendingLabel.empty()) {
                    std::string val = tok.text;
                    while (!val.empty() && val.front() == ' ') val = val.substr(1);
                    while (!val.empty() && val.back() == ' ') val.pop_back();
                    if (!val.empty()) {
                        rows.push_back(makeRow(pendingLabel, val));
                        pendingLabel.clear();
                    }
                }
            }
        }

        // Second+ lines: tab-separated feature fields — extract length and strand
        for (size_t li = 1; li < lines.size(); ++li) {
            const std::string& feat = lines[li];
            if (feat.empty()) continue;

            // Split by tab
            std::vector<std::string> fields;
            {
                size_t p = 0;
                while (p <= feat.size()) {
                    size_t tab = feat.find('\t', p);
                    if (tab == std::string::npos) tab = feat.size();
                    fields.push_back(feat.substr(p, tab - p));
                    p = tab + 1;
                }
            }
            // Remove trailing empty / "\n" marker fields
            while (!fields.empty() && (fields.back().empty() || fields.back() == "\n"))
                fields.pop_back();

            // Detect GFF3 (9 standard columns, strand at index 6)
            // vs BED (3+ columns, strand at index 5 if present)
            bool isGFF = false;
            if (fields.size() >= 9) {
                const auto& s = fields[6];
                if ((s == "+" || s == "-" || s == ".")) {
                    try { std::stoi(fields[3]); std::stoi(fields[4]); isGFF = true; }
                    catch (...) {}
                }
            }

            std::string lenStr, strandStr;
            if (isGFF) {
                try {
                    int start = std::stoi(fields[3]);
                    int end   = std::stoi(fields[4]);
                    lenStr    = std::to_string(end - start);
                } catch (...) {}
                strandStr = fields[6];
            } else if (fields.size() >= 3) {
                try {
                    int start = std::stoi(fields[1]);
                    int end   = std::stoi(fields[2]);
                    lenStr    = std::to_string(end - start);
                } catch (...) {}
                if (fields.size() >= 6) strandStr = fields[5];
            }

            if (!lenStr.empty())    rows.push_back(makeRow("length", lenStr));
            if (!strandStr.empty()) rows.push_back(makeRow("strand", strandStr));
        }

        return rows;
    }

// ─────────────────────────────────────────────────────────────────────────────
// Info popup draw
// ─────────────────────────────────────────────────────────────────────────────

void drawImGuiInfoPopup(Manager::GwPlot* plot) {
        if (plot->readPopups.empty()) return;

        // Per-popup row cache keyed on popup UID.
        static std::unordered_map<int, std::vector<RowData>> rowCache;

        auto& style = ImGui::GetStyle();
        float ms    = std::max(plot->monitorScale, 1.0f);

        // Shared row-height metrics (same for all popups since font is shared).
        float valColW     = 360.f;
        float rowContentH = ImGui::GetTextLineHeight() + style.FramePadding.y * 2;
        float scrollH_sz  = style.ScrollbarSize;
        float rowH_plain  = rowContentH + style.CellPadding.y * 2;
        float rowH_color  = rowContentH + scrollH_sz + style.CellPadding.y * 2;
        float titleH      = ImGui::GetFrameHeight();

        std::vector<int> toRemove;
        struct PendingReadPopupAction {
            Manager::GwPlot::ReadPopup popup;
            std::string command;
        };
        std::optional<PendingReadPopupAction> pendingAction;

        for (size_t popupIdx = 0; popupIdx < plot->readPopups.size(); ++popupIdx) {
            auto& rp = plot->readPopups[popupIdx];
            // Build row cache once per popup.
            if (rowCache.find(rp.uid) == rowCache.end())
                rowCache[rp.uid] = parseAnsiRows(rp.ansi);
            auto& rows = rowCache[rp.uid];
            if (rows.empty()) { toRemove.push_back(rp.uid); continue; }

            // ── Per-popup column widths and window size ───────────────────────
            float labelColW = 0;
            for (auto& row : rows) {
                float w = ImGui::CalcTextSize(row.label.c_str()).x
                        + style.FramePadding.x * 2 + 4;
                if (w > labelColW) labelColW = w;
            }
            float tableH = 0;
            for (auto& r : rows) tableH += r.hasColor ? rowH_color : rowH_plain;
            float winW = labelColW + valColW
                       + style.ScrollbarSize
                       + style.WindowPadding.x * 2
                       + style.CellPadding.x * 2 + 2;
            float winH = titleH + style.WindowPadding.y * 2 + tableH;

            // ── Position: right-flush with screen, top at first alignment track ──
            // Cascade multiple popups so they do not stack at the same position.
            auto& io   = ImGui::GetIO();
            float cascadeOffset = (float)popupIdx * 20.0f;
            float defX = std::max(0.0f, io.DisplaySize.x - winW - cascadeOffset);
            float defY = (plot->refSpace + plot->covY) / ms + cascadeOffset;
            ImGui::SetNextWindowPos(ImVec2(defX, defY), ImGuiCond_Appearing);
            ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Appearing);
            ImGui::SetNextWindowSizeConstraints(ImVec2(300, 100), ImVec2(FLT_MAX, FLT_MAX));

            bool windowOpen = true;
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing
                                   | ImGuiWindowFlags_NoNav;

            std::string winTitle = "Read info##readpopup" + std::to_string(rp.uid);
            ImGui::Begin(winTitle.c_str(), &windowOpen, flags);
            closeOnEscape(&windowOpen);

            // ── Read action buttons in title bar ──────────────────────────────
            {
                float titleBarH = ImGui::GetFrameHeight();
                float iconSide  = titleBarH - 6.0f;
                ImVec2 winPos   = ImGui::GetWindowPos();
                float  winWidth = ImGui::GetWindowWidth();
                float rightInset = 2.0f;
                float topInset = 3.0f;
                float slotWidth = titleBarH * 0.95f;
                PopupTitleIconClip titleIconClip;
                ImDrawList* fgDl = titleIconClip.drawList;
                ImVec2 mp  = ImGui::GetIO().MousePos;

                float titleIconStroke = iconStrokeThickness(ms);
                auto drawTitleIconButton = [&](const char* tooltip,
                                               int slotFromRight,
                                               const auto& iconDrawer) {
                    float minX = winPos.x + winWidth - titleBarH - rightInset - slotWidth * slotFromRight;
                    ImVec2 iconMin(minX, winPos.y + topInset);
                    ImVec2 iconMax(iconMin.x + iconSide, iconMin.y + iconSide);
                    bool hov = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && mp.x >= iconMin.x && mp.x <= iconMax.x
                            && mp.y >= iconMin.y && mp.y <= iconMax.y;
                    if (hov) {
                        fgDl->AddRectFilled(iconMin, iconMax,
                                            ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                                            style.FrameRounding);
                        ImGui::SetTooltip("%s", tooltip);
                    }
                    ImU32 col = hov ? ImGui::GetColorU32(ImGuiCol_Text)
                                    : ImGui::GetColorU32(ImGuiCol_Text, 0.7f);
                    iconDrawer(fgDl, iconMin, iconMax, col, titleIconStroke);
                    return hov && ImGui::IsMouseClicked(0);
                };

                if (drawTitleIconButton("Go to mate", 4, drawArrowRightIcon)) {
                    pendingAction = PendingReadPopupAction{rp, "mate"};
                }

                if (!plot->remoteCommand && drawTitleIconButton("Add mate", 3, drawPlusIcon)) {
                    pendingAction = PendingReadPopupAction{rp, "mate add"};
                }

                if (drawTitleIconButton("Find reads", 2, drawMagnifyIcon)) {
                    if (!rp.qname.empty()) {
                        pendingAction = PendingReadPopupAction{rp, "find " + rp.qname};
                    } else {
                        pendingAction = PendingReadPopupAction{rp, "find"};
                    }
                }

                if (drawTitleIconButton("Copy as SAM", 1, drawClipboardIcon) && !rp.sam.empty()) {
                    ImGui::SetClipboardText(rp.sam.c_str());
                }
            }

            // ── Two-column table: label button | value ────────────────────────
            ImGuiTableFlags tflags = ImGuiTableFlags_SizingFixedFit
                                   | ImGuiTableFlags_BordersInnerV;
            if (ImGui::BeginTable("##rtable", 2, tflags)) {
                ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, labelColW);
                ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthFixed, valColW);

                for (size_t i = 0; i < rows.size(); ++i) {
                    auto& row = rows[i];
                    ImGui::TableNextRow();

                    // Column 0: transparent button — click copies value to clipboard.
                    // Shift cursor down by FramePadding.y so the SmallButton text
                    // (which uses FramePadding.y = 0) aligns with InputText text.
                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.FramePadding.y);
                    ImGui::PushID(rp.uid * 10000 + (int)i);
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 0.2f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.4f, 0.4f, 0.4f, 0.4f));
                    if (ImGui::SmallButton(row.label.c_str())) {
                        if (row.label == "seq") {
                            std::string seq;
                            seq.reserve(row.plainValue.size());
                            for (unsigned char c : row.plainValue) {
                                char u = (char)std::toupper(c);
                                if (u=='A'||u=='T'||u=='C'||u=='G'||u=='N') seq += u;
                            }
                            ImGui::SetClipboardText(seq.c_str());
                        } else {
                            ImGui::SetClipboardText(row.plainValue.c_str());
                        }
                    }
                    ImGui::PopStyleColor(3);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                        ImGui::SetTooltip(row.label == "seq"
                                          ? "Copy sequence (bases only)"
                                          : "Copy %s", row.label.c_str());
                    }
                    ImGui::PopID();

                    // Column 1: value
                    ImGui::TableSetColumnIndex(1);
                    char inputId[64];
                    std::snprintf(inputId, sizeof(inputId), "##iv%d_%zu", rp.uid, i);
                    if (row.hasColor) {
                        char childId[72];
                        std::snprintf(childId, sizeof(childId), "##colorchild%d_%zu", rp.uid, i);
                        ImVec2 childSz(valColW, rowContentH + scrollH_sz);
                        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                        ImGui::BeginChild(childId, childSz, ImGuiChildFlags_None,
                                          ImGuiWindowFlags_HorizontalScrollbar
                                          | ImGuiWindowFlags_NoScrollWithMouse);
                        ImGui::PopStyleVar();

                        ImVec2 cellScreen  = ImGui::GetCursorScreenPos();
                        ImVec2 childWinPos = ImGui::GetWindowPos();
                        ImVec2 clipMin(childWinPos.x, childWinPos.y);
                        ImVec2 clipMax(childWinPos.x + valColW, childWinPos.y + rowContentH);
                        ImVec2 textOrigin(cellScreen.x + style.FramePadding.x,
                                          cellScreen.y + style.FramePadding.y);
                        drawAnsiTextAtPos(ImGui::GetWindowDrawList(),
                                          textOrigin, row.ansiValue, clipMin, clipMax);

                        ImFont* font      = ImGui::GetFont();
                        float   fontSize  = ImGui::GetFontSize();
                        const char* bufB  = row.inputBuf.data();
                        const char* bufE  = bufB + row.inputBuf.size() - 1;
                        float textPixelW  = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f,
                                                                 bufB, bufE).x
                                            + style.FramePadding.x * 2 + 2.0f;
                        ImGui::SetNextItemWidth(textPixelW);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                        ImGui::PushStyleColor(ImGuiCol_Text,    ImVec4(0, 0, 0, 0));
                        ImGui::InputText(inputId,
                                         row.inputBuf.data(), row.inputBuf.size(),
                                         ImGuiInputTextFlags_ReadOnly);
                        ImGui::PopStyleColor(2);
                        ImGui::EndChild();
                    } else {
                        ImGui::SetNextItemWidth(valColW);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                        ImGui::InputText(inputId,
                                         row.inputBuf.data(), row.inputBuf.size(),
                                         ImGuiInputTextFlags_ReadOnly);
                        ImGui::PopStyleColor();
                        if (row.label == "flag" && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                            drawSamFlagTooltip(row.plainValue);
                        }
                    }
                }
                ImGui::EndTable();
            }

            ImGui::End();

            if (!windowOpen) {
                toRemove.push_back(rp.uid);
                rowCache.erase(rp.uid);
            }
        }

        // Remove closed popups.
        if (!toRemove.empty()) {
            plot->readPopups.erase(
                std::remove_if(plot->readPopups.begin(), plot->readPopups.end(),
                               [&](const Manager::GwPlot::ReadPopup& p) {
                                   return std::find(toRemove.begin(), toRemove.end(),
                                                    p.uid) != toRemove.end();
                               }),
                plot->readPopups.end());
        }

        if (pendingAction.has_value()) {
            execReadPopupCommand(plot, pendingAction->popup, pendingAction->command, plot->redraw);
        }
    }

// ─────────────────────────────────────────────────────────────────────────────
// Coverage popup
// ─────────────────────────────────────────────────────────────────────────────

// Parse key:value integers from the plain-text coverage line.
    // e.g. "Coverage    45      A:20  T:3    Pos  1,234,567" → returns value for key
    static int parseCovInt(const std::string& plain, const std::string& key) {
        size_t p = plain.find(key);
        if (p == std::string::npos) return 0;
        p += key.size();
        while (p < plain.size() && plain[p] == ' ') ++p;
        int v = 0;
        while (p < plain.size() && std::isdigit((unsigned char)plain[p]))
            v = v * 10 + (plain[p++] - '0');
        return v;
    }

    void drawImGuiCovPopup(Manager::GwPlot* plot) {
        if (plot->covPopups.empty()) return;

        ImFont* font     = ImGui::GetFont();
        float   fontSize = ImGui::GetFontSize();
        auto&   style    = ImGui::GetStyle();
        auto&   io       = ImGui::GetIO();

        auto textW = [&](const char* s) {
            return font->CalcTextSizeA(fontSize, FLT_MAX, 0.f, s).x;
        };
        auto valW = [&](int v) {
            std::string sv = std::to_string(v);
            return font->CalcTextSizeA(fontSize, FLT_MAX, 0.f,
                                       sv.c_str(), sv.c_str() + sv.size()).x;
        };
        float minW = textW("000");  // 3-char floor

        static const ImVec4 clrA(0.20f, 0.75f, 0.20f, 1.f);
        static const ImVec4 clrT(0.90f, 0.20f, 0.20f, 1.f);
        static const ImVec4 clrC(0.30f, 0.50f, 1.00f, 1.f);
        static const ImVec4 clrG(0.85f, 0.75f, 0.15f, 1.f);

        const float kWinPad = 3.f;
        float lineH  = ImGui::GetTextLineHeight();
        float cpy2   = style.CellPadding.y * 2.f;
        float tableH = (lineH + cpy2) * 2.f;
        float titleH = ImGui::GetFrameHeight();

        std::vector<int> toRemove;

        for (auto& cp : plot->covPopups) {
            // ── Parse the captured coverage line ─────────────────────────────
            std::string plain = stripAnsiCodes(cp.ansi);
            size_t s0 = 0;
            while (s0 < plain.size() && (plain[s0] == '\r' || plain[s0] == '\n')) ++s0;
            if (s0) plain.erase(0, s0);

            int totCov = parseCovInt(plain, "Coverage    ");
            int mA     = parseCovInt(plain, "A:");
            int mT     = parseCovInt(plain, "T:");
            int mC     = parseCovInt(plain, "C:");
            int mG     = parseCovInt(plain, "G:");

            // ── Clipboard text ────────────────────────────────────────────────
            std::string clipText = std::to_string(totCov) + "\t"
                                 + std::to_string(mA)     + "\t"
                                 + std::to_string(mT)     + "\t"
                                 + std::to_string(mC)     + "\t"
                                 + std::to_string(mG)     + "\t"
                                 + cp.chromPos;

            // ── Per-column widths: max(3-char min, header, value) + padding ──
            float cpx2 = style.CellPadding.x * 2.f;
            auto colW = [&](int val, const char* hdr) -> float {
                return std::max({minW, valW(val), textW(hdr)}) + cpx2;
            };
            float wCov = colW(totCov, "Cov");
            float wA   = colW(mA,     "A");
            float wT   = colW(mT,     "T");
            float wC   = colW(mC,     "C");
            float wG   = colW(mG,     "G");

            // 4 inner separators + window border on each side
            float tableW = wCov + wA + wT + wC + wG + 4.f;
            float winW   = tableW + kWinPad * 2.f + style.WindowBorderSize * 2.f + 30.f;
            float winH   = titleH + tableH + kWinPad * 2.f;

            // ── Position near click, clamped to screen ────────────────────────
            float posX = cp.x + 8.f;
            float posY = cp.y - winH - 4.f;
            if (posY < 0.f) posY = cp.y + 4.f;
            posX = std::max(0.f, std::min(posX, io.DisplaySize.x - winW));
            posY = std::max(0.f, std::min(posY, io.DisplaySize.y - winH));

            ImGui::SetNextWindowPos(ImVec2(posX, posY), ImGuiCond_Appearing);
            ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Appearing);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(kWinPad, kWinPad));

            ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing
                                   | ImGuiWindowFlags_NoNav
                                   | ImGuiWindowFlags_NoCollapse
                                   | ImGuiWindowFlags_NoResize
                                   | ImGuiWindowFlags_NoScrollbar;

            bool windowOpen = true;
            std::string winTitle = cp.chromPos + "##covpopup" + std::to_string(cp.uid);
            ImGui::Begin(winTitle.c_str(), &windowOpen, flags);
            closeOnEscape(&windowOpen);
            ImGui::PopStyleVar();  // WindowPadding

            // ── Clipboard icon in the title bar ───────────────────────────────
            {
                float titleBarH = ImGui::GetFrameHeight();
                float iconSide  = titleBarH - 4.f;
                ImVec2 winPos   = ImGui::GetWindowPos();
                float  winWidth = ImGui::GetWindowWidth();
                ImVec2 iconMin(winPos.x + winWidth - titleBarH * 2.f, winPos.y + 2.f);
                ImVec2 iconMax(iconMin.x + iconSide, iconMin.y + iconSide);

                ImVec2 mp  = ImGui::GetIO().MousePos;
                bool   hov = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && mp.x >= iconMin.x && mp.x <= iconMax.x
                          && mp.y >= iconMin.y && mp.y <= iconMax.y;

                PopupTitleIconClip titleIconClip;
                ImDrawList* fgDl = titleIconClip.drawList;
                if (hov) {
                    fgDl->AddRectFilled(iconMin, iconMax,
                                        ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                                        style.FrameRounding);
                    ImGui::SetTooltip("Copy coverage data");
                }
                ImU32 iconCol = hov ? ImGui::GetColorU32(ImGuiCol_Text)
                                    : ImGui::GetColorU32(ImGuiCol_Text, 0.7f);
                drawClipboardIcon(fgDl, iconMin, iconMax, iconCol,
                                  iconStrokeThickness(plot->monitorScale));

                if (hov && ImGui::IsMouseClicked(0))
                    ImGui::SetClipboardText(clipText.c_str());
            }

            // ── Compact 2-row table: header + counts ─────────────────────────
            ImGuiTableFlags tflags = ImGuiTableFlags_SizingFixedFit
                                   | ImGuiTableFlags_BordersInnerV;
            ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, ImVec4(0, 0, 0, 0));
            if (ImGui::BeginTable("##covtable", 5, tflags)) {
                ImGui::TableSetupColumn("Cov", ImGuiTableColumnFlags_WidthFixed, wCov);
                ImGui::TableSetupColumn("A",   ImGuiTableColumnFlags_WidthFixed, wA);
                ImGui::TableSetupColumn("T",   ImGuiTableColumnFlags_WidthFixed, wT);
                ImGui::TableSetupColumn("C",   ImGuiTableColumnFlags_WidthFixed, wC);
                ImGui::TableSetupColumn("G",   ImGuiTableColumnFlags_WidthFixed, wG);
                ImGui::TableHeadersRow();
                ImGui::PopStyleColor();  // TableHeaderBg

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0); ImGui::Text("%d", totCov);
                ImGui::TableSetColumnIndex(1); ImGui::TextColored(clrA, "%d", mA);
                ImGui::TableSetColumnIndex(2); ImGui::TextColored(clrT, "%d", mT);
                ImGui::TableSetColumnIndex(3); ImGui::TextColored(clrC, "%d", mC);
                ImGui::TableSetColumnIndex(4); ImGui::TextColored(clrG, "%d", mG);

                ImGui::EndTable();
            } else {
                ImGui::PopStyleColor();
            }

            ImGui::End();

            if (!windowOpen)
                toRemove.push_back(cp.uid);
        }

        // Remove closed popups.
        if (!toRemove.empty()) {
            plot->covPopups.erase(
                std::remove_if(plot->covPopups.begin(), plot->covPopups.end(),
                               [&](const Manager::GwPlot::CovPopup& p) {
                                   return std::find(toRemove.begin(), toRemove.end(),
                                                    p.uid) != toRemove.end();
                               }),
                plot->covPopups.end());
        }
    }

// ─────────────────────────────────────────────────────────────────────────────
// Track popup
// ─────────────────────────────────────────────────────────────────────────────

    // The "Feature data" of a track popup: the source lines of the clicked feature (a
    // GFF transcript's exon/CDS/UTR lines, or a BED line), split into columns. Values
    // that are identical on every line are listed once, as `shared`, so each row only
    // shows what differs between lines.
    struct FeatureDataView {
        std::vector<std::pair<std::string, std::string>> shared;  // name, value
        std::vector<std::string> header;                          // names of the per-line columns
        std::vector<std::vector<std::string>> cells;              // per line, per column
        std::vector<int> starts, ends;                            // 0-based half-open; -1 = unknown
        int clicked{-1};                                          // line under the click
    };

    static FeatureDataView parseFeatureData(const Manager::GwPlot::TrackPopup& tp) {
        using Fmt = Manager::GwPlot::TrackPopup::DataFormat;
        FeatureDataView v;
        std::vector<std::vector<std::string>> lines;
        size_t nCols = 0;
        for (size_t pos = 0; pos < tp.details.size();) {
            size_t nl = tp.details.find('\n', pos);
            if (nl == std::string::npos) nl = tp.details.size();
            std::string line = tp.details.substr(pos, nl - pos);
            while (!line.empty() && line.back() == '\r') line.pop_back();
            pos = nl + 1;
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> cols;
            Utils::split(line, '\t', cols);
            nCols = std::max(nCols, cols.size());
            lines.push_back(std::move(cols));
        }
        if (lines.empty()) return v;

        static const char* gffNames[] = {"seqid", "source", "type", "start", "end", "score",
                                         "strand", "phase", "attributes"};
        static const char* bedNames[] = {"chrom", "start", "end", "name", "score", "strand",
                                         "thickStart", "thickEnd", "itemRgb", "blockCount",
                                         "blockSizes", "blockStarts"};
        auto colName = [&](size_t c) -> std::string {
            if (tp.format == Fmt::Gff && c < 9) return gffNames[c];
            if (tp.format == Fmt::Bed && c < 12) return bedNames[c];
            return "col" + std::to_string(c + 1);
        };
        const bool gff = tp.format == Fmt::Gff;
        const size_t attrCol = 8;
        const bool multi = lines.size() > 1;

        // GFF attributes are "key=value;..." (GFF3) or "key \"value\"; ..." (GTF). Split
        // them so attributes shared by every line (e.g. Parent) are only shown once.
        auto splitAttrs = [](const std::string& s) {
            std::vector<std::string> out;
            size_t p = 0;
            while (p <= s.size()) {
                size_t q = s.find(';', p);
                if (q == std::string::npos) q = s.size();
                std::string a = s.substr(p, q - p);
                size_t b = a.find_first_not_of(' '), e = a.find_last_not_of(' ');
                if (b != std::string::npos) out.push_back(a.substr(b, e - b + 1));
                p = q + 1;
            }
            return out;
        };
        std::vector<std::vector<std::string>> attrs(lines.size());
        if (gff) {
            for (size_t i = 0; i < lines.size(); ++i)
                if (lines[i].size() > attrCol) attrs[i] = splitAttrs(lines[i][attrCol]);
        }
        auto inAll = [&](const std::string& a) {
            for (auto& l : attrs)
                if (std::find(l.begin(), l.end(), a) == l.end()) return false;
            return true;
        };

        std::vector<size_t> varying;
        for (size_t c = 0; c < nCols; ++c) {
            bool same = multi;
            for (auto& l : lines) {
                if (l.size() <= c || l[c] != lines[0][c]) { same = false; break; }
            }
            if (gff && c == attrCol) {
                // Shared attributes go in the shared list; the rest stay per line.
                bool anyLeft = false;
                if (multi && lines[0].size() > attrCol) {
                    for (auto& a : attrs[0]) {
                        if (!inAll(a)) continue;
                        size_t eq = a.find_first_of("= ");
                        if (eq == std::string::npos) v.shared.emplace_back(a, "");
                        else {
                            std::string val = a.substr(eq + 1);
                            if (val.size() >= 2 && val.front() == '"' && val.back() == '"') val = val.substr(1, val.size() - 2);
                            v.shared.emplace_back(a.substr(0, eq), val);
                        }
                    }
                }
                for (auto& l : attrs) {
                    for (auto& a : l) if (!multi || !inAll(a)) { anyLeft = true; break; }
                    if (anyLeft) break;
                }
                if (anyLeft) varying.push_back(c);
            } else if (same) {
                v.shared.emplace_back(colName(c), lines[0][c]);
            } else {
                varying.push_back(c);
            }
        }

        for (size_t c : varying) v.header.push_back(colName(c));
        long bestSpan = -1;
        for (size_t i = 0; i < lines.size(); ++i) {
            const auto& l = lines[i];
            std::vector<std::string> row;
            for (size_t c : varying) {
                if (gff && c == attrCol) {
                    std::string rest;
                    for (auto& a : attrs[i]) {
                        if (multi && inAll(a)) continue;
                        if (!rest.empty()) rest += "; ";
                        rest += a;
                    }
                    row.push_back(rest);
                } else {
                    row.push_back(c < l.size() ? l[c] : "");
                }
            }
            v.cells.push_back(std::move(row));

            // Feature coordinates, to mark the clicked line and to pan to a line
            int s = -1, e = -1;
            auto num = [&](size_t c, int& out) {
                if (c >= l.size() || l[c].empty()) return false;
                char* end = nullptr;
                long n = std::strtol(l[c].c_str(), &end, 10);
                if (*end != '\0' || n < 0) return false;
                out = (int)n;
                return true;
            };
            if (gff && num(3, s) && num(4, e)) {
                s -= 1;  // GFF is 1-based, inclusive
            } else if (tp.format == Fmt::Bed && num(1, s) && num(2, e)) {
            } else {
                s = e = -1;
            }
            if (e < s) s = e = -1;
            v.starts.push_back(s);
            v.ends.push_back(e);
            // The narrowest feature under the click, e.g. the exon rather than the mRNA
            if (s >= 0 && !l.empty() && l[0] == tp.chrom && s <= tp.clickPos && tp.clickPos < std::max(e, s + 1)) {
                long span = (long)e - s;
                if (bestSpan < 0 || span < bestSpan) { bestSpan = span; v.clicked = (int)i; }
            }
        }
        return v;
    }

    // Pan the clicked region so feature [start, end) is centred, keeping the zoom level.
    static void panToFeature(Manager::GwPlot* plot, const Manager::GwPlot::TrackPopup& tp, int start, int end) {
        if (tp.regionIdx < 0 || tp.regionIdx >= (int)plot->regions.size()) return;
        const Utils::Region& rgn = plot->regions[tp.regionIdx];
        if (rgn.chrom != tp.chrom) return;  // the view has moved to another chromosome
        const int width = rgn.end - rgn.start;
        const int mid = start + (end - start) / 2;
        int newStart = std::max(0, mid - width / 2);
        std::string cmd = rgn.chrom + ":" + std::to_string(newStart) + "-" + std::to_string(newStart + width);
        if (plot->remoteCommand) {
            plot->remoteCommand(cmd);
            return;
        }
        const int previous = plot->regionSelection;
        plot->regionSelection = tp.regionIdx;
        std::ostringstream out;
        Commands::run_command_map(plot, cmd, out);
        plot->regionSelection = std::min(previous, (int)plot->regions.size() - 1);
        plot->processed = false;
        plot->redraw = true;
    }

    // Read-only, frameless text field: looks like text, but can be selected with the mouse.
    static void selectableText(const char* id, const std::string& text, float width) {
        ImGui::SetNextItemWidth(width);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
        ImGui::InputText(id, const_cast<char*>(text.c_str()), text.size() + 1, ImGuiInputTextFlags_ReadOnly);
        ImGui::PopStyleColor();
    }

    // Feature data as tables: values shared by every line once, then one row per line
    // with only the columns that differ. Each row starts with a play button that pans
    // the view to that line's feature; the clicked feature's row is highlighted.
    // Returns the width the tables need, so the popup can open wide enough.
    static float drawFeatureData(Manager::GwPlot* plot, const Manager::GwPlot::TrackPopup& tp,
                                 const FeatureDataView& fd) {
        auto& style = ImGui::GetStyle();
        const float padW = style.FramePadding.x * 2 + 2;
        auto textW = [&](const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x + padW; };

        float needW = 0;
        if (!fd.shared.empty()) {
            float keyW = 0, valW = 0;
            for (auto& kv : fd.shared) {
                keyW = std::max(keyW, ImGui::CalcTextSize(kv.first.c_str()).x);
                valW = std::max(valW, textW(kv.second));
            }
            if (ImGui::BeginTable("##fd_shared", 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("##k", ImGuiTableColumnFlags_WidthFixed, keyW);
                ImGui::TableSetupColumn("##v", ImGuiTableColumnFlags_WidthFixed, valW);
                for (size_t i = 0; i < fd.shared.size(); ++i) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.FramePadding.y);
                    ImGui::TextDisabled("%s", fd.shared[i].first.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::PushID((int)i);
                    selectableText("##sv", fd.shared[i].second, valW);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            needW = keyW + valW + style.CellPadding.x * 4;
            if (!fd.cells.empty()) ImGui::Spacing();
        }
        if (fd.cells.empty()) return needW;

        const size_t nCols = fd.header.size();
        std::vector<float> colW(nCols, 0);
        for (size_t c = 0; c < nCols; ++c) {
            colW[c] = ImGui::CalcTextSize(fd.header[c].c_str()).x + padW;
            for (auto& row : fd.cells)
                if (c < row.size()) colW[c] = std::max(colW[c], textW(row[c]));
        }
        const float btnW = ImGui::GetFrameHeight();
        float rowsW = btnW + style.CellPadding.x * 2;
        for (float w : colW) rowsW += w + style.CellPadding.x * 2;
        needW = std::max(needW, rowsW);
        ImGuiTableFlags tflags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV;
        if (!ImGui::BeginTable("##fd_rows", (int)nCols + 1, tflags)) return needW;
        ImGui::TableSetupColumn("##go", ImGuiTableColumnFlags_WidthFixed, btnW);
        for (size_t c = 0; c < nCols; ++c)
            ImGui::TableSetupColumn(fd.header[c].c_str(), ImGuiTableColumnFlags_WidthFixed, colW[c]);

        // Header: dimmed column names
        ImGui::TableNextRow();
        for (size_t c = 0; c < nCols; ++c) {
            ImGui::TableSetColumnIndex((int)c + 1);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + style.FramePadding.x);
            ImGui::TextDisabled("%s", fd.header[c].c_str());
        }

        const ImU32 accent = ImGui::GetColorU32(ImGuiCol_CheckMark);
        for (size_t i = 0; i < fd.cells.size(); ++i) {
            ImGui::TableNextRow();
            ImGui::PushID((int)i);
            const bool clicked = (int)i == fd.clicked;
            if (clicked)
                ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImGui::GetColorU32(ImGuiCol_Header, 0.6f));

            // Play button: pan to this feature
            ImGui::TableSetColumnIndex(0);
            if (fd.starts[i] >= 0) {
                ImVec2 p = ImGui::GetCursorScreenPos();
                const float side = ImGui::GetFrameHeight();
                if (ImGui::InvisibleButton("##go", ImVec2(side, side)))
                    panToFeature(plot, tp, fd.starts[i], fd.ends[i]);
                const bool hov = ImGui::IsItemHovered();
                if (hov) ImGui::SetTooltip("Move the view to this feature");
                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (hov)
                    dl->AddRectFilled(p, ImVec2(p.x + side, p.y + side),
                                      ImGui::GetColorU32(ImGuiCol_ButtonHovered), style.FrameRounding);
                const float h = side * 0.36f;
                const ImVec2 c(p.x + side * 0.5f + h * 0.15f, p.y + side * 0.5f);
                const ImVec2 a(c.x - h * 0.5f, c.y - h * 0.58f), b(c.x - h * 0.5f, c.y + h * 0.58f), t(c.x + h * 0.5f, c.y);
                if (clicked || hov) {
                    dl->AddTriangleFilled(a, b, t, clicked ? accent : ImGui::GetColorU32(ImGuiCol_Text));
                } else {
                    dl->AddTriangle(a, b, t, ImGui::GetColorU32(ImGuiCol_Text, 0.6f), 1.0f);
                }
            }
            for (size_t c = 0; c < nCols; ++c) {
                ImGui::TableSetColumnIndex((int)c + 1);
                ImGui::PushID((int)c);
                static const std::string empty;
                selectableText("##cell", c < fd.cells[i].size() ? fd.cells[i][c] : empty, colW[c]);
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
        return needW;
    }

void drawImGuiTrackPopup(Manager::GwPlot* plot) {
        if (plot->trackPopups.empty()) return;

        static std::unordered_map<int, std::vector<RowData>> rowCache;
        static std::unordered_map<int, FeatureDataView> featureCache;

        auto& style = ImGui::GetStyle();

        float valColW     = 180.f;
        float rowContentH = ImGui::GetTextLineHeight() + style.FramePadding.y * 2;
        float rowH_plain  = rowContentH + style.CellPadding.y * 2;
        float titleH      = ImGui::GetFrameHeight();

        std::vector<int> toRemove;

        for (auto& tp : plot->trackPopups) {
            if (rowCache.find(tp.uid) == rowCache.end())
                rowCache[tp.uid] = parseTrackRows(tp.ansi);
            auto& rows = rowCache[tp.uid];
            if (rows.empty()) { toRemove.push_back(tp.uid); continue; }

            // Compute column widths and window size
            float labelColW = 0;
            for (auto& row : rows) {
                float w = ImGui::CalcTextSize(row.label.c_str()).x
                        + style.FramePadding.x * 2 + 4;
                if (w > labelColW) labelColW = w;
            }
            float tableH = (float)rows.size() * rowH_plain;
            float winW = labelColW + valColW
                       + style.ScrollbarSize
                       + style.WindowPadding.x * 2
                       + style.CellPadding.x * 2 + 2;
            float winH = titleH + style.WindowPadding.y * 2 + tableH
                       + (tp.details.empty() ? 0 : ImGui::GetFrameHeightWithSpacing());

            // Position near the click, clamped to screen
            auto& io = ImGui::GetIO();
            float defX = std::min(tp.x, io.DisplaySize.x - winW);
            float defY = std::min(tp.y, io.DisplaySize.y - winH);
            if (defX < 0) defX = 0;
            if (defY < 0) defY = 0;
            ImGui::SetNextWindowPos(ImVec2(defX, defY), ImGuiCond_Appearing);
            ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Appearing);
            ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImVec2(FLT_MAX, FLT_MAX));

            bool windowOpen = true;
            ImGuiWindowFlags flags = ImGuiWindowFlags_NoFocusOnAppearing
                                   | ImGuiWindowFlags_NoNav;

            std::string winTitle = "Track info##trackpopup" + std::to_string(tp.uid);
            ImGui::Begin(winTitle.c_str(), &windowOpen, flags);
            closeOnEscape(&windowOpen);

            // Clipboard icon in title bar
            {
                float titleBarH = ImGui::GetFrameHeight();
                float iconSide  = titleBarH - 4.0f;
                ImVec2 winPos   = ImGui::GetWindowPos();
                float  winWidth = ImGui::GetWindowWidth();
                ImVec2 iconMin(winPos.x + winWidth - titleBarH * 2.0f, winPos.y + 2.0f);
                ImVec2 iconMax(iconMin.x + iconSide, iconMin.y + iconSide);

                ImVec2 mp  = ImGui::GetIO().MousePos;
                bool   hov = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && mp.x >= iconMin.x && mp.x <= iconMax.x
                          && mp.y >= iconMin.y && mp.y <= iconMax.y;

                PopupTitleIconClip titleIconClip;
                ImDrawList* fgDl = titleIconClip.drawList;
                if (hov) {
                    fgDl->AddRectFilled(iconMin, iconMax,
                                        ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                                        style.FrameRounding);
                    ImGui::SetTooltip(tp.isVcf ? "Copy var command" : "Copy all");
                }
                ImU32 col = hov ? ImGui::GetColorU32(ImGuiCol_Text)
                                : ImGui::GetColorU32(ImGuiCol_Text, 0.7f);
                drawClipboardIcon(fgDl, iconMin, iconMax, col,
                                  iconStrokeThickness(plot->monitorScale));

                if (hov && ImGui::IsMouseClicked(0)) {
                    if (tp.isVcf) {
                        // Copy the 'var' command for the clicked region
                        std::string regionStr;
                        for (auto& row : rows) {
                            if (row.label == "region") { regionStr = row.plainValue; break; }
                        }
                        ImGui::SetClipboardText(("var " + regionStr).c_str());
                    } else {
                        std::string allText;
                        for (auto& row : rows) {
                            allText += row.label + "\t" + row.plainValue + "\n";
                        }
                        ImGui::SetClipboardText(allText.c_str());
                    }
                }
            }

            // Two-column table: label button | value (all plain text, no color rows)
            ImGuiTableFlags tflags = ImGuiTableFlags_SizingFixedFit
                                   | ImGuiTableFlags_BordersInnerV;
            if (ImGui::BeginTable("##tktable", 2, tflags)) {
                ImGui::TableSetupColumn("##lbl", ImGuiTableColumnFlags_WidthFixed, labelColW);
                ImGui::TableSetupColumn("##val", ImGuiTableColumnFlags_WidthFixed, valColW);

                for (size_t i = 0; i < rows.size(); ++i) {
                    auto& row = rows[i];
                    ImGui::TableNextRow();

                    // Column 0: label button — click copies value to clipboard.
                    // Shift cursor down by FramePadding.y so the SmallButton text
                    // (which uses FramePadding.y = 0) aligns with InputText text.
                    ImGui::TableSetColumnIndex(0);
                    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + style.FramePadding.y);
                    ImGui::PushID(tp.uid * 10000 + (int)i);
                    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.4f, 0.4f, 0.4f, 0.2f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.4f, 0.4f, 0.4f, 0.4f));
                    if (ImGui::SmallButton(row.label.c_str()))
                        ImGui::SetClipboardText(row.plainValue.c_str());
                    ImGui::PopStyleColor(3);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("Copy %s", row.label.c_str());
                    ImGui::PopID();

                    // Column 1: read-only InputText for selectable value display
                    ImGui::TableSetColumnIndex(1);
                    char inputId[64];
                    std::snprintf(inputId, sizeof(inputId), "##tkiv%d_%zu", tp.uid, i);
                    ImGui::SetNextItemWidth(valColW);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
                    ImGui::InputText(inputId,
                                     row.inputBuf.data(), row.inputBuf.size(),
                                     ImGuiInputTextFlags_ReadOnly);
                    ImGui::PopStyleColor();
                }
                ImGui::EndTable();
            }

            float fitW = ImGui::GetWindowWidth();  // widened when feature data opens
            if (!tp.details.empty()) {
                ImGui::SetNextItemOpen(tp.detailsOpen, ImGuiCond_Always);
                bool open = ImGui::TreeNodeEx("Feature data", ImGuiTreeNodeFlags_NoTreePushOnOpen);
                if (open != tp.detailsOpen) {
                    tp.detailsOpen = open;
                    tp.fitHeight = true;  // grow to show the data, or shrink back when closed
                }
                if (tp.detailsOpen) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Copy"))
                        ImGui::SetClipboardText(tp.details.c_str());
                    auto fdIt = featureCache.find(tp.uid);
                    if (fdIt == featureCache.end())
                        fdIt = featureCache.emplace(tp.uid, parseFeatureData(tp)).first;
                    const FeatureDataView& fd = fdIt->second;
                    const float lineH = rowH_plain;
                    const float frameH = style.WindowPadding.y * 2 + style.ScrollbarSize;
                    float dataHeight;
                    if (tp.fitHeight) {
                        // Opening: show up to ~14 rows (shared values, header, one row per
                        // GFF child line), but keep the window on screen. The user can drag
                        // it taller afterwards.
                        int nLines = (int)fd.shared.size() + (fd.header.empty() ? 0 : 1) + (int)fd.cells.size();
                        dataHeight = (float)std::clamp(nLines, 1, 14) * lineH + frameH;
                        float roomBelow = io.DisplaySize.y - ImGui::GetWindowPos().y
                                        - ImGui::GetCursorPosY() - style.WindowPadding.y;
                        if (roomBelow < dataHeight) {
                            // Move the window up to make room, as far as the top of the screen
                            float shift = std::min(dataHeight - roomBelow, ImGui::GetWindowPos().y);
                            if (shift > 0) {
                                ImGui::SetWindowPos(ImVec2(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y - shift));
                                roomBelow += shift;
                            }
                        }
                        dataHeight = std::max(std::min(dataHeight, roomBelow), lineH + frameH);
                    } else {
                        // Afterwards the data fills the window, so resizing the window
                        // resizes the data view.
                        dataHeight = std::max(ImGui::GetContentRegionAvail().y, lineH + frameH);
                    }
                    ImGui::BeginChild("##feature_data", ImVec2(0, dataHeight), true,
                                      ImGuiWindowFlags_HorizontalScrollbar);
                    float needW = drawFeatureData(plot, tp, fd);
                    ImGui::EndChild();
                    if (tp.fitHeight) {
                        // Opening: widen to show every column, within the screen
                        float want = needW + style.WindowPadding.x * 4 + style.ScrollbarSize + 4;
                        float room = io.DisplaySize.x - ImGui::GetWindowPos().x;
                        fitW = std::max(fitW, std::min(want, room));
                    }
                }
            }
            // Keep the top anchored and fit the contents: always while the feature data
            // is closed, and once when it opens. While open the height is the user's, so
            // the window can be dragged taller to see every exon.
            if (!ImGui::IsWindowCollapsed() && (tp.fitHeight || !tp.detailsOpen))
                ImGui::SetWindowSize(ImVec2(fitW,
                    ImGui::GetCursorPosY() + style.WindowPadding.y - style.ItemSpacing.y));
            tp.fitHeight = false;
            ImGui::End();

            if (!windowOpen) {
                toRemove.push_back(tp.uid);
                rowCache.erase(tp.uid);
            }
        }

        if (!toRemove.empty()) {
            for (int uid : toRemove) {
                rowCache.erase(uid);
                featureCache.erase(uid);
            }
            plot->trackPopups.erase(
                std::remove_if(plot->trackPopups.begin(), plot->trackPopups.end(),
                               [&](const Manager::GwPlot::TrackPopup& p) {
                                   return std::find(toRemove.begin(), toRemove.end(),
                                                    p.uid) != toRemove.end();
                               }),
                plot->trackPopups.end());
        }
    }

// ─────────────────────────────────────────────────────────────────────────────
// Reference sequence popup
// ─────────────────────────────────────────────────────────────────────────────

// Displays the colored, wrapped reference sequence when the user clicks the
    // reference track (< 20 kb region).  Each wrapped line is rendered using the
    // same technique as the 'seq' row in drawImGuiInfoPopup: ANSI-colored text
    // painted directly to the draw list with a transparent InputText overlay so
    // the user can drag-select individual lines.  ImGuiListClipper keeps only
    // visible lines in flight so large sequences are cheap to render.

    void drawImGuiSeqPopup(Manager::GwPlot* plot) {
        if (plot->seqPopups.empty()) return;

        struct LineCache { std::string ansi; };
        struct RefCache {
            std::string              region;
            std::string              fasta;
            std::vector<LineCache>   lines;
            std::vector<std::string> plainLines;  // plain text per wrapped line
            int                      charsPerLine{0};
        };
        struct SelectionState {
            int  startLine{-1}, startCol{0};
            int  endLine{-1},   endCol{0};
            bool active{false};
            bool dragging{false};
        };
        static std::unordered_map<int, RefCache>       cache;
        static std::unordered_map<int, SelectionState> selStates;

        auto& style    = ImGui::GetStyle();
        ImFont* font   = plot->monoFont ? plot->monoFont : ImGui::GetFont();
        const float maxSeqH = 400.f;

        std::vector<int> toRemove;

        for (auto& rp : plot->seqPopups) {
            auto& rc  = cache[rp.uid];
            auto& sel = selStates[rp.uid];

            const bool isAA = (rp.kind == Manager::GwPlot::SeqPopup::AminoAcid);
            const char* winSuffix = isAA ? "##aapopup" : "##refpopup";
            const char* defaultTitle = isAA ? "Amino Acids" : "Sequence";

            // Helpers for the few places where reference vs amino-acid popups differ.
            auto parseRefRegion = [&](int& regionStart, std::string& chrom) {
                regionStart = 0;
                chrom.clear();
                size_t colonPos = rc.region.find(':');
                if (colonPos != std::string::npos) {
                    chrom = rc.region.substr(0, colonPos);
                    size_t dashPos = rc.region.find('-', colonPos);
                    if (dashPos != std::string::npos) {
                        try {
                            regionStart = std::stoi(rc.region.substr(colonPos + 1, dashPos - colonPos - 1));
                        } catch (...) {}
                    }
                }
            };
            auto formatHeader = [&](int selStart, int selEnd) -> std::string {
                if (!isAA) {
                    std::string chrom;
                    int regionStart = 0;
                    parseRefRegion(regionStart, chrom);
                    if (!chrom.empty()) {
                        return ">" + chrom + ":" + std::to_string(regionStart + selStart) + "-" + std::to_string(regionStart + selEnd);
                    }
                    return ">" + rc.region + ":" + std::to_string(selStart) + "-" + std::to_string(selEnd);
                }
                return ">" + rc.region + ":" + std::to_string(selStart) + "-" + std::to_string(selEnd);
            };
            auto formatTooltip = [&](int offset) -> std::string {
                if (!isAA) {
                    std::string chrom;
                    int regionStart = 0;
                    parseRefRegion(regionStart, chrom);
                    if (!chrom.empty()) {
                        return chrom + ":" + std::to_string(regionStart + offset);
                    }
                    return "offset " + std::to_string(offset);
                }
                return "codon " + std::to_string(offset) + "  (" + std::to_string(offset * 3) + " bp)";
            };

            // ── Open the window first so we can measure the actual content width ──
            {
                float defW = 380.f + style.WindowPadding.x * 2;
                float defH = ImGui::GetFrameHeight() + style.WindowPadding.y * 2 + maxSeqH;
                auto& io   = ImGui::GetIO();
                float defX = std::min(rp.x, io.DisplaySize.x - defW);
                float defY = std::min(rp.y, io.DisplaySize.y - defH);
                if (defX < 0) defX = 0;
                if (defY < 0) defY = 0;
                ImGui::SetNextWindowPos(ImVec2(defX, defY), ImGuiCond_Appearing);
                ImGui::SetNextWindowSize(ImVec2(defW, defH), ImGuiCond_Appearing);
                ImGui::SetNextWindowSizeConstraints(ImVec2(200, 100), ImVec2(FLT_MAX, FLT_MAX));
            }
            bool windowOpen = true;
            // Keep the window title stable so ImGui doesn't recreate the window
            // every frame (which would reset focus and break text selection).
            std::string winTitle = (rc.region.empty() ? defaultTitle : rc.region)
                                 + winSuffix + std::to_string(rp.uid);
            ImGui::Begin(winTitle.c_str(), &windowOpen,
                         ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);
            closeOnEscape(&windowOpen);

            // ── Clipboard icon in title bar ───────────────────────────────────
            {
                float titleBarH = ImGui::GetFrameHeight();
                float iconSide  = titleBarH - 4.f;
                ImVec2 winPos   = ImGui::GetWindowPos();
                float  winWidth = ImGui::GetWindowWidth();
                ImVec2 iconMin(winPos.x + winWidth - titleBarH * 2.f, winPos.y + 2.f);
                ImVec2 iconMax(iconMin.x + iconSide, iconMin.y + iconSide);

                ImVec2 mp  = ImGui::GetIO().MousePos;
                bool   hov = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && mp.x >= iconMin.x && mp.x <= iconMax.x
                          && mp.y >= iconMin.y && mp.y <= iconMax.y;

                PopupTitleIconClip titleIconClip;
                ImDrawList* fgDl = titleIconClip.drawList;
                if (hov) {
                    fgDl->AddRectFilled(iconMin, iconMax,
                                        ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                                        style.FrameRounding);
                    ImGui::SetTooltip(sel.active && sel.startLine >= 0
                                      ? "Copy selected sequence"
                                      : "Copy full sequence");
                }
                ImU32 iconCol = hov ? ImGui::GetColorU32(ImGuiCol_Text)
                                    : ImGui::GetColorU32(ImGuiCol_Text, 0.7f);
                drawClipboardIcon(fgDl, iconMin, iconMax, iconCol,
                                  iconStrokeThickness(plot->monitorScale));

                if (hov && ImGui::IsMouseClicked(0)) {
                    std::string seqText;
                    int selStart = 0, selEnd = 0;
                    if (sel.active && sel.startLine >= 0) {
                        int nSL = sel.startLine, nSC = sel.startCol;
                        int nEL = sel.endLine,   nEC = sel.endCol;
                        if (nSL > nEL || (nSL == nEL && nSC > nEC)) {
                            std::swap(nSL, nEL); std::swap(nSC, nEC);
                        }
                        for (int li = 0; li <= nEL && li < (int)rc.plainLines.size(); ++li) {
                            int lineLen = (int)rc.plainLines[li].size();
                            if (li < nSL) selStart += lineLen;
                            if (li == nSL) selStart += nSC;
                            if (li < nEL) selEnd += lineLen;
                            if (li == nEL) selEnd += nEC;
                        }
                        for (int li = nSL; li <= nEL && li < (int)rc.plainLines.size(); ++li) {
                            const auto& row = rc.plainLines[li];
                            int cs = std::clamp(li == nSL ? nSC : 0, 0, (int)row.size());
                            int ce = std::clamp(li == nEL ? nEC : (int)row.size(), 0, (int)row.size());
                            if (cs < ce) seqText += row.substr(cs, ce - cs);
                        }
                    } else {
                        selEnd = 0;
                        for (const auto& row : rc.plainLines) {
                            selEnd += (int)row.size();
                            seqText += row;
                        }
                    }
                    std::string fastaHeader = formatHeader(selStart, selEnd);
                    ImGui::SetClipboardText((fastaHeader + "\n" + seqText + "\n").c_str());
                }
            }

            // ── Dynamic title-bar text (drawn over the stable title) ──────────
            {
                std::string displayRegion = rc.region.empty() ? defaultTitle : rc.region;
                if (sel.active && sel.startLine >= 0) {
                    int nSL = sel.startLine, nSC = sel.startCol;
                    int nEL = sel.endLine,   nEC = sel.endCol;
                    if (nSL > nEL || (nSL == nEL && nSC > nEC)) {
                        std::swap(nSL, nEL); std::swap(nSC, nEC);
                    }
                    int selStart = 0, selEnd = 0;
                    for (int li = 0; li <= nEL && li < (int)rc.plainLines.size(); ++li) {
                        int lineLen = (int)rc.plainLines[li].size();
                        if (li < nSL) selStart += lineLen;
                        if (li == nSL) selStart += nSC;
                        if (li < nEL) selEnd += lineLen;
                        if (li == nEL) selEnd += nEC;
                    }
                    std::string header = formatHeader(selStart, selEnd);
                    if (!header.empty() && header[0] == '>') header.erase(header.begin());
                    displayRegion = header;
                }
                // Draw over the built-in title text so it updates every frame
                // without changing the window ID.
                float titleBarH = ImGui::GetFrameHeight();
                ImVec2 winPos   = ImGui::GetWindowPos();
                float padL      = style.WindowPadding.x;
                float textY     = winPos.y + (titleBarH - ImGui::GetTextLineHeight()) * 0.5f;
                ImDrawList* dl  = ImGui::GetWindowDrawList();
                ImU32 titleCol  = ImGui::GetColorU32(ImGuiCol_Text);
                // Cover the original title with the window background colour
                dl->AddRectFilled(
                    ImVec2(winPos.x + padL, winPos.y + 1),
                    ImVec2(winPos.x + padL + ImGui::CalcTextSize(winTitle.c_str()).x + 20, winPos.y + titleBarH - 1),
                    ImGui::GetColorU32(ImGuiCol_TitleBgActive));
                dl->AddText(ImVec2(winPos.x + padL, textY), titleCol, displayRegion.c_str());
            }

            // Block key events (e.g. Cmd+C) from reaching GwPlot while this window is focused
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
                ImGui::GetIO().WantCaptureKeyboard = true;

            // Ensure region name is available for a stable window title from frame 1
            if (rc.region.empty()) {
                size_t hdr = rp.ansi.find('>');
                if (hdr != std::string::npos) {
                    size_t eol = rp.ansi.find('\n', hdr);
                    if (eol != std::string::npos) {
                        rc.region = rp.ansi.substr(hdr + 1, eol - hdr - 1);
                        while (!rc.region.empty() && (rc.region.back() == '\r' || rc.region.back() == ' '))
                            rc.region.pop_back();
                    }
                }
            }

            // ── Scrollable sequence area ───────────────────────────────────────
            float seqH = std::min(maxSeqH, ImGui::GetContentRegionAvail().y);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
            ImGui::BeginChild("##refseq", ImVec2(0, seqH), ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav |
                              ImGuiWindowFlags_AlwaysVerticalScrollbar);
            ImGui::PopStyleVar();
            // Zero item spacing so each Dummy advances the cursor by exactly rowH,
            // keeping the clipper item height and the hit-test divisor in sync.
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
            // Push the monospace font so the sequence text is perfectly uniform.
            ImGui::PushFont(font);

            // Compute char width and row height using the ACTUAL font size that
            // ImGui will use for rendering (GetFontSize() after PushFont).
            // Measuring a longer run and averaging irons out subpixel rounding.
            float fontSize = ImGui::GetFontSize();
            float charW    = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.f, "NNNNNNNNNN").x / 10.0f;
            const float rowH = ImGui::GetTextLineHeight() + style.FramePadding.y * 2;

            // Measure actual child content width.  GetContentRegionAvail().x
            // already accounts for a vertical scrollbar when one is present,
            // so we must NOT subtract ScrollbarSize again.
            float contentW = ImGui::GetContentRegionAvail().x;
            float availW   = contentW;
            int newCharsPerLine = std::max(1, (int)(availW / charW));
            // Tighten: measure a probe line and greedily add chars while they fit.
            {
                std::string probe(newCharsPerLine, 'N');
                float probeW = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.f,
                                                                probe.c_str(),
                                                                probe.c_str() + probe.size()).x;
                while (probeW + charW <= availW) {
                    ++newCharsPerLine;
                    probeW += charW;
                }
            }

            // ── Build (or rebuild) line cache ──────────────────────────────────
            if (rc.charsPerLine != newCharsPerLine || rc.lines.empty()) {
                rc.charsPerLine = newCharsPerLine;
                rc.lines.clear();
                rc.plainLines.clear();

                std::string ansiSeq, plainSeq;
                {
                    size_t hdr = rp.ansi.find('>');
                    if (hdr == std::string::npos) {
                        ImGui::PopFont();
                        ImGui::PopStyleVar(); // ItemSpacing
                        ImGui::EndChild();
                        ImGui::End();
                        toRemove.push_back(rp.uid);
                        continue;
                    }
                    size_t eol = rp.ansi.find('\n', hdr);
                    if (eol == std::string::npos) {
                        ImGui::PopFont();
                        ImGui::PopStyleVar(); // ItemSpacing
                        ImGui::EndChild();
                        ImGui::End();
                        toRemove.push_back(rp.uid);
                        continue;
                    }
                    rc.region = rp.ansi.substr(hdr + 1, eol - hdr - 1);
                    while (!rc.region.empty() && (rc.region.back() == '\r' || rc.region.back() == ' '))
                        rc.region.pop_back();
                    ansiSeq = rp.ansi.substr(eol + 1);
                    size_t s = ansiSeq.find_first_not_of("\n\r ");
                    if (s != std::string::npos) ansiSeq = ansiSeq.substr(s);
                    size_t e = ansiSeq.find_last_not_of("\n\r ");
                    if (e != std::string::npos) ansiSeq = ansiSeq.substr(0, e + 1);
                    plainSeq = stripAnsiCodes(ansiSeq);
                }
                {
                    std::string upper;
                    upper.reserve(plainSeq.size());
                    for (unsigned char c : plainSeq) upper += (char)std::toupper(c);
                    rc.fasta = ">" + rc.region + "\n" + upper + "\n";
                }
                {
                    std::string curAnsi, curPlain;
                    int vis = 0;
                    for (size_t i = 0; i < ansiSeq.size(); ) {
                        if (ansiSeq[i] == '\033' && i + 1 < ansiSeq.size() && ansiSeq[i+1] == '[') {
                            size_t j = i + 2;
                            while (j < ansiSeq.size() && ansiSeq[j] != 'm') ++j;
                            size_t len = (j < ansiSeq.size()) ? j - i + 1 : j - i;
                            curAnsi += ansiSeq.substr(i, len);
                            i += len;
                        } else if (ansiSeq[i] == '\n' || ansiSeq[i] == '\r') {
                            ++i;
                        } else {
                            char uc = (char)std::toupper((unsigned char)ansiSeq[i]);
                            curAnsi += uc; curPlain += uc;
                            ++i; ++vis;
                            if (vis >= newCharsPerLine) {
                                rc.lines.push_back({std::move(curAnsi)});
                                rc.plainLines.push_back({std::move(curPlain)});
                                curAnsi.clear(); curPlain.clear(); vis = 0;
                            }
                        }
                    }
                    if (!curAnsi.empty()) {
                        rc.lines.push_back({std::move(curAnsi)});
                        rc.plainLines.push_back({std::move(curPlain)});
                    }
                }
                sel = SelectionState{};  // clear stale selection after reflow
            }

            if (rc.lines.empty()) {
                ImGui::PopFont();
                ImGui::PopStyleVar(); // ItemSpacing
                ImGui::EndChild();
                ImGui::End();
                toRemove.push_back(rp.uid);
                continue;
            }

            int numLines = (int)rc.lines.size();

            // With a monospace font the pixel width of a line is simply
            // chars * charW — no per-line recalibration is needed.
            float textW = rc.charsPerLine * charW;

            ImVec2 childPos = ImGui::GetWindowPos();
            float  scrollY  = ImGui::GetScrollY();
            ImDrawList* dl  = ImGui::GetWindowDrawList();

            // Capture the actual x-origin where text is drawn (may differ from childPos.x
            // by a few pixels due to borders / internal ImGui spacing).
            float textOriginX = ImGui::GetCursorScreenPos().x;

            // ── Mouse hit detection: screen → (line, col) ─────────────────────
            auto hitTest = [&]() -> std::pair<int,int> {
                ImVec2 mp  = ImGui::GetMousePos();
                float relY = (mp.y - childPos.y) + scrollY;
                float relX = mp.x - textOriginX;
                int line = std::clamp((int)(relY / rowH), 0, numLines - 1);
                int col  = std::clamp((int)std::floor(relX / charW), 0,
                                      (int)rc.plainLines[line].size());
                return {line, col};
            };

            bool hovered = ImGui::IsWindowHovered();
            if (hovered && ImGui::IsMouseClicked(0)) {
                auto [line, col] = hitTest();
                sel = {line, col, line, col, true, true};
            }
            if (sel.dragging && ImGui::IsMouseDown(0)) {
                auto [line, col] = hitTest();
                sel.endLine = line; sel.endCol = col;
            }
            if (sel.dragging && ImGui::IsMouseReleased(0)) {
                sel.dragging = false;
            }

            // ── Hover tooltip showing genomic position ────────────────────────
            if (hovered) {
                auto [line, col] = hitTest();
                int offset = 0;
                for (int li = 0; li < line && li < (int)rc.plainLines.size(); ++li)
                    offset += (int)rc.plainLines[li].size();
                offset += col;

                ImGui::SetTooltip("%s", formatTooltip(offset).c_str());
            }

            // ── Keyboard copy (Ctrl+C / Cmd+C) ────────────────────────────────
            // Check focus on the parent popup (not just the child) so the
            // shortcut works even when the child scrollbar is interacted with.
            if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
                ImGuiIO& io = ImGui::GetIO();
                bool copyPressed = (io.KeyCtrl || io.KeySuper) && ImGui::IsKeyPressed(ImGuiKey_C, false);
                if (copyPressed && sel.active && sel.startLine >= 0) {
                    int nSL = sel.startLine, nSC = sel.startCol;
                    int nEL = sel.endLine,   nEC = sel.endCol;
                    if (nSL > nEL || (nSL == nEL && nSC > nEC)) {
                        std::swap(nSL, nEL); std::swap(nSC, nEC);
                    }
                    std::string selText;
                    for (int li = nSL; li <= nEL && li < (int)rc.plainLines.size(); ++li) {
                        const auto& row = rc.plainLines[li];
                        int cs = std::clamp(li == nSL ? nSC : 0, 0, (int)row.size());
                        int ce = std::clamp(li == nEL ? nEC : (int)row.size(), 0, (int)row.size());
                        if (cs < ce) selText += row.substr(cs, ce - cs);
                        if (li < nEL) selText += '\n';
                    }
                    if (!selText.empty())
                        ImGui::SetClipboardText(selText.c_str());
                }
            }

            // ── Normalised selection bounds for rendering ─────────────────────
            int normSL = -1, normSC = 0, normEL = -1, normEC = 0;
            if (sel.active && sel.startLine >= 0) {
                normSL = sel.startLine; normSC = sel.startCol;
                normEL = sel.endLine;   normEC = sel.endCol;
                if (normSL > normEL || (normSL == normEL && normSC > normEC)) {
                    std::swap(normSL, normEL); std::swap(normSC, normEC);
                }
            }

            // ── Per-row rendering via list clipper ────────────────────────────
            ImGuiListClipper clipper;
            clipper.Begin(numLines, rowH);
            while (clipper.Step()) {
                for (int li = clipper.DisplayStart; li < clipper.DisplayEnd; ++li) {
                    ImVec2 cursor = ImGui::GetCursorScreenPos();

                    // Selection highlight behind text
                    if (normSL >= 0 && li >= normSL && li <= normEL) {
                        int cs = (li == normSL) ? normSC : 0;
                        int ce = (li == normEL) ? normEC : (int)rc.plainLines[li].size();
                        if (cs < ce) {
                            dl->AddRectFilled(
                                ImVec2(cursor.x + cs * charW, cursor.y),
                                ImVec2(cursor.x + ce * charW, cursor.y + rowH),
                                ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
                        }
                    }

                    // ANSI colored text
                    ImVec2 clipMin(childPos.x, cursor.y);
                    ImVec2 clipMax(childPos.x + textW, cursor.y + rowH);
                    drawAnsiTextAtPos(dl,
                        ImVec2(cursor.x, cursor.y + style.FramePadding.y),
                        rc.lines[li].ansi, clipMin, clipMax);

                    ImGui::Dummy(ImVec2(textW, rowH));
                }
            }
            clipper.End();
            ImGui::PopFont();
            ImGui::PopStyleVar();  // ItemSpacing
            ImGui::EndChild();
            ImGui::End();

            if (!windowOpen) {
                toRemove.push_back(rp.uid);
                cache.erase(rp.uid);
                selStates.erase(rp.uid);
            }
        }

        if (!toRemove.empty()) {
            plot->seqPopups.erase(
                std::remove_if(plot->seqPopups.begin(), plot->seqPopups.end(),
                               [&](const Manager::GwPlot::SeqPopup& p) {
                                   return std::find(toRemove.begin(), toRemove.end(),
                                                    p.uid) != toRemove.end();
                               }),
                plot->seqPopups.end());
        }
    }

    void drawImGuiCommandStatus(Manager::GwPlot* plot) {
        if (!plot->showCommandStatus) return;
        if (plot->lastCommandOutputAnsi.empty()) {
            plot->showCommandStatus = false;
            return;
        }

        static std::string displayedText;
        static std::vector<char> textBuf;  // InputText needs a writable buffer
        static long displayedFrame = -1;
        static bool scrollToBottom = false;
        static bool contentJustChanged = false;
        static float prevScrollY = 0.0f;
        static float prevScrollMaxY = 0.0f;

        if (displayedFrame != plot->lastCommandOutputFrame) {
            displayedFrame = plot->lastCommandOutputFrame;
            displayedText = stripAnsiCodes(plot->lastCommandOutputAnsi);
            textBuf.assign(displayedText.begin(), displayedText.end());
            textBuf.push_back('\0');
            // Only auto-scroll if the user was already at the bottom.
            if (prevScrollMaxY <= 0.0f || prevScrollY >= prevScrollMaxY - 2.0f) {
                scrollToBottom = true;
            }
            contentJustChanged = true;
        }

        if (displayedText.empty()) {
            plot->showCommandStatus = false;
            return;
        }

        auto clearHistory = [&]() {
            plot->lastCommandOutputAnsi.clear();
            displayedText.clear();
            textBuf.assign(1, '\0');
            plot->lastCommandOutputFrame = -1;
            displayedFrame = -1;
            plot->showCommandStatus = false;
        };

        auto drawTrashIcon = [](ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col, float thickness) {
            float w = max.x - min.x;
            float h = max.y - min.y;
            float padX = w * 0.18f;
            float padY = h * 0.10f;
            float left = min.x + padX;
            float right = max.x - padX;
            float bodyTop = min.y + h * 0.30f;
            float bodyBottom = max.y - padY;
            float rounding = w * 0.06f;

            // Body
            dl->AddRect(ImVec2(left, bodyTop),
                        ImVec2(right, bodyBottom),
                        col, rounding, 0, thickness);

            // Lid
            float lidY = bodyTop - h * 0.06f;
            dl->AddLine(ImVec2(left - w * 0.04f, lidY),
                        ImVec2(right + w * 0.04f, lidY), col, thickness);

            // Handle
            float handleW = w * 0.30f;
            float handleH = h * 0.16f;
            float hx = (min.x + max.x) * 0.5f - handleW * 0.5f;
            float hy = min.y + padY;
            dl->AddRect(ImVec2(hx, hy),
                        ImVec2(hx + handleW, hy + handleH),
                        col, 0, 0, thickness);

            // Vertical striations
            dl->AddLine(ImVec2(left + w * 0.25f, bodyTop + h * 0.12f),
                        ImVec2(left + w * 0.25f, bodyBottom - h * 0.12f), col, thickness);
            dl->AddLine(ImVec2(right - w * 0.25f, bodyTop + h * 0.12f),
                        ImVec2(right - w * 0.25f, bodyBottom - h * 0.12f), col, thickness);
        };

        auto& io = ImGui::GetIO();
        auto& style = ImGui::GetStyle();
        // Output appears independently in the bottom-right corner.
        float pad = 10.0f;
        const float maxW = std::max(1.f, io.DisplaySize.x - pad * 2);
        const float maxH = std::max(1.f, io.DisplaySize.y - pad * 2);
        const float minW = std::min(320.f, maxW);
        const float minH = std::min(80.f, maxH);
        // Never give ImGui a minimum greater than the maximum. On narrow
        // canvases that made the Output window alternate widths every frame.
        const float limitW = std::min(maxW, std::max(minW, io.DisplaySize.x * 0.5f));
        const float limitH = std::min(maxH, std::max(minH, io.DisplaySize.y * 0.5f));
        float winW = std::min(480.f, limitW);
        float winH = std::min(400.f, limitH);
        ImVec2 pos(io.DisplaySize.x - pad, io.DisplaySize.y - pad);
        ImGui::SetNextWindowPos(pos, ImGuiCond_Appearing, ImVec2(1, 1));
        ImGui::SetNextWindowBgAlpha(plot->remoteCommand ? 1.0f : 0.85f);
        ImGui::SetNextWindowSize(ImVec2(winW, winH), ImGuiCond_Appearing);
        ImGui::SetNextWindowSizeConstraints(
            ImVec2(minW, minH),
            ImVec2(limitW, limitH));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoNav
                               | ImGuiWindowFlags_NoSavedSettings;
        bool visible = ImGui::Begin("Output", &plot->showCommandStatus, flags);
        if (closeOnEscape(&plot->showCommandStatus)) plot->redraw = true;
        if (visible) {
            // ── Title-bar icons (copy left of trash, trash left of close) ─────
            {
                float titleBarH = ImGui::GetFrameHeight();
                float iconSide = titleBarH - 6.0f;
                ImVec2 winPos = ImGui::GetWindowPos();
                float winWidth = ImGui::GetWindowWidth();
                float rightInset = 2.0f;
                float topInset = 3.0f;
                PopupTitleIconClip titleIconClip;
                ImDrawList* fgDl = titleIconClip.drawList;
                ImVec2 mp = io.MousePos;
                float iconStroke = iconStrokeThickness(plot->monitorScale);

                auto drawTitleIcon = [&](float slotFromRight, const char* tooltip,
                                         const auto& iconDrawer, auto&& onClick) {
                    float minX = winPos.x + winWidth - titleBarH * (1.0f + slotFromRight) - rightInset;
                    ImVec2 iconMin(minX, winPos.y + topInset);
                    ImVec2 iconMax(iconMin.x + iconSide, iconMin.y + iconSide);
                    bool hov = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && mp.x >= iconMin.x && mp.x <= iconMax.x
                            && mp.y >= iconMin.y && mp.y <= iconMax.y;
                    if (hov) {
                        fgDl->AddRectFilled(iconMin, iconMax,
                                            ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                                            style.FrameRounding);
                        ImGui::SetTooltip("%s", tooltip);
                    }
                    ImU32 col = hov ? ImGui::GetColorU32(ImGuiCol_Text)
                                    : ImGui::GetColorU32(ImGuiCol_Text, 0.7f);
                    iconDrawer(fgDl, iconMin, iconMax, col, iconStroke);
                    if (hov && ImGui::IsMouseClicked(0)) {
                        onClick();
                    }
                };

                drawTitleIcon(2.0f, "Copy output", drawClipboardIcon, [&]() {
                    ImGui::SetClipboardText(displayedText.c_str());
                });

                drawTitleIcon(1.0f, "Clear output history", drawTrashIcon, [&]() {
                    clearHistory();
                });
            }

            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImVec2 size(avail.x, std::max(40.0f, avail.y));

            ImFont* font = plot->monoFont ? plot->monoFont : ImGui::GetFont();
            ImGui::PushFont(font);

            // One read-only, word-wrapped text box filling the window: selectable with the
            // mouse, and its own scrollbar is the only one
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            ImGui::InputTextMultiline("##cmdoutput", textBuf.data(), textBuf.size(), size,
                                      ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            auto& children = ImGui::GetCurrentWindow()->DC.ChildWindows;
            ImGuiWindow* scrollChild = children.empty() ? nullptr : children.back();

            if (scrollChild) {
                if (scrollToBottom) {
                    ImGui::SetScrollY(scrollChild, scrollChild->ScrollMax.y);
                    if (!contentJustChanged &&
                        scrollChild->Scroll.y >= scrollChild->ScrollMax.y - 2.0f) {
                        scrollToBottom = false;
                    }
                }
                contentJustChanged = false;
                prevScrollY = scrollChild->Scroll.y;
                prevScrollMaxY = scrollChild->ScrollMax.y;
            } else {
                contentJustChanged = false;
            }

            ImGui::PopFont();
        }
        ImGui::End();
    }

} // namespace Menu
