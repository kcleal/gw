//
// Created by Kez Cleal on 25/07/2022.
//
#include <algorithm>
#include <filesystem>
#include <htslib/faidx.h>
#include <iostream>
#include <mutex>
#include <string>
#include "argparse.h"
#include "../include/BS_thread_pool.h"
#include "../include/strnatcmp.h"
#include "glob.h"
#include "hts_funcs.h"
#include "parser.h"
#include "plot_manager.h"
#include "themes.h"
#include "utils.h"
#ifdef __APPLE__
    #include <OpenGL/gl.h>
#elif defined(__linux__)
    #include <GL/gl.h>
    #include <GL/glx.h>
#endif
#include "GLFW/glfw3.h"
#define SK_GL
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkDocument.h"
#include "include/docs/SkPDFDocument.h"

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

    Themes::IniOptions iopts;
    iopts.readIni();
    static const std::vector<std::string> img_fmt = { "png", "pdf" };
    static const std::vector<std::string> img_themes = { "igv", "dark" };
    static const std::vector<std::string> links = { "none", "sv", "all" };

    argparse::ArgumentParser program("gw", "0.8.3");

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
                std::cerr << "Error: --theme not in {igv, dark}" << std::endl;
                abort();
            }).help("Image theme igv|dark");
    program.add_argument("--fmt")
            .default_value(iopts.fmt)
            .action([](const std::string& value) {
                if (std::find(img_fmt.begin(), img_fmt.end(), value) != img_fmt.end()) { return value;}
                return std::string{ "png" };
            }).help("Output file format");
    program.add_argument("--track")
            .default_value(std::string{""}).append()
            .help("Track to display at bottom of window BED/VCF. Repeat for multiple files stacked vertically");
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
    program.add_argument("--use-raster")
            .default_value(false).implicit_value(true)
            .help("Use raster backend for plotting images (applies to --no-show only)");
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

    auto genome = program.get<std::string>("genome");
    bool show_banner = true;

    if (iopts.references.find(genome) != iopts.references.end()) {
        iopts.genome_tag = genome;
        genome = iopts.references[genome];
    } else if (genome.empty() && !program.is_used("--images") && !iopts.ini_path.empty()) {
        // prompt for genome
        print_banner();
        show_banner = false;

        std::cerr << "\n Reference genomes listed in " << iopts.ini_path << std::endl << std::endl;
        int i = 0;
        std::vector<std::string> vals;
        for (auto &rg: iopts.references) {
            std::cerr << "   " << i << ": " << rg.first << "     " << rg.second << std::endl;
            vals.push_back(rg.second);
            i += 1;
        }
        if (i == 0) {
            std::cerr << "No genomes listed, finishing\n";
            std::exit(0);
        }
        std::cerr << "\n Enter number: " << std::flush;
        int user_i;
        std::cin >> user_i;
        std::cerr << std::endl;
        assert (user_i >= 0 && user_i < vals.size());
        try {
            genome = vals[user_i];
        } catch (...) {
            std::cerr << "Something went wrong\n";
            std::terminate();
        }
        assert (Utils::is_file_exist(genome));
        iopts.genome_tag = genome;

    } else if (!genome.empty() && !Utils::is_file_exist(genome)) {
        std::cerr << "Warning: Genome is not a local file" << std::endl;
    }

    std::vector<std::string> bam_paths;
    if (program.is_used("-b")) {
        if (!program.is_used("genome") && genome.empty()) {
            std::cerr << "Error: please provide a reference genome if loading a bam file\n";
            std::exit(1);
        }
        auto bam_paths_temp = program.get<std::vector<std::string>>("-b");
        for (auto &item : bam_paths_temp) {
            if (std::filesystem::exists(item)) {
                bam_paths.push_back(item);
            } else {
                std::vector<std::filesystem::path> glob_paths = glob::glob(item);
#if defined(_WIN32) || defined(_WIN64)
                std::sort(glob_paths.begin(), glob_paths.end());
#else
                std::sort(glob_paths.begin(), glob_paths.end(), compareNat);
#endif
                for (auto &glob_item : glob_paths) {
                    bam_paths.push_back(glob_item.string());
                }
            }
        }
    }

    std::vector<Utils::Region> regions;
    if (program.is_used("-r")) {
        std::vector<std::string> regions_str;
        regions_str = program.get<std::vector<std::string>>("-r");
        for (size_t i=0; i < regions_str.size(); i++){
            regions.push_back(Utils::parseRegion(regions_str[i]));
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

    if (program.is_used("--theme") && program.get<std::string>("--theme") == "igv") {
        iopts.theme = Themes::IgvTheme();
        iopts.theme_str = "igv";
    } else {  // defaults to igv theme
    }

    if (program.is_used("--dims")) {
        auto d = program.get<std::string>("--dims");
        iopts.dimensions = Utils::parseDimensions(d);
    }

    if (program.is_used("-u")) {
        auto d = program.get<std::string>("-u");
        iopts.number = Utils::parseDimensions(d);
    }

    std::vector<std::string> tracks;
    if (program.is_used("--track")) {
        tracks = program.get<std::vector<std::string>>("--track");
        for (auto &trk: tracks){
            if (!Utils::is_file_exist(trk)) {
                std::cerr << "Error: track file does not exists - " << trk << std::endl;
                std::abort();
            }
            iopts.tracks[genome].push_back(trk);
        }
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
            std::terminate();
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
    if (program.is_used("--low-mem")) {
        iopts.low_mem = true;
    }
    if (program.is_used("--start-index")) {
        iopts.start_index = program.get<int>("--start-index");
    }

    /*
     * / Gw start
     */
    Manager::GwPlot plotter = Manager::GwPlot(genome, bam_paths, iopts, regions, tracks);

    if (program.is_used("--filter")) {
        for (auto &s: Utils::split(program.get("--filter"), ';')) {
            Parse::Parser p = Parse::Parser();
            int rr = p.set_filter(s, plotter.bams.size(), plotter.regions.size());
            if (rr > 0) {
                plotter.filters.push_back(p);
            } else {
                std::cerr << "Error: --filter option not understood" << std::endl;
                std::exit(-1);
            }
        }
    }

    if (program.is_used("--images") && program.is_used("--variants")) {
        std::cerr << "Error: only --images or --variants possible, not both" << std::endl;
        std::exit(-1);
    } else if (program.is_used("--images") && program.is_used("--no-show")) {
        std::cerr << "Error: only --images or --no-show possible, not both" << std::endl;
        std::exit(-1);
    }

    if (!iopts.no_show) {  // plot something to screen

        if (show_banner) {
            print_banner();
        }
        // initialize display screen
        plotter.init(iopts.dimensions.x, iopts.dimensions.y);

        int fb_height, fb_width;
        glfwGetFramebufferSize(plotter.window, &fb_width, &fb_height);

        sk_sp<const GrGLInterface> interface = GrGLMakeNativeInterface();

        if (!interface || !interface->validate()) {
		    std::cerr << "Error: skia GrGLInterface was not valid" << std::endl;
            GLint param;
            if (!interface) {
                std::cerr << "    GrGLMakeNativeInterface() returned nullptr" << std::endl;
                std::cerr << "    GrGLInterface probably missing some GL functions" << std::endl;
            } else {
                std::cerr << "    fStandard was " << interface->fStandard << std::endl;
            }
            std::cerr << "    Details of the glfw framebuffer were as follow:\n";
            glGetIntegerv(GL_RED_BITS, &param); std::cerr << "    GL_RED_BITS " << param << std::endl;
            glGetIntegerv(GL_GREEN_BITS, &param); std::cerr << "    GL_GREEN_BITS " << param << std::endl;
            glGetIntegerv(GL_BLUE_BITS, &param); std::cerr << "    GL_BLUE_BITS " << param << std::endl;
            glGetIntegerv(GL_ALPHA_BITS, &param); std::cerr << "    GL_ALPHA_BITS " << param << std::endl;
            glGetIntegerv(GL_DEPTH_BITS, &param); std::cerr << "    GL_DEPTH_BITS " << param << std::endl;
            glGetIntegerv(GL_STENCIL_BITS, &param); std::cerr << "    GL_STENCIL_BITS " << param << std::endl;
            std::cerr << "GL error code: " << glGetError() << std::endl;
            std::terminate();
        }

        sContext = GrDirectContext::MakeGL(interface).release();
        if (!sContext) {
            std::cerr << "Error: could not create skia context using MakeGL\n";
            std::terminate();
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
                std::terminate();
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
            std::terminate();
        }

        // start UI here
        if (!program.is_used("--variants") && !program.is_used("--images")) {
            int res = plotter.startUI(sContext, sSurface, program.get<int>("--delay"));  // plot regions
            if (res < 0) {
                std::cerr << "ERROR: Plot to screen returned " << res << std::endl;
                std::terminate();
            }
        } else if (program.is_used("--variants")) {  // plot variants as tiled images

            auto v = program.get<std::string>("--variants");
            std::vector<std::string> labels = Utils::split(iopts.labels, ',');
            plotter.setLabelChoices(labels);

            bool cacheStdin = (v == "-" || program.is_used("--out-vcf"));

            if (program.is_used("--in-labels")) {
                Utils::openLabels(program.get<std::string>("--in-labels"), plotter.inputLabels, labels, plotter.seenLabels);
            }
            if (program.is_used("--out-labels")) {
                plotter.setOutLabelFile(program.get<std::string>("--out-labels"));
            }

            plotter.setVariantFile(v, iopts.start_index, cacheStdin);
            plotter.mode = Manager::Show::TILED;

            int res = plotter.startUI(sContext, sSurface, program.get<int>("--delay"));
            if (res < 0) {
                std::cerr << "ERROR: Plot to screen returned " << res << std::endl;
                std::terminate();
            }

            if (program.is_used("--out-vcf")) {
                HGW::saveVcf(plotter.vcf, program.get<std::string>("--out-vcf"), plotter.multiLabels);
            }
        } else if (program.is_used("--images")) {

            auto img = program.get<std::string>("-i");
            if (img == ".") {
                img += "/";
            }
            std::vector<std::filesystem::path> paths = glob::glob(img);
#if defined(_WIN32) || defined(_WIN64)
            std::sort(paths.begin(), paths.end());
#else
            std::sort(paths.begin(), paths.end(), compareNat);
#endif
            plotter.image_glob = paths;
            if (plotter.image_glob.size() == 1) {
                plotter.opts.number.x = 1; plotter.opts.number.y = 1;
            }
            std::vector<std::string> labels = Utils::split(iopts.labels, ',');
            plotter.setLabelChoices(labels);

            if (program.is_used("--in-labels")) {
                Utils::openLabels(program.get<std::string>("--in-labels"), plotter.inputLabels, labels, plotter.seenLabels);
                std::string emptylabel;
                int index = 0;
                for (auto &item : plotter.image_glob) {
#if defined(_WIN32) || defined(_WIN64)
			const wchar_t* pc = item.filename().c_str();
			std::wstring ws(pc);
			std::string p(ws.begin(), ws.end());
#else
		    std::string p = item.filename();
#endif
		    if (Utils::endsWith(p, ".png")) {
                        std::vector<std::string> m = Utils::split(p.erase(p.size() - 4), '~');
                        try {
                            plotter.appendVariantSite(m[1], std::stoi(m[2]), m[3], std::stoi(m[4]), m[5], emptylabel, m[0]);
                        } catch (...) {
                            // append an empty variant, use the index at the id
                            std::string stri = std::to_string(index);
                            plotter.appendVariantSite(emptylabel, 0, emptylabel, 0, stri, emptylabel, emptylabel);
                        }
                        index += 1;
                    }
                }
            }
            if (program.is_used("--out-labels")) {
                plotter.setOutLabelFile(program.get<std::string>("--out-labels"));
            }

            plotter.mode = Manager::Show::TILED;

            int res = plotter.startUI(sContext, sSurface, program.get<int>("--delay"));
            if (res < 0) {
                std::cerr << "ERROR: Plot to screen returned " << res << std::endl;
                std::terminate();
            }
        }

    // save plot to file, use GPU if single image, or --use-raster backend otherwise. If no regions are
    // provided, but --outdir is present then the whole genome is plotted using raster backend
    } else {

        if (!program.is_used("--variants") && !program.is_used("--images")) {

            if (program.is_used("--file") && program.is_used("--outdir")) {
                std::cerr << "Error: provide --file or --outdir, not both\n";
                std::exit(-1);
            }

            plotter.opts.theme.setAlphas();

            if (program.is_used("--fmt") && program.get<std::string>("--fmt") == "pdf") {

                if (regions.empty()) {
                    std::cerr << "Error: --fmt pdf is only supported by providing a --region\n";
                    std::exit(-1);
                }

                fs::path fname;
                fs::path out_path;

                if (program.is_used("--file")) {
                    fname = program.get<std::string>("--file");
                    out_path = fname;
                } else {
                    if (outdir.empty()) {
                        std::cerr << "Error: please provide an output directory using --outdir, or direct to --file\n";
                        std::exit(-1);
                    }
                    fname = regions[0].chrom + "_" + std::to_string(regions[0].start) + "_" + std::to_string(regions[0].end) + ".pdf";
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

                auto pdfDocument = SkPDF::MakeDocument(&buffer);
                SkCanvas* pageCanvas = pdfDocument->beginPage(iopts.dimensions.x,iopts.dimensions.y);
                plotter.fb_width = iopts.dimensions.x;
                plotter.fb_height = iopts.dimensions.y;
                plotter.runDraw(pageCanvas);
                pdfDocument->close();
                buffer.writeToStream(&out);

            // Plot a png image, either of target region (GPU/raster) or whole chromosome (raster)
            } else {
                sk_sp<SkImage> img;
                if (!regions.empty()) {  // plot target regions
                    if (!program.is_used("--use-raster")) {
                        plotter.initBack(iopts.dimensions.x, iopts.dimensions.y);
                        int fb_height, fb_width;
                        glfwGetFramebufferSize(plotter.backWindow, &fb_width, &fb_height);
                        sContext = GrDirectContext::MakeGL(nullptr).release();
                        GrGLFramebufferInfo framebufferInfo;
                        framebufferInfo.fFBOID = 0;
                        framebufferInfo.fFormat = GL_RGBA8;
                        GrBackendRenderTarget backendRenderTarget(fb_width, fb_height, 0, 0, framebufferInfo);
                        if (!backendRenderTarget.isValid()) {
                            std::cerr << "ERROR: backendRenderTarget was invalid" << std::endl;
                            glfwTerminate();
                            std::terminate();
                        }
                        sSurface = SkSurface::MakeFromBackendRenderTarget(sContext, backendRenderTarget,kBottomLeft_GrSurfaceOrigin,kRGBA_8888_SkColorType,  nullptr, nullptr).release();
                        if (!sSurface) {
                            std::cerr << "ERROR: sSurface could not be initialized (nullptr). The frame buffer format needs changing\n";
                            sContext->releaseResourcesAndAbandonContext();
                            std::terminate();
                        }
                        SkCanvas *canvas = sSurface->getCanvas();
                        if (iopts.link_op == 0) {
                            plotter.runDrawNoBuffer(canvas);
                        } else {
                            plotter.runDraw(canvas);
                        }
                        img = sSurface->makeImageSnapshot();
                    } else {
                        plotter.setRasterSize(iopts.dimensions.x, iopts.dimensions.y);
                        plotter.gap = 0;
                        sk_sp<SkSurface> rasterSurface = SkSurface::MakeRasterN32Premul(iopts.dimensions.x, iopts.dimensions.y);
                        SkCanvas *canvas = rasterSurface->getCanvas();
                        if (iopts.link_op == 0) {
                            plotter.runDrawNoBuffer(canvas);
                        } else {
                            plotter.runDraw(canvas);
                        }
                        img = rasterSurface->makeImageSnapshot();
                    }
                    if (outdir.empty()) {
                        std::string fpath;
                        if (program.is_used("--file")) {
                            fpath = program.get<std::string>("--file");
                            Manager::imagePngToFile(img, fpath);
                        } else {
                            Manager::imagePngToStdOut(img);
                        }
                    } else {
                        fs::path fname = "GW~";
                        for (auto &rgn : regions) {
                            fname += rgn.chrom + "~" + std::to_string(rgn.start) + "~" + std::to_string(rgn.end) + "~";
                        }
                        fname += ".png";
                        fs::path out_path = outdir / fname;
                        Manager::imageToPng(img, out_path);
                    }

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
                    for (int i = 0; i < iopts.threads; ++i) {
                        auto *m = new Manager::GwPlot(genome, bam_paths, iopts, regions, tracks);
                        m->opts.theme.setAlphas();
                        m->setRasterSize(iopts.dimensions.x, iopts.dimensions.y);
                        m->opts.threads = 1;
                        m->gap = 0;
                        m->regions.resize(1);
                        managers.push_back(m);
                    }

                    std::vector< std::vector<Utils::Region> > jobs(iopts.threads);
                    int part = 0;
                    for (int i=0; i < faidx_nseq(managers[0]->fai); ++i) {
                        const char *chrom = faidx_iseq(managers[0]->fai, i);
                        int seq_len = faidx_seq_len(managers[0]->fai, chrom);
                        Utils::Region N;
                        N.chrom = chrom;
                        N.start = 1;
                        N.end = seq_len;
                        jobs[part].push_back(N);
                        part = (part == (int)jobs.size() - 1) ? 0 : part + 1;
                    }

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
                                              std::vector<Utils::Region> &all_regions = jobs[this_block];
                                              sk_sp<SkSurface> rasterSurface = SkSurface::MakeRasterN32Premul(iopts.dimensions.x, iopts.dimensions.y);
                                              SkCanvas *canvas = rasterSurface->getCanvas();
                                              for (auto &rgn : all_regions) {
                                                  plt->collections.clear();
                                                  delete plt->regions[0].refSeq;
                                                  plt->regions[0].chrom = rgn.chrom;
                                                  plt->regions[0].start = rgn.start;
                                                  plt->regions[0].end = rgn.end;
                                                  if (iopts.link_op == 0) {
                                                      plotter.runDrawNoBuffer(canvas);
                                                  } else {
                                                      plotter.runDraw(canvas);
                                                  }
                                                  sk_sp<SkImage> img(rasterSurface->makeImageSnapshot());
                                                  fs::path fname = "GW~" + plt->regions[0].chrom + "~" + std::to_string(plt->regions[0].start) + "~" + std::to_string(plt->regions[0].end) + "~.png";
                                                  fs::path out_path = outdir / fname;
                                                  Manager::imageToPng(img, out_path);
                                              }
                                          })
                            .wait();

                    for (auto &itm : managers) {
                        delete(itm);
                    }
                }
            }

        } else if (program.is_used("--variants") && !program.is_used("--out-vcf")) {
            if (outdir.empty()) {
                std::cerr << "Error: please provide an output directory using --outdir\n";
                std::exit(-1);
            }

            auto v = program.get<std::string>("--variants");

            if (Utils::endsWith(v, "vcf") || Utils::endsWith(v, "vcf.gz") || Utils::endsWith(v, "bcf")) {

                if (program.is_used("--fmt") && program.get<std::string>("--fmt") == "pdf") {
                    std::cerr << "Error: only --fmt png is supported with -v\n";
                    return -1;
                }
                iopts.theme.setAlphas();

                auto vcf = HGW::VCFfile();
                vcf.cacheStdin = false;
                vcf.label_to_parse = iopts.parse_label.c_str();

                fs::path dir(outdir);

                bool writeLabel;
                std::ofstream fLabels;

                if (!iopts.parse_label.empty()) {
                    writeLabel = true;
                    fs::path file ("gw.parsed_labels.tsv");
                    fs::path full_path = dir / file;
                    std::string outname = full_path.string();
                    fLabels.open(full_path);
                    fLabels << "#chrom\tpos\tvariant_ID\tlabel\tvar_type\tlabelled_date\n";
                } else {
                    writeLabel = false;
                }

                vcf.open(v);

                std::vector<Manager::GwPlot *> managers;
                for (int i = 0; i < iopts.threads; ++i) {
                    auto *m = new Manager::GwPlot(genome, bam_paths, iopts, regions, tracks);
                    m->opts.theme.setAlphas();
                    m->setRasterSize(iopts.dimensions.x, iopts.dimensions.y);
                    m->opts.threads = 1;
                    managers.push_back(m);
                }

                std::vector<Manager::VariantJob> jobs;
                std::vector<std::string> empty_labels;
                std::string dateStr;
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
                        Utils::Label l = Utils::makeLabel(vcf.chrom, vcf.start, vcf.label, empty_labels, vcf.rid, vcf.vartype, "", 0);
                        Utils::labelToFile(fLabels, l, dateStr);
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
                                            sk_sp<SkSurface> rasterSurface = SkSurface::MakeRasterN32Premul(iopts.dimensions.x, iopts.dimensions.y);
                                            SkCanvas *canvas = rasterSurface->getCanvas();
                                            for (int i = a; i < b; ++i) {
                                                Manager::VariantJob job = jobs[i];
                                                plt->setVariantSite(job.chrom, job.start, job.chrom2, job.stop);
                                                if (plt->opts.low_mem && plt->opts.link_op == 0) {
                                                    plt->runDrawNoBuffer(canvas);
                                                } else {
                                                    plt->runDraw(canvas);
                                                }
                                                sk_sp<SkImage> img(rasterSurface->makeImageSnapshot());
                                                fs::path fname = job.varType + "~" + job.chrom + "~" + std::to_string(job.start) + "~" + job.chrom2 + "~" + std::to_string(job.stop) + "~" + job.rid + ".png";
                                                fs::path full_path = outdir / fname;
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

            auto v = program.get<std::string>("--variants");
            std::vector<std::string> labels = Utils::split(iopts.labels, ',');
            if (program.is_used("--in-labels")) {
                Utils::openLabels(program.get<std::string>("--in-labels"), plotter.inputLabels, labels, plotter.seenLabels);
            }
            plotter.setVariantFile(v, iopts.start_index, false);
            plotter.setLabelChoices(labels);
            plotter.mode = Manager::Show::TILED;

            HGW::VCFfile &vcf = plotter.vcf;
            std::vector<std::string> empty_labels;
            while (true) {
                vcf.next();
                if (vcf.done) {break; }
                if (plotter.inputLabels.contains(vcf.rid)) {
                    plotter.multiLabels.push_back(plotter.inputLabels[vcf.rid]);
                } else {
                    plotter.multiLabels.push_back(Utils::makeLabel(vcf.chrom, vcf.start, vcf.label, empty_labels, vcf.rid, vcf.vartype, "", 0));
                }
            }
            if (program.is_used("--out-vcf")) {
                HGW::saveVcf(plotter.vcf, program.get<std::string>("--out-vcf"), plotter.multiLabels);
            }
        }
    }
    if (!iopts.no_show)
        std::cout << "\nGw finished\n";
    return 0;
};
