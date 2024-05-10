//
// Created by kez on 10/05/24.
//

#include <array>
#include <iomanip>
#include <iterator>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>
#include <htslib/sam.h>
#include <htslib/hts.h>
#include <GLFW/glfw3.h>
#include "hts_funcs.h"
#include "parser.h"
#include "plot_manager.h"
#include "term_out.h"
#include "themes.h"


namespace Commands {

    using Plot = Manager::GwPlot*;

    bool noOp(Plot p) {
        p->redraw = false;
        p->processed = true;
        return false;
    }

    bool triggerClose(Plot p) {
        p->triggerClose = true;
        p->redraw = false;
        p->processed = true;
        return false;
    }

    bool getHelp(Plot p, std::string& command, std::vector<std::string>& parts) {
        if (command == "help" || command == "man") {
            Term::help(p->opts);
        } else if (parts.size() == 2) {
            Term::manuals(parts[1]);
        }
        p->redraw = false;
        p->processed = true;
        return false;
    }

    bool refreshGw(Plot p) {
        p->redraw = true;
        p->processed = false;
        p->imageCache.clear();
        p->filters.clear();
        p->target_qname = "";
        for (auto &cl: p->collections) { cl.vScroll = 0; }
        return false;
    }

    bool line(Plot p) {
        p->drawLine = !p->drawLine;
        p->redraw = true;
        p->processed = true;
        return false;
    }

    bool settings(Plot p) {
        p->last_mode = p->mode;
        p->mode = Manager::Show::SETTINGS;
        p->redraw = true;
        p->processed = true;
        return false;
    }

    bool sam(Plot p) {
        if (!p->selectedAlign.empty()) {
            Term::printSelectedSam(p->selectedAlign);
        }
        p->redraw = false;
        p->processed = true;
        return false;
    }

    bool insertions(Plot p) {
        p->opts.small_indel_threshold = (p->opts.small_indel_threshold == 0) ? std::stoi(p->opts.myIni["view_thresholds"]["small_indel"]) : 0;
        if (p->mode == Manager::Show::SINGLE) {
            p->processed = true;
        } else {
            p->processed = false;
        }
        p->redraw = true;
        p->imageCache.clear();
        return false;
    }

    bool mismatches(Plot p) {
        p->opts.snp_threshold = (p->opts.snp_threshold == 0) ? std::stoi(p->opts.myIni["view_thresholds"]["snp"]) : 0;
        if (p->mode == Manager::Show::SINGLE) {
            p->processed = true;
            for (auto &cl : p->collections) {
                cl.skipDrawingReads = false;
                cl.skipDrawingCoverage = false;
            }
        } else {
            p->processed = false;
        }
        p->redraw = true;
        p->imageCache.clear();
        return true;
    }

    bool edges(Plot p) {
        p->opts.edge_highlights = (p->opts.edge_highlights == 0) ? std::stoi(p->opts.myIni["view_thresholds"]["edge_highlights"]) : 0;
        if (p->mode == Manager::Show::SINGLE) {
            p->processed = true;
            for (auto & cl: p->collections) {
                cl.skipDrawingReads = false;
                cl.skipDrawingCoverage = true;
            }
        } else {
            p->processed = false;
        }
        p->redraw = true;
        p->imageCache.clear();
        return true;
    }

    bool soft_clips(Plot p) {
        p->opts.soft_clip_threshold = (p->opts.soft_clip_threshold == 0) ? std::stoi(p->opts.myIni["view_thresholds"]["soft_clip"]) : 0;
        if (p->mode == Manager::Show::SINGLE) {
            p->processed = true;
        } else {
            p->processed = false;
        }
        p->redraw = true;
        p->imageCache.clear();
        return true;
    }

