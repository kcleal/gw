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
#include "htslib/vcf.h"
#include "htslib/sam.h"
#include "htslib/tbx.h"

#include <chrono>
#include <future>
#include <filesystem>
#include <GLFW/glfw3.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../include/unordered_dense.h"
#include "../include/BS_thread_pool.h"
#include "drawing.h"
#include "glfw_keys.h"
#include "hts_funcs.h"
#include "menu.h"
#include "parser.h"
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


namespace Manager {

    class CloseException : public std::exception {};

    typedef ankerl::unordered_dense::map< std::string, std::vector<int>> map_t;
    typedef std::vector< map_t > linked_t;

    enum Show {
        SINGLE,
        TILED,
        SETTINGS
    };

    class HiddenWindow {
    public:
        HiddenWindow () = default;
        ~HiddenWindow () = default;
        GLFWwindow *window;
        void init(int width, int height);
    };

    /*
     * Deals with managing all data and plotting
     */
    class GwPlot {
    public:
        GwPlot(std::string reference, std::vector<std::string> &bampaths, Themes::IniOptions &opts, std::vector<Utils::Region> &regions,
               std::vector<std::string> &track_paths);
        ~GwPlot();

        int fb_width, fb_height;
        float monitorScale, gap;
        int samMaxY;
        int regionSelection, variantFileSelection;
        bool drawToBackWindow;

        std::string reference;

        std::vector<std::string> bam_paths;
        std::vector<htsFile* > bams;
        std::vector<sam_hdr_t* > headers;
        std::vector<hts_idx_t* > indexes;

        std::vector<HGW::GwTrack> tracks;  // tracks that are plotted at the bottom of screen
        std::string outLabelFile;

        std::vector<Utils::Region> regions;

        std::vector<std::string> labelChoices;  // enumeration of labels to use

        std::vector<Segs::ReadCollection> collections;  // stores alignments

        std::vector<HGW::GwVariantTrack> variantTracks; // make image tiles from these

        std::vector<Parse::Parser> filters;

        ankerl::unordered_dense::map< int, sk_sp<SkImage>> imageCache;

        // keys are variantFilename and variantId
        ankerl::unordered_dense::map< std::string, ankerl::unordered_dense::map< std::string, Utils::Label>> inputLabels;
        ankerl::unordered_dense::map< std::string, ankerl::unordered_dense::set<std::string>> seenLabels;

        std::deque< std::pair<long, sk_sp<SkImage> > > imageCacheQueue;

        Themes::IniOptions opts;
        Themes::Fonts fonts;

        faidx_t* fai;
        GLFWwindow* window;
        GLFWwindow* backWindow;

        Show mode;
        Show last_mode;

        std::string selectedAlign;

        void init(int width, int height);

        void initBack(int width, int height);

        void setGlfwFrameBufferSize();

        void setRasterSize(int width, int height);

        void addVariantTrack(std::string &path, int startIndex, bool cacheStdin, bool useFullPath);

        void addFilter(std::string &filter_str);

        void setOutLabelFile(const std::string &path);

        void setLabelChoices(std::vector<std::string> & labels);

        void saveLabels();

        void fetchRefSeq(Utils::Region &rgn);

        void fetchRefSeqs();

        void clearCollections();

        void processBam();

        void setScaling();

        void setVariantSite(std::string &chrom, long start, std::string &chrom2, long stop);

//        void appendVariantSite(std::string &chrom, long start, std::string &chrom2, long stop, std::string & rid, std::string &label, std::string &vartype);

        int startUI(GrDirectContext* sContext, SkSurface *sSurface, int delay);

        void keyPress(GLFWwindow* window, int key, int scancode, int action, int mods);

        void mouseButton(GLFWwindow* wind, int button, int action, int mods);

        void mousePos(GLFWwindow* wind, double x, double y);

        void scrollGesture(GLFWwindow* wind, double xoffset, double yoffset);

        void windowResize(GLFWwindow* wind, int x, int y);

        void pathDrop(GLFWwindow* window, int count, const char** paths);

        void runDraw(SkCanvas *canvas);

        void runDrawNoBuffer(SkCanvas *canvas);

        sk_sp<SkImage> makeImage();

        void printIndexInfo();

        int printRegionInfo();


    private:
        long frameId;
        bool redraw;
        bool processed;
        bool drawLine;
        bool resizeTriggered;
        bool regionSelectionTriggered;
        bool textFromSettings;
        std::chrono::high_resolution_clock::time_point resizeTimer, regionTimer;

        std::string inputText;
        std::string target_qname;
        std::string cursorGenomePos;

        int target_pos;

        bool captureText, shiftPress, ctrlPress, processText;
        bool tabBorderPress;
        std::vector< std::string > commandHistory;
        int commandIndex, charIndex;
        int mouseOverTileIndex;

        float totalCovY, covY, totalTabixY, tabixY, trackY, regionWidth, bamHeight, refSpace, sliderSpace;

        double xDrag, xOri, lastX, yDrag, yOri, lastY;

        float yScaling;

        GLFWcursor* vCursor;
        GLFWcursor* normalCursor;

        Utils::Region clicked;
        int clickedIdx;
        int commandToolTipIndex;

        HGW::GwVariantTrack *currentVarTrack;

        std::vector<Utils::BoundingBox> bboxes;

        BS::thread_pool pool;

        void drawScreen(SkCanvas* canvas, GrDirectContext* sContext, SkSurface *sSurface);

        void drawScreenNoBuffer(SkCanvas* canvas, GrDirectContext* sContext, SkSurface *sSurface);

        void drawOverlay(SkCanvas* canvas, GrDirectContext* sContext, SkSurface *sSurface);

        void tileDrawingThread(SkCanvas* canvas, GrDirectContext* sContext, SkSurface *sSurface);

        void tileLoadingThread();

        void drawTiles(SkCanvas *canvas, GrDirectContext *sContext, SkSurface *sSurface);

        int registerKey(GLFWwindow* window, int key, int scancode, int action, int mods);

        bool commandProcessed();

        void updateSettings();

        int getCollectionIdx(float x, float y);

        void highlightQname();

        void updateCursorGenomePos(float xOffset, float xScaling, float xPos, Utils::Region *region, int bamIdx);
        //void updateCursorGenomePos(Segs::ReadCollection &cl, float xPos);

        void updateSlider(float xPos);

    };

    void imageToPng(sk_sp<SkImage> &img, std::filesystem::path &outdir);

    void imagePngToStdOut(sk_sp<SkImage> &img);

    void imagePngToFile(sk_sp<SkImage> &img, std::string path);

    void imageToPdf(sk_sp<SkImage> &img, std::filesystem::path &outdir);

    struct VariantJob {
        std::string chrom;
        std::string chrom2;
        std::string rid;
        std::string varType;
        long start;
        long stop;
    };

}