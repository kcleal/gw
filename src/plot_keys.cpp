//
// Created by Kez Cleal on 23/08/2022.
//
#include <htslib/faidx.h>
#include <htslib/hfile.h>
#include <htslib/sam.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <iterator>
#include <stdlib.h>
#include <sstream>
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
//#include "../inc/glfw_keys.h"
//#include "hts_funcs.h"
#include "plot_manager.h"
#include "segments.h"
#include "../inc/robin_hood.h"
#include "../inc/termcolor.h"
#include "themes.h"


namespace Manager {

    // keeps track of input commands
    bool GwPlot::registerKey(GLFWwindow* wind, int key, int scancode, int action, int mods) {
        if (action == GLFW_RELEASE) {
            if ((key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) && !captureText) {
                shiftPress = false;
            }
            ctrlPress = false;
            return false;
        }
        if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) {
            shiftPress = true;
        } else if (shiftPress && GLFW_KEY_SEMICOLON && !captureText) {
            captureText = true;
            inputText.append(":");
            std::cout << "\r" << inputText << std::flush;
        } else {
            shiftPress = false;
        }
        if (captureText) {
            if (key == GLFW_KEY_ENTER) {
                captureText = false;
                processText = true;
                shiftPress = false;
                std::cout << "\n";
                return false;
            }
            if (!commandHistory.empty()) {
                if (key == GLFW_KEY_UP && commandIndex > 0) {
                    commandIndex -= 1;
                    inputText = commandHistory[commandIndex];
                    std::cout << "\r" << inputText << std::flush;
                    return true;
                } else if (key == GLFW_KEY_DOWN && commandIndex < commandHistory.size() - 1) {
                    commandIndex += 1;
                    inputText = commandHistory[commandIndex];
                    std::cout << "\r" << inputText << std::flush;
                    return true;
                }
            }

            if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_LEFT_SUPER) {
                if (action == GLFW_PRESS) {
                    ctrlPress = true;
                }
            }
            if (ctrlPress && key == GLFW_KEY_V) {
                std::string string = glfwGetClipboardString(window);
                if (!string.empty()) {
                    inputText.append(string);
                    std::cout << "\r" << inputText << std::flush;
                }
            } else {  // character entry
                if (key == GLFW_KEY_SEMICOLON && inputText.size() == 1) {
                    return true;
                } else if (key == GLFW_KEY_BACKSPACE) {
                    if (inputText.size() > 1) {
                        inputText.pop_back();
                        std::string emptyS(100, ' ');
                        std::cout << "\r" << emptyS << std::flush;
                        std::cout << "\r" << inputText << std::flush;
                    }
                }
                const char *letter = glfwGetKeyName(key, scancode);
                if (letter || key == GLFW_KEY_SPACE) {
                    if (key == GLFW_KEY_SPACE) {
                        inputText.append(" ");
                    } else if (key == GLFW_KEY_SEMICOLON && mods == GLFW_MOD_SHIFT) {
                        inputText.append(":");
                    } else {
                        if (mods == GLFW_MOD_SHIFT) { // uppercase
//                            char let = toupper(*letter);
//                            std::string str = toupper(*letter);
//                            inputText.append(str);
                        } else {
                            inputText.append(letter);
                        }

                    }
                    std::cout << "\r" << inputText << std::flush;
                }
            }
            return true;
        }
        return true;
    }

    void printKeyFromValue(int v) {
        robin_hood::unordered_map<std::string, int> key_table;
        Keys::getKeyTable(key_table);
        for (auto &p: key_table) {
            if (p.second == v) {
                std::cout << p.first;
                break;
            }
        }
    }

    void help(Themes::IniOptions &opts) {
        std::cout << termcolor::italic << "\n** Enter a command by selecting the GW window (not the terminal) and type ':[COMMAND]' **\n" << termcolor::reset;
        std::cout << termcolor::underline << "\nCommand          Modifier        Description                                          \n" << termcolor::reset; // 15 spaces
        std::cout << termcolor::green << "goto             loci, index     " << termcolor::reset << "e.g. 'goto chr1:20000'. Use index if multiple regions \n                                 are open e.g. 'goto chr1:20000 1'\n";
        std::cout << termcolor::green << "link             [none/sv/all]   " << termcolor::reset << "Switch read-linking ':link all'\n";
        std::cout << termcolor::green << "quit, q                          " << termcolor::reset << "Quit GW\n";
        std::cout << termcolor::green << "refresh, r                       " << termcolor::reset << "Refresh and re-draw the window\n";
        std::cout << termcolor::underline << "\nHot keys                   \n" << termcolor::reset;
        std::cout << "scroll left       " << termcolor::bright_yellow; printKeyFromValue(opts.scroll_left); std::cout << "\n" << termcolor::reset;
        std::cout << "scroll right      " << termcolor::bright_yellow; printKeyFromValue(opts.scroll_right); std::cout << "\n" << termcolor::reset;
        std::cout << "scroll down       " << termcolor::bright_yellow; printKeyFromValue(opts.scroll_down); std::cout << "\n" << termcolor::reset;
        std::cout << "scroll up         " << termcolor::bright_yellow; printKeyFromValue(opts.scroll_up); std::cout << "\n" << termcolor::reset;
        std::cout << "zoom in           " << termcolor::bright_yellow; printKeyFromValue(opts.zoom_in); std::cout << "\n" << termcolor::reset;
        std::cout << "zoom out          " << termcolor::bright_yellow; printKeyFromValue(opts.zoom_out); std::cout << "\n" << termcolor::reset;
        std::cout << "next region view  " << termcolor::bright_yellow; printKeyFromValue(opts.next_region_view); std::cout << "\n" << termcolor::reset;
        std::cout << "cycle link mode   " << termcolor::bright_yellow; printKeyFromValue(opts.cycle_link_mode); std::cout << "\n" << termcolor::reset;
        std::cout << "\n";
    }

    bool GwPlot::commandProcessed() {
        if (inputText.empty()) {
            return false;
        }
        bool valid = false;

        if (inputText == ":q" || inputText == ":quit") {
            throw CloseException();
        } else if (inputText == ":help" || inputText == ":h") {
            help(opts);
            valid = true;
        } else if (inputText == ":refresh" || inputText == ":r") {
            redraw = true; processed = false; valid = true;
        } else if (inputText == ":link" || inputText == ":link all") {
            opts.link_op = 2; valid = true;
        } else if (inputText == ":link sv") {
            opts.link_op = 1; valid = true;
        } else if (inputText == ":link none") {
            opts.link_op = 0; valid = true;
        } else if (Utils::startsWith(inputText, ":goto")) {
            constexpr char delim = ' ';
            std::vector<std::string> split = Utils::split(inputText, delim);
            if (split.size() > 1 && split.size() < 4) {
                int index = (split.size() == 3) ? std::stoi(split.back()) : 0;
                if (index < regions.size()) {
                    regions[index] = Utils::parseRegion(split[1]);
                    valid = true;
                } else {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " region index is out of range. Use 0-based indexing\n";
                    inputText = "";
                    return true;
                }
            }
        }
        if (valid) {
            commandHistory.push_back(inputText);
            redraw = true;
            processed = false;
        } else {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " command not understood\n";
        }
        inputText = "";

        return true;
    }

    std::string removeZeros(float value) {  // https://stackoverflow.com/questions/57882748/remove-trailing-zero-in-c
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << value;
        std::string str = ss.str();
        if(str.find('.') != std::string::npos) {
            str = str.substr(0, str.find_last_not_of('0')+1);
            if(str.find('.') == str.size()-1) {
                str = str.substr(0, str.size()-1);
            }
        }
        return str;
    }

    std::string getSize(long num) {
        int chars_needed = std::ceil(std::log10(num));
        double d;
        std::string a;
        std::string b = " bp";
        if (chars_needed > 3) {
            if (chars_needed > 6) {
                d = (double)num / 1e6;
                d = std::ceil(d * 10) / 10;
                a = removeZeros(d);
                b = " mb";
            } else {
                d = (double)num / 1e3;
                d = std::ceil(d * 10) / 10;
                a = removeZeros(d);
                b = " kb";
            }
        } else {
            a = std::to_string(num);
        }
        return a + b;

    }

    void GwPlot::printRegionInfo() {
        if (regions.empty()) {
            return;
        }
        std::cout << termcolor::magenta << "Showing   " ;
        int i = 0;
        for (auto &r : regions) {
            std::cout << termcolor::cyan << regions[0].chrom << ":" << r.start << "-" << r.end << termcolor::white << "  (" << getSize(r.end - r.start) << ")";
            if (i != regions.size() - 1) {
                std::cout << termcolor::magenta << "  |  ";
            }
            i += 1;
        }

        std::cout << termcolor::reset << std::endl;

    }

    void GwPlot::keyPress(GLFWwindow* wind, int key, int scancode, int action, int mods) {

        if (action == GLFW_RELEASE) {
            return;
        }

        // decide if the input key is part of a command or a redraw request
        bool res = registerKey(window, key, scancode, action, mods);

        if (captureText) {
            return;
        }

        try {
            if (commandProcessed()) {
                return;
            }
        } catch (CloseException & mce) {
            glfwSetWindowShouldClose(wind, GLFW_TRUE);
        }


        if (mode == Show::SINGLE) {
            int i;
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                if (key == opts.scroll_right) {
                    int shift = (regions[regionSelection].end - regions[regionSelection].start) * opts.scroll_speed;
                    delete regions[regionSelection].refSeq;
                    Utils::Region N;
                    N.chrom = regions[regionSelection].chrom;
                    N.start = regions[regionSelection].start + shift;
                    N.end = regions[regionSelection].end + shift;
                    fetchRefSeq(N);
                    regions[regionSelection] = N;
                    processed = true;
                    int i = 0;
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.region = N; //regions[regionSelection];
                            HTS::appendReadsAndCoverage(cl,  bams[i], headers[i], indexes[i], opts, opts.coverage, false, &vScroll, linked, &samMaxY);
                        }
                        ++i;
                    }
                    redraw = true;
                    printRegionInfo();

                } else if (key == opts.scroll_left) {
                    int shift = (regions[regionSelection].end - regions[regionSelection].start) * opts.scroll_speed;
                    shift = (regions[regionSelection].start - shift > 0) ? shift : 0;
                    delete regions[regionSelection].refSeq;
                    Utils::Region N;
                    N.chrom = regions[regionSelection].chrom;
                    N.start = regions[regionSelection].start - shift;
                    N.end = regions[regionSelection].end - shift;
                    fetchRefSeq(N);
                    regions[regionSelection] = N;
                    processed = true;
                    i = 0;
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.region = regions[regionSelection];
                            HTS::appendReadsAndCoverage(cl,  bams[i], headers[i], indexes[i], opts, opts.coverage, true, &vScroll, linked, &samMaxY);
                        }
                        ++i;
                    }
                    redraw = true;
                    printRegionInfo();
                } else if (key == opts.zoom_out) {
                    int shift = ((regions[regionSelection].end - regions[regionSelection].start) * opts.scroll_speed) + 10;
                    int shift_left = (regions[regionSelection].start - shift > 0) ? shift : 0;
                    delete regions[regionSelection].refSeq;
                    Utils::Region N;
                    N.chrom = regions[regionSelection].chrom;
                    N.start = regions[regionSelection].start - shift_left;
                    N.end = regions[regionSelection].end + shift;
                    fetchRefSeq(N);
                    regions[regionSelection] = N;
                    processed = true;
                    i = 0;
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.region = regions[regionSelection];
                            HTS::appendReadsAndCoverage(cl,  bams[i], headers[i], indexes[i], opts, false, true, &vScroll, linked, &samMaxY);
                            HTS::appendReadsAndCoverage(cl,  bams[i], headers[i], indexes[i], opts, false, false, &vScroll, linked, &samMaxY);
                            if (opts.coverage) {  // re process coverage for all reads
                                cl.covArr.resize(cl.region.end - cl.region.start);
                                std::fill(cl.covArr.begin(), cl.covArr.end(), 0);
                                int l_arr = (int)cl.covArr.size() - 1;
                                for (auto &i : cl.readQueue) {
                                    Segs::addToCovArray(cl.covArr, i, cl.region.start, cl.region.end, l_arr);
                                }
                            }
                        }
                        ++i;
                    }
                    redraw = true;
                    printRegionInfo();
                } else if (key == opts.zoom_in) {
                    if (regions[regionSelection].end - regions[regionSelection].start > 50) {
                        int shift = (regions[regionSelection].end - regions[regionSelection].start) * opts.scroll_speed;
                        int shift_left = (regions[regionSelection].start - shift > 0) ? shift : 0;
                        delete regions[regionSelection].refSeq;
                        Utils::Region N;
                        N.chrom = regions[regionSelection].chrom;
                        N.start = regions[regionSelection].start + shift_left;
                        N.end = regions[regionSelection].end - shift;
                        fetchRefSeq(N);
                        regions[regionSelection] = N;
                        processed = true;
                        i = 0;
                        for (auto &cl : collections) {
                            if (cl.regionIdx == regionSelection) {
                                cl.region = regions[regionSelection];
                                HTS::trimToRegion(cl, opts.coverage);
                            }
                            ++i;
                        }
                        redraw = true;
                        printRegionInfo();
                    }
                } else if (key == opts.next_region_view) {
                    regionSelection += 1;
                    if (regionSelection >= regions.size()) {
                        regionSelection = 0;
                    }
                    std::cout << "Region selection " << regionSelection << std::endl;
                } else if (key == opts.cycle_link_mode) {
                    opts.link_op = (opts.link_op == 2) ? 0 : opts.link_op += 1;
                    std::string lk = (opts.link_op > 0) ? ((opts.link_op == 1) ? "sv" : "all") : "none";
                    std::cout << "Linking selection " << lk << std::endl;
                    processed = false;
                    redraw = true;
                }
            }
        } else {  // show::TILED
            int bLen = opts.number.x * opts.number.y;
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                if (key == opts.scroll_right) {
                    blockStart += bLen;
                    redraw = true;
                } else if (key == opts.scroll_left) {
                    blockStart = (blockStart - bLen > 0) ? blockStart - bLen : 0;
                    redraw = true;
                } else if (key == opts.zoom_out) {
                    opts.number.x += 1;
                    opts.number.y += 1;
                    redraw = true;
                } else if (key == opts.zoom_in) {
                    opts.number.x = (opts.number.x - 1 > 0) ? opts.number.x - 1 : 1;
                    opts.number.y = (opts.number.y - 1 > 0) ? opts.number.y - 1 : 1;
                    redraw = true;
                }
            }
        }
        if (redraw) {
            linked.clear();
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
                linked.resize(bams.size());
            } else if (Utils::endsWith(pth, ".vcf.gz") || Utils::endsWith(pth, ".vcf") || Utils::endsWith(pth, ".bcf")) {
                good = true;
                std::cout << "Loading: " << pth << std::endl;
                setVariantFile(pth);
                imageCache.clear();
                blockStart = 0;
                mode = Manager::Show::TILED;
            }
            ++paths;
        }
        if (good) {
            processed = false;
            redraw = true;
        }
    }
}