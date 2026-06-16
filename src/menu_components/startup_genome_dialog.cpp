#include "menu_components/startup_genome_dialog.h"
#include "menu_components/online_genome_utils.h"
#include "gw_version.h"

#if !defined(__EMSCRIPTEN__)

#include "themes.h"
#include "utils.h"
#include "glob_cpp.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <clocale>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <ncurses.h>
#include <curl/curl.h>

// Some ncurses builds (e.g. macOS conda) do not define BUTTON5_PRESSED.
#ifndef BUTTON5_PRESSED
#define BUTTON5_PRESSED 0x00002000L
#endif

namespace Menu {

namespace {

struct GenomeEntry {
    std::string tag;
    std::string path;
    std::string name;      // display name; for IGV entries this is the human-readable name
    std::string location;  // parent directory (local) or host (online)
    std::string filename;  // basename of local path or last component of URL
    bool isOnline;
};

enum class Section { Local, Online, Session };

class StartupDialog;

// Global pointer used by the SIGINT handler to restore the terminal.
static StartupDialog* g_activeDialog = nullptr;



static size_t writeToFile(void* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::ofstream*>(userdata);
    if (!out) return 0;
    out->write(static_cast<const char*>(ptr), static_cast<std::streamsize>(size * nmemb));
    return size * nmemb;
}

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

static bool isOnlinePath(const std::string& path) {
    return Utils::startsWith(path, "http://") ||
           Utils::startsWith(path, "https://") ||
           Utils::startsWith(path, "ftp://");
}

static std::string pathLocation(const std::string& path) {
    if (isOnlinePath(path)) {
        size_t start = path.find("://");
        if (start == std::string::npos) return "online";
        start += 3;
        size_t end = path.find('/', start);
        if (end == std::string::npos) return path.substr(start);
        return path.substr(start, end - start);
    }
    std::filesystem::path p(path);
    return p.parent_path().string();
}

static std::string pathFilename(const std::string& path) {
    if (isOnlinePath(path)) {
        size_t start = path.rfind('/');
        if (start == std::string::npos || start == path.size() - 1) return path;
        return path.substr(start + 1);
    }
    return std::filesystem::path(path).filename().string();
}

static std::string expandTilde(const std::string& path) {
    if (!path.empty() && path[0] == '~') {
        try {
            auto matches = glob_cpp::glob(path);
            if (!matches.empty()) return matches[0].string();
        } catch (...) {}
    }
    return path;
}

static std::string longestCommonPrefix(const std::vector<std::filesystem::path>& paths) {
    if (paths.empty()) return "";
    std::string common = paths[0].string();
    for (size_t i = 1; i < paths.size(); ++i) {
        std::string s = paths[i].string();
        size_t j = 0;
        while (j < common.size() && j < s.size() && common[j] == s[j]) ++j;
        common.resize(j);
    }
    return common;
}

static std::string formatBytes(curl_off_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = static_cast<double>(bytes);
    while (size >= 1024.0 && unit < 4) {
        size /= 1024.0;
        ++unit;
    }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f %s", size, units[unit]);
    return std::string(buf);
}

static constexpr char VBLOCK = ' ';

class StartupDialog {
public:
    explicit StartupDialog(Themes::IniOptions& opts) : iopts(opts) {}

    StartupChoice run() {
        if (!initTui()) {
            return {StartupResult::Error, {}, {}};
        }
        g_activeDialog = this;
        std::signal(SIGINT, signalHandler);

        loadGenomes();
        startIgvFetch();

        StartupChoice result = eventLoop();

        cleanupTui();
        std::signal(SIGINT, SIG_DFL);
        g_activeDialog = nullptr;
        return result;
    }

private:
    Themes::IniOptions& iopts;

    std::vector<GenomeEntry> localEntries;
    std::vector<GenomeEntry> onlineEntries;

    struct IgvFetchState {
        std::mutex mutex;
        std::vector<GenomeEntry> onlineEntries;
        std::atomic<bool> loading{false};
        std::atomic<bool> done{false};
        std::string message;
    };
    std::shared_ptr<IgvFetchState> igvState;

    Section section = Section::Local;
    std::string inputBuf;
    std::string message;
    bool needRedraw = true;

    int selected = 0;
    int scrollOffset = 0;

    WINDOW* win = nullptr;
    bool hasColors = false;
    static constexpr int MIN_W = 95;
    static constexpr int MIN_H = 18;

    int mouseX = -1;
    int mouseY = -1;

    bool draggingScroll = false;
    bool mouseEnabled = true;

    enum class HintAction { Enter, Download, Resume, Mouse, Quit };
    struct HintButton {
        int x = 0;
        int y = 0;
        int width = 0;
        HintAction action = HintAction::Enter;
    };
    std::vector<HintButton> hintButtons;

    void loadGenomes() {
        localEntries.clear();
        onlineEntries.clear();
        for (const auto& kv : iopts.myIni["genomes"]) {
            GenomeEntry e;
            e.tag = kv.first;
            e.path = kv.second;
            e.name = kv.first;
            e.location = pathLocation(e.path);
            e.filename = pathFilename(e.path);
            e.isOnline = isOnlinePath(e.path);
            if (e.isOnline) {
                onlineEntries.push_back(std::move(e));
            } else {
                localEntries.push_back(std::move(e));
            }
        }
        auto sortByLocation = [](const GenomeEntry& a, const GenomeEntry& b) {
            if (a.location != b.location) return a.location < b.location;
            return a.tag < b.tag;
        };
        std::sort(localEntries.begin(), localEntries.end(), sortByLocation);
        std::sort(onlineEntries.begin(), onlineEntries.end(), sortByLocation);
        // Start in the Online section if there are no local genomes.
        if (localEntries.empty() && !onlineEntries.empty()) {
            section = Section::Online;
        }
    }

    void startIgvFetch() {
        igvState = std::make_shared<IgvFetchState>();
        igvState->loading = true;
        igvState->onlineEntries = onlineEntries; // snapshot for merging
        message = "Loading online genomes...";

        std::thread([state = igvState]() {
            std::string error;
            std::vector<GenomeEntry> fetched;
            try {
                std::string json = Utils::fetchOnlineFileContent(
                    "https://s3.amazonaws.com/igv.org.genomes/genomes.json");
                for (const auto& g : parseIgvGenomes(json)) {
                    GenomeEntry e;
                    e.tag = g.id;
                    e.path = g.fastaURL;
                    e.name = g.name.empty() ? g.id : g.name;
                    e.location = pathLocation(e.path);
                    e.filename = pathFilename(e.path);
                    e.isOnline = true;
                    fetched.push_back(std::move(e));
                }
            } catch (const std::exception& e) {
                error = std::string("IGV fetch failed: ") + e.what();
            }

            std::lock_guard<std::mutex> lock(state->mutex);
            if (!error.empty()) {
                state->message = error;
            } else {
                // Merge IGV entries, preferring user ini entries on tag collision.
                for (const auto& e : fetched) {
                    bool exists = false;
                    for (const auto& o : state->onlineEntries) {
                        if (o.tag == e.tag) { exists = true; break; }
                    }
                    if (!exists) state->onlineEntries.push_back(e);
                }
                std::sort(state->onlineEntries.begin(), state->onlineEntries.end(),
                          [](const GenomeEntry& a, const GenomeEntry& b) { return a.tag < b.tag; });
            }
            state->loading = false;
            state->done = true;
        }).detach();
    }

    static void signalHandler(int) {
        if (g_activeDialog) {
            g_activeDialog->cleanupTui();
        }
        std::exit(0);
    }

