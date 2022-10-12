//
// Created by Kez Cleal on 25/07/2022.
//

#pragma once

#include <iostream>
#ifdef __APPLE__
    #include <OpenGL/gl.h>
#endif

#include "htslib/faidx.h"
#include "htslib/hfile.h"
#include "htslib/hts.h"
#include "htslib/sam.h"
#include "htslib/tbx.h"

#include <chrono>
#include <GLFW/glfw3.h>
#include <string>
#include <utility>
#include <vector>

#include "../inc/BS_thread_pool.h"
#include "drawing.h"
#include "glfw_keys.h"
#include "hts_funcs.h"
#include "../inc/robin_hood.h"
#include "utils.h"
#include "segments.h"
#include "themes.h"

#define SK_GL
#include "include/gpu/GrBackendSurface.h"
#include "include/gpu/GrDirectContext.h"
#include "include/gpu/gl/GrGLInterface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkSurface.h"


namespace HTS {
    void collectReadsAndCoverage(Segs::ReadCollection &col, htsFile *bam, sam_hdr_t *hdr_ptr,
                                 hts_idx_t *index, Themes::IniOptions &opts, Utils::Region *region, bool coverage);

    void trimToRegion(Segs::ReadCollection &col, bool coverage);

    void appendReadsAndCoverage(Segs::ReadCollection &col, htsFile *bam, sam_hdr_t *hdr_ptr,
                                 hts_idx_t *index, Themes::IniOptions &opts, bool coverage, bool left, int *vScroll, Segs::linked_t &linked, int *samMaxY);

    class VCF {
    public:
        VCF () = default;
        ~VCF();
        htsFile *fp;
        const bcf_hdr_t *hdr;
        bcf1_t *v;
        std::string path;
        std::string chrom, chrom2, rid, vartype, label, tag;
        int parse;
        int info_field_type;
        const char *label_to_parse;
        long start, stop;
        bool done;

        void open(std::string f);
        void next();
    };

    class Tab2Bam {
    public:
        Tab2Bam() = default;
        ~Tab2Bam();
        htsFile *fp;
        tbx_t *idx;
        hts_itr_t * itr;
        void open(std::string f);
        void fetch(std::string chrom, int start, int end, Segs::ReadCollection &cl);
    };
}


namespace Manager {

    class CloseException : public std::exception {};

    typedef ankerl::unordered_dense::map< std::string, std::vector<int>> map_t;
//    typedef robin_hood::unordered_flat_map< const char *, std::vector<int>> map_t;
    typedef std::vector< map_t > linked_t;

    enum Show {
        SINGLE,
        TILED
    };

    class HiddenWindow {
    public:
        HiddenWindow () = default;
        ~HiddenWindow () = default;
        GLFWwindow *window;
        void init(int width, int height);
    };

    /*
     * Deals with managing genomic data
     */
    class GwPlot {
    public:
        GwPlot(std::string reference, std::vector<std::string> &bampaths, Themes::IniOptions &opts, std::vector<Utils::Region> &regions);
        ~GwPlot();

        int vScroll;
        int fb_width, fb_height;
        bool drawToBackWindow;

        std::string reference;

        std::vector<std::string> bam_paths;
        std::vector<htsFile* > bams;
        std::vector<sam_hdr_t* > headers;
        std::vector<hts_idx_t* > indexes;
        std::vector<Utils::Region> regions;
        std::vector<std::vector<Utils::Region>> multiRegions;  // used for creating tiled regions

        std::vector<std::string> labelChoices;
        std::vector<Utils::Label> multiLabels;  // used for labelling tiles

        std::vector<Segs::ReadCollection> collections;

        HTS::VCF vcf;

        robin_hood::unordered_flat_map< int, sk_sp<SkImage>> imageCache;
//        ankerl::unordered_dense::map< int, sk_sp<SkImage>> imageCache;

        Themes::IniOptions opts;
        Themes::Fonts fonts;

        faidx_t* fai;
        GLFWwindow* window;
        GLFWwindow* backWindow;

        Show mode;

        void init(int width, int height);

        void initBack(int width, int height);

        void setGlfwFrameBufferSize();

        void setVariantFile(const std::string &path, int startIndex);

        void setLabelChoices(std::vector<std::string> & labels);

        void fetchRefSeq(Utils::Region &rgn);

        void fetchRefSeqs();

        void clearCollections();

        void processBam();

        void setScaling();

        void setVariantSite(std::string &chrom, long start, std::string &chrom2, long stop);

        void appendVariantSite(std::string &chrom, long start, std::string &chrom2, long stop, std::string & rid, std::string &label);

        int startUI(GrDirectContext* sContext, SkSurface *sSurface);

        void keyPress(GLFWwindow* window, int key, int scancode, int action, int mods);

        void mouseButton(GLFWwindow* wind, int button, int action, int mods);

        void mousePos(GLFWwindow* wind, double x, double y);

        void scrollGesture(GLFWwindow* wind, double xoffset, double yoffset);

        void windowResize(GLFWwindow* wind, int x, int y);

        void pathDrop(GLFWwindow* window, int count, const char** paths);

        void drawSurfaceGpu(SkCanvas *canvas);

        void runDraw(SkCanvas *canvas);

        sk_sp<SkImage> makeImage();

        void printRegionInfo();


    private:

        bool redraw;
        bool processed;
        bool calcScaling;

        bool resizeTriggered;
        std::chrono::high_resolution_clock::time_point resizeTimer;

        std::string inputText;
        std::string target_qname;

        bool captureText, shiftPress, ctrlPress, processText;
        std::vector< std::string > commandHistory;
        int commandIndex;

        float totalCovY, covY, totalTabixY, tabixY, trackY, regionWidth, bamHeight, refSpace;

        double xDrag, xOri, lastX;

        int samMaxY;

        float yScaling;

        linked_t linked;

        int blockStart, blockLen, regionSelection;

        Utils::Region clicked;
        int clickedIdx;

        void drawScreen(SkCanvas* canvas, GrDirectContext* sContext);

        void tileDrawingThread(SkCanvas* canvas, GrDirectContext* sContext, SkSurface *sSurface);

        void drawTiles(SkCanvas* canvas, GrDirectContext* sContext, SkSurface *sSurface);

        bool registerKey(GLFWwindow* window, int key, int scancode, int action, int mods);

        bool commandProcessed();

        int getCollectionIdx(float x, float y);

        void highlightQname();

    };

    void imageToPng(sk_sp<SkImage> &img, std::string &outdir);

    struct VariantJob {
        std::string chrom;
        std::string chrom2;
        long start;
        long stop;
    };

}