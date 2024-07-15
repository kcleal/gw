
#include <array>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <mutex>
#include <string>
#include <cstdio>
#include <vector>

#ifdef __APPLE__
    #include <OpenGL/gl.h>
#elif defined(__linux__)
    #include <GL/gl.h>
    #include <GL/glx.h>
#endif

#include "htslib/faidx.h"
#include "htslib/hts.h"
#include "htslib/sam.h"

#include <GLFW/glfw3.h>
#define SK_GL
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"
#include "include/core/SkDocument.h"

#include "ankerl_unordered_dense.h"
#include "drawing.h"
#include "plot_manager.h"
#include "menu.h"
#include "segments.h"
#include "termcolor.h"
#include "term_out.h"
#include "themes.h"
#include "window_icon.h"


using namespace std::literals;

namespace Manager {

    std::mutex g_mutex;

    void HiddenWindow::init(int width, int height) {
        if (!glfwInit()) {
            std::cerr << "ERROR: could not initialize GLFW3" << std::endl;
            std::exit(-1);
        }
        glfwWindowHint(GLFW_STENCIL_BITS, 8);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        window = glfwCreateWindow(width, height, "GW", NULL, NULL);
        if (!window) {
            std::cerr << "ERROR: could not create back window with GLFW3" << std::endl;
            glfwTerminate();
            std::exit(-1);
        }
        glfwMakeContextCurrent(window);

    }

    EXPORT GwPlot::GwPlot(std::string reference, std::vector<std::string> &bampaths, Themes::IniOptions &opt,
                          std::vector<Utils::Region> &regions,
                          std::vector<std::string> &track_paths) {
        this->reference = reference;
        this->bam_paths = bampaths;
        this->regions = regions;
        this->opts = opt;
        frameId = 0;
        redraw = true;
        processed = false;
        resizeTriggered = false;
        regionSelectionTriggered = false;
        drawLine = false;
        captureText = false;
        drawToBackWindow = false;
        textFromSettings = false;
        terminalOutput = true;
        monitorScale = 1;
        xPos_fb = 0;
        yPos_fb = 0;
        fonts = Themes::Fonts();
        fonts.setTypeface(opt.font_str, opt.font_size);
        if (!reference.empty()) {
            fai = fai_load(reference.c_str());
            if (fai == nullptr) {
                std::cerr << "Error: reference genome could not be opened " << reference << std::endl;
                std::exit(-1);
            }
        }
        for (auto &fn: bampaths) {
            htsFile *f = sam_open(fn.c_str(), "r");
            hts_set_fai_filename(f, reference.c_str());
            hts_set_threads(f, opt.threads);
            bams.push_back(f);
            sam_hdr_t *hdr_ptr = sam_hdr_read(f);
            headers.push_back(hdr_ptr);
            hts_idx_t *idx = sam_index_load(f, fn.c_str());
            indexes.push_back(idx);
        }

        if (!opts.genome_tag.empty() && !opts.ini_path.empty() && opts.myIni["tracks"].has(opts.genome_tag)) {
            std::vector<std::string> track_paths_temp = Utils::split(opts.myIni["tracks"][opts.genome_tag], ',');
            tracks.reserve(track_paths.size() + track_paths_temp.size());
            for (const auto &trk_item: track_paths_temp) {
                if (!Utils::is_file_exist(trk_item)) {
                    if (terminalOutput) {
                        std::cerr << "Warning: track file does not exists - " << trk_item << std::endl;
                    }
                } else {
                    tracks.emplace_back() = HGW::GwTrack();
                    tracks.back().genome_tag = opts.genome_tag;
                    tracks.back().open(trk_item, true);
                    tracks.back().variant_distance = &opts.variant_distance;
                    if (tracks.back().kind == HGW::FType::BIGWIG) {
                        tracks.back().faceColour = opts.theme.fcBigWig;
                    } else {
                        tracks.back().faceColour = opts.theme.fcTrack;
                    }
                }
            }
        } else {
            tracks.reserve(track_paths.size());
        }
        for (const auto &tp: track_paths) {
            tracks.emplace_back() = HGW::GwTrack();
            tracks.back().open(tp, true);
            tracks.back().variant_distance = &opts.variant_distance;
            if (tracks.back().kind == HGW::FType::BIGWIG) {
                tracks.back().faceColour = opts.theme.fcBigWig;
            } else {
                tracks.back().faceColour = opts.theme.fcTrack;
            }
        }
        samMaxY = 0;
        yScaling = 0;
        covY = totalCovY = totalTabixY = tabixY = 0;
        captureText = shiftPress = ctrlPress = processText = tabBorderPress = false;
        xDrag = xOri = yDrag = yOri = -1000000;
        lastX = lastY = -1;
        commandIndex = 0;
        regionSelection = 0;
        variantFileSelection = 0;
        commandToolTipIndex = -1;
        mode = Show::SINGLE;
        if (opts.threads > 1) {
            pool.reset(opts.threads);
        }
        triggerClose = false;
        sortReadsBy = Manager::SortType::NONE;
    }

    GwPlot::~GwPlot() {
        if (window != nullptr && !drawToBackWindow) {
            glfwDestroyWindow(window);
        }
        if (backWindow != nullptr && drawToBackWindow) {
            glfwDestroyWindow(backWindow);
        }
        glfwTerminate();
        for (auto &bm: bams) {
            hts_close(bm);
        }
        for (auto &hd: headers) {
            bam_hdr_destroy(hd);
        }
        for (auto &idx: indexes) {
            hts_idx_destroy(idx);
        }
//        if (fai != nullptr) {
//            fai_destroy(fai);
//        }
    }

    void ErrorCallback(int, const char *err_str) {
        std::cerr << "GLFW Error: " << err_str << std::endl;
    }

    int GwPlot::makeRasterSurface() {
        SkImageInfo info = SkImageInfo::MakeN32Premul(opts.dimensions.x * monitorScale,
                                                      opts.dimensions.y * monitorScale);
        size_t rowBytes = info.minRowBytes();
        size_t size = info.computeByteSize(rowBytes);
        this->pixelMemory.resize(size);
        this->rasterSurface = SkSurface::MakeRasterDirect(
                info, &pixelMemory[0], rowBytes);
        rasterCanvas = rasterSurface->getCanvas();
        rasterSurfacePtr = &rasterSurface;
        return pixelMemory.size();
    }

    void GwPlot::init(int width, int height) {

        glfwSetErrorCallback(ErrorCallback);

        if (!glfwInit()) {
            std::cerr << "ERROR: could not initialize GLFW3" << std::endl;
            std::terminate();
        }


//        GLFWmonitor* primary = glfwGetPrimaryMonitor();
//        const GLFWvidmode* mode = glfwGetVideoMode(primary);
//
//        glfwWindowHint(GLFW_RED_BITS, mode->redBits);
//        glfwWindowHint(GLFW_GREEN_BITS, mode->greenBits);
//        glfwWindowHint(GLFW_BLUE_BITS, mode->blueBits);
//        glfwWindowHint(GLFW_STENCIL_BITS, 8);
//        glfwWindowHint(GLFW_REFRESH_RATE, mode->refreshRate);

//        std::cerr << "Width: " << mode->width << " Height: " << mode->height << " redBits: " << mode->redBits << " greenBits: " << mode->greenBits << " blueBits: " << mode->blueBits << " refreshRate: " << mode->refreshRate << std::endl;
//         Turn this off for non-remote connections?
//        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
//        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
//        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
//        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
//        glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#ifndef __APPLE__
        glfwWindowHint(GLFW_CONTEXT_CREATION_API , GLFW_EGL_CONTEXT_API);
#endif
        window = glfwCreateWindow(width, height, "GW", NULL, NULL);

        if (!window) {
            glfwTerminate();
            std::cerr << "ERROR: glfwCreateWindow failed\n";
            std::exit(-1);
        }
#if defined(_WIN32) || defined(_WIN64)
        GLFWimage iconimage;
        getWindowIconImage(&iconimage);
          glfwSetWindowIcon(window, 1, &iconimage);
#endif

        // https://stackoverflow.com/questions/7676971/pointing-to-a-function-that-is-a-class-member-glfw-setkeycallback/28660673#28660673
        glfwSetWindowUserPointer(window, this);

        auto func_key = [](GLFWwindow *w, int k, int s, int a, int m) {
            static_cast<GwPlot *>(glfwGetWindowUserPointer(w))->keyPress(k, s, a, m);
        };
        glfwSetKeyCallback(window, func_key);

        auto func_drop = [](GLFWwindow *w, int c, const char **paths) {
            static_cast<GwPlot *>(glfwGetWindowUserPointer(w))->pathDrop(c, paths);
        };
        glfwSetDropCallback(window, func_drop);

        auto func_mouse = [](GLFWwindow *w, int b, int a, int m) {
            static_cast<GwPlot *>(glfwGetWindowUserPointer(w))->mouseButton(b, a, m);
        };
        glfwSetMouseButtonCallback(window, func_mouse);

        auto func_pos = [](GLFWwindow *w, double x, double y) {
            static_cast<GwPlot *>(glfwGetWindowUserPointer(w))->mousePos(x, y);
        };
        glfwSetCursorPosCallback(window, func_pos);

        auto func_scroll = [](GLFWwindow *w, double x, double y) {
            static_cast<GwPlot *>(glfwGetWindowUserPointer(w))->scrollGesture(x, y);
        };
        glfwSetScrollCallback(window, func_scroll);

        auto func_resize = [](GLFWwindow *w, int x, int y) {
            static_cast<GwPlot *>(glfwGetWindowUserPointer(w))->windowResize(x, y);
        };
        glfwSetWindowSizeCallback(window, func_resize);

        if (!window) {
            std::cerr << "ERROR: could not create window with GLFW3" << std::endl;
            glfwTerminate();
            std::exit(-1);
        }
        glfwMakeContextCurrent(window);
        setGlfwFrameBufferSize();

        if (rasterSurfacePtr == nullptr) {
            makeRasterSurface();
        }

    }

