// imgui_label_table.cpp
// Label progress table and variant label management UI
// Extracted from menu.cpp with no functional changes.

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>

#include "menu.h"
#include "plot_manager.h"
#include "plot_commands.h"
#include "utils.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imfilebrowser.h"

namespace Menu {

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static bool parsedRegionQuery(const std::string& text,
                              Utils::Region& region)
{
    if (text.empty())
        return false;

    try {
        std::string q = text;
        region = Utils::parseRegion(q);
        return q == region.chrom ||
               Utils::startsWith(q, region.chrom + ":");
    }
    catch (...) {
        return false;
    }
}

static bool labelOverlapsRegion(const std::vector<Utils::Region>& regions,
                                const Utils::Label& lbl,
                                const Utils::Region& query)
{
    if (query.chrom.empty())
        return false;

    if (regions.empty()) {
        return lbl.chrom == query.chrom &&
               Utils::isOverlapping(
                   (uint32_t)lbl.pos,
                   (uint32_t)lbl.pos,
                   (uint32_t)query.start,
                   (uint32_t)query.end);
    }

    for (const auto& region : regions) {
        if (region.chrom != query.chrom)
            continue;

        int left = std::min(region.start, region.end);
        int right = std::max(region.start, region.end);

        if (Utils::isOverlapping(
                (uint32_t)left,
                (uint32_t)right,
                (uint32_t)query.start,
                (uint32_t)query.end))
            return true;
    }

    return false;
}

static std::string lowerCopy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                    [](unsigned char c) { return (char)std::tolower(c); });
    return value;
}

