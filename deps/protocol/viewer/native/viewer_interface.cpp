#include "viewer_interface.h"
#ifndef __EMSCRIPTEN__
#include "plot_manager.h"
#include "plot_commands.h"
#include "viewer_settings.h"
#include "include/core/SkStream.h"
#include "include/encode/SkPngEncoder.h"
#include <algorithm>
#include <filesystem>
#include <regex>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <map>
#include <set>
#include <exception>

namespace GwViewer {
namespace {
void dimensions(int width, int height) {
    if (width < 200 || height < 100 || width > 8192 || height > 8192 ||
        std::int64_t(width) * height > 33554432)
        throw std::invalid_argument("Dimensions must be 200..8192 by 100..8192, at most 32 megapixels");
}
Utils::Region parseRegion(const std::string& value, faidx_t* fai) {
    static const std::regex pattern(R"(^([^:\s]+):([0-9]+)-([0-9]+)$)");
    std::smatch match;
    if (value.size() > 256 || !std::regex_match(value, match, pattern))
        throw std::invalid_argument("Use a 1-based inclusive region: chromosome:start-end");
    long long start, end;
    try { start = std::stoll(match[2]); end = std::stoll(match[3]); }
    catch (...) { throw std::invalid_argument("Region coordinates are too large"); }
    const auto chrom = match[1].str();
    const int length = faidx_seq_len(fai, chrom.c_str());
    if (length < 0 || start < 1 || end < start || end > length)
        throw std::invalid_argument("Region is outside the reference or chromosome is unknown");
    auto input = value;
    return Utils::parseRegion(input);
}
void validateCommand(const std::string& command) {
    // Syntax, chromosome shorthand and feature names belong to GW's parser.
    // Only operations outside this server's file/view lifecycle are excluded.
    std::istringstream input(command);
    std::string verb; input >> verb;
    static const std::set<std::string> unavailable = {
        "load", "save", "online", "snapshot", "s", "q", "quit",
        "mate", "grid", "settings", "general", "genomes", "interaction", "labelling",
        "navigation", "tracks", "thresholds", "keymap"
    };
    if (command.empty() || command.size() > 4096 || command.find_first_of("\r\n") != std::string::npos)
        throw std::invalid_argument("Invalid command length or embedded newline");
    if (unavailable.count(verb) || (verb == "sam" && command != "sam"))
        throw std::invalid_argument("This file or multi-region action is not available in the remote viewer");
}
}
struct Session::Impl {
    std::unique_ptr<Manager::GwPlot> plot;
    std::uint64_t revision = 1, pngRevision = 0;
    std::vector<std::uint8_t> png;
    std::string popups, output;
    std::map<int, Manager::GwPlot::ReadPopup> selections;
};
Config alignmentConfig(const Config& base, const std::string& path) {
    Config result=base;
    result.alignments={path}; result.tracks.clear();
    std::unique_ptr<htsFile, decltype(&hts_close)> input(sam_open(path.c_str(), "r"), hts_close);
    if(!input) throw std::invalid_argument("Cannot open uploaded BAM");
    if(hts_get_format(input.get())->format!=bam) throw std::invalid_argument("Upload must be a BAM file");
    std::unique_ptr<sam_hdr_t, decltype(&sam_hdr_destroy)> hdr(sam_hdr_read(input.get()), sam_hdr_destroy);
    if(!std::filesystem::exists(path+".bai") && !std::filesystem::exists(path+".csi"))
        throw std::invalid_argument("Waiting for index: supply the BAM's .bai or .csi file");
    std::unique_ptr<hts_idx_t, decltype(&hts_idx_destroy)> idx(sam_index_load(input.get(),path.c_str()),hts_idx_destroy);
    std::unique_ptr<faidx_t, decltype(&fai_destroy)> fai(fai_load3(base.reference.c_str(),nullptr,nullptr,0),fai_destroy);
    if(!hdr || !idx || !fai) throw std::invalid_argument("BAM needs a valid index and server reference");
    std::unique_ptr<bam1_t, decltype(&bam_destroy1)> read(bam_init1(),bam_destroy1);
    int tid=-1; int64_t start=0;
    if(sam_read1(input.get(),hdr.get(),read.get())>=0 && read->core.tid>=0) {
        tid=read->core.tid; start=std::max<int64_t>(0,read->core.pos-100);
    }
    if(tid<0) for(int i=0;i<sam_hdr_nref(hdr.get());++i) {
        if(faidx_seq_len(fai.get(),sam_hdr_tid2name(hdr.get(),i))>0) {tid=i;break;}
    }
    if(tid<0) throw std::invalid_argument("BAM has no chromosome matching the server reference");
    const char* chrom=sam_hdr_tid2name(hdr.get(),tid);
    const int64_t length=faidx_seq_len(fai.get(),chrom);
    if(length<=start)
        throw std::invalid_argument("BAM chromosome is incompatible with the server reference");
    result.region=std::string(chrom)+":"+std::to_string(start+1)+"-"+std::to_string(std::min<int64_t>(length,start+1000));
    return result;
}
Session::Session(const Config& config) : impl(std::make_unique<Impl>()) {
    dimensions(config.width, config.height);
    // Validate before GwPlot's legacy constructor, which still exits on bad inputs.
    if (!std::filesystem::is_regular_file(config.reference))
        throw std::invalid_argument("Reference must be an existing local FASTA");
    std::unique_ptr<faidx_t, decltype(&fai_destroy)> fai(
        fai_load3(config.reference.c_str(), nullptr, nullptr, 0), fai_destroy);
    if (!fai) throw std::invalid_argument("Reference needs a readable FASTA index");
    std::vector<Utils::Region> regions{parseRegion(config.region, fai.get())};
    for (const auto& path : config.alignments) {
        if (!std::filesystem::is_regular_file(path)) throw std::invalid_argument("Alignment file does not exist");
        std::unique_ptr<htsFile, decltype(&hts_close)> bam(sam_open(path.c_str(), "r"), hts_close);
        if (!bam) throw std::invalid_argument("Cannot open alignment");
        std::unique_ptr<sam_hdr_t, decltype(&sam_hdr_destroy)> hdr(sam_hdr_read(bam.get()), sam_hdr_destroy);
        std::unique_ptr<hts_idx_t, decltype(&hts_idx_destroy)> idx(sam_index_load(bam.get(), path.c_str()), hts_idx_destroy);
        if (!hdr || !idx) throw std::invalid_argument("Alignment needs a readable header and index");
        const auto& r = regions.front();
        const int tid = sam_hdr_name2tid(hdr.get(), r.chrom.c_str());
        if (tid < 0 || r.end > sam_hdr_tid2len(hdr.get(), tid))
            throw std::invalid_argument("Region chromosome or bounds are incompatible with an alignment");
    }
    Themes::IniOptions opts;
    opts.threads = 1;
    opts.dimensions = {config.width, config.height};
    opts.font_str = "Default";
    opts.font_size = 12;
    // GW's toggle commands restore thresholds from ini state. Populate defaults
    // in memory without reading or writing the host user's settings.
    opts.myIni["view_thresholds"]["edge_highlights"] = std::to_string(opts.edge_highlights);
    opts.myIni["view_thresholds"]["snp"] = std::to_string(opts.snp_threshold);
    opts.myIni["view_thresholds"]["small_indel"] = std::to_string(opts.small_indel_threshold);
    opts.myIni["view_thresholds"]["soft_clip"] = std::to_string(opts.soft_clip_threshold);
    opts.theme.setAlphas();
    std::string theme = "dark";
    opts.setTheme(theme);
    auto paths = config.alignments;
    auto tracks=config.tracks;
    for (const auto& path: tracks) if (!std::filesystem::is_regular_file(path))
        throw std::invalid_argument("Track must be an existing local file");
    impl->plot = std::make_unique<Manager::GwPlot>(config.reference, paths, opts, regions, tracks);
    auto& p = *impl->plot;
    p.drawToBackWindow = true;
    p.terminalOutput = false;
    p.showUIOverlay = true;
    p.setImageSize(config.width, config.height);
    if (!p.makeRasterSurface()) throw std::runtime_error("Could not allocate raster surface");
    frame(impl->revision); // Initial state includes the same layout metadata as its PNG.
}
Session::~Session() = default;
void Session::cancelInput() { impl->plot->pointerEvent("cancel",0,0,0,0,0,0); }
void Session::synchronizeSharedFrom(const Session& source, bool renderNow) {
    if (this == &source) return;
    auto& p = *impl->plot;
    const auto& other = *source.impl->plot;
    if (p.reference != other.reference)
        throw std::invalid_argument("Cannot share views between different references");
    // Closing files is shared, but each renderer must release its own handles.
    for (const auto& path:other.bam_paths)
        if(std::find(p.bam_paths.begin(),p.bam_paths.end(),path)==p.bam_paths.end())
            throw std::invalid_argument("Cannot share views between different datasets");
    for(int i=int(p.bam_paths.size())-1;i>=0;--i)
        if(std::find(other.bam_paths.begin(),other.bam_paths.end(),p.bam_paths[i])==other.bam_paths.end()) p.removeBam(i);
    for(int i=int(p.tracks.size())-1;i>=0;--i) {
        const auto& t=p.tracks[i];
        if(t.kind!=HGW::FType::INTRON && std::none_of(other.tracks.begin(),other.tracks.end(),[&](const auto& x){return x.path==t.path && x.kind==t.kind;})) p.removeTrack(i);
    }
    if(p.bam_paths!=other.bam_paths) throw std::invalid_argument("Dataset order mismatch");
    p.pointerEvent("cancel",0,0,0,0,0,0);
    const auto dimensions = p.opts.dimensions;
    p.clearCollections();
    p.opts = other.opts;
    p.opts.dimensions = dimensions;
    p.regions = other.regions;
    p.regionSelection = p.regions.empty() ? 0 : std::clamp(p.regionSelection,0,int(p.regions.size())-1);
    p.markers = other.markers;
    p.target_qname = other.target_qname;
    // Parser contains a reference to its renderer's output stream: rebind it.
    p.filters.clear();
    for (const auto& filter : other.filters) {
        p.filters.emplace_back(p.outStr);
        p.filters.back() = filter;
    }
    // File-backed tracks already belong to each renderer. Only synthetic intron
    // tracks need matching topology; never copy owning htslib handles.
    for (size_t bam = 0; bam < p.bam_paths.size(); ++bam) {
        auto has = [bam](const Manager::GwPlot& plot) {
            return std::any_of(plot.tracks.begin(),plot.tracks.end(),[bam](const auto& t) { return t.kind == HGW::FType::INTRON && t.bamIndex == int(bam); });
        };
        if (has(p) != has(other)) {
            p.inputText = "introns " + std::to_string(bam);
            p.commandProcessed();
        }
    }
    for (auto& track : p.tracks) {
        for (const auto& original : other.tracks) {
            if (track.kind == original.kind && track.path == original.path && track.bamIndex == original.bamIndex) {
                track.height_fraction = original.height_fraction;
                track.name = original.name;
                track.faceColour = original.faceColour;
                track.shadedFaceColour = original.shadedFaceColour;
                break;
            }
        }
    }
    p.fonts.setTypeface(p.opts.font_str,p.opts.font_size);
    p.fonts.setOverlayHeight(p.monitorScale);
    p.processed = false; p.tracksLayoutDirty = true;
    p.imageCache.clear(); p.clearImageCacheQueue(); p.clearZoomCache();
    impl->selections.clear(); impl->popups.clear(); impl->output.clear();
    ++impl->revision;
    if (renderNow) frame(impl->revision);
}
State Session::state() const {
    const auto& p = *impl->plot;
    State s;
    s.revision = impl->revision;
    s.width = p.fb_width; s.height = p.fb_height; s.theme = p.opts.theme_str;
    s.popups = impl->popups; s.output = impl->output;
    std::ostringstream bars;
    bars << p.trackY << ' ' << p.gap << ' ' << p.drawLine << ' ' << p.refSpace << ' ' << p.sliderSpace << '\n';
    for (const auto& cl : p.collections) {
        const int rows = std::max(1, p.opts.ylim);
        const int estimate = cl.covArr.empty() ? int(cl.readQueue.size()) : cl.maxCoverage * 2;
        const int maximum = std::max(rows, std::max(estimate - rows, cl.vScroll + rows));
        const bool live = cl.region && cl.region->end - cl.region->start < p.opts.low_memory && cl.readQueue.size() <= 10000;
        bars << cl.xOffset << ' ' << cl.yOffset << ' ' << cl.regionPixels << ' ' << maximum << ' ' << cl.vScroll << ' ' << live << '\n';
    }
    s.scrollbars = bars.str();
    std::ostringstream trackLayout;
    float top=p.fb_height-p.sliderSpace-p.totalTabixY;
    for (const auto& track:p.tracks) {
        trackLayout << top << ' ' << track.px_height << ' ' << std::quoted(std::filesystem::path(track.path).filename().string()) << '\n';
        top+=track.px_height;
    }
    s.tracks=trackLayout.str();
    s.pixelRatio = p.monitorScale; s.settings = renderSettings(p.opts);
    for (const auto& r : p.regions) s.regions.push_back(r.chrom + ":" + std::to_string(r.start) + "-" + std::to_string(r.end));
    s.activeRegion = p.regions.empty() ? 0 : std::clamp(p.regionSelection,0,int(p.regions.size())-1);
    s.region = s.regions.empty() ? "" : s.regions[s.activeRegion];
    for (const auto& path : p.bam_paths) s.alignments.push_back(std::filesystem::path(path).filename().string());
    return s;
}
State Session::apply(std::uint64_t expectedRevision, const Action& action) {
    if (expectedRevision != impl->revision) throw StaleView();
    auto& p = *impl->plot;
    struct SelectionGuard {
        Manager::GwPlot& plot; int previous; int exceptions=std::uncaught_exceptions();
        ~SelectionGuard() { if (std::uncaught_exceptions() > exceptions) plot.regionSelection=previous; }
    } guard{p,p.regionSelection};
    if (action.regionIndex >= 0) {
        if ((!p.regions.empty() && action.regionIndex >= int(p.regions.size())) || (p.regions.empty() && action.regionIndex != 0))
            throw std::invalid_argument("Region index is out of range");
        p.regionSelection=action.regionIndex;
    }
    const size_t previousRegionCount=p.regions.size();
    std::string newPopups;
    p.lastCommandOutputAnsi.clear(); p.showCommandStatus = false;
    if (action.type == Action::Type::Input) {
        // Hit testing must use the layout of the frame actually displayed.
        if (impl->pngRevision != impl->revision) frame(impl->revision);
        std::istringstream input(action.value);
        std::string kind, extra; double x, y, dx, dy; int button, mods;
        if (!(input >> kind >> x >> y >> button >> mods >> dx >> dy) || (input >> extra) ||
            !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(dx) || !std::isfinite(dy) ||
            x < 0 || y < 0 || x > p.fb_width || y > p.fb_height ||
            button != GLFW_MOUSE_BUTTON_LEFT || mods < 0 || mods > 15 ||
            std::abs(dx) > 100 || std::abs(dy) > 100 ||
            (kind != "press" && kind != "release" && kind != "move" && kind != "wheel" && kind != "cancel"))
            throw std::invalid_argument("Invalid pointer event");
        p.pointerEvent(kind, x, y, button, mods, dx, dy);
        // Original popup models, drained after each event; IDs remain stable.
        std::ostringstream pop;
        for (const auto& v : p.readPopups) impl->selections[v.uid] = v;
        while (impl->selections.size() > 64) impl->selections.erase(impl->selections.begin());
        for (const auto& v : p.readPopups) pop << "read " << v.uid << ' ' << v.x << ' ' << v.y << ' ' << std::quoted(v.ansi) << ' ' << std::quoted(v.sam) << ' ' << std::quoted(v.qname) << '\n';
        for (const auto& v : p.covPopups) pop << "cov " << v.uid << ' ' << v.x << ' ' << v.y << ' ' << std::quoted(v.ansi) << ' ' << std::quoted(v.chromPos) << '\n';
        for (const auto& v : p.trackPopups) pop << "track " << v.uid << ' ' << v.x << ' ' << v.y << ' ' << std::quoted(v.ansi) << ' ' << std::quoted(v.details) << ' ' << v.isVcf << '\n';
        for (const auto& v : p.seqPopups) pop << "seq " << v.uid << ' ' << v.x << ' ' << v.y << ' ' << std::quoted(v.ansi) << ' ' << int(v.kind) << ' ' << v.aaFrame << ' ' << v.aaStrand << ' ' << v.aaCode << '\n';
        newPopups = pop.str();
        p.readPopups.clear(); p.covPopups.clear(); p.trackPopups.clear(); p.seqPopups.clear();
    } else if (action.type == Action::Type::Navigate) {
        auto region = parseRegion(action.value, p.fai);
        for (auto* header : p.headers) {
            const int tid = sam_hdr_name2tid(header, region.chrom.c_str());
            if (tid < 0 || region.end > sam_hdr_tid2len(header, tid))
                throw std::invalid_argument("Region chromosome or bounds are incompatible with an alignment");
        }
        // Use GW's normal navigation path so buffered reads/row positions are retained.
        p.inputText = action.value;
        p.commandProcessed();
    } else if (action.type == Action::Type::Command) {
        if (action.value.rfind("track-resize ",0)==0) {
            std::istringstream input(action.value.substr(13)); int index; float y; std::string extra;
            if (!(input>>index>>y) || (input>>extra) || index<0 || index>=int(p.tracks.size()) || !std::isfinite(y) || y<0 || y>p.fb_height)
                throw std::invalid_argument("Invalid track separator");
            p.resizeTrackBoundary(index,y); p.inputText.clear();
        } else if (action.value.rfind("key ",0)==0) {
            std::istringstream input(action.value.substr(4)); int key,mods; std::string extra;
            const std::set<int> keys={GLFW_KEY_LEFT,GLFW_KEY_RIGHT,GLFW_KEY_UP,GLFW_KEY_DOWN,GLFW_KEY_PAGE_UP,GLFW_KEY_PAGE_DOWN,GLFW_KEY_LEFT_BRACKET,GLFW_KEY_RIGHT_BRACKET,GLFW_KEY_L,GLFW_KEY_F,GLFW_KEY_R,GLFW_KEY_MINUS,GLFW_KEY_EQUAL};
            if (!(input>>key>>mods) || (input>>extra) || !keys.count(key) || (mods!=0 && mods!=GLFW_MOD_CONTROL && !(mods==GLFW_MOD_SHIFT && key==GLFW_KEY_EQUAL)))
                throw std::invalid_argument("Unsupported canvas hotkey");
            p.viewKey(key,mods); p.inputText.clear();
        } else if (action.value.rfind("rm bam",0)==0 || action.value.rfind("remove bam",0)==0 ||
                   action.value.rfind("rm track",0)==0 || action.value.rfind("remove track",0)==0) {
            const auto token=action.value.substr(action.value.find(' ')+1);
            const bool bam=token.rfind("bam",0)==0;
            std::istringstream input(token.substr(bam?3:5)); int index;std::string extra;
            if(!(input>>index) || (input>>extra) || index<0 || index>=int(bam?p.bam_paths.size():p.tracks.size()))
                throw std::invalid_argument("Invalid file index");
            auto command=action.value;Commands::run_command_map(&p,command,p.outStr);p.inputText.clear();
        } else if (action.value.rfind("rm ",0)==0 || action.value.rfind("remove ",0)==0) {
            std::istringstream input(action.value.substr(action.value.find(' ')+1)); int index; std::string extra;
            if (!(input>>index) || (input>>extra) || index<0 || index>=int(p.regions.size())) throw std::invalid_argument("Invalid region index");
            p.removeRegion(index); p.inputText.clear();
        } else if (action.value.rfind("add ",0)==0) {
            if (p.regions.size()>=16) throw std::invalid_argument("At most 16 regions are supported");
            auto value=action.value.substr(4);
            auto region=Utils::parseRegion(value);
            if (faidx_seq_len(p.fai,region.chrom.c_str()) < 0) throw std::invalid_argument("Unknown chromosome");
            p.inputText=action.value;
        } else if (action.value.rfind("vscroll ", 0) == 0) {
            std::istringstream input(action.value.substr(8)); int index, offset; std::string extra;
            if (!(input >> index >> offset) || (input >> extra) || index < 0 || index >= int(p.collections.size()) || offset < 0 || offset > 10000000)
                throw std::invalid_argument("Invalid alignment scroll offset");
            p.setCollectionVScroll(index, offset);
            p.inputText.clear();
        } else if (action.value.rfind("read-popup ", 0) == 0) {
            std::istringstream command(action.value.substr(11));
            int uid; std::string verb;
            if (!(command >> uid)) throw std::invalid_argument("Invalid selection");
            std::getline(command >> std::ws, verb);
            auto selected = impl->selections.find(uid);
            if (selected == impl->selections.end() || (verb != "find" && verb != "mate"))
                throw std::invalid_argument("Selection expired or unsupported read action");
            const auto& rp = selected->second;
            p.selectedAlign = rp.sam; p.target_qname = rp.qname;
            p.regionSelection = rp.regionSelection;
            p.inputText = verb == "find" ? "find " + rp.qname : verb;
        } else { validateCommand(action.value); p.inputText = action.value; }
        p.commandProcessed();
    } else if (action.type == Action::Type::Settings) {
        applyRenderSettings(p.opts, action.value);
        p.fonts.setTypeface(p.opts.font_str, p.opts.font_size);
        p.fonts.setOverlayHeight(p.monitorScale);
        p.processed = false;
        p.tracksLayoutDirty = true;
        p.imageCache.clear(); p.clearImageCacheQueue(); p.clearZoomCache();
    } else {
        dimensions(action.width, action.height);
        if (!std::isfinite(action.pixelRatio) || action.pixelRatio < .25 || action.pixelRatio > 8)
            throw std::invalid_argument("Pixel ratio must be .25..8");
        if (action.width == p.fb_width && action.height == p.fb_height && action.pixelRatio == p.monitorScale) return state();
        p.pointerEvent("cancel",0,0,0,0,0,0);
        // Allocate exact physical dimensions, including fractional-DPR displays.
        p.monitorScale = 1;
        p.opts.dimensions = {action.width, action.height};
        p.makeRasterSurface();
        p.monitorScale = action.pixelRatio;
        p.opts.dimensions = {static_cast<int>(std::lround(action.width / action.pixelRatio)),
                             static_cast<int>(std::lround(action.height / action.pixelRatio))};
        p.setImageSize(action.width, action.height);
        p.processed = false;
        p.tracksLayoutDirty = true;
        p.imageCache.clear(); p.clearImageCacheQueue(); p.clearZoomCache();
    }
    if (p.regions.size()!=previousRegionCount) impl->selections.clear();
    impl->popups = std::move(newPopups);
    impl->output = p.showCommandStatus ? p.lastCommandOutputAnsi : "";
    p.flushLog();
    ++impl->revision;
    frame(impl->revision);
    return state();
}
const std::vector<std::uint8_t>& Session::frame(std::uint64_t expectedRevision) {
    if (expectedRevision != impl->revision) throw StaleView();
    if (impl->pngRevision != impl->revision) {
        auto& p = *impl->plot;
        p.syncImageCacheQueue();
        p.drawScreen(false);
        // Interactive frames favor encoding latency over the smallest file.
        // A fixed Up filter avoids libpng testing every filter on every row;
        // this remains lossless and does not alter normal GW/gwplot exports.
        SkPixmap pixels;
        SkDynamicMemoryWStream stream;
        SkPngEncoder::Options options;
        options.fZLibLevel = 3;
        options.fFilterFlags = SkPngEncoder::FilterFlag::kUp;
        if (!p.rasterSurface->peekPixels(&pixels) || !SkPngEncoder::Encode(&stream, pixels, options))
            throw std::runtime_error("PNG encoding failed");
        const auto png = stream.detachAsData();
        if (!png || !png->size()) throw std::runtime_error("PNG encoding failed");
        const auto* bytes = static_cast<const std::uint8_t*>(png->data());
        impl->png.assign(bytes, bytes + png->size());
        impl->pngRevision = impl->revision;
        p.redraw = false;
    }
    return impl->png;
}
}
#endif