    void GwPlot::initBack(int width, int height) {
        if (!glfwInit()) {
            std::cerr << "ERROR: could not initialize GLFW3" << std::endl;
            std::exit(-1);
        }

        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        backWindow = glfwCreateWindow(width, height, "GW", NULL, NULL);
        if (!backWindow) {
            std::cerr << "ERROR: could not create back window with GLFW3" << std::endl;
            glfwTerminate();
            std::terminate();
        }
        glfwMakeContextCurrent(backWindow);
        drawToBackWindow = true;

        setGlfwFrameBufferSize();
    }

    void GwPlot::fetchRefSeq(Utils::Region & rgn) {
        // This will be called for every new region visited
        assert (rgn.start >= 0);
        rgn.regionLen = rgn.end - rgn.start;
        if (rgn.chromLen == 0) {
            rgn.chromLen = faidx_seq_len(fai, rgn.chrom.c_str());
        }
        if (rgn.regionLen < opts.snp_threshold || rgn.regionLen < 20000) {
            rgn.refSeq = faidx_fetch_seq(fai, rgn.chrom.c_str(), rgn.start, rgn.end - 1, &rgn.regionLen - 1);
            if (rgn.end <= rgn.chromLen) {
                rgn.refSeqLen = rgn.end - rgn.start;
            } else {
                rgn.refSeqLen = rgn.chromLen - rgn.start;
            }
        } else {
            rgn.refSeqLen = 0;
        }
    }

    void GwPlot::fetchRefSeqs() {
        for (auto &rgn: regions) {
            fetchRefSeq(rgn);
        }
    }

    void GwPlot::addBam(std::string &bam_path) {
        htsFile *f = sam_open(bam_path.c_str(), "r");
        hts_set_fai_filename(f, reference.c_str());
        hts_set_threads(f, opts.threads);
        bams.push_back(f);
        sam_hdr_t *hdr_ptr = sam_hdr_read(f);
        headers.push_back(hdr_ptr);
        hts_idx_t *idx = sam_index_load(f, bam_path.c_str());
        indexes.push_back(idx);
    }

    void GwPlot::addVariantTrack(std::string &path, int startIndex, bool cacheStdin, bool useFullPath) {
        std::string variantFilename;
        if (!useFullPath) {
            std::filesystem::path fsp(path);
#if defined(_WIN32) || defined(_WIN64)
            const wchar_t* pc = fsp.filename().c_str();
        std::wstring ws(pc);
        std::string p(ws.begin(), ws.end());
        variantFilename = p;
#else
            variantFilename = fsp.filename();
#endif
        } else {
            variantFilename = path;
        }

        std::shared_ptr<ankerl::unordered_dense::map < std::string, Utils::Label>>
        inLabels = std::make_shared<ankerl::unordered_dense::map < std::string, Utils::Label>>
        (inputLabels[variantFilename]);
        std::shared_ptr<ankerl::unordered_dense::set < std::string>>
        sLabels = std::make_shared<ankerl::unordered_dense::set < std::string>>
        (seenLabels[variantFilename]);

        variantTracks.push_back(
                HGW::GwVariantTrack(path, cacheStdin, &opts, startIndex,
                                    labelChoices,
                                    inLabels,
                                    sLabels)
        );
    }

    void GwPlot::setOutLabelFile(const std::string &path) {
        outLabelFile = path;
    }

    void GwPlot::saveLabels() {
        if (outLabelFile.empty()) {
            return;
        }
        std::string dateStr = Utils::dateTime();
        std::ofstream f;
        f.open(outLabelFile);
        f << "#chrom\tpos\tvariant_ID\tlabel\tvar_type\tlabelled_date\tvariant_filename\tcomment\n";
        for (auto &vf: variantTracks) {
            std::string fileName;
            if (vf.type != HGW::TrackType::IMAGES) {
                std::filesystem::path fsp(vf.path);
#if defined(_WIN32) || defined(_WIN64)
                const wchar_t* pc = fsp.filename().c_str();
            std::wstring ws(pc);
            std::string p(ws.begin(), ws.end());
            fileName = p;
#else
                fileName = fsp.filename();
#endif
            }
            int i = 0;
            for (auto &l: vf.multiLabels) {
                if (vf.type == HGW::TrackType::IMAGES) {
                    std::filesystem::path fsp(vf.image_glob[i]);
#if defined(_WIN32) || defined(_WIN64)
                    const wchar_t* pc = fsp.filename().c_str();
            std::wstring ws(pc);
            std::string p(ws.begin(), ws.end());
            fileName = p;
#else
                    fileName = fsp.filename();
#endif
                }
                Utils::saveLabels(l, f, dateStr, fileName);
                i += 1;
            }
        }
        f.close();
    }

    void GwPlot::addIdeogram(std::string path) {
        ideogram_path = path;
        Themes::readIdeogramFile(path, ideogram, opts.theme);
    }

    void GwPlot::addFilter(std::string &filter_str) {
        Parse::Parser p = Parse::Parser(outStr);
        if (p.set_filter(filter_str, bams.size(), regions.size()) > 0) {
            filters.push_back(p);
        }
    }

    void GwPlot::setLabelChoices(std::vector<std::string> &labels) {
        labelChoices = labels;
    }

    void GwPlot::setVariantSite(std::string &chrom, long start, std::string &chrom2, long stop) {
        this->clearCollections();
        long rlen = stop - start;
        bool isTrans = chrom != chrom2;
        if (!isTrans && rlen <= opts.split_view_size) {
            regions.resize(1);
            regions[0].chrom = chrom;
            regions[0].start = (1 > start - opts.pad) ? 1 : start - opts.pad;
            regions[0].end = stop + opts.pad;
            regions[0].markerPos = start;
            regions[0].markerPosEnd = stop;
            if (opts.tlen_yscale) {
                opts.max_tlen = stop - start;
            }
        } else {
            regions.resize(2);
            regions[0].chrom = chrom;
            regions[0].start = (1 > start - opts.pad) ? 1 : start - opts.pad;
            regions[0].end = start + opts.pad;
            regions[0].markerPos = start;
            regions[0].markerPosEnd = start;
            regions[1].chrom = chrom2;
            regions[1].start = (1 > stop - opts.pad) ? 1 : stop - opts.pad;
            regions[1].end = stop + opts.pad;
            regions[1].markerPos = stop;
            regions[1].markerPosEnd = stop;
            if (opts.tlen_yscale) {
                opts.max_tlen = 1500;
            }
        }
    }

    // read tracks and data from session, other options already read IniOptions::getOptionsFromSessionIni
    void GwPlot::loadSession() {
        mINI::INIFile file(opts.session_file);
        file.read(opts.seshIni);
        if (!opts.seshIni.has("data") || !opts.seshIni.has("show")) {
            std::cerr << "Error: session file is missing 'data' heading. Invalid session file\n";
            std::exit(-1);
        }
        reference = opts.seshIni["data"]["genome_path"];
        opts.genome_tag = opts.seshIni["data"]["genome_tag"];
        if (opts.seshIni["data"].has("ideogram_path")) {
            if (std::filesystem::exists(opts.seshIni["data"]["ideogram_path"])) {
                addIdeogram(opts.seshIni["data"]["ideogram_path"]);
            } else {
                std::cerr << "Ideogram file does not exists: " << opts.seshIni["data"]["ideogram_path"] << std::endl;
            }
        }
        if (opts.seshIni["data"].has("mode")) {
            mode = (opts.seshIni["data"]["mode"] == "tiled") ? Show::TILED : Show::SINGLE;
        }
        if (!reference.empty()) {
            fai = fai_load(reference.c_str());
            if (fai == nullptr) {
                std::cerr << "Error: reference genome could not be opened " << reference << std::endl;
                std::exit(-1);
            }
            std::cout << "Genome:  " << reference << std::endl;
        }

        if (opts.seshIni.has("labelling") && opts.seshIni["labelling"].has("labels")) {
            std::vector<std::string> labels = Utils::split_keep_empty_str(opts.seshIni["labelling"]["labels"], ',');
            setLabelChoices(labels);
        }

        bam_paths.clear();
        tracks.clear();
        regions.clear();
        filters.clear();
        variantTracks.clear();
        bams.clear();
        headers.clear();
        indexes.clear();
        collections.clear();
        for (const auto &item: opts.seshIni["data"]) {
            if (Utils::startsWith(item.first, "ideogram") || Utils::startsWith(item.first, "genome")) {
                continue;
            }
            if (!std::filesystem::exists(item.second)) {
                std::cerr << item.first << " data file does not exists: " << item.second << std::endl;
                continue;
            }
            if (Utils::startsWith(item.first, "bam")) {
                bam_paths.push_back(item.second);
            } else if (Utils::startsWith(item.first, "track")) {
                tracks.emplace_back(HGW::GwTrack());
                tracks.back().open(item.second, true);
                if (tracks.back().kind == HGW::FType::BIGWIG) {
                    tracks.back().faceColour = opts.theme.fcBigWig;
                } else {
                    tracks.back().faceColour = opts.theme.fcTrack;
                }
                tracks.back().variant_distance = &opts.variant_distance;
            } else if (Utils::startsWith(item.first, "region")) {
                std::string rgn = item.second;
                regions.push_back(Utils::parseRegion(rgn));
            } else if (Utils::startsWith(item.first, "var")) {
                std::string v = item.second;
                addVariantTrack(v, 0, false, true);
            }
        }
        size_t count = 0;
        for (const auto &item: opts.seshIni["show"]) {
            if (Utils::startsWith(item.first, "region")) {
                std::string rgn = item.second;
                regions.push_back(Utils::parseRegion(rgn));
            } else if (Utils::startsWith(item.first, "mode")) {
                mode = (item.second == "tiled") ? Show::TILED : Show::SINGLE;
            } else if (Utils::startsWith(item.first, "var") && count < variantTracks.size()) {
                variantTracks[count].iterateToIndex(std::stoi(item.second) + (opts.number.x * opts.number.y));
                variantTracks[count].blockStart = std::stoi(item.second);
                count += 1;
            }
        }

        fonts.setTypeface(opts.font_str, opts.font_size);

        for (auto &fn: bam_paths) {
            htsFile *f = sam_open(fn.c_str(), "r");
            hts_set_fai_filename(f, reference.c_str());
            hts_set_threads(f, opts.threads);
            bams.push_back(f);
            sam_hdr_t *hdr_ptr = sam_hdr_read(f);
            headers.push_back(hdr_ptr);
            hts_idx_t *idx = sam_index_load(f, fn.c_str());
            indexes.push_back(idx);
        }
        if (opts.seshIni.has("commands")) {
            for (auto &item: opts.seshIni["commands"]) {
                inputText = item.second;
                commandProcessed();
            }
        }
        if (opts.seshIni["general"].has("window_position")) {
            Utils::Dims pos = Utils::parseDimensions(opts.seshIni["general"]["window_position"]);
            glfwSetWindowPos(window, pos.x, pos.y);
        }
        glfwSetWindowSize(window, opts.dimensions.x, opts.dimensions.y);
        windowResize(opts.dimensions.x, opts.dimensions.y);
        redraw = true;
    }

