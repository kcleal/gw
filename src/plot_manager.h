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

#define SK_GL
#ifndef OLD_SKIA
    #include "include/gpu/ganesh/GrBackendSurface.h"
    #include "include/gpu/ganesh/gl/GrGLDirectContext.h"
    #include "include/gpu/ganesh/gl/GrGLInterface.h"
    #include "include/gpu/ganesh/gl/GrGLBackendSurface.h"
    #include "include/gpu/ganesh/SkSurfaceGanesh.h"
#else
    #include "include/gpu/GrBackendSurface.h"
    #include "include/gpu/GrDirectContext.h"
    #include "include/gpu/gl/GrGLInterface.h"
#endif
#include "include/encode/SkPngEncoder.h"
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
    class EXPORT GwPlot {
    public:
        GwPlot(std::string reference, std::vector<std::string> &bampaths, Themes::IniOptions &opts, std::vector<Utils::Region> &regions,
               std::vector<std::string> &track_paths);
        ~GwPlot();
        long frameId;  // number of frames rendered
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
        bool drawLocation;
        bool terminalOutput;  // recoverable runtime errors and output sent to terminal or outStr

        float totalCovY, covY, totalTabixY, tabixY, trackY, regionWidth, bamHeight, refSpace, sliderSpace;

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

        sk_sp<SkSurface> rasterSurface;
        sk_sp<SkSurface>* rasterSurfacePtr;  // option to use externally managed surface (faster)
        SkCanvas* rasterCanvas;

        Show mode;
        Show last_mode;

        std::string selectedAlign;

        // Initialisation functions
        void init(int width, int height);
        void initBack(int width, int height);
        void setGlfwFrameBufferSize();
        void setImageSize(int width, int height);
        int makeRasterSurface();

        // Data loading functions
        void loadGenome(std::string genome_tag_or_path, std::ostream& outerr);
        void addBam(std::string &bam_path);
        void removeBam(int index);
        void addTrack(std::string &path, bool print_message, bool vcf_as_track, bool bed_as_track);
        void removeTrack(int index);
        // addRegion missing todo
        void removeRegion(int index);
        void addVariantTrack(std::string &path, int startIndex, bool cacheStdin, bool useFullPath);
        void removeVariantTrack(int index);
        void addIdeogram(std::string path);
        bool loadIdeogramTag();
        // removeIdeogram missing todo
        void loadSession();

        // State functions
        void fetchRefSeq(Utils::Region &rgn);
        void fetchRefSeqs();
        void addFilter(std::string &filter_str);
        void setLabelChoices(std::vector<std::string> & labels);
        void setOutLabelFile(const std::string &path);
        void clearCollections();
        void processBam();
        void resetCollectionRegionPtrs();
        void setScaling();
        void setVariantSite(std::string &chrom, long start, std::string &chrom2, long stop);
        int startUI(GrDirectContext* sContext, SkSurface *sSurface, int delay, std::vector<std::string> &extra_commands);

        // Interactions
        void keyPress(int key, int scancode, int action, int mods);
        void mouseButton(int button, int action, int mods);
        void mousePos(double x, double y);
        void scrollGesture(double xoffset, double yoffset);
        void windowResize(int x, int y);
        void pathDrop(int count, const char** paths);
        bool commandProcessed();
        void prepareSelectedRegion();
        void addAlignmentToSelectedRegion();

        // Draw functions
        void drawScreen(bool force_buffered_reads=false);
        void drawScreenNoBuffer();
        void runDraw(bool force_buffered_reads=false);
        void runDrawOnCanvas(SkCanvas *canvas, bool force_buffered_reads=false);
        void runDrawNoBuffer();  // draws to canvas managed by GwPlot (slower)
        void runDrawNoBufferOnCanvas(SkCanvas* canvas);  // draws to external canvas (faster)
        void syncImageCacheQueue();
        bool collectionsNeedRedrawing();

        // Printing information functions
        void printIndexInfo();
        int printRegionInfo();
        void highlightQname();
        std::string flushLog();

        // Output functions
        sk_sp<SkImage> makeImage();
        void saveSession(std::string out_session);
        void rasterToPng(const char* path);
        void saveToPdf(const char* path, bool force_buffered_reads=false);
        void saveToSvg(const char* path, bool force_buffered_reads=false);
        std::vector<uint8_t>* encodeToPngVector(int compression_level);
        std::vector<uint8_t>* encodeToJpegVector(int quality);
        void saveLabels();

        // Get properties
        size_t sizeOfBams();
        size_t sizeOfRegions();

    private:

        bool resizeTriggered;
        bool regionSelectionTriggered;
        bool textFromSettings;

        std::chrono::high_resolution_clock::time_point resizeTimer, regionTimer;

        std::string cursorGenomePos;

        int target_pos;

        bool captureText, shiftPress, ctrlPress, processText;
        bool tabBorderPress;

        int commandIndex, charIndex;

        double pointSlop, textDrop, pH;

        double xDrag, xOri, lastX, yDrag, yOri, lastY;
        int windowW, windowH;  // Window width dn height

        double yScaling;

        uint32_t minGapSize;

        //Segs::Align cached_align;

        GLFWcursor* vCursor;
        GLFWcursor* normalCursor;

        Utils::Region clicked;
        int clickedIdx;
        int commandToolTipIndex;

        std::vector<Utils::BoundingBox> bboxes;

        BS::thread_pool pool;

        void drawOverlay(SkCanvas* canvas);

        void tileDrawingThread(SkCanvas* canvas, GrDirectContext* sContext, SkSurface *sSurface);

        void tileLoadingThread();

        void drawTiles(SkCanvas *canvas, GrDirectContext *sContext, SkSurface *sSurface);

        int registerKey(GLFWwindow* window, int key, int scancode, int action, int mods);

        void updateSettings();

        int getCollectionIdx(float x, float y);

        void updateSlider(float xPos);

        void drawCursorPosOnRefSlider(SkCanvas *canvas);

        // Helper methods for mouse interaction

        void calculateCursorCoordinates(int button, int action, float& xW, float& yW);
        bool handleToolButtons(int button, int action, float xW, float yW);
        void toggleSettingsMode();
        void toggleCommandCapture();
        bool handleCommandTooltipInteraction(int button, int action, float xW, float yW);
        void updateDragState();

        // Mode-specific handlers
        void handleSingleModeLeftClick(int button, int action, float xW, float yW);
        void handleSingleModeRightClick();
        void handleTiledModeRightClick(float xW, float yW);
        void handleTiledModeLeftClick(float xW, float yW);
        void handleSettingsModeClick();

        // Track-specific handlers
        bool handleTrackClick(int idx, int action, float xW, float yW);
        void printReferenceSequence(float xW);
        void printTrackInformation(int idx, float xW, float yW);

        // Region handlers
        void selectRegion(int idx);
        void handleReadSelection(int idx, float xW, float yW);
        void zoomToPosition(int pos);
        void selectReadAtPosition(Segs::ReadCollection &cl, int pos, float xW, float yW);
        void toggleReadHighlight(std::vector<Segs::Align>::iterator bnd, Segs::ReadCollection &cl, int pos);
        void handleRegionDragging();
        void updateRegionReads(bool lt_last);

        // Mode switching
        void switchToTiledMode();
        void switchToSingleMode();

        // UI helpers
        int findBoxIndex(float xW, float yW);
        void resetDragState();
        void resetTextCapture();

        // Tiled mode handlers
        void handleMultiRegionSelection(int boxIdx);
        void handleImageSelection(int boxIdx, float xW);
        void handleVariantFileSelection(float xW);
        void handleTiledModeScroll();
        void handleTiledModeBoxClick(float xW, float yW);

        // Mouse position handling functions

        void updateCursorGenomePos(float xOffset, float xScaling, float xPos_fb, Utils::Region* region, int bamIdx);


    };

    void imageToPng(sk_sp<SkImage> &img, std::filesystem::path &outdir);

    void imagePngToStdOut(sk_sp<SkImage> &img);

    void imagePngToFile(sk_sp<SkImage> &img, std::string path);

    void savePlotToPdfFile(Manager::GwPlot *plot, std::string path);

    struct VariantJob {
        std::string chrom;
        std::string chrom2;
        std::string rid;
        std::string varType;
        long start;
        long stop;
    };

    void drawImageCommands(Manager::GwPlot &p, SkCanvas *canvas, std::vector<std::string> &extra_commands);

}