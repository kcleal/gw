
#include <htslib/faidx.h>
#include <htslib/hfile.h>
#include <htslib/sam.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef __APPLE__
    #include <OpenGL/gl.h>
#endif

#include "htslib/faidx.h"
#include "htslib/hfile.h"
#include "htslib/hts.h"
#include "htslib/sam.h"

#include <GLFW/glfw3.h>
#define SK_GL
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"

#include "drawing.h"
#include "hts_funcs.h"
#include "plot_manager.h"
#include "segments.h"
#include "themes.h"


namespace Manager {

    SkiaWindow::~SkiaWindow() {
        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void SkiaWindow::init(int width, int height) {

        if (!glfwInit()) {
            std::cerr<<"ERROR: could not initialize GLFW3"<<std::endl;
            std::terminate();
        }

        glfwWindowHint(GLFW_STENCIL_BITS, 8);

        window = glfwCreateWindow(width, height, "Simple example", NULL, NULL);
        if (!window) {
            std::cerr<<"ERROR: could not create window with GLFW3"<<std::endl;
            glfwTerminate();
            std::terminate();
        }
        glfwMakeContextCurrent(window);

    }

    int SkiaWindow::pollWindow(SkCanvas* canvas, GrDirectContext* sContext) {
        while (true) {
            if (glfwWindowShouldClose(window)) {
                break;
            } else if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                break;
            }

            glfwWaitEvents();

            SkPaint paint;
            paint.setColor(SK_ColorWHITE);
            canvas->drawPaint(paint);
            paint.setColor(SK_ColorBLUE);
            canvas->drawRect({100, 200, 300, 500}, paint);
            sContext->flush();

            glfwSwapBuffers(window);
        }

        return 1;

    }

    GwPlot::GwPlot(std::string reference, std::vector<std::string>& bam_paths, Themes::IniOptions& opt, std::vector<Utils::Region>& regions) {

        this->reference = reference;
        this->bam_paths = bam_paths;
        this->regions = regions;
        this->opts = opt;
        this->redraw = true;
        this->processed = false;
        this->calcScaling = true;

        for (auto& fn: this->bam_paths) {
            htsFile* f = sam_open(fn.c_str(), "r");
            hts_set_threads(f, opt.threads);
            std::cout << opt.threads << std::endl;
            bams.push_back(f);
            sam_hdr_t *hdr_ptr = sam_hdr_read(f);
            headers.push_back(hdr_ptr);
            hts_idx_t* idx = sam_index_load(f, fn.c_str());
            indexes.push_back(idx);
        }

        this->fonts = Themes::Fonts();
        this->fai = fai_load(reference.c_str());

        samMaxY = 0;
        vScroll = 0;
        yScaling = 0;
    }

    GwPlot::~GwPlot() {
    }

    int GwPlot::startUI(SkCanvas* canvas, GrDirectContext* sContext) {

        this->opts.theme.setAlphas();

//        drawScreen(canvas, sContext);
        GLFWwindow * wind = this->window.window;

        while (true) {
            if (glfwWindowShouldClose(wind)) {
                break;
            } else if (glfwGetKey(wind, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
                break;
            }

            glfwWaitEvents();

            if (redraw) {
                drawScreen(canvas, sContext);
            }

//            break;

        }

        return 1;
    }

    void GwPlot::processBam(SkCanvas* canvas) {  // collect reads, calc coverage and find y positions on plot
        if (!processed) {
            if (opts.link_op != 0) {
                linked.clear();
                linked.resize(bams.size() * regions.size());
            }
            int idx = 0;
            collections.clear();
            collections.resize(bams.size() * regions.size());

            // with some work this could be run this in parallel for each region
            for (int i=0; i<bams.size(); ++i) {
                htsFile* b = bams[i];
                sam_hdr_t *hdr_ptr = headers[i];
                hts_idx_t *index = indexes[i];

                for (int j=0; j<regions.size(); ++j) {
                    Utils::Region *reg = &regions[j];
                    collections[idx].bamIdx = i;
                    collections[idx].regionIdx = j;
                    collections[idx].region = regions[j];
                    if (opts.coverage) {
                        collections[idx].covArr.resize(reg->end - reg->start, 0);
                    }
                    HTS::collectReadsAndCoverage(collections[idx], b, hdr_ptr, index,opts, reg, opts.coverage);

                    int maxY = Segs::findY(idx, collections[idx], vScroll, opts.link_op, opts, reg, linked, false);
                    if (maxY > samMaxY) {
                        samMaxY = maxY;
                    }
                    idx += 1;
                }
            }
        } else {
            Segs::dropOutOfScope(regions, collections, bams.size());
        }
    }

    void GwPlot::setScaling() {  // sets z_scaling, y_scaling trackY and regionWidth
        if (samMaxY == 0 || !calcScaling) {
            return;
        }
        glfwGetFramebufferSize(window.window, &fb_width, &fb_height);
        auto fbh = (float) fb_height;
        auto fbw = (float) fb_width;
        if (bams.empty()) {
            covY = 0; totalCovY = 0; totalTabixY = 0; tabixY = 0;
            return;
        }
        if (opts.coverage) {
            totalCovY = fbh * 0.1;
            covY = totalCovY / (float)bams.size();
        } else {
            totalCovY = 0; covY = 0;
        }
        totalTabixY = 0; tabixY = 0;  // todo add if bed track here
        trackY = (fbh - totalCovY - totalTabixY) / (float)bams.size();
        yScaling = (fbh - totalCovY - totalTabixY) / (float)samMaxY;

        fonts.setFontSize(yScaling);

        regionWidth = fbw / (float)regions.size();
        bamHeight = covY + trackY + tabixY;
        for (auto &cl: collections) {
            cl.xScaling = fbw / ((float)(cl.region.end - cl.region.start));
            cl.xOffset = regionWidth * cl.regionIdx;
            cl.yOffset = cl.bamIdx * bamHeight;
        }
    }

    void GwPlot::drawScreen(SkCanvas* canvas, GrDirectContext* sContext) {

        auto start = std::chrono::high_resolution_clock::now();

        canvas->drawPaint(opts.theme.bgPaint);
//        canvas->drawPaint(SK_ColorWHITE);
//        SkPaint paint;
//        paint.setColor(SK_ColorBLUE);
//        canvas->drawRect({100, 200, 300, 500}, paint);

        processBam(canvas);

        auto finish = std::chrono::high_resolution_clock::now();
        auto m = std::chrono::duration_cast<std::chrono::milliseconds >(finish - start);
        std::cout << "Elapsed Time processBm: " << m.count() << " m seconds" << std::endl;

        setScaling();
        Drawing::drawBams(opts, collections, canvas, yScaling, fonts, rect, path);

        sContext->flush();
        glfwSwapBuffers(window.window);

        redraw = false;

        finish = std::chrono::high_resolution_clock::now();
        m = std::chrono::duration_cast<std::chrono::milliseconds >(finish - start);
        std::cout << "Elapsed Time drawScreen: " << m.count() << " m seconds" << std::endl;


    }

}

