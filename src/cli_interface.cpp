//
// Created by Kez Cleal on 09/02/2025.
//

#include "cli_interface.h"
#include "gw_version.h"
#include <algorithm>
#include <filesystem>
#include <htslib/faidx.h>
#include <iostream>
#include <mutex>
#include <string>
#include "argparse.h"
#include "BS_thread_pool.h"
#include "cli_interface.h"
#include "glob_cpp.hpp"
#include "hts_funcs.h"
#include "plot_manager.h"
#include "themes.h"
#include "utils.h"
#include "gw_version.h"

#include "termcolor.h"
#include "GLFW/glfw3.h"


void print_gw_banner() {
#if defined(_WIN32) || defined(_WIN64) || defined(__MSYS__)
    std::cout << "\n"
                     "  __________      __ \n"
 " /  _____/  \\    /  \\\n"
 "/   \\  __\\   \\/\\/   /\n"
 "\\    \\_\\  \\        / \n"
 " \\______  /\\__/\\  /  \n"
 "        \\/      \\/  " << GW_VERSION << std::endl;
#else
    std::cout << "\n"
                 "█▀▀ █ █ █\n"
                 "█▄█ ▀▄▀▄▀ v" << GW_VERSION << std::endl;
#endif
}


bool str_is_number(const std::string &s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), ::isdigit);
}


