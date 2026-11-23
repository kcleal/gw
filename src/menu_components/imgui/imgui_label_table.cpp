// imgui_label_table.cpp
// Label progress table and variant label management UI
// Extracted from menu.cpp with no functional changes.

#include <algorithm>
#include <cfloat>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "menu.h"
#include "plot_manager.h"
#include "plot_commands.h"
#include "utils.h"

#include "imgui.h"
#include "imgui_internal.h"
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

// Genotype bars in the variant list; colours and kinds are shared with VCF tracks.
static const char* const genotypeNames[] = {"missing", "hom ref", "het", "hom alt"};
#define GW_GT_COL(k) IM_COL32(Utils::GENOTYPE_RGBA[k][0], Utils::GENOTYPE_RGBA[k][1], Utils::GENOTYPE_RGBA[k][2], Utils::GENOTYPE_RGBA[k][3])
static const ImU32 genotypeColors[] = {GW_GT_COL(0), GW_GT_COL(1), GW_GT_COL(2), GW_GT_COL(3)};
#undef GW_GT_COL
static int genotypeKind(const char* gt, size_t length) { return Utils::genotypeKind(gt, length); }

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
            bool haveChr2Pos = false;
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
                rows.push_back(std::move(infoHdr));
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
                        } else if (key == "CHR2_POS") {  // overrides END, whichever comes first
                            try {
                                endPos = std::stoi(value);
                                haveChr2Pos = true;
                            } catch (...) {
                            }
                        } else if (key == "END" && !haveChr2Pos) {
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
            // A breakend's ALT names its mate, e.g. N]chr2:321682] or [chr13:123456[N. That is
            // the other end of the variant, whatever END says (END is usually POS for a BND).
            std::string mateChrom;
            int matePos = 0;
            for (const auto& alt : Utils::split(fields[4], ',')) {
                const size_t open = alt.find_first_of("[]");
                if (open == std::string::npos) continue;
                const size_t close = alt.find_first_of("[]", open + 1);
                if (close == std::string::npos) continue;
                const std::string locus = alt.substr(open + 1, close - open - 1);
                const size_t colon = locus.rfind(':');  // contig names may contain ':'
                if (colon == std::string::npos || colon == 0) continue;
                std::string chrom = locus.substr(0, colon);
                if (chrom.size() > 2 && chrom.front() == '<' && chrom.back() == '>') chrom = chrom.substr(1, chrom.size() - 2);
                char* numEnd = nullptr;
                const long pos = std::strtol(locus.c_str() + colon + 1, &numEnd, 10);
                if (*numEnd != '\0' || pos <= 0) continue;
                mateChrom = chrom;
                matePos = (int)pos;
                break;
            }
            // A second button is only useful when the other side is somewhere else: not for an
            // insertion or a breakend whose END is the record's own position (or the next base).
            const long vcfPos = std::strtol(fields[1].c_str(), nullptr, 10);
            auto elsewhere = [&](const std::string& chrom, int pos) {
                return !chrom.empty() && pos > 0 && (chrom != fields[0] || std::labs((long)pos - vcfPos) > 1);
            };
            if (elsewhere(mateChrom, matePos)) {
                rows.insert(rows.begin() + 1, makeNavRow("Mate", mateChrom + ":" + std::to_string(matePos), mateChrom, matePos));
            } else if (elsewhere(endChrom, endPos)) {
                rows.insert(rows.begin() + 1, makeNavRow("End", endChrom + ":" + std::to_string(endPos), endChrom, endPos));
            }
            if (fields.size() > 8 && !fields[8].empty()) {
                // FORMAT section header (carries the sample dropdown)
                RowData fmtHdr = makeRow("FORMAT", fields[8]);
                fmtHdr.isSection = true;
                fmtHdr.sectionKey = "FORMAT";
                rows.push_back(std::move(fmtHdr));
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

// File browser for the label file (the TSV written by --out-labels, as used by dysgu)
static ImGui::FileBrowser& labelFileBrowser() {
    static ImGui::FileBrowser browser(ImGuiFileBrowserFlags_EnterNewFilename | ImGuiFileBrowserFlags_CreateNewDir
                                      | ImGuiFileBrowserFlags_CloseOnEsc | ImGuiFileBrowserFlags_ConfirmOnEnter
                                      | ImGuiFileBrowserFlags_EditPathString | ImGuiFileBrowserFlags_SkipItemsCausingError);
    static bool initialised = false;
    if (!initialised) {
        browser.SetTypeFilters({".tsv", ".txt", ".*"});
        browser.AddStandardShortcuts();
        initialised = true;
    }
    return browser;
}

// A sheet of paper with a folded corner
static void drawFileIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col, float thickness) {
    const float w = max.x - min.x, h = max.y - min.y;
    const float l = min.x + w * 0.27f, r = min.x + w * 0.73f, t = min.y + h * 0.18f, b = min.y + h * 0.82f;
    const float fold = w * 0.2f;
    const ImVec2 pts[] = {{l, t}, {r - fold, t}, {r, t + fold}, {r, b}, {l, b}};
    dl->AddPolyline(pts, 5, col, ImDrawFlags_Closed, thickness);
    const ImVec2 corner[] = {{r - fold, t}, {r - fold, t + fold}, {r, t + fold}};
    dl->AddPolyline(corner, 3, col, 0, thickness);
}

// An italic "i"
static void drawInfoIcon(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col, float thickness) {
    const float w = max.x - min.x, h = max.y - min.y;
    const ImVec2 c(min.x + w * 0.5f, min.y + h * 0.5f);
    const float half = h * 0.27f;       // half the letter height
    const float slant = 0.22f;          // x shift per unit of height
    auto at = [&](float y) { return ImVec2(c.x - y * slant, c.y + y); };  // y runs from -half (top) to half
    const float t = thickness * 1.35f, serif = w * 0.09f;
    const ImVec2 top = at(-half * 0.28f), bottom = at(half);
    dl->AddLine(top, bottom, col, t);
    dl->AddLine(ImVec2(top.x - serif, top.y), top, col, t);                               // lead-in stroke
    dl->AddLine(bottom, ImVec2(bottom.x + serif, bottom.y), col, t);                      // tail
    dl->AddCircleFilled(at(-half * 0.88f), t * 0.75f, col, 10);
}

// The VCF header, split into the sections shown in the file info window
struct VcfHeaderView {
    struct Field { std::string id, number, type, description; };
    const bcf_hdr_t* hdr = nullptr;
    std::vector<std::pair<std::string, std::string>> general;  // ##key=value
    std::vector<Field> info, format, filter;
    std::vector<std::pair<std::string, std::string>> contigs;  // name, length
    std::vector<std::pair<std::string, std::string>> other;    // ##KEY=<...> lines such as ALT, SAMPLE
    std::string assembly;

    void build(const bcf_hdr_t* h) {
        *this = VcfHeaderView();
        hdr = h;
        if (!h) return;
        auto unquote = [](const char* v) {
            std::string out = v ? v : "";
            if (out.size() >= 2 && out.front() == '"' && out.back() == '"') out = out.substr(1, out.size() - 2);
            return out;
        };
        for (int i = 0; i < h->nhrec; ++i) {
            const bcf_hrec_t* rec = h->hrec[i];
            if (!rec || !rec->key) continue;
            auto get = [&](const char* key) -> std::string {
                for (int k = 0; k < rec->nkeys; ++k) {
                    if (rec->keys[k] && std::strcmp(rec->keys[k], key) == 0) return unquote(rec->vals[k]);
                }
                return "";
            };
            if (rec->type == BCF_HL_GEN) {
                general.emplace_back(rec->key, unquote(rec->value));
            } else if (rec->type == BCF_HL_INFO || rec->type == BCF_HL_FMT) {
                (rec->type == BCF_HL_INFO ? info : format).push_back({get("ID"), get("Number"), get("Type"), get("Description")});
            } else if (rec->type == BCF_HL_FLT) {
                filter.push_back({get("ID"), "", "", get("Description")});
            } else if (rec->type == BCF_HL_CTG) {
                contigs.emplace_back(get("ID"), get("length"));
                if (assembly.empty()) assembly = get("assembly");
            } else {
                std::string fields;
                for (int k = 0; k < rec->nkeys; ++k) {
                    if (!rec->keys[k] || std::strcmp(rec->keys[k], "IDX") == 0) continue;
                    if (!fields.empty()) fields += ", ";
                    fields += std::string(rec->keys[k]) + "=" + unquote(rec->vals[k]);
                }
                other.emplace_back(rec->key, fields);
            }
        }
    }
};

static std::string withThousands(long long n) {
    std::string digits = std::to_string(n), out;
    for (size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0 && digits[i - 1] != '-') out += ',';
        out += digits[i];
    }
    return out;
}