    void GwPlot::saveSession(std::string output_session = "") {
        std::vector<std::string> track_paths;
        for (const auto &item: tracks) {
            if (item.kind != HGW::FType::ROI) {
                track_paths.push_back(item.path);
            }
        }
        std::vector<std::pair<std::string, int>> variant_paths_info;
        for (const auto &item: variantTracks) {
            variant_paths_info.push_back({item.path, item.blockStart});
        }
        int xpos, ypos;
        glfwGetWindowPos(window, &xpos, &ypos);
        int windX, windY;
        glfwGetWindowSize(window, &windX, &windY);
        opts.saveCurrentSession(reference, ideogram_path, bam_paths, track_paths, regions, variant_paths_info,
                                commandsApplied,
                                output_session, mode, xpos, ypos, monitorScale, windX, windY);
    }

    int GwPlot::startUI(GrDirectContext *sContext, SkSurface *sSurface, int delay) {
        if (!opts.session_file.empty() && reference.empty()) {
            std::cout << "Loading session: " << opts.session_file << std::endl;
            loadSession();
        }
        if (terminalOutput) {
            std::cout << "\nType" << termcolor::green << " '/help'" << termcolor::reset << " for more info\n";
        } else {
            outStr << "Type '/help' for more info\n";
        }

#if !defined(__EMSCRIPTEN__)
        Term::startVersionCheck();
#endif

        vCursor = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
        setGlfwFrameBufferSize();

        fetchRefSeqs();
        opts.theme.setAlphas();
        GLFWwindow *wind = this->window;
        if (mode == Show::SINGLE) {
            printRegionInfo();
        } else {
            mouseOverTileIndex = 0;
            bboxes = Utils::imageBoundingBoxes(opts.number, (float) fb_width, (float) fb_height);
            if (terminalOutput) {
                std::cout << termcolor::magenta << "File    " << termcolor::reset
                          << variantTracks[variantFileSelection].path << "\n";
            } else {
                outStr << "File    " << variantTracks[variantFileSelection].path << "\n";
            }
        }
        bool wasResized = false;

        std::chrono::high_resolution_clock::time_point autoSaveTimer = std::chrono::high_resolution_clock::now();
        while (true) {
            if (glfwWindowShouldClose(wind) || triggerClose) {
                break;
            }
            glfwWaitEvents();
            if (delay > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delay));
            }

            if (redraw) {
                if (mode == Show::SINGLE) {
                    drawScreen(sSurface->getCanvas(), sContext, sSurface);
                } else if (mode == Show::TILED) {
                    drawTiles(sSurface->getCanvas(), sContext, sSurface);
                    printIndexInfo();
                }
            }
            drawOverlay(sSurface->getCanvas());
            sContext->flush();
            glfwSwapBuffers(window);

//            if (resizeTriggered && !imageCacheQueue.empty()) {
//                sSurface->getCanvas()->drawImage(imageCacheQueue.back().second, 0, 0);
//            }