static std::vector<RowData> parseVariantDetailRows(HGW::GwVariantTrack& vt, Utils::Label& lbl, int selectedSampleIdx) {
        auto makeRow = [](const std::string& label, const std::string& plain) -> RowData {
            RowData rd{label, plain, plain, false, {}};
            rd.inputBuf.assign(plain.begin(), plain.end());
            rd.inputBuf.push_back('\0');
            return rd;
        };
        auto makeNavRow = [&](const std::string& label, const std::string& plain, const std::string& chrom, int pos) -> RowData {
            RowData rd = makeRow(label, plain);
            rd.navigable = !chrom.empty() && pos > 0;
            rd.navChrom = chrom;
            rd.navPos = pos;
            return rd;
        };

        std::vector<RowData> rows;
        rows.push_back(makeNavRow("Position", lbl.chrom + ":" + std::to_string(lbl.pos), lbl.chrom, lbl.pos));
        if (!lbl.variantId.empty()) rows.push_back(makeRow("ID", lbl.variantId));
        if (!lbl.vartype.empty()) rows.push_back(makeRow("Type", lbl.vartype));
        rows.push_back(makeRow("Label", lbl.current()));  // filtered out in detail view
        if (!lbl.savedDate.empty()) rows.push_back(makeRow("Date", lbl.savedDate));
        if (!lbl.comment.empty()) rows.push_back(makeRow("Comment", lbl.comment));
        bool idAlreadyAdded = !lbl.variantId.empty();

        std::string variantString;
        std::vector<std::string> sampleNames;
        bool isVcf = (vt.type == HGW::TrackType::VCF);
        if (isVcf) {
            vt.vcf.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
            variantString = vt.vcf.variantString;
            vt.vcf.get_samples();
            sampleNames = vt.vcf.sample_names;
        } else {
            vt.variantTrack.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
            variantString = vt.variantTrack.variantString;
        }
        if (variantString.empty()) {
            return rows;
        }

        std::vector<std::string> fields = Utils::split(variantString, '\t');
        if (isVcf && fields.size() >= 8) {
            std::string endChrom;
            int endPos = 0;
            static const char* names[] = {"CHROM", "POS", "ID", "REF", "ALT", "QUAL", "FILTER"};
            for (int i = 0; i < 7 && i < (int)fields.size(); ++i) {
                if (i == 2 && idAlreadyAdded) continue; // ID already added from lbl.variantId
                if (!fields[i].empty()) rows.push_back(makeRow(names[i], fields[i]));
            }
            if (fields.size() > 7 && !fields[7].empty() && fields[7] != ".") {
                // INFO section header
                RowData infoHdr = makeRow("INFO", "");
                infoHdr.isSection = true;
                infoHdr.sectionKey = "INFO";
                rows.push_back(infoHdr);
                for (const auto& item : Utils::split(fields[7], ';')) {
                    if (item.empty()) continue;
                    auto eq = item.find('=');
                    if (eq == std::string::npos) {
                        rows.push_back(makeRow("INFO", item));
                    } else {
                        std::string key = item.substr(0, eq);
                        std::string value = item.substr(eq + 1);
                        if (key == "CHROM" || key == "CHR2") { // override with CHR2 is found
                            endChrom = value;
                        } else if (key == "END" || key == "CHR2_POS" ) { // CHR2_POS overrides
                            try {
                                endPos = std::stoi(value);
                            } catch (...) {
                            }
                        }
                        rows.push_back(makeRow(key, value));
                    }
                }
            }
            if (endChrom.empty() && endPos > 0) {
                endChrom = lbl.chrom; // Fallback to current chromosome if missing CHR2
            }
            if (!endChrom.empty() && endPos > 0) {
                rows.insert(rows.begin() + 1, makeNavRow("End", endChrom + ":" + std::to_string(endPos), endChrom, endPos));
            }
            if (fields.size() > 8 && !fields[8].empty()) {
                // FORMAT section header (carries the sample dropdown)
                RowData fmtHdr = makeRow("FORMAT", fields[8]);
                fmtHdr.isSection = true;
                fmtHdr.sectionKey = "FORMAT";
                rows.push_back(fmtHdr);
                std::vector<std::string> fmtKeys = Utils::split(fields[8], ':');
                int nSamples = std::max(0, (int)fields.size() - 9);
                if (nSamples > 0) {
                    int sampleOffset = std::min(std::max(selectedSampleIdx, 0), nSamples - 1);
                    size_t sampleFieldIdx = 9 + (size_t)sampleOffset;
                    std::vector<std::string> sampleVals = Utils::split(fields[sampleFieldIdx], ':');
                    for (size_t k = 0; k < fmtKeys.size() && k < sampleVals.size(); ++k) {
                        rows.push_back(makeRow(fmtKeys[k], sampleVals[k]));
                    }
                }
            }
        } else if (fields.size() >= 3) {
            static const char* bedNames[] = {"CHROM", "START", "END", "NAME", "SCORE", "STRAND"};
            int maxFields = std::min((int)fields.size(), 6);
            for (int i = 0; i < maxFields; ++i) {
                if (!fields[i].empty()) rows.push_back(makeRow(bedNames[i], fields[i]));
            }
            for (int i = 6; i < (int)fields.size(); ++i) {
                rows.push_back(makeRow("FIELD" + std::to_string(i + 1), fields[i]));
            }
        }

        return rows;
    }

