//
// Created by Kez Cleal on 23/08/2022.
//
#include <array>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iterator>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <htslib/sam.h>
#include <htslib/hts.h>
#include <GLFW/glfw3.h>
#define SK_GL
#include "hts_funcs.h"
#include "parser.h"
#include "plot_manager.h"
#include "menu.h"
#include "segments.h"
#include "termcolor.h"
#include "term_out.h"
#include "themes.h"


namespace Manager {

    enum PlotItem {
        NO_REGIONS = -1,
        REFERENCE_TRACK = -2,
        TRACK = -3
    };

    enum Errors {
        NONE,
        CHROM_NOT_IN_REFERENCE,
        FEATURE_NOT_IN_TRACKS,
        BAD_REGION,
        OPTION_NOT_UNDERSTOOD,
        GENERIC
    };

    constexpr int DRAG_UNSET = -1000000;

    void error_report(int err) {
        std::cerr << termcolor::red << "Error:" << termcolor::reset;
        if (err == CHROM_NOT_IN_REFERENCE) {
            std::cerr << " loci not understood\n";
        } else if (err == FEATURE_NOT_IN_TRACKS) {
            std::cerr << " loci not understood, or feature name not found in tracks\n";
        } else if (err == BAD_REGION) {
            std::cerr << " region not understood\n";
        } else if (err == OPTION_NOT_UNDERSTOOD) {
            std::cerr << " option not understood\n";
        } else if (err == GENERIC) {
            std::cerr << " command not understood / invalid loci or feature name\n";
        }
    }

    struct TipBounds {
        int lower, upper;
        std::string cmd;
    };
    TipBounds getToolTipBounds(std::string &inputText) {
        if (inputText.empty()) {
            return {0, (int)  Menu::commandToolTip.size()-1, ""};
        }
        int max_i = 0;
        int min_i = 0;
        int idx = 0;
        bool any_matches = false;
        for (const auto &cmd : Menu::commandToolTip) {
            std::string cmd_s = cmd;
            if (Utils::startsWith(cmd_s, inputText) || Utils::startsWith(inputText, cmd_s)) {
                if (min_i == 0 && idx) {
                    min_i = idx;
                }
                if (idx > max_i) {
                    max_i = idx;
                }
                any_matches = true;
            }
            idx += 1;
        }
        if (any_matches) {
            return {min_i, max_i, Menu::commandToolTip[max_i]};
        } else {
            return {0, (int)Menu::commandToolTip.size()-1, ""};
        }
    }