    bool initTui() {
        std::setlocale(LC_ALL, "");
        initscr();
        if (!stdscr) return false;
        if (LINES < MIN_H || COLS < MIN_W) {
            endwin();
            return false;
        }
        cbreak();
        noecho();
        curs_set(0);
        hasColors = has_colors();
        if (hasColors) {
            start_color();
            use_default_colors();
            init_pair(1, COLOR_BLACK, COLOR_WHITE);   // highlighted row
            init_pair(2, COLOR_GREEN, -1);            // active tab / hints
            init_pair(3, COLOR_YELLOW, -1);           // title
            init_pair(4, COLOR_RED, -1);              // error message
            init_pair(5, COLOR_WHITE, -1);            // divider (with A_DIM for grey)
            init_pair(6, COLOR_CYAN, -1);             // bold hint keywords
        }
        if (!createOrResizeWindow()) return false;
        applyMouseState();
        return true;
    }

    bool createOrResizeWindow() {
        int h = LINES;
        int w = COLS;
        if (h < MIN_H || w < MIN_W) return false;
        int startY = 0;
        int startX = 0;
        if (win) {
            wresize(win, h, w);
            mvwin(win, startY, startX);
        } else {
            win = newwin(h, w, startY, startX);
            if (!win) {
                endwin();
                return false;
            }
            keypad(win, TRUE);
            nodelay(win, FALSE);
            wtimeout(win, 20);
        }
        return true;
    }

    void cleanupTui() {
        if (win) {
            wclear(win);
            wrefresh(win);
            delwin(win);
            win = nullptr;
        }
        // Disable xterm any-event mouse tracking before restoring the terminal.
        std::cout << "\033[?1003l" << std::flush;
        endwin();
        // Reset attributes and cursor, then move to a fresh line so GW's later
        // terminal output is not affected by ncurses cursor/attribute state.
        std::cout << "\033[0m\033[?25h\r\n" << std::flush;
    }

    void enableMouseTracking() {
        mousemask(ALL_MOUSE_EVENTS | REPORT_MOUSE_POSITION, nullptr);
        mouseinterval(0);
        std::cout << "\033[?1003h" << std::flush;
    }

    void disableMouseTracking() {
        mousemask(0, nullptr);
        std::cout << "\033[?1003l" << std::flush;
    }

    void applyMouseState() {
        if (mouseEnabled) {
            enableMouseTracking();
        } else {
            disableMouseTracking();
        }
    }

    // Temporarily disables mouse tracking in a modal dialog so the user can
    // select text with the terminal's normal mouse selection. Restores the
    // previous mouse state on destruction.
    struct MouseSuspender {
        StartupDialog* dialog;
        bool wasEnabled;
        explicit MouseSuspender(StartupDialog* d) : dialog(d), wasEnabled(d->mouseEnabled) {
            dialog->disableMouseTracking();
        }
        ~MouseSuspender() {
            if (wasEnabled) {
                dialog->enableMouseTracking();
            } else {
                dialog->disableMouseTracking();
            }
        }
    };

    std::vector<GenomeEntry> filteredEntries() const {
        if (section == Section::Session) {
            return {};
        }
        const std::vector<GenomeEntry>& source = (section == Section::Local) ? localEntries : onlineEntries;
        std::vector<GenomeEntry> out;
        std::string filter = toLower(inputBuf);
        for (const auto& e : source) {
            if (filter.empty() ||
                toLower(e.tag).find(filter) != std::string::npos ||
                toLower(e.name).find(filter) != std::string::npos) {
                out.push_back(e);
            }
        }
        return out;
    }

    std::vector<std::string> sessionInfoLines() const {
        std::vector<std::string> lines;
        if (iopts.session_file.empty() || !std::filesystem::exists(iopts.session_file)) {
            lines.push_back("No previous session");
            return lines;
        }
        try {
            mINI::INIFile file(iopts.session_file);
            mINI::INIStructure sesh;
            if (!file.read(sesh)) {
                lines.push_back("Could not read session file");
                return lines;
            }
            std::string tag = sesh["data"]["genome_tag"];
            std::string path = sesh["data"]["genome_path"];
            if (!tag.empty()) lines.push_back("Genome: " + tag);
            if (!path.empty()) lines.push_back("Path: " + path);

            if (sesh.has("data")) {
                std::vector<std::string> bams;
                for (const auto& kv : sesh["data"]) {
                    if (kv.first.size() >= 3 && kv.first.substr(0, 3) == "bam") {
                        bams.push_back(kv.second);
                    }
                }
                if (!bams.empty()) {
                    lines.push_back("BAM files:");
                    for (const auto& b : bams) lines.push_back("  " + b);
                }
            }

            if (sesh.has("variants")) {
                std::vector<std::string> variants;
                for (const auto& kv : sesh["variants"]) {
                    if (kv.first.size() >= 7 && kv.first.substr(0, 7) == "variant") {
                        variants.push_back(kv.second);
                    }
                }
                if (!variants.empty()) {
                    lines.push_back("Variant files:");
                    for (const auto& v : variants) lines.push_back("  " + v);
                }
            }

            if (sesh.has("tracks")) {
                std::vector<std::string> tracks;
                for (const auto& kv : sesh["tracks"]) {
                    if (kv.first.size() >= 5 && kv.first.substr(0, 5) == "track") {
                        tracks.push_back(kv.second);
                    }
                }
                if (!tracks.empty()) {
                    lines.push_back("Track files:");
                    for (const auto& t : tracks) lines.push_back("  " + t);
                }
            }

            if (sesh.has("show")) {
                std::string regions;
                for (const auto& kv : sesh["show"]) {
                    if (kv.first.size() >= 6 && kv.first.substr(0, 6) == "region") {
                        if (!regions.empty()) regions += ", ";
                        regions += kv.second;
                    }
                }
                if (!regions.empty()) lines.push_back("Regions: " + regions);
            }
        } catch (...) {
            lines.push_back("Error reading session");
        }
        return lines;
    }

    int listHeight() const {
        if (!win) return 0;
        int h = getmaxy(win);
        // title + top divider + empty + tabs + empty + context + empty +
        // header + header line + path(2) + path divider + input + message + hints
        int nonList = 15;
        if (section == Section::Session) {
            nonList = 14; // no header/divider below context
        }
        return std::max(1, h - nonList);
    }

    void clampSelection(int count) {
        if (count == 0) {
            selected = 0;
            scrollOffset = 0;
            return;
        }
        if (selected < 0) selected = 0;
        if (selected >= count) selected = count - 1;
        int lh = listHeight();
        if (selected < scrollOffset) scrollOffset = selected;
        if (selected >= scrollOffset + lh) scrollOffset = selected - lh + 1;
        if (scrollOffset < 0) scrollOffset = 0;
        if (scrollOffset > count - lh) scrollOffset = count - lh;
        if (scrollOffset < 0) scrollOffset = 0;
    }

    bool applyIgvUpdate() {
        if (!igvState) return false;
        std::lock_guard<std::mutex> lock(igvState->mutex);
        if (!igvState->done) return false;
        onlineEntries = std::move(igvState->onlineEntries);
        message = std::move(igvState->message);
        igvState.reset();
        selected = 0;
        scrollOffset = 0;
        if (localEntries.empty() && !onlineEntries.empty()) {
            section = Section::Online;
        }
        return true;
    }

    std::optional<Section> hitTestTab(int mx, int my) const {
        if (my != 3) return std::nullopt;
        constexpr int tableX = 2;
        if (mx >= tableX && mx <= tableX + 6) return Section::Local;
        if (mx >= tableX + 9 && mx <= tableX + 16) return Section::Online;
        if (mx >= tableX + 20 && mx <= tableX + 28) return Section::Session;
        return std::nullopt;
    }

