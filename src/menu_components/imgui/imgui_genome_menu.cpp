// imgui_genome_menu.cpp
// "Genome" submenu at the foot of the chromosome list. Lists reference genomes from .gw.ini,
// the GW downloads folder and the IGV / Broad catalogue, one row per assembly with local files
// taking precedence. Picking one opens it together with its ideogram; when the open reference
// is unrecognised but turns out to be that assembly, only the ideogram is added.
// Nothing is gathered until the submenu is first opened.

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "menu.h"
#include "plot_manager.h"
#include "ideogram.h"
#include "online/online_files.h"
#include "online/downloads.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace Menu {

struct GenomeItem {
    std::string key;        // assembly id or .gw.ini tag; becomes the genome tag once opened
    std::string label;
    Online::Entry entry;    // reference url and index companions
    bool local = false;     // a file on disk (drawn with a dot)
    bool iniTag = false;    // listed in .gw.ini, so loaded through its tag
    bool scanned = false;   // found in the downloads folder; named by filename until matched
};

struct GenomeMenuState {
    bool built = false;
    bool loading = false;
    std::vector<GenomeItem> items;
    // An unrecognised reference that was matched to an assembly (ideogram only)
    std::string matchedKey, matchedReference;
    bool browserOpening = false;  // GW-wasm: a reference is being prepared by gwOpenOnline
#ifndef __EMSCRIPTEN__
    std::future<std::vector<Online::Entry>> igv;
    std::shared_ptr<std::atomic<bool>> stop;
#endif
};

static GenomeMenuState& genomeMenuState() {
    static GenomeMenuState s;
    return s;
}

static std::string genomeLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

static bool samePath(const std::string& a, const std::string& b) {
    if (a == b) return true;
    if (Online::httpURL(a) || Online::httpURL(b)) return false;
    std::error_code ec;
    return std::filesystem::equivalent(a, b, ec);
}

// GW's genome tags that IGV names differently
static bool sameAssembly(const std::string& a, const std::string& b) {
    std::string x = genomeLower(a), y = genomeLower(b);
    if (x == y) return true;
    auto t2t = [](const std::string& s) { return s == "t2t" || s == "hs1"; };
    return t2t(x) && t2t(y);
}

// Add catalogue references, skipping assemblies or files already listed (local ones win)
static void mergeGenomeCatalogue(GenomeMenuState& s, const std::vector<Online::Entry>& entries) {
    for (const auto& e : entries) {
        if (e.category != "reference" || e.url.empty()) continue;
        const std::string key = e.assembly.empty() ? e.name : e.assembly;
        std::string label = e.name.empty() ? key : e.name;
        Utils::trim(label);  // IGV names carry stray spaces
        const std::string file = Online::resourceName(e.url);
        bool listed = false;
        for (auto& item : s.items) {
            const bool sameKey = sameAssembly(item.key, key);
            const bool sameFile = item.local && Online::resourceName(item.entry.url) == file;
            if (!sameKey && !sameFile && item.entry.url != e.url) continue;
            listed = true;
            if (item.scanned) {  // a download of this assembly: adopt its id so ideograms resolve
                item.key = key;
                item.entry.assembly = key;
                item.scanned = false;
            }
            if (item.label == item.key || sameFile) item.label = label;
#ifdef __EMSCRIPTEN__
            // A browser can rarely read a .gw.ini URL (no CORS); the catalogue's copy is served for it
            if (!item.local && sameKey) {
                item.entry.url = e.url;
                item.entry.indexURL = e.indexURL;
                item.entry.compressedIndexURL = e.compressedIndexURL;
                item.iniTag = false;
            }
#endif
            break;
        }
        if (listed) continue;
        GenomeItem item;
        item.key = key;
        item.label = label;
        item.entry = e;
        item.entry.assembly = key;
        s.items.push_back(std::move(item));
    }
}

static void sortGenomeList(GenomeMenuState& s) {
    std::stable_sort(s.items.begin(), s.items.end(), [](const GenomeItem& a, const GenomeItem& b) {
        return genomeLower(a.label) < genomeLower(b.label);
    });
}

// GitHub release downloads send no CORS headers, so a browser can't fetch them
static bool blockedInBrowser(const GenomeItem& item) {
#ifdef __EMSCRIPTEN__
    return item.entry.url.find("github.com/") != std::string::npos &&
           item.entry.url.find("/releases/download/") != std::string::npos;
#else
    (void)item;
    return false;
#endif
}

