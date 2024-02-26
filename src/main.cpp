//
// Created by Kez Cleal on 25/07/2022.
//
#include <cassert>
#include <algorithm>
#include <filesystem>
#include <htslib/faidx.h>
#include <iostream>
#include <mutex>
#include <string>
#include "argparse.h"
#include "../include/BS_thread_pool.h"
//#include "../include/natsort.hpp"
#include "../include/glob_cpp.hpp"
#include "hts_funcs.h"
#include "parser.h"
#include "plot_manager.h"
#include "themes.h"
#include "utils.h"

#include "GLFW/glfw3.h"

#ifdef __APPLE__
    #include <OpenGL/gl.h>
#elif defined(__linux__)
    #include <GL/gl.h>
    #include <GL/glx.h>
#endif

#define SK_GL
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkDocument.h"
#include "include/docs/SkPDFDocument.h"
#include "include/core/SkStream.h"
#include "include/core/SkPictureRecorder.h"
#include "include/core/SkPicture.h"
#include "include/svg/SkSVGCanvas.h"


// skia context has to be managed from global space to work
GrDirectContext *sContext = nullptr;
SkSurface *sSurface = nullptr;
std::mutex mtx;


void print_banner() {
#if defined(_WIN32) || defined(_WIN64) || defined(__MSYS__)
    std::cout << "\n"
                     "  __________      __ \n"
 " /  _____/  \\    /  \\\n"
 "/   \\  __\\   \\/\\/   /\n"
 "\\    \\_\\  \\        / \n"
 " \\______  /\\__/\\  /  \n"
 "        \\/      \\/  " << std::endl;
#else
    std::cout << "\n"
                 "█▀▀ █ █ █\n"
                 "█▄█ ▀▄▀▄▀" << std::endl;
#endif
}