CLIOptions CLIInterface::parseArguments(int argc, char* argv[], Themes::IniOptions& iopts) {

    CLIOptions options;

    bool success = iopts.readIni();
    if (!success) {

    }
    bool have_session_file = !iopts.session_file.empty();
    options.useSession = false;

    static const std::vector<std::string> img_fmt = { "png", "pdf", "svg" };
    static const std::vector<std::string> img_themes = { "igv", "dark", "slate" };
    static const std::vector<std::string> links = { "none", "sv", "all" };

    options.program = argparse::ArgumentParser("gw", std::string(GW_VERSION));
    argparse::ArgumentParser& program = options.program;

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
    program.add_argument("--ideogram")
            .append()
            .help("Ideogram bed file (uncompressed). Any bed file should work");
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
    program.add_argument("--session")
            .default_value(std::string{""}).append()
            .help("GW session file to load (.ini suffix)");
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
    program.add_argument("--mods")
            .default_value(false).implicit_value(true)
            .help("Base modifications are shown");
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
    program.add_argument("-c", "--command")
            .default_value(std::string{""}).append()
            .help("Apply command before drawing e.g. -c 'sort strand'");
    program.add_argument("--delay")
            .default_value(0).append().scan<'i', int>()
            .help("Delay in milliseconds before each update, useful for remote connections");
    program.add_argument("--config")
            .default_value(false).implicit_value(true)
            .help("Display path of loaded .gw.ini config");

    options.showBanner = true;

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
        std::exit(0);
    }

    std::vector<std::string>& bamPaths = options.bamPaths;
    std::vector<std::string>& tracks = options.tracks;
    std::vector<Utils::Region>& regions = options.regions;

    if (program.is_used("-r")) {
        std::vector<std::string> regions_str;
        regions_str = program.get<std::vector<std::string>>("-r");
        for (size_t i=0; i < regions_str.size(); i++){
            regions.push_back(Utils::parseRegion(regions_str[i]));
        }
    }

    if (program.is_used("--session")) {
        iopts.session_file = program.get<std::string>("--session");
        have_session_file = true;
        options.useSession = true;
    }

    // check if bam/cram file provided as main argument
    std::string &genome = options.genome;
    genome = program.get<std::string>("genome");
    if (Utils::endsWith(genome, ".bam") || Utils::endsWith(genome, ".cram")) {
        HGW::guessRefGenomeFromBam(genome, iopts, bamPaths, regions);
        if (genome.empty()) {
            std::cerr << "Error: please provide a reference genome\n";
            std::exit(0);
        }
    }


    if (iopts.myIni["genomes"].has(genome)) {
        iopts.genome_tag = genome;
        genome = iopts.myIni["genomes"][genome];
    } else if (genome.empty() && !program.is_used("--images") && !iopts.ini_path.empty() && !program.is_used("--no-show") && !program.is_used("--session")) {
        // prompt for genome
        print_gw_banner();
        options.showBanner = false;

        std::cout << "\nReference genomes listed in " << iopts.ini_path << std::endl << std::endl;
        std::string online = "https://github.com/kcleal/ref_genomes/releases/download/v0.1.0";
#if defined(_WIN32) || defined(_WIN64) || defined(__MSYS__)
        const char *block_char = "*";
#else
        const char *block_char = "▀";
#endif
        std::cout << " " << block_char << " " << online << std::endl << std::endl;
        int i = 0;
        int tag_wd = 11;
        std::vector<std::pair<std::string, std::string>> vals;

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
            std::cout << "    " << termcolor::bold << i << termcolor::reset  << ((i < 10) ? "    " : "   ")  << "│ " << tag;
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
            vals.push_back(rg);
            i += 1;
        }
        if (i == 0 && !have_session_file && !std::filesystem::exists(iopts.session_file)) {
            std::cerr << "No genomes listed, finishing\n";
            std::exit(0);
        }

        user_prompt:

        if (have_session_file && std::filesystem::exists(iopts.session_file)) {
            std::cout << "\nPress ENTER to load previous session \nInput a genome number/tag, path, or session: " << std::flush;
        } else {
            std::cout << "\nEnter genome number/tag, path or session: " << std::flush;
        }

        std::string user_input;
        std::getline(std::cin, user_input);
        size_t user_i = 0;
        if (user_input == "q" || user_input == "quit" || user_input == "exit") {
            std::exit(0);
        }
        if (user_input.empty()) {
            have_session_file = std::filesystem::exists(iopts.session_file);
            if (have_session_file) {
                options.useSession = true;
            } else {
                goto user_prompt;
            }
        } else {
            bool is_number = str_is_number(user_input);
            if (Utils::endsWith(user_input, ".ini")) {  // Assume session
                iopts.session_file = user_input;
                have_session_file = true;
                options.useSession = true;
            } else if (is_number) {
                try {
                    user_i = std::stoi(user_input);
                    options.genome = vals[user_i].second;
                    iopts.genome_tag = vals[user_i].first;
                    std::cout << "Genome:  " << iopts.genome_tag << std::endl;
                } catch (...) {
                    goto user_prompt;
                }
                if (user_i < 0 || user_i > vals.size() -1) {
                    goto user_prompt;
                }
            } else {  // try tag or path
                bool found_tag = false;
                for (auto &rg: iopts.myIni["genomes"]) {
                    std::string tag = rg.first;
                    if (tag == user_input) {
                        options.genome = rg.second;
                        found_tag = true;
                        break;
                    }
                }
                if (!found_tag) {
                    options.genome = user_input;
                }
            }
        }

    } else if (!options.genome.empty() && !Utils::is_file_exist(options.genome)) {
        std::cerr << "Loading remote genome" << std::endl;
    }

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
        auto bamPaths_temp = program.get<std::vector<std::string>>("-b");
        for (auto &item : bamPaths_temp) {
            bamPaths.push_back(item);
            continue;
            if (std::filesystem::exists(item)) {
                bamPaths.push_back(item);
                std::cerr << item << std::endl;
            } else {
                std::vector<std::filesystem::path> glob_paths = glob_cpp::glob(item);
//#if defined(_WIN32) || defined(_WIN64)
//                std::sort(glob_paths.begin(), glob_paths.end());
//#else
//                std::sort(glob_paths.begin(), glob_paths.end(), compareNat);
//#endif
                for (auto &glob_item : glob_paths) {
                    bamPaths.push_back(glob_item.string());
                }
            }
        }
        if (!have_session_file && !program.is_used("genome") && options.genome.empty() && !bamPaths.empty()) {
            HGW::guessRefGenomeFromBam(options.genome, iopts, bamPaths, regions);
            if (options.genome.empty()) {
                std::exit(0);
            }
            if (iopts.myIni["genomes"].has(options.genome)) {
                iopts.genome_tag = options.genome;
                options.genome = iopts.myIni["genomes"][genome];
            } else {
                std::cerr << "Error: could not find a reference genome\n";
            }
        }
    }

    std::string& outdir = options.outdir;
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
            std::exit(-1);
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
    if (program.is_used("--mods")) {
        iopts.parse_mods = true;
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

    std::vector<std::string>& filters = options.filters;
    if (program.is_used("--filter")) {
        filters = Utils::split(program.get("--filter"), ';');
    }

    std::vector<std::string>& extra_commands = options.extra_commands;
    if (program.is_used("--command")) {
        extra_commands = program.get<std::vector<std::string>>("--command");
    }


    return options;
}