static void buildGenomeList(Manager::GwPlot* plot, GenomeMenuState& s) {
    s.built = true;
    s.items.clear();
    // A remote viewer opens genomes on its server, so this machine's files don't apply
    if (!plot->remoteCommand) {
        for (const auto& kv : plot->opts.myIni["genomes"]) {
            GenomeItem item;
            item.key = item.label = kv.first;
            item.entry.category = "reference";
            item.entry.assembly = kv.first;
            item.entry.url = kv.second;
            item.local = !Online::httpURL(kv.second);
            item.iniTag = true;
            s.items.push_back(std::move(item));
        }
        std::error_code ec;
        const std::filesystem::path folder = Online::downloadDirectory(plot->opts.ini_path);
        for (const auto& f : std::filesystem::directory_iterator(folder, ec)) {
            const std::string path = f.path().string();
            const std::string name = genomeLower(f.path().filename().string());
            bool fasta = false;
            for (const char* ext : {".fa", ".fasta", ".fna", ".fa.gz", ".fasta.gz", ".fna.gz"}) {
                const std::string e = ext;
                if (name.size() > e.size() && name.compare(name.size() - e.size(), e.size(), e) == 0) fasta = true;
            }
            if (!fasta || !std::filesystem::exists(path + ".fai", ec)) continue;
            bool listed = false;
            for (const auto& item : s.items) listed = listed || samePath(item.entry.url, path);
            if (listed) continue;
            GenomeItem item;
            item.key = item.label = f.path().filename().string();
            item.entry.category = "reference";
            item.entry.assembly = item.key;
            item.entry.url = path;
            item.local = item.scanned = true;
            s.items.push_back(std::move(item));
        }
    }
    try {
        mergeGenomeCatalogue(s, Online::bundledCatalogue());
    } catch (...) {}
    sortGenomeList(s);
#ifdef __EMSCRIPTEN__
    if (!plot->remoteCommand) {
        s.loading = EM_ASM_INT({
            if (!Module.gwFetchCatalogue) return 0;
            Module.gwFetchCatalogue(UTF8ToString($0));
            return 1;
        }, genomeCatalogueURL);
    }
#else
    if (!plot->remoteCommand) {
        s.loading = true;
        s.stop = std::make_shared<std::atomic<bool>>(false);
        auto stop = s.stop;
        s.igv = std::async(std::launch::async, [stop] {
            return Online::parseCatalogue(Online::fetchCatalogue(genomeCatalogueURL, stop));
        });
    }
#endif
}

// Errors go to the Output popup, like command output
static void reportGenomeError(Manager::GwPlot* plot, const std::string& message) {
    std::string text = message;
    while (!text.empty() && (text.back() == '\n' || text.front() == '\n')) {
        if (text.back() == '\n') text.pop_back(); else text.erase(0, 1);
    }
    appendCommandOutput(plot, "\nGenome: " + text + "\n");
}