int main(int argc, char *argv[]) {

//    std::chrono::high_resolution_clock::time_point timer_point = std::chrono::high_resolution_clock::now();

    Themes::IniOptions iopts;
    bool success = iopts.readIni();
    if (!success) {

    }

    static const std::vector<std::string> img_fmt = { "png", "pdf", "svg" };
    static const std::vector<std::string> img_themes = { "igv", "dark", "slate" };
    static const std::vector<std::string> links = { "none", "sv", "all" };

    // note to developer - update version in workflows/main.yml, menu.cpp and deps/gw.desktop, and installers .md in docs
    argparse::ArgumentParser program("gw", "0.9.3");

    program.add_argument("genome")
            .default_value(std::string{""}).append()
            .help("Reference genome in .fasta format with .fai index file");
    program.add_argument("-b", "--bam")
            .default_value(std::string{""}).append()
            .help("Bam/cram alignment file. Repeat for multiple files stacked vertically");
    program.add_argument("-r", "--region")
            .append()
            .help("Region of alignment file to display in window. Repeat to horizontally split window into multiple regions");
    program.add_argument("-v", "--variants")
            .default_value(std::string{""}).append()
            .help("VCF/BCF/BED file to derive regions from. Can not be used with -i");
    program.add_argument("-i", "--images")
            .append()
            .help("Glob path to .png images to display e.g. '*.png'. Can not be used with -v");
    program.add_argument("-o", "--outdir")
            .append()
            .help("Output folder to save images");
    program.add_argument("-f", "--file")
            .append()
            .help("Output single image to file");
    program.add_argument("-n", "--no-show")
            .default_value(false).implicit_value(true)
            .help("Don't display images to screen");
    program.add_argument("-d", "--dims")
            .default_value(iopts.dimensions_str).append()
            .help("Image dimensions (px)");
    program.add_argument("-u", "--number")
            .default_value(iopts.number_str).append()
            .help("Images tiles to display (used with -v and -i)");
    program.add_argument("-t", "--threads")
            .default_value(iopts.threads).append().scan<'i', int>()
            .help("Number of threads to use");
    program.add_argument("--theme")
            .default_value(iopts.theme_str)
            .action([](const std::string& value) {
                if (std::find(img_themes.begin(), img_themes.end(), value) != img_themes.end()) { return value;}
                std::cerr << "Error: --theme not in {igv, dark, slate}" << std::endl;
                abort();
            }).help("Image theme igv|dark|slate");
    program.add_argument("--fmt")
            .default_value("png")
            .action([](const std::string& value) {
                if (std::find(img_fmt.begin(), img_fmt.end(), value) != img_fmt.end()) { return value;}
                return std::string{ "png" };
            }).help("Output file format");
    program.add_argument("--track")
            .default_value(std::string{""}).append()
            .help("Track to display at bottom of window BED/VCF/GFF3/GTF/BEGBID/BIGWIG. Repeat for multiple files stacked vertically");
    program.add_argument("--parse-label")
            .default_value(iopts.parse_label).append()
            .help("Label to parse from vcf file (used with -v) e.g. 'filter' or 'info.SU' or 'qual'");
    program.add_argument("--labels")
            .default_value(iopts.labels).append()
            .help("Choice of labels to use. Provide as comma-separated list e.g. 'PASS,FAIL'");
    program.add_argument("--in-labels")
            .default_value(std::string{""}).append()
            .help("Input labels from tab-separated FILE (use with -v or -i)");
    program.add_argument("--out-vcf")
            .default_value(std::string{""}).append()
            .help("Output labelling results to vcf FILE (the -v option is required)");
    program.add_argument("--out-labels")
            .default_value(std::string{""}).append()
            .help("Output labelling results to tab-separated FILE (use with -v or -i)");
    program.add_argument("--start-index")
            .default_value(0).append().scan<'i', int>()
            .help("Start labelling from -v / -i index (zero-based)");
    program.add_argument("--resume")
            .default_value(false).implicit_value(true)
            .help("Resume labelling from last user-labelled variant");
    program.add_argument("--pad")
            .default_value(iopts.pad).append().scan<'i', int>()
            .help("Padding +/- in bp to add to each region from -v or -r");
    program.add_argument("--ylim")
            .default_value(iopts.ylim).append().scan<'i', int>()
            .help("Maximum y limit (depth) of reads in image");
    program.add_argument("--cov")
            .default_value(iopts.ylim).append().scan<'i', int>()
            .help("Maximum y limit of coverage track");
    program.add_argument("--min-chrom-size")
            .default_value(10000000).append().scan<'i', int>()
            .help("Minimum chromosome size for chromosome-scale images");
    program.add_argument("--no-insertions")
            .default_value(false).implicit_value(true)
            .help("Insertions below --indel-length are not shown");
    program.add_argument("--no-edges")
            .default_value(false).implicit_value(true)
            .help("Edge colours are not shown");
    program.add_argument("--no-mismatches")
            .default_value(false).implicit_value(true)
            .help("Mismatches are not shown");
    program.add_argument("--no-soft-clips")
            .default_value(false).implicit_value(true)
            .help("Soft-clips are not shown");
    program.add_argument("--low-mem")
            .default_value(false).implicit_value(true)
            .help("Reduce memory usage by discarding quality values");
    program.add_argument("--log2-cov")
            .default_value(false).implicit_value(true)
            .help("Scale coverage track to log2");
    program.add_argument("--split-view-size")
            .default_value(iopts.split_view_size).append().scan<'i', int>()
            .help("Max variant size before region is split into two smaller panes (used with -v only)");
    program.add_argument("--indel-length")
            .default_value(iopts.indel_length).append().scan<'i', int>()
            .help("Indels >= this length (bp) will have text labels");
    program.add_argument("--tlen-y").default_value(false).implicit_value(true)
            .help("Y-axis will be set to template-length (tlen) bp. Relevant for paired-end reads only");
    program.add_argument("--max-tlen")
            .default_value(iopts.max_tlen).append().scan<'i', int>()
            .help("Maximum tlen to display on plot");
    program.add_argument("--link")
            .default_value(iopts.link)
            .action([](const std::string& value) {
                if (std::find(links.begin(), links.end(), value) != links.end()) { return value;}
                return std::string{ "None" };
            }).help("Draw linking lines between these alignments");
    program.add_argument("--filter")
            .default_value(std::string{""}).append()
            .help("Filter to apply to all reads");
    program.add_argument("--delay")
            .default_value(0).append().scan<'i', int>()
            .help("Delay in milliseconds before each update, useful for remote connections");
    program.add_argument("--config")
            .default_value(false).implicit_value(true)
            .help("Display path of loaded .gw.ini config");

    // check input for errors and merge input options with IniOptions
    try {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }
    if (program.get<bool>("config")) {
        std::cout << iopts.ini_path << std::endl;
        return 0;
    }

    std::vector<Utils::Region> regions;
    if (program.is_used("-r")) {
        std::vector<std::string> regions_str;
        regions_str = program.get<std::vector<std::string>>("-r");
        for (size_t i=0; i < regions_str.size(); i++){
            regions.push_back(Utils::parseRegion(regions_str[i]));
        }
    }

    std::vector<std::string> bam_paths;

    // check if bam/cram file provided as main argument
    auto genome = program.get<std::string>("genome");
    if (Utils::endsWith(genome, ".bam") || Utils::endsWith(genome, ".cram")) {
        HGW::guessRefGenomeFromBam(genome, iopts, bam_paths, regions);
        if (genome.empty()) {
            std::exit(0);
        }
    }

    bool show_banner = true;

    if (iopts.myIni["genomes"].has(genome)) {
        iopts.genome_tag = genome;
        genome = iopts.myIni["genomes"][genome];
    } else if (genome.empty() && !program.is_used("--images") && !iopts.ini_path.empty() && !program.is_used("--no-show")) {
        // prompt for genome
        print_banner();
        show_banner = false;
        std::cout << "\n Reference genomes listed in " << iopts.ini_path << std::endl << std::endl;
        std::string online = "https://github.com/kcleal/ref_genomes/releases/download/v0.1.0";
#if defined(_WIN32) || defined(_WIN64) || defined(__MSYS__)
        const char *block_char = "*";
#else
        const char *block_char = "▀";
#endif
        std::cout << " " << block_char << " " << online << std::endl << std::endl;
        int i = 0;
        int tag_wd = 11;
        std::vector<std::string> vals;

#if defined(_WIN32) || defined(_WIN64) || defined(__MSYS__)
        std::cout << "  Number | Genome-tag | Path \n";
        std::cout << "  -------|------------|----------------------------------------------" << std::endl;
#else
        std::cout << "  Number │ Genome-tag │ Path \n";
        std::cout << "  ───────┼────────────┼─────────────────────────────────────────────" << std::endl;
#endif
        for (auto &rg: iopts.myIni["genomes"]) {
            std::string tag = rg.first;
            std::string g_path = rg.second;
#if defined(_WIN32) || defined(_WIN64) || defined(__MSYS__)
            std::cout << "    " << i << ((i < 10) ? "    " : "   ")  << "| " << tag;
#else
            std::cout << "    " << i << ((i < 10) ? "    " : "   ")  << "│ " << tag;
#endif

            for (int j=0; j < tag_wd - (int)tag.size(); ++j) {
                std::cout << " ";
            }
#if defined(_WIN32) || defined(_WIN64) || defined(__MSYS__)
            std::cout << "|  ";
#else
            std::cout << "│  ";
#endif

            if (g_path.find(online) != std::string::npos) {
                g_path.erase(g_path.find(online), online.size());
                std::cout << block_char << " " << g_path << std::endl;
            } else {
                std::cout << g_path << std::endl;
            }
            vals.push_back(rg.second);
            i += 1;
        }
        if (i == 0) {
            std::cerr << "No genomes listed, finishing\n";
            std::exit(0);
        }
        std::cout << "\n Enter number: " << std::flush;
        int user_i;
        std::cin >> user_i;
        std::cerr << std::endl;
        assert (user_i >= 0 && (int)user_i < vals.size());
        try {
            genome = vals[user_i];
        } catch (...) {
            std::cerr << "Something went wrong\n";
            std::exit(-1);
        }
        assert (Utils::is_file_exist(genome));
        iopts.genome_tag = genome;

    } else if (!genome.empty() && !Utils::is_file_exist(genome)) {
        std::cerr << "Loading remote genome" << std::endl;
    }

    std::vector<std::string> tracks;
    if (program.is_used("--track")) {
        tracks = program.get<std::vector<std::string>>("--track");
        for (auto &trk: tracks){
            if (!Utils::is_file_exist(trk)) {
                std::cerr << "Error: track file does not exists - " << trk << std::endl;
                std::exit(-1);
            }
        }
    }

    if (program.is_used("-b")) {
        auto bam_paths_temp = program.get<std::vector<std::string>>("-b");
        for (auto &item : bam_paths_temp) {
            if (std::filesystem::exists(item)) {
                bam_paths.push_back(item);
                std::cerr << item << std::endl;
            } else {
                std::vector<std::filesystem::path> glob_paths = glob_cpp::glob(item);
//#if defined(_WIN32) || defined(_WIN64)
//                std::sort(glob_paths.begin(), glob_paths.end());
//#else
//                std::sort(glob_paths.begin(), glob_paths.end(), compareNat);
//#endif
                for (auto &glob_item : glob_paths) {
                    bam_paths.push_back(glob_item.string());
                }
            }
        }
        if (!program.is_used("genome") && genome.empty() && !bam_paths.empty()) {
            HGW::guessRefGenomeFromBam(genome, iopts, bam_paths, regions);
            if (genome.empty()) {
                std::exit(0);
            }
            if (iopts.myIni["genomes"].has(genome)) {
                iopts.genome_tag = genome;
                genome = iopts.myIni["genomes"][genome];
            } else {
                std::cerr << "Error: could not find a reference genome\n";
            }
        }
    }

    std::string outdir;
    if (program.is_used("-o")) {
        outdir = program.get<std::string>("-o");
		iopts.outdir = outdir;
        if (!std::filesystem::is_directory(outdir) || !std::filesystem::exists(outdir)) { // Check if src folder exists
            std::filesystem::create_directory(outdir); // create src folder
        }
    }

    if (program.is_used("-n")) {
        iopts.no_show = program.get<bool>("-n");
    }

    if (program.is_used("--theme")) {
        iopts.theme_str = program.get<std::string>("--theme");
        if (iopts.theme_str == "dark") {
            iopts.theme = Themes::DarkTheme();
        } else if (iopts.theme_str == "igv") {
            iopts.theme = Themes::IgvTheme();
        } else if (iopts.theme_str == "slate") {
            iopts.theme = Themes::SlateTheme();
        } else {
            std::cerr << "Error: unknown theme " << iopts.theme_str << std::endl;
            std::exit(-1);
        }
    }

    if (program.is_used("--dims")) {
        auto d = program.get<std::string>("--dims");
        iopts.dimensions = Utils::parseDimensions(d);
    }

    if (program.is_used("-u")) {
        auto d = program.get<std::string>("-u");
        iopts.number = Utils::parseDimensions(d);
    }

    if (program.is_used("--indel-length")) {
        iopts.indel_length = program.get<int>("--indel-length");
    }

    if (program.is_used("--link")) {
        auto lnk = program.get<std::string>("--link");
        if (lnk == "none") {
            iopts.link_op = 0;
        } else if (lnk == "sv") {
            iopts.link_op = 1;
        } else if (lnk == "all") {
            iopts.link_op = 2;
        } else {
            std::cerr << "Link type not known [none/sv/all]\n";
            std::exit(-1);std::cerr << " 52 \n";
        }
    }

    if (program.is_used("--threads")) {
        iopts.threads = program.get<int>("--threads");
    }
    if (program.is_used("--parse-label")) {
        iopts.parse_label = program.get<std::string>("--parse-label");
    }
    if (program.is_used("--labels")) {
        iopts.labels = program.get<std::string>("--labels");
    }
    if (program.is_used("--ylim")) {
        iopts.ylim = program.get<int>("--ylim");
    }
    if (program.is_used("--pad")) {
        iopts.pad = program.get<int>("--pad");
    }
    if (program.is_used("--log2-cov")) {
        iopts.log2_cov = true;
    }
    if (program.is_used("--tlen-y")) {
        iopts.tlen_yscale = true;
    }
    if (program.is_used("--split-view-size")) {
        iopts.split_view_size = program.get<int>("--split-view-size");
    }
    if (program.is_used("--cov")) {
        iopts.max_coverage = program.get<int>("--cov");
    }
    if (program.is_used("--no-insertions")) {
        iopts.small_indel_threshold = 0;
    }
    if (program.is_used("--no-edges")) {
        iopts.edge_highlights = 0;
    }
    if (program.is_used("--no-mismatches")) {
        iopts.snp_threshold = 0;
    }
    if (program.is_used("--no-soft-clips")) {
        iopts.soft_clip_threshold = 0;
    }
//    if (program.is_used("--low-mem")) {
//        iopts.low_mem = true;
//    }
    if (program.is_used("--start-index")) {
        iopts.start_index = program.get<int>("--start-index");
    }

    if (program.is_used("--images") && program.is_used("--variants")) {
        std::cerr << "Error: only --images or --variants possible, not both" << std::endl;
        std::exit(-1);
    } else if (program.is_used("--images") && program.is_used("--no-show")) {
        std::cerr << "Error: only --images or --no-show possible, not both" << std::endl;
        std::exit(-1);
    }

    std::vector<std::string> filters;
    if (program.is_used("--filter")) {
        filters = Utils::split(program.get("--filter"), ';');
    }

    if (!iopts.no_show) {  // plot something to screen

        /*
         * / Gw start
         */
        Manager::GwPlot plotter = Manager::GwPlot(genome, bam_paths, iopts, regions, tracks);

        if (show_banner) {
            print_banner();
        }

        for (auto &s: filters) {
            plotter.addFilter(s);
        }


        // initialize display screen
        plotter.init(iopts.dimensions.x, iopts.dimensions.y);
        int fb_height, fb_width;
        glfwGetFramebufferSize(plotter.window, &fb_width, &fb_height);
        sk_sp<const GrGLInterface> interface = GrGLMakeNativeInterface();
        if (!interface || !interface->validate()) {
		    std::cerr << "Error: skia GrGLInterface was not valid" << std::endl;
            if (!interface) {
                std::cerr << "    GrGLMakeNativeInterface() returned nullptr" << std::endl;
                std::cerr << "    GrGLInterface probably missing some GL functions" << std::endl;
            } else {
                std::cerr << "    fStandard was " << interface->fStandard << std::endl;
            }
            std::cerr << "GL error code: " << glGetError() << std::endl;
            std::exit(-1);
        }

        sContext = GrDirectContext::MakeGL(interface).release();
        if (!sContext) {
            std::cerr << "Error: could not create skia context using MakeGL\n";
            std::exit(-1);
        }

        GrGLFramebufferInfo framebufferInfo;
        framebufferInfo.fFBOID = 0;

        constexpr int fbFormats[2] = {GL_RGBA8, GL_RGB8};  // GL_SRGB8_ALPHA8
        constexpr SkColorType colorTypes[2] = {kRGBA_8888_SkColorType, kRGB_888x_SkColorType};
        int valid = false;
        for (int i=0; i < 2; ++i) {
            framebufferInfo.fFormat = fbFormats[i];  // GL_SRGB8_ALPHA8; //
            GrBackendRenderTarget backendRenderTarget(fb_width, fb_height, 0, 0, framebufferInfo);
            if (!backendRenderTarget.isValid()) {
                std::cerr << "ERROR: backendRenderTarget was invalid" << std::endl;
                glfwTerminate();
                std::exit(-1);
            }
            sSurface = SkSurface::MakeFromBackendRenderTarget(sContext,
                                                              backendRenderTarget,
                                                              kBottomLeft_GrSurfaceOrigin,
                                                              colorTypes[i], //kRGBA_8888_SkColorType,
                                                              nullptr,
                                                              nullptr).release();
            if (!sSurface) {
                std::stringstream sstream;
                sstream << std::hex << fbFormats[i];
                std::string result = sstream.str();
                std::cerr << "ERROR: sSurface could not be initialized (nullptr). The frame buffer format was 0x" << result << std::endl;
                continue;
            }
            valid = true;
            break;
        }
        if (!valid) {
            std::cerr << "ERROR: could not create a valid frame buffer\n";
            std::exit(-1);
        }

        // start UI here
        if (!program.is_used("--variants") && !program.is_used("--images")) {
            int res = plotter.startUI(sContext, sSurface, program.get<int>("--delay"));  // plot regions
            if (res < 0) {
                std::cerr << "ERROR: Plot to screen returned " << res << std::endl;
                std::exit(-1);
            }
        } else if (program.is_used("--variants")) {  // plot variants as tiled images

            std::vector<std::string> labels = Utils::split_keep_empty_str(iopts.labels, ',');
            plotter.setLabelChoices(labels);
            std::string img;
            if (program.is_used("--in-labels")) {
                Utils::openLabels(program.get<std::string>("--in-labels"), img, plotter.inputLabels, labels, plotter.seenLabels);
            }
            if (program.is_used("--out-labels")) {
                plotter.setOutLabelFile(program.get<std::string>("--out-labels"));
            }
            auto variant_paths = program.get<std::vector<std::string>>("--variants");
            for (auto &v : variant_paths) {
                bool cacheStdin = (v == "-");
                plotter.addVariantTrack(v, iopts.start_index, cacheStdin, false);
            }
            plotter.mode = Manager::Show::TILED;

            int res = plotter.startUI(sContext, sSurface, program.get<int>("--delay"));
            if (res < 0) {
                std::cerr << "ERROR: Plot to screen returned " << res << std::endl;
                std::exit(-1);
            }
//            if (program.is_used("--out-vcf")) {
//                HGW::saveVcf(plotter.vcf, program.get<std::string>("--out-vcf"), plotter.multiLabels);
//            }
        } else if (program.is_used("--images")) {

            auto img = program.get<std::string>("-i");
            if (img == ".") {
                img += "/";
            }
            std::vector<std::string> labels = Utils::split_keep_empty_str(iopts.labels, ',');
            plotter.setLabelChoices(labels);

            if (program.is_used("--in-labels")) {
                Utils::openLabels(program.get<std::string>("--in-labels"), img, plotter.inputLabels, labels, plotter.seenLabels);
            }
            if (program.is_used("--out-labels")) {
                plotter.setOutLabelFile(program.get<std::string>("--out-labels"));
            }

            plotter.addVariantTrack(img, iopts.start_index, false, true);
            if (plotter.variantTracks.back().image_glob.size() == 1) {
                plotter.opts.number.x = 1; plotter.opts.number.y = 1;
            }

            plotter.mode = Manager::Show::TILED;

            int res = plotter.startUI(sContext, sSurface, program.get<int>("--delay"));
            if (res < 0) {
                std::cerr << "ERROR: Plot to screen returned " << res << std::endl;
                std::exit(-1);
            }
        }

    // save plot to file. If no regions are
    // provided, but --outdir is present then the whole genome is plotted using raster backend
    } else {

        if (!program.is_used("--variants") && !program.is_used("--images")) {

            if (program.is_used("--file") && program.is_used("--outdir")) {
                std::cerr << "Error: provide --file or --outdir, not both\n";
                std::exit(-1);
            }
            Manager::GwPlot plotter = Manager::GwPlot(genome, bam_paths, iopts, regions, tracks);

            for (auto &s: filters) {
                plotter.addFilter(s);
            }

            plotter.opts.theme.setAlphas();

            if (program.is_used("--fmt") && (program.get<std::string>("--fmt") == "pdf" || program.get<std::string>("--fmt") == "svg" )) {
                std::string format_str = program.get<std::string>("--fmt");
                if (regions.empty()) {
                    std::cerr << "Error: --fmt is only supported by providing a --region\n";
                    std::exit(-1);
                }

                std::filesystem::path fname;
                std::filesystem::path out_path;

                if (program.is_used("--file")) {
                    fname = program.get<std::string>("--file");
                    out_path = fname;
                } else {
                    if (outdir.empty()) {
                        std::cerr << "Error: please provide an output directory using --outdir, or direct to --file\n";
                        std::exit(-1);
                    }
                    fname = regions[0].chrom + "_" + std::to_string(regions[0].start) + "_" +
                            std::to_string(regions[0].end) + "." + format_str;
                    out_path = outdir / fname;
                }

#if defined(_WIN32) || defined(_WIN64)
                const wchar_t* outp = out_path.c_str();
        std::wstring pw(outp);
        std::string outp_str(pw.begin(), pw.end());
        SkFILEWStream out(outp_str.c_str());
#else
                SkFILEWStream out(out_path.c_str());
#endif
                SkDynamicMemoryWStream buffer;

                if (format_str == "pdf") {
                    auto pdfDocument = SkPDF::MakeDocument(&buffer);
                    SkCanvas *pageCanvas = pdfDocument->beginPage(iopts.dimensions.x, iopts.dimensions.y);
                    plotter.fb_width = iopts.dimensions.x;
                    plotter.fb_height = iopts.dimensions.y;
                    plotter.runDrawOnCanvas(pageCanvas);
                    pdfDocument->close();
                    buffer.writeToStream(&out);
                } else {
                    plotter.fb_width = iopts.dimensions.x;
                    plotter.fb_height = iopts.dimensions.y;
                    SkPictureRecorder recorder;
                    SkCanvas* canvas = recorder.beginRecording(SkRect::MakeWH(iopts.dimensions.x, iopts.dimensions.y));
                    plotter.runDrawOnCanvas(canvas);
                    sk_sp<SkPicture> picture = recorder.finishRecordingAsPicture();
                    std::unique_ptr<SkCanvas> svgCanvas = SkSVGCanvas::Make(SkRect::MakeWH(iopts.dimensions.x, iopts.dimensions.y), &out);
                    if (svgCanvas) {
                        picture->playback(svgCanvas.get());
                        svgCanvas->flush();
                    };
                }

            } else {

                // Plot a png image, either of target region or whole chromosome
                sk_sp<SkImage> img;
                if (!regions.empty()) {  // plot target regions
                    plotter.setRasterSize(iopts.dimensions.x, iopts.dimensions.y);
                    plotter.gap = 0;
                    plotter.makeRasterSurface();
//                    sk_sp<SkSurface> rasterSurface = SkSurface::MakeRasterN32Premul(iopts.dimensions.x,
//                                                                                    iopts.dimensions.y);
//                    SkCanvas *canvas = rasterSurface->getCanvas();
                    if (iopts.link_op == 0) {
                        plotter.runDrawNoBuffer();
                    } else {
                        plotter.runDraw();
                    }
                    img = plotter.rasterSurface->makeImageSnapshot();

                    if (outdir.empty()) {
                        std::string fpath;
                        if (program.is_used("--file")) {
                            fpath = program.get<std::string>("--file");
                            Manager::imagePngToFile(img, fpath);
                        } else {
                            Manager::imagePngToStdOut(img);
                        }
                    } else {
                        std::filesystem::path fname = Utils::makeFilenameFromRegions(regions);
                        std::filesystem::path out_path = outdir / fname;
                        Manager::imageToPng(img, out_path);
                    }
                    return 0;

                } else {  // chromosome plot

                    if (program.is_used("--file")) {
                        std::cerr << "Error: --file option not supported without --region";
                        return -1;
                    }
                    if (iopts.link_op != 0) {
                        std::cerr << "Error: Only --link none is supported for chromosome plots";
                        return -1;
                    }
                    std::cerr << "Plotting chromosomes\n";

                    std::vector<Manager::GwPlot *> managers;
                    managers.reserve(iopts.threads);
                    for (int i = 0; i < iopts.threads; ++i) {
                        auto *m = new Manager::GwPlot(genome, bam_paths, iopts, regions, tracks);
                        m->opts.theme.setAlphas();
                        m->setRasterSize(iopts.dimensions.x, iopts.dimensions.y);
                        m->opts.threads = 1;
                        m->gap = 0;
                        m->regions.resize(1);

                        for (auto &s: filters) {
                            m->addFilter(s);
                        }
                        managers.push_back(m);
                    }

                    std::vector<std::vector<Utils::Region> > jobs(iopts.threads);
                    int part = 0;
                    int min_chrom_size = program.get<int>("--min-chrom-size");
                    for (int i = 0; i < faidx_nseq(managers[0]->fai); ++i) {
                        const char *chrom = faidx_iseq(managers[0]->fai, i);
                        int seq_len = faidx_seq_len(managers[0]->fai, chrom);
                        if (seq_len < min_chrom_size) {
                            continue;
                        }
                        Utils::Region N;
                        N.chrom = chrom;
                        N.start = 1;
                        N.end = seq_len;
                        jobs[part].push_back(N);
                        part = (part == (int) jobs.size() - 1) ? 0 : part + 1;
                    }

                    int ts = std::min(iopts.threads, (int) jobs.size());
                    BS::thread_pool pool(ts);
                    int block = 0;
                    pool.parallelize_loop(0, jobs.size(),
                                          [&](const int a, const int b) {
                                              mtx.lock();
                                              int this_block = block;
                                              block += 1;
                                              mtx.unlock();
                                              Manager::GwPlot *plt = managers[this_block];
                                              plt->makeRasterSurface();
                                              std::vector<Utils::Region> &all_regions = jobs[this_block];
//                                              sk_sp<SkSurface> rasterSurface = SkSurface::MakeRasterN32Premul(
//                                                      iopts.dimensions.x, iopts.dimensions.y);
//                                              SkCanvas *canvas = rasterSurface->getCanvas();
                                              for (auto &rgn: all_regions) {
                                                  plt->collections.clear();
                                                  delete plt->regions[0].refSeq;
                                                  plt->regions[0].chrom = rgn.chrom;
                                                  plt->regions[0].start = rgn.start;
                                                  plt->regions[0].end = rgn.end;
                                                  if (iopts.link_op == 0) {
                                                      plt->runDrawNoBuffer();
                                                  } else {
                                                      plt->runDraw();
                                                  }
                                                  sk_sp<SkImage> img(plt->rasterSurface->makeImageSnapshot());
                                                  std::filesystem::path fname = "GW~" + plt->regions[0].chrom + "~" +
                                                                   std::to_string(plt->regions[0].start) + "~" +
                                                                   std::to_string(plt->regions[0].end) + "~.png";
                                                  std::filesystem::path out_path = outdir / fname;
                                                  Manager::imageToPng(img, out_path);
                                              }
                                          })
                            .wait();

                    for (auto &itm: managers) {
                        delete (itm);
                    }
                }
            }

        } else if (program.is_used("--variants") && !program.is_used("--out-vcf")) {
            if (outdir.empty()) {
                std::cerr << "Error: please provide an output directory using --outdir\n";
                std::exit(-1);
            }
            auto variant_paths = program.get<std::vector<std::string>>("--variants");
            if (variant_paths.size() >1) {
                std::cerr << "Error: only a single --variant file can be used with these options\n";
                std::exit(-1);
            }

            std::string &v = variant_paths[0];

            if (Utils::endsWith(v, "vcf") || Utils::endsWith(v, "vcf.gz") || Utils::endsWith(v, "bcf")) {

                if (program.is_used("--fmt") && program.get<std::string>("--fmt") == "pdf") {
                    std::cerr << "Error: only --fmt png is supported with -v\n";
                    return -1;
                }
                iopts.theme.setAlphas();
                auto vcf = HGW::VCFfile();
                vcf.cacheStdin = false;
                vcf.label_to_parse = iopts.parse_label.c_str();

                std::filesystem::path dir(outdir);

                bool writeLabel;
                std::ofstream fLabels;
                if (!iopts.parse_label.empty()) {
                    writeLabel = true;
                    std::filesystem::path file ("gw.parsed_labels.tsv");
                    std::filesystem::path full_path = dir / file;
                    std::string outname = full_path.string();
                    fLabels.open(full_path);
                    fLabels << "#chrom\tpos\tvariant_ID\tlabel\tvar_type\tlabelled_date\tvariant_filename\n";
                } else {
                    writeLabel = false;
                }

                vcf.open(v);
                std::vector<Manager::GwPlot *> managers;
                managers.reserve(iopts.threads);
                for (int i = 0; i < iopts.threads; ++i) {
                    auto *m = new Manager::GwPlot(genome, bam_paths, iopts, regions, tracks);
                    m->opts.theme.setAlphas();
                    m->setRasterSize(iopts.dimensions.x, iopts.dimensions.y);
                    m->opts.threads = 1;
                    for (auto &s: filters) {
                        m->addFilter(s);
                    }
                    managers.push_back(m);
                }

                std::vector<Manager::VariantJob> jobs;
                std::vector<std::string> empty_labels;
                std::string dateStr;

                std::string fileName;
                std::filesystem::path fsp(vcf.path);
#if defined(_WIN32) || defined(_WIN64)
                const wchar_t* pc = fsp.filename().c_str();
                std::wstring ws(pc);
                std::string p(ws.begin(), ws.end());
                fileName = p;
#else
                fileName = fsp.filename();
#endif
                while (true) {
                    vcf.next();
                    if (vcf.done) {
                        break;
                    }
                    Manager::VariantJob job;
                    job.chrom = vcf.chrom;
                    job.chrom2 = vcf.chrom2;
                    job.start = vcf.start;
                    job.stop = vcf.stop;
                    job.varType = vcf.vartype;
                    job.rid = vcf.rid;
                    jobs.push_back(job);
                    if (writeLabel) {
                        Utils::Label l = Utils::makeLabel(vcf.chrom, vcf.start, vcf.label, empty_labels, vcf.rid, vcf.vartype, "", false, false);
                        Utils::labelToFile(fLabels, l, dateStr, fileName);
                    }
                }
                // shuffling might help distribute high cov regions between jobs
                // std::shuffle(std::begin(jobs), std::end(jobs), std::random_device());
                int ts = std::min(iopts.threads, (int)jobs.size());
                BS::thread_pool pool(ts);
                int block = 0;
                pool.parallelize_loop(0, jobs.size(),
                                      [&](const int a, const int b) {
                                            mtx.lock();
                                            int this_block = block;
                                            block += 1;
                                            mtx.unlock();
                                            Manager::GwPlot *plt = managers[this_block];
                                            plt->makeRasterSurface();
//                                            sk_sp<SkSurface> rasterSurface = SkSurface::MakeRasterN32Premul(iopts.dimensions.x, iopts.dimensions.y);
//                                            SkCanvas *canvas = rasterSurface->getCanvas();
                                            for (int i = a; i < b; ++i) {
                                                Manager::VariantJob job = jobs[i];
                                                plt->setVariantSite(job.chrom, job.start, job.chrom2, job.stop);
                                                plt->runDrawNoBuffer();
//                                                if (plt->opts.low_memory && plt->opts.link_op == 0) {
//                                                    plt->runDrawNoBuffer(canvas);
//                                                } else {
//                                                    plt->runDraw(canvas);
//                                                }
                                                sk_sp<SkImage> img(plt->rasterSurface->makeImageSnapshot());
                                                std::filesystem::path fname = job.varType + "~" + job.chrom + "~" + std::to_string(job.start) + "~" + job.chrom2 + "~" + std::to_string(job.stop) + "~" + job.rid + ".png";
                                                std::filesystem::path full_path = outdir / fname;
                                                Manager::imageToPng(img, full_path);
                                          }
                                      })
                        .wait();

                fLabels.close();

                for (auto &itm : managers) {
                    delete(itm);
                }
            }

        } else if (program.is_used("--variants") && program.is_used("--out-vcf") && program.is_used("--in-labels")) {
            auto variant_paths = program.get<std::vector<std::string>>("--variants");
            if (variant_paths.size() != 1) {
                std::cerr << "Error: please supply a single --variants file at a time here\n";
                std::exit(1);
            }
            std::string in_vcf = variant_paths[0];
            std::string in_labels = program.get<std::string>("--in-labels");
            std::string out_vcf = program.get<std::string>("--out-vcf");
            if (program.is_used("--out-vcf")) {
                HGW::saveVcf(in_vcf, out_vcf, in_labels);
            }
        }
    }
    if (!iopts.no_show) {
        std::cout << "\nGw finished\n";
    } else {
        std::cout << "Gw finished\n";
    }
    return 0;
};