// Evaluate a simple VCF expression filter: "INFO.SU > 3 & FORMAT.PE >= 2"
// Returns true if the record passes (or if expr is empty / not parseable).
static bool evalVcfExpr(const std::vector<RowData>& rows, const std::string& expr) {
    if (expr.empty()) return true;

    // Split on '&' (we only support AND for now)
    auto clauses = Utils::split(expr, '&');
    for (const auto& raw : clauses) {
        std::string clause = raw;
        // trim
        while (!clause.empty() && clause.front() == ' ') clause = clause.substr(1);
        while (!clause.empty() && clause.back()  == ' ') clause.pop_back();
        if (clause.empty()) continue;

        // Parse: FIELD OP VALUE  where FIELD = "INFO.KEY" or "FORMAT.KEY" or plain key
        // Operators: >= <= > < == !=
        static const char* ops[] = {">=", "<=", "!=", ">", "<", "==", nullptr};
        std::string field, opStr, valStr;
        bool parsed = false;
        for (int oi = 0; ops[oi]; ++oi) {
            auto pos = clause.find(ops[oi]);
            if (pos != std::string::npos) {
                field  = clause.substr(0, pos);
                opStr  = ops[oi];
                valStr = clause.substr(pos + std::strlen(ops[oi]));
                while (!field.empty()  && field.back()  == ' ') field.pop_back();
                while (!valStr.empty() && valStr.front() == ' ') valStr = valStr.substr(1);
                while (!valStr.empty() && valStr.back()  == ' ') valStr.pop_back();
                parsed = true;
                break;
            }
        }
        if (!parsed || field.empty() || valStr.empty()) continue;

        // Resolve field: strip "INFO." or "FORMAT." prefix to get the bare key
        std::string key = field;
        if (key.size() > 5 && key.substr(0, 5) == "INFO.") {
            key = key.substr(5);
        } else if (key.size() > 7 && key.substr(0, 7) == "FORMAT.") {
            key = key.substr(7);
        }

        // Find value in rows (skip section headers)
        std::string rowVal;
        bool found = false;
        for (const auto& rd : rows) {
            if (rd.isSection) continue;
            if (rd.label == key) { rowVal = rd.plainValue; found = true; break; }
        }
        if (!found) return false; // field not present → filter out

        // Compare: try numeric first, fall back to string
        try {
            double rv = std::stod(rowVal);
            double ev = std::stod(valStr);
            bool ok = false;
            if (opStr == ">")  ok = rv >  ev;
            else if (opStr == "<")  ok = rv <  ev;
            else if (opStr == ">=") ok = rv >= ev;
            else if (opStr == "<=") ok = rv <= ev;
            else if (opStr == "==") ok = rv == ev;
            else if (opStr == "!=") ok = rv != ev;
            if (!ok) return false;
        } catch (...) {
            // string compare
            bool ok = false;
            if (opStr == "==") ok = rowVal == valStr;
            else if (opStr == "!=") ok = rowVal != valStr;
            else return false; // can't do < > on strings
            if (!ok) return false;
        }
    }
    return true;
}

// -------------------------------------------------------------------------    

