//
// Created by Kez Cleal on 07/12/2022.
//
#include <filesystem>
#include <htslib/sam.h>
#include <string>
#include <thread>
#include <vector>
#include "htslib/hts.h"
#include "drawing.h"
#include "hts_funcs.h"
#include "plot_manager.h"
#include "segments.h"
#include "../include/termcolor.h"
#include "term_out.h"
#include "themes.h"


namespace Term {

    void help(Themes::IniOptions &opts) {
        std::cout << termcolor::italic << "\n* Enter a command by selecting the GW window (not the terminal) and type ':[COMMAND]' *\n" << termcolor::reset;
        std::cout << termcolor::italic << "\n* For a detailed manual type ':man [COMMAND]' *\n" << termcolor::reset;
        std::cout << termcolor::underline << "\nCommand          Modifier        Description                                            \n" << termcolor::reset;
        std::cout << termcolor::green << "[locus]                          " << termcolor::reset << "e.g. 'chr1' or ':chr1:1-20000'\n";
        std::cout << termcolor::green << "add              region(s)       " << termcolor::reset << "Add one or more regions e.g. ':add chr1:1-20000'\n";
        std::cout << termcolor::green << "config                           " << termcolor::reset << "Opens .gw.ini config in a text editor\n";
        std::cout << termcolor::green << "count            expression?     " << termcolor::reset << "Count reads. See filter for example expressions'\n";
        std::cout << termcolor::green << "cov                              " << termcolor::reset << "Toggle coverage\n";
        std::cout << termcolor::green << "filter           expression      " << termcolor::reset << "Examples ':filter mapq > 0', ':filter ~flag & secondary'\n                                 ':filter mapq >= 30 or seq-len > 100'\n";
        std::cout << termcolor::green << "find, f          qname?          " << termcolor::reset << "To find other alignments from selected read use ':find'\n                                 Or use ':find [QNAME]' to find target read'\n";
        //std::cout << termcolor::green << "genome, g        name?           " << termcolor::reset << "Load genome listed in .gw.ini file. Use ':g' for list\n";
        std::cout << termcolor::green << "goto             loci index?     " << termcolor::reset << "e.g. ':goto chr1:1-20000'. Use index if multiple \n                                 regions are open e.g. ':goto 'chr1 20000' 1'\n";
        std::cout << termcolor::green << "line                             " << termcolor::reset << "Toggle mouse position vertical line\n";
        std::cout << termcolor::green << "link             [none/sv/all]   " << termcolor::reset << "Switch read-linking ':link all'\n";
        std::cout << termcolor::green << "log2-cov                         " << termcolor::reset << "Toggle scale coverage by log2\n";
        std::cout << termcolor::green << "mate             add?            " << termcolor::reset << "Use ':mate' to navigate to mate-pair, or ':mate add' \n                                 to add a new region with mate \n";
        std::cout << termcolor::green << "quit, q          -               " << termcolor::reset << "Quit GW\n";
        std::cout << termcolor::green << "refresh, r       -               " << termcolor::reset << "Refresh and re-draw the window\n";
        std::cout << termcolor::green << "remove, rm       index           " << termcolor::reset << "Remove a region by index e.g. ':rm 1'. To remove a bam \n                                 use the bam index ':rm bam0'\n";
        std::cout << termcolor::green << "sam                              " << termcolor::reset << "Print selected read in sam format'\n";
        std::cout << termcolor::green << "theme            [igv/dark]      " << termcolor::reset << "Switch color theme e.g. ':theme dark'\n";
        std::cout << termcolor::green << "ylim             number          " << termcolor::reset << "The maximum y-limit for the image e.g. ':ylim 100'\n";

        std::cout << termcolor::underline << "\nHot keys                   \n" << termcolor::reset;
        std::cout << "scroll left       " << termcolor::bright_yellow; Term::printKeyFromValue(opts.scroll_left); std::cout << "\n" << termcolor::reset;
        std::cout << "scroll right      " << termcolor::bright_yellow; Term::printKeyFromValue(opts.scroll_right); std::cout << "\n" << termcolor::reset;
        std::cout << "scroll down       " << termcolor::bright_yellow; Term::printKeyFromValue(opts.scroll_down); std::cout << "\n" << termcolor::reset;
        std::cout << "scroll up         " << termcolor::bright_yellow; Term::printKeyFromValue(opts.scroll_up); std::cout << "\n" << termcolor::reset;
        std::cout << "zoom in           " << termcolor::bright_yellow; Term::printKeyFromValue(opts.zoom_in); std::cout << "\n" << termcolor::reset;
        std::cout << "zoom out          " << termcolor::bright_yellow; Term::printKeyFromValue(opts.zoom_out); std::cout << "\n" << termcolor::reset;
        std::cout << "next region view  " << termcolor::bright_yellow; Term::printKeyFromValue(opts.next_region_view); std::cout << "\n" << termcolor::reset;
        std::cout << "cycle link mode   " << termcolor::bright_yellow; Term::printKeyFromValue(opts.cycle_link_mode); std::cout << "\n" << termcolor::reset;
        std::cout << "\n";
    }