    bool link(Plot p, std::string& command, std::vector<std::string>& parts) {
        bool relink = false;
        if (command == "link" || command == "link all") {
            relink = (p->opts.link_op != 2) ? true : false;
            p->opts.link_op = 2;
        } else if (parts.size() == 2) {
            if (parts[1] == "sv") {
                relink = (p->opts.link_op != 1) ? true : false;
                p->opts.link_op = 1;
            } else if (parts[1] == "none") {
                relink = (p->opts.link_op != 0) ? true : false;
                p->opts.link_op = 0;
            }
        }
        if (relink) {
            p->imageCache.clear();
            HGW::refreshLinked(p->collections, p->opts, &p->samMaxY);
            p->redraw = true;
            p->processed = true;
        }
        return false;
    }

    bool count(Plot p, std::string& command) {
        std::string str = command;
        str.erase(0, 6);
        Parse::countExpression(p->collections, str, p->headers, p->bam_paths, (int)p->bams.size(), (int)p->regions.size());
        p->redraw = false;
        p->processed = true;
        return false;
    }

    bool addFilter(Plot p, std::string& command) {
        std::string str = command;
        str.erase(0, 7);
        if (str.empty()) {
            return false;
        }
        for (auto &s: Utils::split(str, ';')) {
            Parse::Parser ps = Parse::Parser();
            int rr = ps.set_filter(s, (int)p->bams.size(), (int)p->regions.size());
            if (rr > 0) {
                p->filters.push_back(ps);
                std::cout << command << std::endl;
            }
        }
        p->imageCache.clear();
        p->redraw = true;
        p->processed = false;
        return false;
    }

    bool tags(Plot p, std::string& command) {
        if (!p->selectedAlign.empty()) {
            std::string str = command;
            str.erase(0, 4);
            std::vector<std::string> splitTags = Utils::split(str, ' ');
            std::vector<std::string> splitA = Utils::split(p->selectedAlign, '\t');
            if (splitA.size() > 11) {
                Term::clearLine();
                std::cout << "\r";
                int i = 0;
                for (auto &s : splitA) {
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
        p->redraw = false;
        p->processed = true;
        return false;
    }

    bool findRead(Plot p, std::vector<std::string> parts) {
        if (!p->target_qname.empty() && parts.size() == 1) {
            return false;
        } else if (parts.size() == 2) {
            p->target_qname = parts.back();
        } else {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " please provide one qname\n";
            return false;
        }
        p->highlightQname();
        p->redraw = true;
        p->processed = true;
        p->imageCache.clear();
        return false;
    }

    bool setYlim(Plot p, std::vector<std::string> parts) {
        int ylim = p->opts.ylim;
        int samMaxY = p->opts.ylim;
        int max_tlen = p->opts.max_tlen;
        try {
            if (!p->opts.tlen_yscale) {
                ylim = std::stoi(parts.back());
                samMaxY = p->opts.ylim;
            } else {
                max_tlen = std::stoi(parts.back());
                samMaxY = p->opts.max_tlen;
            }
        } catch (...) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " ylim invalid value\n";
            return false;
        }
        p->imageCache.clear();
        HGW::refreshLinked(p->collections, p->opts, &p->samMaxY);
        p->processed = true;
        p->redraw = true;
        p->opts.ylim = ylim;
        p->samMaxY = samMaxY;
        p->opts.max_tlen = max_tlen;
        return false;
    }

    bool indelLength(Plot p, std::vector<std::string> parts) {
        int indel_length = p->opts.indel_length;
        try {
            indel_length = std::stoi(parts.back());
        } catch (...) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " indel-length invalid value\n";
            return false;
        }
        p->opts.indel_length = indel_length;
        if (p->mode == Manager::Show::SINGLE) {
            p->processed = true;
        } else {
            p->processed = false;
        }
        p->redraw = true;
        p->imageCache.clear();
        return false;
    }

    bool remove(Plot p, std::vector<std::string> parts) {
        int ind = 0;
        if (Utils::startsWith(parts.back(), "bam")) {
            parts.back().erase(0, 3);
            try {
                ind = std::stoi(parts.back());
            } catch (...) {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " bam index not understood\n";
                return false;
            }
            p->removeBam(ind);
        } else if (Utils::startsWith(parts.back(), "track")) {
            parts.back().erase(0, 5);
            try {
                ind = std::stoi(parts.back());
            } catch (...) {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " track index not understood\n";
                return false;
            }
            p->removeTrack(ind);
        } else {
            try {
                ind = std::stoi(parts.back());
            } catch (...) {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " region index not understood\n";
                return false;
            }
            p->removeRegion(ind);
        }
        bool clear_filters = false; // removing a region can invalidate indexes so remove them
        for (auto &f : p->filters) {
            if (!f.targetIndexes.empty()) {
                clear_filters = true;
                break;
            }
        }
        if (clear_filters) {
            p->filters.clear();
        }
        return false;
    }

    bool cov(Plot p, std::vector<std::string> parts) {
        if (parts.size() > 2) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " cov must be either 'cov' to toggle coverage or 'cov NUMBER' to set max coverage\n";
            return false;
        } else if (parts.size() == 1) {
            if (p->opts.max_coverage == 0) {
                p->opts.max_coverage = 10000000;
            } else {
                p->opts.max_coverage = 0;
            }
        } else {
            try {
                p->opts.max_coverage = std::stoi(parts.back());
            } catch (...) {
                std::cerr << termcolor::red << "Error:" << termcolor::reset << " 'cov NUMBER' not understood\n";
                return false;
            }
        }
        for (auto &cl : p->collections) {
            cl.skipDrawingReads = false;
            cl.skipDrawingCoverage = false;
        }
        p->opts.max_coverage = std::max(0, p->opts.max_coverage);
        p->redraw = true;
        p->processed = false;
        p->imageCache.clear();
        return false;
    }