// File and label statistics for the variant file shown in the Variants window, with its VCF header
static void drawVariantFileInfoWindow(Manager::GwPlot* plot, HGW::GwVariantTrack& vt, bool& open) {
    if (!open) return;
    ImGui::SetNextWindowSize(ImVec2(560, 520), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(320, 200), ImVec2(FLT_MAX, FLT_MAX));
    ImGui::SetNextWindowBgAlpha(1.0f);  // tables are hard to read over the plot
    const bool visible = ImGui::Begin("Variant file info", &open, ImGuiWindowFlags_NoCollapse);
    closeOnEscape(&open);
    if (!visible) { ImGui::End(); return; }

    const bool isVcf = vt.type == HGW::TrackType::VCF;
    const bool isImages = vt.type == HGW::TrackType::IMAGES;
    const bool allRead = isVcf ? vt.vcf.done : vt.type == HGW::TrackType::GW_TRACK ? vt.variantTrack.done : true;
    const ImGuiTableFlags kvFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
    auto row = [](const char* key, const std::string& value) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", key);
        ImGui::TableSetColumnIndex(1);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(value.c_str());
        ImGui::PopTextWrapPos();
    };
    auto beginKeyValue = [&](const char* id) {
        if (!ImGui::BeginTable(id, 2, kvFlags)) return false;
        ImGui::TableSetupColumn("key", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
        return true;
    };
    // label -> count, in first-seen order
    auto addCount = [](std::vector<std::pair<std::string, int>>& counts, const std::string& key) {
        for (auto& c : counts) if (c.first == key) { c.second += 1; return; }
        counts.emplace_back(key, 1);
    };
    auto countTable = [&](const char* id, const char* heading, std::vector<std::pair<std::string, int>>& counts) {
        std::stable_sort(counts.begin(), counts.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV
                                      | ImGuiTableFlags_NoSavedSettings)) return;
        ImGui::TableSetupColumn(heading);
        ImGui::TableSetupColumn("Count");
        ImGui::TableHeadersRow();
        for (const auto& c : counts) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(c.first.empty() ? "-" : c.first.c_str());
            ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(withThousands(c.second).c_str());
        }
        ImGui::EndTable();
    };

    // ---- File ----
    ImGui::SeparatorText("File");
    if (beginKeyValue("##vfi_file")) {
        row("Path", vt.path);
        const char* format = isImages ? "Images" : !isVcf ? "Track file (BED / GFF / label table)"
                           : vt.vcf.fp && vt.vcf.fp->format.format == bcf ? "BCF"
                           : vt.vcf.fp && vt.vcf.fp->format.compression != no_compression ? "VCF (compressed)" : "VCF";
        row("Format", format);
        std::error_code ec;
        if (!isImages && std::filesystem::is_regular_file(vt.path, ec)) {
            const double bytes = (double)std::filesystem::file_size(vt.path, ec);
            if (!ec) {
                char buf[64];
                if (bytes >= 1e9) std::snprintf(buf, sizeof(buf), "%.2f GB", bytes / 1e9);
                else if (bytes >= 1e6) std::snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1e6);
                else std::snprintf(buf, sizeof(buf), "%.1f kB", bytes / 1e3);
                row("Size", buf);
            }
        }
        const std::string n = withThousands((long long)vt.multiLabels.size());
        row(isImages ? "Images" : "Variants", allRead ? n : n + " read so far (the file has more)");
        if (isVcf) {
            vt.vcf.get_samples();
            const auto& names = vt.vcf.sample_names;
            std::string samples = withThousands((long long)names.size());
            if (!names.empty()) {
                samples += "  (";
                for (size_t i = 0; i < names.size() && i < 6; ++i) samples += (i ? ", " : "") + names[i];
                if (names.size() > 6) samples += ", ...";
                samples += ")";
            }
            row("Samples", samples);
        }
        ImGui::EndTable();
    }
    if (!isImages && !vt.multiLabels.empty()) {
        std::vector<std::pair<std::string, int>> types;
        for (const auto& l : vt.multiLabels) addCount(types, l.vartype);
        if (!(types.size() == 1 && types[0].first.empty())) {
            ImGui::Spacing();
            countTable("##vfi_types", "Type", types);
        }
    }

    // ---- Labels ----
    ImGui::SeparatorText("Labels");
    int changed = 0, commented = 0;
    std::vector<std::pair<std::string, int>> labelCounts;
    for (auto& l : vt.multiLabels) {
        if (l.clicked) changed += 1;
        if (!l.comment.empty()) commented += 1;
        if (!l.labels.empty()) addCount(labelCounts, l.current());
    }
    if (beginKeyValue("##vfi_labels")) {
        row("Label file", plot->outLabelFile.empty() ? std::string("none set") : plot->outLabelFile);
        const size_t fromFile = vt.inputLabels ? vt.inputLabels->size() : 0;
        row("In label file", withThousands((long long)fromFile) + (fromFile ? " for this variant file" : ""));
        row("Changed here", withThousands(changed) + " this session");
        row("Comments", withThousands(commented));
        ImGui::EndTable();
    }
    if (!labelCounts.empty()) {
        ImGui::Spacing();
        countTable("##vfi_label_counts", allRead ? "Label" : "Label (variants read so far)", labelCounts);
    }

    // ---- VCF header ----
    if (isVcf && vt.vcf.hdr != nullptr) {
        static VcfHeaderView view;
        if (view.hdr != vt.vcf.hdr) view.build(vt.vcf.hdr);
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("VCF header")) {
            if (ImGui::SmallButton("Copy header")) {
                kstring_t text = {0, 0, nullptr};
                if (bcf_hdr_format(vt.vcf.hdr, 0, &text) == 0 && text.s) ImGui::SetClipboardText(text.s);
                free(text.s);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Copy the header text to the clipboard");
            if (!view.general.empty() && beginKeyValue("##vfi_general")) {
                for (const auto& g : view.general) row(g.first.c_str(), g.second);
                ImGui::EndTable();
            }
            const ImGuiTableFlags tf = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable
                                     | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;
            auto fieldTable = [&](const char* name, const std::vector<VcfHeaderView::Field>& fields, bool typed) {
                if (fields.empty()) return;
                const std::string title = std::string(name) + " (" + std::to_string(fields.size()) + ")###vfi_" + name;
                if (!ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) return;
                if (ImGui::BeginTable(name, typed ? 4 : 2, tf)) {
                    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed);
                    if (typed) {
                        ImGui::TableSetupColumn("Number", ImGuiTableColumnFlags_WidthFixed);
                        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
                    }
                    ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableHeadersRow();
                    for (const auto& f : fields) {
                        ImGui::TableNextRow();
                        int col = 0;
                        ImGui::TableSetColumnIndex(col++); ImGui::TextUnformatted(f.id.c_str());
                        if (typed) {
                            ImGui::TableSetColumnIndex(col++); ImGui::TextDisabled("%s", f.number.c_str());
                            ImGui::TableSetColumnIndex(col++); ImGui::TextDisabled("%s", f.type.c_str());
                        }
                        ImGui::TableSetColumnIndex(col);
                        ImGui::PushTextWrapPos(0.0f);
                        ImGui::TextUnformatted(f.description.c_str());
                        ImGui::PopTextWrapPos();
                    }
                    ImGui::EndTable();
                }
                ImGui::TreePop();
            };
            fieldTable("FILTER", view.filter, false);
            fieldTable("INFO", view.info, true);
            fieldTable("FORMAT", view.format, true);
            if (!view.other.empty()) {
                const std::string title = "Other (" + std::to_string(view.other.size()) + ")###vfi_other";
                if (ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    if (beginKeyValue("##vfi_other_table")) {
                        for (const auto& o : view.other) row(o.first.c_str(), o.second);
                        ImGui::EndTable();
                    }
                    ImGui::TreePop();
                }
            }
            if (!view.contigs.empty()) {
                std::string title = "Contigs (" + withThousands((long long)view.contigs.size()) + ")";
                if (!view.assembly.empty()) title += "  " + view.assembly;
                title += "###vfi_contigs";
                if (ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    const float rowH = ImGui::GetTextLineHeightWithSpacing();
                    const float tableH = std::min((float)view.contigs.size() + 1.5f, 12.0f) * rowH;
                    if (ImGui::BeginTable("##vfi_contig_table", 2, tf | ImGuiTableFlags_ScrollY, ImVec2(0, tableH))) {
                        ImGui::TableSetupScrollFreeze(0, 1);
                        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed);
                        ImGui::TableSetupColumn("Length", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableHeadersRow();
                        ImGuiListClipper clipper;
                        clipper.Begin((int)view.contigs.size());
                        while (clipper.Step()) {
                            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                                const auto& c = view.contigs[i];
                                ImGui::TableNextRow();
                                ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(c.first.c_str());
                                ImGui::TableSetColumnIndex(1);
                                char* endp = nullptr;
                                const long long len = std::strtoll(c.second.c_str(), &endp, 10);
                                ImGui::TextUnformatted(!c.second.empty() && endp && *endp == '\0' ? withThousands(len).c_str()
                                                                                                    : c.second.c_str());
                            }
                        }
                        ImGui::EndTable();
                    }
                    ImGui::TreePop();
                }
            }
        }
    }
    ImGui::End();
}