    int tableWidth() const {
        if (!win) return 0;
        int w = getmaxx(win);
        const int scrollBarWidth = 2;
        int tagWidth = std::max(8, (w - 8 - scrollBarWidth) / 4);
        int fileWidth = std::max(10, w - 8 - scrollBarWidth - tagWidth);
        return tagWidth + fileWidth + 4;
    }

    int hitTestListRow(int mx, int my, int lh) const {
        if (section == Section::Session || lh <= 0) return -1;
        constexpr int tableX = 2;
        constexpr int listStartY = 9;
        int tw = tableWidth();
        if (my < listStartY || my >= listStartY + lh) return -1;
        if (mx < tableX || (tw > 0 && mx >= tableX + tw)) return -1;
        return scrollOffset + (my - listStartY);
    }

    bool hitTestSessionResume(int mx, int my, int lh) const {
        if (section != Section::Session || lh <= 0) return false;
        constexpr int tableX = 2;
        int tw = tableWidth();
        bool inTable = (tw <= 0) || (mx >= tableX && mx < tableX + tw);
        // Click on the "[Resume previous session]" context line.
        if (my == 5 && inTable) return true;
        // Click anywhere in the session info area.
        if (my >= 7 && my < 7 + lh && inTable) return true;
        return false;
    }

    struct ScrollBarMetrics {
        int scrollX = 0;
        int listStartY = 9;
        int trackHeight = 0;
        int thumbSize = 0;
        int maxThumbPos = 0;
    };

    ScrollBarMetrics scrollBarMetrics(int lh, int totalItems) const {
        ScrollBarMetrics m;
        if (section == Section::Session || lh <= 0 || !win) return m;
        int w = getmaxx(win);
        const int scrollBarWidth = 2;
        int tagWidth = std::max(8, (w - 8 - scrollBarWidth) / 4);
        int fileWidth = std::max(10, w - 8 - scrollBarWidth - tagWidth);
        int tableWidth = tagWidth + fileWidth + 4;
        m.scrollX = 2 + tableWidth + 1;
        m.trackHeight = lh;
        if (totalItems > lh) {
            m.thumbSize = std::max(1, lh * lh / totalItems);
            m.maxThumbPos = lh - m.thumbSize;
        }
        return m;
    }

    bool hitTestScrollBar(int mx, int my, int lh, int totalItems, int& outTrackY) const {
        if (section == Section::Session || lh <= 0) return false;
        auto m = scrollBarMetrics(lh, totalItems);
        if (m.scrollX == 0) return false;
        if (mx < m.scrollX || mx > m.scrollX + 1) return false;
        if (my < m.listStartY || my >= m.listStartY + lh) return false;
        outTrackY = my - m.listStartY;
        return true;
    }

    void updateScrollFromTrackY(int trackY, int lh, int totalItems) {
        auto m = scrollBarMetrics(lh, totalItems);
        if (m.trackHeight <= 0 || totalItems <= lh) return;
        int clampedY = std::max(0, std::min(trackY, m.maxThumbPos));
        scrollOffset = clampedY * (totalItems - lh) / m.maxThumbPos;
        selected = std::clamp(selected, scrollOffset, scrollOffset + lh - 1);
        needRedraw = true;
    }

    std::optional<StartupChoice> handleMouse(int mx, int my, mmask_t bstate,
                                             const std::vector<GenomeEntry>& visible) {
        if (mx != mouseX || my != mouseY) {
            needRedraw = true;
        }
        mouseX = mx;
        mouseY = my;

        bool pressed = (bstate & BUTTON1_PRESSED) != 0;
        bool released = (bstate & BUTTON1_RELEASED) != 0 ||
                        (bstate & BUTTON1_CLICKED) != 0 ||
                        (bstate & BUTTON1_DOUBLE_CLICKED) != 0;
        bool scrollUp = (bstate & BUTTON4_PRESSED) != 0;
        bool scrollDown = (bstate & BUTTON5_PRESSED) != 0;

        if (scrollUp || scrollDown) {
            selected += scrollUp ? -1 : 1;
            needRedraw = true;
            return std::nullopt;
        }

        const int lh = listHeight();
        const int totalItems = static_cast<int>(visible.size());

        // Active scrollbar drag: update scroll position from the pointer.
        if (draggingScroll) {
            updateScrollFromTrackY(my - 9, lh, totalItems);
            if (released) {
                draggingScroll = false;
            }
            return std::nullopt;
        }

        // Start a scrollbar drag on press.
        if (pressed) {
            int trackY = 0;
            if (hitTestScrollBar(mx, my, lh, totalItems, trackY)) {
                draggingScroll = true;
                updateScrollFromTrackY(trackY, lh, totalItems);
                return std::nullopt;
            }
        }

        if (released) {
            auto tab = hitTestTab(mx, my);
            if (tab.has_value() && tab.value() != section) {
                section = tab.value();
                inputBuf.clear();
                selected = 0;
                scrollOffset = 0;
                needRedraw = true;
                return std::nullopt;
            }

            if (section == Section::Session) {
                if (hitTestSessionResume(mx, my, lh)) {
                    StartupChoice result = resumeSessionChoice();
                    if (result.result != StartupResult::Error) {
                        return result;
                    }
                    needRedraw = true;
                }
                return std::nullopt;
            }

            int idx = hitTestListRow(mx, my, lh);
            if (idx >= 0 && idx < totalItems) {
                selected = idx;
                needRedraw = true;
                return StartupChoice{StartupResult::Genome, visible[idx].path, visible[idx].tag};
            }

            // Hint bar buttons.
            for (const auto& b : hintButtons) {
                if (my == b.y && mx >= b.x && mx < b.x + b.width) {
                    auto result = runHintAction(b.action, visible);
                    if (result.has_value()) {
                        return result.value();
                    }
                    return std::nullopt;
                }
            }

            return std::nullopt;
        }

        // Press-to-highlight: give button feedback on terminals that do not
        // report mouse movement.
        if (pressed) {
            if (hitTestTab(mx, my).has_value()) {
                needRedraw = true;
            } else if (section == Section::Session && hitTestSessionResume(mx, my, lh)) {
                needRedraw = true;
            } else {
                for (const auto& b : hintButtons) {
                    if (my == b.y && mx >= b.x && mx < b.x + b.width) {
                        needRedraw = true;
                        break;
                    }
                }
            }
        }

        if (pressed && section != Section::Session) {
            int idx = hitTestListRow(mx, my, lh);
            if (idx >= 0 && idx < totalItems && selected != idx) {
                selected = idx;
                needRedraw = true;
            }
        }

        // Hover highlight for mouse movement without buttons.
        if (section != Section::Session) {
            int idx = hitTestListRow(mx, my, lh);
            if (idx >= 0 && idx < totalItems && selected != idx) {
                selected = idx;
                needRedraw = true;
            }
        }

        return std::nullopt;
    }