            if (resizeTriggered && std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - resizeTimer) > 100ms) {
                imageCache.clear();
                redraw = true;
                processed = false;
                wasResized = true;
                setGlfwFrameBufferSize();
                setScaling();
                bboxes = Utils::imageBoundingBoxes(opts.number, (float) fb_width, (float) fb_height);

                resizeTriggered = false;

                sContext->abandonContext();
                sk_sp<const GrGLInterface> interface = GrGLMakeNativeInterface();
                sContext = GrDirectContext::MakeGL(interface).release();

                GrGLFramebufferInfo framebufferInfo;
                framebufferInfo.fFBOID = 0;
                framebufferInfo.fFormat = GL_RGBA8;
                GrBackendRenderTarget backendRenderTarget(fb_width, fb_height, 0, 0, framebufferInfo);

                if (!backendRenderTarget.isValid()) {
                    std::cerr << "ERROR: backendRenderTarget was invalid" << std::endl;
                    glfwTerminate();
                    std::exit(-1);
                }

                sSurface = SkSurface::MakeFromBackendRenderTarget(sContext,
                                                                  backendRenderTarget,
                                                                  kBottomLeft_GrSurfaceOrigin,
                                                                  kRGBA_8888_SkColorType,
                                                                  nullptr,
                                                                  nullptr).release();
                if (!sSurface) {
                    std::cerr
                            << "ERROR: sSurface could not be initialized (nullptr). The frame buffer format needs changing\n";
                    std::exit(-1);
                }

                rasterSurface = SkSurface::MakeRasterN32Premul(fb_width, fb_height);
                rasterCanvas = rasterSurface->getCanvas();
                rasterSurfacePtr = &rasterSurface;

                imageCache.clear();
                imageCacheQueue.clear();

                resizeTimer = std::chrono::high_resolution_clock::now();

            }
            if (std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::high_resolution_clock::now() - autoSaveTimer) > 1min) {
                saveLabels();
                autoSaveTimer = std::chrono::high_resolution_clock::now();
            }

        }
        saveLabels();
        saveSession();
        if (wasResized) {
            // no idea why, but unless exit is here then we get an abort error if we return to main. Something to do with lifetime of backendRenderTarget?
            if (terminalOutput) {
                std::cerr << "\nGw finished\n";
            }
            exit(EXIT_SUCCESS);
        }

        return 1;
    }

    void GwPlot::clearCollections() {
        regions.clear();
        for (auto &cl: collections) {
            for (auto &a: cl.readQueue) {
                bam_destroy1(a.delegate);
            }
        }
        collections.clear();
    }

    void GwPlot::processBam() {  // collect reads, calc coverage and find y positions on plot
        const int parse_mods_threshold = (opts.parse_mods) ? opts.mods_qual_threshold : 0;
        if (!processed) {
            int idx = 0;

            for (auto &cl: collections) {
                for (auto &aln: cl.readQueue) {
                    bam_destroy1(aln.delegate);
                }
                cl.readQueue.clear();
                cl.covArr.clear();
                cl.mmVector.clear();
                cl.levelsStart.clear();
                cl.levelsEnd.clear();
                cl.linked.clear();
                cl.skipDrawingReads = false;
                cl.skipDrawingCoverage = false;
            }
            if (collections.size() != bams.size() * regions.size()) {
                collections.resize(bams.size() * regions.size());
            }

            for (int i = 0; i < (int) bams.size(); ++i) {
                htsFile *b = bams[i];
                sam_hdr_t *hdr_ptr = headers[i];
                hts_idx_t *index = indexes[i];

                for (int j = 0; j < (int) regions.size(); ++j) {
                    Utils::Region * reg = &regions[j];

                    collections[idx].bamIdx = i;
                    collections[idx].regionIdx = j;
                    collections[idx].region = &regions[j];
                    if (collections[idx].skipDrawingReads) {
                        continue;
                    }
                    if (opts.max_coverage) {
                        collections[idx].covArr.resize(reg->end - reg->start + 1, 0);
                        if (opts.snp_threshold > reg->end - reg->start) {
                            collections[idx].mmVector.resize(reg->end - reg->start + 1);
                            Segs::Mismatches empty_mm = {0, 0, 0, 0};
                            std::fill(collections[idx].mmVector.begin(), collections[idx].mmVector.end(), empty_mm);
                        } else if (!collections[idx].mmVector.empty()) {
                            collections[idx].mmVector.clear();
                        }
                    }

                    if (reg->end - reg->start < opts.low_memory || opts.link_op != 0 || sortReadsBy != SortType::NONE) {
                        HGW::collectReadsAndCoverage(collections[idx], b, hdr_ptr, index, opts.threads, reg,
                                                     (bool) opts.max_coverage, filters, pool, parse_mods_threshold, sortReadsBy);

                        int maxY = Segs::findY(collections[idx], collections[idx].readQueue, opts.link_op, opts,
                                               false, sortReadsBy);
                        if (maxY > samMaxY) {
                            samMaxY = maxY;
                        }
                    } else {
                        samMaxY = opts.ylim;
                    }

                    idx += 1;
                }
            }

            if (bams.empty()) {
                collections.resize(regions.size());
                for (int j = 0; j < (int) regions.size(); ++j) {
                    collections[idx].bamIdx = -1;
                    collections[idx].regionIdx = j;
                    collections[idx].region = &regions[j];
                    idx += 1;
                }
            }
        }
    }

    void GwPlot::setGlfwFrameBufferSize() {
        if (!drawToBackWindow) {
            float xscale = 1;
            float yscale = 1;
            glfwGetWindowContentScale(window, &xscale, &yscale);
            monitorScale = xscale;  // we assume xscale and yscale are the same
            glfwGetFramebufferSize(window, &fb_width, &fb_height);
            if (monitorScale > 1) {
                opts.theme.lcBright.setStrokeWidth(monitorScale);
            }
        } else {
            monitorScale = 1;
            glfwGetFramebufferSize(backWindow, &fb_width, &fb_height);
        }
        gap = std::fmin(std::fmax(5, fb_height * 0.01 * monitorScale), monitorScale * 10);

    }

    void GwPlot::setRasterSize(int width, int height) {
//        monitorScale = 1;
        fb_width = width;
        fb_height = height;
        gap = std::fmin(std::fmax(5, fb_height * 0.01 * monitorScale), monitorScale * 10);
    }

    // sets scaling of y-position for various elements
    void GwPlot::setScaling() {  // todo only call this function when needed - fb size change, or when plot items are added or removed
        fonts.setOverlayHeight(monitorScale);
        refSpace =  fonts.overlayHeight * 1.25;
        sliderSpace = std::fmax((float)(fb_height * 0.0175), 10*monitorScale) + (gap * 0.5);
        auto fbh = (float) fb_height;
        auto fbw = (float) fb_width;
        if (bams.empty()) {
            covY = 0; totalCovY = 0; totalTabixY = 0; tabixY = 0;
        }
        auto nbams = (float)bams.size();
        if (opts.max_coverage && nbams > 0) {
            totalCovY = fbh * (float)0.1;
            covY = totalCovY / nbams;
        } else {
            totalCovY = 0; covY = 0;
        }
        if (tracks.empty()) {
            totalTabixY = 0; tabixY = 0;
        } else {
            totalTabixY = (fbh - refSpace - sliderSpace) * (float)opts.tab_track_height;
            tabixY = totalTabixY / (float)tracks.size();
        }

        if (nbams > 0 && samMaxY > 0) {
            trackY = (fbh - totalCovY - totalTabixY - refSpace - sliderSpace) / nbams;
            yScaling = (trackY - (gap * nbams)) / (double)samMaxY;

            // Ensure yScaling is an integer if possible
            if (yScaling > 1) {
                yScaling = (int)yScaling;
            }
        } else {
            trackY = 0;
            yScaling = 0;
        }

        if (opts.tlen_yscale) {
            pH = trackY / (float) opts.ylim;
            yScaling *= 0.95;
        } else {
            if (yScaling > 3) {
                pH = yScaling * 0.85;  // polygonHeight
            } else {
                pH = yScaling;
            }
        }

        if (pH > 8) {  // scale to pixel boundary
            pH = (float)(int)pH;
        } else if (opts.tlen_yscale) {
            pH = std::fmax(pH, 8);
        }
//        if (nbams > 0 && samMaxY > 0) {
//            trackY = (fbh - totalCovY - totalTabixY - refSpace - sliderSpace) / nbams;
//            yScaling = (trackY - (gap * nbams)) / (double)samMaxY;
//            if (yScaling > 3 * monitorScale) {
//                yScaling = (int)yScaling;
//            }
//
//        } else {
//            trackY = 0;
//            yScaling = 0;
//        }
//
//        if (yScaling > 3) {
//            pH = yScaling * 0.85;  // polygonHeight
//        } else {
//            pH = yScaling;
//        }
//
//        if (opts.tlen_yscale) {
//            pH = trackY / (float) opts.ylim;
//            yScaling *= 0.95;
//        }
//        if (pH > 8) {  // scale to pixel boundary
//            pH = (float) (int) pH;
//        } else if (opts.tlen_yscale) {
//            pH = std::fmax(pH, 8);
//        }

        fonts.setFontSize(yScaling, monitorScale);
        regionWidth = fbw / (float)regions.size();
        bamHeight = covY + trackY;
        for (auto &cl: collections) {
            cl.xScaling = (float)((regionWidth - gap - gap) / ((double)(cl.region->end - cl.region->start)));
            cl.xOffset = (regionWidth * (float)cl.regionIdx) + gap;
            cl.yOffset = (float)cl.bamIdx * bamHeight + covY + refSpace;
            cl.yPixels = trackY + covY;
            cl.xPixels = regionWidth;

            cl.regionLen = cl.region->end - cl.region->start;
            cl.regionPixels = cl.regionLen * cl.xScaling;

            cl.plotSoftClipAsBlock = cl.regionLen > opts.soft_clip_threshold;
            cl.plotPointedPolygons = cl.regionLen < 50000;
            cl.drawEdges = cl.regionLen < opts.edge_highlights;
        }

        pointSlop = (tan(0.42) * (yScaling * 0.5));  // radians
        textDrop = std::fmax(0, (yScaling - fonts.fontHeight) * 0.5);

        minGapSize = (uint32_t)(fb_width * 0.005);
        if (opts.parse_mods) {
            for (size_t i=0; i < 4; ++i) {
                opts.theme.ModPaints[0][i].setStrokeWidth(std::fmin(pH / 3, 4 * monitorScale));
                opts.theme.ModPaints[0][i].setStrokeWidth(std::fmin(pH / 3, 4 * monitorScale));
                opts.theme.ModPaints[0][i].setStrokeWidth(std::fmin(pH / 3, 4 * monitorScale));
            }
        }

    }

    void GwPlot::drawScreen(SkCanvas* canvas, GrDirectContext* sContext, SkSurface *sSurface) {

//        std::chrono::high_resolution_clock::time_point initial = std::chrono::high_resolution_clock::now();
        SkCanvas *canvasR = rasterCanvas;

        canvas->drawPaint(opts.theme.bgPaint);
        canvasR->drawPaint(opts.theme.bgPaint);

        frameId += 1;
        setGlfwFrameBufferSize();
        if (regions.empty() || bams.empty()) {
            canvasR->drawPaint(opts.theme.bgPaint);
        } else {
            processBam();  // Reads may be buffered here, or else streamed using the runDrawNoBuffer functions below
            setScaling();

            if (yScaling == 0) {
                return;
            }

            if (!imageCacheQueue.empty() && collections.size() > 1) {
                canvasR->drawImage(imageCacheQueue.back().second, 0, 0);
            }
            SkRect clip;

            if (collections.empty()) {
                canvasR->drawPaint(opts.theme.bgPaint);
            }

            for (auto &cl: collections) {
                if (cl.skipDrawingCoverage && cl.skipDrawingReads) {  // keep read and coverage area
                    continue;
                }
                canvasR->save();

                // for now cl.skipDrawingCoverage and cl.skipDrawingReads are almost always the same
                if ((!cl.skipDrawingCoverage && !cl.skipDrawingReads) || imageCacheQueue.empty()) {
                    if (bams.size() == 1) {
                        clip.setXYWH(cl.xOffset, 0, cl.regionPixels, cl.yOffset + trackY + totalTabixY + covY + refSpace);
                    } else if (cl.bamIdx == 0) {  // top bam, cover the ref too
                        clip.setXYWH(cl.xOffset, 0, cl.regionPixels, cl.yOffset + trackY + covY + refSpace);
                    } else if (cl.bamIdx == (int)bams.size() - 1) { // bottom bam
                        clip.setXYWH(cl.xOffset, cl.yOffset - covY, cl.regionPixels, cl.yOffset + trackY + covY + totalTabixY);
                    } else {  //middle bam
                        clip.setXYWH(cl.xOffset, cl.yOffset - covY, cl.regionPixels, cl.yOffset + covY);
                    }
                    canvasR->clipRect(clip, false);
                } else if (cl.skipDrawingCoverage) {
                    clip.setXYWH(cl.xOffset, cl.yOffset, cl.regionPixels, cl.yPixels);
                    canvasR->clipRect(clip, false);
                } else if (cl.skipDrawingReads){
                    clip.setXYWH(cl.xOffset, cl.yOffset - covY, cl.regionPixels, covY);
                    canvasR->clipRect(clip, false);
                }  // else no clip
                canvasR->drawPaint(opts.theme.bgPaint);

                if (!cl.skipDrawingReads) {
                    if (sortReadsBy == SortType::NONE) {
                        if (cl.regionLen >= opts.low_memory && !bams.empty() && opts.link_op == 0) {  // low memory mode will be used
                            cl.clear();
                            if (opts.threads == 1) {
                                HGW::iterDraw(cl, bams[cl.bamIdx], headers[cl.bamIdx], indexes[cl.bamIdx],
                                              &regions[cl.regionIdx], (bool) opts.max_coverage,
                                              filters, opts, canvasR, trackY, yScaling, fonts, refSpace, pointSlop,
                                              textDrop, pH, monitorScale);
                            } else {
                                HGW::iterDrawParallel(cl, bams[cl.bamIdx], headers[cl.bamIdx], indexes[cl.bamIdx],
                                                      opts.threads, &regions[cl.regionIdx], (bool) opts.max_coverage,
                                                      filters, opts, canvasR, trackY, yScaling, fonts, refSpace, pool,
                                                      pointSlop, textDrop, pH, monitorScale);
                            }
                        } else {
                            Drawing::drawCollection(opts, cl, canvasR, trackY, yScaling, fonts, opts.link_op, refSpace,
                                                    pointSlop, textDrop, pH, monitorScale);
                        }
                    } else {

                    }

                }
                canvasR->restore();
            }

            if (opts.max_coverage) {
                Drawing::drawCoverage(opts, collections, canvasR, fonts, covY, refSpace);
            }
            Drawing::drawRef(opts, regions, fb_width, canvasR, fonts, refSpace, (float)regions.size(), gap);
            Drawing::drawBorders(opts, fb_width, fb_height, canvasR, regions.size(), bams.size(), trackY, covY, (int)tracks.size(), totalTabixY, refSpace, gap);
            Drawing::drawTracks(opts, fb_width, fb_height, canvasR, totalTabixY, tabixY, tracks, regions, fonts, gap, monitorScale, sliderSpace);
            Drawing::drawChromLocation(opts, fonts, regions, ideogram, canvasR, fai, fb_width, fb_height, monitorScale, gap);

        }

        imageCacheQueue.emplace_back(frameId, rasterSurfacePtr[0]->makeImageSnapshot());
        canvas->drawImage(imageCacheQueue.back().second, 0, 0);

        sContext->flush();
        glfwSwapBuffers(window);
        redraw = false;
//        std::cerr << " time " << (std::chrono::duration_cast<std::chrono::milliseconds >(std::chrono::high_resolution_clock::now() - initial).count()) << std::endl;
    }

    void GwPlot::drawScreenNoBuffer(SkCanvas* canvas, GrDirectContext* sContext, SkSurface *sSurface) {
        canvas->drawPaint(opts.theme.bgPaint);
        frameId += 1;
        if (regions.empty()) {
            setScaling();
        } else {
            runDrawNoBuffer();
        }
        imageCacheQueue.emplace_back(frameId, rasterSurfacePtr[0]->makeImageSnapshot());
        canvas->drawImage(imageCacheQueue.back().second, 0, 0);
        sContext->flush();
        glfwSwapBuffers(window);
        redraw = false;
    }

    void GwPlot::drawCursorPosOnRefSlider(SkCanvas *canvas) {
        // determine if cursor is over the ref slider
        if (regions.empty() || xPos_fb <= 0 || yPos_fb <= 0 || regionSelection < 0) {
            return;
        }
        const float yh = std::fmax((float) (fb_height * 0.0175), 10 * monitorScale);
        if (yPos_fb < fb_height - (yh * 2)) {
            return;
        }
        const float colWidth = (float) fb_width / (float) regions.size();
        const float gap = 25 * monitorScale;
        const float gap2 = 50 * monitorScale;
        const float drawWidth = colWidth - gap2;
        float xp = ((float)regionSelection * colWidth) + gap;
        if (xPos_fb < xp || xPos_fb > xp + drawWidth) {
            return;
        }
        int pos = (int)(((xPos_fb - xp) / drawWidth) * (float)regions[regionSelection].chromLen);
        std::string s = Term::intToStringCommas(pos);

        float estimatedTextWidth = (float) s.size() * fonts.overlayWidth;
        SkRect rect;
        SkPaint rect_paint = opts.theme.bgPaint;
        rect_paint.setAlpha(160);
        float xbox = xPos_fb + monitorScale;
        rect.setXYWH(xbox, fb_height - (yh *2) + (monitorScale), estimatedTextWidth, (yh*(float)1.33) - monitorScale - monitorScale);
        canvas->drawRect(rect, rect_paint);
        sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(s.c_str(), fonts.overlay);
        SkPath path;
        path.moveTo(xPos_fb, fb_height - (yh * 2));
        path.lineTo(xPos_fb, fb_height - (yh * (float)0.66));
        canvas->drawPath(path, opts.theme.lcBright);
        canvas->drawTextBlob(blob, xbox + monitorScale, fb_height - yh, opts.theme.tcDel);
    }

    void GwPlot::drawOverlay(SkCanvas *canvas) {

        if (!imageCacheQueue.empty()) {
            while (imageCacheQueue.front().first != frameId) {
                imageCacheQueue.pop_front();
            }
            canvas->drawImage(imageCacheQueue.back().second, 0, 0);
        }

        // slider overlay
        if (mode == Show::SINGLE && bams.size() > 0) {
            drawCursorPosOnRefSlider(canvas);
        }

        // draw menu
        if (mode == Show::SETTINGS) {
            if (!imageCacheQueue.empty()) {
                while (imageCacheQueue.front().first != frameId) {
                    imageCacheQueue.pop_front();
                }
                canvas->drawImage(imageCacheQueue.back().second, 0, 0);
                SkPaint bg = opts.theme.bgPaint;
                bg.setAlpha(220);
                canvas->drawPaint(bg);
            }
            Menu::drawMenu(canvas, opts, fonts, monitorScale, fb_width, fb_height, inputText, charIndex);

        // draw box when a change in region selection happens via keyboard
        } else if (regionSelectionTriggered && regions.size() > 1) {
            SkRect rect{};
            float step = (float)fb_width / (float)regions.size();
            if (std::chrono::duration_cast<std::chrono::milliseconds >(std::chrono::high_resolution_clock::now() - regionTimer) > 500ms) {
                regionSelectionTriggered = false;
            }
            float height = (float)fb_height - gap - gap;
            float y = gap;
            SkPaint box{};
            box.setColor(SK_ColorGRAY);
            box.setStrokeWidth(monitorScale);
            box.setAntiAlias(true);
            box.setStyle(SkPaint::kStroke_Style);
            rect.setXYWH((float)regionSelection * step + gap, y + gap, step - gap - gap, height);
            canvas->drawRoundRect(rect, 5 * monitorScale, 5 * monitorScale, box);
        }

        // icon information for vcf-file
        if (mode == Show::TILED && variantTracks.size() > 1) {
            float tile_box_w = std::fmin(100 * monitorScale, (fb_width - (variantTracks.size() * gap + 1)) / variantTracks.size());
            SkPaint box{};
            SkRect rect{};
            box = opts.theme.ecSelected;
            box.setStyle(SkPaint::kStroke_Style);
            float x_val = gap;
            float h = fb_height * 0.01;
            float y_val;
            for (int i=0; i < (int)variantTracks.size(); ++i) {
                if (i == variantFileSelection) {
                    box = opts.theme.fcDup;
                    y_val = 0;
                } else {
                    box = opts.theme.fcCoverage;
                    box.setStyle(SkPaint::kStrokeAndFill_Style);
                    y_val = -h / 2;
                }
                rect.setXYWH(x_val, y_val, tile_box_w, h);
                canvas->drawRect(rect,  box);
                x_val += tile_box_w + gap;
            }
        }

        double xposm, yposm;
        glfwGetCursorPos(window, &xposm, &yposm);
        int windowW, windowH;  // convert screen coords to frame buffer coords
        glfwGetWindowSize(window, &windowW, &windowH);
        if (fb_width > windowW) {
            xposm *= (float) fb_width / (float) windowW;
            yposm *= (float) fb_height / (float) windowH;
        }

        // Text overlay for vcf file info
        bool variantFile_info = mode == TILED && variantTracks.size() > 1 && yposm < fb_height * 0.02;
        if (variantFile_info) {
            float tile_box_w = std::fmin(100 * monitorScale, (fb_width - (variantTracks.size() * gap + 1)) / variantTracks.size());
            float x_val = gap;
            float h = fonts.overlayHeight / 2;
            SkRect rect{};
            for (int i=0; i < (int)variantTracks.size(); ++i) {
                if (x_val - gap <= xposm && x_val + tile_box_w >= xposm) {
                    std::string &fname = variantTracks[i].path;
                    rect.setXYWH(x_val, h, fname.size() * fonts.overlayWidth + gap + gap, fonts.overlayHeight*2);
                    canvas->drawRoundRect(rect, 5 * monitorScale, 5 * monitorScale,opts.theme.bgPaint);
                    sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(fname.c_str(), fonts.overlay);
                    canvas->drawTextBlob(blob, x_val + gap, h + (fonts.overlayHeight * 1.3), opts.theme.tcDel);
                    rect.setXYWH(x_val, 0, tile_box_w, fb_width * 0.005);
                    canvas->drawRect(rect,  opts.theme.fcDup);
                }
                x_val += tile_box_w + gap;
            }
        }

        // command box
        if (captureText && !opts.editing_underway) {
            fonts.setFontSize(yScaling, monitorScale);
            SkRect rect{};
            float height_f = fonts.overlayHeight * 2;
            float x = 50;
            float w = fb_width - 100;

            SkPaint bg = opts.theme.bgMenu;

            if (x < w) {
                float y = fb_height - (fb_height * 0.025);
//                float y2 = fb_height - (height_f * 2.25);
                float y2 = fb_height - (height_f * 2.5);
                float yy = (y2 < y) ? y2 : y;
                float to_cursor_width = fonts.overlay.measureText(inputText.substr(0, charIndex).c_str(), charIndex, SkTextEncoding::kUTF8);
//                SkPaint box{};
//                box.setColor(SK_ColorGRAY);
//                box.setStrokeWidth(monitorScale);
//                box.setAntiAlias(true);
//                box.setStyle(SkPaint::kStroke_Style);
//                rect.setXYWH(x, yy, w, height_f);
//                canvas->drawRoundRect(rect, 5 * monitorScale, 5 * monitorScale, opts.theme.bgPaint);
//                canvas->drawRoundRect(rect, 5 * monitorScale, 5 * monitorScale, box);

                rect.setXYWH(0, yy, fb_width, fb_height);

                canvas->drawRoundRect(rect, 5 * monitorScale, 5 * monitorScale, bg);
                float pad = fonts.overlayHeight * 0.3;

                // Cursor and text
                SkPath path;
                path.moveTo(x + 14 + to_cursor_width, yy + (fonts.overlayHeight * 0.3) + pad + pad);
                path.lineTo(x + 14 + to_cursor_width, yy + (fonts.overlayHeight * 1.5) + pad + pad);
                canvas->drawPath(path, opts.theme.lcBright);
                if (!inputText.empty()) {
                    sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(inputText.c_str(), fonts.overlay);
                    canvas->drawTextBlob(blob, x + 14, yy + (fonts.overlayHeight * 1.3) + pad + pad, opts.theme.tcDel);
                }
                if (mode != SETTINGS && (commandToolTipIndex != -1 || !inputText.empty())) {

                    if (inputText.empty() && yy - (Menu::commandToolTip.size() * (fonts.overlayHeight+ pad)) < covY) {
                        sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(Menu::commandToolTip[commandToolTipIndex], fonts.overlay);
                        SkPaint grey;
                        grey.setColor(SK_ColorGRAY);
                        canvas->drawTextBlob(blob, x + 14 + fonts.overlayWidth + fonts.overlayWidth, yy + (fonts.overlayHeight * 1.3) + pad + pad, grey);
                    }
                    //yy += pad;

                    SkPaint tip_paint = opts.theme.lcBright;
                    tip_paint.setAntiAlias(true);
                    float n = 0;
                    for (const auto &cmd : Menu::commandToolTip) {
                        std::string cmd_s = cmd;
                        if (!inputText.empty() && !Utils::startsWith(cmd_s, inputText)) {
                            continue;
                        }
                        if (cmd_s == inputText) {
                            n = 0;
                            break;
                        }
                        n += 1;
                    }
                    if (n > 0) {
                        float step = fonts.overlayHeight + pad;
                        float top = yy - (n * step);
                        rect.setXYWH(0, top, x + fonts.overlayWidth * 18, (n * step) + pad + pad);
                        canvas->drawRoundRect(rect, 10, 10, bg);
                    }


                    for (const auto &cmd : Menu::commandToolTip) {
                        std::string cmd_s = cmd;
                        if (!inputText.empty() && !Utils::startsWith(cmd_s, inputText)) {
                            continue;
                        }
                        if (cmd_s == inputText) {
                            break;
                        }
//                        rect.setXYWH(x, yy - fonts.overlayHeight - pad, fonts.overlayWidth * 18, fonts.overlayHeight + pad + pad);
//                        canvas->drawRoundRect(rect, 5 * monitorScale, 5 * monitorScale, opts.theme.bgPaint);
                        sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(cmd, fonts.overlay);
                        canvas->drawTextBlob(blob, x + (fonts.overlayWidth * 3), yy, opts.theme.tcDel);
                        tip_paint.setStyle(SkPaint::kStrokeAndFill_Style);
                        if (commandToolTipIndex >= 0 && Menu::commandToolTip[commandToolTipIndex] == cmd) {
                            canvas->drawCircle(x + (fonts.overlayWidth), yy - (fonts.overlayHeight*0.5), 3, tip_paint);
                        }
                        tip_paint.setStyle(SkPaint::kStroke_Style);
                        int cs_val = Menu::getCommandSwitchValue(opts, cmd_s, drawLine);
                        if (cs_val == 1) {
                            path.reset();
                            path.moveTo(x + (2.*fonts.overlayWidth), yy - (fonts.overlayHeight*0.25));
                            path.lineTo(x + (2.25*fonts.overlayWidth), yy);
                            path.lineTo(x + (2.75*fonts.overlayWidth), yy - (fonts.overlayHeight*0.75));
                            canvas->drawPath(path, tip_paint);
                        }
                        yy -= fonts.overlayHeight + pad;
                        if (yy < covY) {
                            break;
                        }
                    }
                }
            }
        }

        // cog and bubble
        float half_h = (float)fb_height / 2;
        bool tool_popup = (xposm > 0 && xposm <= 60 && yposm >= half_h - 150 && yposm <= half_h + 150 && (xDrag == -1000000 || xDrag == 0) && (yDrag == -1000000 || yDrag == 0));
        if (tool_popup) {
            SkRect rect{};
            SkPaint pop_paint = opts.theme.bgPaint;
            pop_paint.setStrokeWidth(monitorScale);
            pop_paint.setStyle(SkPaint::kStrokeAndFill_Style);
            pop_paint.setAntiAlias(true);
            pop_paint.setAlpha(255);
            rect.setXYWH(-10, half_h - 35 * monitorScale, 30 * monitorScale + 10, 70 * monitorScale);
            canvas->drawRoundRect(rect, 5 * monitorScale, 5 * monitorScale, pop_paint);

            SkPaint cog_paint = opts.theme.lcBright;
            cog_paint.setAntiAlias(true);
            cog_paint.setStrokeWidth(3 * monitorScale);
            cog_paint.setStyle(SkPaint::kStrokeAndFill_Style);
            canvas->drawCircle(15 * monitorScale, half_h - 12.5 * monitorScale, 5 * monitorScale, cog_paint);

            SkPath path;
            path.moveTo(15 * monitorScale, half_h - (22.5 * monitorScale));
            path.lineTo(15 * monitorScale, half_h - (2.5 * monitorScale));
            canvas->drawPath(path, cog_paint);
            path.moveTo(5 * monitorScale, half_h - 12.5 * monitorScale);
            path.lineTo((25 * monitorScale), half_h - 12.5 * monitorScale);
            canvas->drawPath(path, cog_paint);
            path.moveTo(8 * monitorScale, half_h - (19.5 * monitorScale));
            path.lineTo(22 * monitorScale, half_h - (5.5 * monitorScale));
            canvas->drawPath(path, cog_paint);
            path.moveTo(8 * monitorScale, half_h - (5.5 * monitorScale));
            path.lineTo(22 * monitorScale, half_h - (19.5 * monitorScale));
            canvas->drawPath(path, cog_paint);
            canvas->drawCircle(15 * monitorScale, half_h - 12.5 * monitorScale, 2.5 * monitorScale, pop_paint);

            rect.setXYWH(7.5 * monitorScale, half_h + 7.5 * monitorScale, 15 * monitorScale, 15 * monitorScale);
            cog_paint.setStrokeWidth(monitorScale);
            cog_paint.setStyle(SkPaint::kStroke_Style);
            canvas->drawRoundRect(rect, 3.5 * monitorScale, 3.5 * monitorScale, cog_paint);

        } else if (drawLine && mode != SETTINGS) {
            SkPath path;
            path.moveTo(xposm, 0);
            path.lineTo(xposm, fb_height);
            canvas->drawPath(path, opts.theme.lcJoins);
        }

        bool current_view_is_images = (!variantTracks.empty() && variantTracks[variantFileSelection].type == HGW::TrackType::IMAGES);
        if (bams.empty() && !current_view_is_images && mode != SETTINGS) {
            float trackBoundary = fb_height - totalTabixY - refSpace;
            std::string dd_msg = "Drag-and-drop bam or cram files here";
            float msg_width = fonts.overlay.measureText(dd_msg.c_str(), dd_msg.size(), SkTextEncoding::kUTF8);
            float txt_start = ((float)fb_width / 2) - (msg_width / 2);
            sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(dd_msg.c_str(), fonts.overlay);
            SkPaint tcMenu;
            tcMenu.setARGB(255, 100, 100, 100);
            tcMenu.setStyle(SkPaint::kStrokeAndFill_Style);
            tcMenu.setAntiAlias(true);
            if (trackBoundary > (float)fb_height / 2) {
                canvas->drawTextBlob(blob.get(), txt_start, (float)fb_height / 2, tcMenu);
            }
        } else if (regions.empty() && !current_view_is_images && mode != SETTINGS) {
            std::string dd_msg = "Type e.g. '/chr1' to add a region, or drag-and-drop a vcf file here";
            float msg_width = fonts.overlay.measureText(dd_msg.c_str(), dd_msg.size(), SkTextEncoding::kUTF8);
            float txt_start = ((float)fb_width / 2) - (msg_width / 2);
            sk_sp<SkTextBlob> blob = SkTextBlob::MakeFromString(dd_msg.c_str(), fonts.overlay);
            SkPaint tcMenu;
            tcMenu.setARGB(255, 100, 100, 100);
            tcMenu.setStyle(SkPaint::kStrokeAndFill_Style);
            tcMenu.setAntiAlias(true);
            canvas->drawTextBlob(blob.get(), txt_start, (float)fb_height / 2, tcMenu);
        }
    }

    void GwPlot::tileDrawingThread(SkCanvas *canvas, GrDirectContext *sContext, SkSurface *sSurface) {
        currentVarTrack = &variantTracks[variantFileSelection];
        int bStart = currentVarTrack->blockStart;
        int bLen = (int)opts.number.x * (int)opts.number.y;
        int endIdx = bStart + bLen;
        currentVarTrack->iterateToIndex(endIdx);
        for (int i=bStart; i<endIdx; ++i) {
            bool c = imageCache.find(i) != imageCache.end();
            if (!c && i < (int)currentVarTrack->multiRegions.size() && !bams.empty()) {
                regions = currentVarTrack->multiRegions[i];
                runDrawOnCanvas(canvas);
                sContext->flush();
                sk_sp<SkImage> img(sSurface->makeImageSnapshot());
                imageCache[i] = img;
            }
        }
    }

    void GwPlot::tileLoadingThread() {
        currentVarTrack = &variantTracks[variantFileSelection];
        int bStart = currentVarTrack->blockStart;
        int bLen = (int)opts.number.x * (int)opts.number.y;
        int endIdx = bStart + bLen;
        int n_images = (int)currentVarTrack->image_glob.size();
        pool.parallelize_loop(bStart, endIdx,
                              [&](const int a, const int b) {
                                  for (int i=a; i<b; ++i) {
                                      g_mutex.lock();
                                      bool c = imageCache.find(i) != imageCache.end();
                                      g_mutex.unlock();
                                      if (!c && i < n_images) {
                                          sk_sp<SkData> data(nullptr);
                                          g_mutex.lock();

#if defined(_WIN32)
                                          const wchar_t *outp = currentVarTrack->image_glob[i].c_str();
                                          std::wstring pw(outp);
                                          std::string outp_str(pw.begin(), pw.end());
                                          const char *fname = outp_str.c_str();
#else
                                          const char *fname = currentVarTrack->image_glob[i].c_str();
#endif
                                          g_mutex.unlock();
                                          data = SkData::MakeFromFileName(fname);
                                          if (!data)
                                              throw std::runtime_error("Error: file not found");
                                          sk_sp<SkImage> image = SkImage::MakeFromEncoded(data);
                                          if (!image)
                                              throw std::runtime_error("Failed to decode an image");
                                          // if image is not explicitly decoded, it might be lazily decoded during drawing
                                          sk_sp<SkImage> image_decoded = image->makeRasterImage( SkImage::kAllow_CachingHint);
                                          g_mutex.lock();
                                          imageCache[i] = image_decoded;
                                          g_mutex.unlock();
                                      }
                                  }
                              }).wait();

        if (currentVarTrack->type == HGW::TrackType::IMAGES) { //multiLabels.size() < endIdx) {
            currentVarTrack->appendImageLabels(bStart, bLen);
        }
    }

    void GwPlot::drawTiles(SkCanvas *canvas, GrDirectContext *sContext, SkSurface *sSurface) {
        currentVarTrack = &variantTracks[variantFileSelection];
        int bStart = currentVarTrack->blockStart;

        setGlfwFrameBufferSize();
        setScaling();
        float y_gap = (variantTracks.size() <= 1) ? 0 : (10 * monitorScale);
        bboxes = Utils::imageBoundingBoxes(opts.number, fb_width, fb_height, 6 * monitorScale, 6 * monitorScale, y_gap);
        if (currentVarTrack->image_glob.empty()) {
            tileDrawingThread(canvas, sContext, sSurface);  // draws images from variant file
        } else {
            tileLoadingThread();  // loads static images in .png format
        }

        std::vector<std::string> srtLabels;
        std::string &fileName = variantTracks[variantFileSelection].fileName;
        for (auto &itm: seenLabels[fileName]) {
            srtLabels.push_back(itm);
        }
        std::sort(srtLabels.begin(), srtLabels.end());
        auto pivot = std::find_if(srtLabels.begin(),  // put PASS at front if available, forces labels to have some order
                                  srtLabels.end(),
                                  [](const std::string& s) -> bool {
                                      return s == "PASS";
                                  });
        if (pivot != srtLabels.end()) {
            std::rotate(srtLabels.begin(), pivot, pivot + 1);
        }

        canvas->drawPaint(opts.theme.bgPaint);
        SkSamplingOptions sampOpts = SkSamplingOptions();
        int i = bStart;
        for (auto &b : bboxes) {
            SkRect rect;
            if (imageCache.find(i) != imageCache.end()) {
                int w = imageCache[i]->width();
                int h = imageCache[i]->height();
                float ratio = (float)w / (float)h;
                float box_ratio = (float)b.width / (float)b.height;
                if (box_ratio > ratio) {
                    // Box is wider than the image (relative to their heights)
                    float newHeight = b.height;
                    float newWidth = newHeight * ratio;
                    rect.setXYWH(b.xStart, b.yStart, newWidth, newHeight);
                } else {
                    // Box is taller than the image (relative to their widths)
                    float newWidth = b.width;
                    float newHeight = newWidth / ratio;
                    rect.setXYWH(b.xStart, b.yStart, newWidth, newHeight);
                }
//                canvas->drawRect(rect, opts.theme.bgPaint);
                canvas->drawImageRect(imageCache[i], rect, sampOpts);
                if (currentVarTrack->multiLabels.empty()) {
                    ++i; continue;
                }
                if (i - bStart != mouseOverTileIndex) {
                    currentVarTrack->multiLabels[i].mouseOver = false;
                }
                Drawing::drawLabel(opts, canvas, rect, currentVarTrack->multiLabels[i], fonts, seenLabels[fileName], srtLabels);
            }
            ++i;
        }
        imageCacheQueue.emplace_back(frameId, sSurface->makeImageSnapshot());
        sContext->flush();
        glfwSwapBuffers(window);
        redraw = false;
    }

    void GwPlot::runDrawOnCanvas(SkCanvas *canvas) {
        fetchRefSeqs();
        processBam();
        setScaling();
        if (yScaling == 0) {
            return;
        }
        canvas->drawPaint(opts.theme.bgPaint);
        SkRect clip;
        for (auto &cl: collections) {

//            if (cl.skipDrawingCoverage && cl.skipDrawingReads) {  // keep read and coverage area
//                continue;
//            }
            canvas->save();

            // for now cl.skipDrawingCoverage and cl.skipDrawingReads are almost always the same
//            if ((!cl.skipDrawingCoverage && !cl.skipDrawingReads) || imageCacheQueue.empty()) {
//                if (cl.bamIdx == 0) {  // cover the ref too
//                    clip.setXYWH(cl.xOffset, 0, cl.regionPixels, cl.yOffset + trackY + covY + refSpace);
//                } else {
//                    clip.setXYWH(cl.xOffset, cl.yOffset - covY, cl.regionPixels, cl.yOffset + trackY + covY);
//                }
//                canvas->clipRect(clip, false);
//            } else if (cl.skipDrawingCoverage) {
//                clip.setXYWH(cl.xOffset, cl.yOffset + refSpace, cl.regionPixels, cl.yPixels - covY);
//                canvas->clipRect(clip, false);
//            } else if (cl.skipDrawingReads){  // skip reads
//                clip.setXYWH(cl.xOffset, cl.yOffset - covY, cl.regionPixels, covY);
//                canvas->clipRect(clip, false);
//            }

            if (cl.bamIdx == 0) {  // cover the ref too
                clip.setXYWH(cl.xOffset, 0, cl.regionPixels, cl.yOffset + trackY + covY + refSpace);
            } else {
                clip.setXYWH(cl.xOffset, cl.yOffset - covY, cl.regionPixels, cl.yOffset + trackY + covY);
            }
            canvas->clipRect(clip, false);
            canvas->drawPaint(opts.theme.bgPaint);

            if (!cl.skipDrawingReads) {

                if (cl.regionLen >= opts.low_memory && !bams.empty() && opts.link_op == 0) {  // low memory mode will be used
                    cl.clear();
                    if (opts.threads == 1) {
                        HGW::iterDraw(cl, bams[cl.bamIdx], headers[cl.bamIdx], indexes[cl.bamIdx],
                                      &regions[cl.regionIdx], (bool) opts.max_coverage,
                                      filters, opts, canvas, trackY, yScaling, fonts, refSpace, pointSlop,
                                      textDrop, pH, monitorScale);
                    } else {
                        HGW::iterDrawParallel(cl, bams[cl.bamIdx], headers[cl.bamIdx], indexes[cl.bamIdx],
                                              opts.threads, &regions[cl.regionIdx], (bool) opts.max_coverage,
                                              filters, opts, canvas, trackY, yScaling, fonts, refSpace, pool,
                                              pointSlop, textDrop, pH, monitorScale);
                    }
                } else {
                    Drawing::drawCollection(opts, cl, canvas, trackY, yScaling, fonts, opts.link_op, refSpace,
                                            pointSlop, textDrop, pH, monitorScale);
                }
            }
            canvas->restore();

//            Drawing::drawCollection(opts, cl, canvas, trackY, yScaling, fonts, opts.link_op, refSpace, pointSlop, textDrop, pH, monitorScale);
        }
        if (opts.max_coverage) {
            Drawing::drawCoverage(opts, collections, canvas, fonts, covY, refSpace);
        }
        Drawing::drawRef(opts, regions, fb_width, canvas, fonts, refSpace, (float)regions.size(), gap);
        Drawing::drawBorders(opts, fb_width, fb_height, canvas, regions.size(), bams.size(), trackY, covY, (int)tracks.size(), totalTabixY, refSpace, gap);
        Drawing::drawTracks(opts, fb_width, fb_height, canvas, totalTabixY, tabixY, tracks, regions, fonts, gap, monitorScale, sliderSpace);
        Drawing::drawChromLocation(opts, fonts, regions, ideogram, canvas, fai, fb_width, fb_height, monitorScale, gap);
    }

    void GwPlot::runDraw() {
        runDrawOnCanvas(rasterCanvas);
    }

    void GwPlot::runDrawNoBufferOnCanvas(SkCanvas* canvas) {

//        std::chrono::high_resolution_clock::time_point initial = std::chrono::high_resolution_clock::now();

        if (bams.empty()) {
            return;
        }
        canvas->drawPaint(opts.theme.bgPaint);

        fetchRefSeqs();

        // This is a subset of processBam function:
        samMaxY = opts.ylim;
        collections.resize(bams.size() * regions.size());
        int idx = 0;
        for (int i=0; i<(int)bams.size(); ++i) {
            for (int j=0; j<(int)regions.size(); ++j) {
                Utils::Region *reg = &regions[j];
                Segs::ReadCollection &col = collections[idx];
                col.skipDrawingCoverage = false;
                col.bamIdx = i;
                if (!col.levelsStart.empty()) {
                    col.clear();
                }
                col.regionIdx = j;
                col.region = reg;
                if (opts.max_coverage) {
                    col.covArr.resize(reg->end - reg->start + 1, 0);
                    if (opts.snp_threshold > reg->end - reg->start) {
                        col.mmVector.resize(reg->end - reg->start + 1);
                        Segs::Mismatches empty_mm = {0, 0, 0, 0};
                        std::fill(col.mmVector.begin(), col.mmVector.end(), empty_mm);
                    } else if (!col.mmVector.empty()) {
                        col.mmVector.clear();
                    }
                }
                idx += 1;
            }
        }
        setScaling();

        SkRect clip;
        for (auto &cl: collections) {
            if (cl.skipDrawingCoverage && cl.skipDrawingReads) {  // keep read and coverage area
                continue;
            }
            canvas->save();

            // for now cl.skipDrawingCoverage and cl.skipDrawingReads are almost always the same
            if ((!cl.skipDrawingCoverage && !cl.skipDrawingReads) || imageCacheQueue.empty()) {
                if (bams.size() == 1) {
                    clip.setXYWH(cl.xOffset, 0, cl.regionPixels, cl.yOffset + trackY + totalTabixY + covY + refSpace);
                } else if (cl.bamIdx == 0) {  // top bam, cover the ref too
                    clip.setXYWH(cl.xOffset, 0, cl.regionPixels, cl.yOffset + trackY + covY + refSpace);
                } else if (cl.bamIdx == (int)bams.size() - 1) { // bottom bam
                    clip.setXYWH(cl.xOffset, cl.yOffset - covY, cl.regionPixels, cl.yOffset + trackY + covY + totalTabixY);
                } else {  //middle bam
                    clip.setXYWH(cl.xOffset, cl.yOffset - covY, cl.regionPixels, cl.yOffset + covY);
                }
                canvas->clipRect(clip, false);
            } else if (cl.skipDrawingCoverage) {
                clip.setXYWH(cl.xOffset, cl.yOffset, cl.regionPixels, cl.yPixels);
                canvas->clipRect(clip, false);
            } else if (cl.skipDrawingReads){
                clip.setXYWH(cl.xOffset, cl.yOffset - covY, cl.regionPixels, covY);
                canvas->clipRect(clip, false);
            }  // else no clip

            canvas->drawPaint(opts.theme.bgPaint);

            if (!cl.skipDrawingReads) {

                if (cl.regionLen >= opts.low_memory && !bams.empty() && opts.link_op == 0) {  // low memory mode will be used
                    cl.clear();
                    if (opts.threads == 1) {
                        HGW::iterDraw(cl, bams[cl.bamIdx], headers[cl.bamIdx], indexes[cl.bamIdx],
                                      &regions[cl.regionIdx], (bool) opts.max_coverage,
                                      filters, opts, canvas, trackY, yScaling, fonts, refSpace, pointSlop,
                                      textDrop, pH, monitorScale);
                    } else {
                        HGW::iterDrawParallel(cl, bams[cl.bamIdx], headers[cl.bamIdx], indexes[cl.bamIdx],
                                              opts.threads, &regions[cl.regionIdx], (bool) opts.max_coverage,
                                              filters, opts, canvas, trackY, yScaling, fonts, refSpace, pool,
                                              pointSlop, textDrop, pH, monitorScale);
                    }
                } else {
                    Drawing::drawCollection(opts, cl, canvas, trackY, yScaling, fonts, opts.link_op, refSpace,
                                            pointSlop, textDrop, pH, monitorScale);
                }
            }
            canvas->restore();
        }



//        idx = 0;
//        for (int i=0; i<(int)bams.size(); ++i) {
//            htsFile* b = bams[i];
//            sam_hdr_t *hdr_ptr = headers[i];
//            hts_idx_t *index = indexes[i];
//            for (int j=0; j<(int)regions.size(); ++j) {
//                Utils::Region *reg = &regions[j];
//                if (opts.threads == 1) {
//                    HGW::iterDraw(collections[idx], b, hdr_ptr, index, reg, (bool) opts.max_coverage,
//                                  filters, opts, canvas, trackY, yScaling, fonts, refSpace, pointSlop, textDrop, pH, monitorScale);
//                } else {
//                    HGW::iterDrawParallel(collections[idx], b, hdr_ptr, index, opts.threads, reg, (bool) opts.max_coverage,
//                                  filters, opts, canvas, trackY, yScaling, fonts, refSpace, pool, pointSlop, textDrop, pH, monitorScale);
//                }
//                idx += 1;
//            }
//        }
        if (opts.max_coverage) {
            Drawing::drawCoverage(opts, collections, canvas, fonts, covY, refSpace);
        }

        Drawing::drawRef(opts, regions, fb_width, canvas, fonts, refSpace, (float)regions.size(), gap);
        Drawing::drawBorders(opts, fb_width, fb_height, canvas, regions.size(), bams.size(), trackY, covY, (int)tracks.size(), totalTabixY, refSpace, gap);
        Drawing::drawTracks(opts, fb_width, fb_height, canvas, totalTabixY, tabixY, tracks, regions, fonts, gap, monitorScale, sliderSpace);
        Drawing::drawChromLocation(opts, fonts, regions, ideogram, canvas, fai, fb_width, fb_height, monitorScale, gap);
//        std::cerr << " time runDrawNoBuffer " << (std::chrono::duration_cast<std::chrono::milliseconds >(std::chrono::high_resolution_clock::now() - initial).count()) << std::endl;
    }

    void GwPlot::runDrawNoBuffer() {
        runDrawNoBufferOnCanvas(rasterCanvas);
    }


    sk_sp<SkImage> GwPlot::makeImage() {
        makeRasterSurface();
        runDraw();
        sk_sp<SkImage> img(rasterSurfacePtr[0]->makeImageSnapshot());
        return img;
    }

    void GwPlot::rasterToPng(const char* path) {
        sk_sp<SkImage> img(rasterSurfacePtr[0]->makeImageSnapshot());
        if (!img) { return; }
        sk_sp<SkData> png(img->encodeToData());
        if (!png) { return; }
        FILE* fout = fopen(path, "w");
        fwrite(png->data(), 1, png->size(), fout);
        fclose(fout);
    }

    void imageToPng(sk_sp<SkImage> &img, std::filesystem::path &path) {
        if (!img) { return; }
        sk_sp<SkData> png(img->encodeToData());
        if (!png) { return; }
#if defined(_WIN32)
	const wchar_t* outp = path.c_str();
	std::wstring pw(outp);
	std::string outp_str(pw.begin(), pw.end());
	SkFILEWStream out(outp_str.c_str());
#else
        SkFILEWStream out(path.c_str());
#endif
        (void)out.write(png->data(), png->size());
    }

    void imagePngToStdOut(sk_sp<SkImage> &img) {
        if (!img) { return; }
        sk_sp<SkData> png(img->encodeToData());
        if (!png) { return; }
        FILE* fout = stdout;
#if defined(_WIN32) || defined(_WIN64)
        HANDLE h = (HANDLE) _get_osfhandle(_fileno(fout));
        DWORD t = GetFileType(h);
	    if (t == FILE_TYPE_CHAR) {
        } // fine
	    if (t != FILE_TYPE_PIPE) {
	        std::cerr << "Error: attempting to write to a bad PIPE. This is unsupported on Windows" <<  std::endl;
            std::exit(-1);
        }
#endif	    
        fwrite(png->data(), 1, png->size(), fout);
        fclose(fout);
    }

    void imagePngToFile(sk_sp<SkImage> &img, std::string path) {
        if (!img) { return; }
        sk_sp<SkData> png(img->encodeToData());
        if (!png) { return; }
        FILE* fout = fopen(path.c_str(), "w");
        fwrite(png->data(), 1, png->size(), fout);
        fclose(fout);
    }

}

