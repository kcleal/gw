//
// Created by Kez Cleal on 23/08/2022.
//
#include <array>
#include <cmath>
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
#include "../include/termcolor.h"
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
//            if (mode == Manager::SINGLE && !regions.empty() && (!multiRegions.empty() || !imageCache.empty()) ) {
//                mode = Manager::TILED;
//                redraw = true;
//                processed = false;
//                imageCacheQueue.clear();
//                mouseOverTileIndex = 0;
//                return key;
//            }
//            if (mode == Manager::TILED && !regions.empty()) {
//                mode = Manager::SINGLE;
//                redraw = true;
//                processed = false;
//                imageCacheQueue.clear();
//                if (blockStart < (int)multiRegions.size()) {
//                    if (multiRegions[blockStart][0].chrom.empty()) {
//                        return key; // check for "" no chrom set
//                    } else {
//                        regions = multiRegions[blockStart];
//                        redraw = true;
//                        processed = false;
//                        fetchRefSeqs();
//                        glfwPostEmptyEvent();
//                    }
//                }
//                return key;
//            }
        }
        if (!captureText) {
            if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
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
                    std::cout << termcolor::magenta << "File      " << termcolor::reset << variantTracks[variantFileSelection].path << "\n";
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
                    std::vector<std::string> exec = {"cov", "count", "edges", "insertions", "line", "low-mem", "log2-cov", "mate", "mate add", "mismatches", "tags", "soft-clips", "sam", "refresh"};
                    if (std::find(exec.begin(), exec.end(), inputText) != exec.end()) {
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
                    } else if (key == GLFW_KEY_SEMICOLON && mods == GLFW_MOD_SHIFT) {
                        Term::editInputText(inputText, ":", charIndex);
                    } else if (key == GLFW_KEY_1 && mods == GLFW_MOD_SHIFT) {  // this will probably not work for every keyboard
                        Term::editInputText(inputText, "!", charIndex);
                    } else if (key == GLFW_KEY_7 && mods == GLFW_MOD_SHIFT) {
                        Term::editInputText(inputText, "&", charIndex);
                    } else if (key == GLFW_KEY_COMMA && mods == GLFW_MOD_SHIFT) {
                        Term::editInputText(inputText, "<", charIndex);
                    } else if (key == GLFW_KEY_PERIOD && mods == GLFW_MOD_SHIFT) {
                        Term::editInputText(inputText, ">", charIndex);
					} else if (key == GLFW_KEY_LEFT_BRACKET && mods == GLFW_MOD_SHIFT) {
                        Term::editInputText(inputText, "{", charIndex);
					} else if (key == GLFW_KEY_RIGHT_BRACKET && mods == GLFW_MOD_SHIFT) {
                        Term::editInputText(inputText, "}", charIndex);
					} else if (key == GLFW_KEY_MINUS && mods == GLFW_MOD_SHIFT) {
                        Term::editInputText(inputText, "_", charIndex);
                    } else {
                        if (mods == GLFW_MOD_SHIFT) { // uppercase
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
        return key;
    }

    void GwPlot::highlightQname() { // todo make this more efficient
        for (auto &cl : collections) {
            for (auto &a: cl.readQueue) {
                if (bam_get_qname(a.delegate) == target_qname) {
                    a.edge_type = 4;
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
            throw CloseException();
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
            for (auto &cl: collections) { cl.vScroll = 0; }
//        }
//        else if (Utils::startsWith(inputText, "toggle")) {
//            std::vector<std::string> split = Utils::split(inputText, delim);
//            if (split.size() != 2) {
//                std::cerr << termcolor::red << "Error:" << termcolor::reset << " toggle takes one option e.g. 'toggle cov'\n";
//                inputText = "";
//                return true;
//            }
//            std::string &ts = split[1];
//            bool toggle_res = false;
//            if (ts == "insertions" || ts == "ins") {
//                valid = true;
//                opts.small_indel_threshold = (opts.small_indel_threshold == 0) ? std::stoi(opts.myIni["view_thresholds"]["small_indel"]) : 0;
//                toggle_res = opts.small_indel_threshold;
//            } else if (ts == "mismatches" || ts == "mm") {
//                valid = true;
//                opts.snp_threshold = (opts.snp_threshold == 0) ? std::stoi(opts.myIni["view_thresholds"]["snp"]) : 0;
//                toggle_res = opts.snp_threshold;
//            } else if (ts == "edges") {
//                valid = true;
//                opts.edge_highlights = (opts.edge_highlights == 0) ? std::stoi(opts.myIni["view_thresholds"]["edge_highlights"]) : 0;
//                toggle_res = opts.edge_highlights;
//            } else if (ts == "soft-clips" || ts == "sc") {
//                valid = true;
//                opts.soft_clip_threshold = (opts.soft_clip_threshold == 0) ? std::stoi(opts.myIni["view_thresholds"]["soft_clip"]) : 0;
//                toggle_res = opts.soft_clip_threshold;
//            } else if (ts == "line") {
//                valid = true;
//                drawLine = !drawLine;
//                toggle_res = drawLine;
//            } else if (ts == "log2-cov") {
//                valid = true;
//                opts.log2_cov = !opts.log2_cov;
//                toggle_res = opts.log2_cov;
//            } else if (ts == "low-mem") {
//                valid = true;
//                opts.low_mem = !opts.low_mem;
//                toggle_res = opts.low_mem;
//            } else if (ts == "tlen-y") {
//                valid = true;
//                opts.tlen_yscale = !opts.tlen_yscale;
//                toggle_res = opts.tlen_yscale;
//                if (!opts.tlen_yscale) { samMaxY = opts.ylim; }
//            } else if (ts == "cov") {
//                valid = true;
//                opts.max_coverage = (opts.max_coverage) ? 0 : 10000000;
//                toggle_res = opts.max_coverage;
//            }
//            if (valid) {
//                std::cout << "Toggled " << ts << " to " << toggle_res << std::endl;
//            } else {
//                reason = OPTION_NOT_UNDERSTOOD;
//            }
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
            filters.clear();
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
        } else if (inputText =="tags"){
            valid = true;
            if (!selectedAlign.empty()) {
                std::vector<std::string> split = Utils::split(selectedAlign, '\t');
                if (split.size() > 11) {
                    Term::clearLine();
                    std::cout << "\r";
                    int i = 0;
                    for (auto &s : split) {
                        if (i > 11) {
                            std::string t = s.substr(0, s.find(':'));
                            std::string rest = s.substr(s.find(':'), s.size());
                            std::cout << termcolor::green << t << termcolor::reset << rest << "\t";
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
            return true;

        } else if (Utils::startsWith(inputText, "ylim")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            try {
                opts.ylim = std::stoi(split.back());
                samMaxY = opts.ylim;
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
            processed = true;
            redraw = true;
            inputText = "";
            return true;
        }
        else if (inputText =="insertions" || inputText == "ins") {
            opts.small_indel_threshold = (opts.small_indel_threshold == 0) ? std::stoi(opts.myIni["view_thresholds"]["small_indel"]) : 0;
            processed = true;
            redraw = true;
            inputText = "";
            return true;
        } else if (inputText =="mismatches" || inputText == "mm") {
            opts.snp_threshold = (opts.snp_threshold == 0) ? std::stoi(opts.myIni["view_thresholds"]["snp"]) : 0;
            processed = true;
            redraw = true;
            inputText = "";
            return true;
        } else if (inputText =="edges") {
            opts.edge_highlights = (opts.edge_highlights == 0) ? std::stoi(opts.myIni["view_thresholds"]["edge_highlights"]) : 0;
            processed = true;
            redraw = true;
            inputText = "";
            return true;
        } else if (inputText =="soft-clips" || inputText == "sc") {
            opts.soft_clip_threshold = (opts.soft_clip_threshold == 0) ? std::stoi(opts.myIni["view_thresholds"]["soft_clip"]) : 0;
            processed = true;
            redraw = true;
            inputText = "";
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
                if (ind >= (int) bams.size()) {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset
                              << " bam index is out of range. Use 0-based indexing\n";
                    return true;
                }
                collections.erase(std::remove_if(collections.begin(), collections.end(), [&ind](const auto &col) {
                    return col.bamIdx == ind;
                }), collections.end());
                bams.erase(bams.begin() + ind);
                indexes.erase(indexes.begin() + ind);
                processed = true;
                redraw = true;
                inputText = "";
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
                    if (ind >= (int)tracks.size()) {
                        std::cerr << termcolor::red << "Error:" << termcolor::reset << " track index is out of range. Use 0-based indexing\n";
                        return true;
                    }
                    int track_index = 0;
                    tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [&ind, &track_index](const auto &trk) {
                        return (track_index++) == ind;
                    }), tracks.end());
                    processed = true;
                    redraw = true;
                    inputText = "";
                    return true;
            } else {
                try {
                    ind = std::stoi(split.back());
                } catch (...) {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " region index not understood\n";
                    inputText = "";
                    return true;
                }
                regionSelection = 0;
                if (!regions.empty() && ind < (int)regions.size()) {
                    if (regions.size() == 1 && ind == 0) {
                        regions.clear();
                    } else {
                        regions.erase(regions.begin() + ind);
                    }
                } else {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " region index is out of range. Use 0-based indexing\n";
                    return true;
                }
                collections.erase(std::remove_if(collections.begin(), collections.end(), [&ind](const auto col) {
                    return col.regionIdx == ind;
                }), collections.end());
                processed = true;
                redraw = true;
                inputText = "";
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
                opts.max_coverage = (opts.max_coverage) ? 0 : 10000000;
            } else {
                try {
                    opts.max_coverage = std::stoi(split.back());
                } catch (...) {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " 'cov NUMBER' not understood\n";
                    return true;
                }
            }
            opts.max_coverage = std::max(0, opts.max_coverage);
            if (opts.max_coverage == 0) {
                for (auto &cl : collections) {
                    cl.mmVector.clear();
                    cl.collection_processed = true;
                }
            }
            redraw = true;
            processed = true;
            inputText = "";
            return true;
        }
        else if (inputText == "log2-cov") {
            opts.log2_cov = !(opts.log2_cov);
            redraw = true;
            processed = true;
            inputText = "";
            return true;
        } else if (inputText == "low-mem") {
            opts.low_mem = !(opts.low_mem);
            redraw = false;
            processed = true;
            inputText = "";
            std::cout << "Low memory mode " << ((opts.low_mem) ? "on" : "off") << std::endl;
            return true;
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
                    processed = true;
                    inputText = "";
                    return true;
                } else if (inputText == "mate add") {
                    regions.push_back(Utils::parseRegion(mate));
                    fetchRefSeq(regions.back());
                    processed = false;
                    processBam();
                    highlightQname();
                    redraw = true;
                    processed = true;
                    inputText = "";
                    return true;
                }
            }
        } else if (Utils::startsWith(inputText, "theme")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            if (split.size() != 2) {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " theme must be either 'igv' or 'dark'\n";
                inputText = "";
                return true;
            }
            if (split.back() == "dark") {
                opts.theme = Themes::DarkTheme();  opts.theme.setAlphas(); valid = true; imageCache.clear(); opts.theme_str = "dark";
            } else if (split.back() == "igv") {
                opts.theme = Themes::IgvTheme(); opts.theme.setAlphas(); valid = true; imageCache.clear(); opts.theme_str = "igv";
            } else {
                valid = false;
                reason = OPTION_NOT_UNDERSTOOD;
            }
        }
        else if (inputText == "tlen-y") {
            opts.tlen_yscale = !(opts.tlen_yscale);
            if (!opts.tlen_yscale) {
                samMaxY = opts.ylim;
            }
            valid = true;
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
        } else if (Utils::startsWith(inputText, "add"))  {
            if (mode != SINGLE) { mode = SINGLE; }
            std::vector<std::string> split = Utils::split(inputText, delim_q);
            if (split.size() == 1) {
                split = Utils::split(inputText, delim);
            }
            if (split.size() > 1) {
                for (int i=1; i < (int)split.size(); ++i) {
                    try {
                        regions.push_back(Utils::parseRegion(split[1]));
                        fetchRefSeq(regions.back());
                    } catch (...) {
                        std::cerr << termcolor::red << "Error parsing :add" << termcolor::reset;
                        inputText = "";
                        return true;
                    }
                }
                valid = true;
            } else {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " expected a Region e.g. chr1:1-20000\n";
                inputText = "";
                return true;
            }
        } else if (inputText == "v" || Utils::startsWith(inputText, "var") || Utils::startsWith(inputText, "v ")) {
//            if (multiLabels.empty()) {
//	            std::cerr << termcolor::red << "Error:" << termcolor::reset << " no variant file provided.\n";
//                inputText = "";
//                processed = true;
//                redraw = false;
//                return true;
//            } else if (blockStart+mouseOverTileIndex >= (int)multiLabels.size() || mouseOverTileIndex == -1) {
//                inputText = "";
//                processed = true;
//                redraw = false;
//                return true;
//            }
//			std::vector<std::string> split = Utils::split(inputText, delim);
//            Utils::Label &lbl = multiLabels[blockStart + mouseOverTileIndex];
//            Term::clearLine();
//			if (useVcf) {
//				vcf.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
//				std::string variantStringCopy = vcf.variantString;
//				vcf.get_samples();
//				std::vector<std::string> sample_names_copy = vcf.sample_names;
//                if (variantStringCopy.empty()) {
//                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " could not parse vcf/bcf line";
//                } else {
//					int requests = (int)split.size();
//					if (requests == 1) {
//                        Term::clearLine();
//                        std::cout << "\r" << variantStringCopy << std::endl;
//					} else {
//						std::string requestedVars;
//						std::vector<std::string> vcfCols = Utils::split(variantStringCopy, '\t');
//						for (int i = 1; i < requests; ++i) {
//							std::string result;
//                            try {
//                                Parse::parse_vcf_split(result, vcfCols, split[i], sample_names_copy);
//                            } catch (...) {
//                                std::cerr << termcolor::red << "Error:" << termcolor::reset
//                                          << " could not parse " << split[i] << std::endl;
//                                break;
//                            }
//							if (i != requests-1) {
//								requestedVars += split[i]+": "+result+"\t";
//							} else {
//								requestedVars += split[i]+": "+result;
//							}
//						}
//                        if (!requestedVars.empty()) {
//                            Term::clearLine();
//                            std::cout << "\r" << requestedVars << std::endl;
//                        }
//					}
//				}
//                valid = true;
//			} else {
//				variantTrack.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
//				if (variantTrack.variantString.empty()) {
//                    Term::clearLine();
//                    std::cout << "\r" << variantTrack.variantString << std::endl;
//				} else {
//                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " could not parse variant line";
//                }
//			}
            valid = true;

		} else if (inputText == "s" || Utils::startsWith(inputText, "snapshot") || Utils::startsWith(inputText, "s ")) {
			std::vector<std::string> split = Utils::split(inputText, delim);
            if (split.size() > 2) {
                valid = false;
            } else {
                std::string fname;
//                if (split.size() == 1) {
//                    if (mode == Show::SINGLE || !useVcf) {
//                        fname += regions[0].chrom + "_" + std::to_string(regions[0].start) + "_" +
//                                 std::to_string(regions[0].end) + ".png";
//                    } else {
//                        fname = "index_" + std::to_string(blockStart) + "_" +
//                                std::to_string(opts.number.x * opts.number.y) + ".png";
//                    }
//                } else {
//                    std::string nameFormat = split[1];
//                    if (useVcf && mode == Show::SINGLE) {
//                        if (mouseOverTileIndex == -1 || blockStart + mouseOverTileIndex > (int) multiLabels.size()) {
//                            inputText = "";
//                            redraw = true;
//                            processed = false;
//                            return true;
//                        }
//                        Utils::Label &lbl = multiLabels[blockStart + mouseOverTileIndex];
//                        vcf.get_samples();
//                        std::vector<std::string> sample_names_copy = vcf.sample_names;
//                        vcf.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
//                        std::string variantStringCopy = vcf.variantString;
//                        if (!variantStringCopy.empty() && variantStringCopy[variantStringCopy.length()-1] == '\n') {
//                            variantStringCopy.erase(variantStringCopy.length()-1);
//                        }
//                        std::vector<std::string> vcfCols = Utils::split(variantStringCopy, '\t');
//                        try {
//                            Parse::parse_output_name_format(nameFormat, vcfCols, sample_names_copy, bam_paths,lbl.current());
//                        } catch (...) {
//                            std::cerr << termcolor::red << "Error:" << termcolor::reset
//                                      << " could not parse " << nameFormat << std::endl;
//                            inputText = "";
//                            redraw = true;
//                            processed = false;
//                            return true;
//                        }
//                    }
//                    fname = nameFormat;
//                    Utils::trim(fname);
//                }
//                fs::path outdir = opts.outdir;
//                fs::path fname_path(fname);
//                fs::path out_path = outdir / fname_path;
//                if (!fs::exists(out_path.parent_path()) && !out_path.parent_path().empty()) {
//                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " path not found " << out_path.parent_path() << std::endl;
//                } else {
//                    if (!imageCacheQueue.empty()) {
//                        Manager::imagePngToFile(imageCacheQueue.back().second, out_path.string());
//                        Term::clearLine();
//                        std::cout << "\rSaved to " << out_path << std::endl;
//                    }
//                }
                valid = true;
            }
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
            error_report(reason);
        }
        inputText = "";
        return true;
    }

    int GwPlot::printRegionInfo() {
        int term_width = Utils::get_terminal_width() - 1;
        if (regions.empty()) {
            return term_width;
        }
        std::string pos_str = "\rPos   ";
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
        } else if (!tracks.empty()) {
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
                if (key == opts.scroll_right) {
                    int shift = (int)(((float)regions[regionSelection].end - (float)regions[regionSelection].start) * opts.scroll_speed);
                    delete regions[regionSelection].refSeq;
                    regions[regionSelection].start = regions[regionSelection].start + shift;
                    regions[regionSelection].end = regions[regionSelection].end + shift;
                    fetchRefSeq(regions[regionSelection]);

                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.region = &regions[regionSelection];
                            if (!bams.empty()) {
                                HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx],
                                                            indexes[cl.bamIdx], opts, (bool)opts.max_coverage, false,
                                                            &samMaxY, filters, pool);

                            }
                        }
                    }
                    processed = true;
                    redraw = true;
                    printRegionInfo();

                } else if (key == opts.scroll_left) {
                    int shift = (int)(((float)regions[regionSelection].end - (float)regions[regionSelection].start) * opts.scroll_speed);
                    shift = (regions[regionSelection].start - shift > 0) ? shift : 0;
                    if (shift == 0) {
                        return;
                    }
                    delete regions[regionSelection].refSeq;
                    regions[regionSelection].start = regions[regionSelection].start - shift;
                    regions[regionSelection].end = regions[regionSelection].end - shift;
                    fetchRefSeq(regions[regionSelection]);

                    Utils::Region N;
                    N.chrom = regions[regionSelection].chrom;
                    N.start = regions[regionSelection].start - shift;
                    N.end = regions[regionSelection].end - shift;
                    N.markerPos = regions[regionSelection].markerPos;
                    N.markerPosEnd = regions[regionSelection].markerPosEnd;
                    fetchRefSeq(N);
                    regions[regionSelection] = N;
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.region = &regions[regionSelection];
                            if (!bams.empty()) {
                                HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx],
                                                            indexes[cl.bamIdx], opts, (bool)opts.max_coverage, true,
                                                            &samMaxY, filters, pool);
                            }
                        }
                    }
                    processed = true;
                    redraw = true;
                    printRegionInfo();

                } else if (key == opts.zoom_out) {
                    int shift = (int)((((float)regions[regionSelection].end - (float)regions[regionSelection].start) * opts.scroll_speed)) + 10;
                    shift = (regions[regionSelection].start - shift > 0) ? shift : 0;
                    if (shift == 0) {
                        return;
                    }
                    delete regions[regionSelection].refSeq;
                    regions[regionSelection].start = regions[regionSelection].start - shift;
                    regions[regionSelection].end = regions[regionSelection].end + shift;
                    fetchRefSeq(regions[regionSelection]);
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.region = &regions[regionSelection];
                            cl.collection_processed = false;
                            if (!bams.empty()) {
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
                            }
                        }
                    }
                    if (opts.link_op != 0) {
                        HGW::refreshLinked(collections, opts, &samMaxY);
                    }
                    processed = true;
                    redraw = true;
                    printRegionInfo();

                } else if (key == opts.zoom_in) {
                    if (regions[regionSelection].end - regions[regionSelection].start > 50) {
                        int shift = (int)(((float)regions[regionSelection].end - (float)regions[regionSelection].start) * opts.scroll_speed);
                        shift = (regions[regionSelection].start - shift > 0) ? shift : 0;
                        if (shift == 0) {
                            return;
                        }
                        delete regions[regionSelection].refSeq;
                        regions[regionSelection].start = regions[regionSelection].start + shift;
                        regions[regionSelection].end = regions[regionSelection].end - shift;
                        fetchRefSeq(regions[regionSelection]);

                        for (auto &cl : collections) {
                            if (cl.regionIdx == regionSelection) {
                                cl.region = &regions[regionSelection];
                                cl.collection_processed = false;
                                if (!bams.empty()) {
                                    HGW::trimToRegion(cl, opts.max_coverage, opts.snp_threshold);
                                }
                            }
                        }
                        processed = true;
                        redraw = true;
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
                        if (cl.regionIdx == regionSelection) {  // should be possible to vScroll without needing findY for all reads, but complicated to implement
                            cl.vScroll += 2;
                            cl.levelsStart.clear();
                            cl.levelsEnd.clear();
                            cl.linked.clear();
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
                    size_t currentSize = (image_glob.empty()) ? currentVarTrack->multiRegions.size() : image_glob.size();
                    if (currentVarTrack->blockStart + bLen > (int)currentSize) {
                        currentVarTrack->blockStart += bLen;
                    }
                    if (!*currentVarTrack->trackDone) {
                        currentVarTrack->blockStart += bLen;
                        redraw = true;
                        Term::clearLine();
                        std::cout << termcolor::green << "\rIndex     " << termcolor::reset << currentVarTrack->blockStart << std::endl;
                    }
                } else if (key == opts.scroll_left) {
                    if (currentVarTrack->blockStart == 0) {
                        return;
                    }
                    currentVarTrack->blockStart = (currentVarTrack->blockStart - bLen > 0) ? currentVarTrack->blockStart - bLen : 0;
                    redraw = true;
                    Term::clearLine();
                    std::cout << termcolor::green << "\rIndex     " << termcolor::reset << currentVarTrack->blockStart << std::endl;
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

    void GwPlot::pathDrop(GLFWwindow* wind, int count, const char** paths) {
        bool good = false;
        for (int i=0; i < count; ++ i) {
            std::string pth = *paths;
            if (Utils::endsWith(pth, ".bam") || Utils::endsWith(pth, ".cram")) {
                good = true;
                std::cout << "Loading: " << pth << std::endl;
                bam_paths.push_back(pth);
                htsFile* f = sam_open(pth.c_str(), "r");
                hts_set_threads(f, opts.threads);
                bams.push_back(f);
                sam_hdr_t *hdr_ptr = sam_hdr_read(f);
                headers.push_back(hdr_ptr);
                hts_idx_t* idx = sam_index_load(f, pth.c_str());
                indexes.push_back(idx);
            } else if (Utils::endsWith(pth, ".vcf.gz") || Utils::endsWith(pth, ".vcf") || Utils::endsWith(pth, ".bcf")) {
                if (!image_glob.empty()) {
                    std::cerr << "Error: --images are already open, can not open variant file\n";
                    return;
                }
                good = true;
                std::vector<std::string> labels = Utils::split(opts.labels, ',');
                setLabelChoices(labels);

                mouseOverTileIndex = 0;
                bboxes = Utils::imageBoundingBoxes(opts.number, (float)fb_width, (float)fb_height);

                imageCache.clear();
                addVariantTrack(pth, opts.start_index, false);

                variantFileSelection = (int)variantTracks.size() - 1;
                currentVarTrack = &variantTracks[variantFileSelection];
//                setVariantFile(pth, opts.start_index, false);
                currentVarTrack->blockStart = 0;
                mode = Manager::Show::TILED;
                std::cout << termcolor::magenta << "File      " << termcolor::reset << variantTracks[variantFileSelection].path << "\n";
                std::cout << termcolor::green << "Index     " << termcolor::reset << currentVarTrack->blockStart << std::endl;
            }
            ++paths;
        }
        if (good) {
            processed = false;
            redraw = true;
        }
    }

    int GwPlot::getCollectionIdx(float x, float y) {
        if (y <= refSpace) {
            return REFERENCE_TRACK; //-2;  // reference
        } else if (!tracks.empty() && y >= refSpace + totalCovY + (trackY*(float)headers.size()) && y < (float)fb_height - refSpace) {
			int index = -3;
			float trackSpace = (float)fb_height - totalCovY - refSpace - refSpace - (trackY*(float)headers.size());
			trackSpace = trackSpace / (float)tracks.size();
			float cIdx = (y - (refSpace + totalCovY + (trackY*(float)headers.size()))) / trackSpace;
			index -= int(cIdx);
			if ((index * -1) - 3 > (int)tracks.size()) {
				index = -1;
			}
			return index;  // track
		}
        if (regions.empty()) {
            return NO_REGIONS; //-1;
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
        return NO_REGIONS; //-1;
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
            } else if (vv > xW) { break; }
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
        bool tool_popup = (xW <= 60 && yW >= half_h - 60 && yW <= half_h + 60);
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE && tool_popup) {
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
        if (commandToolTipIndex != -1 && captureText && mode != SETTINGS && button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            double xPos_fb = x;
            double yPos_fb = y;
            convertScreenCoordsToFrameBufferCoords(wind, &xPos_fb, &yPos_fb, fb_width, fb_height);
            if (xPos_fb > 50 && xPos_fb < 50 + fonts.overlayWidth * 20 ) {
                keyPress(wind, GLFW_KEY_ENTER, 0, GLFW_PRESS, 0);
                return;
            }
        }

        if (xDrag == -1000000) {
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

            if (yW >= (fb_height * 0.98)) {
                if (action == GLFW_PRESS) {
                    updateSlider(xW);
                }
                xOri = x;
                yOri = y;
                return;
            }

            int idx = getCollectionIdx(xW, yW);
            if (idx == REFERENCE_TRACK && action == GLFW_RELEASE) {
                Term::printRefSeq(xW, collections);
            } else if (idx <= TRACK && action == GLFW_RELEASE) {
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
					Term::printTrack(relX, tracks[(idx * -1) -3], &regions[tIdx], false);
				}
			}
            if (idx < 0) {
                return;
            }

            Segs::ReadCollection &cl = collections[idx];
            regionSelection = cl.regionIdx;
            if (action == GLFW_PRESS) {
                clicked = *cl.region;
                clickedIdx = idx;
            }
            if (std::abs(xDrag) < 5 && action == GLFW_RELEASE && !bams.empty()) {
                int pos = (int) (((xW - (float) cl.xOffset) / cl.xScaling) + (float) cl.region->start);

                if (ctrlPress) {  // zoom in to mouse position
                    int strt = pos - 2500;
                    strt = (strt < 0) ? 0 : strt;
                    Utils::Region N;
                    N.chrom = cl.region->chrom;
                    N.start = strt;
                    N.end = strt + 5000;
                    regionSelection = cl.regionIdx;
                    delete regions[regionSelection].refSeq;
                    N.markerPos = regions[regionSelection].markerPos;
                    N.markerPosEnd = regions[regionSelection].markerPosEnd;
                    fetchRefSeq(N);
                    regions[regionSelection] = N;
                    processed = false;
                    redraw = true;
                    return;
                }
                // highlight read below
                int level = 0;
                int slop = 0;
                if (!opts.tlen_yscale) {
                    level = (int) ((yW - (float) cl.yOffset) / ((trackY-(gap/2)) / (float)(cl.levelsStart.size() - cl.vScroll )));
                    if (level < 0) {  // print coverage info (mousePos functions already prints out cov info to console)
                        std::cout << std::endl;
                        return;
                    }
                    if (cl.vScroll < 0) {
                        level += cl.vScroll + 1;
                    }
                } else {
                    int max_bound = (opts.max_tlen) + (cl.vScroll * 100);
                    level = (int) ((yW - (float) cl.yOffset) / (trackY / (float)(max_bound)));
                    slop = (int)((trackY / (float)opts.ylim) * 2);
                    slop = (slop <= 0) ? 5 : slop;
                }
                std::vector<Segs::Align>::iterator bnd;
                bnd = std::lower_bound(cl.readQueue.begin(), cl.readQueue.end(), pos,
                                       [&](const Segs::Align &lhs, const int pos) { return (int)lhs.pos <= pos; });
                while (bnd != cl.readQueue.begin()) {
                    if (!opts.tlen_yscale) {
                        if (bnd->y == level && (int)bnd->pos <= pos && pos < (int)bnd->reference_end) {
                            bnd->edge_type = 4;
                            target_qname = bam_get_qname(bnd->delegate);
                            Term::printRead(bnd, headers[cl.bamIdx], selectedAlign, cl.region->refSeq, cl.region->start, cl.region->end, opts.low_mem);
                            redraw = true;
                            processed = true;
                            break;
                        }
                    } else {
                        if ((bnd->y >= level - slop && bnd->y < level) && (int)bnd->pos <= pos && pos < (int)bnd->reference_end) {
                            bnd->edge_type = 4;
                            target_qname = bam_get_qname(bnd->delegate);
                            Term::printRead(bnd, headers[cl.bamIdx], selectedAlign, cl.region->refSeq, cl.region->start, cl.region->end, opts.low_mem);
                            redraw = true;
                            processed = true;
                        }
                    }
                    --bnd;
                }
                xDrag = -1000000;
                yDrag = -1000000;
                clickedIdx = -1;

            } else if (action == GLFW_RELEASE) {
                auto w = (float)(cl.region->end - cl.region->start) * (float) regions.size();
                if (w >= 50000) {
                    int travel = (int) (w * (xDrag / windowW));
                    Utils::Region N;
                    if (cl.region->start - travel < 0) {
                        travel = cl.region->start;
                        N.chrom = cl.region->chrom;
                        N.start = 0;
                        N.end = clicked.end - travel;
                    } else {
                        N.chrom = cl.region->chrom;
                        N.start = clicked.start - travel;
                        N.end = clicked.end - travel;
                    }
                    if (N.start < 0 || N.end < 0) {
                        return;
                    }
                    regionSelection = cl.regionIdx;
                    delete regions[regionSelection].refSeq;

                    N.markerPos = regions[regionSelection].markerPos;
                    N.markerPosEnd = regions[regionSelection].markerPosEnd;
                    fetchRefSeq(N);

                    bool lt_last = N.start < cl.region->start;
                    regions[regionSelection] = N;
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

        } else if (mode == Manager::SINGLE && button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
            if (regions.empty()) {
                return;
            }
            currentVarTrack = &variantTracks[variantFileSelection];
            if (!currentVarTrack->multiRegions.empty() || !imageCache.empty()) {
                mode = Manager::TILED;
                xDrag = -1000000;
                yDrag = -1000000;
                redraw = true;
                processed = false;
                imageCacheQueue.clear();
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
                    xDrag = -1000000;
                    yDrag = -1000000;
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
                        bool parsed = Utils::parseFilenameToMouseClick(image_glob[currentVarTrack->blockStart + i], rt, fai, opts.pad, opts.split_view_size);
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
                if (std::fabs(xDrag) > fb_width / 8.) {
                    int nmb = opts.number.x * opts.number.y;
                    if (xDrag > 0) {
                        currentVarTrack->blockStart = (currentVarTrack->blockStart - nmb < 0) ? 0 : currentVarTrack->blockStart - nmb;
                        redraw = true;
                        std::cout << "\r                                                                               ";
                        std::cout << termcolor::green << "\rIndex     " << termcolor::reset << currentVarTrack->blockStart << std::flush;
                    } else {
                        size_t targetSize = (image_glob.empty()) ? currentVarTrack->multiRegions.size() : image_glob.size();
                        if (currentVarTrack->blockStart + nmb >= (int)targetSize) {
                            return;
                        }
                        currentVarTrack->blockStart += nmb;
                        redraw = true;
                        std::cout << "\r                                                                               ";
                        std::cout << termcolor::green << "\rIndex     " << termcolor::reset << currentVarTrack->blockStart << std::flush;
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
                        xDrag = -1000000;
                        yDrag = -1000000;
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
                xDrag = -1000000;
                yDrag = -1000000;
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

    void GwPlot::updateCursorGenomePos(Segs::ReadCollection &cl, float xPos) {
        if (regions.empty() || mode == TILED) {
            return;
        }
        int pos = ((int) (((double)xPos - (double)cl.xOffset) / (double)cl.xScaling)) + cl.region->start;
        std::string s = Term::intToStringCommas(pos);
        int term_width_remaining = printRegionInfo();
        s = "    " + s;
        if (term_width_remaining < (int)s.size()) {
            return;
        }
        std::cout << s << std::flush;
        if (!bam_paths.empty()) {
            term_width_remaining -= (int)s.size();
            std::string base_filename = "  -  " + bam_paths[cl.bamIdx].substr(bam_paths[cl.bamIdx].find_last_of("/\\") + 1);
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
        convertScreenCoordsToFrameBufferCoords(wind, &xPos_fb, &yPos_fb, fb_width, fb_height);
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
                if (yPos_fb >= (fb_height * 0.98)) {
                    if (yOri >= (windY * 0.98)) {
                        updateSlider((float) xPos_fb);
                    }
                    return;
                }
                int idx = getCollectionIdx((float) xPos_fb, (float) yPos_fb);
                if (idx < 0) {
                    return;
                }
                Segs::ReadCollection &cl = collections[idx];
                regionSelection = cl.regionIdx;
                if (clickedIdx == -1 || idx != clickedIdx) {
                    return;
                }
                int windowW, windowH;
                glfwGetWindowSize(wind, &windowW, &windowH);

                if (std::fabs(xDrag) > std::fabs(yDrag) && cl.region->end - cl.region->start < 50000) {
                    printRegionInfo();
                    auto w = (float) (cl.region->end - cl.region->start) * (float) regions.size();
                    int travel = (int) (w * (xDrag / windowW));
                    Utils::Region N;
                    if (cl.region->start - travel < 0) {
                        travel = cl.region->start;
                        N.chrom = cl.region->chrom;
                        N.start = 0;
                        N.end = clicked.end - travel;
                    } else {
                        N.chrom = cl.region->chrom;
                        N.start = clicked.start - travel;
                        N.end = clicked.end - travel;
                    }
                    if (N.start < 0 || N.end < 0) {
                        return;
                    }
                    regionSelection = cl.regionIdx;
                    delete regions[regionSelection].refSeq;
                    N.markerPos = regions[regionSelection].markerPos;
                    N.markerPosEnd = regions[regionSelection].markerPosEnd;
                    fetchRefSeq(N);
                    regions[regionSelection] = N;
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.region = &regions[regionSelection];
                            if (!bams.empty()) {
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
                    if (std::fabs(yDrag) > std::fabs(xDrag) && std::fabs(yDrag) > 1) {
                        float travel_y = yDrag / ((float) windowH / (float) cl.levelsStart.size());
                        if (std::fabs(travel_y) > 1) {
                            cl.vScroll -= (int) travel_y;
                            cl.vScroll = (cl.vScroll <= 0) ? 0 : cl.vScroll;

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
                    redraw = true;
                    glfwPostEmptyEvent();
                    return;
                }
            }
        } else {
            if (mode == Manager::SINGLE) {
                if (regions.empty()) {
                    return;
                }
                int rs = getCollectionIdx((float)xPos_fb, (float)yPos_fb);
	            if (rs <= TRACK) {  // print track info
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
			            Term::printTrack(relX, tracks[(rs * -1) -3], &regions[tIdx], true);
		            }
	            }
                if (rs < 0) { // print reference info
                    if (rs == REFERENCE_TRACK) {
                        if (collections.empty()) {
                            float xScaling = (float)((regionWidth - gap - gap) / ((double)(regions[regionSelection].end -regions[regionSelection].start)));
                            float xOffset = (regionWidth * (float)regionSelection) + gap;
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
                updateCursorGenomePos(cl, (float)xPos_fb);
            } else if (mode == TILED) {
                currentVarTrack = &variantTracks[variantFileSelection];
                int i = 0;
                for (auto &b: bboxes) {
                    if (xPos > b.xStart && xPos < b.xEnd && yPos > b.yStart && yPos < b.yEnd) {
                        break;
                    }
                    ++i;
                }
                if (i == (int)bboxes.size()) {
                    return;
                }
                if (currentVarTrack->blockStart + i < (int)currentVarTrack->multiRegions.size()) {
                    Utils::Label &lbl = currentVarTrack->multiLabels[currentVarTrack->blockStart + i];
                    lbl.mouseOver = true;
                    if (i != mouseOverTileIndex) {
                        mouseOverTileIndex = i;
                    }
                    Term::clearLine();
                    std::cout << termcolor::bold << "\rPos  " << termcolor::reset << lbl.chrom << ":" << lbl.pos << termcolor::bold <<
                              "    ID  "  << termcolor::reset << lbl.variantId << termcolor::bold <<
                              "    Type  "  << termcolor::reset << lbl.vartype << termcolor::bold <<
                              "    Index  "  << termcolor::reset << mouseOverTileIndex + currentVarTrack->blockStart;
                    std::cout << std::flush;
                // todo check this currentVarTrack->
                } else if (currentVarTrack->blockStart + i < (int)image_glob.size()) {
                    std::vector<Utils::Region> rt;
                    if (Utils::parseFilenameToMouseClick(image_glob[currentVarTrack->blockStart + i], rt, fai, opts.pad, opts.split_view_size)) {
                        Term::clearLine();
                        std::cout << termcolor::bold << "\rPos  ";
                        for (auto &r : rt) {
                            std::cout << termcolor::reset << r.chrom << ":" << r.start << "-" << r.end << termcolor::bold << "    ";
                        }
                        std::cout << "File  "  << termcolor::reset << image_glob[currentVarTrack->blockStart + i].filename();
                        std::cout << std::flush;
                    }
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
