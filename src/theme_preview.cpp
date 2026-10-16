#include "menu.h"
#include "include/core/SkTextBlob.h"
#include <cstdlib>

namespace Menu {

// A small, deterministic scene uses the production alignment/coverage/reference
// drawing paths without fetching files or disturbing the user's read queues.
sk_sp<SkImage> renderThemePreview(const Themes::IniOptions& source, int width, int height) {
    if (width <= 0 || height <= 0) return nullptr;
#if !defined(OLD_SKIA) || OLD_SKIA == 0
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
#else
    auto surface = SkSurface::MakeRasterN32Premul(width, height);
#endif
    if (!surface) return nullptr;
    auto* canvas = surface->getCanvas();
    canvas->scale(width / 900.f, height / 560.f);
    auto opts = source;
    opts.alignments = true; opts.data_labels = false; opts.link_op = 0;
    opts.snp_threshold = opts.soft_clip_threshold = opts.mod_threshold = 1000;
    opts.small_indel_threshold = 1000; opts.indel_length = 1;
    opts.max_coverage = 10000000; opts.parse_mods = true; opts.tlen_yscale = false;
    opts.theme.setAlphas();
    const auto& theme = opts.theme;
    canvas->drawPaint(theme.bgPaint);
    canvas->drawRect(SkRect::MakeXYWH(0,0,900,36), theme.bgMenu);
    Themes::Fonts fonts;
    fonts.setTypeface(opts.font_str, 15); fonts.setOverlayHeight(1);
    auto text = [&](const char* label, float x, float y) {
        auto blob = SkTextBlob::MakeFromString(label, fonts.overlay);
        canvas->drawTextBlob(blob, x, y, theme.tcLabels);
    };
    text("Theme preview", 16, 24);
    text("Reference", 16, 72); text("Coverage", 16, 120);
    std::string reference;
    for (int i = 0; i < 100; ++i) reference += "ACGT"[i % 4];
    Utils::Region region("Preview", 0, 100);
    region.refSeq = reference.c_str(); region.refSeqLen = reference.size();
    std::vector<Utils::Region> regions{region};
    std::vector<Segs::ReadCollection> collections(1);
    auto& cl = collections[0];
    cl.region = &regions[0]; cl.regionLen = 100;
    cl.regionPixels = cl.xPixels = 680; cl.xOffset = 190; cl.xScaling = 6.8f;
    cl.yOffset = 152; cl.yPixels = 380;
    cl.plotPointedPolygons = true; cl.plotSoftClipAsBlock = true; cl.drawEdges = true;
    cl.covArr.assign(101, 0); cl.makeEmptyMMArray();
    struct Sample { const char* label; const char* cigar; int orientation; int mapq; };
    const Sample samples[] = {
        {"Normal read", "72M", Segs::NORMAL, 60},
        {"Mismatches A/C/G/T", "72M", Segs::NORMAL, 60},
        {"Deletion", "30M6D36M", Segs::DEL, 60},
        {"Insertion", "30M5I36M", Segs::NORMAL, 60},
        {"Soft clips", "6S66M5S", Segs::NORMAL, 60},
        {"Duplication", "72M", Segs::DUP, 60},
        {"Inversion forward", "72M", Segs::INV_F, 60},
        {"Inversion reverse", "72M", Segs::INV_R, 60},
        {"Translocation", "72M", Segs::TRA, 60},
        {"Mapping quality zero", "72M", Segs::NORMAL, 0},
        {"Selected / split", "72M", Segs::NORMAL, 60},
        {"Base modifications", "72M", Segs::NORMAL, 60},
    };
    const std::string headerText = "@SQ\tSN:Preview\tLN:1000\n";
    auto* header = sam_hdr_parse(headerText.size(), headerText.c_str());
    for (int row = 0; row < 12; ++row) {
        const auto& sample = samples[row];
        const int start = 7 + row % 3;
        std::string sequence;
        int pos = start;
        const char* cigar = sample.cigar;
        while (*cigar) {
            char* end;
            int count = (int)std::strtol(cigar, &end, 10);
            char op = *end; cigar = end + 1;
            if (op == 'M') for (int i = 0; i < count; ++i) sequence += reference[pos++ % 100];
            else if (op == 'D') pos += count;
            else sequence.append(count, 'T');
        }
        if (row == 1) for (int i = 0; i < 4; ++i) sequence[20 + i * 9] = "CGTA"[i];
        std::string sam = "sample" + std::to_string(row) + "\t" + (row % 2 ? "16" : "0") +
            "\tPreview\t" + std::to_string(start + 1) + "\t" + std::to_string(sample.mapq) +
            "\t" + sample.cigar + "\t*\t0\t0\t" + sequence + "\t" + std::string(sequence.size(), 'I');
        kstring_t line{0,0,nullptr}; kputs(sam.c_str(), &line);
        auto* record = bam_init1();
        int parsed = sam_parse1(&line, header, record); std::free(line.s);
        if (parsed < 0) { bam_destroy1(record); continue; }
        cl.readQueue.emplace_back(record);
        auto& read = cl.readQueue.back(); Segs::align_init(&read, 0, false);
        read.y = row; read.orient_pattern = sample.orientation;
        if (row == 10) read.edge_type = 4;
        if (row == 11) for (int i = 0; i < 3; ++i) {
            Segs::ModItem mod; mod.index = 15 + i * 15; mod.n_mods = 1;
            mod.mods[0] = "mhx"[i]; mod.quals[0] = 3; mod.strands[0] = false;
            read.any_mods.push_back(mod);
        }
        Segs::addToCovArray(cl.covArr, read, 0, 100);
        text(sample.label, 16, 168 + row * 28);
    }
    sam_hdr_destroy(header);
    Drawing::drawContext ctx{};
    ctx.fb_width = 900; ctx.fb_height = 560; ctx.refSpace = 80;
    ctx.covYh = ctx.covY = 60; ctx.monitorScale = 1; ctx.gap = 10;
    ctx.trackY = 350; ctx.yScaling = 28; ctx.pH = 20; ctx.pointSlop = 5;
    ctx.nRegions = ctx.nbams = 1; ctx.overlayHeight = fonts.overlayHeight;
    auto refCtx = ctx; refCtx.fb_width = 700;
    canvas->save(); canvas->translate(180, 0);
    Drawing::drawRef(opts, regions, canvas, fonts, refCtx);
    canvas->restore();
    std::vector<std::string> names{"Preview"};
    Drawing::drawCollection(opts, cl, canvas, fonts, names, ctx);
    Drawing::drawCoverage(opts, collections, canvas, fonts, names, ctx);
    text("Track", 16, 530);
    canvas->drawRect(SkRect::MakeXYWH(240,510,170,22), theme.fcTrack);
    canvas->drawLine(410,521,540,521, theme.lcGTFJoins);
    canvas->drawRect(SkRect::MakeXYWH(540,510,220,22), theme.fcTrack);
    cl.clear();
    return surface->makeImageSnapshot();
}
}