    void manuals(std::string &s) {
        std::cout << "\nManual for command '" << s << "'\n";
        std::cout << "--------------------"; for (auto &_ : s) std::cout << "-"; std::cout << "-\n\n";
        if (s == "[locus]" || s == "locus") {
            std::cout << "    Navigate to a genomic locus.\n        You can use chromosome names or chromosome coordinates.\n    Examples:\n        'chr1:1-20000', 'chr1', 'chr1:10000'\n\n";
        } else if (s == "add") {
            std::cout << "    Add a genomic locus.\n        This will add a new locus to the right-hand-side of your view.\n    Examples:\n        'add chr1:1-20000', 'add chr2'\n\n";
        } else if (s == "config") {
            std::cout << "    Open the GW config file.\n        The config file will be opened in a text editor, for Mac TextEdit will be used, linux will be vi, and windows will be notepad.\n\n";
        } else if (s == "count") {
            std::cout << "    Count the visible reads in each view.\n        A summary output will be displayed for each view on the screen.\n        Optionally a filter expression may be added to the command. See the man page for 'filter' for mote details\n    Examples:\n        'count', 'count flag & 2', 'count flag & proper-pair' \n\n";
        } else if (s == "cov") {
            std::cout << "    Toggle coverage track.\n        This will turn on/off coverage tracks.\n\n";
        } else if (s == "filter") {
            std::cout << "    Filter visible reads.\n"
                         "        Reads can be filtered using an expression '{property} {operation} {value}' (the white-spaces are also needed).\n"
                         "        For example, here are some useful expressions:\n"
                         "            :filter mapq >= 20\n"
                         "            :filter flag & 2048\n"
                         "            :filter seq contains TTAGGG\n\n"
                         "        Here are a list of '{property}' values you can use:\n"
                         "             maps, flag, ~flag, name, tlen, abs-tlen, rnext, pos, ref-end, pnext, seq, seq-len,\n"
                         "             RG, BC, BX, RX, LB, MD, MI, PU, SA, MC, NM, CM, FI, HO, MQ, SM, TC, UQ, AS\n\n"
                         "        These can be combined with '{operator}' values:\n"
                         "             &, ==, !=, >, <, >=, <=, eq, ne, gt, lt, ge, le, contains, omit\n\n"
                         "        Bitwise flags can also be applied with named values:\n"
                         "             paired, proper-pair, unmapped, munmap, reverse, mreverse, read1, read2, secondary, dup, supplementary\n\n"
                         "        Expressions can be chained together providing all expressions are 'AND' or 'OR' blocks:\n"
                         "             :filter mapq >= 20 and mapq < 30\n"
                         "             :filter mapq >= 20 or flag & supplementary\n\n"
                         "        Finally, you can apply filters to specific panels using array indexing notation:\n"
                         "              :filter mapq > 0 [:, 0]   # All rows, column 0 (all bams, first region only)\n"
                         "              :filter mapq > 0 [0, :]   # Row 0, all columns (the first bam only, all regions)\n"
                         "              :filter mapq > 0 [1, -1]  # Row 1, last column\n\n";
        } else if (s == "find") {
            std::cout << "    Find a read with name.\n        All alignments with the same name will be highlighted with a black border\n    Examples:\n        'find D00360:18:H8VC6ADXX:1:1107:5538:24033'\n\n";
        } else if (s == "goto") {
            std::cout << "    Navigate to a locus.\n        This moves the left-most view. Or, you can use indexing to specify a region\n    Examples:\n        'goto chr1'   # this will move the left-most view\n        'goto chr1:20000 1'   # this will move the view at column index 1\n\n";
        } else if (s == "line") {
            std::cout << "    Toggle line.\n        A vertical line will turn on/off.\n\n";
        } else if (s == "link") {
            std::cout << "    Link alignments.\n        This will change how alignments are linked, options are 'none', 'sv', 'all'.\n    Examples:\n        'link sv', 'link all'\n\n";
        } else if (s == "log2-cov") {
            std::cout << "    Toggle log2-coverage.\n        The coverage track will be scaled by log2.\n\n";
        } else if (s == "mate") {
            std::cout << "    Goto mate alignment.\n        Either moves the left-most view to the mate locus, or adds a new view of the mate locus.\n    Examples:\n        'mate', 'mate add'\n\n";
        } else if (s == "refresh") {
            std::cout << "    Refresh the drawing.\n        All filters will be removed any everything will be redrawn.\n\n";
        } else if (s == "remove") {
            std::cout << "    Remove a region or bam.\n        Remove a region or bam by index. To remove a bam add a 'bam' prefix.\n    Examples:\n        'rm 0', 'rm bam1'\n\n";
        } else if (s == "sam") {
            std::cout << "    Print the sam format of the read.\n        First select a read using the mouse then type ':sam'.\n\n";
        } else if (s == "theme") {
            std::cout << "    Switch the theme.\n        Currently 'igv' or 'dark' themes are supported.\n\n";
        } else if (s == "ylim") {
            std::cout << "    Set the y limit.\n        The y limit is the maximum depth shown on the drawing e.g. 'ylim 100'.\n\n";
        }
    }