    StartupChoice eventLoop() {
        int ch = 0;
        while (true) {
            if (applyIgvUpdate()) {
                needRedraw = true;
            }

            auto visible = filteredEntries();
            clampSelection(static_cast<int>(visible.size()));
            if (needRedraw) {
                draw(visible);
                needRedraw = false;
            }

            ch = wgetch(win);
            if (ch == ERR) continue;

            bool handled = false;
            switch (ch) {
                case KEY_RESIZE:
                    if (!createOrResizeWindow()) {
                        cleanupTui();
                        return {StartupResult::Error, {}, {}};
                    }
                    needRedraw = true;
                    handled = true;
                    break;
                case KEY_UP:
                case 'k':
                    --selected;
                    needRedraw = true;
                    handled = true;
                    break;
                case KEY_DOWN:
                case 'j':
                    ++selected;
                    needRedraw = true;
                    handled = true;
                    break;
                case KEY_LEFT:
                    if (section == Section::Local) {
                        section = Section::Session;
                    } else if (section == Section::Online) {
                        section = Section::Local;
                    } else {
                        section = Section::Online;
                    }
                    inputBuf.clear();
                    selected = 0;
                    scrollOffset = 0;
                    needRedraw = true;
                    handled = true;
                    break;
                case KEY_RIGHT:
                    if (section == Section::Local) {
                        section = Section::Online;
                    } else if (section == Section::Online) {
                        section = Section::Session;
                    } else {
                        section = Section::Local;
                    }
                    inputBuf.clear();
                    selected = 0;
                    scrollOffset = 0;
                    needRedraw = true;
                    handled = true;
                    break;
                case KEY_BACKSPACE:
                case 127:
                case '\b':
                    if (!inputBuf.empty()) {
                        inputBuf.pop_back();
                        selected = 0;
                        scrollOffset = 0;
                        needRedraw = true;
                    }
                    handled = true;
                    break;
                case 27: // Escape
                    return {StartupResult::Quit, {}, {}};
                case 'q':
                    return {StartupResult::Quit, {}, {}};
                case 'r':
                case 'R':
                    {
                        StartupChoice result = resumeSessionChoice();
                        if (result.result != StartupResult::Error) {
                            return result;
                        }
                        needRedraw = true;
                    }
                    handled = true;
                    break;
                case 'm':
                case 'M':
                    mouseEnabled = !mouseEnabled;
                    if (mouseEnabled) {
                        enableMouseTracking();
                    } else {
                        disableMouseTracking();
                    }
                    needRedraw = true;
                    handled = true;
                    break;
                case 'd':
                case 'D':
                    if (section == Section::Online && !visible.empty()) {
                        StartupChoice dl = downloadSelected(visible[selected]);
                        applyMouseState();
                        if (dl.result != StartupResult::Error) {
                            return dl;
                        }
                        needRedraw = true;
                    } else {
                        message = "Select an online genome to download";
                        needRedraw = true;
                    }
                    handled = true;
                    break;
                case '\n':
                case '\r':
                case KEY_ENTER:
                    {
                        StartupChoice result = handleEnter(visible);
                        if (result.result != StartupResult::Error) {
                            return result;
                        }
                        // Error means nothing was selected; keep the loop running.
                    }
                    handled = true;
                    break;
                case KEY_MOUSE:
                    {
                        MEVENT mev;
                        if (getmouse(&mev) == OK) {
                            int mx = mev.x;
                            int my = mev.y;
                            if (wmouse_trafo(win, &my, &mx, FALSE)) {
                                auto maybeResult = handleMouse(mx, my, mev.bstate, visible);
                                if (maybeResult.has_value()) {
                                    return maybeResult.value();
                                }
                            }
                        }
                    }
                    handled = true;
                    break;
                default:
                    break;
            }

            if (!handled && ch >= 32 && ch < 127) {
                inputBuf.push_back(static_cast<char>(ch));
                selected = 0;
                scrollOffset = 0;
                needRedraw = true;
            }

            // Clear transient status messages once the user interacts.
            bool stillLoading = igvState && igvState->loading;
            if (!stillLoading && !message.empty()) {
                message.clear();
                needRedraw = true;
            }
        }
    }

    StartupChoice resumeSessionChoice() {
        if (iopts.session_file.empty() || !std::filesystem::exists(iopts.session_file)) {
            message = "No previous session";
            needRedraw = true;
            return {StartupResult::Error, {}, {}};
        }
        try {
            mINI::INIFile file(iopts.session_file);
            mINI::INIStructure sesh;
            if (file.read(sesh)) {
                std::string genomePath = sesh["data"]["genome_path"];
                if (!genomePath.empty() && !isOnlinePath(genomePath) &&
                    !std::filesystem::exists(genomePath)) {
                    message = "Session genome missing: " + genomePath;
                    needRedraw = true;
                    return {StartupResult::Error, {}, {}};
                }
            }
        } catch (...) {
            // If the session cannot be parsed, let GW report the error.
        }
        return {StartupResult::Session, {}, {}};
    }

    StartupChoice handleEnter(const std::vector<GenomeEntry>& visible) {
        if (section == Section::Session) {
            return resumeSessionChoice();
        }

        // Current selection takes priority over session resume.
        if (!visible.empty() && selected >= 0 && selected < static_cast<int>(visible.size())) {
            const GenomeEntry& e = visible[selected];
            return {StartupResult::Genome, e.path, e.tag};
        }

        // Empty input + existing session -> load session.
        if (inputBuf.empty()) {
            return resumeSessionChoice();
        }

        // Exact tag match in any section.
        for (const auto& e : localEntries) {
            if (e.tag == inputBuf) {
                return {StartupResult::Genome, e.path, e.tag};
            }
        }
        for (const auto& e : onlineEntries) {
            if (e.tag == inputBuf) {
                return {StartupResult::Genome, e.path, e.tag};
            }
        }

        // Treat typed text as a path.
        return {StartupResult::Genome, inputBuf, {}};
    }

    struct DownloadProgress {
        curl_off_t total = 0;
        curl_off_t now = 0;
        bool completed = false;
        bool failed = false;
        std::string error;
    };

    std::optional<StartupChoice> runHintAction(HintAction action,
                                               const std::vector<GenomeEntry>& visible) {
        switch (action) {
            case HintAction::Enter:
                return handleEnter(visible);
            case HintAction::Download:
                if (section == Section::Online && !visible.empty()) {
                    StartupChoice dl = downloadSelected(visible[selected]);
                    enableMouseTracking();
                    if (dl.result != StartupResult::Error) {
                        return dl;
                    }
                    needRedraw = true;
                }
                return std::nullopt;
            case HintAction::Resume:
                {
                    StartupChoice result = resumeSessionChoice();
                    if (result.result != StartupResult::Error) {
                        return result;
                    }
                    needRedraw = true;
                }
                return std::nullopt;
            case HintAction::Mouse:
                mouseEnabled = !mouseEnabled;
                if (mouseEnabled) {
                    enableMouseTracking();
                } else {
                    disableMouseTracking();
                }
                needRedraw = true;
                return std::nullopt;
            case HintAction::Quit:
                return StartupChoice{StartupResult::Quit, {}, {}};
        }
        return std::nullopt;
    }

    struct DownloadContext {
        StartupDialog* dialog = nullptr;
        std::string label;
        DownloadProgress progress;
        std::chrono::steady_clock::time_point start;
        std::chrono::steady_clock::time_point lastDraw;
    };