void drawImGuiLabelTableDialog(Manager::GwPlot* plot, bool& redraw) {
        if (!plot->labelTableDialogOpen) return;
        if (plot->variantTracks.empty()) {
            plot->labelTableDialogOpen = false;
            return;
        }
        if (plot->variantFileSelection < 0 || plot->variantFileSelection >= (int)plot->variantTracks.size()) {
            plot->variantFileSelection = 0;
        }
        plot->currentVarTrack = &plot->variantTracks[plot->variantFileSelection];
        if (!plot->currentVarTrack) return;

        auto& vt = *plot->currentVarTrack;
        if (vt.multiLabels.empty()) return;

        static std::unordered_map<std::string, int> selectedByTrack;
        static std::unordered_map<std::string, int> sampleByTrack;
        static std::unordered_map<std::string, std::string> filterPendingByTrack; // text box buffer
        static std::unordered_map<std::string, std::string> filterCommittedByTrack; // committed on Apply
        static std::unordered_map<std::string, std::string> exprFilterByTrack;
        static std::unordered_map<std::string, std::string> exprFilterPendingByTrack;
        static std::unordered_map<std::string, std::vector<RowData>> detailCache;
        static std::unordered_map<std::string, bool> labelDetailsOpenByTrack;
        static std::unordered_map<std::string, std::string> commentEditByTrack;
        static std::unordered_map<std::string, int> labelDetailsRowByTrack;
        static std::unordered_map<std::string, std::string> detailFieldFilterByTrack;
        static std::unordered_map<std::string, std::string> detailFieldFilterPendingByTrack;
        static std::unordered_map<std::string, int> tileMarkerIndexByTrack; // set on row click, -1 = hidden
        static std::unordered_map<std::string, int> lastSyncedTileByTrack;  // detect external tile changes

        std::string trackKey = vt.fileName.empty() ? vt.path : vt.fileName;
        int& selectedRow = selectedByTrack[trackKey];
        int labelCount = (int)vt.multiLabels.size();

        bool isVcfTrack = (vt.type == HGW::TrackType::VCF);

        if (selectedRow < 0 || selectedRow >= labelCount) {
            selectedRow = std::min(std::max(vt.blockStart, 0), labelCount - 1);
        }

        int& selectedSampleIdx = sampleByTrack[trackKey];
        std::string& filterPending  = filterPendingByTrack[trackKey];
        std::string& filterText     = filterCommittedByTrack[trackKey];
        std::string& exprFilter     = exprFilterByTrack[trackKey];
        std::string& exprPending    = exprFilterPendingByTrack[trackKey];
        bool& labelDetailsOpen      = labelDetailsOpenByTrack[trackKey];
        std::string& commentEdit    = commentEditByTrack[trackKey];
        int& labelDetailsRow        = labelDetailsRowByTrack[trackKey];
        std::string& detailFieldFilter        = detailFieldFilterByTrack[trackKey];
        std::string& detailFieldFilterPending = detailFieldFilterPendingByTrack[trackKey];
        int& tileMarkerIndex        = tileMarkerIndexByTrack[trackKey];
        int& lastSyncedTile         = lastSyncedTileByTrack[trackKey];

        // Sync when an external tile selection changes (e.g. right-click on tile)
        if (plot->mode == Manager::Show::TILED && plot->mouseOverTileIndex != lastSyncedTile) {
            lastSyncedTile = plot->mouseOverTileIndex;
            int newRow = vt.blockStart + plot->mouseOverTileIndex;
            if (newRow >= 0 && newRow < labelCount) {
                selectedRow     = newRow;
                tileMarkerIndex = plot->mouseOverTileIndex;
            }
        }

        std::string trimmedFilter = filterText;
        Utils::trim(trimmedFilter);

        auto& labels = vt.multiLabels;

        ImGuiIO& io = ImGui::GetIO();
        float initW = std::min(280.0f, io.DisplaySize.x * 0.40f);
        // Leave room for top menu and bottom ideogram/slider
        float topOff    = plot->topMenuSpace / plot->monitorScale;
        float botOff    = plot->sliderSpace  / plot->monitorScale;
        float initH     = io.DisplaySize.y - topOff - botOff - 8.0f;
        float initX     = io.DisplaySize.x - initW - 4.0f;

        ImGui::SetNextWindowSize(ImVec2(initW, initH), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImVec2(initX, topOff + 4.0f), ImGuiCond_Appearing);

        bool windowOpen = true;
        if (ImGui::Begin("Variants", &windowOpen, ImGuiWindowFlags_NoFocusOnAppearing)) {

            // --- Filter row: text box + Apply button ---
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Filter");
                ImGui::SameLine();
                char filterBuf[256];
                std::snprintf(filterBuf, sizeof(filterBuf), "%s", filterPending.c_str());
                float applyW = ImGui::CalcTextSize("Apply").x + ImGui::GetStyle().FramePadding.x * 2 + 8.0f;
                ImGui::SetNextItemWidth(-applyW - ImGui::GetStyle().ItemSpacing.x);
                bool enterPressed = ImGui::InputTextWithHint("##label_filter",
                    "chr:start-end or text", filterBuf, sizeof(filterBuf),
                    ImGuiInputTextFlags_EnterReturnsTrue);
                filterPending = filterBuf;
                ImGui::SameLine();
                if (ImGui::Button("Apply##flt") || enterPressed) {
                    filterText = filterPending;
                    trimmedFilter = filterText;
                    Utils::trim(trimmedFilter);
                    // Trigger VCF scan to append any matching records not yet loaded
                    if (!trimmedFilter.empty()) {
                        vt.appendSearchMatches(trimmedFilter);
                        labelCount = (int)vt.multiLabels.size();
                    }
                }
            }

            // Expression filter row (VCF only)
            if (isVcfTrack) {
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted("Expr  ");
                ImGui::SameLine();
                char exprBuf[256];
                std::snprintf(exprBuf, sizeof(exprBuf), "%s", exprPending.c_str());
                float applyW = ImGui::CalcTextSize("Apply").x + ImGui::GetStyle().FramePadding.x * 2 + 8.0f;
                ImGui::SetNextItemWidth(-applyW - ImGui::GetStyle().ItemSpacing.x);
                bool exprEnter = ImGui::InputTextWithHint("##vcf_expr",
                    "INFO.SU > 3 & FORMAT.PE >= 2", exprBuf, sizeof(exprBuf),
                    ImGuiInputTextFlags_EnterReturnsTrue);
                exprPending = exprBuf;
                ImGui::SameLine();
                if (ImGui::Button("Apply##expr") || exprEnter) {
                    exprFilter = exprPending;
                }
            }

            ImGui::Separator();

            // Build visible-row list from committed filter only
            std::vector<int> visibleRows;
            Utils::Region filterRegion;
            bool useRegionFilter = parsedRegionQuery(trimmedFilter, filterRegion);
            std::string filterLower = lowerCopy(trimmedFilter);
            for (int i = 0; i < (int)labels.size(); ++i) {
                Utils::Label& lbl = labels[i];
                bool keep = trimmedFilter.empty();
                if (!keep && useRegionFilter) {
                    keep = (i < (int)vt.multiRegions.size())
                         ? labelOverlapsRegion(vt.multiRegions[i], lbl, filterRegion)
                         : labelOverlapsRegion({}, lbl, filterRegion);
                }
                if (!keep && !filterLower.empty()) {
                    std::string hay = lbl.chrom + ":" + std::to_string(lbl.pos) + " "
                                    + lbl.variantId + " " + lbl.current() + " "
                                    + lbl.comment + " " + lbl.vartype;
                    for (auto& c : hay) c = (char)std::tolower((unsigned char)c);
                    keep = hay.find(filterLower) != std::string::npos;
                }
                if (keep) visibleRows.push_back(i);
            }

            // --- Section 1: Master List ---
            float topH = ImGui::GetContentRegionAvail().y * 0.40f;
            if (ImGui::BeginChild("##labels_master", ImVec2(0, topH), false)) {
                // Dot column width: fits two small dots side-by-side
                float dotColW = ImGui::GetFrameHeight() * 1.4f;
                if (ImGui::BeginTable("##ltbl", 3,
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("##dots",     ImGuiTableColumnFlags_WidthFixed,   dotColW);
                    ImGui::TableSetupColumn("Position",   ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("Label",      ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();

                    // Auto-scroll to keep selectedRow in view
                    static int lastSelectedRow = -1;
                    bool needScroll = (selectedRow != lastSelectedRow);
                    lastSelectedRow = selectedRow;

                    ImDrawList* drawList = ImGui::GetWindowDrawList();
                    const ImU32 dotDateCol    = IM_COL32(100, 200, 100, 220); // green – has date
                    const ImU32 dotCommentCol = IM_COL32(100, 160, 240, 220); // blue  – has comment

                    for (int i : visibleRows) {
                        // Expression filter: need detail rows to evaluate
                        if (!exprFilter.empty() && isVcfTrack) {
                            Utils::Label& lbl2 = labels[i];
                            std::string ck = trackKey + "#" + std::to_string(i) + "#"
                                           + std::to_string(lbl2.i) + "#" + lbl2.savedDate
                                           + "#" + lbl2.comment + "#" + std::to_string(selectedSampleIdx);
                            if (detailCache.find(ck) == detailCache.end()) {
                                detailCache[ck] = parseVariantDetailRows(vt, lbl2, selectedSampleIdx);
                            }
                            if (!evalVcfExpr(detailCache[ck], exprFilter)) continue;
                        }

                        Utils::Label& lbl = labels[i];
                        bool isSelected  = (selectedRow == i);
                        ImGui::TableNextRow();
                        ImGui::PushID(i);

                        // ---- Column 0: dots / circle marker ----
                        ImGui::TableSetColumnIndex(0);

                        // Scroll this row into view when it becomes selected
                        if (isSelected && needScroll) {
                            ImGui::SetScrollHereY(0.5f);
                        }

                        bool hasDate    = !lbl.savedDate.empty();
                        bool hasComment = !lbl.comment.empty();
                        bool hasDots    = hasDate || hasComment;

                        {
                            ImVec2 cellPos  = ImGui::GetCursorScreenPos();
                            float  cellH    = ImGui::GetFrameHeight();
                            float  lineH    = ImGui::GetTextLineHeightWithSpacing();
                            float  r        = lineH * 0.22f;
                            float  cy       = cellPos.y + lineH * 0.5f;
                            float  xCursor  = cellPos.x + r + 2.0f;

                            if (hasDate) {
                                drawList->AddCircleFilled(ImVec2(xCursor, cy), r, dotDateCol);
                                xCursor += r * 2.0f + 2.0f;
                            }
                            if (hasComment) {
                                drawList->AddCircleFilled(ImVec2(xCursor, cy), r, dotCommentCol);
                            }

                            // Invisible button over the dot area — opens Label details for THIS row
                            if (hasDots) {
                                ImGui::SetCursorScreenPos(cellPos);
                                if (ImGui::InvisibleButton("##dotbtn", ImVec2(dotColW, cellH))) {
                                    labelDetailsRow  = i;
                                    labelDetailsOpen = true;
                                    commentEdit      = lbl.comment;
                                }
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
                                    std::string tip;
                                    if (hasDate)    tip += "Date: "    + lbl.savedDate + "\n";
                                    if (hasComment) tip += "Comment: " + lbl.comment;
                                    ImGui::SetTooltip("%s", tip.c_str());
                                }
                            } else {
                                ImGui::Dummy(ImVec2(dotColW, cellH));
                            }
                        }

                        // ---- Column 1: position (Selectable spans cols 1+2) ----
                        ImGui::TableSetColumnIndex(1);
                        std::string location = lbl.chrom + ":" + Utils::formatNum(lbl.pos);
                        if (ImGui::Selectable(location.c_str(), isSelected,
                                              ImGuiSelectableFlags_SpanAllColumns |
                                              ImGuiSelectableFlags_AllowOverlap)) {
                            selectedRow = i;
                            plot->mode = Manager::Show::TILED;
                            int tilesPerPage = plot->opts.number.x * plot->opts.number.y;
                            vt.blockStart = (i / tilesPerPage) * tilesPerPage;
                            plot->mouseOverTileIndex = i - vt.blockStart;
                            tileMarkerIndex = plot->mouseOverTileIndex; // only update on click
                            plot->redraw = true;
                            plot->processed = false;
                        }

                        // ---- Column 2: label text ----
                        ImGui::TableSetColumnIndex(2);
                        ImGui::TextUnformatted(lbl.labels.empty() ? "-" : lbl.current().c_str());

                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();

            // --- Tile marker: black filled circle at bottom-centre of the selected tile ---
            // Only shown when >1 tile visible, drawn behind UI via BackgroundDrawList,
            // and only updated on row click (tileMarkerIndex), not on mouse hover.
            {
                int nx = plot->opts.number.x;
                int ny = plot->opts.number.y;
                int totalTiles = nx * ny;
                if (plot->mode == Manager::Show::TILED &&
                    totalTiles > 1 &&
                    tileMarkerIndex >= 0 &&
                    tileMarkerIndex < totalTiles)
                {
                    float ms   = plot->monitorScale;
                    float fbW  = (float)plot->fb_width;
                    float fbH  = (float)plot->fb_height;
                    float padX = 6.0f * ms;
                    float padY = 6.0f * ms;
                    float cellW = fbW / (float)nx;
                    float cellH = (fbH - 6.0f * ms) / (float)ny;
                    int tx = tileMarkerIndex / ny; // column
                    int ty = tileMarkerIndex % ny; // row
                    float xStart = cellW * (float)tx + padX + padX * 0.5f;
                    float yEnd   = cellH * (float)ty + padY + cellH - padY;
                    float xEnd   = xStart + cellW - padX;
                    float cx = (xStart + xEnd) * 0.5f / ms;
                    float cy = yEnd / ms;
                    float r  = 6.0f;
                    // Use background draw list so the marker sits behind the ImGui windows
                    ImDrawList* bgDl = ImGui::GetBackgroundDrawList();
                    bgDl->AddCircleFilled(ImVec2(cx, cy), r, IM_COL32(0, 0, 0, 220));
                    bgDl->AddCircle(ImVec2(cx, cy), r, IM_COL32(255, 255, 255, 160), 0, 1.2f);
                }
            }

            // --- Label details popup (opened via dot clicks) ---
            if (labelDetailsRow < 0 || labelDetailsRow >= labelCount) labelDetailsRow = 0;
            if (labelDetailsOpen && labelDetailsRow >= 0 && labelDetailsRow < labelCount) {
                Utils::Label& dLbl = labels[labelDetailsRow];
                ImGui::SetNextWindowSize(ImVec2(300, 160), ImGuiCond_Appearing);
                bool detOpen = true;
                if (ImGui::Begin("Label details##popup", &detOpen,
                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextDisabled("Date:  "); ImGui::SameLine();
                    ImGui::TextUnformatted(dLbl.savedDate.empty() ? "-" : dLbl.savedDate.c_str());
                    ImGui::TextDisabled("Label: "); ImGui::SameLine();
                    ImGui::TextUnformatted(dLbl.labels.empty() ? "-" : dLbl.current().c_str());
                    ImGui::TextDisabled("Pos:   "); ImGui::SameLine();
                    ImGui::TextUnformatted((dLbl.chrom + ":" + std::to_string(dLbl.pos)).c_str());
                    ImGui::Separator();
                    ImGui::TextUnformatted("Comment:");
                    char cmtBuf[512];
                    std::snprintf(cmtBuf, sizeof(cmtBuf), "%s", commentEdit.c_str());
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputText("##cmt", cmtBuf, sizeof(cmtBuf))) {
                        commentEdit = cmtBuf;
                    }
                    if (ImGui::Button("Save comment")) {
                        dLbl.comment = commentEdit;
                        dLbl.savedDate = Utils::dateTime();
                        // Erase only the cache entry for this row so it rebuilds next frame
                        std::string evictKey = trackKey + "#" + std::to_string(labelDetailsRow) + "#";
                        for (auto it = detailCache.begin(); it != detailCache.end(); ) {
                            if (it->first.rfind(evictKey, 0) == 0)
                                it = detailCache.erase(it);
                            else
                                ++it;
                        }
                        plot->redraw = true;
                    }
                }
                ImGui::End();
                if (!detOpen) labelDetailsOpen = false;
            }

            ImGui::Separator();
            ImGui::TextDisabled("Variant Details");

            // Field filter (comma-separated keys, e.g. "ID, INFO.SU, FORMAT.PROB")
            {
                char fldBuf[256];
                std::snprintf(fldBuf, sizeof(fldBuf), "%s", detailFieldFilterPending.c_str());
                float applyW = ImGui::CalcTextSize("Apply").x + ImGui::GetStyle().FramePadding.x * 2 + 8.0f;
                ImGui::SetNextItemWidth(-applyW - ImGui::GetStyle().ItemSpacing.x);
                bool enter = ImGui::InputTextWithHint("##fld_filter",
                    "ID, SU, PROB", fldBuf, sizeof(fldBuf),
                    ImGuiInputTextFlags_EnterReturnsTrue);
                detailFieldFilterPending = fldBuf;
                ImGui::SameLine();
                if (ImGui::Button("Apply##fld") || enter)
                    detailFieldFilter = detailFieldFilterPending;
            }

            // --- Section 2: Details View ---
            if (ImGui::BeginChild("##labels_detail", ImVec2(0, 0), false)) {
                if (selectedRow >= 0 && selectedRow < (int)labels.size()) {
                    Utils::Label& lbl = labels[selectedRow];

                    std::string cacheKey = trackKey + "#" + std::to_string(selectedRow) + "#"
                                         + std::to_string(lbl.i) + "#" + lbl.savedDate
                                         + "#" + lbl.comment + "#" + std::to_string(selectedSampleIdx);
                    if (detailCache.find(cacheKey) == detailCache.end()) {
                        detailCache[cacheKey] = parseVariantDetailRows(vt, lbl, selectedSampleIdx);
                    }

                    auto& rows = detailCache[cacheKey];

                    // Build active field-filter set from comma-separated string
                    std::vector<std::string> fldTokens;
                    if (!detailFieldFilter.empty()) {
                        for (auto& tok : Utils::split(detailFieldFilter, ',')) {
                            std::string t = tok;
                            Utils::trim(t);
                            if (!t.empty()) fldTokens.push_back(lowerCopy(t));
                        }
                    }
                    // Rows that are always shown regardless of filter
                    auto isAlwaysShown = [](const RowData& rd) -> bool {
                        return rd.label == "Position" || rd.label == "End";
                    };
                    // "Label" row is never shown in the detail table
                    auto isNeverShown = [](const RowData& rd) -> bool {
                        return rd.label == "Label";
                    };
                    auto rowPassesFilter = [&](const RowData& rd) -> bool {
                        if (isNeverShown(rd)) return false;
                        if (isAlwaysShown(rd)) return true;
                        if (fldTokens.empty()) return true;
                        // Match against bare key, INFO.KEY, FORMAT.KEY
                        std::string bare = lowerCopy(rd.label);
                        std::string withInfo   = "info."   + bare;
                        std::string withFormat = "format." + bare;
                        for (const auto& tok : fldTokens) {
                            if (tok == bare || tok == withInfo || tok == withFormat) return true;
                        }
                        return false;
                    };

                    if (ImGui::BeginTable("##detailtbl", 2,
                            ImGuiTableFlags_SizingStretchProp)) {
                        ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableSetupColumn("Value");

                        for (size_t i = 0; i < rows.size(); ++i) {
                            auto& row = rows[i];

                            if (row.isSection) {
                                // Section header: italic dimmed text, no shading
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0);
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                                ImGui::TextUnformatted(row.sectionKey.c_str());
                                ImGui::PopStyleColor();

                                ImGui::TableSetColumnIndex(1);
                                if (row.sectionKey == "FORMAT" && isVcfTrack
                                        && !vt.vcf.sample_names.empty()) {
                                    vt.vcf.get_samples();
                                    int ns = (int)vt.vcf.sample_names.size();
                                    if (selectedSampleIdx < 0 || selectedSampleIdx >= ns)
                                        selectedSampleIdx = 0;
                                    ImGui::SetNextItemWidth(-FLT_MIN);
                                    if (ImGui::BeginCombo("##sample",
                                            vt.vcf.sample_names[selectedSampleIdx].c_str())) {
                                        for (int s = 0; s < ns; ++s) {
                                            if (ImGui::Selectable(vt.vcf.sample_names[s].c_str(),
                                                                  selectedSampleIdx == s)) {
                                                selectedSampleIdx = s;
                                                detailCache.erase(cacheKey);
                                            }
                                        }
                                        ImGui::EndCombo();
                                    }
                                } else {
                                    ImGui::TextDisabled("%s", row.plainValue.c_str());
                                }
                                continue;
                            }

                            if (!rowPassesFilter(row)) continue;

                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(row.label.c_str());

                            ImGui::TableSetColumnIndex(1);
                            if (row.navigable) {
                                if (ImGui::Button((row.plainValue + "##nav" + std::to_string(i)).c_str(),
                                                  ImVec2(-FLT_MIN, 0))) {
                                    plot->setVariantSite(row.navChrom, row.navPos,
                                                        row.navChrom, row.navPos);
                                    plot->mode = Manager::Show::SINGLE;
                                    plot->redraw = true;
                                    plot->processed = false;
                                    plot->fetchRefSeqs();
                                }
                            } else {
                                // Selectable text — copies to clipboard on click
                                ImGui::PushID((int)i + 10000);
                                ImGui::Selectable(row.plainValue.c_str(), false,
                                                  ImGuiSelectableFlags_None);
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                    ImGui::SetTooltip("Click to copy");
                                if (ImGui::IsItemClicked()) {
                                    ImGui::SetClipboardText(row.plainValue.c_str());
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::EndTable();
                    }
                }
            }
            ImGui::EndChild();
        }
        ImGui::End();

        if (!windowOpen) plot->labelTableDialogOpen = false;
    }



} // namespace Menu