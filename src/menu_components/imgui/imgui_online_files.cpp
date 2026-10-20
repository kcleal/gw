// Shared online picker. Discovery and reading are separate from the ImGui layout.
#include "online/online_catalogue.h"
#include "online/online_files.h"
#include "online/downloads.h"
#include <atomic>
#include <chrono>
#include <cstring>
#include <future>
#include <map>

namespace Menu {
namespace {
bool onlineFileDialogOpen = false;
constexpr const char* genomeCatalogueURL = "https://s3.amazonaws.com/igv.org.genomes/genomes.json";
const char* onlineCategories[] = {"reference", "alignment", "track", "variant"};

struct OnlineDialogState {
    std::vector<Online::Entry> entries = Online::bundledCatalogue(), recent;
    Online::Entry selected;
    char search[256]{}, url[2048]{}, index[2048]{}, gzi[2048]{}, assembly[81]{};
    std::string status, error, persistencePath, catalogueError;
    int category = 0, list = 0;
    bool discovering=false;
    std::string lastReference, discoveryError, discoveryInfo, discoveryKind, discoveryAssembly, discoveryKey, discoveryQuery, ucscQuery;
    Online::Entry inspecting, pendingInspection;
    std::map<std::string,std::vector<Online::Entry>> searchCache;
    std::map<std::string,Online::Entry> detailCache;
    std::chrono::steady_clock::time_point lastApiRequest{};
    bool downloading=false;
    bool busy = false, asTrack = true, asTable = false, checkAssembly = true;
    bool cancelled = false, catalogueRequested = false, catalogueLoading = false;
#ifndef __EMSCRIPTEN__
    std::future<std::vector<Online::Entry>> task, catalogueTask;
    std::future<std::string> discoveryTask;
    std::shared_ptr<std::atomic<bool>> catalogueStop;
    std::shared_ptr<Online::DownloadProgress> progress;
#endif
};
OnlineDialogState& onlineState() { static OnlineDialogState state; return state; }
void onlineCopy(char* dest, size_t size, const std::string& text) {
    std::snprintf(dest, size, "%s", text.c_str());
}
void onlineSelect(OnlineDialogState& s, const Online::Entry& entry) {
    s.selected = entry;
    onlineCopy(s.url, sizeof(s.url), entry.url);
    onlineCopy(s.index, sizeof(s.index), entry.indexURL);
    onlineCopy(s.gzi, sizeof(s.gzi), entry.compressedIndexURL);
    s.error.clear();
}
void onlineRemember(Manager::GwPlot* plot, OnlineDialogState& s) {
    s.recent.erase(std::remove_if(s.recent.begin(), s.recent.end(),
        [&](const auto& entry) { return entry.url == s.selected.url; }), s.recent.end());
    s.recent.insert(s.recent.begin(), s.selected);
    if (s.recent.size() > 12) s.recent.resize(12);
    auto& saved = plot->opts.myIni["online-recent"];
    saved.clear();
    for (size_t i = 0; i < s.recent.size(); ++i)
        saved[std::to_string(i)] = Online::encodeEntry(s.recent[i]);
    plot->opts.saveSettings();
}
void onlineMerge(OnlineDialogState& s, const std::vector<Online::Entry>& entries) {
    for (const auto& entry : entries) {
        auto it = std::find_if(s.entries.begin(), s.entries.end(),
            [&](const auto& old) { return old.id == entry.id; });
        if (it == s.entries.end()) s.entries.push_back(entry);
        else {
            auto replacement = entry;
            if (replacement.locus.empty()) replacement.locus = it->locus;
            *it = std::move(replacement);
        }
    }
}
void onlineCancel(OnlineDialogState& s) {
    s.cancelled = true;
#ifndef __EMSCRIPTEN__
    if(s.progress)s.progress->cancelled=true;
#endif
    s.status = "Cancelling…";
#ifdef __EMSCRIPTEN__
    EM_ASM({ if(Module.gwOnlineCancel) Module.gwOnlineCancel(); });
#endif
    // Native preflight owns separate file handles; discard its result when it finishes.
}
void onlineFetchGenomes(OnlineDialogState& s) {
    s.catalogueRequested = s.catalogueLoading = true;
    s.catalogueError.clear();
#ifdef __EMSCRIPTEN__
    EM_ASM({ if(Module.gwFetchCatalogue) Module.gwFetchCatalogue(UTF8ToString($0)); }, genomeCatalogueURL);
#else
    s.catalogueStop = std::make_shared<std::atomic<bool>>(false);
    auto stop = s.catalogueStop;
    s.catalogueTask = std::async(std::launch::async, [stop] {
        return Online::parseCatalogue(Online::fetchCatalogue(genomeCatalogueURL, stop));
    });
#endif
}
#ifdef __EMSCRIPTEN__
std::string onlineBrowserResult(int catalogue) {
    char* result = reinterpret_cast<char*>(EM_ASM_PTR({
        var key=$0===2?'gwDiscoveryResult':($0?'gwCatalogueResult':'gwOnlineResult');
        if(!Module[key]) return 0;
        var text=Module[key];Module[key]=null;
        var ptr=_malloc(lengthBytesUTF8(text)+1);
        stringToUTF8(text,ptr,lengthBytesUTF8(text)+1);return ptr;
    }, catalogue));
    if (!result) return {};
    std::string text(result); free(result); return text;
}
#endif
void onlineUnavailable(OnlineDialogState& s, Online::Entry entry) {
    entry.format="unavailable";
    if(entry.description.find("individual data track")==std::string::npos)entry.description+=" — Track group or unavailable table; select an individual data track.";
    s.detailCache[entry.id]=entry;
    for(auto& old:s.entries)if(old.id==entry.id)old=entry;
    if(s.selected.id==entry.id)onlineSelect(s,entry);
}
bool onlineNeedsResolution(const Online::Entry& entry){return entry.provider=="ucsc"&&entry.format.empty();}
void onlineBeginDiscovery(OnlineDialogState& s,const std::string& url,const std::string& kind){
    s.discovering=true;s.discoveryKind=kind;s.discoveryError.clear();
    if(kind!="size")s.lastApiRequest=std::chrono::steady_clock::now();
#ifdef __EMSCRIPTEN__
    EM_ASM({
        if($1){if(Module.gwOnlineFileSize)Module.gwOnlineFileSize(UTF8ToString($0));}
        else if(Module.gwDiscoverOnline)Module.gwDiscoverOnline(UTF8ToString($0));
    },url.c_str(),kind=="size");
#else
    s.discoveryTask=std::async(std::launch::async,[url,kind]{
        if(kind=="size")return std::to_string(Online::fetchFileSize(url));
        return Online::fetchCatalogue(url,std::make_shared<std::atomic<bool>>(false));
    });
#endif
}
void onlineInspect(OnlineDialogState& s,const Online::Entry& entry){
    if(!onlineNeedsResolution(entry)&&(Online::regionalFile(entry)||entry.sizeBytes>=0))return;
    auto key=onlineNeedsResolution(entry)?entry.id:entry.url;
    auto cached=s.detailCache.find(key);
    if(cached!=s.detailCache.end()){s.pendingInspection={};if(cached->second.format=="unavailable")onlineUnavailable(s,cached->second);else if(s.selected.id==entry.id)onlineSelect(s,cached->second);return;}
    if(s.discovering||(onlineNeedsResolution(entry)&&std::chrono::steady_clock::now()-s.lastApiRequest<std::chrono::seconds(1))){
        s.pendingInspection=entry;return;
    }
    s.pendingInspection={};s.inspecting=entry;s.discoveryKey=key;
    onlineBeginDiscovery(s,entry.url,onlineNeedsResolution(entry)?"resolve":"size");
}
void onlineInstallSearch(OnlineDialogState& s,std::vector<Online::Entry> entries,const std::string& query){
    s.entries.erase(std::remove_if(s.entries.begin(),s.entries.end(),[](const auto& e){return e.provider=="ucsc";}),s.entries.end());
    for(auto& entry:entries){auto cached=s.detailCache.find(entry.id);if(cached!=s.detailCache.end())entry=cached->second;}
    onlineMerge(s,entries);s.ucscQuery=query;
    s.discoveryInfo=std::to_string(entries.size())+" UCSC matches — select a track to check its file";
}
void onlineSearchUcsc(OnlineDialogState& s){
    try{
        const auto url=Online::ucscSearchURL(s.assembly,s.search);
        auto found=s.searchCache.find(url);
        if(found!=s.searchCache.end()){onlineInstallSearch(s,found->second,s.search);return;}
        if(s.discovering||std::chrono::steady_clock::now()-s.lastApiRequest<std::chrono::seconds(1))return;
        s.discoveryAssembly=s.assembly;s.discoveryQuery=s.search;s.discoveryKey=url;s.pendingInspection={};
        onlineBeginDiscovery(s,url,"search");
    }catch(const std::runtime_error& error){s.discoveryError=error.what();}
}
void onlinePollDiscovery(OnlineDialogState& s){
    if(s.discovering){
        try{
            std::string text;
#ifdef __EMSCRIPTEN__
            text=onlineBrowserResult(2);
#else
            if(s.discoveryTask.valid()&&s.discoveryTask.wait_for(std::chrono::seconds(0))==std::future_status::ready){text=s.discoveryTask.get();if(text.empty())throw std::runtime_error("Empty metadata response");}
#endif
            if(!text.empty()){
                s.discovering=false;
                if(text.rfind("error:",0)==0)throw std::runtime_error(text.substr(6));
                if(s.discoveryKind=="search"){
                    auto entries=Online::parseUcscSearch(text,s.discoveryAssembly);
                    for(auto& entry:entries){auto cached=s.detailCache.find(entry.id);if(cached!=s.detailCache.end())entry=cached->second;}
                    if(s.searchCache.size()>=8)s.searchCache.erase(s.searchCache.begin());
                    s.searchCache[s.discoveryKey]=entries;onlineInstallSearch(s,entries,s.discoveryQuery);
                }else{
                    auto entry=s.inspecting;
                    if(s.discoveryKind=="resolve"){
                        entry=Online::resolveUcscTrack(text,entry);
                        if(entry.format=="unavailable"){
                            onlineUnavailable(s,entry);
                            return;
                        }
                    }
                    else entry.sizeBytes=std::stoll(text);
                    if(s.detailCache.size()>=128)s.detailCache.erase(s.detailCache.begin());
                    s.detailCache[s.discoveryKey]=entry;
                    for(auto& old:s.entries)if(old.id==entry.id)old=entry;
                    if(s.selected.id==entry.id){
                        if(s.discoveryKind=="resolve")onlineSelect(s,entry);
                        else s.selected.sizeBytes=entry.sizeBytes;
                    }
                }
            }
        }catch(const std::runtime_error& error){s.discovering=false;s.discoveryError=error.what();
            if(s.discoveryKind=="resolve" && (s.discoveryError.find("HTTP 400")!=std::string::npos || s.discoveryError.find("HTTP 404")!=std::string::npos)) {
                onlineUnavailable(s,s.inspecting);s.discoveryError.clear();
            }
            if(s.discoveryKind=="search"&&s.discoveryError.find("2 MiB")!=std::string::npos)s.discoveryError+=" Try a more specific UCSC search.";}
        catch(...){s.discovering=false;s.discoveryError="Could not inspect online track metadata";}
    }
    if(!s.discovering&&!s.pendingInspection.id.empty()&&onlineFileDialogOpen){auto entry=s.pendingInspection;if(s.selected.id==entry.id)onlineInspect(s,entry);else s.pendingInspection={};}
}
void onlinePoll(Manager::GwPlot* plot, OnlineDialogState& s, bool& redraw) {
    if (s.catalogueLoading) {
        try {
#ifdef __EMSCRIPTEN__
            auto text = onlineBrowserResult(true);
            if (!text.empty()) {
                s.catalogueLoading = false;
                if (text.rfind("error:", 0) == 0) throw std::runtime_error(text.substr(6));
                onlineMerge(s, Online::parseCatalogue(text));
            }
#else
            if (s.catalogueTask.valid() && s.catalogueTask.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                s.catalogueLoading = false;
                onlineMerge(s, s.catalogueTask.get());
            }
#endif
        } catch (const std::runtime_error& error) { s.catalogueError = error.what(); }
        catch (...) { s.catalogueError = "Could not load the IGV genome list"; }
    }
    onlinePollDiscovery(s);
    if (!s.busy) return;
#ifdef __EMSCRIPTEN__
    auto text = onlineBrowserResult(false);
    if (text.empty()) return;
    s.busy = false;
    if (s.cancelled) { s.status = "Cancelled"; return; }
    if (text.rfind("error:", 0) == 0) { s.error = text.substr(6); s.status.clear(); return; }
    if (text.rfind("downloaded:",0)==0 || text.rfind("saved:",0)==0) {
        try {
            auto entries=Online::parseCatalogue(text.substr(text.rfind("saved:",0)==0?6:11),true);
            for(const auto& e:entries)if(e.category=="reference")plot->opts.myIni["genomes"][!e.assembly.empty()?e.assembly:(e.name.empty()?Online::resourceName(e.url):e.name)]=e.url;
            plot->opts.saveSettings();
        }catch(const std::runtime_error& error){s.error=error.what();}
        onlineRemember(plot,s);s.status=text.rfind("saved:",0)==0?"Downloaded table dump (not a drawable track)":"Downloaded and opened";redraw=true;
    }
    if (text == "opened") { onlineRemember(plot, s); s.status = "Opened"; redraw = true; }
#else
    if (!s.task.valid() || s.task.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    s.busy = false;
    try {
        auto entries = s.task.get();
        if (s.cancelled) { s.status = "Cancelled"; return; }
        if(!entries.empty()&&entries.front().format=="ucsc-raw"){onlineRemember(plot,s);s.status="Downloaded table dump (not a drawable track)";return;}
        Online::openGroup(plot, entries, s.asTrack, s.checkAssembly, true, s.asTable);
        if(s.downloading) {
            for(const auto& e:entries)if(e.category=="reference")plot->opts.myIni["genomes"][!e.assembly.empty()?e.assembly:(e.name.empty()?Online::resourceName(e.url):e.name)]=e.url;
            plot->opts.saveSettings();
        }
        onlineRemember(plot, s); s.status = s.downloading ? "Downloaded and opened" : "Opened"; redraw = true;
    } catch (const std::runtime_error& error) { s.error = error.what(); s.status.clear(); }
    catch (...) { s.error = "Could not open the online file"; s.status.clear(); }
#endif
}
void onlineStart(Manager::GwPlot* plot, OnlineDialogState& s, bool download) {
    auto entry = s.selected;
    if (entry.url != s.url) {
        entry = {}; entry.id = s.url; entry.name = Online::resourceName(s.url);
        entry.category = onlineCategories[s.category];
    }
    entry.url = s.url; entry.indexURL = s.index; entry.compressedIndexURL = s.gzi;
    if (!Online::httpURL(entry.url)) { s.error = "Enter an HTTP(S) file URL"; return; }
    try {
        auto resources = std::vector<Online::Entry>{entry};
        s.downloading=download;
        s.selected = entry; s.busy = true; s.cancelled = false; s.error.clear();
        s.status = download ? "Downloading data and indexes…" : "Checking header and index…";
#ifdef __EMSCRIPTEN__
        auto encoded = Online::encodeCatalogue(resources);
        EM_ASM({
            if($4 && Module.gwDownloadOnline) {Module.gwDownloadOnline(UTF8ToString($0),!!$1,!!$2,!!$3,UTF8ToString($5));return;}
            if(Module.gwOpenOnline) Module.gwOpenOnline(UTF8ToString($0),!!$1,!!$2,!!$3);
        }, encoded.c_str(), s.asTrack, s.checkAssembly, s.asTable, download, Online::downloadDirectory(plot->opts.ini_path).c_str());
#else
        auto reference = Online::contigsFor(plot, false), alignments = Online::contigsFor(plot, true);
        bool check = s.checkAssembly;
        s.progress=std::make_shared<Online::DownloadProgress>();
        auto progress=s.progress;auto directory=Online::downloadDirectory(plot->opts.ini_path);
        s.task = std::async(std::launch::async, [resources, reference, alignments, check, download, directory, progress]() mutable {
            if(download)for(auto& resource:resources)resource=Online::download(resource,directory,progress);
            if(!resources.empty()&&resources.front().format=="ucsc-raw")return resources;
            return Online::validateGroup(resources, reference, alignments, check);
        });
#endif
    } catch (const std::runtime_error& error) { s.busy = false; s.error = error.what(); }
    catch (...) { s.busy = false; s.error = "Could not prepare the online file"; }
}
}

static void drawImGuiOnlineFileDialog(Manager::GwPlot* plot, bool& redraw) {
    auto& s = onlineState();
    onlinePoll(plot, s, redraw);
    if (!onlineFileDialogOpen) return;
    if (!s.catalogueRequested) onlineFetchGenomes(s);
    if(s.lastReference!=plot->reference){s.lastReference=plot->reference;onlineCopy(s.assembly,sizeof(s.assembly),Online::referenceAssembly(plot));}
    if (s.persistencePath != plot->opts.ini_path) {
        s.persistencePath = plot->opts.ini_path; s.recent.clear();
        if (plot->opts.myIni.has("online-recent"))
            for (const auto& item : plot->opts.myIni["online-recent"]) try {
                auto entries = Online::parseCatalogue(item.second);
                s.recent.insert(s.recent.end(), entries.begin(), entries.end());
            } catch (...) {}
    }
    auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowSize(ImVec2(std::min(740.f, display.x-16), std::min(450.f, display.y-16)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Online file", &onlineFileDialogOpen, ImGuiWindowFlags_NoCollapse)) { ImGui::End(); return; }
    if (closeOnEscape(&onlineFileDialogOpen) && s.busy) onlineCancel(s);

    // Keep actions at the bottom; the body scrolls when the window is small.
    float footer = ImGui::GetFrameHeightWithSpacing() + ((s.selected.format=="ucsc-table"||s.selected.format=="ucsc-raw"||s.selected.format=="unavailable")?2*ImGui::GetTextLineHeightWithSpacing():0) + ((s.busy&&s.downloading)?ImGui::GetFrameHeightWithSpacing():0) + ((!s.error.empty() || !s.discoveryError.empty() || !s.status.empty()) ? 2 * ImGui::GetTextLineHeightWithSpacing() : 0);
    ImGui::BeginChild("##online_body", ImVec2(0, -footer), false);
    ImGui::BeginDisabled(s.busy);
    if (ImGui::BeginTabBar("##online_categories", ImGuiTabBarFlags_FittingPolicyScroll)) {
        const char* titles[] = {"Reference genome", "Alignment files", "Tracks", "Variant files"};
        for (int i : {0, 2, 3}) if (ImGui::BeginTabItem(titles[i])) {
            if (s.category != i) {
                s.category = i; s.selected = {}; s.pendingInspection={}; s.url[0] = s.index[0] = s.gzi[0] = 0;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::SetNextItemWidth(-1);
    bool searchSubmit=ImGui::InputTextWithHint("##online_search", "Search files or paste a URL below", s.search, sizeof(s.search),ImGuiInputTextFlags_EnterReturnsTrue);
    if(s.category==2){
        ImGui::SetNextItemWidth(110);
        ImGui::InputTextWithHint("Assembly","e.g. hg38",s.assembly,sizeof(s.assembly));ImGui::SameLine();
        ImGui::BeginDisabled(s.discovering||std::chrono::steady_clock::now()-s.lastApiRequest<std::chrono::seconds(1));
        if(ImGui::Button("Search UCSC")||searchSubmit)onlineSearchUcsc(s);
        ImGui::EndDisabled();
        if(s.discovering){ImGui::SameLine();ImGui::TextDisabled("Checking metadata…");}
    }
    ImGui::RadioButton("Catalogue", &s.list, 0); ImGui::SameLine();
    ImGui::RadioButton("Recent", &s.list, 1);
    if (s.catalogueLoading && s.category == 0) {
        ImGui::SameLine(); ImGui::TextDisabled("Loading genome list…");
    }
    if (!s.catalogueError.empty() && s.category == 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Retry genome list")) onlineFetchGenomes(s);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", s.catalogueError.c_str());
    }
    const auto& entries = s.list ? s.recent : s.entries;
    float controls = ImGui::GetFrameHeightWithSpacing() * (s.category == 3 ? 5 : 4);
    ImGui::BeginChild("##online_entries", ImVec2(0, std::max(100.f, ImGui::GetContentRegionAvail().y-controls)), true);
    if(ImGui::BeginTable("##online_file_table",1,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_RowBg)){
    ImGui::TableSetupColumn("File");
    for (const auto& entry : entries) {
        if(entry.provider=="ucsc"&&entry.assembly!=s.assembly)continue;
        if (entry.category != onlineCategories[s.category]) continue;
        std::string hay = entry.name+' '+entry.assembly+' '+entry.sample+' '+entry.technology+' '+entry.source+' '+entry.description+' '+entry.providerId;
        std::string needle = s.search;
        auto lower = [](unsigned char c) { return std::tolower(c); };
        std::transform(hay.begin(), hay.end(), hay.begin(), lower);
        std::transform(needle.begin(), needle.end(), needle.begin(), lower);
        if (!(entry.provider=="ucsc"&&!s.list&&s.ucscQuery==s.search)&&hay.find(needle)==std::string::npos)continue;
        ImGui::TableNextRow();ImGui::TableSetColumnIndex(0);ImGui::PushID(entry.id.c_str());
        if (ImGui::Selectable(entry.name.c_str(), s.selected.id == entry.id,ImGuiSelectableFlags_SpanAllColumns)){
            auto selected=entry;onlineSelect(s,selected);s.discoveryError.clear();onlineInspect(s,selected);
        }
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            for (const auto* value : {&entry.assembly, &entry.sample, &entry.technology, &entry.release, &entry.source, &entry.description, &entry.providerId})
                if (!value->empty()) ImGui::TextUnformatted(value->c_str());
            ImGui::TextUnformatted(Online::regionalFile(entry) ? "Indexed / streamed" : (Online::fileSizeLabel(entry.sizeBytes)+" download").c_str());
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    ImGui::EndTable();}
    ImGui::EndChild();
    ImGui::SetNextItemWidth(-75);
    if (ImGui::InputTextWithHint("File URL", "https://…", s.url, sizeof(s.url))) {
        s.selected = {};s.pendingInspection={};
        s.index[0] = s.gzi[0] = 0; // A pasted URL discovers its own companions.
    }
    if(ImGui::IsItemDeactivatedAfterEdit()&&Online::httpURL(s.url)){
        Online::Entry pasted;pasted.id=s.url;pasted.url=s.url;pasted.name=Online::resourceName(s.url);pasted.category=onlineCategories[s.category];
        s.selected=pasted;onlineInspect(s,pasted);
    }
    if (s.category == 3) {
        ImGui::Checkbox("Variant browser", &s.asTable); ImGui::SameLine();
        ImGui::Checkbox("Annotation track", &s.asTrack);
    }
    if (ImGui::CollapsingHeader("Configure paths")) {
        ImGui::SetNextItemWidth(-100);
        ImGui::InputTextWithHint("Index URL", "Automatic (.fai, .bai, .csi, .tbi)", s.index, sizeof(s.index));
        if (s.category == 0) {
            ImGui::SetNextItemWidth(-100);
            ImGui::InputTextWithHint("GZI URL", "Automatic for BGZF FASTA", s.gzi, sizeof(s.gzi));
        }
        if(s.category!=0)ImGui::Checkbox("Check reference assembly", &s.checkAssembly);
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
    if (!s.error.empty() || !s.discoveryError.empty() || !s.status.empty()) {
    ImGui::BeginChild("##online_status", ImVec2(0, 2 * ImGui::GetTextLineHeightWithSpacing()), false);
    if (!s.error.empty()) ImGui::TextWrapped("%s", s.error.c_str());
    else if(!s.discoveryError.empty())ImGui::TextWrapped("%s",s.discoveryError.c_str());
    else if (!s.status.empty()) ImGui::TextUnformatted(s.status.c_str());
    ImGui::EndChild();
    }
    if(!s.busy&&s.selected.format=="ucsc-table")ImGui::TextDisabled("Database track: use Download to create a local BED file.");
    if(!s.busy&&s.selected.format=="ucsc-raw")ImGui::TextDisabled("Download available as a table dump; this schema cannot be drawn.");
    if(!s.busy&&s.selected.format=="unavailable")ImGui::TextWrapped("%s",s.selected.description.c_str());
    if(s.busy&&s.downloading) {
        long long received=0,total=0;
#ifdef __EMSCRIPTEN__
        received=static_cast<long long>(EM_ASM_DOUBLE({return Module.gwDownloadReceived||0;}));
        total=static_cast<long long>(EM_ASM_DOUBLE({return Module.gwDownloadTotal||0;}));
#else
        if(s.progress){received=s.progress->received;total=s.progress->total;}
#endif
        auto label=Online::fileSizeLabel(received)+(total>0?" / "+Online::fileSizeLabel(total):" downloaded");
        ImGui::ProgressBar(total>0?float(double(received)/double(total)):-float(ImGui::GetTime()),ImVec2(-1,0),label.c_str());
    }
    ImGui::BeginDisabled(s.busy || !Online::httpURL(s.url) || s.selected.format=="unavailable" || onlineNeedsResolution(s.selected) || (s.category == 3 && !s.asTrack && !s.asTable));
    ImGui::BeginDisabled(s.selected.format=="ucsc-table"||s.selected.format=="ucsc-raw");
    if (ImGui::Button("Open", ImVec2(110, 0))) onlineStart(plot, s, false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Download", ImVec2(110, 0))) onlineStart(plot, s, true);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Close")) {
        if (s.busy) onlineCancel(s);
        onlineFileDialogOpen = false;
    }
    ImGui::End();
    if (!onlineFileDialogOpen && s.busy && !s.cancelled) onlineCancel(s);
}
}