static void pollGenomeList(Manager::GwPlot* plot, GenomeMenuState& s) {
    if (!s.loading) return;
    try {
#ifdef __EMSCRIPTEN__
        auto text = onlineBrowserResult(true);
        if (text.empty()) return;
        s.loading = false;
        if (text.rfind("error:", 0) == 0) throw std::runtime_error(text.substr(6));
        mergeGenomeCatalogue(s, Online::parseCatalogue(text));
        sortGenomeList(s);
#else
        if (!s.igv.valid() || s.igv.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        s.loading = false;
        mergeGenomeCatalogue(s, s.igv.get());
        sortGenomeList(s);
#endif
    } catch (const std::exception& e) {
        reportGenomeError(plot, std::string("IGV genome list unavailable: ") + e.what());
    }
}

static bool genomeIsOpen(const Manager::GwPlot* plot, const GenomeMenuState& s, const GenomeItem& item) {
    if (!plot->fai || plot->reference.empty()) return false;
    if (item.key == s.matchedKey && plot->reference == s.matchedReference) return true;
    return samePath(plot->reference, item.entry.url);
}

#ifdef __EMSCRIPTEN__
// When the open reference's chromosome lengths equal the ends of an assembly's ideogram, keep
// that ideogram (it is already loaded) and return true; otherwise restore the previous one.
// Needs no index download, so it suits the browser build.
static bool ideogramMatchesReference(Manager::GwPlot* plot, const std::string& key) {
    if (!plot->fai) return false;
    auto saved = plot->ideogram;
    auto savedPath = plot->ideogram_path;
    plot->loadIdeogramForAssembly(key);
    size_t shared = 0;
    bool mismatch = false;
    for (const auto& kv : plot->ideogram) {
        if (kv.second.empty() || !faidx_has_seq(plot->fai, kv.first.c_str())) continue;
        int end = 0;
        for (const auto& band : kv.second) end = std::max(end, band.end);
        if (end != faidx_seq_len(plot->fai, kv.first.c_str())) { mismatch = true; break; }
        ++shared;
    }
    if (!mismatch && shared > 0 && shared >= std::min<size_t>(3, plot->ideogram.size())) return true;
    plot->ideogram = std::move(saved);
    plot->ideogram_path = std::move(savedPath);
    return false;
}
#endif

static void selectGenome(Manager::GwPlot* plot, GenomeMenuState& s, const GenomeItem& item, bool recognised, bool& redraw) {
    if (plot->remoteCommand) {
        execCommand(plot, "load genome " + item.entry.url, redraw);
        std::string ideo = Ideo::ideogramForAssembly(item.key);
        execCommand(plot, "rm ideogram", redraw);
        if (!ideo.empty()) execCommand(plot, "load ideogram " + ideo, redraw);
        return;
    }
    try {
        const bool open = plot->fai && samePath(plot->reference, item.entry.url);
#ifdef __EMSCRIPTEN__
        // Remote indexes can't be read from the render loop; compare against the ideogram instead
        const bool same = !open && plot->fai && !recognised && ideogramMatchesReference(plot, item.key);
#else
        const bool same = !open && plot->fai && !recognised && Online::referenceMatches(plot, item.entry);
#endif
        if (open || same) {
            // Already have this assembly locally: only the ideogram is needed
            plot->opts.genome_tag = item.key;
            if (!same) plot->loadIdeogramForAssembly(item.key);  // the ideogram check already loaded it
#ifdef __EMSCRIPTEN__
        } else if (Online::httpURL(item.entry.url)) {
            // Same route as Open > Online file: JS prepares the remote file, then GW opens it
            // (with its ideogram) through Online::open. gwOpenOnline reports its own errors.
            Online::Entry e = item.entry;
            e.category = "reference";
            e.assembly = item.key;
            if (e.id.empty()) e.id = "genome:" + item.key;  // the catalogue format requires both
            if (e.name.empty()) e.name = item.label;
            auto encoded = Online::encodeCatalogue({e});
            EM_ASM({ if (Module.gwOpenOnline) Module.gwOpenOnline(UTF8ToString($0), false, false, false); }, encoded.c_str());
            s.browserOpening = true;
            s.matchedKey = item.key;
            s.matchedReference.clear();
            return;
#endif
        } else {
            std::string error = Online::openReference(plot, item.entry, item.iniTag);
            if (!error.empty()) {
                reportGenomeError(plot, "could not open " + item.label + "\n" + stripAnsiCodes(error));
                return;
            }
        }
        s.matchedKey = item.key;
        s.matchedReference = plot->reference;
    } catch (const std::exception& e) {
        reportGenomeError(plot, "could not open " + item.label + ": " + e.what());
    } catch (...) {
        reportGenomeError(plot, "could not open " + item.label);
    }
    plot->processed = false;
    redraw = true;
}

// GW-wasm: pick up gwOpenOnline's result for a genome chosen from the menu
static void pollGenomeOpen(Manager::GwPlot* plot) {
#ifdef __EMSCRIPTEN__
    auto& s = genomeMenuState();
    if (!s.browserOpening) return;
    auto text = onlineBrowserResult(false);
    if (text.empty()) return;
    s.browserOpening = false;
    if (text.rfind("error:", 0) == 0) {
        s.matchedKey.clear();
        reportGenomeError(plot, "could not open genome: " + text.substr(6));
    } else {
        s.matchedReference = plot->reference;
        // The frame drawn as the reference mounts misses the ruler and ideogram;
        // revisiting the region (as typing it would) redraws everything
        std::string region = plot->regions.size() == 1 ? plot->regions[0].toString() : "refresh";
        std::ostringstream out;
        Commands::run_command_map(plot, region, out);
        plot->processed = false;
        plot->redraw = true;
    }
#else
    (void)plot;
#endif
}

static void drawGenomeMenu(Manager::GwPlot* plot, bool& redraw) {
    auto& s = genomeMenuState();
    ImGui::SetNextWindowSizeConstraints(ImVec2(0, 0), ImVec2(FLT_MAX, ImGui::GetIO().DisplaySize.y * 0.6f));
    if (!ImGui::BeginMenu("Genome")) return;
    if (!s.built) buildGenomeList(plot, s);
    pollGenomeList(plot, s);

    bool recognised = false;
    for (const auto& item : s.items) recognised = recognised || genomeIsOpen(plot, s, item);

    const float dotR = ImGui::GetFontSize() * 0.16f;
    const ImU32 dotCol = ImGui::GetColorU32(ImGuiCol_CheckMark);
    auto drawDot = [&](float textWidth) {
        const ImVec2 lo = ImGui::GetItemRectMin(), hi = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddCircleFilled(
            ImVec2(lo.x + textWidth + ImGui::GetStyle().ItemInnerSpacing.x + dotR, (lo.y + hi.y) * 0.5f), dotR, dotCol);
    };

    int pick = -1;
    bool anyLocal = false;
    for (int i = 0; i < (int)s.items.size(); ++i) {  // alphabetical; local files carry a dot
        const auto& item = s.items[i];
        if (blockedInBrowser(item)) continue;  // can't be loaded here, so not offered
        anyLocal = anyLocal || item.local;
        ImGui::PushID(i);
        if (ImGui::MenuItem(item.label.c_str(), nullptr, genomeIsOpen(plot, s, item))) pick = i;
        if (item.local) drawDot(ImGui::CalcTextSize(item.label.c_str()).x);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("%s", item.entry.url.c_str());
        ImGui::PopID();
    }
    if (s.loading) ImGui::TextDisabled("Loading IGV genomes...");
    if (anyLocal) {
        ImGui::Separator();
        ImGui::TextDisabled("Local file");
        drawDot(ImGui::CalcTextSize("Local file").x);
    }
    ImGui::EndMenu();

    if (pick >= 0) {
        const GenomeItem chosen = s.items[pick];
        selectGenome(plot, s, chosen, recognised, redraw);
        ImGui::CloseCurrentPopup();
    }
}

} // namespace Menu
