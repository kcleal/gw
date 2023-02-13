//
// Created by Kez Cleal on 23/08/2022.
//
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
#include "segments.h"
#include "../include/termcolor.h"
#include "term_out.h"
#include "themes.h"
#include"../include/unordered_dense.h"


namespace Manager {

    // keeps track of input commands
    void GwPlot::registerKey(GLFWwindow* wind, int key, int scancode, int action, int mods) {
        if (action == GLFW_RELEASE) {
            if ((key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) && !captureText) {
                shiftPress = false;
            }
            ctrlPress = false;
            return;
        }
	    if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_LEFT_SUPER) {
		    if (action == GLFW_PRESS) {
			    ctrlPress = true;
		    }
	    }
        if (key == GLFW_KEY_LEFT_SHIFT || key == GLFW_KEY_RIGHT_SHIFT) {
            shiftPress = true;
        } else if (shiftPress && GLFW_KEY_SEMICOLON && !captureText) {
            captureText = true;
            inputText.append(":");
            charIndex = inputText.size();
        } else {
            shiftPress = false;
        }
        if (!captureText && (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER)) {
            std::cout << std::endl;
            if (!commandHistory.empty()) {
                inputText = commandHistory.back();
            }
        }
        if (captureText) {
            if (key == GLFW_KEY_ENTER) {
                captureText = false;
                processText = true;
                shiftPress = false;
                std::cout << "\n";
                return;
            }

            if (!commandHistory.empty()) {
                if (key == GLFW_KEY_UP && commandIndex > 0) {
                    commandIndex -= 1;
                    inputText = commandHistory[commandIndex];
                    charIndex = inputText.size();
                    Term::clearLine();
                    return;
                } else if (key == GLFW_KEY_DOWN && commandIndex < (int)commandHistory.size() - 1) {
                    commandIndex += 1;
                    inputText = commandHistory[commandIndex];
                    charIndex = inputText.size();
                    Term::clearLine();
                    return;
                }
            }

            if (key == GLFW_KEY_LEFT) {
                charIndex = (charIndex - 1 >= 0) ? charIndex - 1 : charIndex;
                return;
            } else if (key == GLFW_KEY_RIGHT) {
                charIndex = (charIndex < (int)inputText.size()) ? charIndex + 1 : charIndex;
                return;
            }

            if (ctrlPress && key == GLFW_KEY_V) {
                std::string string = glfwGetClipboardString(window);
                if (!string.empty()) {
                    inputText.append(string);
                    charIndex = inputText.size();
                }
                ctrlPress = false;
            } else {  // character entry
                if (key == GLFW_KEY_SEMICOLON && inputText.size() == 1) {
                    return;
                } else if (key == GLFW_KEY_BACKSPACE) {
                    if (inputText.size() > 1) {
                        inputText.erase(charIndex - 1, 1);
                        charIndex -= 1;
                    }
                } else if (key == GLFW_KEY_DELETE) {
                    if (inputText.size() > 1 && charIndex < (int)inputText.size()) {
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
            }
            return;
        }
        return;
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
            Term::help(opts);
            valid = true;
        } else if (Utils::startsWith(inputText, ":man ")) {
            inputText.erase(0, 5);
            Term::manuals(inputText);
            valid = true;
        } else if (inputText == ":refresh" || inputText == ":r") {
            valid = true; imageCache.clear(); filters.clear();
            for (auto &cl: collections) {cl.vScroll = 0; }
        } else if (inputText == ":link" || inputText == ":link all") {
            opts.link_op = 2;
            redraw = true;
            processed = true;
            inputText = "";
            return true;
        } else if (inputText == ":link sv") {
            opts.link_op = 1;
            redraw = true;
            processed = true;
            inputText = "";
            return true;
        } else if (inputText == ":link none") {
            opts.link_op = 0;
            redraw = true;
            processed = true;
            inputText = "";
            return true;
        } else if (inputText == ":line") {
            drawLine = (drawLine) ? false : true;
            redraw = true;
            processed = true;
            inputText = "";
            return true;
        } else if (Utils::startsWith(inputText, ":count")) {
            std::string str = inputText;
            str.erase(0, 7);
            Parse::countExpression(collections, str, headers, bam_paths, bams.size(), regions.size());
            inputText = "";
            return true;
        } else if (Utils::startsWith(inputText, ":config")) {
            std::string com = opts.editor + " " + opts.ini_path;
            FILE *fp = popen(com.c_str(), "w");
            pclose(fp);
//            opts.readIni();
            std::cout << "Please restart GW to apply any changes\n";
            redraw = true;
            processed = true;
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

        } else if (inputText ==":sam") {
            valid = true;
            if (!selectedAlign.empty()) {
                Term::printSelectedSam(selectedAlign);
            }
            redraw = false;
            processed = true;
            inputText = "";
            return true;
        } else if (inputText ==":tags"){
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
            try {
                opts.ylim = std::stoi(split.back());
                samMaxY = opts.ylim;
                valid = true;
            } catch (...) {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " ylim invalid value\n";
                inputText = "";
                return true;
            }

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
                if (ind >= (int)bams.size()) {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " bam index is out of range. Use 0-based indexing\n";
                    return true;
                }
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
            redraw = true;
            processed = true;
            inputText = "";
            return true;
        } else if (inputText == ":log2-cov") {
            opts.log2_cov = (opts.log2_cov) ? false : true;
            redraw = true;
            processed = true;
            inputText = "";
            return true;
        } else if (Utils::startsWith(inputText, ":mate")) {
            std::string mate;
            Utils::parseMateLocation(selectedAlign, mate, target_qname);
            if (mate.empty()) {
	            std::cerr << termcolor::red << "Error:" << termcolor::reset << " could not parse mate location\n";
                inputText = "";
                return true;
            }
            if (regionSelection >= 0 && regionSelection < (int) regions.size()) {
                if (inputText == ":mate") {
                    regions[regionSelection] = Utils::parseRegion(mate);
                    processed = false;
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
                } else if (inputText == ":mate add") {
                    regions.push_back(Utils::parseRegion(mate));
                    fetchRefSeq(regions.back());
                    processed = false;
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
            }
        } else if (inputText == ":tlen-y") {
            opts.tlen_yscale = !(opts.tlen_yscale);
            if (!opts.tlen_yscale) {
                samMaxY = opts.ylim;
            }
            valid = true;
        } else if (Utils::startsWith(inputText, ":goto")) {
            std::vector<std::string> split = Utils::split(inputText, delim_q);
            if (split.size() == 1) {
                split = Utils::split(inputText, delim);
            }
            if (split.size() > 1 && split.size() < 4) {
                int index;
                try {
                    index = (split.size() == 3) ? std::stoi(split.back()) : 0;
                } catch (...) {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " index not understood\n";
                    inputText = "";
                    return true;
                }
                Utils::Region rgn;
                try {
                    rgn = Utils::parseRegion(split[1]);
                } catch (...) {
                    std::cerr << termcolor::red << "Error:" << termcolor::reset << " region not understood\n";
                    inputText = "";
                    return true;
                }
                if (regions.empty()) {
                    regions.push_back(rgn);
                    fetchRefSeq(regions.back());
                    valid = true;
                } else {
                    if (index < (int)regions.size()) {
                        regions[index] = rgn;
                        fetchRefSeq(regions[index]);
                        valid = true;
                    } else {
                        std::cerr << termcolor::red << "Error:" << termcolor::reset << " region index is out of range. Use 0-based indexing\n";
                        inputText = "";
                        return true;
                    }
                }
            }
        } else if (Utils::startsWith(inputText, ":grid")) {
            std::vector<std::string> split = Utils::split(inputText, delim_q);
            opts.number = Utils::parseDimensions(split[1]);

        } else if (Utils::startsWith(inputText, ":add"))  {
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
        } else if (inputText == ":v" || Utils::startsWith(inputText, ":var")) {
            if (multiLabels.empty()) {
	            std::cerr << termcolor::red << "Error:" << termcolor::reset << " no variant file provided.\n";
                inputText = "";
                return true;
            }
            else if (blockStart+mouseOverTileIndex >= (int)multiLabels.size()) {
	            std::cerr << termcolor::red << "Error:" << termcolor::reset << " index outside of range.";
                inputText = "";
                return true;
            }
			std::vector<std::string> split = Utils::split(inputText, delim);
            Utils::Label &lbl = multiLabels[blockStart + mouseOverTileIndex];
            Term::clearLine();
			if (useVcf) {
				vcf.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
				std::string variantStringCopy = vcf.variantString;
				vcf.get_samples();
				std::vector<std::string> sample_names_copy = vcf.sample_names;
				if (variantStringCopy != "ERROR") {
					int requests = split.size();
					if (requests == 1) {
						std::cout << std::endl << variantStringCopy << std::endl;
					}
					else {
						std::string requestedVars = "";
						std::vector<std::string> vcfCols = Utils::split(variantStringCopy, '\t');
						for (int i = 1; i < requests; ++i) {
							std::string result = "";
							Parse::parse_vcf_split(result, vcfCols, split[i], sample_names_copy);
							if (i != requests-1) {
								requestedVars += split[i]+":\t"+result+",\t";
							}
							else {
								requestedVars += split[i]+":\t"+result;
							}
						}
						std::cout << std::endl << requestedVars << std::endl;
					}
				}
			} else {
				variantTrack.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
				std::string variantStringCopy = variantTrack.variantString;
				if (variantStringCopy != "ERROR") {
					std::cout << std::endl << variantStringCopy << std::endl;
				}
			}
            inputText = "";
            return true;    
		} else if (inputText == ":s" || Utils::startsWith(inputText, ":snapshot")) { // work in progress
			std::vector<std::string> split = Utils::split(inputText, delim);
			std::string fname = "";
			if (split.size() == 1) {
				Parse::parse_sample_variable(fname, bam_paths);
				fname += regions[0].chrom + "_" + std::to_string(regions[0].start) + "_" + std::to_string(regions[0].end) + ".png";
			}
			else {
				std::string nameFormat = split[1];
            	Utils::Label &lbl = multiLabels[blockStart + mouseOverTileIndex];
				if (useVcf) {
					vcf.get_samples();
					std::vector<std::string> sample_names_copy = vcf.sample_names;
					vcf.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
					std::string variantStringCopy = vcf.variantString;
					std::vector<std::string> vcfCols = Utils::split(variantStringCopy, '\t');
					Parse::parse_output_name_format(nameFormat, vcfCols, sample_names_copy, bam_paths, lbl.current());
					fname += nameFormat;
					Utils::trim(fname);
				}
			}

			fs::path outdir = opts.outdir;
			fs::path out_path = outdir / fname;
			std::cout << std::endl << "Saved to:\t" << out_path << std::endl;
			
			if (!imageCacheQueue.empty()) {
				Manager::imagePngToFile(imageCacheQueue.back().second, out_path);
			}
			// Utils::Label &lbl = multiLabels[blockStart + mouseOverTileIndex];
			return true;
        } else {
	        inputText.erase(0, 1);
	        Utils::Region rgn;
			try {
				rgn = Utils::parseRegion(inputText);
				valid = true;
			} catch (...) {
				valid = false;
			}
	        if (valid) {
		        int res = faidx_has_seq(fai, rgn.chrom.c_str());
		        if (res <= 0) {
			        std::cerr << termcolor::red << "Error:" << termcolor::reset << " chromosome not found in header " << rgn.chrom << std::endl;
			        valid = false;
		        }
			}
			if (valid) {
				try {
					if (regions.empty()) {
						regions.push_back(rgn);
						fetchRefSeq(regions.back());
					} else {
						regions[0] = rgn;
						fetchRefSeq(regions[0]);
					}
					valid = true;
				} catch (...) {
					valid = false;
				}
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

    void GwPlot::printRegionInfo() {
        if (regions.empty()) {
            return;
        }
        Term::clearLine();
        std::cout << termcolor::bold << "\rPos   " << termcolor::reset ;
        int i = 0;
        auto r = regions[regionSelection];
        std::cout << termcolor::cyan << r.chrom << ":" << r.start << "-" << r.end << termcolor::white << "  (" << Utils::getSize(r.end - r.start) << ")";
        if (i != (int)regions.size() - 1) {
            std::cout << "    ";
        }
        std::cout << termcolor::reset << std::flush;
    }

    void GwPlot::keyPress(GLFWwindow* wind, int key, int scancode, int action, int mods) {
        if (action == GLFW_RELEASE) {
			ctrlPress = false;
            return;
        }

        // decide if the input key is part of a command or a redraw request
        registerKey(window, key, scancode, action, mods);
        if (captureText) {
            glfwPostEmptyEvent();
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
            if (regions.empty() || regionSelection < 0) {
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
//                    if (opts.link_op != 0) {
//                        processed = false;
//                        redraw = true;
//                    } else {
                        processed = true;
                        for (auto &cl : collections) {
                            if (cl.regionIdx == regionSelection) {
                                cl.region = N;
                                if (!bams.empty()) {
                                    HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx],
                                                                indexes[cl.bamIdx], opts, (bool)opts.max_coverage, false,
                                                                &samMaxY, filters);
                                }
                            }
                        }
                        redraw = true;
//                    }
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
//                    if (opts.link_op != 0) {
//                        processed = false;
//                        redraw = true;
//                    } else {
                        processed = true;
                        for (auto &cl : collections) {
                            if (cl.regionIdx == regionSelection) {
                                cl.region = regions[regionSelection];
                                if (!bams.empty()) {
                                    HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx],
                                                                indexes[cl.bamIdx], opts, (bool)opts.max_coverage, true,
                                                                &samMaxY, filters);
                                }
                            }
//                        }
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
                                                                opts, false, true,  &samMaxY, filters);
                                    HGW::appendReadsAndCoverage(cl, bams[cl.bamIdx], headers[cl.bamIdx], indexes[cl.bamIdx],
                                                                opts, false, false, &samMaxY, filters);
                                    if (opts.max_coverage) {  // re process coverage for all reads
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
                                        HGW::trimToRegion(cl, opts.max_coverage);
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
                        if (cl.regionIdx == regionSelection) {  // should be possible to vScroll without needing findY for all reads, but complicated to implement
                            cl.vScroll += 2;
                            cl.levelsStart.clear();
                            cl.levelsEnd.clear();
                            cl.linked.clear();
                            for (auto &itm: cl.readQueue) { itm.y = -1; }
                            int maxY = Segs::findY(cl, cl.readQueue, opts.link_op, opts, &cl.region,  false);
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
                            int maxY = Segs::findY(cl, cl.readQueue, opts.link_op, opts, &cl.region, false);
                            samMaxY = (maxY > samMaxY || opts.tlen_yscale) ? maxY : samMaxY;
                        }
                    }
                    redraw = true;
                    processed = true;
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
                        Term::clearLine();
                        std::cout << termcolor::green << "\rIndex     " << termcolor::reset << blockStart << std::endl;
                    }
                } else if (key == opts.scroll_left) {
                    if (blockStart == 0) {
                        return;
                    }
                    blockStart = (blockStart - bLen > 0) ? blockStart - bLen : 0;
                    redraw = true;
                    Term::clearLine();
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
            return -2;  // reference
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
                float max_y = fb_height - refSpace - totalTabixY;
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

        if (xDrag == -1000000) {
            xDrag = 0;
            xOri = x;
        }
        xDrag = x - xOri;

        if (mode == Manager::SINGLE && button == GLFW_MOUSE_BUTTON_LEFT) {
            if (regions.empty()) {
                return;
            }

            if (yW >= (fb_height * 0.98)) {
                updateSlider(xW);
                return;
            }

            int idx = getCollectionIdx(xW, yW);
            if (idx == -2 && action == GLFW_RELEASE) {
                Term::printRefSeq(xW, collections);
            } else if (idx <= -3 && action == GLFW_RELEASE) {
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
                clicked = cl.region;
                clickedIdx = idx;
            }
            if (std::abs(xDrag) < 5 && action == GLFW_RELEASE && !bams.empty()) {
                int pos = (int) (((xW - (float) cl.xOffset) / cl.xScaling) + (float) cl.region.start);

                if (ctrlPress) {  // zoom in to mouse position
                    int strt = pos - 2500;
                    strt = (strt < 0) ? 0 : strt;
                    Utils::Region N;
                    N.chrom = cl.region.chrom;
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
                    level = (int) ((yW - (float) cl.yOffset) / (trackY / (float)(cl.levelsStart.size() - cl.vScroll )));
                    if (level < 0) {  // print coverage info
                        Term::clearLine();
                        Term::printCoverage(pos, cl);
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
                            Term::printRead(bnd, headers[cl.bamIdx], selectedAlign, cl.region.refSeq, cl.region.start, cl.region.end, opts.low_mem);
                            redraw = true;
                            processed = true;
                            break;
                        }
                    } else {
                        if ((bnd->y >= level - slop && bnd->y < level) && (int)bnd->pos <= pos && pos < (int)bnd->reference_end) {
                            bnd->edge_type = 4;
                            target_qname = bam_get_qname(bnd->delegate);
                            Term::printRead(bnd, headers[cl.bamIdx], selectedAlign, cl.region.refSeq, cl.region.start, cl.region.end, opts.low_mem);
                            redraw = true;
                            processed = true;
                        }
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
                    if (N.start < 0 || N.end < 0) {
                        return;
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
                                HGW::appendReadsAndCoverage(col,  bams[col.bamIdx], headers[col.bamIdx], indexes[col.bamIdx], opts, (bool)opts.max_coverage, lt_last, &samMaxY, filters);
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
                imageCacheQueue.clear();
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
                if (yPos >= (fb_height * 0.98)) {
                    updateSlider((float)xPos);
                    return;
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
                    if (N.start < 0 || N.end < 0) {
                        return;
                    }

                    regionSelection = cl.regionIdx;
                    delete regions[regionSelection].refSeq;
                    N.markerPos = regions[regionSelection].markerPos;
                    N.markerPosEnd = regions[regionSelection].markerPosEnd;
                    fetchRefSeq(N);
                    regions[regionSelection] = N;
                    processed = true;
                    for (auto &col : collections) {
                        if (col.regionIdx == regionSelection) {
                            col.region = regions[regionSelection];
                            if (!bams.empty()) {
                                HGW::appendReadsAndCoverage(col, bams[col.bamIdx], headers[col.bamIdx],
                                                            indexes[col.bamIdx], opts, (bool)opts.max_coverage, !lt_last,
                                                             &samMaxY, filters);
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
	            if (rs <= -3) {  // print track info
		            float rgS = ((float)fb_width / (float)regions.size());
		            int tIdx = (int)((xPos) / rgS);
		            if (tIdx < (int)regions.size()) {
			            float relX = xPos - gap;
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
                    if (rs == -2) {
                        Term::updateRefGenomeSeq((float)xPos, collections);
                    }
                    return;
                }
                Segs::ReadCollection &cl = collections[rs];
                regionSelection = cl.regionIdx;

	            int pos = (int) (((xPos - (float) cl.xOffset) / cl.xScaling) + (float) cl.region.start);
	            int level = (int) ((yPos - (float) cl.yOffset) / (trackY / (float)(cl.levelsStart.size() - cl.vScroll )));
	            if (level < 0 && cl.region.end - cl.region.start < 50000) {
		            Term::clearLine();
		            Term::printCoverage(pos, cl);
		            return;
	            }

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
                    Term::clearLine();
                    std::cout << termcolor::bold << "\rPos  " << termcolor::reset << lbl.chrom << ":" << lbl.pos << termcolor::bold <<
                              "    ID  "  << termcolor::reset << lbl.variantId << termcolor::bold <<
                              "    Type  "  << termcolor::reset << lbl.vartype;
                    std::cout << std::flush;
                }
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
