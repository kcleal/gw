//
// Created by Kez Cleal on 25/07/2022.
//

#pragma once

#include <iostream>

#include "htslib/faidx.h"
#include "htslib/hfile.h"
#include "htslib/hts.h"
#include "htslib/vcf.h"
#include "htslib/sam.h"
#include "htslib/tbx.h"

#include <chrono>
#include <future>
#include <filesystem>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ankerl_unordered_dense.h"
#include "BS_thread_pool.h"
#include "drawing.h"
#include "glfw_keys.h"
#include "hts_funcs.h"
#include "menu.h"
#include "parser.h"
#include "utils.h"
#include "segments.h"
#include "themes.h"
#include "export_definitions.h"

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

    enum SortType {
        NONE,
        STRAND,
        HP
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
    class EXPORT GwPlot {
    public:
        GwPlot(std::string reference, std::vector<std::string> &bampaths, Themes::IniOptions &opts, std::vector<Utils::Region> &regions,
               std::vector<std::string> &track_paths);
        ~GwPlot();

        int fb_width, fb_height;  // frame buffer size
        double xPos_fb, yPos_fb;  // mouse position in frame buffer coords
        float monitorScale, gap;
        int samMaxY;
        int regionSelection, variantFileSelection;
        bool drawToBackWindow;
        bool triggerClose;
        bool redraw;
        bool processed;
        bool drawLine;

        bool terminalOutput;  // recoverable runtime errors and output sent to terminal or outStr

        SortType sortReadsBy;

        std::ostringstream outStr;

        std::vector<char> pixelMemory;

        std::string reference;

        std::string ideogram_path;

        std::string inputText;

        std::string target_qname;

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

        std::unordered_map<std::string, std::vector<Ideo::Band>> ideogram;

        std::vector< std::string > commandHistory, commandsApplied;

        HGW::GwVariantTrack *currentVarTrack;  // var track with current focus/event
        int mouseOverTileIndex;  // tile with mouse over

        std::vector<Parse::Parser> filters;

        std::unordered_map< int, sk_sp<SkImage>> imageCache;  // Cace of tiled images
        std::deque< std::pair<long, sk_sp<SkImage> > > imageCacheQueue;  // cache of previously draw main screen images

        // keys are variantFilename and variantId
        ankerl::unordered_dense::map< std::string, ankerl::unordered_dense::map< std::string, Utils::Label>> inputLabels;
        ankerl::unordered_dense::map< std::string, ankerl::unordered_dense::set<std::string>> seenLabels;

        Themes::IniOptions opts;
        Themes::Fonts fonts;

        faidx_t* fai;
        GLFWwindow* window;
        GLFWwindow* backWindow;

        sk_sp<SkSurface> rasterSurface;
        sk_sp<SkSurface>* rasterSurfacePtr;  // option to use externally managed surface (faster)
        SkCanvas* rasterCanvas;

        Show mode;
        Show last_mode;

        std::string selectedAlign;

        void init(int width, int height);

        void initBack(int width, int height);

        void setGlfwFrameBufferSize();

        void setRasterSize(int width, int height);

        int makeRasterSurface();

        void rasterToPng(const char* path);

        void loadGenome(std::string genome_tag_or_path, std::ostream& outerr);

        void addBam(std::string &bam_path);

        void removeBam(int index);

        void addTrack(std::string &path, bool print_message, bool vcf_as_track, bool bed_as_track);

        void removeTrack(int index);

        void removeVariantTrack(int index);

        void removeRegion(int index);

        void addVariantTrack(std::string &path, int startIndex, bool cacheStdin, bool useFullPath);

        void addIdeogram(std::string path);

        bool loadIdeogramTag();

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

        void loadSession();

        int startUI(int delay);

        void keyPress(int key, int scancode, int action, int mods);

        void mouseButton(int button, int action, int mods);

        void mousePos(double x, double y);

        void scrollGesture(double xoffset, double yoffset);

        void windowResize(int x, int y);

        void pathDrop(int count, const char** paths);

        void runDraw();

        void runDrawOnCanvas(SkCanvas *canvas);

        void runDrawNoBuffer();  // draws to canvas managed by GwPlot (slower)

        void runDrawNoBufferOnCanvas(SkCanvas* canvas);  // draws to external canvas (faster)

        sk_sp<SkImage> makeImage();

        void printIndexInfo();

        int printRegionInfo();

        bool commandProcessed();

        void highlightQname();

        void saveSession(std::string out_session);

    private:
        GLuint vertexShader;
        GLuint fragmentShader;
        GLuint shaderProgram;
        GLuint texture;
        SkPixmap pixmap;
        GLuint VBO, EBO;

        long frameId;
        bool resizeTriggered;
        bool regionSelectionTriggered;
        bool textFromSettings;

        std::chrono::high_resolution_clock::time_point resizeTimer, regionTimer;

        std::string cursorGenomePos;

        int target_pos;

        bool captureText, shiftPress, ctrlPress, processText;
        bool tabBorderPress;

        int commandIndex, charIndex;

        float totalCovY, covY, totalTabixY, tabixY, trackY, regionWidth, bamHeight, refSpace, sliderSpace;

        double pointSlop, textDrop, pH;

        double xDrag, xOri, lastX, yDrag, yOri, lastY;

        double yScaling;

        uint32_t minGapSize;

//        std::vector<std::vector<char>> extraPixelArrays;  // one for each thread

        GLFWcursor* vCursor;
        GLFWcursor* normalCursor;

        Utils::Region clicked;
        int clickedIdx;
        int commandToolTipIndex;

        std::vector<Utils::BoundingBox> bboxes;

        BS::thread_pool pool;

        void drawScreen();

        void drawOverlay(SkCanvas* canvas);

        void tileDrawingThread(SkCanvas* canvas, sk_sp<SkSurface> *surfacePtr);

        void tileLoadingThread();

        void drawTiles(SkCanvas *canvas);

        int registerKey(GLFWwindow* window, int key, int scancode, int action, int mods);

        void updateSettings();

        int getCollectionIdx(float x, float y);

        void updateCursorGenomePos(float xOffset, float xScaling, float xPos, Utils::Region *region, int bamIdx);

        void updateSlider(float xPos);

        void drawCursorPosOnRefSlider(SkCanvas *canvas);

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