    StartupChoice downloadSelected(const GenomeEntry& e) {
        std::filesystem::path iniPath(iopts.ini_path);
        std::filesystem::path downloadDir = iniPath.parent_path() / "genomes";
        std::filesystem::path defaultFile = downloadDir / (e.tag + ".fa.gz");

        // Let the user edit the save path (with Tab completion).
        EditPathResult pathResult = editDownloadPath(e, defaultFile.string());
        if (pathResult.quit) {
            return {StartupResult::Quit, {}, {}};
        }
        if (pathResult.cancelled || pathResult.path.empty()) {
            message = "Download cancelled";
            return {StartupResult::Error, {}, {}};
        }
        std::string chosenPath = expandTilde(pathResult.path);
        std::filesystem::path outFile(chosenPath);
        std::filesystem::path outDir = outFile.parent_path();

        try {
            std::filesystem::create_directories(outDir);
        } catch (const std::exception& ex) {
            message = std::string("Cannot create directory: ") + ex.what();
            return {StartupResult::Error, {}, {}};
        }

        DownloadProgress progress;
        bool fastaOk = downloadWithProgress(e.path, outFile.string(),
                                            e.tag + " FASTA", progress);
        if (!fastaOk) {
            message = "Download failed: " + progress.error;
            return {StartupResult::Error, {}, {}};
        }

        // Download the index files as well.
        DownloadProgress faiProgress;
        bool faiOk = downloadWithProgress(e.path + ".fai", outFile.string() + ".fai",
                                          e.tag + " .fai index", faiProgress);
        if (!faiOk) {
            message = "Downloaded FASTA but .fai index failed: " + faiProgress.error;
            return {StartupResult::Error, {}, {}};
        }

        if (e.path.size() > 3 && e.path.substr(e.path.size() - 3) == ".gz") {
            DownloadProgress gziProgress;
            bool gziOk = downloadWithProgress(e.path + ".gzi", outFile.string() + ".gzi",
                                              e.tag + " .gzi index", gziProgress);
            if (!gziOk) {
                message = "Downloaded FASTA but .gzi index failed: " + gziProgress.error;
                return {StartupResult::Error, {}, {}};
            }
        }

        // Move the entry from online to local in the ini.
        iopts.myIni["genomes"][e.tag] = outFile.string();
        iopts.saveIniChanges();
        loadGenomes();
        section = Section::Local;
        inputBuf.clear();
        selected = 0;
        scrollOffset = 0;
        return {StartupResult::Genome, outFile.string(), e.tag};
    }

    static int curlProgressCallback(void* clientp, curl_off_t dltotal,
                                    curl_off_t dlnow,
                                    curl_off_t /*ultotal*/,
                                    curl_off_t /*ulnow*/) {
        auto* ctx = static_cast<DownloadContext*>(clientp);
        ctx->progress.total = dltotal;
        ctx->progress.now = dlnow;

        using Clock = std::chrono::steady_clock;
        auto now = Clock::now();
        if (ctx->dialog && now - ctx->lastDraw > std::chrono::milliseconds(100)) {
            ctx->dialog->drawDownloadDialog(ctx->label, ctx->progress, ctx->start, true);
            ctx->lastDraw = now;
        }
        return ctx->progress.failed ? 1 : 0;
    }

    bool downloadWithProgress(const std::string& url,
                              const std::string& outPath,
                              const std::string& label,
                              DownloadProgress& progress) {
        MouseSuspender mouseGuard(this);

        using Clock = std::chrono::steady_clock;
        DownloadContext ctx;
        ctx.dialog = this;
        ctx.label = label;
        ctx.start = Clock::now();
        ctx.lastDraw = ctx.start;
        ctx.progress = progress;

        // Draw the initial progress dialog.
        drawDownloadDialog(label, ctx.progress, ctx.start, true);

        CURL* curl = curl_easy_init();
        if (!curl) {
            progress.error = "curl init failed";
            progress.failed = true;
            return false;
        }

        std::ofstream out(outPath, std::ios::binary);
        if (!out) {
            progress.error = "Cannot open " + outPath;
            curl_easy_cleanup(curl);
            progress.failed = true;
            return false;
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "gw-genome-browser/2.0");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToFile);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &ctx);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

        CURLcode res = curl_easy_perform(curl);
        out.close();

        long httpCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
        curl_easy_cleanup(curl);

        ctx.progress.completed = true;
        progress = ctx.progress;

        if (res != CURLE_OK) {
            progress.error = curl_easy_strerror(res);
            progress.failed = true;
            std::error_code ec;
            std::filesystem::remove(outPath, ec);
            drawDownloadDialog(label, progress, ctx.start, true);
            waitForAnyKey();
            return false;
        }
        if (httpCode >= 400) {
            progress.error = "HTTP " + std::to_string(httpCode);
            progress.failed = true;
            std::error_code ec;
            std::filesystem::remove(outPath, ec);
            drawDownloadDialog(label, progress, ctx.start, true);
            waitForAnyKey();
            return false;
        }

        drawDownloadDialog(label, progress, ctx.start, true);

        // Reject empty/corrupt downloads.
        std::error_code ec;
        auto size = std::filesystem::file_size(outPath, ec);
        if (ec || size == 0) {
            progress.error = "Downloaded file is empty";
            progress.failed = true;
            std::filesystem::remove(outPath, ec);
            drawDownloadDialog(label, progress, ctx.start, true);
            waitForAnyKey();
            return false;
        }

        return true;
    }

    void waitForAnyKey() {
        nodelay(win, FALSE);
        wgetch(win);
    }

    void drawDownloadDialog(const std::string& label,
                            const DownloadProgress& progress,
                            std::chrono::steady_clock::time_point start,
                            bool refresh) {
        using Clock = std::chrono::steady_clock;
        werase(win);
        int w = getmaxx(win);
        int h = getmaxy(win);
        constexpr int marginX = 2;

        auto printLabel = [&](int y, const std::string& text) {
            if (hasColors) wattron(win, A_BOLD | COLOR_PAIR(6));
            mvwprintw(win, y, marginX, "%s", text.c_str());
            if (hasColors) wattroff(win, A_BOLD | COLOR_PAIR(6));
        };

        auto printValue = [&](int y, const std::string& text) {
            mvwprintw(win, y, marginX + 10, "%s", text.c_str());
        };

        // Title with version.
        if (hasColors) wattron(win, COLOR_PAIR(3) | A_BOLD);
        std::string title = std::string("GW v") + GW_VERSION + " - Download";
        mvwprintw(win, 0, marginX, "%s", title.c_str());
        if (hasColors) wattroff(win, COLOR_PAIR(3) | A_BOLD);

        // Light grey divider under title.
        drawPartialDivider(win, 1, marginX, std::max(10, w - 4), 0);

        // Context line.
        if (hasColors) wattron(win, A_BOLD | COLOR_PAIR(6));
        mvwprintw(win, 3, marginX, "Downloading:");
        if (hasColors) wattroff(win, A_BOLD | COLOR_PAIR(6));
        std::string clippedLabel = label;
        int labelMax = std::max(10, w - marginX - 16);
        if (static_cast<int>(clippedLabel.size()) > labelMax) {
            clippedLabel = clippedLabel.substr(0, labelMax - 3) + "...";
        }
        mvwprintw(win, 3, marginX + 14, "%s", clippedLabel.c_str());

        int y = 5;

        // Status.
        std::string status;
        int statusColor = 0;
        if (progress.failed) {
            status = "Failed";
            statusColor = 4;
        } else if (progress.completed) {
            status = "Done";
            statusColor = 2;
        } else {
            status = "In progress";
        }
        printLabel(y, "Status:");
        if (statusColor != 0 && hasColors) wattron(win, A_BOLD | COLOR_PAIR(statusColor));
        printValue(y, status);
        if (statusColor != 0 && hasColors) wattroff(win, A_BOLD | COLOR_PAIR(statusColor));
        ++y;

        // Size.
        std::string sizeText = formatBytes(progress.now);
        if (progress.total > 0) {
            sizeText += " / " + formatBytes(progress.total);
        }
        printLabel(y, "Size:");
        printValue(y, sizeText);
        ++y;

        // Rate.
        auto elapsed = Clock::now() - start;
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();
        std::string rateText;
        if (seconds > 0) {
            rateText = formatBytes(progress.now / seconds) + "/s";
        }
        if (!rateText.empty()) {
            printLabel(y, "Rate:");
            printValue(y, rateText);
            ++y;
        }

        y += 1; // empty line before progress bar

        // Progress bar.
        int barWidth = std::max(10, w - marginX - 14 - 2);
        int filled = 0;
        if (progress.total > 0) {
            filled = static_cast<int>(progress.now * barWidth / progress.total);
            filled = std::min(filled, barWidth);
        }
        printLabel(y, "Progress:");
        std::string bar = "[" + std::string(filled, '#') +
                          std::string(barWidth - filled, '-') + "]";
        if (hasColors) wattron(win, A_BOLD | COLOR_PAIR(2));
        mvwprintw(win, y, marginX + 14, "%s", bar.c_str());
        if (hasColors) wattroff(win, A_BOLD | COLOR_PAIR(2));

        // Footer hint / error.
        int footerY = h - 2;
        if (progress.failed && !progress.error.empty()) {
            std::string err = progress.error;
            if (static_cast<int>(err.size()) > w - 10) {
                err = err.substr(0, w - 13) + "...";
            }
            if (hasColors) wattron(win, COLOR_PAIR(4));
            mvwprintw(win, footerY, marginX, "Error: %s", err.c_str());
            if (hasColors) wattroff(win, COLOR_PAIR(4));
            footerY = h - 3;
        }

        // Hint line.
        auto printHint = [&](int& x, const std::string& normal, const std::string& bold) {
            mvwprintw(win, footerY, x, "%s", normal.c_str());
            x += static_cast<int>(normal.size());
            if (hasColors) wattron(win, A_BOLD | COLOR_PAIR(6));
            mvwprintw(win, footerY, x, "%s", bold.c_str());
            if (hasColors) wattroff(win, A_BOLD | COLOR_PAIR(6));
            x += static_cast<int>(bold.size());
        };
        int hx = marginX;
        if (progress.failed) {
            printHint(hx, "Press ", "any key");
            mvwprintw(win, footerY, hx, " to continue");
        } else if (progress.completed) {
            printHint(hx, "Download ", "complete");
        } else {
            printHint(hx, "Please ", "wait");
        }

        if (refresh) {
            wrefresh(win);
        }
    }

