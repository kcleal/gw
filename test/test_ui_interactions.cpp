// Headless interaction regressions. Run with make test-ui.
// Synthesizes mouse input through ImGui without opening a native window.
#include <cassert>
#include <cstring>
#include "htslib/bgzf.h"
#include <fstream>
#include <filesystem>
#include <chrono>
#include <iostream>
#include <sstream>
#include <map>
#include "plot_manager.h"
#include "menu.h"
#include "plot_commands.h"
#include "term_out.h"
#include "find_mismatches.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imfilebrowser.h"
#include "include/core/SkBitmap.h"

static std::string clipboard;
// Compare fetched records with a fresh query while anchoring visible rows, including equal-start records,
// long reads blocking shorter reads, exhausted rows, zoom and empty intervals.
static void testBufferedScrolling(bool equalStarts, Utils::SortType sort = Utils::SortType::NONE) {
    std::string path = (std::filesystem::temp_directory_path() /
        ("gw-scroll-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".bam")).string();
    auto* output = sam_open(path.c_str(), "wb");
    std::string headerText = "@HD\tVN:1.6\tSO:coordinate\n@SQ\tSN:chr1\tLN:100000\n";
    auto* header = sam_hdr_parse(headerText.size(), headerText.c_str());
    assert(output && header && sam_hdr_write(output, header) >= 0);
    for (int i = 0; i < 400; ++i) {
        int start = 100 + (equalStarts ? (i / 3) * 80 : i * 25);
        int length = i == 0 ? 30000 : (i % 7 == 0 ? 6000 : 60);
        std::string cigar = i % 11 == 0 ? "100S" + std::to_string(length) + "M" : std::to_string(length) + "M";
        if (i % 13 == 0) cigar += "250S";
        int seqLen = length + (i % 11 == 0 ? 100 : 0) + (i % 13 == 0 ? 250 : 0);
        std::string text = "read" + std::to_string(i) + "\t" + (i % 2 ? "16" : "0") + "\tchr1\t" + std::to_string(start + 1) +
            "\t60\t" + cigar + "\t*\t0\t0\t" + std::string(seqLen, 'A') + "\t" + std::string(seqLen, 'I');
        if (sort == Utils::SortType::HP || sort == Utils::SortType::HP_AND_POS) {
            if (i % 3) text += "\tHP:i:" + std::to_string(i % 3 == 1 ? 1 : 2);
        }
        kstring_t line{0, 0, nullptr}; kputs(text.c_str(), &line);
        auto* record = bam_init1();
        assert(sam_parse1(&line, header, record) >= 0 && sam_write1(output, header, record) >= 0);
        bam_destroy1(record); free(line.s);
    }
    assert(sam_close(output) == 0 && sam_index_build(path.c_str(), 0) == 0);
    auto* input = sam_open(path.c_str(), "rb");
    auto* index = sam_index_load(input, path.c_str());
    auto* inputHeader = sam_hdr_read(input);
    assert(input && index && inputHeader);
    BS::thread_pool pool(2);
    std::vector<Parse::Parser> filters;
    Themes::IniOptions options;
    options.ylim = 8; options.threads = 2; options.link_op = 0;
    options.parse_mods = false; options.tlen_yscale = false;
    for (bool clips : {false, true}) {
        options.soft_clip_threshold = clips ? 1 : 0;
        for (int scroll : {0, 3}) {
            Utils::Region region; region.chrom = "chr1"; region.start = 100; region.end = 900; region.sortOption = sort;
            Segs::ReadCollection incremental; incremental.region = &region; incremental.vScroll = scroll;
            int maxY = 0;
            std::map<std::string, std::pair<int, int>> retainedClips;
            auto captureClips = [&](int start, int end) {
                retainedClips.clear();
                for (const auto& a : incremental.readQueue)
                    if (a.cov_end > start && a.cov_start < end &&
                        !(a.reference_end > (uint32_t)start && a.pos < (uint32_t)end))
                        retainedClips.emplace(bam_get_qname(a.delegate), std::make_pair(a.cov_start, a.cov_end));
            };
            auto verify = [&]() {
                Segs::ReadCollection fresh; fresh.region = &region; fresh.vScroll = scroll;
                fresh.covArr.resize(region.end - region.start + 1);
                HGW::collectReadsAndCoverage(fresh, input, inputHeader, index, 2, &region, true, filters, pool, 0, clips);
                int sortState = Segs::getSortCodes(fresh.readQueue, 2, pool, &region);
                Segs::findY(fresh, fresh.readQueue, 0, options, false, sortState);
                assert(incremental.readQueue.size() == fresh.readQueue.size() + retainedClips.size());
                auto remainingClips = retainedClips;
                std::map<std::string, const Segs::Align*> expected;
                for (const auto& a : fresh.readQueue) expected.emplace(bam_get_qname(a.delegate), &a);
                assert(expected.size() == fresh.readQueue.size());
                for (size_t n = 0; n < incremental.readQueue.size(); ++n) {
                    const auto& actual = incremental.readQueue[n];
                    auto it = expected.find(bam_get_qname(actual.delegate));
                    if (it == expected.end()) {
                        auto clipped = remainingClips.find(bam_get_qname(actual.delegate));
                        assert(clipped != remainingClips.end());
                        assert(actual.cov_start == clipped->second.first && actual.cov_end == clipped->second.second);
                        remainingClips.erase(clipped);
                    } else {
                        assert(actual.cov_start == it->second->cov_start && actual.cov_end == it->second->cov_end);
                        expected.erase(it);
                    }
                    // Layout is intentionally stable rather than freshly packed.
                    // Every record must occur exactly once and rows cannot collide.
                    if (n) assert(incremental.readQueue[n-1].pos <= actual.pos);
                    if (actual.y >= 0) {
                        assert(actual.y < options.ylim);
                        for (size_t m = 0; m < n; ++m) {
                            const auto& other = incremental.readQueue[m];
                            if (other.y == actual.y)
                                assert(actual.cov_start > other.cov_end || other.cov_start > actual.cov_end);
                        }
                    } else if (sort == Utils::SortType::NONE && scroll == 0 && actual.y == -1) {
                        // No read may remain hidden if it fits in an available row.
                        for (int row = 0; row < options.ylim; ++row) {
                            bool occupied = false;
                            for (const auto& other : incremental.readQueue)
                                if (other.y == row && actual.cov_start <= other.cov_end && other.cov_start <= actual.cov_end)
                                    occupied = true;
                            if (!occupied) std::cerr << "Hidden read " << bam_get_qname(actual.delegate) << " at " << region.start << "-" << region.end
                                << " free row " << row << " clips " << clips << " equal " << equalStarts << std::endl;
                            assert(occupied);
                        }
                    }
                }
                assert(expected.empty() && remainingClips.empty());
                assert(incremental.covArr == fresh.covArr);
                fresh.clear();
            };
            auto pan = [&](int start, int end) {
                captureClips(start, end);
                std::map<std::string, int> anchors;
                for (const auto& a : incremental.readQueue)
                    if (a.y >= 0 && a.cov_end > start && a.cov_start < end)
                        anchors.emplace(bam_get_qname(a.delegate), a.y);
                bool left = start < region.start;
                region.start = start; region.end = end;
                HGW::appendReadsAndCoverage(incremental, input, inputHeader, index, options, true, left, &maxY, filters, pool, region);
                verify();
                for (const auto& a : incremental.readQueue) {
                    auto old = anchors.find(bam_get_qname(a.delegate));
                    if (old != anchors.end()) {
                        assert(a.y == old->second);
                        anchors.erase(old);
                    }
                }
                assert(anchors.empty());
            };
            pan(100, 900);
            // Keep right- and left-clip-only survivors, then bring their aligned
            // spans back into view. Neither return may duplicate the record.
            pan(500, 600); pan(600, 650); pan(735, 800); pan(400, 600);
            pan(250, 350); pan(200, 275); pan(300, 450); pan(100, 900);
            for (int start = 500; start < 20000; start += 400) pan(start, start + 800);
            for (int start = 19300; start >= 100; start -= 400) pan(start, start + 800);
            pan(6000, 9000); pan(5000, 10000); // Both sides extend together.
            pan(40000, 41000); pan(50000, 51000); pan(100, 900); // Empty queues and large jumps.
            captureClips(300, 600);
            region.start = 300; region.end = 600;
            HGW::trimToRegion(incremental, true, 0);
            Segs::placeUnassignedReads(incremental, options, sort);
            verify();
            pan(100, 900);
            incremental.clear();
        }
    }
    if (sort == Utils::SortType::HP) {
        Themes::IniOptions opts; opts.ylim = 30; opts.threads = 1;
        opts.low_memory = 1000; opts.scroll_speed = 0.25f;
        opts.link_op = 0; opts.tlen_yscale = false;
        std::vector<std::string> paths{path}, tracks;
        Utils::Region region; region.chrom = "chr1"; region.start = 1000; region.end = 1800;
        region.sortOption = Utils::SortType::HP;
        std::vector<Utils::Region> regions{region};
        Manager::GwPlot plot("", paths, opts, regions, tracks);
        plot.terminalOutput = false;
        plot.setImageSize(800, 400); plot.makeRasterSurface();
        plot.drawScreen();
        auto verifyGroups = [&]() {
            auto& cl = plot.collections[0];
            assert((cl.sortLevels == std::vector<int>{1, 2, 0}));
            for (const auto& a : cl.readQueue) {
                auto* hp = bam_aux_get(a.delegate, "HP");
                assert(a.sort_tag == (hp ? bam_aux2i(hp) : 0));
            }
        };
        verifyGroups();
        for (int repeat = 0; repeat < 3; ++repeat) {
            do {
                plot.keyPress(plot.opts.zoom_out, 0, GLFW_PRESS, 0);
                plot.drawScreen();
            } while (plot.regions[0].end - plot.regions[0].start < opts.low_memory);
            assert(plot.collections[0].sortLevels.empty());
            plot.keyPress(plot.opts.zoom_in, 0, GLFW_PRESS, 0);
            plot.drawScreen();
            assert(plot.regions[0].end - plot.regions[0].start < opts.low_memory);
            verifyGroups();
        }
        // A full fetch must also discard categories from the previous layout.
        plot.collections[0].sortLevels = {1, 2, 3, 4, 5, 6, 7, 0};
        plot.processed = false; plot.drawScreen(); verifyGroups();
    }
    sam_hdr_destroy(header); sam_hdr_destroy(inputHeader); hts_idx_destroy(index); sam_close(input);
    std::filesystem::remove(path); std::filesystem::remove(path + ".bai");
    std::cout << "Buffered scrolling preserves visible rows and matches fresh fetches\n";
}

static void testCoverageBaseCounts() {
    std::string headerText = "@SQ\tSN:chr1\tLN:1000\n";
    auto* header = sam_hdr_parse(headerText.size(), headerText.c_str());
    Utils::Region region; region.chrom = "chr1"; region.start = 90; region.end = 200;
    Segs::ReadCollection collection; collection.region = &region;
    auto add = [&](const std::string& name, const std::string& cigar, const std::string& sequence) {
        std::string text = name + "\t0\tchr1\t101\t60\t" + cigar + "\t*\t0\t0\t" +
            sequence + "\t" + std::string(sequence.size(), 'I');
        kstring_t line{0, 0, nullptr}; kputs(text.c_str(), &line);
        auto* record = bam_init1(); assert(sam_parse1(&line, header, record) >= 0); free(line.s);
        collection.readQueue.emplace_back(record);
        Segs::align_init(&collection.readQueue.back(), 0, true);
    };
    for (char base : std::string("ACGTN")) add(std::string(1, base), "20M", std::string(20, base));
    add("mixed", "2S3M1I2=2X2D2N3M1H", "NNACGTACGTNAC");
    auto check = [&](int pos, int depth, int a, int c, int g, int t) {
        std::ostringstream out; std::string summary;
        Term::printCoverage(pos, collection, out, &summary);
        const std::string expected = "Coverage\tPosition\tchr1:" + std::to_string(pos + 1) +
            "\tDepth\t" + std::to_string(depth) + "\tA\t" + std::to_string(a) +
            "\tC\t" + std::to_string(c) + "\tG\t" + std::to_string(g) + "\tT\t" + std::to_string(t);
        if (summary != expected) std::cerr << "Coverage at " << pos << ": " << summary << " expected " << expected << '\n';
        assert(summary == expected);
        assert(out.str().find("Coverage") != std::string::npos);
    };
    // No reference sequence or mismatch cache is needed to count read bases.
    check(99, 0, 0, 0, 0, 0);
    check(100, 6, 2, 1, 1, 1);
    check(103, 6, 2, 1, 1, 1); // = consumes query and reference.
    check(105, 6, 1, 1, 2, 1); // X after = and insertion.
    check(107, 5, 1, 1, 1, 1); // Deletion.
    check(109, 5, 1, 1, 1, 1); // Reference skip.
    check(111, 6, 1, 1, 1, 1); // N contributes to depth.
    check(112, 6, 2, 1, 1, 1);
    check(114, 5, 1, 1, 1, 1);
    check(120, 0, 0, 0, 0, 0); // Alignment end is exclusive.
    collection.clear(); check(100, 0, 0, 0, 0, 0);
    add("spliced", "3S10M28576310N10M", "NNNAAAAAAAAAACGTACGTACG");
    region.start = 28576419; region.end = 28576465;
    for (int i = 0; i < 10; ++i) {
        std::ostringstream out; std::string summary;
        const int pos = 28576420 + i;
        Term::printCoverage(pos, collection, out, &summary);
        const char base = "CGTACGTACG"[i];
        assert(out.str().find(std::string(1, base) + ":1") != std::string::npos);
        assert(summary.find("Depth\t1") != std::string::npos);
    }
    collection.clear();
    sam_hdr_destroy(header);
}

static void testHaplotypeRows() {
    for (auto sort : {Utils::SortType::HP, Utils::SortType::HP_AND_POS}) {
        for (int scroll : {0, 2}) {
            Themes::IniOptions options; options.ylim = 17; options.tlen_yscale = false;
            Utils::Region region; region.sortOption = sort;
            Segs::ReadCollection collection; collection.region = &region; collection.vScroll = scroll;
            for (int hp : {0, 5, 2}) {
                for (int i = 0; i < 8; ++i) {
                    collection.readQueue.emplace_back(nullptr);
                    auto& a = collection.readQueue.back();
                    a.pos = 100; a.cov_start = 100; a.cov_end = 200; a.y = -1; a.sort_tag = hp;
                }
            }
            Segs::findY(collection, collection.readQueue, 0, options, false, sort);
            assert((collection.sortLevels == std::vector<int>{2, 5, 0}));
            std::vector<int> before;
            for (const auto& a : collection.readQueue) {
                before.push_back(a.y);
                if (a.y < 0) continue;
                int group = a.sort_tag == 2 ? 0 : a.sort_tag == 5 ? 1 : 2;
                int begin = group * options.ylim / 3;
                int end = (group + 1) * options.ylim / 3 - (group < 2 ? 1 : 0);
                assert(a.y >= begin && a.y < end);
            }
            Segs::placeUnassignedReads(collection, options, sort);
            for (size_t i = 0; i < before.size(); ++i) assert(collection.readQueue[i].y == before[i]);
        }
    }
}

static void testStableRowGaps() {
    Themes::IniOptions options; options.ylim = 3;
    Segs::ReadCollection collection;
    auto add = [&](int start, int end, int row) {
        collection.readQueue.emplace_back(nullptr);
        auto& a = collection.readQueue.back();
        a.cov_start = start; a.cov_end = end; a.y = row;
    };
    add(100, 200, 0); add(400, 500, 0); add(100, 500, 2);
    add(250, 300, -1); add(150, 450, -1); add(600, 700, -1);
    Segs::placeUnassignedReads(collection, options, Utils::SortType::NONE);
    const int expected[] = {0, 0, 2, 0, 1, 0};
    for (size_t i = 0; i < collection.readQueue.size(); ++i) assert(collection.readQueue[i].y == expected[i]);
}

static void testLeftClipDrawingPastRightEdge() {
    Themes::IniOptions options;
    options.theme = Themes::IgvTheme(); options.theme.setAlphas();
    options.soft_clip_threshold = 1; options.snp_threshold = 0; options.parse_mods = false; options.data_labels = false;
    Themes::Fonts fonts;
    fonts.overlayHeight = 12; fonts.overlayCharWidth = 8;
    std::string headerText = "@SQ\tSN:chr1\tLN:1000\n";
    auto* header = sam_hdr_parse(headerText.size(), headerText.c_str());
    for (int flag : {0, 16}) {
        std::string text = "clipped\t" + std::to_string(flag) + "\tchr1\t201\t60\t100S20M\t*\t0\t0\t" +
            std::string(120, 'A') + "\t" + std::string(120, 'I');
        kstring_t line{0, 0, nullptr}; kputs(text.c_str(), &line);
        auto* record = bam_init1(); assert(sam_parse1(&line, header, record) >= 0); free(line.s);
        Utils::Region region; region.chrom = "chr1";
        Segs::ReadCollection collection; collection.region = &region;
        collection.xScaling = 1; collection.xOffset = 0; collection.yOffset = 5; collection.regionPixels = 200;
        collection.plotSoftClipAsBlock = true; collection.plotPointedPolygons = false; collection.drawEdges = false;
        collection.readQueue.emplace_back(record);
        auto& read = collection.readQueue.back(); Segs::align_init(&read, 0, true); read.y = 0;
        Drawing::drawContext context{};
        context.yScaling = 10; context.pH = 10; context.monitorScale = 1;
        SkBitmap bitmap; bitmap.allocN32Pixels(200, 40);
        SkCanvas canvas(bitmap);
        std::vector<std::string> paths;
        // BAM POS moves from inside the viewport to exactly its right edge and
        // then beyond it; the same visible portion of the clip must remain.
        for (int end : {220, 200, 199, 150}) {
            region.start = 100; region.end = end;
            canvas.clear(SK_ColorWHITE);
            canvas.save(); canvas.clipRect(SkRect::MakeWH(end - region.start, 40));
            Drawing::drawCollection(options, collection, &canvas, fonts, paths, context);
            canvas.restore();
            assert(bitmap.getColor(25, 10) != SK_ColorWHITE);
            assert(read.y == 0);
        }
        region.start = 20; region.end = 100; // Clip starts at the right edge: fully out of view.
        canvas.clear(SK_ColorWHITE);
        canvas.save(); canvas.clipRect(SkRect::MakeWH(80, 40));
        Drawing::drawCollection(options, collection, &canvas, fonts, paths, context);
        canvas.restore();
        assert(bitmap.getColor(25, 10) == SK_ColorWHITE);
        collection.clear();
    }
    sam_hdr_destroy(header);
}

static void testReferenceWindowOwnership() {
    std::string path = (std::filesystem::temp_directory_path() /
        ("gw-ref-window-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".fa")).string();
    std::string bases;
    for (int i = 0; i < 10000; ++i) bases += "ACGT";
    std::ofstream(path) << ">chr1\n" << bases << "\n";
    assert(fai_build(path.c_str()) == 0);
    std::weak_ptr<char> released;
    {
        Themes::IniOptions options; options.snp_threshold = 1000;
        std::vector<std::string> empty;
        std::vector<Utils::Region> regions{Utils::Region("chr1", 100, 180)};
        Manager::GwPlot plot(path, empty, options, regions, empty);
        auto& region = plot.regions[0];
        plot.fetchRefSeq(region);
        auto owner = region.refSeqBuffer;
        released = owner;
        assert(owner && region.refBufferLen <= 20000);
        const auto* packed = &FindMismatches::prepareReference(region);
        assert(packed->base(region.start - region.refBufferStart) == 1);
        auto snapshot = region;
        std::ostringstream printed;
        Term::printRefSeq(&region, 20, 0, 1, printed);
        std::string expected;
        for (int i = 100; i < 180; ++i) expected += (char)std::tolower(bases[i]);
        assert(printed.str().find("\n" + expected + "\n") != std::string::npos);
        assert(printed.str().size() < 200);
        for (int start = 110; start < 500; start += 10) {
            region.start = start; region.end = start + 80; plot.fetchRefSeq(region);
            assert(region.refSeqBuffer == owner && region.refSeqLen == 80);
            assert(&FindMismatches::prepareReference(region) == packed);
            assert(std::string(region.refSeq, region.refSeqLen) == bases.substr(start, 80));
        }
        region.start = 30000; region.end = 30080; plot.fetchRefSeq(region);
        assert(region.refSeqBuffer != owner && region.refBufferLen <= 20000);
        assert(!region.packedReference && snapshot.packedReference.get() == packed);
        assert(std::string(region.refSeq, region.refSeqLen) == bases.substr(30000, 80));
        assert(std::string(snapshot.refSeq, snapshot.refSeqLen) == bases.substr(100, 80));
        region.start = 0; region.end = 30000; plot.fetchRefSeq(region);
        assert(!region.refSeq && region.refSeqLen == 0);
        region.start = 39980; region.end = 40080; plot.fetchRefSeq(region);
        assert(region.refSeqLen == 20 && std::string(region.refSeq, 20) == bases.substr(39980));
    }
    assert(released.expired());
    std::filesystem::remove(path); std::filesystem::remove(path + ".fai");
}

static void testHiddenMismatchPrefix() {
    std::string headerText = "@SQ\tSN:chr1\tLN:2000\n";
    auto* header = sam_hdr_parse(headerText.size(), headerText.c_str());
    std::string sequence = std::string(1000, 'A') + "GGAAATCCAAAC";
    std::string text = "prefix\t0\tchr1\t1\t60\t1000M2I3=2X5M\t*\t0\t0\t" + sequence + "\t" + std::string(sequence.size(), 'I');
    kstring_t line{0, 0, nullptr}; kputs(text.c_str(), &line);
    auto* record = bam_init1(); assert(sam_parse1(&line, header, record) >= 0); free(line.s);
    std::string reference(20, 'A');
    Utils::Region region("chr1", 995, 1015); region.refSeq = reference.c_str(); region.refSeqLen = 20;
    Segs::ReadCollection collection; collection.region = &region;
    collection.readQueue.emplace_back(record); Segs::align_init(&collection.readQueue.back(), 0, false);
    collection.mmVector.resize(20);
    Themes::IniOptions options; options.max_coverage = 100; options.snp_threshold = 1000;
    Segs::findMismatches(options, collection);
    for (int i = 0; i < 20; ++i) {
        const auto& mm = collection.mmVector[i];
        assert(mm.T == (i == 8 ? 1u : 0u));
        assert(mm.C == (i == 9 || i == 10 || i == 14 ? 1u : 0u));
        assert(mm.A == 0 && mm.G == 0);
    }
    collection.clear(); sam_hdr_destroy(header);
}

static void testCustomThemePersistence() {
    auto directory = std::filesystem::temp_directory_path() /
        ("gw-theme-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    auto empty = std::filesystem::path();
    auto iniName = std::filesystem::path(".gw.ini");
    Themes::IniOptions opts;
    opts.ini_path = Themes::IniOptions::writeDefaultIni(directory, empty, iniName).string();
    assert(mINI::INIFile(opts.ini_path).read(opts.myIni));
    opts.getOptionsFromIni();
    opts.theme_str = "igv";
    opts.custom_theme_enabled = true;
    auto jsonPath = opts.customThemePath();
    std::ofstream(jsonPath) << R"({"BACKGROUND_MENU":[255,12,34,56],
        "NORMAL_READ":[73,41,82,123],"NUCLEOTIDE_A":[255,7,8,9],
        "NORMAL_READ_MQ0":[22,11,33,55],"OTHER_MODIFICATION":[127,91,92,93]})";
    assert(opts.loadCustomTheme(jsonPath));
    opts.rebuildTheme();
    for (int i = 0; i < 3; ++i) {
        opts.theme.setAlphas(); // Startup and render paths call this repeatedly.
        assert(opts.theme.bgMenu.getColor() == SkColorSetARGB(255,12,34,56));
        assert(opts.theme.fcNormal.getColor() == SkColorSetARGB(73,41,82,123));
        assert(opts.theme.fcNormal0.getColor() == SkColorSetARGB(22,11,33,55));
        assert(opts.theme.fcOther.getColor() == SkColorSetARGB(127,91,92,93));
        assert(opts.theme.BasePaints[1][10].getColor() == SkColorSetARGB(255,7,8,9));
        assert(SkColorGetR(opts.theme.ModPaints[2][0].getColor()) == 91);
    }
    opts.custom_theme_colors[Themes::fcA] = SkColorSetARGB(128,7,8,9);
    opts.rebuildTheme();
    assert(opts.theme.BasePaints[1][10].getAlpha() == 128);
    assert(opts.theme.BasePaints[1][0].getAlpha() == 26);
    // Exercise every JSON color, including get/set support for menu and mates.
    const auto original = opts.custom_theme_colors;
    for (const auto& entry : Themes::themeColors())
        opts.custom_theme_colors[entry.paint] = SkColorSetARGB(19,27,37,47);
    assert(opts.saveSettings());
    Themes::IniOptions restored;
    restored.ini_path = opts.ini_path;
    assert(mINI::INIFile(restored.ini_path).read(restored.myIni));
    restored.getOptionsFromIni();
    assert(restored.custom_theme_enabled && restored.theme_str == "igv");
    assert(restored.custom_theme_colors == opts.custom_theme_colors);
    for (const auto& entry : Themes::themeColors()) {
        int a,r,g,b;
        restored.theme.getPaintARGB(entry.paint,a,r,g,b);
        assert(a == 19 && r == 27 && g == 37 && b == 47);
    }
    for (const auto& invalid : {R"({"UNKNOWN":[255,1,2,3]})",
            R"({"NUCLEOTIDE_A":[256,1,2,3]})", R"({"NUCLEOTIDE_A":[-1,1,2,3]})",
            R"({"NUCLEOTIDE_A":[0.5,1,2,3]})", R"({"NUCLEOTIDE_A":[00,1,2,3]})",
            R"({"NUCLEOTIDE_A":[255,1,2]})", R"({"NUCLEOTIDE_A":[255,1,2,3],})",
            R"({"NUCLEOTIDE_A":[255,1,2,3],"NUCLEOTIDE_A":[255,2,3,4]})",
            R"({"NUCLEOTIDE_A":[255,1,2,3]}garbage)", "{", ""}) {
        std::ofstream(jsonPath) << invalid;
        const auto colors = restored.custom_theme_colors;
        assert(!restored.loadCustomTheme(jsonPath) && !restored.custom_theme_error.empty());
        assert(restored.custom_theme_colors == colors);
    }
    opts.custom_theme_colors = original;
    opts.theme_str = "slate"; opts.rebuildTheme();
    assert(opts.theme.bgMenu.getColor() == SkColorSetARGB(255,12,34,56));
    opts.custom_theme_colors.erase(Themes::bgMenu); opts.rebuildTheme();
    Themes::SlateTheme slate;
    assert(opts.theme.bgMenu.getColor() == slate.bgMenu.getColor());
    std::string dark = "dark"; opts.setTheme(dark);
    assert(!opts.custom_theme_enabled && opts.theme.colorOverrides.empty());
    // Disabling does not discard the saved custom palette.
    opts.custom_theme_save_pending = true;
    assert(opts.saveSettings());
    opts.custom_theme_enabled = true; opts.custom_theme_colors.clear();
    assert(opts.loadCustomTheme(jsonPath) && !opts.custom_theme_colors.empty());
    opts.custom_theme_colors.clear(); opts.rebuildTheme();
    assert(opts.saveSettings()); // An enabled, empty palette is a valid base theme.
    assert(opts.loadCustomTheme(jsonPath) && opts.custom_theme_colors.empty());
    const std::string firstName = "Ocean \"blue\" \\ Night ☾";
    assert(opts.renameCustomTheme(firstName));
    opts.theme_str = "igv";
    opts.custom_theme_colors[Themes::bgPaint] = SkColorSetARGB(128,10,20,30);
    opts.rebuildTheme();
    assert(opts.saveSettings());
    assert(opts.createCustomTheme("Second", true));
    assert(opts.custom_theme_colors.at(Themes::bgPaint) == SkColorSetARGB(128,10,20,30));
    opts.theme_str = "slate";
    opts.custom_theme_colors[Themes::bgPaint] = SkColorSetARGB(100,40,50,60);
    assert(opts.createCustomTheme("Clean", false));
    assert(opts.custom_theme_colors.empty() && opts.theme_str == "slate");
    assert(opts.selectCustomTheme(firstName) && opts.theme_str == "igv");
    assert(opts.custom_theme_colors.at(Themes::bgPaint) == SkColorSetARGB(128,10,20,30));
    assert(opts.selectCustomTheme("Second") && opts.theme_str == "slate");
    assert(opts.custom_theme_colors.at(Themes::bgPaint) == SkColorSetARGB(100,40,50,60));
    assert(!opts.renameCustomTheme(firstName) && !opts.createCustomTheme("Second", false));
    assert(!opts.renameCustomTheme("  ") && !opts.createCustomTheme("bad\nname", false));
    assert(opts.custom_theme_name == "Second" && opts.custom_themes.size() == 3);
    assert(opts.selectCustomTheme(firstName) && opts.saveSettings());
    Themes::IniOptions collection;
    collection.ini_path = opts.ini_path;
    assert(mINI::INIFile(collection.ini_path).read(collection.myIni));
    collection.getOptionsFromIni();
    assert(collection.custom_themes.size() == 3 && collection.custom_theme_name == firstName);
    assert(collection.theme_str == "igv" && collection.custom_theme_enabled);
    assert(collection.selectCustomTheme("Second") && collection.theme_str == "slate");
    assert(collection.custom_theme_colors.at(Themes::bgPaint) == SkColorSetARGB(100,40,50,60));
    // A bad entry rejects the whole collection without losing any valid themes.
    for (const auto& invalid : {
            R"({"version":2,"themes":[]})", R"({"version":1,"themes":[]})",
            R"({"version":1,"themes":[{"name":"X","base":"bad","colors":{}}]})",
            R"({"version":1,"themes":[{"name":"X","base":"dark","colors":{}},{"name":"X","base":"igv","colors":{}}]})",
            R"({"version":1,"themes":[{"name":"X","base":"dark","colors":{}},{"name":"Y","base":"igv","colors":{"BACKGROUND":[-1,2,3,4]}}]})",
            R"({"name":"\ud800","base":"dark","colors":{}})",
            R"({"name":"\q","base":"dark","colors":{}})"}) {
        std::ofstream(jsonPath) << invalid;
        assert(!collection.loadCustomTheme(jsonPath));
        assert(collection.custom_themes.size() == 3 && collection.custom_theme_name == "Second");
        assert(collection.theme_str == "slate");
    }
    std::ofstream(jsonPath) << R"({"name":"Moon \u263e \ud83c\udf19","base":"dark","colors":{}})";
    assert(collection.loadCustomTheme(jsonPath));
    assert(collection.custom_theme_name == "Moon ☾ 🌙" && collection.custom_themes.size() == 1);
    assert(collection.saveSettings());
    assert(collection.loadCustomTheme(jsonPath) && collection.custom_theme_name == "Moon ☾ 🌙");
    opts.ini_path = (directory / "missing" / ".gw.ini").string();
    opts.settings_save_pending = true;
    assert(!opts.saveSettings() && !opts.settings_save_error.empty() && !opts.settings_save_pending);
    std::filesystem::remove_all(directory);
}

static void testThemeBackgroundReplacement() {
    Themes::IniOptions opts;
    opts.dimensions = {80,60}; opts.theme = Themes::DarkTheme();
    opts.theme.colorOverrides[Themes::bgPaint] = SkColorSetARGB(100,30,60,90);
    opts.theme.setAlphas();
    std::vector<std::string> files;
    std::vector<Utils::Region> regions;
    Manager::GwPlot plot("", files, opts, regions, files);
    plot.setImageSize(80,60); plot.makeRasterSurface();
    plot.showUIOverlay = false;
    plot.rasterCanvas->clear(SK_ColorRED);
    plot.drawBackground();
    SkPixmap pixels;
    auto first = plot.rasterSurface->makeImageSnapshot(); assert(first->peekPixels(&pixels));
    const auto expected = pixels.getColor(20,20);
    assert(SkColorGetA(expected) == 100);
    for (int i = 0; i < 5; ++i) plot.drawBackground();
    auto repeated = plot.rasterSurface->makeImageSnapshot(); assert(repeated->peekPixels(&pixels));
    assert(pixels.getColor(20,20) == expected); // No accumulating opacity or old colors.
    plot.imageCacheQueue.emplace_back(plot.frameId, repeated);
    SkBitmap target; target.allocN32Pixels(80,60); SkCanvas canvas(target);
    canvas.clear(SK_ColorGREEN);
    for (int i = 0; i < 5; ++i) {
        Drawing::drawFrameImage(&canvas, repeated);
        assert(target.getColor(20,20) == expected); // Cached frame copy must also replace.
    }
    for (auto paint : {plot.opts.theme.bgPaintTiled, plot.opts.theme.bgMenu}) {
        paint.setARGB(80,90,60,30);
        canvas.clear(SK_ColorBLUE); canvas.drawPaint(paint);
        const auto color = target.getColor(20,20);
        for (int i = 0; i < 5; ++i) canvas.drawPaint(paint);
        assert(target.getColor(20,20) == color && SkColorGetA(color) == 80);
    }
}

static void testThemePreviewRendering() {
    Themes::IniOptions opts;
    opts.theme = Themes::DarkTheme(); opts.theme.setAlphas();
    const auto originalColor = opts.theme.bgPaint.getColor();
    auto before = Menu::renderThemePreview(opts, 900, 560);
    assert(before && before->width() == 900 && before->height() == 560);
    SkPixmap first; assert(before->peekPixels(&first));
    assert(first.getColor(890,550) == originalColor);
    opts.theme.colorOverrides[Themes::bgPaint] = SkColorSetARGB(255,22,88,155);
    opts.theme.colorOverrides[Themes::fcNormal] = SkColorSetARGB(255,220,35,165);
    opts.theme.setAlphas();
    auto after = Menu::renderThemePreview(opts, 900, 560);
    SkPixmap second; assert(after && after->peekPixels(&second));
    assert(second.getColor(890,550) == SkColorSetARGB(255,22,88,155));
    assert(second.getColor(500,160) == SkColorSetARGB(255,220,35,165));
    assert(first.getColor(500,160) != second.getColor(500,160));
    auto resized = Menu::renderThemePreview(opts, 600, 373);
    assert(resized && resized->width() == 600 && resized->height() == 373);
    assert(!Menu::renderThemePreview(opts, 0, 560));
}

static void testThemeCommandSelection() {
    auto directory = std::filesystem::temp_directory_path() /
        ("gw-theme-command-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    auto empty = std::filesystem::path(), iniName = std::filesystem::path(".gw.ini");
    Themes::IniOptions options;
    options.ini_path = Themes::IniOptions::writeDefaultIni(directory, empty, iniName).string();
    assert(mINI::INIFile(options.ini_path).read(options.myIni)); options.getOptionsFromIni();
    std::ofstream(options.customThemePath()) << R"({"version":1,"themes":[
        {"name":"Ocean blue","base":"igv","colors":{"BACKGROUND":[255,12,34,56]}},
        {"name":"Paper","base":"slate","colors":{}},
        {"name":"dark","base":"igv","colors":{"BACKGROUND":[255,65,43,21]}}
    ]})";
    std::vector<std::string> files; std::vector<Utils::Region> regions;
    Manager::GwPlot plot("", files, options, regions, files);
    auto command = [&](std::string text) {
        std::ostringstream output;
        Commands::run_command_map(&plot, text, output);
        return output.str();
    };
    auto frame = Menu::renderThemePreview(options, 100, 60);
    plot.imageCache[0] = frame; plot.imageCacheQueue.emplace_back(plot.frameId, frame);
    command("theme Ocean blue"); // Loads names lazily before the settings tab is visited.
    assert(plot.opts.custom_theme_enabled && plot.opts.custom_theme_name == "Ocean blue");
    assert(plot.opts.theme_str == "igv" && plot.opts.theme.bgPaint.getColor() == SkColorSetARGB(255,12,34,56));
    assert(plot.imageCache.empty() && plot.imageCacheQueue.empty() && plot.redraw && !plot.processed);
    command("theme dark"); assert(!plot.opts.custom_theme_enabled && plot.opts.theme_str == "dark");
    command("theme \"Ocean blue\""); assert(plot.opts.custom_theme_enabled && plot.opts.custom_theme_name == "Ocean blue");
    command("theme 'Paper'"); assert(plot.opts.custom_theme_name == "Paper" && plot.opts.theme_str == "slate");
    command("theme custom:dark");
    assert(plot.opts.custom_theme_enabled && plot.opts.custom_theme_name == "dark" && plot.opts.theme_str == "igv");
    command("theme Missing"); assert(plot.opts.custom_theme_name == "dark" && plot.opts.custom_theme_enabled);
    const auto help = command("theme");
    assert(help.find("Ocean blue") != std::string::npos && help.find("Paper") != std::string::npos);
    assert(plot.opts.saveSettings());
    Themes::IniOptions restored; restored.ini_path = options.ini_path;
    assert(mINI::INIFile(restored.ini_path).read(restored.myIni)); restored.getOptionsFromIni();
    assert(restored.custom_theme_enabled && restored.custom_theme_name == "dark");
    std::filesystem::remove_all(directory);
}

int main() {
    testThemeCommandSelection();
    testThemePreviewRendering();
    testThemeBackgroundReplacement();
    testCustomThemePersistence();
    testReferenceWindowOwnership();
    testHiddenMismatchPrefix();
    testLeftClipDrawingPastRightEdge();
    testStableRowGaps();
    testHaplotypeRows();
    testCoverageBaseCounts();
    testBufferedScrolling(false);
    testBufferedScrolling(true);
    testBufferedScrolling(false, Utils::SortType::STRAND);
    testBufferedScrolling(false, Utils::SortType::HP);
    Themes::IniOptions opts;
    opts.theme = Themes::IgvTheme();
    opts.theme.setAlphas();
    opts.labels = "PASS,FAIL,REVIEW";
    opts.number = {1, 1};
    std::vector<std::string> bams, tracks;
    std::vector<Utils::Region> regions;
    std::string reg = "chr1:10-100";
    regions.push_back(Utils::parseRegion(reg));
    Manager::GwPlot plot("", bams, opts, regions, tracks);
    plot.setImageSize(1000, 700);
    plot.showUIOverlay = true;
    plot.setScaling();
    std::string variantPath = (std::filesystem::temp_directory_path() /
        ("gw-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".vcf")).string();
    std::ofstream(variantPath) << "##fileformat=VCFv4.2\n##contig=<ID=chr1,length=1000>\n"
        "##INFO=<ID=END,Number=1,Type=Integer,Description=\"End\">\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n"
        "chr1\t20\tvariant\tA\t<DEL>\t60\tPASS\tEND=40\n";
    plot.addVariantTrack(variantPath, 0, false, true);
    plot.variantTracks[0].nextN(1);
    auto& label = plot.variantTracks[0].multiLabels.at(0);
    label.labels = {"PASS", "FAIL", "REVIEW"}; label.i = 0;
    plot.labelTableDialogOpen = true;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1000, 700};
    io.DeltaTime = 1.0f/60;
    unsigned char* pixels; int w, h;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &w, &h);
    ImGui::GetPlatformIO().Platform_SetClipboardTextFn = [](ImGuiContext*, const char* text) { clipboard = text; };
    {
        ImGui::FileBrowser browser(ImGuiFileBrowserFlags_EnterNewFilename |
            ImGuiFileBrowserFlags_MultipleSelection | ImGuiFileBrowserFlags_ConfirmOnEnter);
        browser.SetTitle("Path entry regression");
        const auto directory = browser.GetDirectory();
        std::vector<std::pair<std::string, std::string>> paths{
            {"reads with spaces.bam", (directory / "reads with spaces.bam").string()},
            {variantPath, variantPath},
            {"https://example.org/data/reads.bam", "https://example.org/data/reads.bam"},
            {"ftp://example.org/variants.vcf.gz", "ftp://example.org/variants.vcf.gz"},
            {"~/reads.bam", (std::filesystem::path(std::getenv("HOME")) / "reads.bam").string()}
        };
        for (const auto& path : paths) {
            browser.SetInputName(path.first); browser.Open();
            auto browserFrame = [&]() { ImGui::NewFrame(); browser.Display(); ImGui::Render(); };
            browserFrame(); browserFrame();
            assert(browser.PeekSelected().at(0).string() == path.second);
            io.AddKeyEvent(ImGuiKey_Enter, true); browserFrame();
            assert(browser.HasSelected());
            assert(browser.GetSelected().string() == path.second);
            assert(browser.GetMultiSelected().at(0).string() == path.second);
            io.AddKeyEvent(ImGuiKey_Enter, false); browserFrame();
            browser.ClearSelected();
        }
    }
    {
        // Widths below the former 500px minimum made Settings alternate sizes
        // every frame. Keep the footer fixed while only the options scroll.
        const auto originalGeneral = plot.opts.myIni["general"];
        for (const char* key : {"coverage", "log2_cov", "expand_tracks", "scale_bar",
                               "data_labels", "sv_arcs", "mods", "vcf_as_tracks"})
            plot.opts.myIni["general"][key] = "true";
        bool open = true, redraw = false;
        for (const ImVec2 size : {ImVec2(300, 180), ImVec2(480, 300), ImVec2(1000, 700)}) {
            io.DisplaySize = size;
            bool scrollbar = false;
            for (int f = 0; f < 12; ++f) {
                ImGui::NewFrame();
                Menu::drawImGuiSettings(&plot, &open, redraw, nullptr);
                ImGui::Render();
                auto* settings = ImGui::FindWindowByName("Settings");
                assert(settings && settings->Size.x == size.x && settings->Size.y == size.y);
                assert(settings->Pos.x == 0 && settings->Pos.y == 0 && !settings->ScrollbarY);
                ImGuiWindow* content = nullptr;
                for (auto* child : settings->DC.ChildWindows)
                    if (std::strstr(child->Name, "##settings_content")) content = child;
                assert(content);
                assert(content->Pos.y + content->Size.y + ImGui::GetFrameHeight() <=
                       settings->InnerRect.Max.y);
                if (f == 3) scrollbar = content->ScrollbarY;
                if (f > 3) assert(content->ScrollbarY == scrollbar);
            }
            if (size.y == 180) assert(scrollbar);
        }
        plot.opts.myIni["general"] = originalGeneral;
        io.DisplaySize = ImVec2(1000, 700);
    }
    // Escape dismisses only the foremost dialog, including when a file
    // browser modal is layered over ordinary windows.
    {
        plot.helpDialogOpen = true;
        plot.commandDialogOpen = true;
        plot.showCommandStatus = true;
        plot.lastCommandOutputAnsi = "Error: command not understood\n";
        plot.lastCommandOutputFrame = 100;
        auto dialogFrame = [&]() {
            ImGui::NewFrame();
            bool redraw = false;
            Menu::drawImGuiHelpDialog(&plot, &plot.helpDialogOpen, redraw);
            Menu::drawImGuiCommandDialog(&plot, &plot.commandDialogOpen, redraw);
            Menu::drawImGuiCommandStatus(&plot);
            Menu::drawImGuiOpenDialog(&plot, &plot.openDialogOpen, redraw);
            ImGui::Render();
        };
        auto escape = [&]() {
            io.AddKeyEvent(ImGuiKey_Escape, true); dialogFrame();
            io.AddKeyEvent(ImGuiKey_Escape, false); dialogFrame();
        };
        dialogFrame(); dialogFrame();
        assert(Menu::hasImGuiEscapeTarget());
        ImGui::BringWindowToDisplayFront(ImGui::FindWindowByName("Output"));
        escape();
        assert(!plot.showCommandStatus && plot.helpDialogOpen && plot.commandDialogOpen);
        assert(!plot.lastCommandOutputAnsi.empty());
        plot.openDialogOpen = true;
        dialogFrame(); dialogFrame();
        escape();
        assert(!plot.openDialogOpen && plot.helpDialogOpen && plot.commandDialogOpen);
        ImGui::BringWindowToDisplayFront(ImGui::FindWindowByName("Tools"));
        escape();
        assert(!plot.commandDialogOpen && plot.helpDialogOpen);
        escape();
        assert(!plot.helpDialogOpen);
        dialogFrame();
        assert(!Menu::hasImGuiEscapeTarget());
    }
    bool scrollbars = false;
    auto frame = [&]() {
        ImGui::NewFrame();
        bool redraw = false;
        Menu::drawImGuiLabelTableDialog(&plot, redraw);
        if (scrollbars) Menu::drawImGuiAlignmentScrollbars(&plot);
        Menu::drawImGuiTrackPopup(&plot);
        ImGui::Render();
    };
    auto click = [&](float x, float y) {
        io.AddMousePosEvent(x, y); frame();
        io.AddMouseButtonEvent(0, true); frame();
        io.AddMouseButtonEvent(0, false); frame();
    };
    frame(); frame();
    auto* variantWindow = ImGui::FindWindowByName("Variants");
    assert(variantWindow->Pos.y == 0 && variantWindow->Size.y == io.DisplaySize.y);
    assert(variantWindow->WindowBorderSize == 0 && variantWindow->WindowRounding == 0);
    float titleH = ImGui::GetFrameHeight();
    float dockW = titleH - 4;
    float dockX = variantWindow->Pos.x + variantWindow->Size.x - titleH - dockW/2 - 4;
    float dockY = variantWindow->Pos.y + titleH/2;
    click(dockX, dockY);
    assert(!plot.labelTableDocked);
    frame();
    assert(variantWindow->Size.y < io.DisplaySize.y - 24);
    dockX = variantWindow->Pos.x + variantWindow->Size.x - titleH - dockW/2 - 4;
    dockY = variantWindow->Pos.y + titleH/2;
    click(dockX, dockY);
    assert(plot.labelTableDocked);
    frame();
    auto table = [&](int columns) -> ImGuiTable* {
        for (int i=0; i<GImGui->Tables.GetMapSize(); ++i) {
            auto* t = GImGui->Tables.TryGetMapData(i);
            if (t && t->ColumnsCount == columns) return t;
        }
        return nullptr;
    };
    auto* master = table(3);
    assert(master);
    click(master->Columns[2].WorkMinX + 10, master->RowPosY1 + ImGui::GetTextLineHeight()/2);
    assert(label.i == 1);
    assert(label.clicked && !label.savedDate.empty());
    assert(plot.mode == Manager::Show::SINGLE);
    auto* details = table(2);
    assert(details);
    click(details->Columns[0].WorkMinX + 10, details->OuterRect.Min.y + ImGui::GetTextLineHeight()/2);
    assert(clipboard == "chr1:" + std::to_string(label.pos));
    click(details->Columns[0].WorkMinX + 10, details->OuterRect.Min.y + ImGui::GetTextLineHeight()*1.5f + ImGui::GetStyle().CellPadding.y*2);
    assert(clipboard == "chr1:40");
    assert(ImGui::FindWindowByName("Variants")->ScrollMax.y == 0);
    auto* listWindow = master->OuterWindow;
    float headerX = listWindow->Pos.x + 20;
    float headerY = listWindow->Pos.y - ImGui::GetStyle().ItemSpacing.y - ImGui::GetFrameHeight()/2;
    click(headerX, headerY);
    assert(!listWindow->Active);
    click(headerX, headerY);
    assert(listWindow->Active);
    details = table(2);
    auto* detailWindow = details->OuterWindow;
    float detailHeaderY = detailWindow->Pos.y - ImGui::GetFrameHeightWithSpacing() - ImGui::GetStyle().ItemSpacing.y - ImGui::GetFrameHeight()/2;
    click(variantWindow->WorkRect.Max.x - ImGui::GetFrameHeight()/2, detailHeaderY);
    assert(clipboard == "chr1\t20\tvariant\tA\t<DEL>\t60\tPASS\tEND=40");
    assert(detailWindow->Active); // Copy must not collapse the details pane.
    plot.currentVarTrack->blockStart = 7;
    plot.mouseOverTileIndex = 3;
    plot.redraw = true;
    std::string varOutput;
    assert(Commands::var_record(&plot, 0, varOutput));
    assert(varOutput == clipboard);
    assert(plot.currentVarTrack->blockStart == 7 && plot.mouseOverTileIndex == 3 && plot.redraw);
    assert(!Commands::var_record(&plot, 999, varOutput));
    plot.currentVarTrack->blockStart = 0;
    plot.mouseOverTileIndex = 0;
    std::string varCommand = "var";
    std::ostringstream varTerminalOutput;
    Commands::run_command_map(&plot, varCommand, varTerminalOutput);
    assert(varTerminalOutput.str().find(clipboard) != std::string::npos);
    click(detailWindow->Pos.x + 20, detailHeaderY);
    assert(!detailWindow->Active);
    assert(plot.drawingWidth() == 740);
    io.AddMousePosEvent(743, 200); frame();
    io.AddMouseButtonEvent(0, true); frame();
    io.AddMousePosEvent(783, 200); frame();
    io.AddMouseButtonEvent(0, false); frame();
    assert(plot.labelTableWidth == 220);
    assert(plot.drawingWidth() == 780);
    plot.labelTableWidth = 260;
    plot.labelTableDocked = false;
    assert(plot.drawingWidth() == 1000);
    plot.labelTableDocked = true;
    plot.labelTableWidth = 400;
    assert(plot.drawingWidth() == 600);
    plot.regions.push_back(plot.regions[0]);
    plot.setScaling();
    assert(plot.regionWidth == 300);
    plot.regions.resize(1);
    plot.regionSelection = 0;
    plot.labelTableDialogOpen = false;
    assert(plot.drawingWidth() == 1000);
    assert(ImGui::FindWindowByName("Variants")->ScrollMax.y == 0);
    plot.collections.emplace_back();
    auto& cl = plot.collections.back();
    cl.region = &plot.regions[0]; cl.regionIdx = 0;
    cl.xOffset = 10; cl.regionPixels = 700; cl.yOffset = 100;
    cl.covArr.assign(100, 0);
    cl.covArr.front() = 100; cl.covArr.back() = -100; cl.maxCoverage = 100;
    plot.samMaxY = 10; plot.opts.ylim = 10; plot.opts.alignments = true;
    plot.opts.tlen_yscale = false; plot.trackY = 300;
    scrollbars = true;
    io.AddMousePosEvent(702, 200); frame(); frame();
    io.AddMouseButtonEvent(0, true); frame();
    assert(cl.vScroll > 0); // Small view updates during drag.
    io.AddMouseButtonEvent(0, false); frame();
    cl.vScroll = 0;
    plot.opts.low_memory = 1;
    io.AddMousePosEvent(702, 280); frame();
    io.AddMouseButtonEvent(0, true); frame();
    assert(cl.vScroll == 0); // Streamed view waits for release.
    io.AddMouseButtonEvent(0, false); frame();
    assert(cl.vScroll > 0);
    for (const auto& theme : {"igv", "dark", "slate"}) {
        Themes::applyImGuiTheme(theme);
        io.AddMousePosEvent(702, 200); frame(); frame();
        auto* sliderWindow = ImGui::FindWindowByName("##alignment_scroll_0");
        assert(sliderWindow);
        ImU32 blueBackground = ImGui::GetColorU32(ImVec4(0.26f, 0.59f, 0.98f, 0.40f));
        bool foundBlue = false;
        for (const auto& vertex : sliderWindow->DrawList->VtxBuffer)
            foundBlue |= vertex.col == blueBackground;
        assert(foundBlue);
    }
    // Feature data starts closed, fits one line when open and grows downward.
    scrollbars = false;
    Manager::GwPlot::TrackPopup popup;
    popup.uid = 12345;
    popup.ansi = "\r\033[1mfeatures.bed\033[0m    \033[36mchr1:20-40\033[0m\n";
    popup.details = "chr1\t20\t40\tfeature\t10\t+";
    popup.x = 100; popup.y = 100;
    plot.trackPopups.push_back(popup);
    frame(); frame();
    auto* popupWindow = ImGui::FindWindowByName("Track info##trackpopup12345");
    assert(popupWindow && !plot.trackPopups.back().detailsOpen);
    float collapsedHeight = popupWindow->Size.y;
    float popupTop = popupWindow->Pos.y;
    float featureHeaderX = popupWindow->Pos.x + 30;
    float featureHeaderY = popupWindow->Pos.y + collapsedHeight - ImGui::GetStyle().WindowPadding.y - ImGui::GetTextLineHeight()/2;
    click(featureHeaderX, featureHeaderY);
    assert(plot.trackPopups.back().detailsOpen);
    assert(popupWindow->Pos.y == popupTop);
    frame(); frame();
    assert(popupWindow->Size.y > collapsedHeight);
    assert(popupWindow->Size.y - collapsedHeight < 60);
    click(featureHeaderX, featureHeaderY);
    assert(!plot.trackPopups.back().detailsOpen);
    frame(); frame();
    assert(popupWindow->Size.y == collapsedHeight);
    scrollbars = true;
    io.AddMousePosEvent(702, 200); frame(); frame();
    auto* sliderWindow = ImGui::FindWindowByName("##alignment_scroll_0"); assert(sliderWindow);
    // A previously focused scrollbar must return below existing dialogs.
    ImGui::BringWindowToDisplayFront(sliderWindow);
    frame();
    auto& windowOrder = ImGui::GetCurrentContext()->Windows;
    assert(std::find(windowOrder.begin(), windowOrder.end(), sliderWindow) <
           std::find(windowOrder.begin(), windowOrder.end(), popupWindow));
    scrollbars = false;

    // Non-square grids fill the dock-adjusted area, with consistent small gutters.
    plot.labelTableDialogOpen = true;
    plot.labelTableWidth = 260;
    plot.opts.number = {3, 2};
    auto boxes = plot.tileBoundingBoxes();
    assert(boxes.size() == 6);
    assert(std::abs(boxes.back().xEnd - (plot.drawingWidth() - 6 * plot.monitorScale)) < 0.01f);
    assert(std::abs(boxes.back().yEnd - (plot.fb_height - 6 * plot.monitorScale)) < 0.01f);
    for (const auto& box : boxes) {
        assert(std::abs(box.width - (box.xEnd - box.xStart)) < 0.01f);
        assert(std::abs(box.height - (box.yEnd - box.yStart)) < 0.01f);
    }
    auto tile = plot.renderVariantTile(0, boxes[0]);
    assert(tile && tile->width() >= 200 && tile->height() >= 100);
    assert(std::abs((double)tile->width()/tile->height() - boxes[0].width/boxes[0].height) < 0.01);
    assert(plot.fb_width == 1000 && plot.fb_height == 700 && plot.showUIOverlay);
    // Exercise cell-sized rendering with a real indexed alignment and chr aliases.
    std::string referencePath = variantPath + ".fa";
    std::string bamPath = variantPath + ".bam";
    std::ofstream(referencePath) << ">1\n" << std::string(1000, 'A') << "\n";
    auto* output = sam_open(bamPath.c_str(), "wb");
    const std::string headerText = "@HD\tVN:1.6\tSO:coordinate\n@SQ\tSN:chr1\tLN:1000\n";
    auto* header = sam_hdr_parse(headerText.size(), headerText.c_str());
    assert(output && header && sam_hdr_write(output, header) >= 0);
    kstring_t line{0, 0, nullptr};
    kputs("synthetic\t0\tchr1\t20\t60\t20M\t*\t0\t0\tAAAAAAAAAACGTAAAAAAA\tIIIIIIIIIIIIIIIIIIII", &line);
    auto* record = bam_init1();
    assert(sam_parse1(&line, header, record) >= 0);
    assert(sam_write1(output, header, record) >= 0);
    bam_destroy1(record);
    free(line.s);
    sam_hdr_destroy(header);
    assert(sam_close(output) == 0);
    assert(sam_index_build(bamPath.c_str(), 0) == 0);
    {
        std::vector<std::string> paths{bamPath};
        Manager::GwPlot tilePlot(referencePath, paths, opts, regions, tracks);
        tilePlot.setImageSize(1000, 700);
        tilePlot.showUIOverlay = true;
        tilePlot.labelTableDialogOpen = true;
        tilePlot.opts.number = {3, 2};
        tilePlot.samMaxY = opts.ylim;
        tilePlot.setScaling();
        // A reference/translation-lane drag must finish as a pan, not toggle
        // the reading frame and discard the buffered alignment layout on release.
        tilePlot.mode = Manager::Show::SINGLE;
        tilePlot.opts.show_translation = true;
        tilePlot.opts.scale_bar = false;
        tilePlot.setScaling();
        tilePlot.opts.dimensions = {1000, 700};
        tilePlot.makeRasterSurface(); tilePlot.drawScreen();
        assert(!tilePlot.collections[0].readQueue.empty());
        // Exercise rendered mismatches through mouse selection and popup copy,
        // including genomic coordinates where float addition loses single bases.
        auto& coverageCollection = tilePlot.collections[0];
        for (int shift : {0, 100000000}) {
            coverageCollection.region->start += shift;
            coverageCollection.region->end += shift;
            auto& read = coverageCollection.readQueue[0];
            read.pos += shift; read.reference_end += shift; read.delegate->core.pos += shift;
            for (int relativePos : {29, 30, 31}) {
                const int pos = relativePos + shift;
                tilePlot.xPos_fb = coverageCollection.xOffset + (pos - coverageCollection.region->start + 0.5f) * coverageCollection.xScaling;
                tilePlot.yPos_fb = coverageCollection.yOffset - tilePlot.covY * 0.5f;
                tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
                tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
                assert(!tilePlot.covPopups.empty());
                const std::string expected = std::string(1, "CGT"[relativePos - 29]) + ":1";
                assert(tilePlot.covPopups.back().ansi.find(expected) != std::string::npos);
                assert(tilePlot.covPopups.back().ansi.find("A:1") == std::string::npos);
                // Verify the popup's displayed counts via its clipboard action.
                auto coverageFrame = [&]() {
                    ImGui::NewFrame(); Menu::drawImGuiCovPopup(&tilePlot); ImGui::Render();
                };
                coverageFrame(); coverageFrame();
                const auto& popup = tilePlot.covPopups.back();
                const std::string windowName = popup.chromPos + "##covpopup" + std::to_string(popup.uid);
                auto* window = ImGui::FindWindowByName(windowName.c_str()); assert(window);
                const float title = ImGui::GetFrameHeight();
                io.AddMousePosEvent(window->Pos.x + window->Size.x - title * 1.5f, window->Pos.y + title * 0.5f);
                coverageFrame(); io.AddMouseButtonEvent(0, true); coverageFrame();
                io.AddMouseButtonEvent(0, false); coverageFrame();
                const std::string counts = relativePos == 29 ? "1\t0\t0\t1\t0\t" :
                                           relativePos == 30 ? "1\t0\t0\t0\t1\t" : "1\t0\t1\t0\t0\t";
                assert(clipboard == counts + popup.chromPos);
                tilePlot.covPopups.clear();
            }
            coverageCollection.region->start -= shift;
            coverageCollection.region->end -= shift;
            read.pos -= shift; read.reference_end -= shift; read.delegate->core.pos -= shift;
        }
        tilePlot.collections[0].readQueue[0].y = 5;
        // Dragging beyond the top scroll limit must preserve the existing rows,
        // including gaps left by horizontal panning. Consume blocked motion so
        // reversing direction can scroll immediately during the same gesture.
        auto& dragCollection = tilePlot.collections[0];
        auto levelsBefore = dragCollection.levelsStart;
        auto endsBefore = dragCollection.levelsEnd;
        tilePlot.setCollectionVScroll(0, 0);
        assert(dragCollection.readQueue[0].y == 5);
        double dragY = dragCollection.yOffset + 60;
        tilePlot.xPos_fb = 200; tilePlot.yPos_fb = dragY;
        tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        tilePlot.mousePos(200, dragY + 20);
        assert(dragCollection.vScroll == 0 && dragCollection.readQueue[0].y == 5);
        tilePlot.mousePos(200, dragY + 40);
        assert(dragCollection.vScroll == 0 && dragCollection.readQueue[0].y == 5);
        assert(dragCollection.levelsStart == levelsBefore && dragCollection.levelsEnd == endsBefore);
        tilePlot.mousePos(200, dragY + 20);
        assert(dragCollection.vScroll > 0);
        tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        tilePlot.setCollectionVScroll(0, 0);
        dragCollection.readQueue[0].y = 5;
        float laneHeight = tilePlot.fonts.overlayHeight * 0.55f;
        float laneSpacing = tilePlot.gap * 0.35f;
        float laneY = tilePlot.refSpace - tilePlot.ctx.translationTrackHeight + tilePlot.gap * 1.5f +
                      laneHeight + laneSpacing + laneHeight * 0.5f;
        int oldFrame = tilePlot.opts.translation_frame;
        bool oldLock = tilePlot.translation_row_locked;
        tilePlot.xPos_fb = 200; tilePlot.yPos_fb = laneY;
        tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        tilePlot.xPos_fb = 220;
        tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        assert(tilePlot.processed && tilePlot.regions[0].start < regions[0].start);
        assert(tilePlot.opts.translation_frame == oldFrame && tilePlot.translation_row_locked == oldLock);
        assert(tilePlot.collections[0].readQueue[0].y == 5);
        // An ordinary lane click still selects its frame, without rebuilding BAMs.
        tilePlot.xPos_fb = 200; tilePlot.yPos_fb = laneY;
        tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        assert(tilePlot.translation_row_locked && tilePlot.opts.translation_frame == 1);
        assert(tilePlot.processed && tilePlot.collections[0].readQueue[0].y == 5);
        tilePlot.opts.show_translation = false;
        tilePlot.setScaling();
        // The reference bases use the same buffered pan when translation is hidden.
        int referenceStart = tilePlot.regions[0].start;
        tilePlot.xPos_fb = 220; tilePlot.yPos_fb = tilePlot.refSpace - tilePlot.fonts.overlayHeight * 0.5f;
        tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        tilePlot.xPos_fb = 200;
        tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        assert(tilePlot.processed && tilePlot.regions[0].start > referenceStart);
        assert(tilePlot.collections[0].readQueue[0].y == 5);
        // Replay continuous, subpixel moves of the bottom genome box. The same
        // retained BAM record and its chosen row must survive every cursor event.
        auto& sliderRegion = tilePlot.regions[0];
        sliderRegion.chromLen = 1000;
        sliderRegion.ideogramStart = 50; sliderRegion.ideogramEnd = 650;
        auto sliderX = [&](float position) {
            return sliderRegion.ideogramStart + position / sliderRegion.chromLen *
                (sliderRegion.ideogramEnd - sliderRegion.ideogramStart);
        };
        auto* anchoredRecord = tilePlot.collections[0].readQueue[0].delegate;
        int sliderStart = sliderRegion.start;
        tilePlot.xPos_fb = sliderX(sliderStart + 0.25f);
        tilePlot.yPos_fb = tilePlot.fb_height - tilePlot.sliderSpace + tilePlot.gap;
        double sliderY = tilePlot.yPos_fb;
        tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        assert(tilePlot.processed);
        for (int n = 0; n < 30; ++n) {
            float position = sliderStart + 0.25f + (n < 15 ? n : 29 - n) * 0.2f;
            tilePlot.mousePos(sliderX(position), sliderY);
            assert(tilePlot.processed);
            assert(tilePlot.collections[0].readQueue[0].delegate == anchoredRecord);
            assert(tilePlot.collections[0].readQueue[0].y == 5);
            tilePlot.drawScreen();
            assert(tilePlot.collections[0].readQueue[0].delegate == anchoredRecord);
            assert(tilePlot.collections[0].readQueue[0].y == 5);
        }
        tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        // A distant jump still takes the full-fetch path.
        tilePlot.xPos_fb = sliderX(500);
        tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_PRESS, 0);
        assert(!tilePlot.processed && sliderRegion.start > sliderStart + 100);
        tilePlot.mouseButton(GLFW_MOUSE_BUTTON_LEFT, GLFW_RELEASE, 0);
        tilePlot.opts.translation_frame = oldFrame;
        tilePlot.regions[0].start = regions[0].start; tilePlot.regions[0].end = regions[0].end;
        tilePlot.fetchRefSeq(tilePlot.regions[0]); tilePlot.processed = false;
        tilePlot.setScaling();
        tilePlot.addVariantTrack(variantPath, 0, false, true);
        tilePlot.currentVarTrack = &tilePlot.variantTracks[0];
        tilePlot.currentVarTrack->nextN(1);
        for (const auto& box : tilePlot.tileBoundingBoxes()) {
            auto rendered = tilePlot.renderVariantTile(0, box);
            assert(rendered && rendered->width() >= 200 && rendered->height() >= 100);
            assert(std::abs((double)rendered->width()/rendered->height() - box.width/box.height) < 0.01);
            assert(!tilePlot.collections.empty() && !tilePlot.collections[0].readQueue.empty());
        }
        assert(tilePlot.fb_width == 1000 && tilePlot.fb_height == 700 && tilePlot.showUIOverlay);
        tilePlot.opts.data_labels = true;
        tilePlot.currentVarTrack->multiRegions.push_back(tilePlot.currentVarTrack->multiRegions[0]);
        tilePlot.currentVarTrack->multiLabels.push_back(tilePlot.currentVarTrack->multiLabels[0]);
        auto box = tilePlot.tileBoundingBoxes()[0];
        tilePlot.opts.number = {1, 1};
        assert(tilePlot.tileDataLabelsEnabled());
        auto single = tilePlot.renderVariantTile(0, box);
        tilePlot.opts.number = {2, 1};
        assert(!tilePlot.tileDataLabelsEnabled());
        auto multiple = tilePlot.renderVariantTile(0, box);
        assert(tilePlot.opts.data_labels); // Rendering must preserve the user's setting.
        SkPixmap singlePixels, multiplePixels;
        assert(single->peekPixels(&singlePixels) && multiple->peekPixels(&multiplePixels));
        assert(std::memcmp(singlePixels.addr(), multiplePixels.addr(), singlePixels.computeByteSize()) != 0);

        tilePlot.prepareTileCache();
        tilePlot.imageCache[0] = multiple;
        tilePlot.imageCacheQueue.emplace_back(tilePlot.frameId, multiple);
        tilePlot.prepareTileCache();
        assert(tilePlot.imageCache.size() == 1 && tilePlot.imageCacheQueue.size() == 1);
        tilePlot.opts.number = {1, 1};
        tilePlot.prepareTileCache();
        assert(tilePlot.imageCache.empty() && tilePlot.imageCacheQueue.empty());
        std::string trackPath = variantPath + ".bed";
        std::ofstream(trackPath) << "chr1\t20\t40\tfeature\n";
        assert(tilePlot.addTrack(trackPath, false, false, true));
        auto beforeResize = tilePlot.renderVariantTile(0, box);
        tilePlot.imageCache[0] = beforeResize;
        tilePlot.tracks[0].height_fraction = 0.4;
        tilePlot.tracksLayoutDirty = true;
        tilePlot.opts.dimensions = {1000, 700};
        tilePlot.makeRasterSurface();
        tilePlot.mode = Manager::Show::SINGLE;
        tilePlot.processed = false;
        tilePlot.drawScreen();
        assert(tilePlot.imageCache.empty()); // Alignment redraw invalidates generated variants.
        auto afterResize = tilePlot.renderVariantTile(0, box);
        SkPixmap beforePixels, afterPixels;
        assert(beforeResize->peekPixels(&beforePixels) && afterResize->peekPixels(&afterPixels));
        assert(std::memcmp(beforePixels.addr(), afterPixels.addr(), beforePixels.computeByteSize()) != 0);
        tilePlot.removeTrack(0);
        std::filesystem::remove(trackPath);
    }
    std::filesystem::remove(bamPath);
    std::filesystem::remove(bamPath + ".bai");
    std::filesystem::remove(referencePath);
    std::filesystem::remove(referencePath + ".fai");
    // Indexed files load ahead, then extend the list through wheel scrolling alone.
    std::string indexedPath = variantPath + ".gz";
    std::string vcfText = "##fileformat=VCFv4.2\n##contig=<ID=chr1,length=4000>\n"
                          "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n";
    for (int n = 1; n <= 512; ++n)
        vcfText += "chr1\t" + std::to_string(n) + "\tv" + std::to_string(n) + "\tA\tC\t60\tPASS\t.\n";
    auto* compressed = bgzf_open(indexedPath.c_str(), "w");
    assert(compressed && bgzf_write(compressed, vcfText.data(), vcfText.size()) == (ssize_t)vcfText.size());
    assert(bgzf_close(compressed) == 0);
    assert(tbx_index_build(indexedPath.c_str(), 0, &tbx_conf_vcf) == 0);
    std::string bcfPath = variantPath + ".bcf";
    auto* source = bcf_open(indexedPath.c_str(), "r");
    auto* binary = bcf_open(bcfPath.c_str(), "wb");
    auto* binaryHeader = bcf_hdr_read(source);
    assert(binaryHeader && bcf_hdr_write(binary, binaryHeader) == 0);
    auto* variant = bcf_init();
    while (bcf_read(source, binaryHeader, variant) == 0)
        assert(bcf_write(binary, binaryHeader, variant) == 0);
    bcf_destroy(variant); bcf_hdr_destroy(binaryHeader);
    assert(bcf_close(binary) == 0); assert(bcf_close(source) == 0);
    assert(bcf_index_build(bcfPath.c_str(), 14) == 0);
    for (auto path : {indexedPath, bcfPath}) {
        std::vector<std::string> emptyPaths;
        Manager::GwPlot listPlot("", emptyPaths, opts, regions, emptyPaths);
        listPlot.setImageSize(1000, 700);
        listPlot.showUIOverlay = true;
        listPlot.setScaling();
        listPlot.addVariantTrack(path, 0, false, true);
        auto& listTrack = listPlot.variantTracks[0];
        listTrack.nextN(1);
        listPlot.labelTableDialogOpen = true;
        auto listFrame = [&]() {
            ImGui::NewFrame(); bool redraw = false;
            Menu::drawImGuiLabelTableDialog(&listPlot, redraw);
            ImGui::Render();
        };
        listFrame(); listFrame();
        size_t prefetched = listTrack.multiLabels.size();
        assert(prefetched >= 128 && prefetched < 512 && listTrack.blockStart == 0);
        auto firstLabel = listTrack.multiLabels[0];
        listTrack.vcf.printTargetRecord(firstLabel.variantId, firstLabel.chrom, firstLabel.pos);
        assert(!listTrack.vcf.variantString.empty());
        auto* listTable = table(3);
        assert(listTable);
        auto* scrollWindow = listTable->InnerWindow;
        io.AddMousePosEvent(scrollWindow->InnerRect.GetCenter().x, scrollWindow->InnerRect.GetCenter().y);
        listFrame();
        io.AddMouseWheelEvent(0, -1000);
        listFrame(); listFrame(); listFrame(); listFrame();
        assert(listTrack.multiLabels.size() > prefetched && listTrack.blockStart == 0);
        listTrack.iterateToIndex(1000);
        assert(listTrack.multiLabels.size() == 512 && listTrack.vcf.done);
        for (size_t n = 0; n < listTrack.multiLabels.size(); ++n)
            assert(listTrack.multiLabels[n].variantId == "v" + std::to_string(n + 1));
        listPlot.mode = Manager::Show::TILED;
        listPlot.scrollGesture(0, -1);
        assert(listTrack.blockStart == 1);
        listPlot.scrollGesture(0, 1);
        assert(listTrack.blockStart == 0 && listTrack.vcf.done);
    }
    std::filesystem::remove(indexedPath);
    std::filesystem::remove(indexedPath + ".tbi");
    std::filesystem::remove(bcfPath);
    std::filesystem::remove(bcfPath + ".csi");
    // INS reference endpoints must ignore insertion length, including inflated BCF rlen.
    std::string insertionVcf = variantPath + ".insertions.vcf";
    std::ofstream(insertionVcf) << "##fileformat=VCFv4.3\n##contig=<ID=chr1,length=200000>\n"
        "##INFO=<ID=SVTYPE,Number=1,Type=String,Description=\"Type\">\n"
        "##INFO=<ID=SVLEN,Number=1,Type=Integer,Description=\"Length\">\n"
        "##INFO=<ID=END,Number=1,Type=Integer,Description=\"End\">\n"
        "#CHROM\tPOS\tID\tREF\tALT\tQUAL\tFILTER\tINFO\n"
        "chr1\t100\tins_anchor\tA\t<INS>\t60\tPASS\tSVTYPE=INS;SVLEN=50000;END=100\n"
        "chr1\t200\tins_end\tA\t<INS:ME>\t60\tPASS\tSVTYPE=INS;END=205;SVLEN=100000\n"
        "chr1\t300\tins_no_end\tA\t<INS>\t60\tPASS\tSVTYPE=INS;SVLEN=30000\n"
        "chr1\t500\tdel_control\tA\t<DEL>\t60\tPASS\tSVTYPE=DEL;SVLEN=-50;END=550\n";
    std::string insertionBcf = insertionVcf + ".bcf";
    source = bcf_open(insertionVcf.c_str(), "r");
    binary = bcf_open(insertionBcf.c_str(), "wb");
    binaryHeader = bcf_hdr_read(source);
    assert(binaryHeader && bcf_hdr_write(binary, binaryHeader) == 0);
    variant = bcf_init();
    int insertionIndex = 0;
    while (bcf_read(source, binaryHeader, variant) == 0) {
        if (insertionIndex++ < 3) variant->rlen = 100001;
        assert(bcf_write(binary, binaryHeader, variant) == 0);
    }
    bcf_destroy(variant); bcf_hdr_destroy(binaryHeader);
    assert(bcf_close(source) == 0 && bcf_close(binary) == 0);
    assert(bcf_index_build(insertionBcf.c_str(), 14) == 0);
    for (auto path : {insertionVcf, insertionBcf}) {
        std::vector<std::string> emptyPaths;
        Manager::GwPlot insertionPlot("", emptyPaths, opts, regions, emptyPaths);
        insertionPlot.addVariantTrack(path, 0, false, true);
        auto& insertionTrack = insertionPlot.variantTracks[0];
        insertionTrack.nextN(4);
        const int expectedEnds[] = {100, 205, 300, 550};
        assert(insertionTrack.multiRegions.size() == 4);
        for (int n = 0; n < 4; ++n) {
            assert(insertionTrack.multiRegions[n].size() == 1);
            assert(insertionTrack.multiRegions[n][0].markers[0].second == expectedEnds[n]);
        }
        HGW::GwTrack rawTrack;
        rawTrack.fp = bcf_open(path.c_str(), "r");
        rawTrack.hdr = bcf_hdr_read(rawTrack.fp);
        rawTrack.v = bcf_init();
        int n = 0;
        while (bcf_read(rawTrack.fp, rawTrack.hdr, rawTrack.v) == 0) {
            Utils::TrackBlock feature;
            rawTrack.parseVcfRecord(feature);
            assert(feature.end == expectedEnds[n]);
            rawTrack.parseVcfRecord();
            assert(rawTrack.stop == expectedEnds[n++]);
        }
        assert(n == 4);
        bcf_destroy(rawTrack.v);
        bcf_hdr_destroy(rawTrack.hdr);
        assert(bcf_close(rawTrack.fp) == 0);
    }
    std::filesystem::remove(insertionVcf);
    std::filesystem::remove(insertionBcf);
    std::filesystem::remove(insertionBcf + ".csi");
    {
        // Exercise the real Settings widgets through ImGui's keyboard activation.
        // Save only to a temporary config, including during color picker edits.
        const auto originalOptions = plot.opts;
        auto directory = std::filesystem::temp_directory_path() /
            ("gw-theme-ui-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        auto empty = std::filesystem::path(), iniName = std::filesystem::path(".gw.ini");
        plot.opts = Themes::IniOptions();
        plot.opts.ini_path = Themes::IniOptions::writeDefaultIni(directory, empty, iniName).string();
        assert(mINI::INIFile(plot.opts.ini_path).read(plot.opts.myIni));
        plot.opts.getOptionsFromIni();
        bool open = true, redraw = false;
        auto settingsFrame = [&](ImGuiID activate = 0) {
            ImGui::NewFrame();
            if (activate) {
                auto& context = *ImGui::GetCurrentContext();
                context.NavActivateId = context.NavActivateDownId = activate;
                context.NavActivateFlags = ImGuiActivateFlags_PreferInput;
                context.NavInputSource = ImGuiInputSource_Keyboard;
            }
            Menu::drawImGuiSettings(&plot, &open, redraw, nullptr);
            ImGui::Render();
        };
        settingsFrame(); settingsFrame();
        auto* settings = ImGui::FindWindowByName("Settings");
        ImGuiWindow* content = nullptr;
        for (auto* child : settings->DC.ChildWindows)
            if (std::strstr(child->Name, "##settings_content")) content = child;
        assert(content);
        auto* bar = ImGui::GetCurrentContext()->TabBars.GetByKey(content->GetID("##settings_tabs"));
        assert(bar);
        ImGuiID customTab = 0;
        for (auto& tab : bar->Tabs)
            if (std::strcmp(ImGui::TabBarGetTabName(bar, &tab), "Custom theme") == 0) customTab = tab.ID;
        assert(customTab); bar->NextSelectedTabId = customTab;
        settingsFrame(); settingsFrame();
        ImGuiWindow* editor = nullptr;
        ImGuiWindow* preview = nullptr;
        for (auto* child : content->DC.ChildWindows) {
            if (std::strstr(child->Name, "##theme_editor")) editor = child;
            if (std::strstr(child->Name, "##theme_preview")) preview = child;
        }
        assert(editor && preview && preview->Pos.x >= editor->Pos.x + editor->Size.x);
        assert(preview->Size.x > 500 && preview->Size.y > 400);
        const ImGuiID editorID = editor->ID;
        settingsFrame(ImHashStr("Use custom colors", 0, editorID)); settingsFrame();
        assert(plot.opts.custom_theme_enabled);
        assert(std::filesystem::exists(plot.opts.customThemePath()));
        assert(!plot.opts.settings_save_pending && plot.opts.settings_save_error.empty());
        editor->StateStorage.SetInt(ImHashStr("Background and text", 0, editorID), 1);
        settingsFrame();
        const ImGuiID scope = ImHashStr("BACKGROUND", 0, editorID);
        const ImGuiID color = ImHashStr("Background", 0, scope);
        const ImGuiID redInput = ImHashStr("##X", 0, color);
        auto readJson = [&]() {
            std::ifstream in(plot.opts.customThemePath());
            return std::string(std::istreambuf_iterator<char>(in), {});
        };
        const auto beforeEdit = readJson();
        settingsFrame(redInput);
        io.AddInputCharactersUTF8("17"); settingsFrame();
        assert(plot.opts.custom_theme_colors.count(Themes::bgPaint));
        assert(SkColorGetR(plot.opts.theme.bgPaint.getColor()) == 17);
        assert(plot.opts.settings_save_pending && readJson() == beforeEdit);
        io.AddKeyEvent(ImGuiKey_Enter, true); settingsFrame();
        io.AddKeyEvent(ImGuiKey_Enter, false); settingsFrame();
        assert(!plot.opts.settings_save_pending && readJson() != beforeEdit);
        Themes::IniOptions fromDisk;
        assert(fromDisk.loadCustomTheme(plot.opts.customThemePath()));
        assert(SkColorGetR(fromDisk.custom_theme_colors.at(Themes::bgPaint)) == 17);
        settingsFrame(ImHashStr("Reset to base", 0, scope));
        settingsFrame(); settingsFrame();
        assert(!plot.opts.custom_theme_colors.count(Themes::bgPaint));
        assert(fromDisk.loadCustomTheme(plot.opts.customThemePath()) && fromDisk.custom_theme_colors.empty());
        // General options also persist without a Save button.
        ImGuiID generalTab = 0;
        for (auto& tab : bar->Tabs)
            if (std::strcmp(ImGui::TabBarGetTabName(bar, &tab), "General") == 0) generalTab = tab.ID;
        bar->NextSelectedTabId = generalTab; settingsFrame(); settingsFrame();
        const bool coverage = plot.opts.max_coverage > 0;
        settingsFrame(ImHashStr("coverage", 0, ImHashStr("coverage", 0, generalTab))); settingsFrame();
        assert((plot.opts.max_coverage > 0) != coverage);
        mINI::INIStructure saved;
        assert(mINI::INIFile(plot.opts.ini_path).read(saved));
        assert(saved["general"]["coverage"] == (coverage ? "false" : "true"));
        bar->NextSelectedTabId = customTab; settingsFrame(); settingsFrame();
        const auto nameInput = ImHashStr("Name", 0, editorID);
        settingsFrame(nameInput);
        auto* nameState = ImGui::GetInputTextState(nameInput); assert(nameState);
        nameState->SelectAll();
        io.AddInputCharactersUTF8("Ocean"); settingsFrame();
        io.AddKeyEvent(ImGuiKey_Enter, true); settingsFrame();
        io.AddKeyEvent(ImGuiKey_Enter, false); settingsFrame(); settingsFrame();
        assert(plot.opts.custom_theme_name == "Ocean");
        assert(plot.opts.custom_themes.size() == 1);
        assert(plot.opts.createCustomTheme("Shared", true) && plot.opts.saveSettings());
        // Saved profiles are selectable from the General theme dropdown too.
        bar->NextSelectedTabId = generalTab; settingsFrame(); settingsFrame();
        const auto themeCombo = ImHashStr("theme", 0, ImHashStr("theme", 0, generalTab));
        settingsFrame(themeCombo); settingsFrame();
        auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
        assert(!popups.empty() && popups.back().Window);
        auto* comboPopup = popups.back().Window;
        int oceanIndex = 0;
        const auto oceanID = ImHashStr("Ocean", 0, ImHashData(&oceanIndex, sizeof(oceanIndex), comboPopup->ID));
        settingsFrame(oceanID); settingsFrame(); settingsFrame();
        assert(plot.opts.custom_theme_enabled && plot.opts.custom_theme_name == "Ocean");
        assert(mINI::INIFile(plot.opts.ini_path).read(saved));
        assert(saved["custom_theme"]["name"] == "Ocean");
        bar->NextSelectedTabId = customTab; settingsFrame(); settingsFrame();
        // Opening Custom theme with colors disabled must load the collection
        // before New writes it, while keeping the selected built-in base.
        plot.opts.custom_theme_enabled = false;
        plot.opts.custom_themes.clear(); plot.opts.custom_theme_colors.clear();
        plot.opts.custom_theme_file_checked = false;
        plot.opts.theme_str = "slate"; plot.opts.rebuildTheme();
        settingsFrame(); settingsFrame();
        assert(plot.opts.custom_themes.size() == 2 && plot.opts.theme_str == "slate");
        settingsFrame(nameInput);
        nameState = ImGui::GetInputTextState(nameInput); assert(nameState);
        nameState->SelectAll();
        io.AddInputCharactersUTF8("Fresh"); settingsFrame();
        settingsFrame(ImHashStr("New", 0, editorID)); settingsFrame(); settingsFrame();
        assert(plot.opts.custom_theme_name == "Fresh" && plot.opts.custom_themes.size() == 3);
        assert(fromDisk.loadCustomTheme(plot.opts.customThemePath()) && fromDisk.custom_themes.size() == 3);
        io.DisplaySize = ImVec2(700,700); settingsFrame(); settingsFrame();
        assert(preview->Pos.y >= editor->Pos.y + editor->Size.y);
        assert(preview->Pos.x == editor->Pos.x);
        const auto editorSize = editor->Size;
        for (int f = 0; f < 8; ++f) {
            settingsFrame();
            assert(editor->Size.x == editorSize.x && editor->Size.y == editorSize.y);
        }
        io.DisplaySize = ImVec2(1000,700);
        std::filesystem::remove_all(directory);
        plot.opts = originalOptions;
    }
    Menu::clearThemePreview();
    ImGui::DestroyContext();
    std::filesystem::remove(variantPath);
    std::cout << "ImGui controls, indexed prefetch, paging, tile resolution, theme scrollbars and cache regressions passed\n";
}