    // Command functions can access these parameters only
    #define PARAMS [](Commands::Plot p, std::string& command, std::vector<std::string>& parts) -> bool

    // Note the function map will be cached after first call. plt is bound, but parts are updated with each call
    bool run_command_map(Plot p, std::string& command) {

        std::vector<std::string> parts = Utils::split(command, ' ');

        static std::unordered_map<std::string, std::function<bool(Plot, std::string&, std::vector<std::string>&)>> functionMap = {

                {":",        PARAMS { return noOp(p); }},
                {"/",        PARAMS { return noOp(p); }},
                {"q",        PARAMS { return triggerClose(p); }},
                {"quit",     PARAMS { return triggerClose(p); }},
                {"r",        PARAMS { return refreshGw(p); }},
                {"refresh",  PARAMS { return refreshGw(p); }},
                {"line",     PARAMS { return line(p); }},
                {"settings", PARAMS { return settings(p); }},
                {"sam",      PARAMS { return sam(p); }},
                {"ins",      PARAMS { return insertions(p); }},
                {"insertions",  PARAMS { return insertions(p); }},
                {"mm",       PARAMS { return mismatches(p); }},
                {"mismatches",  PARAMS { return mismatches(p); }},
                {"edges",    PARAMS { return edges(p); }},
                {"soft_clips",  PARAMS { return soft_clips(p); }},
                {"help",     PARAMS { return getHelp(p, command, parts); }},
                {"man",      PARAMS { return getHelp(p, command, parts); }},
                {"link",     PARAMS { return link(p, command, parts); }},
                {"count",    PARAMS { return count(p, command); }},
                {"filter",   PARAMS { return addFilter(p, command); }},
                {"tags",     PARAMS { return tags(p, command); }},
                {"f",        PARAMS { return findRead(p, parts); }},
                {"find",     PARAMS { return findRead(p, parts); }},
                {"ylim",     PARAMS { return setYlim(p, parts); }},
                {"indel-length", PARAMS { return indelLength(p, parts); }},
                {"remove",   PARAMS { return remove(p, parts); }},
                {"cov",      PARAMS { return cov(p, parts); }},


        };

        auto it = functionMap.find(parts[0]);
        if (it != functionMap.end()) {
            std::cout << "Command success!\n";
            return it->second(p, command, parts);  // Execute the mapped function
        } else {
            std::cout << "Command not found!\n";
            return false;
        }

        p->inputText = "";
    }


}