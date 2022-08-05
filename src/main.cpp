//
// Created by Kez Cleal on 25/07/2022.
//
#include <filesystem>
#include <htslib/faidx.h>
#include <htslib/hfile.h>
#include <htslib/sam.h>
#include <iostream>
#include <string>

#include "argparse.h"
#include "glob.h"
#include "plot_manager.h"
#include "themes.h"
#include "utils.h"

#ifdef __APPLE__
    #include <OpenGL/gl.h>
#endif
#include "GLFW/glfw3.h"
#define SK_GL
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"


// skia context has to be managed from global space to work
GrDirectContext *sContext = nullptr;
SkSurface *sSurface = nullptr;


int main(int argc, char *argv[]) {

    Themes::IniOptions iopts;
    static const std::vector<std::string> img_fmt = { "png", "pdf" };
    static const std::vector<std::string> img_themes = { "igv", "dark" };
    static const std::vector<std::string> links = { "none", "sv", "all" };

    argparse::ArgumentParser program("gw", "0.1");
    program.add_argument("genome")
            .required()
            .help("Reference genome in .fasta format with .fai index file");
    program.add_argument("-b", "--bam")
            .default_value(std::string{""}).append()
            .help("Bam/cram alignment file. Repeat for multiple files stacked vertically");
    program.add_argument("-r", "--region")
            .append()
            .help("Region of alignment file to display in window. Repeat to horizontally split window into multiple regions");
    program.add_argument("-v", "--variants")
            .default_value(std::string{""}).append()
            .help("VCF/BCF/BED/BEDPE file to derive regions from. Can not be used with -i");
    program.add_argument("-i", "--images")
            .append()
            .help("Glob path to .png images to displaye e.g. '*.png'. Can not be used with -v");
    program.add_argument("-o", "--outdir")
            .default_value(std::string{"."}).append()
            .help("Output folder to store images");
    program.add_argument("-n", "--no-show")
            .default_value(false).implicit_value(true)
            .help("Dont display images to screen");
    program.add_argument("-d", "--dims")
            .default_value(iopts.dimensions).append()
            .help("Image dimensions (px)");
    program.add_argument("-u", "--number")
            .default_value(iopts.number).append()
            .help("Images tiles to display (used with -v and -i)");
    program.add_argument("--theme")
            .default_value(iopts.theme)
            .action([](const std::string& value) {
                if (std::find(img_themes.begin(), img_themes.end(), value) != img_themes.end()) { return value;}
                return std::string{ "igv" };
            }).help("Image colour theme");
    program.add_argument("--fmt")
            .default_value(iopts.fmt)
            .action([](const std::string& value) {
                if (std::find(img_fmt.begin(), img_fmt.end(), value) != img_fmt.end()) { return value;}
                return std::string{ "png" };
            }).help("Output file format");
    program.add_argument("--bed")
            .default_value(std::string{""}).append()
            .help("Bed track to display at bottom of window. Repeat for multiple files stacked vertically");
    program.add_argument("-t", "--threads")
            .default_value(iopts.threads).append().scan<'i', int>()
            .help("Number of threads to use");
    program.add_argument("--parse-label")
            .default_value(iopts.labels).append()
            .help("Label to parse from vcf file (used with -v) e.g. 'filter' or 'info.SU'");
    program.add_argument("--in-labels")
            .default_value(std::string{""}).append()
            .help("Overlay labels from FILE on images (use with -v or -i)");
    program.add_argument("--out-labels")
            .default_value(std::string{""}).append()
            .help("Save labelling results to FILE (use with -v or -i)");
    program.add_argument("--start-index")
            .default_value(0).append().scan<'i', int>()
            .help("Start labelling from -v / -i index (zero-based)");
    program.add_argument("--resume")
            .default_value(false).implicit_value(true)
            .help("Resume labelling from first un-reviewed block of images");
    program.add_argument("--pad")
            .default_value(iopts.pad).append().scan<'i', int>()
            .help("Padding +/- in bp to add to each region from -v or -r");
    program.add_argument("--ylim")
            .default_value(iopts.ylim).append().scan<'i', int>()
            .help("Maximum y limit (depth of coverage) of image");
    program.add_argument("--no-cov")
            .default_value(false).implicit_value(true)
            .help("Scale coverage track to log2");
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
    program.add_argument("--link")
            .default_value(iopts.link)
            .action([](const std::string& value) {
                if (std::find(links.begin(), links.end(), value) != links.end()) { return value;}
                return std::string{ "None" };
            }).help("Draw linking lines between these alignments");

    // check input for errors and merge input options with IniOptions
    try {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err) {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        std::exit(1);
    }

    auto genome = program.get<std::string>("genome");
    if (iopts.references.find(genome) != iopts.references.end()){
        genome = iopts.references[genome];
    } else if (!Utils::is_file_exist(genome)) {
        std::cerr << "Error: Genome not found" << std::endl;
        abort();
    }
    std::cout << "Genome: " << genome << std::endl;

    std::vector<std::string> bam_paths;
    if (program.is_used("-b")) {
        bam_paths = program.get<std::vector<std::string>>("-b");
    }

    std::vector<Utils::Region> regions;
    if (program.is_used("-r")) {
        std::vector<std::string> regions_str;
        regions_str = program.get<std::vector<std::string>>("-r");
        for (size_t i=0; i < regions_str.size(); i++){
            regions.push_back(Utils::parseRegion(regions_str[i]));
            std::cout << regions[i].chrom << ":" << regions[i].start << " " << regions[i].end << std::endl;
        }
    }

    std::vector<std::filesystem::path> image_glob;
    if (program.is_used("-i")) {
        image_glob = glob::glob(program.get<std::string>("-i"));
    }

    if (program.is_used("-o")) {
        auto outdir = program.get<std::string>("-o");
        if (!std::filesystem::is_directory(outdir) || !std::filesystem::exists(outdir)) { // Check if src folder exists
            std::filesystem::create_directory(outdir); // create src folder
        }
    }

    if (program.is_used("-n")) {
        iopts.no_show = program.get<bool>("-n");
    }

    if (program.is_used("--theme") && program.get<std::string>("--theme") == "dark") {
        iopts.theme = Themes::DarkTheme();
    } else {
        iopts.theme = Themes::IgvTheme();
    }

    if (program.is_used("--dims")) {
        auto d = program.get<std::string>("--dims");
        iopts.dimensions = Utils::parseDimensions(d);
    }

    if (program.is_used("-u")) {
        auto d = program.get<std::string>("-u");
        iopts.number = Utils::parseDimensions(d);
    }

    if (program.is_used("--bed")) {
        std::vector<std::string> bed_regions;
        bed_regions = program.get<std::vector<std::string>>("--bed");
        for (size_t i=0; i < bed_regions.size(); i++){
            if (!Utils::is_file_exist(bed_regions[i])) {
                std::cerr << "Error: bed file does not exists - " << bed_regions[i] << std::endl;
                std::abort();
            }
            iopts.tracks[genome].push_back(bed_regions[i]);
        }
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


    if (program.is_used("-t")) {
        iopts.threads = program.get<int>("t");
    }

    for (size_t i=0; i < bam_paths.size(); i++){
        std::cout << bam_paths[i] << std::endl;
    }

    for (size_t i=0; i < image_glob.size(); i++){
        std::cout << image_glob[i] << std::endl;
    }


    Manager::GwPlot plotter = Manager::GwPlot(genome, bam_paths, iopts, regions);


    if (!program.get<bool>("-n")) {

        // Manager::SkiaWindow window = Manager::SkiaWindow();
        plotter.window.init(iopts.dimensions.x, iopts.dimensions.y);
        int fb_height, fb_width;
        glfwGetFramebufferSize(plotter.window.window, &fb_width, &fb_height);

        sContext = GrDirectContext::MakeGL(nullptr).release();
        GrGLFramebufferInfo framebufferInfo;
        framebufferInfo.fFBOID = 0;
        framebufferInfo.fFormat = GL_SRGB8_ALPHA8; // GL_RGBA8;

        GrBackendRenderTarget backendRenderTarget(fb_width, fb_height, 0, 0, framebufferInfo);
        if (!backendRenderTarget.isValid()) {
            std::cerr << "ERROR: backendRenderTarget was invalid" << std::endl;
            glfwTerminate();
            std::terminate();
        }

        sSurface = SkSurface::MakeFromBackendRenderTarget(sContext,
                                                          backendRenderTarget,
                                                          kBottomLeft_GrSurfaceOrigin,
                                                          kRGBA_8888_SkColorType,
                                                          nullptr,
                                                          nullptr).release();
        if (!sSurface) {
            std::cerr << "ERROR: sSurface could not be initialized (nullptr). The frame buffer format needs changing\n";
            sContext->releaseResourcesAndAbandonContext();
            std::terminate();
        }

        int res = plotter.plotToScreen(sSurface->getCanvas(), sContext);

        if (res < 0) {
            std::cerr << "ERROR: Plot to screen returned " << res << std::endl;
            sContext->releaseResourcesAndAbandonContext();
            std::terminate();
        }
    }

    return 0;
};