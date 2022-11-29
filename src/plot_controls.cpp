//
// Created by Kez Cleal on 23/08/2022.
//
#include <htslib/sam.h>

#include <cmath>
#include <iomanip>
#include <iterator>
#include <cstdlib>

#include <string>
#include <thread>
#include <vector>

//#ifdef __APPLE__
//#include <OpenGL/gl.h>
//#endif

#include "htslib/hts.h"

#include <GLFW/glfw3.h>
#define SK_GL

#include "drawing.h"

#include "hts_funcs.h"
#include "parser.h"
#include "plot_manager.h"
#include "segments.h"
#include "../include/termcolor.h"
#include "themes.h"


namespace Manager {

    constexpr char basemap[] = {'.', 'A', 'C', '.', 'G', '.', '.', '.', 'T', '.', '.', '.', '.', '.', 'N', 'N', 'N'};

    void clearLine() {
        std::string s(Utils::get_terminal_width() - 1, ' ');
        std::cout << "\r" << s << std::flush;
        return;
    }

    void editInputText(std::string &inputText, const char *letter, int &charIndex) {
        if (charIndex != (int)inputText.size()) {
            inputText.insert(charIndex, letter);
            charIndex += 1;
            std::cout << "\r" << inputText.substr(0, charIndex) << "_" << inputText.substr(charIndex, inputText.size()) << std::flush;
        } else {
            inputText.append(letter);
            charIndex = inputText.size();
            std::cout << "\r" << inputText << std::flush;
        }
    }

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
            charIndex = inputText.size();
            std::cout <<  "\n" << inputText << std::flush;
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
                    charIndex = inputText.size();
                    clearLine();
                    std::cout << "\r" << inputText << std::flush;
                    return true;
                } else if (key == GLFW_KEY_DOWN && commandIndex < (int)commandHistory.size() - 1) {
                    commandIndex += 1;
                    inputText = commandHistory[commandIndex];
                    charIndex = inputText.size();
                    clearLine();
                    std::cout << "\r" << inputText << std::flush;
                    return true;
                }
            }

            if (key == GLFW_KEY_LEFT) {
                charIndex = (charIndex - 1 >= 0) ? charIndex - 1 : charIndex;
                std::cout << "\r" << inputText.substr(0, charIndex) << "_" << inputText.substr(charIndex, inputText.size()) << std::flush;
                return true;
            } else if (key == GLFW_KEY_RIGHT) {
                charIndex = (charIndex < (int)inputText.size()) ? charIndex + 1 : charIndex;
                std::cout << "\r" << inputText.substr(0, charIndex) << "_" << inputText.substr(charIndex, inputText.size()) << std::flush;
                return true;
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
                    charIndex = inputText.size();
                    std::cout << "\r" << inputText << std::flush;
                }
            } else {  // character entry
                if (key == GLFW_KEY_SEMICOLON && inputText.size() == 1) {
                    return true;
                } else if (key == GLFW_KEY_BACKSPACE) {
                    if (inputText.size() > 1) {
                        inputText.erase(charIndex - 1, 1);
                        charIndex -= 1;
                        std::string emptyS(100, ' ');
                        std::cout << "\r" << emptyS << std::flush;
                        std::cout << "\r" << inputText << std::flush;
                    }
                }
                const char *letter = glfwGetKeyName(key, scancode);
                if (letter || key == GLFW_KEY_SPACE) {
                    if (key == GLFW_KEY_SPACE) {  // deal with special keys first
                        editInputText(inputText, " ", charIndex);
                    } else if (key == GLFW_KEY_SEMICOLON && mods == GLFW_MOD_SHIFT) {
                        editInputText(inputText, ":", charIndex);
                    } else if (key == GLFW_KEY_1 && mods == GLFW_MOD_SHIFT) {  // this will probaaly not work for every keyboard
                        editInputText(inputText, "!", charIndex);
                    } else if (key == GLFW_KEY_7 && mods == GLFW_MOD_SHIFT) {
                        editInputText(inputText, "&", charIndex);
                    } else if (key == GLFW_KEY_COMMA && mods == GLFW_MOD_SHIFT) {
                        editInputText(inputText, "<", charIndex);
                    } else if (key == GLFW_KEY_PERIOD && mods == GLFW_MOD_SHIFT) {
                        editInputText(inputText, ">", charIndex);
                    } else {
                        if (mods == GLFW_MOD_SHIFT) { // uppercase
                            std::string str = letter;
                            std::transform(str.begin(), str.end(),str.begin(), ::toupper);
                            editInputText(inputText, str.c_str(), charIndex);
                        } else {  // normal text here
                            editInputText(inputText, letter, charIndex);
                        }
                    }
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

    void GwPlot::highlightQname() { // todo make this more efficient
        for (auto &cl : collections) {
            for (auto &a: cl.readQueue) {
                if (bam_get_qname(a.delegate) == target_qname) {
                    a.edge_type = 4;
                }
            }
        }
    }

    void printCigar(std::vector<Segs::Align>::iterator r) {
        uint32_t l, cigar_l, op, k;
        uint32_t *cigar_p;
        cigar_l = r->delegate->core.n_cigar;
        cigar_p = bam_get_cigar(r->delegate);
        for (k = 0; k < cigar_l; k++) {
            op = cigar_p[k] & BAM_CIGAR_MASK;
            l = cigar_p[k] >> BAM_CIGAR_SHIFT;
            if (op == 0) {
                std::cout << l << "M";
            } else if (op == 1) {
                std::cout << termcolor::magenta << l << "I" << termcolor::reset;
            } else if (op == 2) {
                std::cout << termcolor::red << l << "D"<< termcolor::reset;
            } else if (op == 8) {
                std::cout << l << "X";
            } else if (op == 4) {
                std::cout << termcolor::bright_blue << l << "S"<< termcolor::reset;
            } else if (op == 5) {
                std::cout << termcolor::blue << l << "H" << termcolor::reset;
            }
            else {
                std::cout << termcolor::blue << l << "?" << termcolor::reset;
            }
        }
    }

    void printSeq(std::vector<Segs::Align>::iterator r, int max=5000) {
        auto l_seq = (int)r->delegate->core.l_qseq;

        if (l_seq == 0) {
            std::cout << "*";
            return;
        }
        uint32_t l, cigar_l, op, k;
        uint32_t *cigar_p;
        cigar_l = r->delegate->core.n_cigar;
        cigar_p = bam_get_cigar(r->delegate);
        uint8_t *ptr_seq = bam_get_seq(r->delegate);
        int i = 0;

        for (k = 0; k < cigar_l; k++) {
            op = cigar_p[k] & BAM_CIGAR_MASK;
            l = cigar_p[k] >> BAM_CIGAR_SHIFT;
            if (i >= max) {
                std::cout << "...";
                return;
            }
            if (op == BAM_CHARD_CLIP) {
                continue;
            } else if (op == BAM_CDEL) {
                for (int n=0; n < (int)l; ++n) {
                    std::cout << "-";
                }

            } else if (op == BAM_CMATCH) {
                for (int n = 0; n < (int)l; ++n) {
                    uint8_t base = bam_seqi(ptr_seq, i);
                    bool mm = false;
                    for (auto &item: r->mismatches) {
                        if (i == (int)item.idx) {
                            std::cout << termcolor::underline;
                            switch (basemap[base]) {
                                case 65 :
                                    std::cout << termcolor::green << "A" << termcolor::reset;
                                    break;
                                case 67 :
                                    std::cout << termcolor::blue << "C" << termcolor::reset;
                                    break;
                                case 71 :
                                    std::cout << termcolor::yellow << "G" << termcolor::reset;
                                    break;
                                case 78 :
                                    std::cout << termcolor::grey << "N" << termcolor::reset;
                                    break;
                                case 84 :
                                    std::cout << termcolor::red << "T" << termcolor::reset;
                                    break;
                            }
                            mm = true;
                            break;
                        }
                    }
                    if (!mm) {
                        switch (basemap[base]) {
                            case 65 :
                                std::cout << "A";
                                break;
                            case 67 :
                                std::cout << "C";
                                break;
                            case 71 :
                                std::cout << "G";
                                break;
                            case 78 :
                                std::cout << "N";
                                break;
                            case 84 :
                                std::cout << "T";
                                break;
                        }
                    }
                    i += 1;
                }

            } else if (op == BAM_CEQUAL) {
                for (int n = 0; n < (int)l; ++n) {
                    uint8_t base = bam_seqi(ptr_seq, i);
                    switch (basemap[base]) {
                        case 65 :
                            std::cout << "A";
                            break;
                        case 67 :
                            std::cout << "C";
                            break;
                        case 71 :
                            std::cout << "G";
                            break;
                        case 78 :
                            std::cout << "N";
                            break;
                        case 84 :
                            std::cout << "T";
                            break;
                    }
                    i += 1;
                }

            } else if (op == BAM_CDIFF) {
                for (int n = 0; n < (int)l; ++n) {
                    uint8_t base = bam_seqi(ptr_seq, i);
                    switch (basemap[base]) {
                        case 65 :
                            std::cout << termcolor::green << "A" << termcolor::reset;
                            break;
                        case 67 :
                            std::cout << termcolor::blue << "C" << termcolor::reset;
                            break;
                        case 71 :
                            std::cout << termcolor::yellow << "G" << termcolor::reset;
                            break;
                        case 78 :
                            std::cout << termcolor::grey << "N" << termcolor::reset;
                            break;
                        case 84 :
                            std::cout << termcolor::red << "T" << termcolor::reset;
                            break;
                    }
                    i += 1;
                }

            } else {
                for (int n=0; n < (int)l; ++n) {
                    uint8_t base = bam_seqi(ptr_seq, i);
                    switch (basemap[base]) {
                        case 65 : std::cout << termcolor::green << "A" << termcolor::reset; break;
                        case 67 : std::cout << termcolor::blue << "C" << termcolor::reset; break;
                        case 71 : std::cout << termcolor::yellow << "G" << termcolor::reset; break;
                        case 78 : std::cout << termcolor::grey << "N" << termcolor::reset; break;
                        case 84 : std::cout << termcolor::red << "T" << termcolor::reset; break;
                    }
                    i += 1;
                }
            }
        }
    }

    void read2sam(std::vector<Segs::Align>::iterator r, const sam_hdr_t* hdr, std::string &sam) {
        uint32_t l, cigar_l, op, k;
        uint32_t *cigar_p;
        cigar_l = r->delegate->core.n_cigar;
        cigar_p = bam_get_cigar(r->delegate);
        const char *rname = sam_hdr_tid2name(hdr, r->delegate->core.tid);
        std::string d = "\t";
        std::ostringstream oss;
        oss << bam_get_qname(r->delegate) << d
            << r->delegate->core.flag << d
            << rname << d
            << r->pos + 1 << d
            << (int)r->delegate->core.qual << d;
        if (cigar_l) {
            for (k = 0; k < cigar_l; k++) {
                op = cigar_p[k] & BAM_CIGAR_MASK;
                l = cigar_p[k] >> BAM_CIGAR_SHIFT;
                oss << l;
                switch (op) {
                    case 0: oss << "M"; break;
                    case 1: oss << "I"; break;
                    case 2: oss << "D"; break;
                    case 3: oss << "N"; break;
                    case 4: oss << "S"; break;
                    case 5: oss << "H"; break;
                    case 6: oss << "P"; break;
                    case 7: oss << "="; break;
                    case 8: oss << "X"; break;
                    default: oss << "B";
                }
            } oss << d;
        } else {
            oss << "*" << d;
        }
        if (r->delegate->core.mtid < 0) {
            oss << "*" << d;
        } else if (r->delegate->core.mtid == r->delegate->core.tid) {
            oss << "=" << d;
        } else {
            oss << sam_hdr_tid2name(hdr, r->delegate->core.mtid) << d;
        }
        oss << r->delegate->core.mpos + 1 << d;
        oss << r->delegate->core.isize << d;
        if (r->delegate->core.l_qseq) {
            uint8_t *ptr_seq = bam_get_seq(r->delegate);
            for (int n = 0; n < r->delegate->core.l_qseq; ++n) {
                oss << basemap[bam_seqi(ptr_seq, n)];
            }
            oss << d;
            uint8_t *ptr_qual = bam_get_qual(r->delegate);
            for (int n = 0; n < r->delegate->core.l_qseq; ++n) {
                uint8_t qual = ptr_qual[n];
                oss << (char)(qual + 33);
            }
            oss << d;
        } else {
            oss << "*" << d << "*" << d;
        }
        uint8_t *s = bam_get_aux(r->delegate);
        uint8_t *end = r->delegate->data + r->delegate->l_data;
        kstring_t str = { 0, 0, nullptr };
        while (end - s >= 4) {
            kputc_('\t', &str);
            if ((s = (uint8_t *)sam_format_aux1(s, s[2], s+3, end, &str)) == nullptr) {

            }
        }
        kputsn("", 0, &str); // nul terminate
        char * si = str.s;
        for (int n = 0; n < (int)str.l; ++n) {
            oss << *si;
            si ++;
        }
        sam = oss.str();
        ks_free(&str);
    }

    void printRead(std::vector<Segs::Align>::iterator r, const sam_hdr_t* hdr, std::string &sam) {
        const char *rname = sam_hdr_tid2name(hdr, r->delegate->core.tid);
        const char *rnext = sam_hdr_tid2name(hdr, r->delegate->core.mtid);
        std::cout << std::endl << std::endl;
        std::cout << termcolor::bold << "qname    " << termcolor::reset << bam_get_qname(r->delegate) << std::endl;
        std::cout << termcolor::bold << "span     " << termcolor::reset << rname << ":" << r->pos << "-" << r->reference_end << std::endl;
        if (rnext) {
            std::cout << termcolor::bold << "mate     " << termcolor::reset << rnext << ":" << r->delegate->core.mpos << std::endl;
        }
        std::cout << termcolor::bold << "flag     " << termcolor::reset << r->delegate->core.flag << std::endl;
        std::cout << termcolor::bold << "mapq     " << termcolor::reset << (int)r->delegate->core.qual << std::endl;
        std::cout << termcolor::bold << "cigar    " << termcolor::reset; printCigar(r); std::cout << std::endl;
        std::cout << termcolor::bold << "seq      " << termcolor::reset; printSeq(r); std::cout << std::endl << std::endl;

        read2sam(r, hdr, sam);
    }

    void printSelectedSam(std::string &sam) {
        std::cout << std::endl << sam << std::endl << std::endl;
    }

    void parseMateLocation(std::string &selectedAlign, std::string &mate, std::string &target_qname) {
        if (selectedAlign.empty()) {
            return;
        }
        std::vector<std::string> s = Utils::split(selectedAlign, '\t');
        if (!s[6].empty() && s[6] != "*") {
            int p = std::stoi(s[7]);
            int b = (p - 500 > 0) ? p - 500 : 0;
            int e = p + 500;
            mate = (s[6] == "=") ? (s[2] + ":" + std::to_string(b) + "-" + std::to_string(e)) : (s[6] + ":" + ":" + std::to_string(b) + "-" + std::to_string(e));
            target_qname = s[0];
        }
    }

    void help(Themes::IniOptions &opts) {
        std::cout << termcolor::italic << "\n* Enter a command by selecting the GW window (not the terminal) and type ':[COMMAND]' *\n" << termcolor::reset;
        std::cout << termcolor::underline << "\nCommand          Modifier        Description                                            \n" << termcolor::reset;
        std::cout << termcolor::green << "add              region(s)       " << termcolor::reset << "Add one or more regions e.g. ':add chr1:1-20000'\n";
        std::cout << termcolor::green << "count            expression?     " << termcolor::reset << "Count reads. See filter for example expressions'\n";
        std::cout << termcolor::green << "cov              [of/off]        " << termcolor::reset << "Turn coverage on/off e.g. ':cov off'\n";
        std::cout << termcolor::green << "filter           expression      " << termcolor::reset << "Examples ':filter mapq > 0', ':filter ~flag & secondary'\n                                 ':filter mapq >= 30 or seq-len > 100'\n";
        std::cout << termcolor::green << "find, f          qname?          " << termcolor::reset << "To find other alignments from selected read use ':find'\n                                 Or use ':find [QNAME]' to find target read'\n";
        //std::cout << termcolor::green << "genome, g        name?           " << termcolor::reset << "Load genome listed in .gw.ini file. Use ':g' for list\n";
        std::cout << termcolor::green << "goto             loci index?     " << termcolor::reset << "e.g. ':goto chr1:1-20000'. Use index if multiple \n                                 regions are open e.g. ':goto 'chr1 20000' 1'\n";
        std::cout << termcolor::green << "link             [none/sv/all]   " << termcolor::reset << "Switch read-linking ':link all'\n";
        std::cout << termcolor::green << "log2-cov         [of/off]        " << termcolor::reset << "Scale coverage by log2 e.g. ':log2-cov on'\n";
        std::cout << termcolor::green << "mate                             " << termcolor::reset << "Use ':mate' to navigate to mate-pair\n";
        std::cout << termcolor::green << "quit, q          -               " << termcolor::reset << "Quit GW\n";
        std::cout << termcolor::green << "refresh, r       -               " << termcolor::reset << "Refresh and re-draw the window\n";
        std::cout << termcolor::green << "remove, rm       index           " << termcolor::reset << "Remove a region by index e.g. ':rm 1'. To remove a bam \n                                 use the bam index ':rm bam0'\n";
        std::cout << termcolor::green << "sam                              " << termcolor::reset << "Print selected read in sam format'\n";
        std::cout << termcolor::green << "theme            [igv/dark]      " << termcolor::reset << "Switch color theme e.g. ':theme dark'\n";
        std::cout << termcolor::green << "ylim             number          " << termcolor::reset << "The maximum y-limit for the image e.g. ':ylim 100'\n";

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
        constexpr char delim = ' ';
        constexpr char delim_q = '\'';

        commandHistory.push_back(inputText);
        commandIndex = commandHistory.size();

        if (inputText == ":q" || inputText == ":quit") {
            throw CloseException();
        } else if (inputText == ":") {
            inputText = "";
            return true;
        } else if (inputText == ":help" || inputText == ":h") {
            help(opts);
            valid = true;
        } else if (inputText == ":refresh" || inputText == ":r") {
            redraw = true; processed = false; valid = true; imageCache.clear(); filters.clear();
        } else if (inputText == ":link" || inputText == ":link all") {
            opts.link_op = 2; valid = true;
        } else if (inputText == ":link sv") {
            opts.link_op = 1; valid = true;
        } else if (inputText == ":link none") {
            opts.link_op = 0;
            valid = true;
        } else if (Utils::startsWith(inputText, ":count")) {
            std::string str = inputText;
            str.erase(0, 7);
            Parse::countExpression(collections, str, headers, bam_paths, bams.size(), regions.size());
            inputText = "";
            return true;

        } else if (Utils::startsWith(inputText, ":filter ")) {
            std::string str = inputText;
            str.erase(0, 8);
            filters.clear();
            for (auto &s: Utils::split(str, ';')) {
                Parse::Parser p = Parse::Parser();
                int rr = p.set_filter(s, bams.size(), regions.size());
                if (rr > 0) {
                    filters.push_back(p);
                } else {
                    inputText = "";
                    return false;
                }
            }
            valid = true;

        } else if (inputText == ":sam") {
            valid = true;
            if (!selectedAlign.empty()) {
                printSelectedSam(selectedAlign);
            }
            redraw = false;
            processed = true;
            inputText = "";
            return true;

        } else if (Utils::startsWith(inputText, ":f ") || Utils::startsWith(inputText, ":find")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            if (!target_qname.empty() && split.size() == 1) {
            } else if (split.size() == 2) {
                target_qname = split.back();
            } else {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " please provide one qname\n";
                inputText = "";
                return true;
            }
            redraw = true;
            processed = true;
            highlightQname();
            inputText = "";
            return true;

        } else if (Utils::startsWith(inputText, ":ylim")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            opts.ylim = std::stoi(split.back());
            samMaxY = opts.ylim;
            valid = true;
        } else if (Utils::startsWith(inputText, ":remove") || Utils::startsWith(inputText, ":rm")) {
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
                inputText = "";
                valid = true;
                collections.erase(std::remove_if(collections.begin(), collections.end(), [&ind](const auto col) {
                    return col.bamIdx == ind;
                }), collections.end());
                bams.erase(bams.begin() + ind);
                indexes.erase(indexes.begin() + ind);
            } else {
                try {
                    ind = std::stoi(split.back());
                } catch (...) {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " region index not understood\n";
                    inputText = "";
                    return true;
                }
                inputText = "";
                valid = true;
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
        } else if (Utils::startsWith(inputText, ":cov")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            if (split.back() == "on") {
                opts.coverage = true; valid = true;
            } else if (split.back() == "off") {
                opts.coverage = false; valid = true;
            } else {
                valid = false;
            }
        } else if (Utils::startsWith(inputText, ":log2-cov")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            if (split.back() == "on") {
                opts.log2_cov = true; valid = true;
            } else if (split.back() == "off") {
                opts.log2_cov = false; valid = true;
            } else {
                valid = false;
            }
        } else if (Utils::startsWith(inputText, ":mate")) {

            std::string mate;
            parseMateLocation(selectedAlign, mate, target_qname);
            if (mate.empty()) {
                std::cerr << "Error: could not parse mate location\n";
                inputText = "";
                return true;
            }
            if (inputText == ":mate") {
                if (regionSelection >= 0 && regionSelection < (int) regions.size()) {
                    regions[regionSelection] = Utils::parseRegion(mate);
                    processed = false;
//                    collections.clear();
                    for (auto &cl: collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.region = regions[regionSelection];
                            cl.readQueue.clear();
                            cl.covArr.clear();
                            cl.levelsStart.clear();
                            cl.levelsEnd.clear();
                        }
                    }
                    processBam();
                    processed = true;
                    highlightQname();
                    redraw = true;
                    processed = true;
                    inputText = "";
                    return true;
                }
            }

        } else if (Utils::startsWith(inputText, ":theme")) {
            std::vector<std::string> split = Utils::split(inputText, delim);
            if (split.back() == "dark") {
                opts.theme = Themes::DarkTheme();  opts.theme.setAlphas(); valid = true; imageCache.clear();
            } else if (split.back() == "igv") {
                opts.theme = Themes::IgvTheme(); opts.theme.setAlphas(); valid = true; imageCache.clear();
            } else {
                valid = false;
            }
        } else if (Utils::startsWith(inputText, ":goto")) {
            std::vector<std::string> split = Utils::split(inputText, delim_q);
            if (split.size() == 1) {
                split = Utils::split(inputText, delim);
            }
            if (split.size() > 1 && split.size() < 4) {
                int index = (split.size() == 3) ? std::stoi(split.back()) : 0;
                if (regions.empty()) {
                    regions.push_back(Utils::parseRegion(split[1]));
                    fetchRefSeq(regions.back());
                    valid = true;
                } else {
                    if (index < (int)regions.size()) {
                        regions[index] = Utils::parseRegion(split[1]);
                        valid = true;
                    } else {
                        std::cerr << termcolor::red << "Error:" << termcolor::reset << " region index is out of range. Use 0-based indexing\n";
                        inputText = "";
                        return true;
                    }
                }
            }
        } else if (Utils::startsWith(inputText, ":add"))  {
            std::vector<std::string> split = Utils::split(inputText, delim_q);
            if (split.size() == 1) {
                split = Utils::split(inputText, delim);
            }
            if (split.size() > 1) {
                for (int i=1; i < (int)split.size(); ++i) {
                    regions.push_back(Utils::parseRegion(split[1]));
                    fetchRefSeq(regions.back());
                }
                valid = true;
            } else {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " expected a Region e.g. chr1:1-20000\n";
                inputText = "";
                return true;
            }
        }
        if (valid) {
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
                a = removeZeros((float)d);
                b = " mb";
            } else {
                d = (double)num / 1e3;
                d = std::ceil(d * 10) / 10;
                a = removeZeros((float)d);
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
        clearLine();
        std::cout << termcolor::bold << "\rShowing   " << termcolor::reset ;
        int i = 0;
        auto r = regions[regionSelection];
        std::cout << termcolor::cyan << r.chrom << ":" << r.start << "-" << r.end << termcolor::white << "  (" << getSize(r.end - r.start) << ")";
        if (i != (int)regions.size() - 1) {
            std::cout << "    ";
        }
        std::cout << termcolor::reset << std::flush;
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
            if (regions.empty() || regionSelection < 0) {  // bams.empty() ||
                return;
            }

            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                if (key == opts.scroll_right) {
                    int shift = (int)(((float)regions[regionSelection].end - (float)regions[regionSelection].start) * opts.scroll_speed);
                    delete regions[regionSelection].refSeq;
                    Utils::Region N;
                    N.chrom = regions[regionSelection].chrom;
                    N.start = regions[regionSelection].start + shift;
                    N.end = regions[regionSelection].end + shift;
                    N.markerPos = regions[regionSelection].markerPos;
                    N.markerPosEnd = regions[regionSelection].markerPosEnd;
                    fetchRefSeq(N);
                    regions[regionSelection] = N;
                    if (opts.link_op != 0) {
                        processed = false;
                        redraw = true;
                    } else {
                        processed = true;
                        for (auto &cl : collections) {
                            if (cl.regionIdx == regionSelection) {
                                cl.region = N; //regions[regionSelection];
                                if (!bams.empty()) {
                                    HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx],
                                                                indexes[cl.bamIdx], opts, opts.coverage, false, linked,
                                                                &samMaxY, filters);
                                }
                            }
                        }
                        redraw = true;
                    }
                    printRegionInfo();

                } else if (key == opts.scroll_left) {
                    int shift = (int)(((float)regions[regionSelection].end - (float)regions[regionSelection].start) * opts.scroll_speed);
                    shift = (regions[regionSelection].start - shift > 0) ? shift : 0;
                    delete regions[regionSelection].refSeq;
                    Utils::Region N;
                    N.chrom = regions[regionSelection].chrom;
                    N.start = regions[regionSelection].start - shift;
                    N.end = regions[regionSelection].end - shift;
                    N.markerPos = regions[regionSelection].markerPos;
                    N.markerPosEnd = regions[regionSelection].markerPosEnd;
                    fetchRefSeq(N);
                    regions[regionSelection] = N;
                    if (opts.link_op != 0) {
                        processed = false;
                        redraw = true;
                    } else {
                        processed = true;
                        for (auto &cl : collections) {
                            if (cl.regionIdx == regionSelection) {
                                cl.region = regions[regionSelection];
                                if (!bams.empty()) {
                                    HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx],
                                                                indexes[cl.bamIdx], opts, opts.coverage, true, linked,
                                                                &samMaxY, filters);
                                }
                            }
                        }
                        redraw = true;
                    }
                    printRegionInfo();
                } else if (key == opts.zoom_out) {
                    int shift = (int)((((float)regions[regionSelection].end - (float)regions[regionSelection].start) * opts.scroll_speed)) + 10;
                    int shift_left = (regions[regionSelection].start - shift > 0) ? shift : 0;
                    delete regions[regionSelection].refSeq;
                    Utils::Region N;
                    N.chrom = regions[regionSelection].chrom;
                    N.start = regions[regionSelection].start - shift_left;
                    N.end = regions[regionSelection].end + shift;
                    N.markerPos = regions[regionSelection].markerPos;
                    N.markerPosEnd = regions[regionSelection].markerPosEnd;
                    fetchRefSeq(N);
                    regions[regionSelection] = N;
                    if (opts.link_op != 0) {
                        processed = false;
                        redraw = true;
                    } else {
                        processed = true;
                        for (auto &cl : collections) {
                            if (cl.regionIdx == regionSelection) {
                                cl.region = regions[regionSelection];
                                if (!bams.empty()) {
                                    HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx], indexes[cl.bamIdx],
                                                                opts, false, true, linked, &samMaxY, filters);
                                    HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx], indexes[cl.bamIdx],
                                                                opts, false, false, linked, &samMaxY, filters);
                                    if (opts.coverage) {  // re process coverage for all reads
                                        cl.covArr.resize(cl.region.end - cl.region.start + 1);
                                        std::fill(cl.covArr.begin(), cl.covArr.end(), 0);
                                        int l_arr = (int) cl.covArr.size() - 1;
                                        for (auto &i: cl.readQueue) {
                                            Segs::addToCovArray(cl.covArr, i, cl.region.start, cl.region.end, l_arr);
                                        }
                                    }
                                }
                            }
                        }
                        redraw = true;
                    }
                    printRegionInfo();
                } else if (key == opts.zoom_in) {
                    if (regions[regionSelection].end - regions[regionSelection].start > 50) {
                        int shift = (int)(((float)regions[regionSelection].end - (float)regions[regionSelection].start) * opts.scroll_speed);
                        int shift_left = (regions[regionSelection].start - shift > 0) ? shift : 0;
                        delete regions[regionSelection].refSeq;
                        Utils::Region N;
                        N.chrom = regions[regionSelection].chrom;
                        N.start = regions[regionSelection].start + shift_left;
                        N.end = regions[regionSelection].end - shift;
                        N.markerPos = regions[regionSelection].markerPos;
                        N.markerPosEnd = regions[regionSelection].markerPosEnd;
                        fetchRefSeq(N);
                        regions[regionSelection] = N;
                        if (opts.link_op != 0) {
                            processed = false;
                            redraw = true;
                        } else {
                            processed = true;
                            for (auto &cl : collections) {
                                if (cl.regionIdx == regionSelection) {
                                    cl.region = regions[regionSelection];
                                    if (!bams.empty()) {
                                        HGW::trimToRegion(cl, opts.coverage);
                                    }
                                }
                            }
                            redraw = true;
                        }
                        printRegionInfo();
                    }
                } else if (key == opts.next_region_view) {
                    regionSelection += 1;
                    if (regionSelection >= (int)regions.size()) {
                        regionSelection = 0;
                    }
                    std::cout << "\nRegion    " << regionSelection << std::endl;
                } else if (key == opts.scroll_down) {
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.vScroll += 2;
                        }
                    }
                    redraw = true;
                    processed = false;
                } else if (key == opts.scroll_up) {
                    for (auto &cl : collections) {
                        if (cl.regionIdx == regionSelection) {
                            cl.vScroll = (cl.vScroll - 2 < 0) ? 0 : cl.vScroll - 2;
                        }
                    }
                    redraw = true;
                    processed = false;
                }
            }
        } else {  // show::TILED
            int bLen = opts.number.x * opts.number.y;
            if (action == GLFW_PRESS || action == GLFW_REPEAT) {
                if (key == opts.scroll_right) {
                    size_t targetSize = (image_glob.empty()) ? multiRegions.size() : image_glob.size();
                    if (blockStart + bLen <= (int)targetSize) {
                        blockStart += bLen;
                        redraw = true;
                        clearLine();
                        std::cout << termcolor::green << "\rIndex     " << termcolor::reset << blockStart << std::endl;
                    }
                } else if (key == opts.scroll_left) {
                    if (blockStart == 0) {
                        return;
                    }
                    blockStart = (blockStart - bLen > 0) ? blockStart - bLen : 0;
                    redraw = true;
                    clearLine();
                    std::cout << termcolor::green << "\rIndex     " << termcolor::reset << blockStart << std::endl;
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
        if (key == opts.cycle_link_mode) {
            opts.link_op = (opts.link_op == 2) ? 0 : opts.link_op += 1;
            std::string lk = (opts.link_op > 0) ? ((opts.link_op == 1) ? "sv" : "all") : "none";
            std::cout << "\nLinking selection " << lk << std::endl;
            imageCache.clear();
            processed = false;
            redraw = true;
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
                if (!image_glob.empty()) {
                    std::cerr << "Error: --images are already open, can not open variant file\n";
                    return;
                }
                good = true;
                std::cout << "Loading: " << pth << std::endl;
                setVariantFile(pth, opts.start_index, false);
                imageCache.clear();
                blockStart = 0;
                mode = Manager::Show::TILED;
                std::cout << termcolor::green << "Index     " << termcolor::reset << blockStart << std::endl;
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
            return -2;
        }
        if (regions.empty()) {
            return -1;
        }
        int i = 0;
        if (bams.empty()) {
            i = (int)(x / ((float)fb_width / (float)regions.size()));
            i = (i > (int)regions.size()) ? (int)regions.size() : i;
            return i;
        } else if (bams.size() <= 1) {
            for (auto &cl: collections) {
                float min_x = cl.xOffset;
                float max_x = cl.xScaling * ((float) (cl.region.end - cl.region.start)) + min_x;
                float min_y = refSpace;
                float max_y = fb_height - refSpace;
                if (x > min_x && x < max_x && y > min_y && y < max_y) {
                    return i;
                }
                i += 1;
            }
        } else {
            for (auto &cl: collections) {
                float min_x = cl.xOffset;
                float max_x = cl.xScaling * ((float) (cl.region.end - cl.region.start)) + min_x;
                float min_y = cl.yOffset;
                float max_y = min_y + trackY;
                if (x > min_x && x < max_x && y > min_y && y < max_y)
                    return i;
                i += 1;
            }
        }
        return -1;
    }

    void printRefSeq(float x, std::vector<Segs::ReadCollection> &collections) {
        for (auto &cl: collections) {
            float min_x = cl.xOffset;
            float max_x = cl.xScaling * ((float)(cl.region.end - cl.region.start)) + min_x;
            int size = cl.region.end - cl.region.start;
            if (x > min_x && x < max_x && size <= 20000) {
                const char * s = cl.region.refSeq;
                std::cout << "\n\n" << cl.region.chrom << ":" << cl.region.start << "-" << cl.region.end << "\n";
                while (*s) {
                    switch ((unsigned int)*s) {
                        case 65: std::cout << termcolor::green << "a"; break;
                        case 67: std::cout << termcolor::blue << "c"; break;
                        case 71: std::cout << termcolor::yellow << "g"; break;
                        case 78: std::cout << termcolor::bright_grey << "n"; break;
                        case 84: std::cout << termcolor::red << "t"; break;
                        case 97: std::cout << termcolor::green << "A"; break;
                        case 99: std::cout << termcolor::blue << "C"; break;
                        case 103: std::cout << termcolor::yellow << "G"; break;
                        case 110: std::cout << termcolor::bright_grey << "N"; break;
                        case 116: std::cout << termcolor::red << "T"; break;
                        default: std::cout << "?"; break;
                    }
                    ++s;
                }
                std::cout << termcolor::reset << std::endl << std::endl;
                return;
            }
        }
    }

    void GwPlot::mouseButton(GLFWwindow* wind, int button, int action, int mods) {
        double x, y;
//        if (regions.empty()) {
//            return;
//        }
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

        if (xDrag == -1000000) {
            xDrag = 0;
            xOri = x;
        }
        xDrag = x - xOri;

        if (mode == Manager::SINGLE && button == GLFW_MOUSE_BUTTON_LEFT) {
            if (regions.empty()) {
                return;
            }
            int idx = getCollectionIdx(xW, yW);
            if (idx == -2 && action == GLFW_RELEASE) {
                printRefSeq(xW, collections);
            }
            if (idx < 0) {
                return;
            }

            Segs::ReadCollection &cl = collections[idx];
            regionSelection = cl.regionIdx;
            if (action == GLFW_PRESS) {
                clicked = cl.region;
                clickedIdx = idx;
            }
            if (std::abs(xDrag) < 2 && action == GLFW_RELEASE && !bams.empty()) {
                int pos = (int) (((xW - (float) cl.xOffset) / cl.xScaling) +
                                 (float) cl.region.start);
                int level = (int) ((yW - (float) cl.yOffset) /
                                   (trackY / (float) cl.levelsStart.size()));
                std::vector<Segs::Align>::iterator bnd;
                bnd = std::lower_bound(cl.readQueue.begin(), cl.readQueue.end(), pos,
                                       [&](const Segs::Align &lhs, const int pos) { return (int)lhs.pos < pos; });
                while (true) {
                    if (bnd->y == level && (int)bnd->pos <= pos && pos < (int)bnd->reference_end) {
                        bnd->edge_type = 4;
                        target_qname = bam_get_qname(bnd->delegate);
                        printRead(bnd, headers[cl.bamIdx], selectedAlign);
                        redraw = true;
                        processed = true;
                        break;
                    }
                    if (bnd == cl.readQueue.begin()) {
                        break;
                    }
                    --bnd;
                }
                xDrag = -1000000;
                clickedIdx = -1;

            } else if (action == GLFW_RELEASE) {
                auto w = (float) (((float)cl.region.end - (float)cl.region.start) * (float) regions.size());
                if (w >= 50000) {
                    int travel = (int) (w * (xDrag / windowW));

                    Utils::Region N;

                    if (cl.region.start - travel < 0) {
                        travel = cl.region.start;
                        N.chrom = cl.region.chrom;
                        N.start = 0;
                        N.end = clicked.end - travel;
                    } else {
                        N.chrom = cl.region.chrom;
                        N.start = clicked.start - travel;
                        N.end = clicked.end - travel;
                    }

                    regionSelection = cl.regionIdx;
                    delete regions[regionSelection].refSeq;

                    N.markerPos = regions[regionSelection].markerPos;
                    N.markerPosEnd = regions[regionSelection].markerPosEnd;
                    fetchRefSeq(N);

                    bool lt_last = N.start < cl.region.start;
                    regions[regionSelection] = N;
                    if (opts.link_op != 0) {
                        processed = false;
                        redraw = true;
                    } else {
                        processed = true;
                        for (auto &col : collections) {
                            if (col.regionIdx == regionSelection) {
                                col.region = regions[regionSelection];
                                HGW::appendReadsAndCoverage(col,  bams[col.bamIdx], headers[col.bamIdx], indexes[col.bamIdx], opts, opts.coverage, lt_last, linked, &samMaxY, filters);
                            }
                        }
                        redraw = true;
                    }
                }
                clickedIdx = -1;
            }
            xOri = x;

        } else if (mode == Manager::SINGLE && button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_RELEASE) {
            if (regions.empty()) {
                return;
            }
            if (!multiRegions.empty() || !imageCache.empty()) {
                mode = Manager::TILED;
                xDrag = -1000000;
                redraw = true;
                processed = false;
            }
        } else if (mode == Manager::TILED) {
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
                    return;
                }
                if (!bams.empty()) {
                    if (i < (int)multiRegions.size() && !bams.empty()) {
                        if (blockStart + i < (int)multiRegions.size()) {
                            if (multiRegions[blockStart + i][0].chrom.empty()) {
                                return; // check for "" no chrom set
                            } else {
                                mode = Manager::SINGLE;
                                regions = multiRegions[blockStart + i];
                                redraw = true;
                                processed = false;
                                fetchRefSeqs();
                            }
                        }
                    }
                }
            } else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE) {
                if (std::fabs(xDrag) > fb_width / 8.) {
                    int nmb = opts.number.x * opts.number.y;
                    if (xDrag > 0) {
                        blockStart = (blockStart - nmb < 0) ? 0 : blockStart - nmb;
                        redraw = true;
                        std::cout << "\r                                                                               ";
                        std::cout << termcolor::green << "\rIndex     " << termcolor::reset << blockStart << std::flush;
                    } else {
                        blockStart += nmb;
                        redraw = true;
                        std::cout << "\r                                                                               ";
                        std::cout << termcolor::green << "\rIndex     " << termcolor::reset << blockStart << std::flush;
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
                        return;
                    }
                    if (blockStart + i < (int)multiLabels.size()) {
                        multiLabels[blockStart + i].next();
                        multiLabels[blockStart + i].clicked = true;
                        multiLabels[blockStart + i].savedDate = Utils::dateTime();
                        redraw = true;
                    }
                }
                xDrag = -1000000;
            }
        }
    }

    void GwPlot::updateCursorGenomePos(Segs::ReadCollection &cl, float xPos) {
        int pos = (int) (((xPos - (float) cl.xOffset) / cl.xScaling) +
                         (float) cl.region.start);
        auto s = std::to_string(pos);
        int n = (int)s.length() - 3;
        int end = (pos >= 0) ? 0 : 1; // Support for negative numbers
        while (n > end) {
            s.insert(n, ",");
            n -= 3;
        }
        printRegionInfo();
        std::cout << "    " << s << std::flush;
    }

    void updateRefGenomeSeq(float xW, std::vector<Segs::ReadCollection> &collections) {
        for (auto &cl: collections) {
            float min_x = cl.xOffset;
            float max_x = cl.xScaling * ((float)(cl.region.end - cl.region.start)) + min_x;
            if (xW > min_x && xW < max_x) {
                int pos = (int) (((xW - (float) cl.xOffset) / cl.xScaling) + (float) cl.region.start);
                int chars =  Utils::get_terminal_width() - 11;
                if (chars <= 0) {
                    return;
                }
                int i = chars / 2; //30;
                int startIdx = pos - cl.region.start - i;
                if (startIdx < 0) {
                    i += startIdx;
                    startIdx = 0;
                }
                if (startIdx > cl.region.end - cl.region.start) {
                    return;  // something went wrong
                }
                if (cl.region.refSeq == nullptr) {
                    return;
                }
                const char * s = &cl.region.refSeq[startIdx];
                clearLine();
                std::cout << termcolor::bold << "\rRef       " << termcolor::reset ;
                int l = 0;
                while (*s && l < chars) {
                    switch ((unsigned int)*s) { // this is a bit of mess, but gets the job done
                        case 65: ((l == i) ? (std::cout << termcolor::underline << termcolor::green << "a" << termcolor::reset) : (std::cout << termcolor::green << "a") ); break;
                        case 67: ((l == i) ? (std::cout << termcolor::underline << termcolor::blue << "c" << termcolor::reset) : (std::cout << termcolor::blue << "c") ); break;
                        case 71: ((l == i) ? (std::cout << termcolor::underline << termcolor::yellow << "g" << termcolor::reset) : (std::cout << termcolor::yellow << "g") ); break;
                        case 78: ((l == i) ? (std::cout << termcolor::underline << termcolor::bright_grey << "n" << termcolor::reset) : (std::cout << termcolor::bright_grey << "n") ); break;
                        case 84: ((l == i) ? (std::cout << termcolor::underline << termcolor::red << "t" << termcolor::reset) : (std::cout << termcolor::red << "t") ); break;
                        case 97: ((l == i) ? (std::cout << termcolor::underline << termcolor::green << "A" << termcolor::reset) : (std::cout << termcolor::green << "A") ); break;
                        case 99: ((l == i) ? (std::cout << termcolor::underline << termcolor::blue << "C" << termcolor::reset) : (std::cout << termcolor::blue << "C") ); break;
                        case 103: ((l == i) ? (std::cout << termcolor::underline << termcolor::yellow << "G" << termcolor::reset) : (std::cout << termcolor::yellow << "G") ); break;
                        case 110: ((l == i) ? (std::cout << termcolor::underline << termcolor::bright_grey << "N" << termcolor::reset) : (std::cout << termcolor::bright_grey << "N") ); break;
                        case 116: ((l == i) ? (std::cout << termcolor::underline << termcolor::red << "T" << termcolor::reset) : (std::cout << termcolor::red << "T") ); break;
                        default: std::cout << "?"; break;
                    }
                    ++s;
                    l += 1;
                }
                std::cout << termcolor::reset << std::flush;
                return;
            }
        }
    }

    void GwPlot::mousePos(GLFWwindow* wind, double xPos, double yPos) {

        if (lastX == -1) {
            lastX = xPos;
        }
        int state = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT);
        bool lt_last = xPos < lastX;
        lastX = xPos;
        if (state == GLFW_PRESS) {
            xDrag = xPos - xOri;
            if (mode == Manager::SINGLE) {

                if (regions.empty()) {
                    return;
                }

                int windowW, windowH;  // convert screen coords to frame buffer coords
                glfwGetWindowSize(wind, &windowW, &windowH);
                if (fb_width > windowW) {
                    xPos *= (float) fb_width / (float) windowW;
                    yPos *= (float) fb_height / (float) windowH;
                }
                int idx = getCollectionIdx((float)xPos, (float)yPos);
                if (idx < 0) {
                    return;
                }
                Segs::ReadCollection &cl = collections[idx];
                regionSelection = cl.regionIdx;
                if (clickedIdx == -1 || idx != clickedIdx) {
                    return;
                }

                if (cl.region.end - cl.region.start < 50000) {

                    printRegionInfo();

                    auto w = (float) (((float)cl.region.end - (float)cl.region.start) * (float) regions.size());
                    int travel = (int) (w * (xDrag / windowW));

                    Utils::Region N;

                    if (cl.region.start - travel < 0) {
                        travel = cl.region.start;
                        N.chrom = cl.region.chrom;
                        N.start = 0;
                        N.end = clicked.end - travel;
                    } else {
                        N.chrom = cl.region.chrom;
                        N.start = clicked.start - travel;
                        N.end = clicked.end - travel;
                    }

                    regionSelection = cl.regionIdx;
                    delete regions[regionSelection].refSeq;

                    N.markerPos = regions[regionSelection].markerPos;
                    N.markerPosEnd = regions[regionSelection].markerPosEnd;
                    fetchRefSeq(N);

                    regions[regionSelection] = N;

                    if (opts.link_op != 0) {
                        processed = false;
                        redraw = true;
                    } else {
                        processed = true;
                        for (auto &col : collections) {
                            if (col.regionIdx == regionSelection) {
                                col.region = regions[regionSelection];
                                if (!bams.empty()) {
                                    HGW::appendReadsAndCoverage(col, bams[col.bamIdx], headers[col.bamIdx],
                                                                indexes[col.bamIdx], opts, opts.coverage, !lt_last,
                                                                linked, &samMaxY, filters);
                                }
                            }
                        }
                        redraw = true;
                    }
                }
            }
        } else {
            if (mode == Manager::SINGLE) {
                if (regions.empty()) {
                    return;
                }
                int windowW, windowH;  // convert screen coords to frame buffer coords
                glfwGetWindowSize(wind, &windowW, &windowH);
                if (fb_width > windowW) {
                    xPos *= (float) fb_width / (float) windowW;
                    yPos *= (float) fb_height / (float) windowH;
                }

                int rs = getCollectionIdx((float)xPos, (float)yPos);
                if (rs < 0) {
                    if (rs == -2) {
                        updateRefGenomeSeq((float)xPos, collections);
                    }
                    return;
                }
                Segs::ReadCollection &cl = collections[rs];
                regionSelection = cl.regionIdx;

                updateCursorGenomePos(cl, (float)xPos);
            } else {
                int windowW, windowH;  // convert screen coords to frame buffer coords
                glfwGetWindowSize(wind, &windowW, &windowH);
                if (fb_width > windowW) {
                    xPos *= (float) fb_width / (float) windowW;
                    yPos *= (float) fb_height / (float) windowH;
                }

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
                if (blockStart + i < (int)multiRegions.size()) {
                    Utils::Label &lbl = multiLabels[blockStart + i];
                    lbl.mouseOver = true;
                    if (i != mouseOverTileIndex) {
                        redraw = true;
                        mouseOverTileIndex = i;
                    }
                    clearLine();
                    std::cout << termcolor::bold << "\rPosition  " << termcolor::reset << lbl.chrom << ":" << lbl.pos << termcolor::bold <<
                              "    ID  "  << termcolor::reset << lbl.variantId << termcolor::bold <<
                              "    Type  "  << termcolor::reset << lbl.vartype;
                    std::cout << std::flush;
                }
            }
        }
    }

    void GwPlot::scrollGesture(GLFWwindow* wind, double xoffset, double yoffset) {
        if (mode == Manager::SINGLE) {
            if (yoffset < 0) {
                keyPress(wind, opts.zoom_out, 0, GLFW_PRESS, 0);
            } else {
                keyPress(wind, opts.zoom_in, 0, GLFW_PRESS, 0);
            }
        } else {
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
