//
// Created by Kez Cleal on 23/08/2022.
//
#include <htslib/faidx.h>
#include <htslib/hfile.h>
#include <htslib/sam.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <stdlib.h>
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
//#include "hts_funcs.h"
#include "plot_manager.h"
#include "segments.h"
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

    bool GwPlot::commandProcessed() {
        if (inputText.empty()) {
            return false;
        }
        bool valid = false;

        if (inputText == ":q" || inputText == ":quit" || inputText == ":exit") {
            throw CloseException();
        } //else if (inputText.)

        if (valid) {
            commandHistory.push_back(inputText);
        }

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
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                if (key == opts.scroll_right) {
                    int shift = (regions[regionSelection].end - regions[regionSelection].start) * opts.scroll_speed;
                    regions[regionSelection].start += shift;
                    regions[regionSelection].end += shift;
                    processed = false;
                    redraw = true;
                    fetchRefSeq(regions[regionSelection]);
                    printRegionInfo();
                } else if (key == opts.scroll_left) {
                    int shift = (regions[regionSelection].end - regions[regionSelection].start) * opts.scroll_speed;
                    shift = (regions[regionSelection].start - shift > 0) ? shift : regions[regionSelection].start;
                    regions[regionSelection].start -= shift;
                    regions[regionSelection].end -= shift;
                    processed = false;
                    redraw = true;
                    fetchRefSeq(regions[regionSelection]);
                    printRegionInfo();
                } else if (key == opts.zoom_out) {
                    int shift = ((regions[regionSelection].end - regions[regionSelection].start) * opts.scroll_speed) + 10;
                    int shift_left = (regions[regionSelection].start - shift > 0) ? shift : regions[regionSelection].start;
                    regions[regionSelection].start -= shift_left;
                    regions[regionSelection].end += shift;
                    processed = false;
                    redraw = true;
                    fetchRefSeq(regions[regionSelection]);
                    printRegionInfo();
                } else if (key == opts.zoom_in) {
                    if (regions[regionSelection].end - regions[regionSelection].start > 50) {
                        int shift = (regions[regionSelection].end - regions[regionSelection].start) * opts.scroll_speed;
                        int shift_left = (regions[regionSelection].start - shift > 0) ? shift : regions[regionSelection].start;
                        regions[regionSelection].start += shift_left;
                        regions[regionSelection].end -= shift;
                        processed = false;
                        redraw = true;
                        fetchRefSeq(regions[regionSelection]);
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