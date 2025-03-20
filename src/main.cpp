
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

#define SK_GL
#include "include/core/SkDocument.h"
#include "include/docs/SkPDFDocument.h"
#include "include/core/SkPictureRecorder.h"
#include "include/core/SkPicture.h"
#include "include/svg/SkSVGCanvas.h"

#ifdef __EMSCRIPTEN__
    #include <emscripten.h>
#endif

// note to developer - update version in gw_version.h, Makefile, workflows/main.yml, and deps/gw.desktop, and installers .md in docs (post release)

// skia context has to be managed from global space
GrDirectContext *sContext = nullptr;
SkSurface *sSurface = nullptr;
std::mutex mtx;


int main(int argc, char *argv[]) {

    // Options needed by GW at runtime
    Themes::IniOptions iopts;

    // Additional setup options for initializing GW
    CLIOptions options = CLIInterface::parseArguments(argc, argv, iopts);

    // All cli arguments and options, some parsing is deferred below
    argparse::ArgumentParser& program = options.program;

    if (!iopts.no_show) {  // draw something to screen
        if (options.useSession) {
            mINI::INIFile file(iopts.session_file);
            file.read(iopts.seshIni);
            if (!iopts.seshIni.has("data") || !iopts.seshIni.has("show")) {
                std::cerr << "Error: session file is missing 'data' or 'show' headings. Invalid session file\n";
                std::exit(-1);
            }
            iopts.getOptionsFromSessionIni(iopts.seshIni);
        } else {
            iopts.session_file = "";
        }
        /*
         * / Gw start
         */
        Manager::GwPlot plotter = Manager::GwPlot(options.genome, options.bamPaths, iopts, options.regions, options.tracks);

        if (options.showBanner) {
            print_gw_banner();
        }

        for (auto &s: options.filters) {
            plotter.addFilter(s);
        }

        if (program.is_used("--ideogram")) {
            plotter.addIdeogram(program.get("--ideogram"));
        } else if (!iopts.genome_tag.empty() && plotter.ideogram_path.empty()) {
            plotter.loadIdeogramTag();
        }

        // initialize graphics window
        plotter.init(iopts.dimensions.x, iopts.dimensions.y);
        int fb_height, fb_width;
        glfwGetFramebufferSize(plotter.window, &fb_width, &fb_height);

        plotter.setGlfwFrameBufferSize();

        sk_sp<const GrGLInterface> interface = GrGLMakeNativeInterface();

#ifndef OLD_SKIA
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

        //
        sContext = GrDirectContexts::MakeGL(interface).release();
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

            framebufferInfo.fFormat = fbFormats[i];

            auto backendRenderTarget = GrBackendRenderTargets::MakeGL(
                    fb_width,
                    fb_height,
                    0,        // sampleCnt
                    0,        // stencilBits
                    framebufferInfo
            );

            if (!backendRenderTarget.isValid()) {
                std::cerr << "ERROR: backendRenderTarget was invalid" << std::endl;
                glfwTerminate();
                std::exit(-1);
            }

            // Now create the surface
            SkSurfaceProps props(0, kUnknown_SkPixelGeometry);
            sSurface = SkSurfaces::WrapBackendRenderTarget(
                    sContext,
                    backendRenderTarget,
                    kBottomLeft_GrSurfaceOrigin,
                    colorTypes[i],
                    nullptr,
                    &props
            ).release();

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

        auto imageInfo = SkImageInfo::MakeN32Premul(
                iopts.dimensions.x * plotter.monitorScale,
                iopts.dimensions.y * plotter.monitorScale);
        sk_sp<SkSurface> rasterSurface = SkSurfaces::Raster(imageInfo);
#else
        sContext = GrDirectContext::MakeGL(interface).release();
        if (!sContext) {
            std::cerr << "Error: could not create skia-m93 context using MakeGL\n";
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
        sk_sp<SkSurface> rasterSurface = SkSurface::MakeRasterN32Premul(iopts.dimensions.x * plotter.monitorScale,
                                                                        iopts.dimensions.y * plotter.monitorScale);
#endif

        plotter.rasterCanvas = rasterSurface->getCanvas();
        plotter.rasterSurfacePtr = &rasterSurface;


        /*
         * / UI starts here
         */
        if (!program.is_used("--variants") && !program.is_used("--images")) {
            int res = plotter.startUI(sContext, sSurface,
                                      program.get<int>("--delay"),
                                      options.extra_commands);  // plot regions
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

            int res = plotter.startUI(sContext, sSurface,
                                      program.get<int>("--delay"),
                                      options.extra_commands);
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

            int res = plotter.startUI(sContext, sSurface,
                                      program.get<int>("--delay"),
                                      options.extra_commands);
            if (res < 0) {
                std::cerr << "ERROR: Plot to screen returned " << res << std::endl;
                std::exit(-1);
            }
        }

    // save plot to file. If no regions are
    // provided, but if --outdir is present then the whole genome is plotted using raster backend
    } else {

        if (!program.is_used("--variants") && !program.is_used("--images")) {

            if (program.is_used("--file") && program.is_used("--outdir")) {
                std::cerr << "Error: provide --file or --outdir, not both\n";
                std::exit(-1);
            }
            Manager::GwPlot plotter = Manager::GwPlot(options.genome, options.bamPaths, iopts, options.regions, options.tracks);

            for (auto &s: options.filters) {
                plotter.addFilter(s);
            }
            if (program.is_used("--ideogram")) {
                plotter.addIdeogram(program.get("--ideogram"));
            }

            plotter.opts.theme.setAlphas();

            std::filesystem::path fname;
            std::filesystem::path out_path;
            std::string format_str = ".png";
            if (program.is_used("--file")) {
                fname = program.get<std::string>("--file");
                format_str = fname.extension().generic_string();
            } else if (program.is_used("--fmt") && program.get<std::string>("--fmt") != "png") {
                format_str = "." + program.get<std::string>("--fmt");
            }

            if (format_str != ".png") {
                if (options.regions.empty()) {
                    std::cerr << "Error: please provide a --region\n";
                    std::exit(-1);
                }

                if (fname.empty()) {
                    if (fname.empty()) {
                        std::cerr << "Error: please provide an output directory using --outdir, or direct to --file\n";
                        std::exit(-1);
                    }
                    fname = options.regions[0].chrom + "_" + std::to_string(options.regions[0].start) + "_" +
                            std::to_string(options.regions[0].end) + format_str;

                }
                out_path = options.outdir / fname;

#if defined(_WIN32) || defined(_WIN64)
                const wchar_t* outp = out_path.c_str();
        std::wstring pw(outp);
        std::string outp_str(pw.begin(), pw.end());
        SkFILEWStream out(outp_str.c_str());
#else
                SkFILEWStream out(out_path.c_str());
#endif
                SkDynamicMemoryWStream buffer;

                if (format_str == ".pdf") {
                    auto pdfDocument = SkPDF::MakeDocument(&buffer);
                    SkCanvas *canvas = pdfDocument->beginPage(iopts.dimensions.x, iopts.dimensions.y);
                    Manager::drawImageCommands(plotter, canvas, options.extra_commands);

                    pdfDocument->close();
                    buffer.writeToStream(&out);
                } else {

                    plotter.setImageSize(iopts.dimensions.x, iopts.dimensions.y);
                    SkPictureRecorder recorder;
                    SkCanvas* canvas = recorder.beginRecording(SkRect::MakeWH(iopts.dimensions.x, iopts.dimensions.y));
                    Manager::drawImageCommands(plotter, canvas, options.extra_commands);

                    sk_sp<SkPicture> picture = recorder.finishRecordingAsPicture();
                    std::unique_ptr<SkCanvas> svgCanvas = SkSVGCanvas::Make(SkRect::MakeWH(iopts.dimensions.x, iopts.dimensions.y), &out);
                    if (svgCanvas) {
                        picture->playback(svgCanvas.get());
                    };
                }

            } else {
                // Plot a png image, either of target region or whole chromosome
                sk_sp<SkImage> img;
                if (!plotter.regions.empty()) {  // plot target regions

#ifndef OLD_SKIA
                    auto imageInfo = SkImageInfo::MakeN32Premul(
                            iopts.dimensions.x * plotter.monitorScale,
                            iopts.dimensions.y * plotter.monitorScale);
                    sk_sp<SkSurface> rasterSurface = SkSurfaces::Raster(imageInfo);
#else
                    sk_sp<SkSurface> rasterSurface = SkSurface::MakeRasterN32Premul(iopts.dimensions.x,
                                                                                    iopts.dimensions.y);
#endif
                    SkCanvas *canvas = rasterSurface->getCanvas();
                    Manager::drawImageCommands(plotter, canvas, options.extra_commands);

                    img = rasterSurface->makeImageSnapshot();

                    if (options.outdir.empty()) {
                        std::string fpath;
                        if (program.is_used("--file")) {
                            fpath = program.get<std::string>("--file");
                            Manager::imagePngToFile(img, fpath);
                        } else {
                            Manager::imagePngToStdOut(img);
                        }
                    } else {
                        std::filesystem::path fname = Utils::makeFilenameFromRegions(options.regions);
                        std::filesystem::path out_path = options.outdir / fname;
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
                        auto *m = new Manager::GwPlot(options.genome, options.bamPaths, iopts, options.regions, options.tracks);
                        m->opts.theme.setAlphas();
                        m->setImageSize(iopts.dimensions.x, iopts.dimensions.y);
                        m->opts.threads = 1;
                        m->gap = 0;
                        m->drawLocation = false;
                        m->regions.resize(1);

                        for (auto &s: options.filters) {
                            m->addFilter(s);
                        }
                        if (program.is_used("--ideogram")) {
                            m->addIdeogram(program.get("--ideogram"));
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

                                              for (auto &rgn: all_regions) {
                                                  plt->collections.clear();
                                                  delete plt->regions[0].refSeq;
                                                  plt->regions[0].chrom = rgn.chrom;
                                                  plt->regions[0].start = rgn.start;
                                                  plt->regions[0].end = rgn.end;
                                                  if (!options.extra_commands.empty()) {
                                                      for (const auto& command: options.extra_commands) {
                                                          plt->inputText = command;
                                                          plt->commandProcessed();
                                                      }
                                                  }
                                                  plt->runDrawNoBuffer();

                                                  sk_sp<SkImage> img(plt->rasterSurface->makeImageSnapshot());
                                                  std::filesystem::path fname = "GW~" + plt->regions[0].chrom + "~" +
                                                                   std::to_string(plt->regions[0].start) + "~" +
                                                                   std::to_string(plt->regions[0].end) + "~.png";
                                                  std::filesystem::path out_path = options.outdir / fname;
                                                  Manager::imageToPng(img, out_path);
                                              }
                                          })
                            .wait();

                    for (auto &itm: managers) {
                        delete (itm);
                    }
                }
            }
        // Save images of all variants to a folder .png format
        } else if (program.is_used("--variants") && !program.is_used("--out-vcf")) {
            if (options.outdir.empty()) {
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

                std::filesystem::path dir(options.outdir);

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
                    auto *m = new Manager::GwPlot(options.genome, options.bamPaths, iopts, options.regions, options.tracks);
                    m->opts.theme.setAlphas();
                    m->setImageSize(iopts.dimensions.x, iopts.dimensions.y);
                    m->makeRasterSurface();
                    m->opts.threads = 1;
                    for (auto &s: options.filters) {
                        m->addFilter(s);
                    }
                    if (program.is_used("--ideogram")) {
                        m->addIdeogram(program.get("--ideogram"));
                    }
                    managers.push_back(m);
                }

                std::vector<Manager::VariantJob> jobs;
                std::vector<std::string> empty_labels;
                std::string dateStr;

                std::string fileName;
                std::string empty_comment;
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
                        Utils::Label l = Utils::makeLabel(vcf.chrom, vcf.start, vcf.label, empty_labels, vcf.rid, vcf.vartype, "", false, false, empty_comment);
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

//                                            sk_sp<SkSurface> rasterSurface = SkSurface::MakeRasterN32Premul(iopts.dimensions.x, iopts.dimensions.y);
//                                            SkCanvas *canvas = rasterSurface->getCanvas();
                                            for (int i = a; i < b; ++i) {
                                                Manager::VariantJob job = jobs[i];
                                                plt->setVariantSite(job.chrom, job.start, job.chrom2, job.stop);

                                                bool stream_reads = iopts.link_op == 0;
                                                if (!options.extra_commands.empty()) {
                                                    plt->regionSelection = 0;
                                                    for (const auto& command: options.extra_commands) {
                                                        plt->inputText = command;
                                                        plt->commandProcessed();
                                                        if (plt->regions.front().sortOption != 0) {
                                                            stream_reads = false;
                                                        }
                                                    }
                                                }
                                                if (stream_reads) {
                                                    plt->runDrawNoBuffer();
                                                } else {
                                                    plt->runDraw();
                                                }
                                                sk_sp<SkImage> img(plt->rasterSurface->makeImageSnapshot());
                                                std::filesystem::path fname = job.varType + "~" + job.chrom + "~" + std::to_string(job.start) + "~" + job.chrom2 + "~" + std::to_string(job.stop) + "~" + job.rid + ".png";
                                                std::filesystem::path full_path = options.outdir / fname;
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
    }
    return 0;
};