    // keeps track of input commands. returning GLFW_KEY_UNKNOWN stops further processing of key codes
    int GwPlot::registerKey(GLFWwindow* wind, int key, int scancode, int action, int mods) {
	    if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_LEFT_SUPER) {
		    if (action == GLFW_PRESS || action == GLFW_REPEAT) {
			    ctrlPress = true;
		    } else if (action == GLFW_RELEASE) {
                ctrlPress = false;
            }
            return key;
	    } else if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) {
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                shiftPress = true;
            } else if (action == GLFW_RELEASE) {
                shiftPress = false;
            }
            return key;
        } else if (action == GLFW_RELEASE) {
            return key;
        }

        if ( (key == GLFW_KEY_SLASH && !captureText) || (shiftPress && key == GLFW_KEY_SEMICOLON && !captureText)) {
            captureText = true;
            inputText = "";
            charIndex = 0;
            textFromSettings = false;
            return key;
        }
        if (key == GLFW_KEY_TAB && !captureText) {
            currentVarTrack = &variantTracks[variantFileSelection];
            if (currentVarTrack == nullptr) {
                return key;
            }

            if (mode == Manager::SINGLE && !regions.empty() && (!currentVarTrack->multiRegions.empty() || currentVarTrack->type == HGW::TrackType::IMAGES) ) {
                mode = Manager::TILED;
                redraw = true;
                processed = false;
                imageCacheQueue.clear();
                mouseOverTileIndex = 0;
                return key;
            }
            if (mode == Manager::TILED && !regions.empty()) {
                mode = Manager::SINGLE;
                redraw = true;
                processed = false;
                imageCacheQueue.clear();
                if (currentVarTrack->blockStart < (int)currentVarTrack->multiRegions.size()) {
                    if (currentVarTrack->multiRegions[currentVarTrack->blockStart][0].chrom.empty()) {
                        return key; // check for "" no chrom set
                    } else {
                        regions = currentVarTrack->multiRegions[currentVarTrack->blockStart];
                        redraw = true;
                        processed = false;
                        fetchRefSeqs();
                        glfwPostEmptyEvent();
                    }
                }
                return key;
            }
        }
        if (!captureText) {
            if (key == opts.repeat_command) {
                if (mode == SETTINGS) {
                    return key;
                }
                std::cout << std::endl;
                if (!commandHistory.empty()) {
                    inputText = commandHistory.back();
                }
            } else if (mode == TILED && ctrlPress && variantTracks.size() > 1) {
                int before = variantFileSelection;
                if (key == GLFW_KEY_LEFT) {
                    variantFileSelection = (variantFileSelection > 0) ? variantFileSelection - 1 : 0;
                } else if (key == GLFW_KEY_RIGHT) {
                    variantFileSelection = (variantFileSelection < (int)variantTracks.size() - 1) ? variantFileSelection + 1 : variantFileSelection;
                }
                if (variantFileSelection != before) {
                    std::cout << termcolor::magenta << "\nFile    " << termcolor::reset << variantTracks[variantFileSelection].path << "\n";
                    redraw = true;
                    processed = false;
                    imageCache.clear();
                }
                return GLFW_KEY_UNKNOWN;
            } else if ((key == GLFW_KEY_RIGHT || key == GLFW_KEY_LEFT || key == GLFW_KEY_UP || key == GLFW_KEY_DOWN) && shiftPress) {
                GLFWmonitor * monitor = glfwGetPrimaryMonitor();
                int monitor_xpos, monitor_ypos, monitor_w, monitor_h;
                int current_x, current_y;
                int current_w, current_h;
                glfwGetMonitorWorkarea(monitor, &monitor_xpos, &monitor_ypos, &monitor_w, &monitor_h);
                glfwGetWindowPos(wind, &current_x, &current_y);
                glfwGetWindowSize(wind, &current_w, &current_h);
                int new_x, new_y;
                int new_width, new_height;
                int step_x = monitor_w / 8;
                int step_y = monitor_h / 8;

                redraw = true;

                if (key == GLFW_KEY_RIGHT) {
                    if (current_x <= 0 && current_w < monitor_w ) {
                        new_x = 0;
                        new_width = std::min(current_w + step_x, monitor_w);
                    } else {
                        new_x = current_x + step_x;
                        new_width = monitor_w - new_x;
                    }
                    if (new_width < step_x) {
                        return GLFW_KEY_UNKNOWN; // key press is now ignored
                    }
                    glfwSetWindowPos(wind, new_x, current_y);
                    glfwSetWindowSize(wind, new_width, current_h);
                } else if (key == GLFW_KEY_LEFT) {
                    if (current_x <= 0 && current_w <= monitor_w ) {
                        new_x = 0;
                        new_width = current_w - step_x;
                    } else {
                        new_x = std::max(0, current_x - step_x);
                        new_width = std::min(monitor_w, current_w + new_x);
                    }
                    if (new_width < step_x) {
                        return GLFW_KEY_UNKNOWN;
                    }
                    glfwSetWindowPos(wind, new_x, current_y);
                    glfwSetWindowSize(wind, new_width, current_h);
                } else if (key == GLFW_KEY_UP) {
                    if (current_y <= step_y && current_h <= monitor_h ) {
                        new_y = 0;
                        new_height = current_h - step_y;
                    } else {
                        new_y = std::max(0, current_y - step_y);
                        new_height = std::min(monitor_h, current_h + step_y);
                    }
                    if (new_height < step_y) {
                        return GLFW_KEY_UNKNOWN;
                    }
                    glfwSetWindowPos(wind, current_x, new_y);
                    glfwSetWindowSize(wind, current_w, new_height);
                } else if (key == GLFW_KEY_DOWN) {
                    if (current_y <= step_y && current_h <= monitor_h - step_y) {
                        new_y = 0;
                        new_height = std::min(current_h + step_y, monitor_h);
                    } else {
                        new_y = std::min(monitor_h - step_y, current_y + step_y);
                        new_height = std::min(monitor_h - new_y, current_h + step_y);
                    }
                    if (new_height < step_y) {
                        return GLFW_KEY_UNKNOWN;
                    }
                    glfwSetWindowPos(wind, current_x, new_y);
                    glfwSetWindowSize(wind, current_w, new_height);
                }
                return GLFW_KEY_UNKNOWN;
            }
        } else { //  captureText here
            if (key == GLFW_KEY_ESCAPE) {
                captureText = false;
                processText = false;
                shiftPress = false;
                commandToolTipIndex = -1;
                if (mode == SETTINGS) {
                    if (opts.editing_underway) {
                        opts.editing_underway = false;
                        inputText = "";
                        charIndex = 0;
                        textFromSettings = true;
                    }
                    return GLFW_KEY_UNKNOWN;
                } else {
                    inputText = "";
                    charIndex = 0;
                }
                return GLFW_KEY_UNKNOWN;
            }

            const bool no_command_selected = commandToolTipIndex == -1;
            if (no_command_selected) {
                if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
                    captureText = false;
                    processText = true;
                    shiftPress = false;
                    redraw = true;
                    processed = true;
                    commandToolTipIndex = -1;
                    std::cout << "\n";
                    return key;
                } else if (key == GLFW_KEY_TAB) {
                    if (mode != SETTINGS) {
                        TipBounds tip_bounds = getToolTipBounds(inputText);
                        if (tip_bounds.lower == tip_bounds.upper) {
                            inputText = tip_bounds.cmd;
                            charIndex = (int)inputText.size();
                        }
                        commandToolTipIndex = tip_bounds.upper;
                        return GLFW_KEY_UNKNOWN;
                    } else {
                        captureText = false;
                        processText = true;
                        shiftPress = false;
                        redraw = false;
                        processed = true;
                        commandToolTipIndex = -1;
                        return GLFW_KEY_ENTER;
                    }
                }
            } else {
                if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER || key == GLFW_KEY_SPACE) {
                    inputText = Menu::commandToolTip[commandToolTipIndex];
                    charIndex = (int)inputText.size();
                    if (std::find( Menu::exec.begin(), Menu::exec.end(), inputText) != Menu::exec.end() || (inputText == "online" && !opts.genome_tag.empty())) {
                        captureText = false;
                        processText = true;
                        shiftPress = false;
                        redraw = true;
                        processed = true;
                        imageCache.clear();
                        commandToolTipIndex = -1;
                        std::cout << "\n";
                        return GLFW_KEY_ENTER;
                    }
                    inputText += " ";
                    charIndex += 1;
                    commandToolTipIndex = -1;
                    return GLFW_KEY_UNKNOWN;
                }
            }

            if (!commandHistory.empty()) {
                if (mode != SETTINGS && commandToolTipIndex == -1) {
                    if (key == GLFW_KEY_UP && commandIndex > 0) {
                        commandIndex -= 1;
                        inputText = commandHistory[commandIndex];
                        charIndex = (int)inputText.size();
                        Term::clearLine();
                        return key;
                    } else if (key == GLFW_KEY_DOWN && commandIndex < (int)commandHistory.size() - 1) {
                        commandIndex += 1;
                        inputText = commandHistory[commandIndex];
                        charIndex = (int)inputText.size();
                        Term::clearLine();
                        return key;
                    }
                }
            }

            if (key == GLFW_KEY_LEFT) {
                charIndex = (charIndex - 1 >= 0) ? charIndex - 1 : charIndex;
                return key;
            } else if (key == GLFW_KEY_RIGHT) {
                charIndex = (charIndex < (int)inputText.size()) ? charIndex + 1 : charIndex;
                return key;
            }

            if (ctrlPress) {
                if (key == GLFW_KEY_V) {
                    std::string string = glfwGetClipboardString(window);
                    if (!string.empty()) {
                        inputText.append(string);
                        charIndex = (int)inputText.size();
                    }
                } else if (!inputText.empty()) {
                    if (key == GLFW_KEY_A) {
                        charIndex = 0;
                    } else if (key == GLFW_KEY_E) {
                        charIndex = (int)inputText.size();
                    } else if (key == GLFW_KEY_C) {
                        glfwSetClipboardString(window, inputText.c_str());
                    }
                }
                ctrlPress = false;
            } else {
                if (key == GLFW_KEY_BACKSPACE) {
                    if (!inputText.empty()) {
                        inputText.erase(charIndex - 1, 1);
                        charIndex -= 1;
                    }
                } else if (key == GLFW_KEY_DELETE) {
                    if (!inputText.empty() && charIndex < (int)inputText.size()) {
                        inputText.erase(charIndex, 1);
                    }
                }
                const char *letter = glfwGetKeyName(key, scancode);
                if (letter || key == GLFW_KEY_SPACE) {
                    if (key == GLFW_KEY_SPACE) {  // deal with special keys first
                        Term::editInputText(inputText, " ", charIndex);
                    }
                    else {
                        if (mods == GLFW_MOD_SHIFT && opts.shift_keymap.contains(key)) {
                            Term::editInputText(inputText, opts.shift_keymap[key].c_str(), charIndex);
                        }
                        else if (mods == GLFW_MOD_SHIFT) { // uppercase
                            std::string str = letter;
                            std::transform(str.begin(), str.end(),str.begin(), ::toupper);
                            Term::editInputText(inputText, str.c_str(), charIndex);
                        } else {  // normal text here
                            Term::editInputText(inputText, letter, charIndex);
                        }
                    }
                }
                // determine which command prefix the user has typed
                TipBounds tip_bounds = getToolTipBounds(inputText);
                if (key == GLFW_KEY_TAB || key == GLFW_KEY_DOWN) {
                    if (commandToolTipIndex <= 0 || commandToolTipIndex <= tip_bounds.lower) {
                        commandToolTipIndex = tip_bounds.upper;
                    } else {
                        commandToolTipIndex = std::max(commandToolTipIndex - 1, tip_bounds.lower);
                    }
                    return GLFW_KEY_UNKNOWN;
                } else if (key == GLFW_KEY_UP) {
                    if (commandToolTipIndex < 0 || commandToolTipIndex >= tip_bounds.upper) {
                        commandToolTipIndex = tip_bounds.lower;
                    } else {
                        commandToolTipIndex = commandToolTipIndex + 1;
                    }
                    return GLFW_KEY_UNKNOWN;
                }
                if (tip_bounds.lower == tip_bounds.upper) {
                    commandToolTipIndex = -1;
                }
            }
        }
        if (key == GLFW_KEY_ENTER) {
            std::cout << std::endl;
        }
        return key;
    }

    void GwPlot::removeBam(int index) {
        if (index >= (int) bams.size()) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " bam index is out of range. Use 0-based indexing\n";
            return;
        }
        collections.erase(std::remove_if(collections.begin(), collections.end(), [&index](const auto &col) {
            return col.bamIdx == index;
        }), collections.end());
        bams.erase(bams.begin() + index, bams.begin() + index + 1);
        indexes.erase(indexes.begin() + index, indexes.begin() + index + 1);
        headers.erase(headers.begin() + index, headers.begin() + index + 1);
        processed = false;
        redraw = true;
        inputText = "";
        imageCache.clear();
    }

    void GwPlot::removeTrack(int index) {
        if (index >= (int)tracks.size()) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " track index is out of range. Use 0-based indexing\n";
            return;
        }
        for (auto &rgn : regions) {
            rgn.featuresInView.clear();
            rgn.featureLevels.clear();
        }
        tracks[index].close();
        tracks.erase(tracks.begin() + index, tracks.begin() + index + 1);
        for (auto &trk: tracks) {
            trk.open(trk.path, true);
        }
        processed = false;
        redraw = true;
        inputText = "";
        imageCache.clear();
    }

    void GwPlot::removeRegion(int index) {
        regionSelection = 0;
        if (!regions.empty() && index < (int)regions.size()) {
            if (regions.size() == 1 && index == 0) {
                regions.clear();
            } else {
                regions.erase(regions.begin() + index);
            }
        } else {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " region index is out of range. Use 0-based indexing\n";
            return;
        }
        collections.erase(std::remove_if(collections.begin(), collections.end(), [&index](const auto col) {
            return col.regionIdx == index;
        }), collections.end());
        processed = false;
        redraw = true;
        inputText = "";
        imageCache.clear();
    }

    void GwPlot::highlightQname() { // todo make this more efficient
        for (auto &cl : collections) {
            for (auto &a: cl.readQueue) {
                if (bam_get_qname(a.delegate) == target_qname) {
                    a.edge_type = 4;
                    cl.skipDrawingReads = false;
                    cl.skipDrawingCoverage = false;
                }
            }
        }
    }

    bool GwPlot::commandProcessed() {
        // note setting valid = true sets redraw to true and processed to false, resulting in re-drawing and
        // re-collecting of reads
        Utils::rtrim(inputText);
        if (charIndex >= (int)inputText.size()) {
            charIndex = (int)inputText.size() - 1;
        }
        if (inputText.empty()) {
            return false;
        }
        bool valid = false;
        int reason = NONE;
        constexpr char delim = ' ';
        constexpr char delim_q = '\'';

        if (mode != SETTINGS) {
            commandHistory.push_back(inputText);
            commandIndex = (int)commandHistory.size();
        } else {
            if (opts.editing_underway) {
                Menu::processTextEntry(opts, inputText);
                imageCache.clear();
                return false;
            }
            if (textFromSettings) { // process this elsewhere
                return false;
            }
        }
        processText = false; // all command text will be processed below

        if (inputText == "q" || inputText == "quit") {
            triggerClose = true;
            redraw = false;
            processed = true;
            return false;
//            throw CloseException();
        } else if (inputText == ":" || inputText == "/") {
            inputText = "";
            return true;
        } else if (inputText == "help" || inputText == "h") {
            Term::help(opts);
            redraw = false;
            processed = true;
            inputText = "";
            return true;
        } else if (Utils::startsWith(inputText, "man ")) {
            inputText.erase(0, 4);
            Term::manuals(inputText);
            redraw = false;
            processed = true;
            inputText = "";
            return true;
        } else if (inputText == "refresh" || inputText == "r") {
            valid = true;
            imageCache.clear();
            filters.clear();
            target_qname = "";
            for (auto &cl: collections) { cl.vScroll = 0; }
        } else if (inputText == "link" || inputText == "link all") {
            opts.link_op = 2;
            imageCache.clear();
            HGW::refreshLinked(collections, opts, &samMaxY);
            redraw = true;
            processed = true;
            inputText = "";
            return true;
        } else if (inputText == "link sv") {
            opts.link_op = 1;
            imageCache.clear();
            HGW::refreshLinked(collections, opts, &samMaxY);
            redraw = true;
            processed = true;
            inputText = "";
            return true;
        } else if (inputText == "link none") {
            opts.link_op = 0;
            imageCache.clear();
            HGW::refreshLinked(collections, opts, &samMaxY);
            redraw = true;
            processed = true;
            inputText = "";
            return true;
        }
        else if (inputText == "line") {
            drawLine = !drawLine;
            redraw = false;
            processed = true;
            inputText = "";
            return true;
        } else if (Utils::startsWith(inputText, "count")) {
            std::string str = inputText;
            str.erase(0, 6);
            Parse::countExpression(collections, str, headers, bam_paths, (int)bams.size(), (int)regions.size());
            redraw = false;
            processed = true;
            inputText = "";
            return true;
        } else if (inputText == "settings" ) {
            last_mode = mode;
            mode = Show::SETTINGS;
            redraw = true;
            processed = true;
            inputText = "";
            return true;
        } else if (Utils::startsWith(inputText, "filter ")) {
            std::string str = inputText;
            str.erase(0, 7);
            for (auto &s: Utils::split(str, ';')) {
                Parse::Parser p = Parse::Parser();
                int rr = p.set_filter(s, (int)bams.size(), (int)regions.size());
                if (rr > 0) {
                    filters.push_back(p);
                    std::cout << inputText << std::endl;
                } else {
                    inputText = "";
                    return false;
                }
            }
            imageCache.clear();
            valid = true;

        } else if (inputText =="sam") {
            valid = true;
            if (!selectedAlign.empty()) {
                Term::printSelectedSam(selectedAlign);
            }
            redraw = false;
            processed = true;
            inputText = "";
            return true;
        } else if (Utils::startsWith(inputText, "tags")) {
            valid = true;
            if (!selectedAlign.empty()) {
                std::string str = inputText;
                str.erase(0, 4);
                std::vector<std::string> splitTags = Utils::split(str, delim);

                std::vector<std::string> split = Utils::split(selectedAlign, '\t');
                if (split.size() > 11) {
                    Term::clearLine();
                    std::cout << "\r";
                    int i = 0;
                    for (auto &s : split) {
                        if (i > 11) {
                            std::string t = s.substr(0, s.find(':'));
                            if (splitTags.empty()) {
                                std::string rest = s.substr(s.find(':'), s.size());
                                std::cout << termcolor::green << t << termcolor::reset << rest << "\t";
                            } else {
                                for (auto &target : splitTags) {
                                    if (target == t) {
                                        std::string rest = s.substr(s.find(':'), s.size());
                                        std::cout << termcolor::green << t << termcolor::reset << rest << "\t";
                                    }
                                }
                            }
                        }
                        i += 1;
                    }
                    std::cout << std::endl;
                }
            }
            redraw = false;
            processed = true;
            inputText = "";
            return true;
        } else if (Utils::startsWith(inputText, "f ") || Utils::startsWith(inputText, "find ")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            if (!target_qname.empty() && split.size() == 1) {
            } else if (split.size() == 2) {
                target_qname = split.back();
            } else {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " please provide one qname\n";
                inputText = "";
                return true;
            }
            highlightQname();
            redraw = true;
            processed = true;
            inputText = "";
            imageCache.clear();
            return true;

        } else if (Utils::startsWith(inputText, "ylim")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            try {
                if (!opts.tlen_yscale) {
                    opts.ylim = std::stoi(split.back());
                    samMaxY = opts.ylim;
                } else {
                    opts.max_tlen = std::stoi(split.back());
                    samMaxY = opts.max_tlen;
                }

                imageCache.clear();
                HGW::refreshLinked(collections, opts, &samMaxY);
                processed = true;
                redraw = true;
            } catch (...) {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " ylim invalid value\n";
            }
            inputText = "";
            return true;
        } else if (Utils::startsWith(inputText, "indel-length")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            try {
                opts.indel_length = std::stoi(split.back());
            } catch (...) {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " indel-length invalid value\n";
            }
            if (mode == SINGLE) {
                processed = true;
            } else {
                processed = false;
            }
            redraw = true;
            inputText = "";
            imageCache.clear();
            return true;
        }
        else if (inputText =="insertions" || inputText == "ins") {
            opts.small_indel_threshold = (opts.small_indel_threshold == 0) ? std::stoi(opts.myIni["view_thresholds"]["small_indel"]) : 0;
            if (mode == SINGLE) {
                processed = true;
            } else {
                processed = false;
            }
            redraw = true;
            inputText = "";
            imageCache.clear();
            return true;
        } else if (inputText =="mismatches" || inputText == "mm") {
            opts.snp_threshold = (opts.snp_threshold == 0) ? std::stoi(opts.myIni["view_thresholds"]["snp"]) : 0;
            if (mode == SINGLE) {
                processed = true;
                for (auto &cl : collections) {
                    cl.skipDrawingReads = false;
                    cl.skipDrawingCoverage = false;
                }
            } else {
                processed = false;
            }
            redraw = true;
            inputText = "";
            imageCache.clear();
            return true;
        } else if (inputText =="edges") {
            opts.edge_highlights = (opts.edge_highlights == 0) ? std::stoi(opts.myIni["view_thresholds"]["edge_highlights"]) : 0;
            if (mode == SINGLE) {
                processed = true;
                for (auto & cl: collections) {
                    cl.skipDrawingReads = false;
                    cl.skipDrawingCoverage = true;
                }
            } else {
                processed = false;
            }
            redraw = true;
            inputText = "";
            imageCache.clear();
            return true;
        } else if (inputText =="soft-clips" || inputText == "sc") {
            opts.soft_clip_threshold = (opts.soft_clip_threshold == 0) ? std::stoi(opts.myIni["view_thresholds"]["soft_clip"]) : 0;
            if (mode == SINGLE) {
                processed = true;
            } else {
                processed = false;
            }
            redraw = true;
            inputText = "";
            imageCache.clear();
            return true;
        } else if (Utils::startsWith(inputText, "remove ") || Utils::startsWith(inputText, "rm ")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            int ind = 0;
            if (Utils::startsWith(split.back(), "bam")) {
                split.back().erase(0, 3);
                try {
                    ind = std::stoi(split.back());
                } catch (...) {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " bam index not understood\n";
                    inputText = "";
                    return true;
                }
                removeBam(ind);
                return true;
            } else if (Utils::startsWith(split.back(), "track")) {
                split.back().erase(0, 5);
                try {
                    ind = std::stoi(split.back());
                } catch (...) {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " track index not understood\n";
                    inputText = "";
                    return true;
                }
                removeTrack(ind);
                return true;
            } else {
                try {
                    ind = std::stoi(split.back());
                } catch (...) {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " region index not understood\n";
                    inputText = "";
                    return true;
                }
                removeRegion(ind);
                return true;
            }

            bool clear_filters = false; // removing a region can invalidate indexes so remove them
            for (auto &f : filters) {
                if (!f.targetIndexes.empty()) {
                    clear_filters = true;
                    break;
                }
            }
            if (clear_filters) {
                filters.clear();
            }
        } else if (Utils::startsWith(inputText, "cov")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            if (split.size() > 2) {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " cov must be either 'cov' to toggle coverage or 'cov NUMBER' to set max coverage\n";
                inputText = "";
                return true;
            }
            else if (split.size() == 1) {
                if (opts.max_coverage == 0) {
                    opts.max_coverage = 10000000;
                } else {
                    opts.max_coverage = 0;
                }

            } else {
                try {
                    opts.max_coverage = std::stoi(split.back());
                } catch (...) {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " 'cov NUMBER' not understood\n";
                    return true;
                }
            }
            opts.max_coverage = std::max(0, opts.max_coverage);

            for (auto &cl : collections) {
                cl.skipDrawingReads = false;
                cl.skipDrawingCoverage = false;
            }
            redraw = true;
            processed = false;

            inputText = "";
            imageCache.clear();
            return true;
        }
        else if (inputText == "log2-cov") {
            opts.log2_cov = !(opts.log2_cov);
            redraw = true;
            if (mode == SINGLE) {
                processed = true;
                for (auto &cl : collections) {
                    cl.skipDrawingReads = false;
                    cl.skipDrawingCoverage = false;
                }
            } else {
                processed = false;
            }
            inputText = "";
            imageCache.clear();
            return true;
        }
        else if (inputText == "expand-tracks") {
            opts.expand_tracks = !(opts.expand_tracks);
            redraw = true;
            if (mode == SINGLE) {
                processed = true;
            } else {
                processed = false;
            }
            inputText = "";
            imageCache.clear();
            return true;
//        } else if (inputText == "low-mem") {
//            opts.low_mem = !(opts.low_mem);
//            redraw = false;
//            if (mode == SINGLE) {
//                processed = true;
//            } else {
//                processed = false;
//            }
//            inputText = "";
//            std::cout << "Low memory mode " << ((opts.low_mem) ? "on" : "off") << std::endl;
//            return true;
        } else if (Utils::startsWith(inputText, "mate")) {
            std::string mate;
            Utils::parseMateLocation(selectedAlign, mate, target_qname);
            if (mate.empty()) {
	            std::cerr << termcolor::red << "Error:" << termcolor::reset << " could not parse mate location\n";
                inputText = "";
                return true;
            }
            if (regionSelection >= 0 && regionSelection < (int) regions.size()) {
                if (inputText == "mate") {
                    regions[regionSelection] = Utils::parseRegion(mate);
                    processed = false;
                    for (auto &cl: collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.region = &regions[regionSelection];
                            cl.readQueue.clear();
                            cl.covArr.clear();
                            cl.levelsStart.clear();
                            cl.levelsEnd.clear();
                        }
                    }
                    processBam();
                    highlightQname();
                    redraw = true;
                    if (mode == SINGLE) {
                        processed = true;
                    } else {
                        processed = false;
                    }
                    inputText = "";
                    imageCache.clear();
                    return true;
                } else if (inputText == "mate add" && mode == SINGLE) {
                    regions.push_back(Utils::parseRegion(mate));
                    fetchRefSeq(regions.back());
                    processed = false;
                    processBam();
                    highlightQname();
                    redraw = true;
                    processed = true;
                    inputText = "";
                    imageCache.clear();
                    return true;
                }
            }
        } else if (Utils::startsWith(inputText, "theme")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            if (split.size() != 2) {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " theme must be either 'igv', 'dark' or 'slate'\n";
                inputText = "";
                return true;
            }
            if (split.back() == "dark") {
                opts.theme = Themes::DarkTheme();  opts.theme.setAlphas(); valid = true; imageCache.clear(); opts.theme_str = "dark";
                imageCache.clear();
            } else if (split.back() == "igv") {
                opts.theme = Themes::IgvTheme(); opts.theme.setAlphas(); valid = true; imageCache.clear(); opts.theme_str = "igv";
                imageCache.clear();
            } else if (split.back() == "slate") {
                opts.theme = Themes::SlateTheme(); opts.theme.setAlphas(); valid = true; imageCache.clear(); opts.theme_str = "slate";
                imageCache.clear();
            } else {
                valid = false;
                reason = OPTION_NOT_UNDERSTOOD;
                error_report(reason);
            }
        }
        else if (inputText == "tlen-y") {
            if (!opts.tlen_yscale) {
                opts.max_tlen = 2000;
                opts.ylim = 2000;
                samMaxY = 2000;
            } else {
                opts.ylim = 60;
                samMaxY = 60;
            }
            opts.tlen_yscale = !opts.tlen_yscale;
            std::cerr << opts.max_tlen << std::endl;
            valid = true;
//            samMaxY = opts.ylim;
            //opts.max_tlen = samMaxY;
//        }
//        else if (Utils::startsWith(inputText, "tlen-y")) {
//            valid = true;
//            std::vector<std::string> split = Utils::split(inputText, delim);
//            if (split.size() == 2) {
//                try {
//                    opts.max_tlen = std::stoi(split.back());
//                    opts.tlen_yscale = true;
//                } catch (...) {
//                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " 'tlen-y NUMBER' not understood\n";
//                    return true;
//                }
//            } else {
//                opts.tlen_yscale = !(opts.tlen_yscale);
//                if (!opts.tlen_yscale) {
//                    samMaxY = opts.ylim;
//                }
//            }
        } else if (Utils::startsWith(inputText, "goto")) {
            std::vector<std::string> split = Utils::split(inputText, delim_q);
            if (split.size() == 1) {
                split = Utils::split(inputText, delim);
            }
            if (split.size() > 1 && split.size() < 4) {
                int index = regionSelection;
                Utils::Region rgn;
                try {
                    rgn = Utils::parseRegion(split[1]);
                    int res = faidx_has_seq(fai, rgn.chrom.c_str());
                    if (res <= 0) {
                        valid = false;
                        reason = CHROM_NOT_IN_REFERENCE;
                    } else {
                        valid = true;
                    }
                } catch (...) {
                    valid = false;
                    reason = BAD_REGION;
                }
                if (valid) {
                    if (mode != SINGLE) { mode = SINGLE; }
                    if (regions.empty()) {
                        regions.push_back(rgn);
                        fetchRefSeq(regions.back());
                    } else {
                        if (index < (int)regions.size()) {
                            if (regions[index].chrom == rgn.chrom) {
                                rgn.markerPos = regions[index].markerPos;
                                rgn.markerPosEnd = regions[index].markerPosEnd;
                            }
                            regions[index] = rgn;
                            fetchRefSeq(regions[index]);
                        }
                    }
                } else {  // search all tracks for matching name, slow but ok for small tracks
                    if (!tracks.empty()) {
                        bool res = HGW::searchTracks(tracks, split[1], rgn);
                        if (res) {
                            valid = true;
                            if (mode != SINGLE) { mode = SINGLE; }
                            if (regions.empty()) {
                                regions.push_back(rgn);
                                fetchRefSeq(regions.back());
                            } else {
                                if (index < (int) regions.size()) {
                                    regions[index] = rgn;
                                    fetchRefSeq(regions[index]);
                                    valid = true;
                                }
                            }
                        } else {
                            reason = GENERIC;
                            valid = false;
                        }
                    }
                }
                if (valid) {
                    redraw = true;
                    processed = false;
                } else {
                    error_report(reason);
                }
                inputText = "";
                return true;
            }
        } else if (Utils::startsWith(inputText, "grid")) {
            try {
                std::vector<std::string> split = Utils::split(inputText, ' ');
                opts.number = Utils::parseDimensions(split[1]);
                valid = true;
            } catch (...) {
                valid = false;
            }
        } else if (Utils::startsWith(inputText, "add") && mode != TILED)  {

            if (mode != SINGLE) { mode = SINGLE; }
            std::vector<std::string> split = Utils::split(inputText, delim_q);
            if (split.size() == 1) {
                split = Utils::split(inputText, delim);
            }
            if (split.size() > 1) {
                for (int i=1; i < (int)split.size(); ++i) {
                    try {
                        Utils::Region dummy_region = Utils::parseRegion(split[1]);
                        int res = faidx_has_seq(fai, dummy_region.chrom.c_str());
                        if (res <= 0) {
                            valid = false;
                            reason = GENERIC;
                            if (inputText == "add mate") {
                                std::cerr << "Did you mean to use the 'mate add' function instead?\n";
                            }
                        } else {
                            regions.push_back(dummy_region);
                            fetchRefSeq(regions.back());
                            valid = true;
                        }
                    } catch (...) {
                        std::cerr << termcolor::red << "Error parsing :add" << termcolor::reset;
                        inputText = "";
                        return true;
                    }
                }
            } else {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " expected a Region e.g. chr1:1-20000\n";
                inputText = "";
                return true;
            }
        } else if (inputText == "v" || Utils::startsWith(inputText, "var") || Utils::startsWith(inputText, "v ")) {
            if (variantTracks.empty()) {
                inputText = "";
                return true;
            }
            currentVarTrack = &variantTracks[variantFileSelection];
            if (currentVarTrack->multiLabels.empty()) {
	            std::cerr << termcolor::red << "Error:" << termcolor::reset << " no variant loaded\n";
                inputText = "";
                processed = true;
                redraw = false;
                return true;
            } else if (currentVarTrack->blockStart+mouseOverTileIndex >= (int)currentVarTrack->multiLabels.size() || mouseOverTileIndex == -1) {
                inputText = "";
                processed = true;
                redraw = false;
                return true;
            }
			std::vector<std::string> split = Utils::split(inputText, delim);
            Utils::Label &lbl = currentVarTrack->multiLabels[currentVarTrack->blockStart + mouseOverTileIndex];
            Term::clearLine();
			if (currentVarTrack->type == HGW::TrackType::VCF) {
                currentVarTrack->vcf.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
				std::string variantStringCopy = currentVarTrack->vcf.variantString;
                currentVarTrack->vcf.get_samples();
				std::vector<std::string> sample_names_copy = currentVarTrack->vcf.sample_names;
                if (variantStringCopy.empty()) {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " could not parse vcf/bcf line";
                } else {
					int requests = (int)split.size();
					if (requests == 1) {
                        Term::clearLine();
                        std::cout << "\r" << variantStringCopy << std::endl;
					} else {
						std::string requestedVars;
						std::vector<std::string> vcfCols = Utils::split(variantStringCopy, '\t');
						for (int i = 1; i < requests; ++i) {
							std::string result;
                            try {
                                Parse::parse_vcf_split(result, vcfCols, split[i], sample_names_copy);
                            } catch (...) {
                                std::cerr << termcolor::red << "Error:" << termcolor::reset << " could not parse " << split[i] << std::endl;
                                break;
                            }
							if (i != requests-1) {
								requestedVars += split[i]+": "+result+"\t";
							} else {
								requestedVars += split[i]+": "+result;
							}
						}
                        if (!requestedVars.empty()) {
                            Term::clearLine();
                            std::cout << "\r" << requestedVars << std::endl;
                        }
					}
				}
                valid = true;
			} else {
                currentVarTrack->variantTrack.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
				if (currentVarTrack->variantTrack.variantString.empty()) {
                    Term::clearLine();
                    std::cout << "\r" << currentVarTrack->variantTrack.variantString << std::endl;
				} else {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " could not parse variant line";
                }
			}
            valid = true;

		} else if (inputText == "s" || Utils::startsWith(inputText, "snapshot") || Utils::startsWith(inputText, "s ")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            if (split.size() > 2) {
                valid = false;
            } else {
                std::string fname;
                currentVarTrack = &variantTracks[variantFileSelection];
                if (split.size() == 1) {
                    if (mode == Show::SINGLE) {
                        std::filesystem::path fname_path = Utils::makeFilenameFromRegions(regions);
#if defined(_WIN32) || defined(_WIN64)
                        const wchar_t* pc = fname_path.filename().c_str();
                        std::wstring ws(pc);
                        std::string p(ws.begin(), ws.end());
                        fname = p;
#else
                        fname = fname_path.filename();
#endif
                    } else if (currentVarTrack != nullptr) {
                        fname = "index_" + std::to_string(currentVarTrack->blockStart) + "_" +
                                std::to_string(opts.number.x * opts.number.y) + ".png";
                    }
                } else {
                    std::string nameFormat = split[1];
                    if (currentVarTrack->type == HGW::TrackType::VCF && mode == Show::SINGLE) {
                        if (mouseOverTileIndex == -1 || currentVarTrack->blockStart + mouseOverTileIndex > (int) currentVarTrack->multiLabels.size()) {
                            inputText = "";
                            redraw = true;
                            processed = false;
                            return true;
                        }
                        Utils::Label &lbl = currentVarTrack->multiLabels[currentVarTrack->blockStart + mouseOverTileIndex];
                        currentVarTrack->vcf.get_samples();
                        std::vector<std::string> sample_names_copy = currentVarTrack->vcf.sample_names;
                        currentVarTrack->vcf.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
                        std::string variantStringCopy = currentVarTrack->vcf.variantString;
                        if (!variantStringCopy.empty() && variantStringCopy[variantStringCopy.length()-1] == '\n') {
                            variantStringCopy.erase(variantStringCopy.length()-1);
                        }
                        std::vector<std::string> vcfCols = Utils::split(variantStringCopy, '\t');
                        try {
                            Parse::parse_output_name_format(nameFormat, vcfCols, sample_names_copy, bam_paths,lbl.current());
                        } catch (...) {
                            std::cerr << termcolor::red << "Error:" << termcolor::reset
                                      << " could not parse " << nameFormat << std::endl;
                            inputText = "";
                            redraw = true;
                            processed = false;
                            return true;
                        }
                    }
                    fname = nameFormat;
                    Utils::trim(fname);
                }
                std::filesystem::path outdir = opts.outdir;
                std::filesystem::path fname_path(fname);
                std::filesystem::path out_path = outdir / fname_path;
                if (!std::filesystem::exists(out_path.parent_path()) && !out_path.parent_path().empty()) {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " path not found " << out_path.parent_path() << std::endl;
                } else {
                    if (!imageCacheQueue.empty()) {
                        Manager::imagePngToFile(imageCacheQueue.back().second, out_path.string());
                        Term::clearLine();
                        std::cout << "\rSaved to " << out_path << std::endl;
                    }
                }
                valid = true;
            }
        } else if (Utils::startsWith(inputText, "online")) {
            if (regions.empty()) {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " please navigate to a region first" << std::endl;
                redraw = false;
                processed = true;
                inputText = "";
                return false;
            }
            std::vector<std::string> split = Utils::split(inputText, delim);
            std::string genome_tag;
            if (opts.genome_tag.empty() && split.size() >= 2) {
                genome_tag = split[1];
            } else {
                genome_tag = opts.genome_tag;
            }
            Term::printOnlineLinks(tracks, regions[regionSelection], genome_tag);
            redraw = false;
            processed = true;
            inputText = "";
            return true;

        } else {
	        Utils::Region rgn;
			try {
				rgn = Utils::parseRegion(inputText);
				valid = true;
			} catch (...) {
				valid = false;
                reason = BAD_REGION;
			}
	        if (valid) {
		        int res = faidx_has_seq(fai, rgn.chrom.c_str());
		        if (res <= 0) {
			        valid = false;
                    reason = GENERIC;
		        }
			}
			if (valid) {
                if (mode != SINGLE) { mode = SINGLE; }
                if (regions.empty()) {
                    regions.push_back(rgn);
                    fetchRefSeq(regions.back());
                } else {
                    if (regions[regionSelection].chrom == rgn.chrom) {
                        rgn.markerPos = regions[regionSelection].markerPos;
                        rgn.markerPosEnd = regions[regionSelection].markerPosEnd;
                    }
                    regions[regionSelection] = rgn;
                    fetchRefSeq(regions[regionSelection]);
                }
	        } else {  // search all tracks for matching name, slow but ok for small tracks
                if (!tracks.empty()) {
                    bool res = HGW::searchTracks(tracks, inputText, rgn);
                    if (res) {
                        valid = true;
                        reason = NONE;
                        if (mode != SINGLE) { mode = SINGLE; }
                        if (regions.empty()) {
                            regions.push_back(rgn);
                            fetchRefSeq(regions.back());
                        } else {
                            if (regionSelection  < (int) regions.size()) {
                                regions[regionSelection] = rgn;
                                fetchRefSeq(regions[regionSelection]);
                            }
                        }
                    } else {
                        valid = false;
                        reason = GENERIC;
                    }
                }
            }
        }
        if (valid) {
            redraw = true;
            processed = false;
        } else {
            std::cout << inputText << "\n\n";
//            error_report(reason);
        }
        inputText = "";
        return true;
    }

    void GwPlot::printIndexInfo() {
        int term_width = Utils::get_terminal_width() - 1;
        Term::clearLine();
        std::string i_str = "\rIndex   ";
        if (term_width <= (int)i_str.size()) {
            return;
        }
        Term::clearLine();
        std::cout << termcolor::bold << i_str << termcolor::reset;
        term_width -= (int)i_str.size();
        int blockStart = currentVarTrack->blockStart;

        std::string ind;
        if (opts.number.x * opts.number.y > 1) {
            ind = std::to_string(blockStart) + "-" + std::to_string(blockStart + (opts.number.x * opts.number.y) - 1);
        } else {
            ind = std::to_string(blockStart);
        }

        if (term_width <= (int)ind.size()) {
            std::cout << std::flush;
            return;
        }
        std::cout << ind;
        term_width -= (int)ind.size();

        if ((int)currentVarTrack->multiRegions.size() <= blockStart) {
            std::cout << std::flush;
            return;
        }
        Utils::Region &start_region = currentVarTrack->multiRegions[blockStart].front();
        std::string chrom = start_region.chrom;
        int start = start_region.start;

        std::string region_str1 = "   " + chrom + ":" + std::to_string(start);
        if (term_width <= (int)region_str1.size()) {
            std::cout << std::flush;
            return;
        }
        std::cout << region_str1;
        term_width -= (int)region_str1.size();

        if ((int)currentVarTrack->multiRegions.size() <= blockStart + (opts.number.x * opts.number.y) - 1) {
            std::cout << std::flush;
            return;
        }

        Utils::Region &end_region = currentVarTrack->multiRegions[blockStart + (opts.number.x * opts.number.y) - 1].front();
        int end = end_region.end;
        std::string region_str2 = + "-" + std::to_string(end);
        if (term_width <= (int)region_str2.size()) {
            std::cout << std::flush;
            return;
        }
        std::cout << region_str2;
        std::cout << std::flush;
    }

    int GwPlot::printRegionInfo() {
        int term_width = Utils::get_terminal_width() - 1;
        if (regions.empty()) {
            std::cout << "\r" << std::flush;
            return term_width;
        }
        std::string pos_str = "\rPos     ";
        if (term_width <= (int)pos_str.size()) {
            return term_width;
        }
        Term::clearLine();
        std::cout << termcolor::bold << pos_str << termcolor::reset;
        term_width -= (int)pos_str.size();

        auto r = regions[regionSelection];
        std::string region_str = r.chrom + ":" + std::to_string(r.start + 1) + "-" + std::to_string(r.end + 1);
        if (term_width <= (int)region_str.size()) {
            std::cout << std::flush;
            return term_width;
        }
        std::cout << termcolor::cyan << region_str << termcolor::white;
        term_width -= (int)region_str.size();

        std::string size_str = "  (" + Utils::getSize(r.end - r.start) + ")";
        if (term_width <= (int)size_str.size()) {
            std::cout << std::flush;
            return term_width;
        }
        std::cout << size_str;
        term_width -= (int)size_str.size();
        std::cout << termcolor::reset << std::flush;
        return term_width;
    }

    void GwPlot::updateSettings() {
        mode = last_mode;
        redraw = true;
        processed = false;
        imageCache.clear();
        inputText = "";
        opts.editing_underway = false;
        textFromSettings = false;
        samMaxY = opts.ylim;
        //fonts.setTypeface(opts.font_str, opts.font_size);
        fonts = Themes::Fonts();
        fonts.setTypeface(opts.font_str, opts.font_size);
        if (opts.myIni.get("genomes").has(opts.genome_tag) && reference != opts.myIni["genomes"][opts.genome_tag]) {
            faidx_t *fai_test = fai_load( opts.myIni["genomes"][opts.genome_tag].c_str());
            if (fai_test != nullptr) {
                reference =  opts.myIni["genomes"][opts.genome_tag];
                fai = fai_load(reference.c_str());
                for (auto &bm: bams) {
                    hts_set_fai_filename(bm, reference.c_str());
                }
                std::cerr << termcolor::bold << "\n" << opts.genome_tag << termcolor::reset << " loaded from " << reference << std::endl;
            } else {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " could not open " << opts.myIni["genomes"][opts.genome_tag].c_str() << std::endl;
            }
            fai_destroy(fai_test);
        }
        if (opts.myIni.get("tracks").has(opts.genome_tag)) {
            std::vector<std::string> track_paths_temp = Utils::split(opts.myIni["tracks"][opts.genome_tag], ',');
            for (auto &trk_item : track_paths_temp) {
                if (!Utils::is_file_exist(trk_item)) {
                    std::cerr << "Warning: track file does not exists - " << trk_item << std::endl;
                } else {
                    bool already_loaded = false;
                    for (const auto &current_track : tracks) {
                        if (current_track.path == trk_item) {
                            already_loaded = true;
                            break;
                        }
                    }
                    if (!already_loaded) {
                        tracks.resize(tracks.size() + 1);
                        tracks.back().open(trk_item, true);
                    }
                }
            }
        } else if (!tracks.empty() && !opts.genome_tag.empty()) {
            std::string &genome_tag = opts.genome_tag;
            tracks.erase(std::remove_if(tracks.begin(), tracks.end(),
                                        [&genome_tag] (HGW::GwTrack &trk) { return (trk.genome_tag == genome_tag); } ),
                        tracks.end());
        }
        pool.reset(opts.threads);
    }

    void convertScreenCoordsToFrameBufferCoords(GLFWwindow *wind, double *xPos, double *yPos, int fb_width, int fb_height) {
        int windowW, windowH;
        glfwGetWindowSize(wind, &windowW, &windowH);
        if (fb_width > windowW) {
            *xPos *= (double) fb_width / (double) windowW;
            *yPos *= (double) fb_height / (double) windowH;
        }
    }

    void GwPlot::keyPress(GLFWwindow *wind, int key, int scancode, int action, int mods) {
        // Decide if the key is part of a user input command (inputText) or a request to process a command / refresh screen
        // Of note, mouseButton events may be translated into keyPress events and processed here
        // For example, clicking on a commands from the menu pop-up will translate into a keyPress ENTER and
        // processed using registerKey
        key = registerKey(window, key, scancode, action, mods);
        if (key == GLFW_KEY_UNKNOWN || captureText) {
            return;
        }
        // text based commands from the user are stored in inputText and handled here
        try {
            if (commandProcessed()) {
                return;
            }
        } catch (CloseException & mce) {
            glfwSetWindowShouldClose(wind, GLFW_TRUE);
        }
        // key events concerning the menu are handled here
        if (mode != Show::SETTINGS && key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            last_mode = mode;
            mode = Show::SETTINGS;
            opts.menu_table = Themes::MenuTable::MAIN;
            return;
        } else if (mode == Show::SETTINGS && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
            if (key == GLFW_KEY_ESCAPE && opts.menu_table == Themes::MenuTable::MAIN) {
                mode = last_mode;
                redraw = true;
                processed = false;
                updateSettings();
            } else {
                bool keep_alive = Menu::navigateMenu(opts, key, action, inputText, &charIndex, &captureText, &textFromSettings, &processText, reference);
                if (opts.editing_underway) {
                    textFromSettings = true;
                }
                if (!keep_alive) {
                    updateSettings();
                }
            }
            return;
        }
        // Navigation key events are handled below
        if (mode == Show::SINGLE) {
            if (regions.empty() || regionSelection < 0) {
                return;
            }

            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                Utils::Region &region = regions[regionSelection];
                if (key == opts.scroll_right) {
                    int shift = (int)(((float)region.end - (float)region.start) * opts.scroll_speed);
                    if (region.refSeq != nullptr) {
                        delete region.refSeq;
                    }
                    region.start = region.start + shift;
                    region.end = region.end + shift;
                    fetchRefSeq(region);
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.region = &regions[regionSelection];
                            if (!bams.empty()) {
                                cl.skipDrawingReads = false;
                                cl.skipDrawingCoverage = false;
                                if (cl.regionLen < opts.low_memory) {
                                    HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx],
                                                                indexes[cl.bamIdx], opts, (bool)opts.max_coverage, false,
                                                                &samMaxY, filters, pool);
                                    processed = true;
                                    redraw = true;
                                } else {
                                    processed = false;
                                    redraw = true;
                                }
                            } else {
                                processed = false;
                                redraw = true;
                            }
                        }
                    }
                    printRegionInfo();

                } else if (key == opts.scroll_left) {
                    int shift = (int)(((float)region.end - (float)region.start) * opts.scroll_speed);
                    shift = (region.start - shift > 0) ? shift : 0;
                    if (shift == 0) {
                        return;
                    }
                    if (region.refSeq != nullptr) {
                        delete region.refSeq;
                    }
                    region.start = region.start - shift;
                    region.end = region.end - shift;
                    fetchRefSeq(region);
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.region = &regions[regionSelection];
                            if (!bams.empty()) {
                                cl.skipDrawingReads = false;
                                cl.skipDrawingCoverage = false;
                                if (cl.regionLen < opts.low_memory) {
                                    HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx],
                                                                indexes[cl.bamIdx], opts, (bool) opts.max_coverage,
                                                                true,
                                                                &samMaxY, filters, pool);
                                    processed = true;
                                    redraw = true;
                                } else {
                                    processed = false;
                                    redraw = true;
                                }
                            } else {
                                processed = false;
                                redraw = true;
                            }
                        }
                    }
                    printRegionInfo();

                } else if (key == opts.zoom_out) {
                    int shift = (int)((((float)region.end - (float)region.start) * opts.scroll_speed)) + 10;
                    int shift_left = (region.start - shift > 0) ? shift : region.start;
                    if (shift == 0) {
                        return;
                    }
                    if (region.refSeq != nullptr) {
                        delete region.refSeq;
                    }
                    region.start = region.start - shift_left;
                    region.end = region.end + shift;
                    fetchRefSeq(region);
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.region = &regions[regionSelection];
                            cl.collection_processed = false;
                            if (!bams.empty()) {
                                cl.skipDrawingReads = false;
                                cl.skipDrawingCoverage = false;
                                if (cl.regionLen < opts.low_memory && region.end - region.start < opts.low_memory) {
                                    HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx], indexes[cl.bamIdx],
                                                                opts, false, true,  &samMaxY, filters, pool);
                                    HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx], indexes[cl.bamIdx],
                                                                opts, false, false, &samMaxY, filters, pool);
                                    if (opts.max_coverage) {  // re process coverage for all reads
                                        cl.covArr.resize(cl.region->end - cl.region->start + 1);
                                        std::fill(cl.covArr.begin(), cl.covArr.end(), 0);
                                        int l_arr = (int) cl.covArr.size() - 1;
                                        for (auto &i: cl.readQueue) {
                                            Segs::addToCovArray(cl.covArr, i, cl.region->start, cl.region->end, l_arr);
                                        }
                                        if (opts.snp_threshold > cl.region->end - cl.region->start) {
                                            cl.mmVector.resize(cl.region->end - cl.region->start + 1);
                                            Segs::Mismatches empty_mm = {0, 0, 0, 0};
                                            std::fill(cl.mmVector.begin(), cl.mmVector.end(), empty_mm);
                                        } else {
                                            cl.mmVector.clear();
                                        }
                                    }
                                    processed = true;
                                    redraw = true;
                                } else {
                                    processed = false;
                                    redraw = true;
                                }
                            } else {
                                processed = false;
                                redraw = true;
                            }
                        }
                    }
                    if (opts.link_op != 0) {
                        HGW::refreshLinked(collections, opts, &samMaxY);
                    }
                    printRegionInfo();

                } else if (key == opts.zoom_in) {
                    if (region.end - region.start > 50) {
                        int shift = (int)(((float)region.end - (float)region.start) * opts.scroll_speed);
                        if (region.refSeq != nullptr) {
                            delete region.refSeq;
                        }
                        region.start = region.start + shift;
                        region.end = region.end - shift;
                        fetchRefSeq(region);
                        for (auto &cl : collections) {
                            if (cl.regionIdx == regionSelection) {
                                if (!bams.empty() && cl.regionLen >= opts.low_memory && region.end - region.start < opts.low_memory) {
                                    cl.clear();
                                    HGW::collectReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx], indexes[cl.bamIdx], opts.threads, &region, (bool)opts.max_coverage, filters, pool);
                                    int maxY = Segs::findY(cl, cl.readQueue, opts.link_op, opts, &region, false);
                                    if (maxY > samMaxY) {
                                        samMaxY = maxY;
                                    }
                                } else {
                                    if (!bams.empty()) {
                                        HGW::trimToRegion(cl, opts.max_coverage, opts.snp_threshold);
                                    }
                                }
                                cl.skipDrawingReads = false;
                                cl.skipDrawingCoverage = false;
                                cl.region = &regions[regionSelection];
                                cl.collection_processed = false;
                            }
                        }
                        if (bams.empty()) {
                            processed = false;
                            redraw = true;
                        } else {
                            processed = true;
                            redraw = true;
                        }

                        if (opts.link_op != 0) {
                            HGW::refreshLinked(collections, opts, &samMaxY);
                        }
                        printRegionInfo();
                    }
                } else if (key == opts.next_region_view) {
                    if (regions.size() <= 1) { return; }
                    regionSelection += 1;
                    if (regionSelection >= (int)regions.size()) {
                        regionSelection = 0;
                    }
                    std::cout << "\nRegion    " << regionSelection << std::endl;
                    regionSelectionTriggered = true;
                    regionTimer = std::chrono::high_resolution_clock::now();
                } else if (key == opts.previous_region_view) {
                    if (regions.size() <= 1) { return; }
                    regionSelection -= 1;
                    if (regionSelection < 0) {
                        regionSelection = (int)regions.size() - 1;
                    }
                    std::cout << "\nRegion    " << regionSelection << std::endl;
                    regionSelectionTriggered = true;
                    regionTimer = std::chrono::high_resolution_clock::now();
                } else if (key == opts.scroll_down) {
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.vScroll += 2;
                            cl.levelsStart.clear();
                            cl.levelsEnd.clear();
                            cl.linked.clear();
                            cl.skipDrawingReads = false;
                            cl.skipDrawingCoverage = false;
                            for (auto &itm: cl.readQueue) { itm.y = -1; }
                            int maxY = Segs::findY(cl, cl.readQueue, opts.link_op, opts, cl.region,  false);
                            samMaxY = (maxY > samMaxY || opts.tlen_yscale) ? maxY : samMaxY;
                        }
                    }
                    redraw = true;
                    processed = true;
                } else if (key == opts.scroll_up) {
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            if (opts.tlen_yscale) {
                                cl.vScroll -= 2;
                            } else {
                                cl.vScroll = (cl.vScroll - 2 <= 0) ? 0 : cl.vScroll - 2;
                            }
                            cl.levelsStart.clear();
                            cl.levelsEnd.clear();
                            cl.linked.clear();
                            cl.skipDrawingReads = false;
                            cl.skipDrawingCoverage = false;
                            for (auto &itm: cl.readQueue) { itm.y = -1; }
                            int maxY = Segs::findY(cl, cl.readQueue, opts.link_op, opts, cl.region, false);
                            samMaxY = (maxY > samMaxY || opts.tlen_yscale) ? maxY : samMaxY;
                        }
                    }
                    redraw = true;
                    processed = true;
                } else if (key == opts.find_alignments && !selectedAlign.empty()) {
                    std::string qname = Utils::split(selectedAlign, '\t')[0];
                    inputText = "find " + qname;
                    commandProcessed();
                }
            }
        } else if (mode == Show::TILED) {
            currentVarTrack = &variantTracks[variantFileSelection];
            int bLen = opts.number.x * opts.number.y;
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                if (key == opts.scroll_right) {
                    size_t currentSize = (currentVarTrack->image_glob.empty()) ? currentVarTrack->multiRegions.size() : currentVarTrack->image_glob.size();
                    if (currentVarTrack->type == HGW::TrackType::IMAGES && currentVarTrack->blockStart + bLen < (int)currentSize) {
                        currentVarTrack->blockStart += bLen;
                        redraw = true;
                    }
                    else if (!*currentVarTrack->trackDone) {
                        currentVarTrack->blockStart += bLen;
                        redraw = true;
                    }
                } else if (key == opts.scroll_left) {
                    if (currentVarTrack->blockStart == 0) {
                        return;
                    }
                    currentVarTrack->blockStart = (currentVarTrack->blockStart - bLen > 0) ? currentVarTrack->blockStart - bLen : 0;
                    redraw = true;
                } else if (key == opts.zoom_out) {
                    opts.number.x += 1;
                    opts.number.y += 1;
                    redraw = true;
                } else if (key == opts.zoom_in) {
                    opts.number.x = (opts.number.x - 1 > 0) ? opts.number.x - 1 : 1;
                    opts.number.y = (opts.number.y - 1 > 0) ? opts.number.y - 1 : 1;
                    redraw = true;
                } else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9 && mouseOverTileIndex >= 0) {
                    int num_idx = key - (int)GLFW_KEY_1;
                    if (currentVarTrack->multiLabels.empty() || currentVarTrack->blockStart + mouseOverTileIndex > (int)currentVarTrack->multiLabels.size()) {
                        return;
                    }
                    Utils::Label &lbl = currentVarTrack->multiLabels[currentVarTrack->blockStart + mouseOverTileIndex];
                    if (num_idx < (int)lbl.labels.size()) {
                        redraw = true;
                        lbl.clicked = true;
                        lbl.i = num_idx;
                    }
                }
            }
        }

        if (key == opts.cycle_link_mode && action == GLFW_PRESS) {
            if (opts.link_op == 2) {
                opts.link_op = 0;
            } else {
                opts.link_op += 1;
            }
            std::string lk = (opts.link_op > 0) ? ((opts.link_op == 1) ? "sv" : "all") : "none";
            std::cout << "\nLinking selection " << lk << std::endl;
            imageCache.clear();
            HGW::refreshLinked(collections, opts, &samMaxY);
            redraw = true;
        }
    }

    void GwPlot::addTrack(std::string &path, bool print_message=true) {
        bool good = false;
        if (Utils::endsWith(path, ".bam") || Utils::endsWith(path, ".cram")) {
            good = true;
            if (print_message) {
                std::cout << termcolor::magenta << "\nAlignments  " << termcolor::reset << path << "\n";
            }
            bam_paths.push_back(path);
            htsFile* f = sam_open(path.c_str(), "r");
            hts_set_threads(f, opts.threads);
            bams.push_back(f);
            sam_hdr_t *hdr_ptr = sam_hdr_read(f);
            headers.push_back(hdr_ptr);
            hts_idx_t* idx = sam_index_load(f, path.c_str());
            indexes.push_back(idx);
        } else if (!opts.vcf_as_tracks && (Utils::endsWith(path, ".vcf.gz") || Utils::endsWith(path, ".vcf") || Utils::endsWith(path, ".bcf"))) {
            good = true;
            std::vector<std::string> labels = Utils::split(opts.labels, ',');
            setLabelChoices(labels);
            mouseOverTileIndex = 0;
            bboxes = Utils::imageBoundingBoxes(opts.number, (float) fb_width, (float) fb_height);
            imageCache.clear();
            addVariantTrack(path, opts.start_index, false, false);
            variantFileSelection = (int) variantTracks.size() - 1;
            currentVarTrack = &variantTracks[variantFileSelection];
            currentVarTrack->blockStart = 0;
            mode = Manager::Show::TILED;
            if (print_message) {
                std::cout << termcolor::magenta << "\nFile        " << termcolor::reset
                          << variantTracks[variantFileSelection].path << "\n";
            }
        } else {
            tracks.push_back(HGW::GwTrack());
            try {
                tracks.back().open(path, true);
                tracks.back().variant_distance = &opts.variant_distance;
                if (print_message) {
                    std::cout << termcolor::magenta << "\nTrack       " << termcolor::reset << path << "\n";
                }
            } catch (...) {
                tracks.pop_back();
            }
        }
        if (good) {
            processed = false;
            redraw = true;
        }
    }

    void GwPlot::pathDrop(GLFWwindow* wind, int count, const char** paths) {
        for (int i=0; i < count; ++ i) {
            std::string pth = *paths;
            addTrack(pth);
        }
    }

    int GwPlot::getCollectionIdx(float x, float y) {
        if (y <= refSpace) {
            return REFERENCE_TRACK; //-2
        } else if (!tracks.empty() && y >= refSpace + totalCovY + (trackY*(float)headers.size()) && y < (float)fb_height - refSpace) {
			int index = -3;
			float trackSpace = (float)fb_height - totalCovY - refSpace - sliderSpace - (trackY*(float)headers.size());
			trackSpace = trackSpace / (float)tracks.size();
			float cIdx = (y - (refSpace + totalCovY + (trackY*(float)headers.size()))) / trackSpace;
			index -= int(cIdx);
			if ((index * -1) - 3 > (int)tracks.size()) {
				index = -1;
			}
			return index;  // track
		}
        if (regions.empty()) {
            return NO_REGIONS; //-1
        }
        int i = 0;
        if (bams.empty()) {
            i = (int)(x / ((float)fb_width / (float)regions.size()));
            i = (i >= (int)regions.size()) ? (int)regions.size() - 1 : i;
            return i;
        } else if (bams.size() <= 1) {
            for (auto &cl: collections) {
                float min_x = cl.xOffset;
                float max_x = cl.xScaling * ((float) (cl.region->end - cl.region->start)) + min_x;
                float min_y = refSpace;
                float max_y = fb_height - refSpace - totalTabixY;
                if (x > min_x && x < max_x && y > min_y && y < max_y) {
                    return i;
                }
                i += 1;
            }
        } else {
            for (auto &cl: collections) {
                float min_x = cl.xOffset;
                float max_x = cl.xScaling * ((float) (cl.region->end - cl.region->start)) + min_x;
                float min_y = cl.yOffset;
                float max_y = min_y + trackY;
                if (x > min_x && x < max_x && y > min_y && y < max_y)
                    return i;
                i += 1;
            }
        }
        return NO_REGIONS; //-1
    }

    void GwPlot::updateSlider(float xW) {
        float colWidth = (float)fb_width / (float)regions.size();
        float gap = 50;
        float gap2 = 2*gap;
        float drawWidth = colWidth - gap2;
        if (drawWidth < 0) {
            return;
        }
        float vv = 0;
        int i = 0;
        for (auto &rgn : regions) {
            if (xW > vv && xW < colWidth * (i+1)) {
                float relX = xW - (colWidth * i) - gap;
                relX = (relX < 0) ? 0 : relX;
                relX = (relX > drawWidth) ? drawWidth : relX;
                float relP = relX / drawWidth;
                auto length = (float)faidx_seq_len(fai, rgn.chrom.c_str());
                auto new_s = (int)(length * relP);
                int regionL = rgn.end - rgn.start;
                rgn.start = new_s;
                rgn.end = new_s + regionL;
                if (rgn.end > length) {
                    rgn.end = length;
                    rgn.start = (rgn.end - regionL > 0) ? rgn.end - regionL : 0;
                } else if (rgn.start < 0) {
                    rgn.start = 0;
                    rgn.end = regionL;
                }
                processed = false;
                redraw = true;
                fetchRefSeq(rgn);
            } else if (vv > xW) {
                break;
            }
            vv += colWidth;
            i += 1;
        }
    }

    void GwPlot::mouseButton(GLFWwindow* wind, int button, int action, int mods) {
        double x, y;
        glfwGetCursorPos(window, &x, &y);

        int windowW, windowH;  // convert screen coords to frame buffer coords
        glfwGetWindowSize(wind, &windowW, &windowH);
        float xW, yW;
        if (fb_width > windowW) {
            float ratio = (float) fb_width / (float) windowW;
            xW = (float)x * ratio;
            yW = (float)y * ratio;
        } else {
            xW = (float)x;
            yW = (float)y;
        }
        // settings button or command box button
        float half_h = (float)fb_height / 2;
        bool tool_popup = (xW > 0 && xW <= 60 && yW >= half_h - 60 && yW <= half_h + 60) ; // && (std::fabs(xDrag) < 5 || xDrag == DRAG_UNSET) && (std::fabs(yDrag) < 5 || yDrag == DRAG_UNSET));
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE && tool_popup) {
            xDrag = DRAG_UNSET;
            yDrag = DRAG_UNSET;
            if (yW < half_h) {
                if (mode != SETTINGS) {
                    last_mode = mode;
                    mode = Show::SETTINGS;
                    opts.menu_table = Themes::MenuTable::MAIN;
                    opts.menu_level = "";
                } else {
                    mode = last_mode;
                    updateSettings();
                    opts.editing_underway = false;
                }
                commandToolTipIndex = -1;
            } else {
                if (!captureText) {
                    captureText = true;
                    inputText = "";
                    charIndex = 0;
                    textFromSettings = false;
                } else {
                    captureText = false;
                    textFromSettings = false;
                    opts.editing_underway = false;
                }
                commandToolTipIndex = 0;
            }
            return;
        }

        // click on one of the commands in the pop-up menu
        if (commandToolTipIndex != -1 && captureText && mode != SETTINGS && button == GLFW_MOUSE_BUTTON_LEFT) {
            double xPos_fb = x;
            double yPos_fb = y;
            convertScreenCoordsToFrameBufferCoords(wind, &xPos_fb, &yPos_fb, fb_width, fb_height);
            if (xPos_fb > 50 && xPos_fb < 50 + fonts.overlayWidth * 20 && action == GLFW_RELEASE) {
                keyPress(wind, GLFW_KEY_ENTER, 0, GLFW_PRESS, 0);
                return;
            }
            return;
        }
        if (xDrag == DRAG_UNSET) {
            xDrag = 0;
            xOri = x;
            yDrag = 0;
            yOri = y;

        }
        xDrag = x - xOri;
        yDrag = y - yOri;

        // custom clicks for each mode SINGLE/TILED/SETTINGS. Menu navigation is deferred to Menu::navigateMenu
        if (mode == Manager::SINGLE && button == GLFW_MOUSE_BUTTON_LEFT) {
            if (regions.empty()) {
                return;
            }

            if (yW >= (fb_height - sliderSpace - gap)) {
                if (action == GLFW_PRESS) {
                    updateSlider(xW);
                }
                xOri = x;
                yOri = y;
                return;
            }

            int idx = getCollectionIdx(xW, yW);
            if (idx == REFERENCE_TRACK && action == GLFW_RELEASE) {
                if (collections.empty()) {
                    float xScaling = (float)((regionWidth - gap - gap) / ((double)(regions[regionSelection].end -regions[regionSelection].start)));
                    float xOffset = (regionWidth * (float)regionSelection) + gap;
                    Term::printRefSeq(&regions[regionSelection], xW, xOffset, xScaling);
                } else {
                    for (auto &cl: collections) {
                        float min_x = cl.xOffset;
                        float max_x = cl.xScaling * ((float)(cl.region->end - cl.region->start)) + min_x;
                        if (xW > min_x && xW < max_x) {
                            Term::printRefSeq(cl.region, xW, cl.xOffset, cl.xScaling);
                            break;
                        }
                    }
                }

            } else if (idx <= TRACK && action == GLFW_RELEASE) {
                if (std::abs(xDrag) < 5 && std::abs(yDrag) < 5) {
                    float rS = ((float)fb_width / (float)regions.size());
                    int tIdx = (int)((xW) / rS);
                    if (tIdx < (int)regions.size()) {
                        float relX = xW - gap;
                        if (tIdx > 0) {
                            relX -= (float)tIdx * rS;
                        }
                        relX /= (rS - gap - gap);
                        if (relX < 0 || relX > 1) {
                            return;
                        }
                        int trackIdx = (idx * -1) -3;
                        HGW::GwTrack &targetTrack = tracks[trackIdx];
                        float stepY =  (totalTabixY) / (float)tracks.size();
                        float step_track = (stepY) / ((float)regions[regionSelection].featureLevels[trackIdx]);
                        float y = fb_height - totalTabixY - refSpace;  // start of tracks on canvas
                        int featureLevel = (int)(yW - y - (trackIdx * stepY)) / step_track;
                        Term::printTrack(relX, targetTrack, &regions[tIdx], false, featureLevel, trackIdx, target_qname, &target_pos);
                    }
                }
                clickedIdx = -1;
                xOri = x;
                yOri = y;
                xDrag = DRAG_UNSET;
                yDrag = DRAG_UNSET;
                return;
			}
            if (action == GLFW_PRESS) {
                if (collections.empty() || idx < 0) {
                    clicked = regions[regionSelection];
                    clickedIdx = -1;
                } else {
                    clicked = *collections[idx].region;
                    clickedIdx = idx;
                    regionSelection = collections[idx].regionIdx;
                }
            }
            if (idx < 0) {
                return;
            }

            if (std::abs(xDrag) < 5 && action == GLFW_RELEASE && !bams.empty()) {
                Segs::ReadCollection &cl = collections[idx];
                int pos = (int) (((xW - (float) cl.xOffset) / cl.xScaling) + (float) cl.region->start);
                if (ctrlPress) {  // zoom in to mouse position
                    int strt = pos - 2500;
                    strt = (strt < 0) ? 0 : strt;
                    Utils::Region &region = regions[regionSelection];
                    region.start = strt;
                    region.end = strt + 5000;
                    regionSelection = cl.regionIdx;
                    delete region.refSeq;
                    fetchRefSeq(region);
                    processed = false;
                    redraw = true;
                    return;
                }
                // highlight read below
                int level = 0;
                int slop = 0;
                if (!opts.tlen_yscale) {
                    level = (int)((yW - (float) cl.yOffset) / yScaling);
                    if (level < 0) {  // print coverage info (mouse Pos functions already prints out cov info to console)
                        std::cout << std::endl;
                        return;
                    }
                    if (cl.vScroll < 0) {
                        level += cl.vScroll + 1;
                    }
                } else {
                    int max_bound = opts.max_tlen;
                    level = (int) ((yW - (float) cl.yOffset) / (((trackY - gap) * 0.95) / (float)(max_bound)));
                    slop = (int)(max_bound * 0.025);
                    slop = (slop <= 0) ? 25 : slop;
                    std::cerr << level << std::endl;
                }
                std::vector<Segs::Align>::iterator bnd;
                bnd = std::lower_bound(cl.readQueue.begin(), cl.readQueue.end(), pos,
                                       [&](const Segs::Align &lhs, const int pos) { return (int)lhs.pos <= pos; });
                while (bnd != cl.readQueue.begin()) {
                    if (!opts.tlen_yscale) {
                        if (bnd->y == level && (int)bnd->pos <= pos && pos < (int)bnd->reference_end) {
                            bnd->edge_type = 4;
                            target_qname = bam_get_qname(bnd->delegate);
                            Term::printRead(bnd, headers[cl.bamIdx], selectedAlign, cl.region->refSeq, cl.region->start, cl.region->end, opts.low_memory);
                            redraw = true;
                            processed = true;
                            cl.skipDrawingReads = false;
                            cl.skipDrawingCoverage = true;
                            break;
                        }
                    } else {
                        if ((bnd->y >= level - slop && bnd->y < level) && (int)bnd->pos <= pos && pos < (int)bnd->reference_end) {
                            bnd->edge_type = 4;
                            target_qname = bam_get_qname(bnd->delegate);
                            Term::printRead(bnd, headers[cl.bamIdx], selectedAlign, cl.region->refSeq, cl.region->start, cl.region->end, opts.low_memory);
                            redraw = true;
                            processed = true;
                            cl.skipDrawingReads = false;
                            cl.skipDrawingCoverage = true;
                        }
                    }
                    --bnd;
                }
                xDrag = DRAG_UNSET;
                yDrag = DRAG_UNSET;
                clickedIdx = -1;

            } else if (action == GLFW_RELEASE) {
                Utils::Region &region = regions[regionSelection];
                auto w = (float)(region.end - region.start) * (float) regions.size();
                if (w >= 50000) {
                    int travel = (int) (w * (xDrag / windowW));
                    int old_start = region.start;
                    if (region.start - travel < 0) {
                        travel = region.start;
                        region.start = 0;
                        region.end = clicked.end - travel;
                    } else {
                        region.start = clicked.start - travel;
                        region.end = clicked.end - travel;
                    }
                    if (region.start < 0 || region.end < 0) {
                        return;
                    }
                    delete region.refSeq;
                    fetchRefSeq(region);

                    bool lt_last = region.start < old_start;
                    if (opts.link_op != 0) {
                        processed = false;
                        redraw = true;
                    } else {
                        processed = true;
                        redraw = true;
                        if (bams.empty()) {
                            return;
                        }
                        for (auto &col : collections) {
                            if (col.regionIdx == regionSelection) {
                                col.region = &regions[regionSelection];
                                col.skipDrawingReads = false;
                                col.skipDrawingCoverage = false;
                                HGW::appendReadsAndCoverage(col, bams[col.bamIdx], headers[col.bamIdx],
                                                            indexes[col.bamIdx], opts, (bool) opts.max_coverage,
                                                            lt_last, &samMaxY, filters, pool);
                            }
                        }
                    }
                }
                clickedIdx = -1;
            }
            xOri = x;
            yOri = y;
            xDrag = DRAG_UNSET;
            yDrag = DRAG_UNSET;

        } else if (mode == Manager::SINGLE && button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
            if (regions.empty() || variantTracks.empty()) {
                return;
            }
            currentVarTrack = &variantTracks[variantFileSelection];
            if (currentVarTrack != nullptr && (!currentVarTrack->multiRegions.empty() || currentVarTrack->type == HGW::TrackType::IMAGES)) {
                mode = Manager::TILED;
                xDrag = DRAG_UNSET;
                yDrag = DRAG_UNSET;
                redraw = true;
                processed = false;
                imageCacheQueue.clear();
                if (currentVarTrack->type == HGW::TrackType::IMAGES) {
                    currentVarTrack->multiRegions.clear();
                }
                std::cout << std::endl;
            }
        } else if (mode == Manager::TILED) {
            currentVarTrack = &variantTracks[variantFileSelection];
            if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
                int i = 0;
                for (auto &b: bboxes) {
                    if (xW > b.xStart && xW < b.xEnd && yW > b.yStart && yW < b.yEnd) {
                        break;
                    }
                    ++i;
                }
                if (i == (int)bboxes.size()) {
                    xDrag = DRAG_UNSET;
                    yDrag = DRAG_UNSET;
                    xOri = x;
                    yOri = y;
                    return;
                }
                if (!bams.empty()) {
                    if (i < (int)currentVarTrack->multiRegions.size() && !bams.empty()) {
                        if (currentVarTrack->blockStart + i < (int)currentVarTrack->multiRegions.size()) {
                            if (currentVarTrack->multiRegions[currentVarTrack->blockStart + i][0].chrom.empty()) {
                                return; // check for "" no chrom set
                            } else {
                                regions = currentVarTrack->multiRegions[currentVarTrack->blockStart + i];
                                redraw = true;
                                processed = false;
                                fetchRefSeqs();
                                mode = Manager::SINGLE;
                                glfwPostEmptyEvent();
                            }
                        }
                    } else {
                        // todo check this!
                        // try and parse location from filename
                        std::vector<Utils::Region> rt;
                        bool parsed = Utils::parseFilenameToRegions(currentVarTrack->image_glob[currentVarTrack->blockStart + i], rt, fai, opts.pad, opts.split_view_size);
                        if (parsed) {
                            if (rt.size() == 1 && rt[0].end - rt[0].start > 500000) {
                                int posX = (int)(((xW - gap) / (float)(fb_width - gap - gap)) * (float)(rt[0].end - rt[0].start)) + rt[0].start;
                                rt[0].start = (posX - 10000 > 0) ? posX - 100000 : 1;
                                rt[0].end = posX + 100000;
                            }
                            for (auto &r: regions) {
                                delete r.refSeq;
                            }
                            regions.clear();
                            regions = rt;
                            redraw = true;
                            processed = false;
                            fetchRefSeqs();
                            mode = Manager::SINGLE;
                            glfwPostEmptyEvent();
                        }
                    }
                }
            } else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
                if (captureText) {
                    captureText = false;
                    processText = false;
                    shiftPress = false;
                    commandToolTipIndex = -1;
                    xDrag = DRAG_UNSET;
                    yDrag = DRAG_UNSET;
                    return;
                }

                bool variantFile_click = variantTracks.size() > 1 && yW < fb_height * 0.02;
                if (variantFile_click) {
                    float tile_box_w = std::fmin(100 * monitorScale, (fb_width - (variantTracks.size() * gap + 1)) / variantTracks.size());
                    float x_val = gap;
                    for (int i=0; i < (int)variantTracks.size(); ++i) {
                        if (x_val - gap <= xW && x_val + tile_box_w >= xW) {
                            variantFileSelection = i;
                            redraw = true;
                            break;
                        }
                        x_val += tile_box_w + gap;
                    }
                }
                if (std::fabs(xDrag) > fb_width / 16.) {
                    int nmb = opts.number.x * opts.number.y;
                    bool scroll_left;
                    if (xDrag > 0) {
                        scroll_left = true;
                    } else {
                        scroll_left = false;
                    }
                    if (!scroll_left) {
//                        size_t currentSize = (currentVarTrack->image_glob.empty()) ? currentVarTrack->multiRegions.size() : currentVarTrack->image_glob.size();
////                        if (currentVarTrack->blockStart + nmb > (int)currentSize) {
////                            currentVarTrack->blockStart += nmb;
////                        }
                        if (currentVarTrack->type == HGW::TrackType::IMAGES ) {
                            if (currentVarTrack->blockStart + nmb > (int)currentVarTrack->image_glob.size() - nmb) {
                                return;
                            }
                        } else if (*currentVarTrack->trackDone) {
                            return;
                        }
                        currentVarTrack->blockStart += nmb;
                        redraw = true;

//                        if (!*currentVarTrack->trackDone && currentVarTrack->blockStart + nmb > (int)currentSize) {
//                            if (currentVarTrack->type == HGW::TrackType::IMAGES && currentVarTrack->blockStart + nmb > currentVarTrack->image_glob.size() - nmb) {
//                                return;
//                            }
//                            currentVarTrack->blockStart += nmb;
//                            redraw = true;
//                        }
                    } else {

                        if (currentVarTrack->blockStart == 0) {
                            return;
                        }
                        currentVarTrack->blockStart = (currentVarTrack->blockStart - nmb > 0) ? currentVarTrack->blockStart - nmb : 0;
                        redraw = true;

                    }
                } else if (std::fabs(xDrag) < 5) {
                    int i = 0;
                    for (auto &b: bboxes) {
                        if (xW > b.xStart && xW < b.xEnd && yW > b.yStart && yW < b.yEnd) {
                            break;
                        }
                        ++i;
                    }
                    if (i == (int)bboxes.size()) {
                        xDrag = DRAG_UNSET;
                        yDrag = DRAG_UNSET;
                        xOri = x;
                        yOri = y;
                        return;
                    }
                    if (currentVarTrack->blockStart + i < (int)currentVarTrack->multiLabels.size()) {
                        currentVarTrack->multiLabels[currentVarTrack->blockStart + i].next();
                        currentVarTrack->multiLabels[currentVarTrack->blockStart + i].clicked = true;
                        currentVarTrack->multiLabels[currentVarTrack->blockStart + i].savedDate = Utils::dateTime();
                        redraw = true;
                    }
                }
                xDrag = DRAG_UNSET;
                yDrag = DRAG_UNSET;
                xOri = x;
                yOri = y;
            }
        } else if (mode == Manager::SETTINGS && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
            bool keep_alive = Menu::navigateMenu(opts, GLFW_KEY_ENTER, GLFW_PRESS, inputText, &charIndex, &captureText, &textFromSettings, &processText, reference);

           redraw = true;
            if (opts.editing_underway) {
                textFromSettings = true;
            }
            if (!keep_alive) {
                updateSettings();
            }
        }
    }

    void GwPlot::updateCursorGenomePos(float xOffset, float xScaling, float xPos, Utils::Region *region, int bamIdx=0) {
        if (regions.empty() || mode == TILED) {
            return;
        }
        int pos = ((int) (((double)xPos - (double)xOffset) / (double)xScaling)) + region->start;
        std::string s = Term::intToStringCommas(pos);
        int term_width_remaining = printRegionInfo();
        s = "    " + s;
        if (term_width_remaining < (int)s.size()) {
            return;
        }
        std::cout << s << std::flush;
        if (!bam_paths.empty()) {
            term_width_remaining -= (int)s.size();
            std::string base_filename = "  -  " + bam_paths[bamIdx].substr(bam_paths[bamIdx].find_last_of("/\\") + 1);
            if (term_width_remaining < (int)base_filename.size()) {
                std::cout << std::flush;
                return;
            }
            std::cout << base_filename << std::flush;
        }
    }

    void GwPlot::mousePos(GLFWwindow* wind, double xPos, double yPos) {
        int windX, windY;
        glfwGetWindowSize(wind, &windX, &windY);
        if (yPos < 0 || xPos < 0 || xPos > windX || yPos > windY) {
            return;
        }
        if (lastX == -1) {
            lastX = xPos;
            lastY = yPos;
        }
        int state = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);

        bool lt_last = xPos < lastX;
        lastX = xPos;
        lastY = yPos;

        double xPos_fb = xPos;
        double yPos_fb = yPos;
        double xPosOri_fb = xOri;
        double yPosOri_fb = yOri;
        convertScreenCoordsToFrameBufferCoords(wind, &xPos_fb, &yPos_fb, fb_width, fb_height);
        convertScreenCoordsToFrameBufferCoords(wind, &xPosOri_fb, &yPosOri_fb, fb_width, fb_height);
        // register popup toolbar
        if (captureText && mode != SETTINGS && xPos_fb < (50 + fonts.overlayWidth * 20)) {
            int tip_lb = 0;
            int tip_ub = (int)inputText.size();
            if (!inputText.empty()) {
                TipBounds tip_bounds = getToolTipBounds(inputText);
                tip_lb = tip_bounds.lower;
                tip_ub = tip_bounds.upper;
            }
            float height_f = fonts.overlayHeight * 2;
            float x = 50;
            float w = fb_width - 100;
            if (x > w) {
                return;
            }
            float y = fb_height - (fb_height * 0.025);
            float y2 = fb_height - (height_f * 2.25);
            float yy = (y2 < y) ? y2 : y;
            int pad = fonts.overlayHeight * 0.3;
            yy -= pad + pad;
            for (int idx=0; idx < (int)Menu::commandToolTip.size(); idx++) {
                if (!inputText.empty() && (idx < tip_lb || idx > tip_ub)) {
                    continue;
                }
                if (yy >= yPos_fb && yy - fonts.overlayHeight <= yPos_fb ) {
                    commandToolTipIndex = idx;
                    break;
                }
                yy -= fonts.overlayHeight + pad;
            }
        }
        else {
            commandToolTipIndex = -1;
        }

        float trackBoundary = totalCovY + refSpace + (trackY*(float)headers.size());
        if (!tracks.empty()) {
            if (std::fabs(yPos_fb - trackBoundary) < 5 * monitorScale) {
                glfwSetCursor(window, vCursor);
            } else {
                glfwSetCursor(window, normalCursor);
            }
        }

        if (state == GLFW_PRESS) {
            xDrag = xPos - xOri;  // still in window co-ords not frame buffer co-ords
            yDrag = yPos - yOri;
            if (std::abs(xDrag) > 5 || std::abs(yDrag) > 5) {
                captureText = false;
            }

            if (mode == Manager::SINGLE) {
                if (regions.empty()) {
                    return;
                }

                if (yPos_fb >= (fb_height - sliderSpace - gap) && yPosOri_fb >= (fb_height - sliderSpace - gap)) {
                    updateSlider((float) xPos_fb);
                    yDrag = DRAG_UNSET;
                    xDrag = DRAG_UNSET;
                    return;
                }
                if (!tracks.empty()) {
                    float trackBoundary = fb_height - totalTabixY - refSpace;
                    if (tabBorderPress || (std::fabs(yPos_fb - trackBoundary) < 5 * monitorScale && xDrag < 5 && yDrag < 5)) {
                        if (yPos_fb <= refSpace + totalCovY + 10) {
                            return;
                        }
                        tabBorderPress = true;
                        float drawingArea = ((float)fb_height - refSpace - sliderSpace);
                        float new_boundary = fb_height - yPos_fb - refSpace;
                        opts.tab_track_height = new_boundary / drawingArea;
                        redraw = true;
                        return;
                    }
                }

                int idx = getCollectionIdx((float) xPos_fb, (float) yPos_fb);
                int windowW, windowH;
                glfwGetWindowSize(wind, &windowW, &windowH);
                Utils::Region &region = regions[regionSelection];
                if (std::fabs(xDrag) > std::fabs(yDrag) && region.end - region.start < 50000) {
                    printRegionInfo();
                    auto w = (float) (region.end - region.start) * (float) regions.size();
                    int travel = (int) (w * (xDrag / windowW));
                    if (region.start - travel < 1) {
                        return;
                    } else {
                        region.start = clicked.start - travel;
                        region.end = clicked.end - travel;
                    }
                    if (region.start < 1 || region.end < 1) {
                        return;
                    }
                    delete region.refSeq;
                    fetchRefSeq(region);
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            if (!bams.empty()) {
                                cl.skipDrawingReads = false;
                                cl.skipDrawingCoverage = false;
                                HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx],
                                                            indexes[cl.bamIdx], opts, (bool)opts.max_coverage, !lt_last,
                                                            &samMaxY, filters, pool);

                            }
                        }
                    }
                    processed = true;
                    redraw = true;
                    glfwPostEmptyEvent();
                    return;
                } else {
                    if (idx < 0) {
                        return;
                    }
                    Segs::ReadCollection &cl = collections[idx];
                    regionSelection = cl.regionIdx;
                    if (clickedIdx == -1 || idx != clickedIdx) {
                        return;
                    }

                    if (std::fabs(yDrag) > std::fabs(xDrag) && std::fabs(yDrag) > 1) {
                        float travel_y;
                        if (!opts.tlen_yscale) {
                            travel_y = yDrag /
                                         ((float) ((windowH * (1 - opts.tab_track_height)) / (float) bams.size()) /
                                          (float) cl.levelsStart.size());
                        } else {
                            travel_y = yDrag /
                                       ((float) ((windowH * (1 - opts.tab_track_height)) / (float) bams.size()) /
                                        (float) opts.max_tlen);
                        }
                        if (std::fabs(travel_y) > 1) {
                            if (opts.tlen_yscale) {
                                opts.max_tlen -= (int)travel_y;
                                opts.ylim -= (int)travel_y;
                            } else {
                                cl.vScroll -= (int) travel_y;
                                cl.vScroll = (cl.vScroll <= 0) ? 0 : cl.vScroll;
                            }

                            cl.levelsStart.clear();
                            cl.levelsEnd.clear();
                            cl.linked.clear();
                            for (auto &itm: cl.readQueue) { itm.y = -1; }
                            int maxY = Segs::findY(cl, cl.readQueue, opts.link_op, opts, cl.region, false);
                            samMaxY = (maxY > samMaxY || opts.tlen_yscale) ? maxY : samMaxY;
                            yOri = yPos;
                        }
                    }
                    processed = true;
                    redraw = false;
                    glfwPostEmptyEvent();
                    return;
                }
            }
        } else {
            if (mode == Manager::SINGLE) {
                tabBorderPress = false;
                if (regions.empty()) {
                    return;
                }
                int rs = getCollectionIdx((float)xPos_fb, (float)yPos_fb);
	            if (rs <= TRACK) {  // print track info
                    if (tracks.empty()) {
                        return;
                    }
		            float rgS = ((float)fb_width / (float)regions.size());
		            int tIdx = (int)((xPos_fb) / rgS);
		            if (tIdx < (int)regions.size()) {
			            float relX = xPos_fb - gap;
			            if (tIdx > 0) {
				            relX -= (float)tIdx * rgS;
			            }
			            relX /= (rgS - gap - gap);
						if (relX < 0 || relX > 1) {
							return;
						}
                        int targetIndex = (rs * -1) -3;
                        HGW::GwTrack &targetTrack = tracks[targetIndex];
                        float stepY =  (totalTabixY) / (float)tracks.size();
                        float step_track = (stepY) / ((float)regions[regionSelection].featureLevels[targetIndex]);
                        float y = fb_height - totalTabixY - refSpace;  // start of tracks on canvas
                        int featureLevel = (int)(yPos_fb - y - (targetIndex * stepY)) / step_track;
			            Term::printTrack(relX, targetTrack, &regions[tIdx], true, featureLevel, targetIndex, target_qname, &target_pos);
		            }
	            }
                if (rs < 0) { // print reference info
                    float xScaling = (float)((regionWidth - gap - gap) / ((double)(regions[regionSelection].end -regions[regionSelection].start)));
                    float xOffset = (regionWidth * (float)regionSelection) + gap;
                    if (rs == REFERENCE_TRACK) {
                        if (collections.empty()) {
                            Term::updateRefGenomeSeq(&regions[regionSelection], (float)xPos_fb, xOffset,  xScaling);
                        } else {
                            for (auto &cl: collections) {
                                float min_x = cl.xOffset;
                                float max_x = cl.xScaling * ((float)(cl.region->end - cl.region->start)) + min_x;
                                if (xPos_fb > min_x && xPos_fb < max_x) {
                                    Term::updateRefGenomeSeq(cl.region, (float)xPos_fb, cl.xOffset,  cl.xScaling);
                                    break;
                                }
                            }
                        }
//                    } else {
//                        updateCursorGenomePos(xOffset, xScaling, (float)xPos_fb, &regions[regionSelection]);
                    }
                    return;
                }

                Segs::ReadCollection &cl = collections[rs];
                regionSelection = cl.regionIdx;
	            int pos = (int) ((((double)xPos_fb - (double)cl.xOffset) / (double)cl.xScaling) + (double)cl.region->start);
                float f_level = ((yPos_fb - (float) cl.yOffset) / (trackY / (float)(cl.levelsStart.size() - cl.vScroll )));
	            int level = (f_level < 0) ? -1 : (int)(f_level);
	            if (level < 0 && cl.region->end - cl.region->start < 50000) {
		            Term::clearLine();
		            Term::printCoverage(pos, cl);
		            return;
	            }
                updateCursorGenomePos(cl.xOffset, cl.xScaling, (float)xPos_fb, cl.region, cl.bamIdx);
            } else if (mode == TILED) {
                currentVarTrack = &variantTracks[variantFileSelection];
                int i = 0;
                for (auto &b: bboxes) {
                    if (xPos_fb > b.xStart && xPos_fb < b.xEnd && yPos_fb > b.yStart && yPos_fb < b.yEnd) {
                        break;
                    }
                    ++i;
                }
                if (i == (int)bboxes.size()) {
                    return;
                }
                if (currentVarTrack->blockStart + i < (int)currentVarTrack->multiRegions.size()) {
                    if (i != mouseOverTileIndex) {
                        mouseOverTileIndex = i;
                    }
                    Utils::Label *label = &currentVarTrack->multiLabels[currentVarTrack->blockStart + i];
                    label->mouseOver = true;
                    Term::printVariantFileInfo(label, mouseOverTileIndex + currentVarTrack->blockStart);
                } else if (currentVarTrack->blockStart + i < (int)currentVarTrack->image_glob.size()) {
                    if (i != mouseOverTileIndex) {
                        mouseOverTileIndex = i;
                    }
                    Utils::Label *label = &currentVarTrack->multiLabels[currentVarTrack->blockStart + i];
                    label->mouseOver = true;
                    Term::printVariantFileInfo(label, mouseOverTileIndex + currentVarTrack->blockStart);
                }
            } else if (mode == SETTINGS) {
                Menu::menuMousePos(opts, fonts, (float)xPos_fb, (float)yPos_fb, (float)fb_height, (float)fb_width, &redraw);
            }
        }
    }

    void GwPlot::scrollGesture(GLFWwindow* wind, double xoffset, double yoffset) {
        if (mode == Manager::SINGLE) {
            if (std::fabs(yoffset) > std::fabs(xoffset) && 0.1 < std::fabs(yoffset)) {
                if (yoffset < 0) {
                    keyPress(wind, opts.zoom_out, 0, GLFW_PRESS, 0);
                } else {
                    keyPress(wind, opts.zoom_in, 0, GLFW_PRESS, 0);
                }
            } else if (std::fabs(xoffset) > std::fabs(yoffset) && 0.1 < std::fabs(xoffset)) {
                if (xoffset < 0) {
                    keyPress(wind, opts.scroll_right, 0, GLFW_PRESS, 0);
                } else {
                    keyPress(wind, opts.scroll_left, 0, GLFW_PRESS, 0);
                }
            }
        } else if (std::fabs(yoffset) > std::fabs(xoffset) && 0.1 < std::fabs(yoffset)) {
            if (yoffset < 0) {
                keyPress(wind, opts.scroll_right, 0, GLFW_PRESS, 0);
            } else {
                keyPress(wind, opts.scroll_left, 0, GLFW_PRESS, 0);
            }
        }
    }

    void GwPlot::windowResize(GLFWwindow* wind, int x, int y) {
        resizeTriggered = true;
        resizeTimer = std::chrono::high_resolution_clock::now();
        glfwGetFramebufferSize(window, &fb_width, &fb_height);
        bboxes = Utils::imageBoundingBoxes(opts.number, (float)fb_width, (float)fb_height);
    }
}