    constexpr char basemap[] = {'.', 'A', 'C', '.', 'G', '.', '.', '.', 'T', '.', '.', '.', '.', '.', 'N', 'N', 'N'};


    void clearLine() {
        std::string s(Utils::get_terminal_width(), ' ');
        std::cout << "\r" << s << std::flush;
    }

    void editInputText(std::string &inputText, const char *letter, int &charIndex) {
        if (charIndex != (int)inputText.size()) {
            inputText.insert(charIndex, letter);
            charIndex += 1;
        } else {
            inputText.append(letter);
            charIndex = inputText.size();
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

    void printSeq(std::vector<Segs::Align>::iterator r, const char *refSeq, int refStart, int refEnd, int max=1000) {
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
        int p = r->pos;
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
                p += l;
            } else if (op == BAM_CMATCH) {
                for (int n = 0; n < (int)l; ++n) {
                    uint8_t base = bam_seqi(ptr_seq, i);
                    bool mm = false;
                    if (p >= refStart && p < refEnd && refSeq != nullptr && std::toupper(refSeq[p - refStart]) != basemap[base]) {
                        mm = true;
                    }
                    if (mm) {
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
                    } else {
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
                    p += 1;
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
                p += l;

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
                p += l;

            } else {
                for (int n=0; n < (int)l; ++n) {  // soft-clips
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

    void printRead(std::vector<Segs::Align>::iterator r, const sam_hdr_t* hdr, std::string &sam, const char *refSeq, int refStart, int refEnd) {
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
        std::cout << termcolor::bold << "seq      " << termcolor::reset; printSeq(r, refSeq, refStart, refEnd); std::cout << std::endl << std::endl;

        read2sam(r, hdr, sam);
    }

    void printSelectedSam(std::string &sam) {
        std::cout << std::endl << sam << std::endl << std::endl;
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

	void printTrack(float x, HGW::GwTrack &track, const Utils::Region *rgn, bool mouseOver) {
		track.fetch(rgn);
		int target = (int)((float)(rgn->end - rgn->start) * x) + rgn->start;
		std::filesystem::path p = track.path;
		bool printed = false;
		while (true) {
			track.next();
			if (track.done) {
				break;
			}
			if (track.start <= target && track.stop > target) {
				std::cout << "\r" << termcolor::bold << p.filename().string() << termcolor::reset << "    " << \
                    termcolor::cyan << track.chrom << ":" << track.start << "-" << track.stop << termcolor::reset << \
                    "    " << track.rid;
				printed = true;
				if (!mouseOver) {
					std::cout << std::endl;
				} else {
					break;
				}
			}
		}
		if (mouseOver && printed) {
			std::cout << std::flush;
		}
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
                Term::clearLine();
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

}