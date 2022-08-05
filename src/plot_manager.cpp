
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

        this->fai = fai_load(reference.c_str());

        vScroll = 0;
    }

    GwPlot::~GwPlot() {
    }

    int GwPlot::plotToScreen(SkCanvas* canvas, GrDirectContext* sContext) {

        this->opts.theme.setAlphas();

        drawScreen(canvas, sContext);
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

            break;

        }

        return 1;
    }

    void GwPlot::setYspace() {
        if (bams.empty()) {
            covY = 0; totalCovY = 0; totalTabixY = 0; tabixY = 0;
            return;
        }
        if (opts.coverage) {
            totalCovY = opts.canvas_height * 0.1;
            covY = totalCovY / bams.size();
        } else {
            totalCovY = 0; covY = 0;
        }
        totalTabixY = 0; tabixY = 0;
        // todo add if bed track here
        trackY = (opts.canvas_height - totalCovY - totalTabixY) / bams.size();
    }

    void GwPlot::drawScreen(SkCanvas* canvas, GrDirectContext* sContext) {

        glfwGetFramebufferSize(window.window, &opts.canvas_width, &opts.canvas_height);

        setYspace();

        canvas->drawPaint(opts.theme.bgPaint);


        SkPaint paint;
        paint.setColor(SK_ColorBLUE);
        canvas->drawRect({100, 200, 300, 500}, paint);

        process_sam(canvas);

        sContext->flush();
        glfwSwapBuffers(window.window);

        redraw = false;


    }

    void GwPlot::process_sam(SkCanvas* canvas) {
        if (!processed) {

            if (opts.link_op != 0) {
                linked.clear();
                linked.resize(bams.size() * regions.size());
            }

            int maxy;
            int idx = 0;

            for (int i=0; i<bams.size(); ++i) {

                htsFile* b = bams[i];
                sam_hdr_t *hdr_ptr = headers[i];
                hts_idx_t *index = indexes[i];

                for (int j=0; j<regions.size(); ++j) {

                    Utils::Region *reg = &regions[j];
                    Segs::ReadCollection rc = Segs::ReadCollection();
                    if (opts.coverage) {
                        rc.covArr.resize(reg->end - reg->start, 0);
                    }

                    HTS::collectReadsAndCoverage(rc, b, hdr_ptr, index,opts, reg, opts.coverage);

                    Segs::findY(idx, rc, vScroll, opts.link_op, opts, reg, linked, false);

                    idx += 1;
                }
            }



        }

    }



}

