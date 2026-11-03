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
#include "online/online_catalogue.h"
#include "online/online_files.h"
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

// Pair-pattern filters built by the Tools Filter section: kept patterns are or'd,
// removed patterns are and'd.
static void testPatternFilters() {
    std::string headerText = "@SQ\tSN:chr1\tLN:1000\n";
    auto* header = sam_hdr_parse(headerText.size(), headerText.c_str());
    std::string text = "pair\t1\tchr1\t101\t60\t20M\tchr1\t500\t420\t" + std::string(20, 'A') + "\t" + std::string(20, 'I');
    kstring_t line{0, 0, nullptr}; kputs(text.c_str(), &line);
    auto* record = bam_init1(); assert(sam_parse1(&line, header, record) >= 0); free(line.s);
    Segs::Align aln(record);
    Segs::align_init(&aln, 0, true);
    auto keeps = [&](std::string expr, Segs::Pattern pattern) {
        aln.orient_pattern = pattern;
        Parse::Parser parser(std::cerr);
        assert(parser.set_filter(expr, 1, 1) > 0);
        return parser.eval(aln, header, 0, 0);
    };
    const std::string keep = "pattern == del or pattern == dup", drop = "pattern != del and pattern != dup";
    assert(keeps(keep, Segs::DEL) && keeps(keep, Segs::DUP) && !keeps(keep, Segs::TRA) && !keeps(keep, Segs::NORMAL));
    assert(!keeps(drop, Segs::DEL) && !keeps(drop, Segs::DUP) && keeps(drop, Segs::TRA) && keeps(drop, Segs::NORMAL));
    assert(keeps("pattern == inv_f", Segs::INV_F) && !keeps("pattern == inv_f", Segs::INV_R));
    sam_hdr_destroy(header);
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

// A BAM may cover a whole chromosome while the loaded FASTA is only a slice.
static void testPanBeyondShortReference() {
    Themes::IniOptions options;options.threads=1;options.show_translation=true;options.scroll_speed=0.25f;
    std::vector<std::string> paths{"test/test.bam"},tracks;
    std::vector<Utils::Region> regions{Utils::Region("chr1",10980,11060)};
    Manager::GwPlot plot("test/test.fa",paths,options,regions,tracks);
    plot.terminalOutput=false;plot.setImageSize(800,400);plot.makeRasterSurface();
    auto verify=[&]() {
        auto& region=plot.regions[0];plot.fetchRefSeq(region);
        assert(region.chromLen==sam_hdr_tid2len(plot.headers[0],sam_hdr_name2tid(plot.headers[0],"chr1")));
        assert(region.refSeqLen==std::max(0,std::min(region.end,11000)-region.start));
        if(region.start>=11000)assert(!region.refSeq);
        plot.processed=false;plot.drawScreen();
    };
    verify();
    for(int i=0;i<8;++i){plot.keyPress(GLFW_KEY_RIGHT,0,GLFW_PRESS,0);verify();}
    assert(plot.regions[0].start>=11000);
    for(int i=0;i<8;++i){plot.keyPress(GLFW_KEY_LEFT,0,GLFW_PRESS,0);verify();}
    assert(plot.regions[0].start<11000 && plot.regions[0].refSeqLen>0);
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

static void testOnlineCatalogueAndValidation() {
    const auto entries=Online::bundledCatalogue();assert(entries.size()>=7);
    for(const auto& e:entries){auto copy=Online::parseCatalogue(Online::encodeEntry(e));assert(copy.size()==1&&copy[0].url==e.url&&copy[0].indexURL==e.indexURL);}
    auto roundtrip=Online::parseCatalogue(Online::encodeCatalogue(entries));assert(roundtrip.size()==entries.size());
    for(const auto& e:entries)if(!e.bundle.empty()){auto bundle=Online::resolveBundle(e,entries);assert(bundle.size()==4&&bundle.front().category=="reference");}
    auto igv=Online::parseCatalogue(R"([{"id":"test","name":"Escaped \"genome\"","fastaURL":"https:\/\/data.test\/ref.fa","indexURL":"https://data.test/custom.fai","tracks":[{"name":"nested"}]}])");
    assert(igv.size()==1&&igv[0].url=="https://data.test/ref.fa"&&igv[0].indexURL=="https://data.test/custom.fai");
    auto invalid=[](const std::string& json){try{Online::parseCatalogue(json);assert(false);}catch(const std::runtime_error&){};};
    invalid("{broken");invalid(R"({"version":2,"entries":[]})");
    invalid(R"({"version":1,"entries":[{"id":"x","name":"x","category":"reference","url":"file:///etc/passwd"}]})");
    Online::Entry bam; bam.id="fixture";bam.category="alignment";bam.url="test/test.bam";bam.indexURL="test/test.bam.bai";
    auto validated=Online::validate(bam,{{"1",249250621}},true);assert(validated.indexURL==bam.indexURL);
    try{Online::validate(bam,{{"1",248956422}},true);assert(false);}catch(const std::runtime_error&){}
    Online::Entry ref;ref.category="reference";ref.url="test/test.fa";ref.indexURL="test/test.fa.fai";
    auto reference=Online::validate(ref,{},true);assert(reference.indexURL==ref.indexURL);
    ref.indexURL="/tmp/gw-no-such-reference.fai";try{Online::validate(ref,{},false);assert(false);}catch(const std::runtime_error&){}
    Themes::IniOptions options;std::vector<std::string> empty;std::vector<Utils::Region> regions{Utils::Region("chr1",9900,10000)};
    Manager::GwPlot plot("test/test.fa",empty,options,regions,empty);plot.terminalOutput=false;
    auto originalReference=plot.reference;auto* originalIndex=plot.fai;
    try{Online::openGroup(&plot,{reference,ref},true,false);assert(false);}catch(const std::runtime_error&){}
    assert(plot.reference==originalReference&&plot.fai==originalIndex&&plot.bams.empty());
    plot.setImageSize(800,400);plot.makeRasterSurface();
    Online::Entry variants;variants.id="fixture-variants";variants.category="variant";variants.url="test/test.vcf";
    Online::openGroup(&plot,{variants},true,false,false,true);
    assert(plot.tracks.size()==1&&plot.variantTracks.size()==1&&plot.labelTableDialogOpen);
    std::cout<<"Online catalogue, explicit indexes, chr aliases and assembly validation passed\n";
}

static void testOnlineReferenceSwitch() {
    auto directory=std::filesystem::temp_directory_path()/
        ("gw-online-reference-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    auto reference=[&](const std::string& name,const std::string& chrom){
        Online::Entry entry;entry.category="reference";entry.url=(directory/name).string();entry.indexURL=entry.url+".fai";
        std::ofstream(entry.url)<<">"<<chrom<<"\n"<<std::string(1000,'A')<<"\n";
        assert(fai_build(entry.url.c_str())==0);return entry;
    };
    auto chr=reference("chr.fa","chr1"),bare=reference("bare.fa","1"),other=reference("other.fa","different");
    {
        Themes::IniOptions options;options.threads=1;
        std::vector<std::string> paths{"test/test.bam"},tracks;
        std::vector<Utils::Region> regions{Utils::Region("chr1",9900,10000)};
        Manager::GwPlot plot("test/test.fa",paths,options,regions,tracks);plot.terminalOutput=false;
        plot.tracks.emplace_back();plot.tracks.back().kind=HGW::FType::INTRON;plot.tracks.back().bamIndex=0;
        // Same spelling, different contig length: reference opens and the BAM stays.
        Online::openGroup(&plot,{chr},true,true);
        assert(plot.reference==chr.url&&plot.bams.size()==1);
        assert(plot.regions[0].chrom=="chr1"&&plot.regions[0].end<=1000);
        // The new reference uses "1", but this local BAM only has "chr1".
        Online::openGroup(&plot,{bare},true,true);
        assert(plot.reference==bare.url&&plot.bams.empty()&&plot.headers.empty()&&plot.indexes.empty());
        assert(plot.regions[0].chrom=="1"&&plot.tracks.empty());
        assert(plot.outStr.str().find("Closed local alignment")!=std::string::npos);
        // No alias at all: choose a valid chromosome in the new reference.
        Online::openGroup(&plot,{other},true,true);
        assert(plot.regions[0].chrom=="different"&&plot.regions[0].start>=0&&plot.regions[0].end<=1000);
        // Failed reference loads still retain the existing reference and viewport.
        auto before=plot.regions[0].toString();other.indexURL+=".missing";
        try{Online::openGroup(&plot,{other},true,true);assert(false);}catch(const std::runtime_error&){}
        assert(plot.reference==other.url&&plot.regions[0].toString()==before);
    }
    std::filesystem::remove_all(directory);
}

int main() {
    testOnlineCatalogueAndValidation();
    testOnlineReferenceSwitch();
    testThemeCommandSelection();
    testThemePreviewRendering();
    testThemeBackgroundReplacement();
    testCustomThemePersistence();
    testReferenceWindowOwnership();
    testPanBeyondShortReference();
    testHiddenMismatchPrefix();
    testLeftClipDrawingPastRightEdge();
    testStableRowGaps();
    testHaplotypeRows();
    testCoverageBaseCounts();
    testPatternFilters();
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
    // The real GW menu must route actions through the optional remote transport,
    // while preserving the local view (there are no local genomic data to fetch).
    {
        std::string sent;
        plot.remoteCommand = [&](const std::string& command) { sent = command; };
        plot.inputText = "chr1:20-50";
        plot.commandProcessed();
        assert(sent == "chr1:20-50" && plot.regions[0].start == 10);
        // The browser uses the native command editor, including caret edits,
        // history and Escape. No local data are touched on submission.
        assert(plot.commandKey(GLFW_KEY_SLASH, 0, GLFW_PRESS, 0));
        plot.commandChar('/'); // activation character is suppressed
        for (char c : std::string("helpX")) plot.commandChar(c);
        plot.commandKey(GLFW_KEY_BACKSPACE, 0, GLFW_PRESS, 0);
        assert(plot.inputText == "help");
        plot.commandKey(GLFW_KEY_ENTER, 0, GLFW_PRESS, 0);
        assert(sent == "help" && !plot.commandBoxOpen());
        plot.commandKey(GLFW_KEY_SLASH, 0, GLFW_PRESS, 0); plot.commandChar('/');
        plot.commandKey(GLFW_KEY_UP, 0, GLFW_PRESS, 0);
        assert(plot.inputText == "help");
        plot.commandKey(GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
        assert(!plot.commandBoxOpen());
        auto menuFrame = [&]() {
            ImGui::NewFrame(); bool redraw = false;
            Menu::drawImGuiTopMenu(&plot, plot.gap, plot.fonts.overlayHeight, redraw);
            ImGui::Render();
        };
        auto menuClick = [&](float x, float y) {
            io.AddMousePosEvent(x,y); menuFrame();
            io.AddMouseButtonEvent(0,true); menuFrame();
            io.AddMouseButtonEvent(0,false); menuFrame();
        };
        menuFrame(); menuFrame();
        const float side = plot.fonts.overlayHeight;
        menuClick(plot.gap * .5f + side + plot.gap * .25f + side * .5f,
                  plot.gap * .25f + side * .5f);
        menuFrame();
        auto& popups = ImGui::GetCurrentContext()->OpenPopupStack;
        assert(!popups.empty() && popups.back().Window);
        auto* popup = popups.back().Window;
        // Refresh is the last item of the View popup
        const ImGuiID refreshId = ImHashStr("Refresh", 0, popup->ID);
        ImVec2 refreshAt(-1, -1);
        for (float y = popup->WorkRect.Max.y; y > popup->WorkRect.Min.y && refreshAt.x < 0; y -= 2) {
            io.AddMousePosEvent(popup->WorkRect.Min.x + 8, y); menuFrame();
            if (GImGui->HoveredId == refreshId) refreshAt = ImVec2(popup->WorkRect.Min.x + 8, y);
        }
        assert(refreshAt.x >= 0);
        menuClick(refreshAt.x, refreshAt.y);
        assert(sent == "refresh");
        io.AddKeyEvent(ImGuiKey_Escape,true); menuFrame();
        io.AddKeyEvent(ImGuiKey_Escape,false); menuFrame();
        plot.remoteCommand = {};
    }

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
    // The browser uses the same scale-bar preview state as native GW.
    {
        const bool savedScaleBar=plot.opts.scale_bar;
        plot.opts.scale_bar=true;
        assert(plot.beginScaleBarSelection(100,plot.topMenuSpace+1));
        assert(plot.scaleBarSelectionActive());
        plot.endScaleBarSelection(); assert(!plot.scaleBarSelectionActive());
        assert(!plot.beginScaleBarSelection(100,plot.fb_height));
        plot.opts.scale_bar=savedScaleBar;
    }
    // Cursor guides are immediate ImGui geometry, independent of read rendering.
    {
        const auto savedMode=plot.mode; const bool savedLine=plot.drawLine;
        const float savedScale=plot.monitorScale, savedRef=plot.refSpace, savedSlider=plot.sliderSpace;
        plot.mode=Manager::SINGLE; plot.drawLine=true; plot.monitorScale=1;
        plot.refSpace=100; plot.sliderSpace=30;
        auto guide=[&](float x,float y) {
            io.AddMousePosEvent(x,y); ImGui::NewFrame();
            io.WantCaptureMouse=false;
            Menu::drawImGuiCursorLine(&plot);
            auto* list=ImGui::GetBackgroundDrawList();
            std::pair<int,float> result{list->VtxBuffer.Size,list->VtxBuffer.empty()?0:list->VtxBuffer[0].pos.x};
            ImGui::Render(); return result;
        };
        const auto first=guide(100,200), second=guide(200,200);
        assert(first.first>0 && first.first==second.first);
        assert(std::abs(second.second-first.second-100.f)<.01f);
        plot.monitorScale=2;
        assert(guide(100,100).first>0);
        plot.drawLine=false; assert(guide(100,100).first==0);
        plot.drawLine=true; assert(guide(-100,-100).first==0);
        plot.mode=savedMode; plot.drawLine=savedLine; plot.monitorScale=savedScale;
        plot.refSpace=savedRef; plot.sliderSpace=savedSlider;
    }
    // Track separators preview locally and emit one command on release.
    {
        auto savedMode=plot.mode; const float savedScale=plot.monitorScale, savedRef=plot.refSpace, savedSlider=plot.sliderSpace;
        plot.mode=Manager::SINGLE; plot.monitorScale=1; plot.refSpace=50; plot.sliderSpace=30;
        plot.remoteTracks={{300,100,"first"},{400,100,"second"}};
        std::vector<std::string> sent;
        plot.remoteCommand=[&](const std::string& command){sent.push_back(command);};
        auto frame=[&](){ImGui::NewFrame(); Menu::drawImGuiTrackSeparators(&plot); ImGui::Render();};
        io.AddMousePosEvent(200,300); frame(); frame();
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMousePosEvent(200,270); frame(); frame(); assert(sent.empty());
        io.AddMouseButtonEvent(0,false); frame();
        assert(sent.size()==1 && sent[0].rfind("track-resize 0 ",0)==0);
        sent.clear(); io.AddMousePosEvent(200,400); frame(); frame();
        io.AddMouseButtonEvent(0,true); frame(); io.AddMousePosEvent(200,380); frame();
        io.AddKeyEvent(ImGuiKey_Escape,true); frame();
        io.AddMouseButtonEvent(0,false); frame(); assert(sent.empty());
        io.AddKeyEvent(ImGuiKey_Escape,false); frame();
        plot.remoteCommand={}; plot.remoteTracks.clear(); plot.mode=savedMode;
        plot.monitorScale=savedScale; plot.refSpace=savedRef; plot.sliderSpace=savedSlider;
        io.AddMousePosEvent(-100,-100); frame();
    }
    // Remote scrollbar movement stays local until release, even for small views.
    {
        const auto savedOptions=plot.opts;
        const float savedTrackY=plot.trackY, savedScale=plot.monitorScale;
        const auto savedMode=plot.mode;
        plot.mode=Manager::SINGLE; plot.monitorScale=1; plot.trackY=250;
        plot.opts.alignments=true; plot.opts.tlen_yscale=false;
        plot.remoteScrollbars={{0,100,500,100,0,true}};
        std::vector<std::string> sent;
        plot.remoteCommand=[&](const std::string& command) { sent.push_back(command); };
        auto frame=[&]() { ImGui::NewFrame(); Menu::drawImGuiAlignmentScrollbars(&plot); ImGui::Render(); };
        io.AddMousePosEvent(490,115); frame(); frame();
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMousePosEvent(490,280); frame(); frame();
        assert(sent.empty());
        io.AddMouseButtonEvent(0,false); frame();
        assert(sent.size()==1 && sent[0].rfind("vscroll 0 ",0)==0);
        plot.remoteCommand={}; plot.remoteScrollbars.clear();
        plot.opts=savedOptions; plot.trackY=savedTrackY; plot.monitorScale=savedScale; plot.mode=savedMode;
        io.AddMousePosEvent(-100,-100); frame();
    }
    // The View popup's Sort and Hide submenus route every item through an existing
    // command and stay open after a selection, so several can be toggled.
    {
        const auto savedOptions=plot.opts;
        plot.remoteScrollbars={{0,100,500,100,0,true}};
        std::vector<std::string> sent;
        plot.remoteCommand=[&](const std::string& command){sent.push_back(command);};
        auto menuFrame=[&]() {
            ImGui::NewFrame(); bool redraw=false;
            Menu::drawImGuiTopMenu(&plot, plot.gap, plot.fonts.overlayHeight, redraw);
            ImGui::Render();
            assert(GImGui->DebugDrawIdConflictsId==0);
        };
        auto click=[&](float x,float y) {
            io.AddMousePosEvent(x,y); menuFrame();
            io.AddMouseButtonEvent(0,true); menuFrame();
            io.AddMouseButtonEvent(0,false); menuFrame();
        };
        auto& stack=ImGui::GetCurrentContext()->OpenPopupStack;
        const float side=plot.fonts.overlayHeight;
        const float bx=plot.gap*.5f + side + plot.gap*.25f + side*.5f, by=plot.gap*.25f + side*.5f;
        // Hover the named submenu entry of the View popup, then click down its rows
        auto runSubmenu=[&](const char* name) {
            sent.clear();
            menuFrame(); menuFrame();
            click(bx,by); menuFrame();
            assert(stack.Size==1);
            ImGuiWindow* view=stack.back().Window;
            const ImGuiID id=ImHashStr(name,0,view->ID);
            bool opened=false;
            for (float y=view->WorkRect.Min.y; y<view->WorkRect.Max.y && !opened; y+=2) {
                io.AddMousePosEvent(view->WorkRect.Min.x+8,y); menuFrame(); menuFrame();
                opened = GImGui->HoveredId==id && stack.Size==2;
            }
            assert(opened);
            const ImRect work=stack.back().Window->WorkRect;
            io.AddMousePosEvent(work.Min.x+8,work.Min.y+2); menuFrame(); menuFrame();
            for (float y=work.Min.y+2; y<work.Max.y; y+=ImGui::GetFrameHeight()*.25f) {
                size_t before=sent.size();
                click(work.Min.x+8,y);
                assert(stack.Size==2);  // selections never close the menu
                if (sent.size()>before) y+=ImGui::GetFrameHeight()*.5f;
            }
            click(600,650);  // clicking outside dismisses it
            assert(stack.empty());
        };
        auto has=[&](const std::string& c){ return std::find(sent.begin(),sent.end(),c)!=sent.end(); };
        // Menu rows (Sort, Hide, Link) use the same pitch as the checkboxes above them
        {
            menuFrame(); click(bx,by); menuFrame();
            ImGuiWindow* view=stack.back().Window;
            auto firstY=[&](const char* name) {
                const ImGuiID id=ImHashStr(name,0,view->ID);
                for (float y=view->WorkRect.Min.y; y<view->WorkRect.Max.y; y+=0.5f) {
                    io.AddMousePosEvent(view->WorkRect.Min.x+4,y); menuFrame();
                    if (GImGui->HoveredId==id) return y;
                }
                return -1.f;
            };
            const float a=firstY("Alignments"), c=firstY("Coverage");
            const float sortY=firstY("Sort"), hideY=firstY("Hide"), linkY=firstY("Link");
            assert(a>=0 && c>a && sortY>c && hideY>sortY && linkY>hideY);
            // Sort's hit area starts below the separator, so compare the Hide -> Link pitch
            assert(std::abs((linkY-hideY)-(c-a))<1.0f);
            click(600,650);
        }
        runSubmenu("Sort");
        assert(has("sort strand") && has("sort hap") && !has("sort none"));  // None is current
        plot.regions[0].sortOption=Utils::SortType::STRAND;
        runSubmenu("Sort");
        assert(has("sort none") && has("sort hap") && !has("sort strand"));
        plot.regions[0].sortOption=Utils::SortType::NONE;
        runSubmenu("Link");
        assert(has("link sv") && has("link all") && !has("link none"));  // None is current
        runSubmenu("Hide");
        assert(has("filter ~flag & 1024") && has("filter ~flag & 256") && has("filter ~flag & 2048") &&
               has("filter mapq > 0") && !has("filter ~flag & 512") && !has("refresh"));
        // An active quick filter is unticked with remove-filter, and Clear filters appears
        Parse::Parser dup(std::cerr); std::string expr="~flag & 1024";
        dup.set_filter(expr,1,1); plot.filters.push_back(dup);
        runSubmenu("Hide");
        assert(has("remove-filter ~flag & 1024") && !has("filter ~flag & 1024") && has("refresh"));
        plot.filters.clear();
        plot.remoteCommand={}; plot.remoteScrollbars.clear(); plot.opts=savedOptions;
        io.AddMousePosEvent(-100,-100); menuFrame();
    }
    // The ylim box appears only near the read scrollbar and sends ylim on +.
    {
        const auto savedOptions=plot.opts;
        const float savedTrackY=plot.trackY, savedScale=plot.monitorScale;
        const auto savedMode=plot.mode;
        plot.mode=Manager::SINGLE; plot.monitorScale=1; plot.trackY=250;
        plot.opts.alignments=true; plot.opts.tlen_yscale=false; plot.opts.ylim=50;
        plot.remoteScrollbars={{0,100,500,100,0,true}};
        std::vector<std::string> sent;
        plot.remoteCommand=[&](const std::string& command){sent.push_back(command);};
        auto frame=[&]() { ImGui::NewFrame(); Menu::drawImGuiAlignmentScrollbars(&plot); Menu::drawImGuiYlimBox(&plot); ImGui::Render(); };
        auto boxVisible=[&]() { auto* w=ImGui::FindWindowByName("##ylim_box"); return w && w->Active; };
        io.AddMousePosEvent(250,200); frame(); frame();
        assert(!boxVisible());
        io.AddMousePosEvent(490,200); frame(); frame();
        assert(boxVisible());
        auto* box=ImGui::FindWindowByName("##ylim_box");
        // Move onto the box itself, then press its + step button (the rightmost widget)
        ImVec2 plus(box->Pos.x+box->Size.x-box->Size.y*.5f, box->Pos.y+box->Size.y*.5f);
        io.AddMousePosEvent(plus.x,plus.y); frame(); frame();
        assert(boxVisible());
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMouseButtonEvent(0,false); frame();
        assert(sent.size()==1 && sent[0]=="ylim 55");
        // Leaving the area hides the box after a short delay
        io.AddMousePosEvent(100,400);
        for (int f=0; f<60; ++f) frame();
        assert(!boxVisible());
        plot.remoteCommand={}; plot.remoteScrollbars.clear();
        plot.opts=savedOptions; plot.trackY=savedTrackY; plot.monitorScale=savedScale; plot.mode=savedMode;
        io.AddMousePosEvent(-100,-100); frame();
    }
    // Tools sections and the track manager have no conflicting IDs, and Count
    // tabulates every region (two regions here, with one alignment file).
    {
        std::string hdrText="@SQ\tSN:chr1\tLN:1000\n";
        auto* hdr=sam_hdr_parse(hdrText.size(), hdrText.c_str());
        std::string r2="chr1:200-300";
        plot.regions.push_back(Utils::parseRegion(r2));
        plot.bam_paths={"reads.bam"}; plot.bams={nullptr}; plot.headers={hdr};
        plot.collections.clear();
        plot.collections.resize(2);
        for (int i=0;i<2;++i) {
            plot.collections[i].bamIdx=0; plot.collections[i].regionIdx=i;
            plot.collections[i].region=&plot.regions[i];
        }
        plot.markers.push_back({"chr1", 50, 51});
        plot.tracks.emplace_back();
        plot.tracks.back().kind=HGW::FType::ROI;
        Utils::TrackBlock roi; roi.chrom="chr1"; roi.start=20; roi.end=40; roi.name="roi";
        plot.tracks.back().allBlocks["chr1"].add(20,40,roi);
        plot.tracks.back().allBlocks["chr1"].index();
        plot.tracks.emplace_back(); plot.tracks.back().kind=HGW::FType::BED_NOI; plot.tracks.back().path="a.bed";
        plot.commandDialogOpen=true; plot.trackManagerOpen=true;
        const bool savedVariantsOpen=plot.labelTableDialogOpen;
        plot.labelTableDialogOpen=false;  // Tools takes the full dock height
        auto frame=[&]() {
            ImGui::NewFrame(); bool redraw=false;
            Menu::drawImGuiCommandDialog(&plot, &plot.commandDialogOpen, redraw);
            Menu::drawImGuiTrackManager(&plot, &plot.trackManagerOpen, redraw);
            ImGui::Render();
        };
        frame();
        auto* tools=ImGui::FindWindowByName("Tools"); assert(tools);
        auto* tracks=ImGui::FindWindowByName("Tracks"); assert(tracks);
        tracks->Pos=ImVec2(0,0); tracks->Size=ImVec2(480,690);  // Tools is docked on the right
        for (const char* h : {"Count", "Filter", "Find read", "Introns", "Markers & ROI", "Save", "Sort"})
            tools->StateStorage.SetInt(tools->GetID(h), 1);
        for (int f=0; f<4; ++f) frame();
        // Hover every widget; ImGui flags any two items sharing an ID
        auto sweepFor=[&](ImGuiWindow* w, ImGuiID target) {
            ImVec2 found(-1,-1);
            for (float y=w->Pos.y+2; y<w->Pos.y+w->Size.y; y+=4)
                for (float x=w->Pos.x+4; x<w->Pos.x+w->Size.x; x+=12) {
                    io.AddMousePosEvent(x,y); frame();
                    assert(GImGui->DebugDrawIdConflictsId==0);
                    if (target && GImGui->HoveredId==target && found.x<0) found=ImVec2(x,y);
                }
            return found;
        };
        ImVec2 countBtn=sweepFor(tools, ImHashStr("Count##run", 0, tools->ID));
        sweepFor(tracks, 0);
        assert(countBtn.x>=0);
        io.AddMousePosEvent(countBtn.x,countBtn.y); frame();
        io.AddMouseButtonEvent(0,true); frame();
        io.AddMouseButtonEvent(0,false); frame(); frame();
        sweepFor(tools, 0);  // the results table, with one column per region
        bool sawTable=false;
        for (int i=0; i<GImGui->Tables.GetMapSize(); ++i) {
            auto* t=GImGui->Tables.TryGetMapData(i);
            if (t && t->ID==ImHashStr("##counts", 0, tools->ID) && t->ColumnsCount==3) sawTable=true;
        }
        assert(sawTable);
        // Filter: ticking every Keep row adds the or'd pattern filter, every Remove row the and'd one
        auto findTable=[&](const char* name) -> ImGuiTable* {
            for (int i=0; i<GImGui->Tables.GetMapSize(); ++i) {
                auto* t=GImGui->Tables.TryGetMapData(i);
                if (t && t->OuterWindow && t->OuterWindow->RootWindow==tools && t->ID==ImHashStr(name,0,tools->ID)) return t;
            }
            return nullptr;
        };
        // Only Filter open so its table is fully in view
        for (const char* h : {"Count", "Find read", "Introns", "Markers & ROI", "Save", "Sort"})
            tools->StateStorage.SetInt(tools->GetID(h), 0);
        frame(); frame();
        ImVec2 applyBtn=sweepFor(tools, ImHashStr("Apply Filters", 0, tools->ID));
        assert(applyBtn.x>=0);
        auto* flagsTable=findTable("##samflags"); assert(flagsTable);
        auto toolClick=[&](float x,float y) {
            io.AddMousePosEvent(x,y); frame();
            io.AddMouseButtonEvent(0,true); frame();
            io.AddMouseButtonEvent(0,false); frame();
        };
        auto tickColumn=[&](int column) {
            const float x=flagsTable->Columns[column].WorkMinX+ImGui::GetFrameHeight()*0.5f;  // checkbox square
            const float rowH=ImGui::GetFrameHeight();
            for (float y=flagsTable->OuterRect.Min.y+rowH*1.5f; y<flagsTable->OuterRect.Max.y; y+=rowH*0.5f) {
                ImGuiID before=GImGui->HoveredId;
                io.AddMousePosEvent(x,y); frame();
                if (GImGui->HoveredId && GImGui->HoveredId!=before) { toolClick(x,y); y+=rowH*0.5f; }
            }
        };
        auto hasFilter=[&](const std::string& f) {
            return std::any_of(plot.filters.begin(), plot.filters.end(), [&](const Parse::Parser& p){ return p.filter_str==f; });
        };
        tickColumn(1);
        toolClick(applyBtn.x, applyBtn.y);
        assert(hasFilter("pattern == del or pattern == dup or pattern == inv_f or pattern == inv_r or pattern == tra"));
        tickColumn(2);
        toolClick(applyBtn.x, applyBtn.y);
        assert(hasFilter("pattern != del and pattern != dup and pattern != inv_f and pattern != inv_r and pattern != tra"));
        assert(!hasFilter("pattern == del or pattern == dup or pattern == inv_f or pattern == inv_r or pattern == tra"));
        plot.filters.clear();
        auto* cp=&plot; std::ostringstream out;
        std::string cmd="remove-roi chr1:20-40"; Commands::run_command_map(cp, cmd, out);
        assert(plot.tracks[0].allBlocks["chr1"].data.empty());
        cmd="remove-marker chr1:50"; Commands::run_command_map(cp, cmd, out);
        assert(plot.markers.empty());
        plot.tracks.clear(); plot.collections.clear(); plot.bam_paths.clear(); plot.bams.clear(); plot.headers.clear();
        plot.regions.pop_back();
        plot.commandDialogOpen=false; plot.trackManagerOpen=false;
        plot.labelTableDialogOpen=savedVariantsOpen;
        io.AddMousePosEvent(-100,-100); frame();
        sam_hdr_destroy(hdr);
    }
    // Output constraints must stay consistent on narrow remote viewports.
    {
        plot.remoteCommand = [](const std::string&) {};
        for (auto size : {ImVec2(539,582), ImVec2(300,180), ImVec2(1000,700)}) {
            io.DisplaySize=size;
            plot.showCommandStatus=true;
            plot.lastCommandOutputAnsi="Error: unknown command\n" + std::string(300,'x') + "\n";
            ++plot.lastCommandOutputFrame;
            ImVec2 stable;
            for (int f=0;f<24;++f) {
                ImGui::NewFrame(); Menu::drawImGuiCommandStatus(&plot); ImGui::Render();
                auto* win=ImGui::FindWindowByName("Output"); assert(win);
                assert(win->Size.x <= size.x && win->Size.y <= size.y);
                if (f==5) stable=win->Size;
                if (f>5) assert(win->Size.x==stable.x && win->Size.y==stable.y);
            }
            plot.showCommandStatus=false;
            ImGui::NewFrame(); ImGui::Render();
        }
        plot.remoteCommand={}; io.DisplaySize=ImVec2(1000,700);
    }
    // Hovering a read's flag value lists what its bits mean; other values have no tooltip
    {
        Manager::GwPlot::ReadPopup rp; rp.uid=4242; rp.x=200; rp.y=100;
        rp.ansi="\033[1mqname    \033[00mread1\n\033[1mflag     \033[00m99\n\033[1mmapq     \033[00m60\n";
        plot.readPopups={rp};
        auto pop=[&]{ ImGui::NewFrame(); Menu::drawImGuiInfoPopup(&plot); ImGui::Render(); };
        pop(); pop();
        auto* win=ImGui::FindWindowByName("Read info##readpopup4242"); assert(win);
        auto tooltipAt=[&](float y){
            io.AddMousePosEvent(win->Pos.x+win->Size.x*0.75f, y);
            for (int k=0;k<60;++k) pop();
            auto* t=ImGui::FindWindowByName("##Tooltip_00"); return t && t->Active;
        };
        std::vector<float> hits;
        for (float y=win->Pos.y+ImGui::GetFrameHeight(); y<win->Pos.y+win->Size.y; y+=ImGui::GetFrameHeight()*0.5f)
            if (tooltipAt(y)) hits.push_back(y);
        assert(!hits.empty());  // only the flag row explains itself
        const float rowH=ImGui::GetFrameHeight()*1.5f;
        for (float y : hits) assert(std::abs(y-hits.front())<=rowH);
        plot.readPopups.clear(); io.AddMousePosEvent(-100,-100); pop();
    }
    // Output text fills the window as one wrapped, mouse-selectable box with a single scrollbar
    {
        plot.showCommandStatus=true;
        plot.lastCommandOutputAnsi="Genome: could not open Human (GRCh37/hg19)\n"+std::string(400,'y')+"\n";
        ++plot.lastCommandOutputFrame;
        auto out=[&]{ ImGui::NewFrame(); Menu::drawImGuiCommandStatus(&plot); ImGui::Render(); };
        out(); out();
        auto* win=ImGui::FindWindowByName("Output"); assert(win && win->DC.ChildWindows.Size==1);
        auto* box=win->DC.ChildWindows[0];
        assert(box->Size.x>=win->ContentRegionRect.GetWidth()-1 && !box->ScrollbarX);  // wrapped, full width
        const ImVec2 a(box->Pos.x+8, box->Pos.y+8), b(box->Pos.x+80, box->Pos.y+8);
        io.AddMousePosEvent(a.x,a.y); out();
        io.AddMouseButtonEvent(0,true); out();
        io.AddMousePosEvent(b.x,b.y); out(); out();
        io.AddMouseButtonEvent(0,false); out();
        auto* state=ImGui::GetInputTextState(ImGui::GetActiveID()?ImGui::GetActiveID():GImGui->InputTextState.ID);
        assert(state && state->HasSelection());
        plot.showCommandStatus=false; io.AddMousePosEvent(-100,-100); out();
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
            // Other dialogs' tables stay in the pool; only look inside the Variants window
            if (t && t->ColumnsCount == columns && t->OuterWindow && t->OuterWindow->RootWindow == variantWindow) return t;
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
    // Tools and Variants share the right-hand dock: Tools on top (half height, or
    // just its title bar when collapsed), Variants below; the tile picker sets the grid.
    {
        const auto savedNumber=plot.opts.number;
        plot.labelTableDialogOpen=true; plot.labelTableDocked=true;
        plot.commandDialogOpen=true; plot.toolsDocked=true;
        bool collapseTools=false;
        auto frame=[&]() {
            ImGui::NewFrame(); bool redraw=false;
            if (collapseTools) { ImGui::SetWindowCollapsed("Tools", true); collapseTools=false; }
            Menu::drawImGuiCommandDialog(&plot, &plot.commandDialogOpen, redraw);
            Menu::drawImGuiLabelTableDialog(&plot, redraw);
            ImGui::Render();
        };
        for (int f=0; f<4; ++f) frame();
        auto* tools=ImGui::FindWindowByName("Tools");
        auto* variants=ImGui::FindWindowByName("Variants");
        const float dockW=(plot.fb_width-plot.drawingWidth())/plot.monitorScale;
        assert(dockW>0 && tools->Pos.x==io.DisplaySize.x-dockW && variants->Pos.x==tools->Pos.x);
        assert(tools->Size.x==variants->Size.x);
        assert(tools->Pos.y==0 && tools->Size.y==io.DisplaySize.y*0.5f);
        assert(variants->Pos.y==io.DisplaySize.y*0.5f && variants->Pos.y+variants->Size.y==io.DisplaySize.y);
        collapseTools=true; frame(); frame();
        assert(plot.toolsCollapsed && variants->Pos.y==ImGui::GetFrameHeight());
        ImGui::SetWindowCollapsed("Tools", false);
        bool collapseVariants=true;
        auto collapsedFrame=[&]() {
            ImGui::NewFrame(); bool redraw=false;
            if (collapseVariants) { ImGui::SetWindowCollapsed("Variants", true); collapseVariants=false; }
            Menu::drawImGuiCommandDialog(&plot, &plot.commandDialogOpen, redraw);
            Menu::drawImGuiLabelTableDialog(&plot, redraw);
            ImGui::Render();
        };
        collapsedFrame(); collapsedFrame(); collapsedFrame();
        assert(plot.labelTableCollapsed && !plot.toolsCollapsed);
        assert(variants->Pos.y==io.DisplaySize.y-ImGui::GetFrameHeight());
        assert(tools->Size.y==io.DisplaySize.y-ImGui::GetFrameHeight());
        ImGui::SetWindowCollapsed("Variants", false); frame(); frame();
        assert(!plot.labelTableCollapsed);
        plot.toolsDocked=false; frame(); frame();
        assert(variants->Pos.y==0 && tools->Pos.x+tools->Size.x<=io.DisplaySize.x);
        plot.commandDialogOpen=false; plot.toolsDocked=true; frame(); frame();
        // Tile picker: the four-square button left of Dock opens an 8x8 grid
        const float titleH=ImGui::GetFrameHeight(), controlW=titleH-4;
        const float gx=variants->Pos.x+variants->Size.x-titleH-(controlW+4)*2+controlW/2, gy=variants->Pos.y+2+controlW/2;
        auto click=[&](float x,float y) {
            io.AddMousePosEvent(x,y); frame();
            io.AddMouseButtonEvent(0,true); frame();
            io.AddMouseButtonEvent(0,false); frame();
        };
        click(gx,gy); frame();
        auto& stack=ImGui::GetCurrentContext()->OpenPopupStack;
        assert(stack.Size==1);
        const ImVec2 origin=stack.back().Window->WorkRect.Min;
        const float cell=ImGui::GetFrameHeight()*0.75f+3.0f;
        click(origin.x+cell*2.5f, origin.y+cell*1.5f);
        assert(plot.opts.number.x==3 && plot.opts.number.y==2 && stack.empty());
        plot.opts.number=savedNumber;
        plot.labelTableDialogOpen=false; frame();
    }
    // A variant row's play button opens that variant in alignment view, and
    // clicking it again returns to the tiles.
    {
        const auto savedMode=plot.mode; const auto savedRegions=plot.regions;
        plot.mode=Manager::Show::TILED; plot.labelTableDialogOpen=true; plot.commandDialogOpen=false;
        auto frame=[&]() { ImGui::NewFrame(); bool redraw=false;
            Menu::drawImGuiLabelTableDialog(&plot, redraw); ImGui::Render(); };
        auto click=[&](float x,float y){ io.AddMousePosEvent(x,y); frame();
            io.AddMouseButtonEvent(0,true); frame(); io.AddMouseButtonEvent(0,false); frame(); };
        frame(); frame();
        auto& vt=plot.variantTracks[0];
        assert(!vt.multiRegions.empty());
        ImGuiTable* rows=nullptr;
        auto* variants=ImGui::FindWindowByName("Variants");
        for (int i=0; i<GImGui->Tables.GetMapSize(); ++i) {
            auto* t=GImGui->Tables.TryGetMapData(i);
            if (t && t->ColumnsCount==3 && t->OuterWindow && t->OuterWindow->RootWindow==variants) rows=t;
        }
        assert(rows);
        const float h=ImGui::GetTextLineHeight();
        const ImVec2 play(rows->Columns[1].WorkMinX+h*0.5f, rows->RowPosY1+h*0.5f);
        click(play.x,play.y); frame();
        assert(plot.mode==Manager::Show::SINGLE);
        assert(plot.regions[0].chrom==vt.multiRegions[0][0].chrom && plot.regions[0].start==vt.multiRegions[0][0].start);
        click(play.x,play.y); frame();
        assert(plot.mode==Manager::Show::TILED);
        plot.mode=savedMode; plot.regions=savedRegions; plot.labelTableDialogOpen=false;
        io.AddMousePosEvent(-100,-100); frame();
    }
    // The label close pill's x is centred, and the chromosome name opens the
    // header's chromosome list in index order.
    {
        const auto savedOptions=plot.opts; const auto savedMode=plot.mode;
        const float savedScale=plot.monitorScale, savedTabix=plot.totalTabixY, savedSlider=plot.sliderSpace;
        plot.mode=Manager::SINGLE; plot.monitorScale=1; plot.opts.data_labels=true;
        plot.sliderSpace=plot.gap*4; plot.totalTabixY=80;
        plot.tracks.emplace_back(); plot.tracks.back().name="a.bed"; plot.tracks.back().px_height=80;
        auto frame=[&]() { ImGui::NewFrame(); bool redraw=false;
            Menu::drawImGuiDataLabels(&plot, plot.gap, plot.fonts.overlayHeight, redraw);
            Menu::drawImGuiChromosomeList(&plot, redraw); ImGui::Render(); };
        const float trackTop=plot.fb_height-plot.totalTabixY-plot.sliderSpace;
        io.AddMousePosEvent(plot.gap+6, trackTop+plot.gap+4); frame(); frame();
        auto* close=ImGui::FindWindowByName("##datalabel_close_trk0");
        assert(close && close->Active);
        const SkColor gc=plot.opts.theme.tcDel.getColor();
        ImVec2 lo(1e9f,1e9f), hi(-1e9f,-1e9f);
        for (auto& v : close->DrawList->VtxBuffer) {
            if ((v.col & 0x00FFFFFF) != (IM_COL32(SkColorGetR(gc),SkColorGetG(gc),SkColorGetB(gc),0) & 0x00FFFFFF)) continue;
            lo=ImMin(lo,v.pos); hi=ImMax(hi,v.pos);
        }
        assert(hi.x>lo.x);
        const ImVec2 mid=close->Rect().GetCenter();
        assert(std::abs((lo.x+hi.x)*.5f-mid.x)<0.01f && std::abs((lo.y+hi.y)*.5f-mid.y)<0.01f);
        assert(close->Pos.x==std::round(close->Pos.x) && close->Pos.y==std::round(close->Pos.y));
        // With labels hidden the close pill still appears, in the label's place
        plot.opts.data_labels=false;
        io.AddMousePosEvent(-100,-100); frame();
        io.AddMousePosEvent(plot.gap+6, trackTop+plot.gap+4); frame(); frame();
        close=ImGui::FindWindowByName("##datalabel_close_trk0");
        assert(close && close->Active && std::abs(close->Pos.x-std::round(plot.gap+1))<1.0f);
        assert(!ImGui::FindWindowByName("##datalabel_trk0") || !ImGui::FindWindowByName("##datalabel_trk0")->Active);
        // The whole pill stays hoverable, even for an unnamed track whose label is narrower than it
        plot.tracks.back().name="";
        io.AddMousePosEvent(close->Pos.x+close->Size.x*0.9f, close->Pos.y+close->Size.y*0.5f); frame(); frame();
        close=ImGui::FindWindowByName("##datalabel_close_trk0");
        assert(close && close->Active);
        plot.tracks.back().name="a.bed";
        plot.opts.data_labels=true;
        plot.tracks.clear();

        std::string hdrText="@SQ\tSN:chr1\tLN:1000\n@SQ\tSN:chr2\tLN:900\n@SQ\tSN:chr10\tLN:800\n";
        auto* hdr=sam_hdr_parse(hdrText.size(), hdrText.c_str());
        plot.headers={hdr};
        auto& rgn=plot.regions[0];
        const auto savedRegion=rgn;
        rgn.chromNameWidth=40; rgn.regionLen=90; rgn.chromLen=1000;
        std::vector<std::string> sent;
        plot.remoteCommand=[&](const std::string& c){ sent.push_back(c); };
        auto click=[&](float x,float y){ io.AddMousePosEvent(x,y); frame();
            io.AddMouseButtonEvent(0,true); frame(); io.AddMouseButtonEvent(0,false); frame(); };
        const float nameX=plot.gap+10, nameY=(plot.fb_height-plot.gap*2);
        auto& stack=ImGui::GetCurrentContext()->OpenPopupStack;
        click(nameX,nameY); frame();
        assert(stack.Size==1);
        const ImRect work=stack.back().Window->WorkRect;
        for (float y=work.Min.y; y<work.Max.y; y+=2) {
            if (stack.empty()) { click(nameX,nameY); frame(); }
            size_t before=sent.size();
            click(work.Min.x+10,y);
            if (sent.size()>before) y+=ImGui::GetTextLineHeight()*0.5f;
        }
        sent.erase(std::unique(sent.begin(), sent.end()), sent.end());  // a row may be hit twice by the sweep
        assert((sent==std::vector<std::string>{"goto chr1","goto chr2","goto chr10"}));
        // The Genome submenu below the list opens a catalogue reference with its ideogram;
        // a remote viewer lists only the bundled catalogue (alphabetical: hg19 first), so no network is touched
        if (stack.empty()) { click(nameX,nameY); frame(); }
        sent.clear();
        frame(); frame();  // let the auto-resized popup settle before reading its rect
        {
            const ImRect list=stack.back().Window->Rect();
            // The menu item is the first row; hovering opens it after a short delay
            const float menuY=list.Min.y+stack.back().Window->WindowPadding.y+ImGui::GetTextLineHeight()*0.5f;
            auto subOpen=[&]{ auto* m=ImGui::FindWindowByName("Genome###Menu_00"); return m && m->Active; };
            io.AddMousePosEvent(list.Min.x+20, menuY);
            for (int k=0; k<120 && !subOpen(); ++k) frame();
            assert(ImGui::FindWindowByName("Genome###Menu_00") && ImGui::FindWindowByName("Genome###Menu_00")->Active);
            assert(sent.empty());
            auto* sub=ImGui::FindWindowByName("Genome###Menu_00");
            const ImVec2 first(sub->WorkRect.Min.x+20, sub->WorkRect.Min.y+ImGui::GetTextLineHeight()*0.5f);
            io.AddMousePosEvent(first.x, menuY); frame();
            click(first.x, first.y); frame();
            assert((sent==std::vector<std::string>{"load genome https://igv.org/genomes/data/hg19/hg19.fasta","rm ideogram","load ideogram hg19"}));
            assert(stack.empty());
        }
        if (!stack.empty()) click(600,300);
        plot.remoteCommand={}; plot.headers.clear(); sam_hdr_destroy(hdr);
        rgn=savedRegion;
        plot.opts=savedOptions; plot.mode=savedMode; plot.monitorScale=savedScale;
        plot.totalTabixY=savedTabix; plot.sliderSpace=savedSlider;
        io.AddMousePosEvent(-100,-100); frame();
    }
    // Opening a reference picks the ideogram for its assembly, embedded first, then IGV's online list
    {
        assert(Ideo::ideogramForAssembly("hs1")=="t2t" && Ideo::ideogramForAssembly("danRer11")=="danrer11");
        assert(Ideo::ideogramForAssembly("mm10").rfind("https://",0)==0 && Ideo::ideogramForAssembly("sacCer3").empty());
        const auto savedIdeo=plot.ideogram; const auto savedPath=plot.ideogram_path;
        plot.loadIdeogramForAssembly("hg19");
        assert(plot.ideogram.count("chr1") && !plot.ideogram["chr1"].empty());
        plot.loadIdeogramForAssembly("sacCer3");  // unknown: the old genome's ideogram goes
        assert(plot.ideogram.empty() && plot.ideogram_path.empty());
        plot.ideogram=savedIdeo; plot.ideogram_path=savedPath;
    }
    // Genome menu plumbing: an unrecognised reference is matched by its contigs, a no-chr
    // reference gets a no-chr ideogram, and a .gw.ini tag opens through loadGenome
    {
        auto dir=std::filesystem::temp_directory_path()/"gw-genome-menu"; std::filesystem::create_directories(dir);
        const auto copy=(dir/"same.fa").string(), other=(dir/"other.fa").string(), bare=(dir/"bare.fa").string();
        std::filesystem::copy_file("test/test.fa",copy,std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file("test/test.fa.fai",copy+".fai",std::filesystem::copy_options::overwrite_existing);
        { std::ofstream(other)<<">chr1\nACGTACGTAC\n"; std::ofstream(other+".fai")<<"chr1\t10\t6\t10\t11\n"; }
        { std::ofstream(bare)<<">1\nACGTACGTAC\n"; std::ofstream(bare+".fai")<<"1\t10\t3\t10\t11\n"; }
        Themes::IniOptions o; o.threads=1;
        std::vector<std::string> none; std::vector<Utils::Region> rg{Utils::Region("chr1",1,100)};
        Manager::GwPlot g("test/test.fa",none,o,rg,none); g.terminalOutput=false;
        Online::Entry e; e.category="reference"; e.url=copy;
        assert(Online::referenceMatches(&g,e));
        e.url=other; assert(!Online::referenceMatches(&g,e));  // chr1 length differs
        Manager::GwPlot b(bare,none,o,rg,none); b.terminalOutput=false;
        b.loadIdeogramForAssembly("hg19");
        assert(b.ideogram.count("1") && !b.ideogram.count("chr1"));
        g.opts.myIni["genomes"]["mine"]=copy;
        e.url=copy; e.assembly="mine";
        assert(Online::openReference(&g,e,true).empty());
        assert(g.reference==copy && g.opts.genome_tag=="mine");
        std::filesystem::remove_all(dir);
    }
    // UCSC cytoBandIdeo files load gzipped, with the extra mouse stains and empty columns
    {
        auto path=(std::filesystem::temp_directory_path()/"gw-cytoBandIdeo.txt.gz").string();
        const std::string text="chr1\t0\t100\tqA1\tgpos33\nchr1\t100\t200\tqA2\tn/a\nchrUn_x\t0\t50\t\t\nnot a band\n";
        BGZF* w=bgzf_open(path.c_str(),"wg");
        assert(w && bgzf_write(w,text.data(),text.size())==(ssize_t)text.size()); bgzf_close(w);
        std::unordered_map<std::string, std::vector<Ideo::Band>> ideo;
        Themes::readIdeogramFile(path, ideo, plot.opts.theme);
        assert(ideo["chr1"].size()==2 && ideo["chr1"][0].red==215 && ideo["chr1"][1].red==255);
        assert(ideo["chrUn_x"].size()==1 && ideo["chrUn_x"][0].end==50);
        std::filesystem::remove(path);
    }
    // Overlays stay hidden while another window is under the pointer
    {
        const auto savedOptions=plot.opts;
        const float savedTrackY=plot.trackY, savedScale=plot.monitorScale;
        const auto savedMode=plot.mode;
        plot.mode=Manager::SINGLE; plot.monitorScale=1; plot.trackY=250;
        plot.opts.alignments=true; plot.opts.tlen_yscale=false;
        plot.remoteScrollbars={{0,100,500,100,0,true}};
        plot.remoteCommand=[&](const std::string&){};
        bool cover=false;
        auto frame=[&]() {
            ImGui::NewFrame();
            if (cover) {
                ImGui::SetNextWindowPos(ImVec2(300,100)); ImGui::SetNextWindowSize(ImVec2(300,300));
                ImGui::Begin("Read info cover"); ImGui::End();
            }
            Menu::drawImGuiAlignmentScrollbars(&plot); Menu::drawImGuiYlimBox(&plot);
            ImGui::Render();
        };
        auto shown=[&](const char* name) { auto* w=ImGui::FindWindowByName(name); return w && w->Active; };
        io.AddMousePosEvent(490,200); frame(); frame();
        assert(shown("##ylim_box") && shown("##alignment_scroll_0"));
        cover=true; frame(); frame(); frame();
        assert(!shown("##ylim_box") && !shown("##alignment_scroll_0"));
        cover=false;
        plot.remoteCommand={}; plot.remoteScrollbars.clear();
        plot.opts=savedOptions; plot.trackY=savedTrackY; plot.monitorScale=savedScale; plot.mode=savedMode;
        io.AddMousePosEvent(-100,-100); frame();
    }
    Menu::clearThemePreview();
    ImGui::DestroyContext();
    std::filesystem::remove(variantPath);
    std::cout << "ImGui controls, indexed prefetch, paging, tile resolution, theme scrollbars and cache regressions passed\n";
}