    struct EditPathResult {
        std::string path;
        bool cancelled = false;
        bool quit = false;
    };

    // Path editor for choosing where to download an online genome.
    EditPathResult editDownloadPath(const GenomeEntry& e, const std::string& defaultPath) {
        applyMouseState();

        std::string pathBuf = defaultPath;
        size_t cursorPos = pathBuf.size();
        bool done = false;
        curs_set(1);  // Show the text cursor while editing.
        while (!done) {
            werase(win);
            int w = getmaxx(win);
            int h = getmaxy(win);
            constexpr int marginX = 2;
            const int footerY = h - 2;
            enum class DlButtonAction { Esc, Enter, MouseToggle, Quit };
            struct DlButton {
                int x = 0;
                int width = 0;
                DlButtonAction action = DlButtonAction::Esc;
            };
            std::vector<DlButton> dlButtons;

            auto printLabel = [&](int y, const std::string& text) {
                if (hasColors) wattron(win, A_BOLD | COLOR_PAIR(6));
                mvwprintw(win, y, marginX, "%s", text.c_str());
                if (hasColors) wattroff(win, A_BOLD | COLOR_PAIR(6));
            };

            // Title with version.
            if (hasColors) wattron(win, COLOR_PAIR(3) | A_BOLD);
            std::string title = std::string("GW v") + GW_VERSION + " - Download";
            mvwprintw(win, 0, marginX, "%s", title.c_str());
            if (hasColors) wattroff(win, COLOR_PAIR(3) | A_BOLD);

            // Light grey divider under title.
            drawPartialDivider(win, 1, marginX, std::max(10, w - 4), 0);

            // Genome tag context line.
            int infoY = 3;
            printLabel(infoY, "Genome:");
            std::string clippedTag = e.tag;
            int tagMax = std::max(10, w - marginX - 10);
            if (static_cast<int>(clippedTag.size()) > tagMax) {
                clippedTag = clippedTag.substr(0, tagMax - 3) + "...";
            }
            mvwprintw(win, infoY, marginX + 10, "%s", clippedTag.c_str());

            // URL.
            printLabel(infoY + 1, "URL:");
            std::string clippedURL = e.path;
            int urlMax = std::max(10, w - marginX - 10);
            if (static_cast<int>(clippedURL.size()) > urlMax) {
                clippedURL = clippedURL.substr(0, urlMax - 3) + "...";
            }
            mvwprintw(win, infoY + 1, marginX + 10, "%s", clippedURL.c_str());

            printLabel(infoY + 3, "Save path:");

            // Display the path, scrolling horizontally if it is too long.
            int maxPath = std::max(10, w - 10);
            constexpr int promptLen = 2; // "> "
            int textStartX = marginX + promptLen;
            std::string displayPath;
            size_t displayCursor = cursorPos;
            if (static_cast<int>(pathBuf.size()) <= maxPath) {
                displayPath = pathBuf;
            } else {
                int visible = maxPath - 3; // room for "..."
                size_t start = 0;
                if (cursorPos > static_cast<size_t>(visible / 2)) {
                    start = cursorPos - visible / 2;
                }
                size_t end = std::min(pathBuf.size(), start + visible);
                if (end - start < static_cast<size_t>(visible) && end == pathBuf.size()) {
                    start = end - visible;
                }
                if (start > 0) {
                    displayPath = "...";
                    displayCursor = 3 + (cursorPos - start);
                }
                displayPath += pathBuf.substr(start, end - start);
                if (end < pathBuf.size()) {
                    displayPath += "...";
                }
            }
            mvwprintw(win, infoY + 4, marginX, "> %s", displayPath.c_str());

            // Footer buttons (single line, same style as the main page hint bar).
            int bx = marginX;
            auto drawFooterButton = [&](const std::string& label, DlButtonAction action) {
                int width = static_cast<int>(label.size());
                bool hovered = (mouseY == footerY && mouseX >= bx && mouseX < bx + width);
                if (hovered) {
                    if (hasColors) wattron(win, A_REVERSE | A_BOLD | COLOR_PAIR(6));
                    mvwprintw(win, footerY, bx, "%s", label.c_str());
                    if (hasColors) wattroff(win, A_REVERSE | A_BOLD | COLOR_PAIR(6));
                } else {
                    if (hasColors) wattron(win, A_BOLD | COLOR_PAIR(6));
                    mvwprintw(win, footerY, bx, "%s", label.c_str());
                    if (hasColors) wattroff(win, A_BOLD | COLOR_PAIR(6));
                }
                dlButtons.push_back({bx, width, action});
                bx += width;
            };

            mvwprintw(win, footerY, bx, "Navigate: ");
            bx += 10;
            if (hasColors) wattron(win, A_BOLD | COLOR_PAIR(6));
            mvwprintw(win, footerY, bx, "<>^v");
            if (hasColors) wattroff(win, A_BOLD | COLOR_PAIR(6));
            bx += 4;
            mvwprintw(win, footerY, bx, " | ");
            bx += 3;

            drawFooterButton("[Esc - back]", DlButtonAction::Esc);
            mvwprintw(win, footerY, bx, " | ");
            bx += 3;
            drawFooterButton("[Enter - download]", DlButtonAction::Enter);
            mvwprintw(win, footerY, bx, " | ");
            bx += 3;
            drawFooterButton(std::string("[m - mouse:") + (mouseEnabled ? "on" : "off") + "]",
                             DlButtonAction::MouseToggle);
            mvwprintw(win, footerY, bx, " | ");
            bx += 3;
            drawFooterButton("[q - quit]", DlButtonAction::Quit);

            // Position the cursor inside the visible path.
            wmove(win, infoY + 4, textStartX + static_cast<int>(displayCursor));
            wrefresh(win);

            auto doTabCompletion = [&]() {
                try {
                    std::string pattern = pathBuf;
                    if (pattern.empty()) pattern = "./";
                    if (pattern.back() != '/') pattern += "*";
                    else pattern += "*";
                    auto matches = glob_cpp::glob(pattern);
                    if (matches.size() == 1) {
                        pathBuf = matches[0].string();
                        cursorPos = pathBuf.size();
                    } else if (matches.size() > 1) {
                        pathBuf = longestCommonPrefix(matches);
                        cursorPos = pathBuf.size();
                    }
                } catch (...) {
                    // Leave path unchanged on completion error.
                }
            };

            int ch = wgetch(win);
            switch (ch) {
                case '\t':
                    doTabCompletion();
                    break;
                case KEY_LEFT:
                    if (cursorPos > 0) --cursorPos;
                    break;
                case KEY_RIGHT:
                    if (cursorPos < pathBuf.size()) ++cursorPos;
                    break;
                case KEY_HOME:
                    cursorPos = 0;
                    break;
                case KEY_END:
                    cursorPos = pathBuf.size();
                    break;
                case KEY_BACKSPACE:
                case 127:
                case '\b':
                    if (cursorPos > 0) {
                        pathBuf.erase(cursorPos - 1, 1);
                        --cursorPos;
                    }
                    break;
                case KEY_DC:
                    if (cursorPos < pathBuf.size()) {
                        pathBuf.erase(cursorPos, 1);
                    }
                    break;
                case '\n':
                case '\r':
                case KEY_ENTER:
                    done = true;
                    break;
                case 27:
                    curs_set(0);
                    return EditPathResult{"", true, false};
                case 'q':
                case 'Q':
                    curs_set(0);
                    return EditPathResult{"", false, true};
                case 'm':
                case 'M':
                    mouseEnabled = !mouseEnabled;
                    applyMouseState();
                    break;
                case KEY_MOUSE:
                    {
                        MEVENT mev;
                        if (getmouse(&mev) == OK) {
                            int mx = mev.x;
                            int my = mev.y;
                            if (wmouse_trafo(win, &my, &mx, FALSE)) {
                                mouseX = mx;
                                mouseY = my;
                                bool clicked = (mev.bstate & BUTTON1_RELEASED) != 0 ||
                                               (mev.bstate & BUTTON1_CLICKED) != 0 ||
                                               (mev.bstate & BUTTON1_DOUBLE_CLICKED) != 0 ||
                                               (mev.bstate & BUTTON1_PRESSED) != 0;
                                if (clicked) {
                                    for (const auto& b : dlButtons) {
                                        if (my == footerY && mx >= b.x && mx < b.x + b.width) {
                                            switch (b.action) {
                                                case DlButtonAction::Esc:
                                                    curs_set(0);
                                                    return EditPathResult{"", true, false};
                                                case DlButtonAction::Enter:
                                                    done = true;
                                                    break;
                                                case DlButtonAction::MouseToggle:
                                                    mouseEnabled = !mouseEnabled;
                                                    applyMouseState();
                                                    break;
                                                case DlButtonAction::Quit:
                                                    curs_set(0);
                                                    return EditPathResult{"", false, true};
                                            }
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    break;
                default:
                    if (ch >= 32 && ch < 127) {
                        pathBuf.insert(cursorPos, 1, static_cast<char>(ch));
                        ++cursorPos;
                    }
                    break;
            }
        }
        curs_set(0);
        return EditPathResult{pathBuf, false, false};
    }

    static void drawPartialDivider(WINDOW* win, int y, int x, int width, int colorPair) {
        if (width <= 0 || !win) return;
        bool colors = has_colors();
        if (colors) wattron(win, A_DIM);
        if (colorPair != 0 && colors) wattron(win, COLOR_PAIR(colorPair));
        else if (colors) wattron(win, COLOR_PAIR(5));
        wmove(win, y, x);
        whline(win, '-', width);
        if (colorPair != 0 && colors) wattroff(win, COLOR_PAIR(colorPair));
        else if (colors) wattroff(win, COLOR_PAIR(5));
        if (colors) wattroff(win, A_DIM);
    }

    void drawHintLine(int y) {
        hintButtons.clear();
        int x = 2;

        auto drawButton = [&](const std::string& label, HintAction action) {
            int width = static_cast<int>(label.size());
            bool hovered = (mouseY == y && mouseX >= x && mouseX < x + width);
            if (hovered) {
                if (hasColors) wattron(win, A_REVERSE | A_BOLD | COLOR_PAIR(6));
                mvwprintw(win, y, x, "%s", label.c_str());
                if (hasColors) wattroff(win, A_REVERSE | A_BOLD | COLOR_PAIR(6));
            } else {
                if (hasColors) wattron(win, A_BOLD | COLOR_PAIR(6));
                mvwprintw(win, y, x, "%s", label.c_str());
                if (hasColors) wattroff(win, A_BOLD | COLOR_PAIR(6));
            }
            hintButtons.push_back({x, y, width, action});
            x += width;
        };

        auto drawText = [&](const std::string& text) {
            mvwprintw(win, y, x, "%s", text.c_str());
            x += static_cast<int>(text.size());
        };

        drawText("Navigate: ");

        // Arrow-key indicator.
        if (hasColors) wattron(win, A_BOLD | COLOR_PAIR(6));
        mvwprintw(win, y, x, "<>^v");
        if (hasColors) wattroff(win, A_BOLD | COLOR_PAIR(6));
        x += 4;
        drawText(" | ");

        // [Enter]
        drawButton("[Enter]", HintAction::Enter);
        drawText(" | ");

        // [d - download] (Online section only)
        if (section == Section::Online) {
            drawButton("[d - download]", HintAction::Download);
            drawText(" | ");
        }

        // [r - Resume]
        drawButton("[r - Resume]", HintAction::Resume);
        drawText(" | ");

        // [m - mouse:on/off]
        std::string mouseLabel = std::string("[m - mouse:") + (mouseEnabled ? "on" : "off") + "]";
        drawButton(mouseLabel, HintAction::Mouse);
        drawText(" | ");

        // [q - quit]
        drawButton("[q - quit]", HintAction::Quit);
    }

    std::string contextLine() const {
        if (section == Section::Local) {
            return "Genomes listed in " + iopts.ini_path;
        } else if (section == Section::Online) {
            return "Genomes listed online";
        } else if (section == Section::Session) {
            return "[Resume previous session]";
        }
        return "";
    }

    void draw(const std::vector<GenomeEntry>& visible) {
        if (!win) return;
        int h = getmaxy(win);
        int w = getmaxx(win);

        werase(win);

        // Column widths and table geometry.
        const int scrollBarWidth = 2;
        int tagWidth = std::max(8, (w - 8 - scrollBarWidth) / 4);
        int fileWidth = std::max(10, w - 8 - scrollBarWidth - tagWidth);
        int tableWidth = tagWidth + fileWidth + 4;
        int tableX = 2;

        // Title with version.
        if (hasColors) wattron(win, COLOR_PAIR(3) | A_BOLD);
        std::string title = std::string("GW v") + GW_VERSION + " - select reference genome";
        mvwprintw(win, 0, tableX, "%s", title.c_str());
        if (hasColors) wattroff(win, COLOR_PAIR(3) | A_BOLD);

        // Light grey divider under title.
        drawPartialDivider(win, 1, tableX, tableWidth, 0);

        auto isMouseOver = [&](int x, int y, int width, int height) -> bool {
            return mouseX >= x && mouseX < x + width &&
                   mouseY >= y && mouseY < y + height;
        };

        // Tabs.
        int tabY = 3;
        int tabX = tableX;
        auto drawTab = [&](int x, const std::string& label, bool active, bool hovered) {
            if (active) {
                if (hasColors) wattron(win, COLOR_PAIR(2) | A_BOLD);
                mvwprintw(win, tabY, x, "[%s]", label.c_str());
                if (hasColors) wattroff(win, COLOR_PAIR(2) | A_BOLD);
            } else if (hovered) {
                if (hasColors) wattron(win, A_REVERSE | A_BOLD);
                mvwprintw(win, tabY, x, "[%s]", label.c_str());
                if (hasColors) wattroff(win, A_REVERSE | A_BOLD);
            } else {
                if (hasColors) wattron(win, A_BOLD | COLOR_PAIR(6));
                mvwprintw(win, tabY, x, "[%s]", label.c_str());
                if (hasColors) wattroff(win, A_BOLD | COLOR_PAIR(6));
            }
        };
        drawTab(tabX, "Local", section == Section::Local,
                isMouseOver(tabX, tabY, 7, 1));
        drawTab(tabX + 9, "Online", section == Section::Online,
                isMouseOver(tabX + 9, tabY, 8, 1));
        drawTab(tabX + 20, "Session", section == Section::Session,
                isMouseOver(tabX + 20, tabY, 9, 1));

        // Context line.
        int contextY = 5;
        std::string ctx = contextLine();
        if (!ctx.empty()) {
            if (section == Section::Session && hasColors) {
                bool hovered = isMouseOver(tableX, contextY, static_cast<int>(ctx.size()), 1);
                if (hovered) wattron(win, A_REVERSE | A_BOLD | COLOR_PAIR(6));
                else wattron(win, A_BOLD | COLOR_PAIR(6));
                mvwprintw(win, contextY, tableX, "%s", ctx.c_str());
                if (hovered) wattroff(win, A_REVERSE | A_BOLD | COLOR_PAIR(6));
                else wattroff(win, A_BOLD | COLOR_PAIR(6));
            } else {
                mvwprintw(win, contextY, tableX, "%s", ctx.c_str());
            }
        }

        int y = 8;
        int lh = listHeight();
        std::string selectedPath;

        if (section == Section::Session) {
            int infoY = 7;

            auto infoLines = sessionInfoLines();
            int avail = std::max(10, tableWidth - 4);
            std::vector<std::string> wrapped;
            for (const auto& line : infoLines) {
                if (static_cast<int>(line.size()) <= avail) {
                    wrapped.push_back(line);
                } else {
                    for (size_t i = 0; i < line.size(); i += avail) {
                        wrapped.push_back(line.substr(i, avail));
                    }
                }
            }
            for (int i = 0; i < lh && i < static_cast<int>(wrapped.size()); ++i) {
                mvwprintw(win, infoY + i, tableX, "%s", wrapped[i + scrollOffset].c_str());
            }
        } else {
            // Column headers
            if (hasColors) wattron(win, A_BOLD);
            mvwprintw(win, 7, tableX, "%-*s %-*s", tagWidth, "Tag", fileWidth, "File");
            if (hasColors) wattroff(win, A_BOLD);
            drawPartialDivider(win, 8, tableX, tableWidth, 0);
            y = 9;

            // List
            for (int i = 0; i < lh; ++i) {
                int idx = scrollOffset + i;
                if (idx >= static_cast<int>(visible.size())) break;
                const GenomeEntry& e = visible[idx];
                bool highlighted = (idx == selected);
                if (highlighted) selectedPath = e.path;
                if (highlighted && hasColors) wattron(win, COLOR_PAIR(1) | A_BOLD);
                std::string tag = e.tag;
                if (tag.size() > static_cast<size_t>(tagWidth)) tag = tag.substr(0, tagWidth - 3) + "...";
                std::string file = e.filename;
                if (file.size() > static_cast<size_t>(fileWidth)) {
                    file = file.substr(0, fileWidth - 3) + "...";
                }
                mvwprintw(win, y + i, tableX, "%-*s %-*s", tagWidth, tag.c_str(), fileWidth, file.c_str());
                if (highlighted && hasColors) wattroff(win, COLOR_PAIR(1) | A_BOLD);
            }

            // Scrollbar
            int totalItems = static_cast<int>(visible.size());
            if (totalItems > lh && lh > 0) {
                int trackHeight = lh;
                int thumbSize = std::max(1, trackHeight * trackHeight / totalItems);
                int maxThumbPos = trackHeight - thumbSize;
                int thumbPos = (maxThumbPos <= 0) ? 0 : (scrollOffset * maxThumbPos / (totalItems - lh));
                int scrollX = tableX + tableWidth + 1;
                for (int i = 0; i < trackHeight; ++i) {
                    if (i >= thumbPos && i < thumbPos + thumbSize) {
                        if (hasColors) wattron(win, A_REVERSE);
                        mvwaddch(win, y + i, scrollX, VBLOCK);
                        if (hasColors) wattroff(win, A_REVERSE);
                    }
                }
            }
        }

        // Divider above path.
        int abovePathY = h - 6;
        drawPartialDivider(win, abovePathY, tableX, tableWidth, 0);

        // Wrapped path display for the selected item (up to 2 lines).
        if (!selectedPath.empty()) {
            int statusY = h - 5;
            int avail = std::max(10, w - 6);
            std::vector<std::string> lines;
            for (size_t i = 0; i < selectedPath.size() && lines.size() < 2; i += avail) {
                lines.push_back(selectedPath.substr(i, avail));
            }
            if (lines.size() == 2 && lines[1].size() == static_cast<size_t>(avail)) {
                lines[1].replace(lines[1].size() - 3, 3, "...");
            }
            for (size_t i = 0; i < lines.size(); ++i) {
                mvwprintw(win, statusY + static_cast<int>(i), tableX, "%s", lines[i].c_str());
            }
        }

        // Input line (blank when empty).
        int inputY = h - 3;
        int inputWidth = std::max(10, w - 6);
        std::string inputDisplay = inputBuf;
        if (static_cast<int>(inputDisplay.size()) > inputWidth) {
            inputDisplay = inputDisplay.substr(inputDisplay.size() - inputWidth);
        }
        mvwprintw(win, inputY, tableX, "%-*s", inputWidth, inputDisplay.c_str());

        // Message line.
        int msgY = h - 2;
        if (!message.empty()) {
            if (hasColors && (message.find("failed") != std::string::npos || message.find("Error") != std::string::npos)) {
                wattron(win, COLOR_PAIR(4));
            }
            std::string msg = message;
            if (msg.size() > static_cast<size_t>(w - 4)) {
                msg = msg.substr(0, w - 7) + "...";
            }
            mvwprintw(win, msgY, tableX, "%s", msg.c_str());
            if (hasColors && (message.find("failed") != std::string::npos || message.find("Error") != std::string::npos)) {
                wattroff(win, COLOR_PAIR(4));
            }
        }

        // Hints at the bottom.
        drawHintLine(h - 1);

        wrefresh(win);
    }
};

} // anonymous namespace

StartupChoice runStartupGenomeDialog(Themes::IniOptions& iopts) {
    StartupDialog dlg(iopts);
    return dlg.run();
}

} // namespace Menu

#else // __EMSCRIPTEN__

namespace Menu {

StartupChoice runStartupGenomeDialog(Themes::IniOptions&) {
    return {StartupResult::Error, {}, {}};
}

} // namespace Menu

#endif // !defined(__EMSCRIPTEN__)