void drawImGuiLabelTableDialog(Manager::GwPlot* plot, bool& redraw) {
        if (!plot->labelTableDialogOpen) return;
        // Label file chosen in the browser last frame: handled here, before anything below
        // holds on to the variant tracks (loading re-opens them)
        static bool labelBrowserSaving = false;
        static std::string labelFileMessage;
        static double labelFileMessageUntil = 0;
        {
            ImGui::FileBrowser& browser = labelFileBrowser();
            browser.Display();
            if (browser.HasSelected()) {
                const std::string path = browser.GetSelected().string();
                browser.ClearSelected();
                if (labelBrowserSaving) {
                    plot->setOutLabelFile(path);
                    plot->saveLabels();
                    const bool ok = std::filesystem::exists(path);
                    labelFileMessage = ok ? "Labels saved" : "Could not write the label file";
                } else if (std::filesystem::exists(path)) {
                    try {
                        plot->loadLabels(path);
                        labelFileMessage = "Labels loaded";
                    } catch (const std::exception&) {
                        labelFileMessage = "Not a label file";
                    }
                    plot->redraw = true;
                    plot->processed = false;
                    redraw = true;
                } else {
                    labelFileMessage = "File not found";
                }
                labelFileMessageUntil = ImGui::GetTime() + 3.0;
            }
        }
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
        std::string trackKey = vt.fileName.empty() ? vt.path : vt.fileName;
        static std::unordered_map<std::string, bool> loadMoreByTrack;
        bool& loadMore = loadMoreByTrack[trackKey];
        auto hasMoreVariants = [&]() {
            return vt.type == HGW::TrackType::VCF ? !vt.vcf.done
                 : vt.type == HGW::TrackType::GW_TRACK && !vt.variantTrack.done;
        };
        if (hasMoreVariants()) {
            const int batch = 128;
            int target = std::max(batch, vt.blockStart + plot->opts.number.x * plot->opts.number.y + batch);
            if (loadMore) target = std::max(target, (int)vt.multiLabels.size() + batch);
            size_t previousCount = vt.multiLabels.size();
            vt.iterateToIndex(target);
            if (vt.multiLabels.size() != previousCount) {
                plot->redraw = true;
                redraw = true;
            }
        }
        loadMore = false;
        if (vt.multiLabels.empty()) { plot->labelTableDialogOpen = false; return; }

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
        static std::unordered_map<std::string, int> lastSyncedBlockByTrack; // detect blockStart changes from arrow keys

        // Row currently opened in alignment view from its play button (-1 = none)
        static std::string alignmentViewTrack;
        static int alignmentViewRow = -1;
        if (plot->mode == Manager::Show::TILED) alignmentViewRow = -1;

        int& selectedRow = selectedByTrack[trackKey];
        int labelCount = (int)vt.multiLabels.size();
        // Follow the variant shown in the alignment view (edge arrows, Ctrl/Cmd + arrow keys)
        if (plot->mode == Manager::Show::SINGLE && plot->liveVariantIndex >= 0 && plot->liveVariantIndex < labelCount &&
            (alignmentViewRow != plot->liveVariantIndex || alignmentViewTrack != trackKey)) {
            alignmentViewTrack = trackKey;
            alignmentViewRow = plot->liveVariantIndex;
            selectedRow = alignmentViewRow;
        }

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
        int& lastSyncedBlock        = lastSyncedBlockByTrack[trackKey];

        // Sync when the tile selection or page changes externally
        // (right-click on tile, or LEFT/RIGHT arrow page flips)
        if (plot->mode == Manager::Show::TILED &&
            (plot->mouseOverTileIndex != lastSyncedTile || vt.blockStart != lastSyncedBlock)) {
            lastSyncedTile  = plot->mouseOverTileIndex;
            lastSyncedBlock = vt.blockStart;
            int tileIdx = std::max(plot->mouseOverTileIndex, 0);
            int newRow = vt.blockStart + tileIdx;
            if (newRow >= 0 && newRow < labelCount) {
                selectedRow     = newRow;
                tileMarkerIndex = tileIdx;
            }
        }

        std::string trimmedFilter = filterText;
        Utils::trim(trimmedFilter);

        auto& labels = vt.multiLabels;

        ImGuiIO& io = ImGui::GetIO();
        float initW = std::min(240.0f, io.DisplaySize.x * 0.32f);
        // Leave room for top menu and bottom ideogram/slider
        float topOff    = plot->topMenuSpace / plot->monitorScale;
        float botOff    = plot->sliderSpace  / plot->monitorScale;
        float initH     = std::max(100.0f, io.DisplaySize.y - topOff - botOff - 32.0f);
        float initX     = io.DisplaySize.x - initW - 4.0f;

        static std::unordered_map<std::string, bool> wasDockedByTrack;
        bool& wasDocked = wasDockedByTrack[trackKey];
        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoFocusOnAppearing;
        if (plot->labelTableDocked) {
            initW = (plot->fb_width - plot->drawingWidth()) / plot->monitorScale;
            // A docked Tools window sits above; see dockSplitY for the collapsed cases
            float dockTop = 0.0f;
            if (plot->commandDialogOpen && plot->toolsDocked)
                dockTop = dockSplitY(plot, io.DisplaySize.y);
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - initW, dockTop), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(initW, io.DisplaySize.y - dockTop), ImGuiCond_Always);
            windowFlags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize;
        } else {
            if (wasDocked) {
                ImGui::SetNextWindowSize(ImVec2(plot->labelTableWidth, initH), ImGuiCond_Always);
                ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - plot->labelTableWidth - 4, topOff + 4), ImGuiCond_Always);
            } else {
                ImGui::SetNextWindowSize(ImVec2(initW, initH), ImGuiCond_Appearing);
                ImGui::SetNextWindowPos(ImVec2(initX, topOff + 4.0f), ImGuiCond_Appearing);
            }
        }
        wasDocked = plot->labelTableDocked;

        bool windowOpen = true;
        bool dockedStyle = plot->labelTableDocked;
        if (dockedStyle) {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        }
        pushDockTitleColors();
        bool windowVisible = ImGui::Begin("Variants", &windowOpen, windowFlags);
        ImGui::PopStyleColor(3);
        plot->labelTableCollapsed = ImGui::IsWindowCollapsed();
        // Escape does not close the Variants window (see ignoresEscape)
        if (dockedStyle) ImGui::PopStyleVar(2);
        if (windowVisible) {
            if (plot->labelTableDocked) drawDockResizeHandle(plot, initW);
            // Title-bar controls, right to left before X: dock toggle, tile grid, tile view toggle
            drawDockTitleButton(plot->labelTableDocked, 0);
            inTitleBar(2, [&](const ImRect& r) {
                // Tile view on / off. Turning it on pins it, so it is shown even with a 1 x 1 grid
                const bool on = plot->mode == Manager::Show::TILED;
                if (ImGui::Button("##tile_view_btn", r.GetSize())) {
                    plot->tileViewPinned = !on;
                    if (!plot->toggleCurrentVariantTiledView()) plot->tileViewPinned = false;
                    plot->redraw = true;
                    redraw = true;
                }
                const bool hov = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (on) dl->AddRectFilled(r.Min, r.Max, ImGui::GetColorU32(ImGuiCol_ButtonActive), ImGui::GetStyle().FrameRounding);
                // A picture: frame, sun and hills
                const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
                const float w = r.GetWidth(), h = r.GetHeight(), t = iconStrokeThickness(plot->monitorScale);
                const ImVec2 a(r.Min.x + w * 0.14f, r.Min.y + h * 0.2f), b(r.Max.x - w * 0.14f, r.Max.y - h * 0.2f);
                dl->AddRect(a, b, col, 1.5f, 0, t);
                const float iw = b.x - a.x, ih = b.y - a.y;
                dl->AddCircleFilled(ImVec2(a.x + iw * 0.28f, a.y + ih * 0.32f), std::max(1.0f, ih * 0.11f), col, 8);
                const ImVec2 hills[] = {{a.x + iw * 0.08f, b.y - ih * 0.14f}, {a.x + iw * 0.4f, a.y + ih * 0.58f},
                                        {a.x + iw * 0.58f, b.y - ih * 0.26f}, {a.x + iw * 0.74f, a.y + ih * 0.44f},
                                        {b.x - iw * 0.08f, b.y - ih * 0.14f}};
                dl->AddPolyline(hills, 5, col, 0, t);
                if (hov) ImGui::SetTooltip(on ? "Tile view is on: the mouse wheel pages through variants\nClick for the alignment view"
                                              : "Tile view: static images, the mouse wheel pages through variants");
            });
            static ImRect gridButton;
            inTitleBar(1, [&](const ImRect& r) {
                gridButton = r;
                if (ImGui::Button("##tile_grid_btn", r.GetSize())) ImGui::OpenPopup("##tile_grid");
                bool hov = ImGui::IsItemHovered();
                // Full text colour (white on dark themes, black on light) so the dots stand out on the title bar
                ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                float cell = r.GetWidth() * 0.22f, gapPx = r.GetWidth() * 0.12f;
                float x0 = r.GetCenter().x - cell - gapPx * 0.5f, y0 = r.GetCenter().y - cell - gapPx * 0.5f;
                for (int gy = 0; gy < 2; ++gy)
                    for (int gx = 0; gx < 2; ++gx) {
                        ImVec2 q(x0 + gx * (cell + gapPx), y0 + gy * (cell + gapPx));
                        dl->AddRectFilled(q, ImVec2(q.x + cell, q.y + cell), col, 1.0f);
                    }
                if (hov) ImGui::SetTooltip("Tiles shown (%d x %d)", plot->opts.number.x, plot->opts.number.y);
            });
            ImGui::SetNextWindowPos(ImVec2(gridButton.Min.x, gridButton.Max.y + 2), ImGuiCond_Appearing, ImVec2(0.5f, 0));
            if (ImGui::BeginPopup("##tile_grid")) {
                // Hover to preview columns x rows, click to apply (works with touch: tap a cell)
                constexpr int maxTiles = 8;
                static int hoverX = 0, hoverY = 0;
                float cell = ImGui::GetFrameHeight() * 0.75f, pad = 3.0f;
                ImVec2 origin = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##grid_cells", ImVec2(maxTiles * (cell + pad), maxTiles * (cell + pad)));
                bool over = ImGui::IsItemHovered();
                if (over) {
                    ImVec2 m = ImGui::GetIO().MousePos;
                    hoverX = std::clamp((int)((m.x - origin.x) / (cell + pad)) + 1, 1, maxTiles);
                    hoverY = std::clamp((int)((m.y - origin.y) / (cell + pad)) + 1, 1, maxTiles);
                }
                int showX = over ? hoverX : plot->opts.number.x, showY = over ? hoverY : plot->opts.number.y;
                if (ImGui::IsItemClicked()) {
                    // Choosing a grid also shows it: from the alignment view, switch to the tiles on the
                    // page holding the variant that was being viewed
                    const int viewed = plot->mode == Manager::Show::SINGLE ? plot->liveVariantIndex : -1;
                    execCommand(plot, "grid " + std::to_string(hoverX) + "x" + std::to_string(hoverY), redraw);
                    if (plot->singleTileIsLive()) {
                        // 1 x 1: the single "tile" is the live alignment view
                        if (plot->mode == Manager::Show::TILED) {
                            if (plot->showVariantFileInAlignmentView()) redraw = true;
                        }
                    } else if (plot->mode == Manager::Show::SINGLE) {
                        const int perPage = std::max(1, plot->opts.number.x * plot->opts.number.y);
                        if (viewed >= 0) vt.blockStart = (viewed / perPage) * perPage;
                        if (plot->toggleCurrentVariantTiledView()) redraw = true;
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImDrawList* dl = ImGui::GetWindowDrawList();
                for (int gy = 0; gy < maxTiles; ++gy)
                    for (int gx = 0; gx < maxTiles; ++gx) {
                        ImVec2 q(origin.x + gx * (cell + pad), origin.y + gy * (cell + pad));
                        bool on = gx < showX && gy < showY;
                        dl->AddRectFilled(q, ImVec2(q.x + cell, q.y + cell),
                                          ImGui::GetColorU32(on ? ImGuiCol_ButtonActive : ImGuiCol_FrameBg), 2.0f);
                    }
                ImGui::Text("%d x %d tiles", showX, showY);
                ImGui::EndPopup();
            }

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
                    // Reused buffer: no allocation per label per frame once it has grown
                    static std::string hay;
                    hay.clear();
                    hay += lbl.chrom; hay += ':'; hay += std::to_string(lbl.pos); hay += ' ';
                    hay += lbl.variantId; hay += ' '; hay += lbl.current(); hay += ' ';
                    hay += lbl.comment; hay += ' '; hay += lbl.vartype;
                    for (auto& c : hay) c = (char)std::tolower((unsigned char)c);
                    keep = hay.find(filterLower) != std::string::npos;
                }
                if (keep) visibleRows.push_back(i);
            }

            // --- Section 1: Master List (top pane, resizable) ---
            // Per-track persisted fraction of the remaining vertical space used by the master list.
            static std::unordered_map<std::string, float> splitFracByTrack;
            auto splitIt = splitFracByTrack.find(trackKey);
            if (splitIt == splitFracByTrack.end()) {
                splitFracByTrack[trackKey] = 0.40f;
            }
            float& splitFrac = splitFracByTrack[trackKey];

            static std::unordered_map<std::string, bool> detailsCollapsedByTrack;
            bool& detailsCollapsed = detailsCollapsedByTrack[trackKey];
            ImGui::SetNextItemAllowOverlap();
            bool listOpen = ImGui::CollapsingHeader("Variant List", ImGuiTreeNodeFlags_DefaultOpen);
            // Columns icon: which optional columns the list shows
            static bool showLabelColumn = true, showCommentColumn = false;
            float columnsIconX = 0;
            {
                const float side = ImGui::GetFrameHeight();
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - side * 3 - 4);
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                columnsIconX = p0.x;
                if (ImGui::InvisibleButton("##variant_columns_btn", ImVec2(side, side))) ImGui::OpenPopup("##variant_columns_menu");
                const bool hov = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (hov) {
                    dl->AddRectFilled(p0, ImVec2(p0.x + side, p0.y + side), ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                                      ImGui::GetStyle().FrameRounding);
                    ImGui::SetTooltip("Columns");
                }
                // Three upright bars
                const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text, hov ? 1.0f : 0.8f);
                const float t = iconStrokeThickness(plot->monitorScale);
                for (int bar = 0; bar < 3; ++bar) {
                    const float x = p0.x + side * (0.3f + 0.2f * bar);
                    dl->AddLine(ImVec2(x, p0.y + side * 0.26f), ImVec2(x, p0.y + side * 0.74f), col, t * 1.4f);
                }
                ImGui::SetNextWindowPos(ImVec2(p0.x + side, p0.y + side + 2), ImGuiCond_Appearing, ImVec2(1.0f, 0));
                if (ImGui::BeginPopup("##variant_columns_menu")) {
                    ImGui::TextDisabled("Show columns");
                    ImGui::Checkbox("Label", &showLabelColumn);
                    ImGui::Checkbox("Comment", &showCommentColumn);
                    ImGui::EndPopup();
                }
            }
            ImGui::SetNextItemAllowOverlap();
            // Info icon at the right of the header: file and label statistics, VCF header
            static bool fileInfoOpen = false;
            float infoIconX = 0;
            {
                const float side = ImGui::GetFrameHeight();
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - side * 2 - 2);
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                infoIconX = p0.x;
                if (ImGui::InvisibleButton("##variant_file_info_btn", ImVec2(side, side))) fileInfoOpen = !fileInfoOpen;
                const bool hov = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (hov) {
                    dl->AddRectFilled(p0, ImVec2(p0.x + side, p0.y + side), ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                                      ImGui::GetStyle().FrameRounding);
                    ImGui::SetTooltip("File and label information");
                }
                drawInfoIcon(dl, p0, ImVec2(p0.x + side, p0.y + side), ImGui::GetColorU32(ImGuiCol_Text, hov ? 1.0f : 0.8f),
                             iconStrokeThickness(plot->monitorScale));
            }
            drawVariantFileInfoWindow(plot, vt, fileInfoOpen);
            // File icon at the right of the header: save / load the label file
            {
                const float side = ImGui::GetFrameHeight();
                ImGui::SetNextItemAllowOverlap();
                ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - side);
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##label_file_btn", ImVec2(side, side))) ImGui::OpenPopup("##label_file_menu");
                const bool hov = ImGui::IsItemHovered();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (hov) {
                    dl->AddRectFilled(p0, ImVec2(p0.x + side, p0.y + side), ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                                      ImGui::GetStyle().FrameRounding);
                    ImGui::SetTooltip("Save or load labels");
                }
                drawFileIcon(dl, p0, ImVec2(p0.x + side, p0.y + side), ImGui::GetColorU32(ImGuiCol_Text, hov ? 1.0f : 0.8f),
                             iconStrokeThickness(plot->monitorScale));
                if (ImGui::GetTime() < labelFileMessageUntil) {
                    const float w = ImGui::CalcTextSize(labelFileMessage.c_str()).x;
                    dl->AddText(ImVec2(columnsIconX - w - ImGui::GetStyle().ItemSpacing.x, p0.y + ImGui::GetStyle().FramePadding.y),
                                ImGui::GetColorU32(ImGuiCol_Text, 0.8f), labelFileMessage.c_str());
                }
                ImGui::SetNextWindowPos(ImVec2(p0.x + side, p0.y + side + 2), ImGuiCond_Appearing, ImVec2(1.0f, 0));
                if (ImGui::BeginPopup("##label_file_menu")) {
                    ImGui::FileBrowser& browser = labelFileBrowser();
                    auto openBrowser = [&](bool saving) {
                        labelBrowserSaving = saving;
                        browser.SetTitle(saving ? "Save labels" : "Load labels");
                        browser.SetOkButtonLabel(saving ? "Save" : "Load");
                        std::filesystem::path current(plot->outLabelFile);
                        if (!plot->outLabelFile.empty() && std::filesystem::exists(current.parent_path()))
                            browser.SetDirectory(current.parent_path());
                        browser.SetInputName(saving ? (plot->outLabelFile.empty() ? std::string("labels.tsv")
                                                                                    : current.filename().string())
                                                    : std::string());
                        browser.Open();
                    };
                    if (!plot->outLabelFile.empty()) {
                        const std::string name = std::filesystem::path(plot->outLabelFile).filename().string();
                        if (ImGui::MenuItem(("Save to " + name).c_str())) {
                            plot->saveLabels();
                            labelFileMessage = "Labels saved";
                            labelFileMessageUntil = ImGui::GetTime() + 3.0;
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) ImGui::SetTooltip("%s", plot->outLabelFile.c_str());
                    }
                    if (ImGui::MenuItem(plot->outLabelFile.empty() ? "Save labels..." : "Save labels as...")) openBrowser(true);
                    if (ImGui::MenuItem("Load labels...")) openBrowser(false);
                    ImGui::EndPopup();
                }
            }
            float availH = std::max(1.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 2.0f);
            float minPane = ImGui::GetFrameHeight() * 2.0f;
            float topH = std::max(minPane, std::min(availH - minPane, availH * splitFrac));
            if (detailsCollapsed) topH = std::max(1.0f, availH);
            if (listOpen) {
            if (ImGui::BeginChild("##labels_master", ImVec2(0, topH), false)) {
                // Marks column on the right: wide enough for the labelled / comment cross
                float dotColW = ImGui::GetTextLineHeight() * 0.9f + 6.0f;
                // Genotype bars, one per sample, when the VCF has few enough samples to show
                int gtSamples = 0;
                if (isVcfTrack) {
                    vt.vcf.get_samples();
                    const int n = (int)vt.vcf.sample_names.size();
                    if (n > 0 && n <= HGW::GwVariantTrack::maxGenotypeSamples) gtSamples = n;
                }
                const float gtBarW = gtSamples > 8 ? 3.0f : 5.0f, gtBarGap = 1.0f;
                const float gtColW = std::max(ImGui::CalcTextSize("GT").x, gtSamples * (gtBarW + gtBarGap));
                // Optional columns come and go with the Columns menu in the header
                const int colGt = gtSamples ? 0 : -1, colPos = colGt + 1;
                const int colLabel = showLabelColumn ? colPos + 1 : -1;
                const int colComment = showCommentColumn ? std::max(colPos, colLabel) + 1 : -1;
                const int colNote = std::max({colPos, colLabel, colComment}) + 1;
                // Tighten vertical cell padding so more rows fit on screen
                ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 1.0f));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f));
                char tableId[32];  // one id per column layout, as ImGui keeps column state by id
                std::snprintf(tableId, sizeof(tableId), "##ltbl_%d%d%d", gtSamples ? 1 : 0, showLabelColumn ? 1 : 0, showCommentColumn ? 1 : 0);
                bool tableOpen = ImGui::BeginTable(tableId, colNote + 1,
                        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp);
                if (tableOpen) {
                    if (gtSamples) ImGui::TableSetupColumn("GT", ImGuiTableColumnFlags_WidthFixed, gtColW);
                    ImGui::TableSetupColumn("Position",   ImGuiTableColumnFlags_WidthStretch);
                    if (colLabel >= 0) ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch);
                    if (colComment >= 0) ImGui::TableSetupColumn("Comment", ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("##dots",     ImGuiTableColumnFlags_WidthFixed,   dotColW);
                    ImGui::TableHeadersRow();

                    // Auto-scroll to keep selectedRow in view
                    static int lastSelectedRow = -1;
                    bool needScroll = (selectedRow != lastSelectedRow);
                    lastSelectedRow = selectedRow;

                    ImDrawList* drawList = ImGui::GetWindowDrawList();

                    // Expression filter is applied up front so the list can be clipped to the visible rows
                    const size_t preExprCount = visibleRows.size();
                    if (!exprFilter.empty() && isVcfTrack) {
                        size_t kept = 0;
                        for (int i : visibleRows) {
                            Utils::Label& lbl2 = labels[i];
                            std::string ck = trackKey + "#" + std::to_string(i) + "#"
                                           + std::to_string(lbl2.i) + "#" + lbl2.savedDate
                                           + "#" + lbl2.comment + "#" + std::to_string(selectedSampleIdx);
                            auto cit = detailCache.find(ck);
                            if (cit == detailCache.end()) {
                                cit = detailCache.emplace(std::move(ck), parseVariantDetailRows(vt, lbl2, selectedSampleIdx)).first;
                            }
                            if (evalVcfExpr(cit->second, exprFilter)) visibleRows[kept++] = i;
                        }
                        visibleRows.resize(kept);
                    }

                    // Only rows in view are submitted; the selected row is always included so it can be scrolled to
                    ImGuiListClipper clipper;
                    clipper.Begin((int)visibleRows.size());
                    if (needScroll) {
                        auto sel = std::find(visibleRows.begin(), visibleRows.end(), selectedRow);
                        if (sel != visibleRows.end()) clipper.IncludeItemByIndex((int)(sel - visibleRows.begin()));
                    }
                    while (clipper.Step())
                    for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                        const int i = visibleRows[row];

                        Utils::Label& lbl = labels[i];
                        bool isSelected  = (selectedRow == i);
                        ImGui::TableNextRow();
                        ImGui::PushID(i);

                        // Scroll this row into view when it becomes selected
                        if (isSelected && needScroll) {
                            ImGui::SetScrollHereY(0.5f);
                        }

                        // ---- Genotypes: one bar per sample, an empty box where the call is missing ----
                        if (colGt >= 0) {
                            ImGui::TableSetColumnIndex(colGt);
                            const ImVec2 p = ImGui::GetCursorScreenPos();
                            const float textH = ImGui::GetTextLineHeight();
                            ImGui::Dummy(ImVec2(gtSamples * (gtBarW + gtBarGap), textH));
                            const bool cellHovered = ImGui::IsItemHovered();
                            const float mouseX = ImGui::GetIO().MousePos.x;
                            const std::string& g = lbl.genotypes;
                            size_t from = 0;
                            for (int sample = 0; sample < gtSamples && from <= g.size() && !g.empty(); ++sample) {
                                size_t to = g.find(',', from);
                                if (to == std::string::npos) to = g.size();
                                const int kind = genotypeKind(g.data() + from, to - from);
                                const ImVec2 a(p.x + sample * (gtBarW + gtBarGap), p.y + 1.0f);
                                const ImVec2 b(a.x + gtBarW, p.y + textH - 1.0f);
                                if (kind == 0) drawList->AddRect(a, b, genotypeColors[0]);
                                else drawList->AddRectFilled(a, b, genotypeColors[kind]);
                                if (cellHovered && mouseX >= a.x && mouseX < b.x + gtBarGap) {
                                    ImGui::SetTooltip("%s\n%.*s  %s", vt.vcf.sample_names[sample].c_str(),
                                                      (int)(to - from), g.data() + from, genotypeNames[kind]);
                                }
                                from = to + 1;
                            }
                        }

                        // ---- Alignment-view toggle, then position ----
                        ImGui::TableSetColumnIndex(colPos);
                        {
                            // Right triangle: open this variant in alignment view; filled while it
                            // is shown there, and clicking it again returns to the tiles
                            const bool showing = plot->mode == Manager::Show::SINGLE &&
                                                 alignmentViewTrack == trackKey && alignmentViewRow == i;
                            const float side = ImGui::GetTextLineHeight();
                            ImVec2 p0 = ImGui::GetCursorScreenPos();
                            // With a 1 x 1 grid there are no tiles to go back to
                            const bool canShowTiles = !plot->singleTileIsLive();
                            if (ImGui::InvisibleButton("##play", ImVec2(side, side))) {
                                if (showing) {
                                    if (canShowTiles) {
                                        alignmentViewRow = -1;
                                        plot->toggleCurrentVariantTiledView();
                                    }
                                } else if (plot->openVariantInAlignmentView(i)) {
                                    selectedRow = i;
                                    tileMarkerIndex = plot->mouseOverTileIndex;
                                    alignmentViewTrack = trackKey;
                                    alignmentViewRow = i;
                                }
                                plot->redraw = true;
                                redraw = true;
                            }
                            const bool hov = ImGui::IsItemHovered();
                            const ImU32 col = ImGui::GetColorU32(hov || showing ? ImGuiCol_Text : ImGuiCol_TextDisabled);
                            const ImVec2 a(p0.x + side * 0.25f, p0.y + side * 0.18f), b(p0.x + side * 0.82f, p0.y + side * 0.5f),
                                         c(p0.x + side * 0.25f, p0.y + side * 0.82f);
                            if (showing) ImGui::GetWindowDrawList()->AddTriangleFilled(a, b, c, col);
                            else ImGui::GetWindowDrawList()->AddTriangle(a, b, c, col, 1.2f);
                            if (hov && !(showing && !canShowTiles)) ImGui::SetTooltip(showing ? "Back to tiles" : "Show in alignment view");
                            ImGui::SameLine(0, 2.0f);
                        }
                        std::string location = lbl.chrom + ":" + Utils::formatNum(lbl.pos);
                        if (ImGui::Selectable(location.c_str(), isSelected,
                                              ImGuiSelectableFlags_None)) {
                            selectedRow = i;
                            if (plot->singleTileIsLive()) {
                                // 1 x 1 grid: the row opens in the alignment view
                                if (plot->openVariantInAlignmentView(i)) tileMarkerIndex = plot->mouseOverTileIndex;
                            } else {
                                plot->mode = Manager::Show::TILED;
                                int tilesPerPage = plot->opts.number.x * plot->opts.number.y;
                                vt.blockStart = (i / tilesPerPage) * tilesPerPage;
                                plot->mouseOverTileIndex = i - vt.blockStart;
                                tileMarkerIndex = plot->mouseOverTileIndex; // only update on click
                            }
                            plot->redraw = true;
                            plot->processed = false;
                        }

                        // ---- Label text ----
                        if (colLabel >= 0) {
                            ImGui::TableSetColumnIndex(colLabel);
                            if (ImGui::Selectable(lbl.labels.empty() ? "-" : lbl.current().c_str(), false)
                                    && !lbl.labels.empty()) {
                                lbl.next();
                                lbl.clicked = true;
                                lbl.savedDate = Utils::dateTime();
                                plot->redraw = true;
                                redraw = true;
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                ImGui::SetTooltip("Click to cycle label");
                        }

                        // ---- Comment text; click to add or edit ----
                        if (colComment >= 0) {
                            ImGui::TableSetColumnIndex(colComment);
                            const std::string shown = lbl.comment.empty() ? std::string("##nocomment") : lbl.comment + "##comment";
                            if (ImGui::Selectable(shown.c_str(), false)) {
                                labelDetailsRow  = i;
                                labelDetailsOpen = true;
                                commentEdit      = lbl.comment;
                            }
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                ImGui::SetTooltip("%s", lbl.comment.empty() ? "Click to add a comment" : lbl.comment.c_str());
                        }

                        // ---- Last column: comment / date dots; click to add or edit a comment ----
                        ImGui::TableSetColumnIndex(colNote);

                        // Two marks that make a cross when both apply: an upright bar for "labelled by
                        // hand" and a level bar for "has a comment". Drawn in the text colour so they
                        // are not mistaken for the genotype bars
                        bool hasLabel   = lbl.clicked || !lbl.savedDate.empty();
                        bool hasComment = !lbl.comment.empty();
                        bool hasMarks   = hasLabel || hasComment;

                        {
                            ImVec2 cellPos  = ImGui::GetCursorScreenPos();
                            float  textH    = ImGui::GetTextLineHeight();
                            float  cellH    = textH + ImGui::GetStyle().CellPadding.y * 2.0f;
                            float  arm      = textH * 0.3f;
                            float  thick    = std::max(1.5f, textH * 0.13f);
                            ImVec2 c(cellPos.x + dotColW * 0.5f, cellPos.y + cellH * 0.5f);
                            const ImU32 markCol = ImGui::GetColorU32(ImGuiCol_Text, 0.9f);

                            if (hasLabel) drawList->AddLine(ImVec2(c.x, c.y - arm), ImVec2(c.x, c.y + arm), markCol, thick);
                            if (hasComment) drawList->AddLine(ImVec2(c.x - arm, c.y), ImVec2(c.x + arm, c.y), markCol, thick);

                            // Invisible button always present: opens Label details for THIS row.
                            // When the row has no marks, a faint cross appears on hover.
                            ImGui::SetCursorScreenPos(cellPos);
                            if (ImGui::InvisibleButton("##dotbtn", ImVec2(dotColW, cellH))) {
                                labelDetailsRow  = i;
                                labelDetailsOpen = true;
                                commentEdit      = lbl.comment;
                            }
                            bool hovered = ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort);
                            if (!hasMarks && ImGui::IsItemHovered()) {
                                const ImU32 hint = ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.7f);
                                drawList->AddLine(ImVec2(c.x, c.y - arm), ImVec2(c.x, c.y + arm), hint, 1.0f);
                                drawList->AddLine(ImVec2(c.x - arm, c.y), ImVec2(c.x + arm, c.y), hint, 1.0f);
                            }
                            if (hovered) {
                                std::string tip;
                                if (hasLabel)   tip += lbl.savedDate.empty() ? std::string("Labelled this session\n")
                                                                             : "Labelled " + lbl.savedDate + "\n";
                                if (hasComment) tip += "Comment: " + lbl.comment;
                                if (tip.empty()) tip = "Click to add a comment";
                                ImGui::SetTooltip("%s", tip.c_str());
                            }
                        }

                        ImGui::PopID();
                    }
                    if (hasMoreVariants() &&
                        ((ImGui::IsWindowHovered() && ImGui::GetScrollMaxY() > 0 &&
                          ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - ImGui::GetWindowHeight() * 2) ||
                         ((!trimmedFilter.empty() || !exprFilter.empty()) && preExprCount < 32))) {
                        loadMore = true;
                        plot->redraw = true;
                        redraw = true;
                    }
                    ImGui::EndTable();
                }
                ImGui::PopStyleVar(2);
            }
            ImGui::EndChild();
            }

            // --- Tile marker: black filled circle at bottom-centre of the selected tile ---
            // Only shown when >1 tile visible, drawn behind UI via BackgroundDrawList,
            // and only updated on row click (tileMarkerIndex), not on mouse hover.
            {
                int nx = plot->opts.number.x;
                int ny = plot->opts.number.y;
                int totalTiles = nx * ny;
                const auto boxes = plot->tileBoundingBoxes();
                if (plot->mode == Manager::Show::TILED &&
                    totalTiles > 1 &&
                    tileMarkerIndex >= 0 &&
                    tileMarkerIndex < totalTiles &&
                    tileMarkerIndex < (int)boxes.size())
                {
                    float ms = plot->monitorScale;
                    const auto& box = boxes[tileMarkerIndex];
                    float cx = (box.xStart + box.xEnd) * 0.5f / ms;
                    float cy = box.yEnd / ms;
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
                bool detailsVisible = ImGui::Begin("Label details##popup", &detOpen,
                        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_AlwaysAutoResize);
                closeOnEscape(&detOpen);
                if (detailsVisible) {
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
                        // A comment alone does not count as labelling: the date is left as it is
                        dLbl.comment = commentEdit;
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

            // --- Draggable splitter between master list and details ---
            if (listOpen && !detailsCollapsed) {
                const float splitterH = 6.0f;
                ImVec2 sp = ImGui::GetCursorScreenPos();
                float splitterW = ImGui::GetContentRegionAvail().x;
                ImGui::InvisibleButton("##vsplit", ImVec2(splitterW, splitterH));
                bool hovered = ImGui::IsItemHovered();
                bool active  = ImGui::IsItemActive();
                if (hovered || active) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                }
                if (active) {
                    float dy = ImGui::GetIO().MouseDelta.y;
                    if (availH > 0.0f) {
                        splitFrac = std::max(0.05f,
                                             std::min(0.95f,
                                                      (topH + dy) / availH));
                    }
                }
                ImU32 col = active  ? ImGui::GetColorU32(ImGuiCol_SeparatorActive)
                          : hovered ? ImGui::GetColorU32(ImGuiCol_SeparatorHovered)
                                    : ImGui::GetColorU32(ImGuiCol_Separator);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                float lineY = sp.y + splitterH * 0.5f;
                dl->AddLine(ImVec2(sp.x, lineY),
                            ImVec2(sp.x + splitterW, lineY), col, 1.0f);
            }
            // The header spans the full width and the clipboard action sits on top of it, so
            // the button shares the header's own background (a separate fill behind a
            // translucent header colour never matched it).
            ImGuiWindow* detailWindow = ImGui::GetCurrentWindow();
            float headerRight = detailWindow->WorkRect.Max.x;
            float iconSide = ImGui::GetFrameHeight();
            ImGui::SetNextItemAllowOverlap();
            detailsCollapsed = !ImGui::CollapsingHeader("Variant Details", ImGuiTreeNodeFlags_DefaultOpen);
            ImGui::SameLine(headerRight - detailWindow->Pos.x - iconSide + 2);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
            bool copyVar = ImGui::InvisibleButton("##copy_variant_command", ImVec2(iconSide - 4, iconSide - 4));
            ImVec2 iconMin = ImGui::GetItemRectMin(), iconMax = ImGui::GetItemRectMax();
            bool copyHovered = ImGui::IsItemHovered();
            if (copyHovered) {
                ImGui::GetWindowDrawList()->AddRectFilled(iconMin, iconMax,
                    ImGui::GetColorU32(ImGuiCol_ButtonHovered), ImGui::GetStyle().FrameRounding);
                ImGui::SetTooltip("Copy var output");
            }
            drawClipboardIcon(ImGui::GetWindowDrawList(), iconMin, iconMax,
                ImGui::GetColorU32(ImGuiCol_Text, copyHovered ? 1.0f : 0.7f),
                iconStrokeThickness(plot->monitorScale));
            if (copyVar && selectedRow >= 0 && selectedRow < (int)labels.size()) {
                std::string record;
                if (Commands::var_record(plot, selectedRow, record))
                    ImGui::SetClipboardText(record.c_str());
            }
            if (!detailsCollapsed) {

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
                    auto cacheIt = detailCache.find(cacheKey);
                    if (cacheIt == detailCache.end()) {
                        cacheIt = detailCache.emplace(cacheKey, parseVariantDetailRows(vt, lbl, selectedSampleIdx)).first;
                    }

                    auto& rows = cacheIt->second;

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
                        return rd.label == "Position" || rd.label == "End" || rd.label == "Mate";
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
                                                // The sample index is part of the cache key.
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
                            ImGui::PushID((int)i);
                            if (ImGui::Selectable(row.label.c_str(), false))
                                ImGui::SetClipboardText(row.plainValue.c_str());
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                                ImGui::SetTooltip("Copy %s", row.label.c_str());
                            ImGui::PopID();

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
                                ImGui::TextUnformatted(row.plainValue.c_str());
                            }
                        }
                        ImGui::EndTable();
                    }
                }
            }
            ImGui::EndChild();
            }
        }
        ImGui::End();

        if (!windowOpen) plot->labelTableDialogOpen = false;
    }



} // namespace Menu