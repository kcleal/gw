//
// Created by kez on 10/05/24.
//

#include <array>
#include <iomanip>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <functional>
#include <vector>
#include <queue>
#include <htslib/sam.h>
#include <htslib/hts.h>
#include <htslib/bgzf.h>
#include <htslib/hfile.h>
#include <htslib/cram.h>
#include <hts_funcs.h>
#include <filesystem>
#include <iostream>

#if defined(_WIN32)

#else
    #include <pwd.h>
#endif

#include "parser.h"
#include "plot_manager.h"
#include "term_out.h"
#include "themes.h"


#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"
#include "include/core/SkDocument.h"
#include "include/docs/SkPDFDocument.h"
#include "include/core/SkStream.h"
#include "include/core/SkPictureRecorder.h"
#include "include/core/SkPicture.h"
#include "include/svg/SkSVGCanvas.h"


/* Notes for adding new commands:
 * 1. Make a new function, accepting a pointer to a GwPlot instance and any other args
 * 2. Add function to run_command_map below
 * 3. Add documentation in term_out.cpp - quick help in menu.cpp text as well as a manual in term_out.cpp
 * 4. Register command in the menu functions if needed:
 *     register with optionFromStr function
 *     add to relevant functions
 *         getCommandSwitchValue applyBoolOption, applyKeyboardKeyOption etc
 *     add a tool tip in drawMenu
 *     if the function should be in the pop-up menu, add to commandToolTip in menu.h
 *     if the function should be applied straight away add to exec in menu.h
 * 5. Optionally add a setting to .gw.ini
 * 6. If new option should be saved in a session add it to saveCurrentSession in themes.cpp
 *    Loading from a session should also be detailed in loadSession in plot_manager.cpp
*/

namespace Commands {

    enum Err {
        NONE = 0,
        UNKNOWN,
        SILENT,

        TOO_MANY_OPTIONS,
        CHROM_NOT_IN_REFERENCE,
        FEATURE_NOT_IN_TRACKS,
        BAD_REGION,
        OPTION_NOT_SUPPORTED,
        OPTION_NOT_UNDERSTOOD,
        INVALID_PATH,
        EMPTY_TRACKS,
        EMPTY_BAMS,
        EMPTY_REGIONS,
        EMPTY_VARIANTS,

        PARSE_VCF,
        PARSE_INPUT,

    };

    using Plot = Manager::GwPlot;

    Err noOp(Plot* p) {
        p->redraw = false;
        p->processed = true;
        return Err::NONE;
    }

    Err triggerClose(Plot* p) {
        p->triggerClose = true;
        p->redraw = false;
        p->processed = true;
        return Err::NONE;
    }

    Err getHelp(Plot* p, std::string& command, std::vector<std::string>& parts, std::ostream& out) {
        if (command == "h" || command == "help" || command == "man") {
            Term::help(p->opts, out);
        } else if (parts.size() == 2) {
            Term::manuals(parts[1], out);
        }
        p->redraw = false;
        p->processed = true;
        return Err::NONE;
    }

    Err refreshGw(Plot* p) {
        p->redraw = true;
        p->processed = false;
        if (p->frameId >= 0) {
            p->imageCache.clear();
            p->imageCacheQueue.clear();
            p->filters.clear();
            p->target_qname = "";
            for (auto &cl: p->collections) {
                cl.vScroll = 0;
                cl.resetDrawState();
            }
            for (auto &rgn: p->regions) {
                rgn.sortOption = Utils::SortType::NONE, rgn.sortPos = -1;
                rgn.refBaseAtPos = '\0';
            }
        }
        return Err::NONE;
    }

    Err line(Plot* p) {
        p->drawLine = !p->drawLine;
        p->redraw = false;
        p->processed = true;
        return Err::NONE;
    }

    Err settings(Plot* p, std::string subHeading) {
        p->last_mode = p->mode;
        p->mode = Manager::Show::SETTINGS;
        p->redraw = true;
        p->processed = true;
        if (subHeading == "settings") {
            p->opts.menu_table = Themes::MenuTable::MAIN;
        } else if (subHeading == "general") {
            p->opts.menu_table = Themes::MenuTable::GENERAL;
        }
        else if (subHeading == "genomes") {
            p->opts.menu_table = Themes::MenuTable::GENOMES;
        }
        else if (subHeading == "interaction") {
            p->opts.menu_table = Themes::MenuTable::INTERACTION;
        }
        else if (subHeading == "labelling") {
            p->opts.menu_table = Themes::MenuTable::LABELLING;
        }
        else if (subHeading == "navigation") {
            p->opts.menu_table = Themes::MenuTable::NAVIGATION;
        }
        else if (subHeading == "tracks") {
            p->opts.menu_table = Themes::MenuTable::TRACKS;
        }
        else if (subHeading == "thresholds") {
            p->opts.menu_table = Themes::MenuTable::VIEW_THRESHOLDS;
        }
        else if (subHeading == "keymap") {
            p->opts.menu_table = Themes::MenuTable::SHIFT_KEYMAP;
        }
        return Err::NONE;
    }

    Err sam(Plot* p, std::string& command, std::vector<std::string>& parts, std::ostream& out) {
        if (!p->selectedAlign.empty()) {
            if (command == "sam") {
                Term::printSelectedSam(p->selectedAlign, out);
                return Err::NONE;
            } else if (parts.size() == 3 && (Utils::endsWith(parts[2], ".sam") || Utils::endsWith(parts[2], ".bam") || Utils::endsWith(parts[2], ".cram"))) {
                std::string o_str = parts[2];
                if (p->headers.empty() || p->regionSelection >= (int)p->headers.size()) {
                    return Err::SILENT;
                }
                sam_hdr_t *hdr = sam_hdr_dup(p->headers[p->regionSelection]);

                cram_fd* fc = nullptr;
                htsFile *h_out = nullptr;
                bool write_cram = false;
                int res;
                std::string full_path = Parse::tilde_to_home(parts[2]);
                const char* outf = full_path.c_str();
                if (parts[1] == ">" || !Utils::is_file_exist(full_path)) {
                    out << "Creating new file: " << outf << "\n";
                    if (Utils::endsWith(o_str, ".sam")) {
                        h_out = hts_open(outf, "w");
                        res = sam_hdr_write(h_out, hdr);
                        if (res < 0) {
                            out << termcolor::red << "Error:" << termcolor::reset << " Failed to copy header\n";
                            sam_close(h_out);
                            return Err::NONE;
                        }
                    } else if (Utils::endsWith(o_str, ".bam")) {
                        h_out = hts_open(outf, "wb");
                        res = sam_hdr_write(h_out, hdr);
                        if (res < 0) {
                            out << termcolor::red << "Error:" << termcolor::reset << " Failed to copy header\n";
                            sam_close(h_out);
                            return Err::NONE;
                        }
                    } else {
                        h_out = hts_open(outf, "wc");
                        fc = h_out->fp.cram;
                        write_cram = true;
                        cram_fd_set_header(fc, hdr);
                        res = sam_hdr_write(h_out, hdr);
                        if (res < 0) {
                            out << termcolor::red << "Error:" << termcolor::reset << " Failed to copy header\n";
                            sam_close(h_out);
                            return Err::NONE;
                        }
                    }
                } else if (parts[1] == ">>") {
                    out << "Appending to file: " << outf << "\n";
                    if (Utils::endsWith(o_str, ".sam")) {
                        h_out = hts_open(outf, "a");
                    } else if (Utils::endsWith(o_str, ".bam")) {
                        h_out = hts_open(outf, "ab");
                    } else {
                        h_out = hts_open(outf, "ac");
                        fc = h_out->fp.cram;
                        write_cram = true;
                    }
                }
                bam1_t* b = bam_init1();
                kstring_t kstr = {0, 0, nullptr};
                std::string& stdStr = p->selectedAlign;
                kstr.l = stdStr.size();
                kstr.m = stdStr.size() + 1;
                kstr.s = (char*)malloc(kstr.m);
                memcpy(kstr.s, stdStr.data(), stdStr.size());
                kstr.s[kstr.l] = '\0';
                std::cerr << kstr.s << std::endl;
                res = sam_parse1(&kstr, hdr, b);
                free(kstr.s);
                if (res < 0) {
                    out << termcolor::red << "Error:" << termcolor::reset << " Could not convert str to bam1_t\n";
                    bam_destroy1(b);
                    sam_close(h_out);
                    return Err::NONE;
                }
                if (!write_cram) {
                    hts_set_fai_filename(h_out, p->reference.c_str());
                    hts_set_threads(h_out, p->opts.threads);
                } else {
                    cram_set_option(fc, CRAM_OPT_REFERENCE, p->fai);
                    cram_set_option(fc, CRAM_OPT_NTHREADS, p->opts.threads);
                }
                res = sam_write1(h_out, hdr, b);
                if (res < 0) {
                    out << termcolor::red << "Error:" << termcolor::reset << "Write failed\n";
                }
                bam_destroy1(b);
                sam_close(h_out);
            }
        }
        return Err::NONE;
    }

    Err insertions(Plot* p) {
        p->opts.small_indel_threshold = (p->opts.small_indel_threshold == 0) ? std::stoi(p->opts.myIni["view_thresholds"]["small_indel"]) : 0;
        p->processed = false;
        p->redraw = true;
        p->imageCache.clear();
        p->imageCacheQueue.clear();
        return Err::NONE;
    }

    Err mismatches(Plot* p) {
        p->opts.snp_threshold = (p->opts.snp_threshold == 0) ? std::stoi(p->opts.myIni["view_thresholds"]["snp"]) : 0;
        p->processed = false;
        p->redraw = true;
        p->imageCache.clear();
        p->imageCacheQueue.clear();
        return Err::NONE;
    }

    Err mods(Plot* p) {
        p->opts.parse_mods = !(p->opts.parse_mods);
        p->processed = false;
        p->redraw = true;
        p->imageCache.clear();
        p->imageCacheQueue.clear();
        return Err::NONE;
    }

    Err edges(Plot* p) {
        p->opts.edge_highlights = (p->opts.edge_highlights == 0) ? std::stoi(p->opts.myIni["view_thresholds"]["edge_highlights"]) : 0;
        p->processed = false;
        p->redraw = true;
        p->imageCache.clear();
        p->imageCacheQueue.clear();
        return Err::NONE;
    }

    Err soft_clips(Plot* p) {
        p->opts.soft_clip_threshold = (p->opts.soft_clip_threshold == 0) ? std::stoi(p->opts.myIni["view_thresholds"]["soft_clip"]) : 0;
        refreshGw(p);
        return Err::NONE;
    }

    Err log2_cov(Plot* p) {
        p->opts.log2_cov = !(p->opts.log2_cov);
        if (p->frameId >= 0) {
            p->redraw = true;
            if (p->mode == Manager::Show::SINGLE) {
                p->processed = true;
                for (auto &cl : p->collections) {
                    cl.resetDrawState();
                }
            } else {
                p->processed = false;
            }
            p->imageCache.clear();
            p->imageCacheQueue.clear();
        }
        return Err::NONE;
    }

    Err expand_tracks(Plot* p) {
        p->opts.expand_tracks = !(p->opts.expand_tracks);
        refreshGw(p);
        return Err::NONE;
    }

    Err tlen_y(Plot* p) {
        if (!p->opts.tlen_yscale) {
            p->opts.max_tlen = 2000;
            p->opts.ylim = 2000;
            p->samMaxY = 2000;
        } else {
            p->opts.ylim = 60;
            p->samMaxY = 60;
        }
        p->opts.tlen_yscale = !p->opts.tlen_yscale;
        p->processed = false;
        p->redraw = true;
        if (p->mode != Manager::Show::SINGLE) {
            refreshGw(p);
        }
        return Err::NONE;
    }

    Err alignments(Plot* p) {
        p->opts.alignments = !p->opts.alignments;
        if (p->frameId >= 0) {
            for (auto &cl: p->collections) {
                cl.resetDrawState();
            }
            p->redraw = true;
        }
        if (p->mode != Manager::Show::SINGLE) {
            refreshGw(p);
        }
        return Err::NONE;
    }

    Err labels(Plot* p) {
        p->opts.data_labels = !p->opts.data_labels;
        if (p->frameId >= 0) {
            for (auto &cl: p->collections) {
                cl.resetDrawState();
            }
            p->redraw = true;
        }
        return Err::NONE;
    }

    Err link(Plot* p, std::string& command, std::vector<std::string>& parts) {
        bool relink = false;
        if (command == "link" || command == "link all") {
            relink = p->opts.link_op != 2;
            p->opts.link_op = 2;
            p->opts.link = "all";
        } else if (parts.size() == 2) {
            if (parts[1] == "sv") {
                relink = p->opts.link_op != 1;
                p->opts.link_op = 1;
                p->opts.link = "sv";
            } else if (parts[1] == "none") {
                relink = p->opts.link_op != 0;
                p->opts.link_op = 0;
                p->opts.link = "none";
            }
        }
        if (relink && p->frameId >= 0) {
            refreshGw(p);
        }
        return Err::NONE;
    }

    Err var_info(Plot* p, std::string& command, std::vector<std::string>& parts, std::ostream& out) {
        p->redraw = false;
        if (p->variantTracks.empty()) {
            return Err::EMPTY_VARIANTS;
        }
        p->currentVarTrack = &p->variantTracks[p->variantFileSelection];
        if (p->currentVarTrack->multiLabels.empty()) {
            return Err::EMPTY_VARIANTS;
        } else if (p->currentVarTrack->blockStart + p->mouseOverTileIndex >= (int)p->currentVarTrack->multiLabels.size() || p->mouseOverTileIndex == -1) {
            return Err::SILENT;
        }
        Utils::Label &lbl = p->currentVarTrack->multiLabels[p->currentVarTrack->blockStart + p->mouseOverTileIndex];
        Term::clearLine(out);
        if (p->currentVarTrack->type == HGW::TrackType::VCF) {
            p->currentVarTrack->vcf.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
            std::string variantStringCopy = p->currentVarTrack->vcf.variantString;
            p->currentVarTrack->vcf.get_samples();
            std::vector<std::string> sample_names_copy = p->currentVarTrack->vcf.sample_names;
            if (variantStringCopy.empty()) {
                out << termcolor::red << "Error:" << termcolor::reset << " could not parse vcf/bcf line";
                return Err::PARSE_VCF;
            } else {
                size_t requests = parts.size();
                if (requests == 1) {
                    Term::clearLine(out);
                    out << "\r" << variantStringCopy << std::endl;
                } else {
                    std::string requestedVars;
                    std::vector<std::string> vcfCols = Utils::split(variantStringCopy, '\t');
                    for (size_t i = 1; i < requests; ++i) {
                        std::string result;
                        try {
                            Parse::parse_vcf_split(result, vcfCols, parts[i], sample_names_copy, out);
                        } catch (...) {
                            out << termcolor::red << "Error:" << termcolor::reset << " could not parse " << parts[i] << ". Valid fields are chrom, pos, id, ref, alt, qual, filter, info, format\n";
                            return Err::PARSE_VCF;
                        }
                        if (i != requests-1) {
                            requestedVars += parts[i] + ": " + result + "\t";
                        } else {
                            requestedVars += parts[i] + ": " + result;
                        }
                    }
                    if (!requestedVars.empty()) {
                        Term::clearLine(out);
                        out << "\r" << requestedVars << std::endl;
                    }
                }
            }
        } else {
            p->currentVarTrack->variantTrack.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
            if (p->currentVarTrack->variantTrack.variantString.empty()) {
                Term::clearLine(out);
                out << "\r" << p->currentVarTrack->variantTrack.variantString << std::endl;
            } else {
                out << termcolor::red << "Error:" << termcolor::reset << " could not parse variant line";
                return Err::PARSE_VCF;
            }
        }
        return Err::NONE;
    }

    Err count(Plot* p, std::string& command, std::ostream& out) {
        p->redraw = false;
        std::string str = command;
        str.erase(0, 6);
        Parse::countExpression(p->collections, str, p->headers, p->bam_paths, (int)p->bams.size(), (int)p->regions.size(), out);
        p->processed = true;
        return Err::NONE;
    }

    Err addFilter(Plot* p, std::string& command, std::ostream& out) {
        p->redraw = true;
        std::string str = command;
        str.erase(0, 7);
        if (str.empty()) {
            return Err::NONE;
        }
        for (auto &s: Utils::split(str, ';')) {
            Parse::Parser ps = Parse::Parser(out);
            int rr = ps.set_filter(s, (int)p->bams.size(), (int)p->regions.size());
            if (rr > 0) {
                p->filters.push_back(ps);
                out << command << std::endl;
            }
        }
        if (p->frameId >= 0) {
            p->imageCache.clear();
            p->imageCacheQueue.clear();
            p->processed = false;
        }
        return Err::NONE;
    }

    Err tags(Plot* p, std::string& command, std::ostream& out) {
        if (!p->selectedAlign.empty()) {
            std::string str = command;
            str.erase(0, 4);
            std::vector<std::string> splitTags = Utils::split(str, ' ');
            std::vector<std::string> splitA = Utils::split(p->selectedAlign, '\t');
            if (splitA.size() > 11) {
                Term::clearLine(out);
                out << "\r";
                int i = 0;
                for (auto &s : splitA) {
                    if (i > 11) {
                        std::string t = s.substr(0, s.find(':'));
                        if (splitTags.empty()) {
                            std::string rest = s.substr(s.find(':'), s.size());
                            out << termcolor::green << t << termcolor::reset << rest << "\t";
                        } else {
                            for (auto &target : splitTags) {
                                if (target == t) {
                                    std::string rest = s.substr(s.find(':'), s.size());
                                    out << termcolor::green << t << termcolor::reset << rest << "\t";
                                }
                            }
                        }
                    }
                    i += 1;
                }
                out << std::endl;
            }
        }
        if (p->frameId >= 0) {
            p->processed = true;
            p->redraw = false;
        }
        return Err::NONE;
    }

    Err mate(Plot* p, std::string& command, std::ostream& out) {
        p->redraw = true;
        std::string mate;
        Utils::parseMateLocation(p->selectedAlign, mate, p->target_qname);
        if (mate.empty()) {
            out << termcolor::red << "Error:" << termcolor::reset << " no read has been selected\n";
            return Err::SILENT;
        }
        if (p->regionSelection >= 0 && p->regionSelection < (int)p->regions.size()) {
            if (command == "mate") {
                p->regions[p->regionSelection] = Utils::parseRegion(mate);
                p->processed = false;
                for (auto &cl: p->collections) {
                    if (cl.regionIdx == p->regionSelection) {
                        cl.region = &(p->regions)[p->regionSelection];
                        cl.readQueue.clear();
                        cl.covArr.clear();
                        cl.levelsStart.clear();
                        cl.levelsEnd.clear();
                    }
                }
                p->processBam();
                p->highlightQname();
                p->redraw = true;
                if (p->mode == Manager::Show::SINGLE) {
                    p->processed = true;
                } else {
                    p->processed = false;
                }
                p->imageCache.clear();
                p->imageCacheQueue.clear();
            } else if (command == "mate add" && p->mode == Manager::Show::SINGLE) {
                p->regions.push_back(Utils::parseRegion(mate));
                p->fetchRefSeq(p->regions.back());
                p->processed = false;
                p->processBam();
                p->highlightQname();
                p->redraw = true;
                p->processed = true;
                p->imageCache.clear();
                p->imageCacheQueue.clear();
            }
        }
        return Err::NONE;
    }

    Err findRead(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        p->redraw = true;
        if (!p->target_qname.empty() && parts.size() == 1) {
            return Err::NONE;
        } else if (parts.size() == 2) {
            p->target_qname = parts.back();
        } else {
            out << termcolor::red << "Error:" << termcolor::reset << " please provide one qname\n";
            return Err::NONE;
        }
        p->highlightQname();
        p->processed = true;
//        p->imageCache.clear();
//        p->imageCacheQueue.clear();
        return Err::NONE;
    }

    Err setYlim(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        p->redraw = true;
        try {
            if (!p->opts.tlen_yscale) {
                p->opts.ylim = std::stoi(parts.back());
                p->samMaxY = p->opts.ylim;
            } else {
                p->opts.max_tlen = std::stoi(parts.back());
                p->samMaxY = p->opts.max_tlen;
            }
        } catch (...) {
            out << termcolor::red << "Error:" << termcolor::reset << " ylim invalid value\n";
            return Err::NONE;
        }
        refreshGw(p);
        return Err::NONE;
    }

    Err indelLength(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        p->redraw = true;
        int indel_length;
        try {
            indel_length = std::stoi(parts.back());
        } catch (...) {
            out << termcolor::red << "Error:" << termcolor::reset << " indel-length invalid value\n";
            return Err::NONE;
        }
        p->opts.indel_length = indel_length;
        if (p->frameId >= 0) {
            p->processed = false;
            p->imageCache.clear();
            p->imageCacheQueue.clear();
        }
        return Err::NONE;
    }

    Err remove(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        int ind = 0;
        p->redraw = true;
        if (Utils::startsWith(parts.back(), "bam")) {
            parts.back().erase(0, 3);
            try {
                ind = std::stoi(parts.back());
            } catch (...) {
                out << termcolor::red << "Error:" << termcolor::reset << " bam index not understood\n";
                return Err::SILENT;
            }
            p->removeBam(ind);
        } else if (Utils::startsWith(parts.back(), "track")) {
            parts.back().erase(0, 5);
            try {
                ind = std::stoi(parts.back());
            } catch (...) {
                out << termcolor::red << "Error:" << termcolor::reset << " track index not understood\n";
                return Err::SILENT;
            }
            p->removeTrack(ind);

        } else if (Utils::startsWith(parts.back(), "var")) {
            parts.back().erase(0, 3);
            try {
                ind = std::stoi(parts.back());
            } catch (...) {
                out << termcolor::red << "Error:" << termcolor::reset << " var index not understood\n";
                return Err::SILENT;
            }
            p->removeVariantTrack(ind);
        } else if (Utils::startsWith(parts.back(), "ideogram")) {
            p->ideogram.clear();
        } else {
            try {
                ind = std::stoi(parts.back());
            } catch (...) {
                out << termcolor::red << "Error:" << termcolor::reset << " region index not understood\n";
                return Err::SILENT;
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
        for (auto &cl : p->collections) {
            cl.resetDrawState();
        }

        p->imageCacheQueue.clear();
        return Err::NONE;
    }

    Err cov(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        p->redraw = true;
        if (parts.size() > 2) {
            out << termcolor::red << "Error:" << termcolor::reset << " cov must be either 'cov' to toggle coverage or 'cov NUMBER' to set max coverage\n";
            return Err::NONE;
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
                out << termcolor::red << "Error:" << termcolor::reset << " 'cov NUMBER' not understood\n";
                return Err::NONE;
            }
        }
        p->opts.max_coverage = std::max(0, p->opts.max_coverage);
        if (p->frameId >= 0) {
            for (auto &cl: p->collections) {
                cl.resetDrawState();
            }
            p->processed = false;
            p->imageCache.clear();
            p->imageCacheQueue.clear();
        }
        return Err::NONE;
    }

    Err theme(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        p->redraw = true;
        if (parts.size() != 2) {
            out << termcolor::red << "Error:" << termcolor::reset << " theme must be either 'igv', 'dark' or 'slate'\n";
            return Err::NONE;
        }
        if (parts.back() == "dark") {
            p->opts.theme = Themes::DarkTheme();  p->opts.theme.setAlphas(); p->imageCache.clear(); p->imageCacheQueue.clear(); p->opts.theme_str = "dark";
        } else if (parts.back() == "igv") {
            p->opts.theme = Themes::IgvTheme(); p->opts.theme.setAlphas(); p->imageCache.clear(); p->imageCacheQueue.clear(); p->opts.theme_str = "igv";
        } else if (parts.back() == "slate") {
            p->opts.theme = Themes::SlateTheme(); p->opts.theme.setAlphas(); p->imageCache.clear(); p->imageCacheQueue.clear(); p->opts.theme_str = "slate";
        } else {
            return Err::OPTION_NOT_UNDERSTOOD;
        }
        for (auto &t: p->tracks) {
            t.setPaint(p->opts.theme.fcTrack);
        }
        p->processed = false;
        return Err::NONE;
    }

    Err goto_command(Plot* p, std::vector<std::string> parts) {
        Err reason = Err::NONE;
        p->redraw = true;
        if (parts.size() > 1 && parts.size() < 4) {
            int index = p->regionSelection;
            Utils::Region rgn;
            try {
                rgn = Utils::parseRegion(parts[1]);
                int res = faidx_has_seq(p->fai, rgn.chrom.c_str());
                if (res <= 0) {
                    reason = Err::CHROM_NOT_IN_REFERENCE;
                }
            } catch (...) {
                reason = Err::BAD_REGION;
            }
            if (reason == Err::NONE) {
                if (p->mode != Manager::Show::SINGLE) { p->mode = Manager::Show::SINGLE; }
                if (p->regions.empty()) {
                    p->regions.push_back(rgn);
                    p->fetchRefSeq(p->regions.back());
                } else {
                    if (index < (int)p->regions.size()) {
                        if (p->regions[index].chrom == rgn.chrom) {
                            rgn.markerPos = p->regions[index].markerPos;
                            rgn.markerPosEnd = p->regions[index].markerPosEnd;
                        }
                        p->regions[index] = rgn;
                        p->fetchRefSeq(p->regions[index]);
                    }
                }
            } else {  // search all tracks for matching name, slow but ok for small tracks
                if (!p->tracks.empty()) {
                    bool res = HGW::searchTracks(p->tracks, parts[1], rgn);
                    if (res) {
                        reason = Err::NONE;
                        if (p->mode != Manager::Show::SINGLE) { p->mode = Manager::Show::SINGLE; }
                        if (p->regions.empty()) {
                            p->regions.push_back(rgn);
                            p->fetchRefSeq(p->regions.back());
                        } else {
                            if (index < (int)p->regions.size()) {
                                p->regions[index] = rgn;
                                p->fetchRefSeq(p->regions[index]);
                            }
                        }
                    } else {
                        reason = Err::SILENT;
                    }
                }
            }
        }
        if (reason == Err::NONE) {
            refreshGw(p);
        }
        return reason;
    }

    Err grid(Plot* p, std::vector<std::string> parts) {
        p->redraw = true;
        try {
            p->opts.number = Utils::parseDimensions(parts[1]);
        } catch (...) {
            return Err::PARSE_INPUT;
        }
        if (p->frameId >= 0) {
            p->processed = false;
            p->imageCache.clear();
            p->imageCacheQueue.clear();
        }
        return Err::NONE;
    }

    Err add_region(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        p->redraw = true;
        if (parts.size() <= 1) {
            out << termcolor::red << "Error:" << termcolor::reset << " expected a Region e.g. chr1:1-20000\n";
            return Err::PARSE_INPUT;
        }
        std::vector<Utils::Region> new_regions;
        for (size_t i=1; i < parts.size(); ++i) {
            if (parts[i] == "mate") {
                out << "Did you mean to use the 'mate add' function instead?\n";
                break;
            }
            try {
                Utils::Region dummy_region = Utils::parseRegion(parts[i]);
                int res = faidx_has_seq(p->fai, dummy_region.chrom.c_str());
                if (res <= 0) {
                    return Err::CHROM_NOT_IN_REFERENCE;
                } else {
                    dummy_region.chromLen = faidx_seq_len(p->fai, dummy_region.chrom.c_str());
                    new_regions.push_back(dummy_region);
                    p->fetchRefSeq(new_regions.back());
                }
            } catch (...) {
                out << termcolor::red << "Error parsing :add" << termcolor::reset;
                return Err::PARSE_INPUT;
            }
        }
        // Append new region to tiled view
        if (p->mode == Manager::Show::TILED) {
            p->regions.clear();
            if (p->currentVarTrack->blockStart < (int)p->currentVarTrack->multiRegions.size()) {
                assert (!p->currentVarTrack->multiRegions[p->currentVarTrack->blockStart].empty());
                if (!p->currentVarTrack->multiRegions[p->currentVarTrack->blockStart][0].chrom.empty()) {
                    p->regions = p->currentVarTrack->multiRegions[p->currentVarTrack->blockStart];
                    p->fetchRefSeqs();
                }
            }
        }
        p->regions.insert(p->regions.end(), new_regions.begin(), new_regions.end());
        if (p->frameId >= 0) {
            p->processed = false;
            for (auto &cl: p->collections) {
                cl.resetDrawState();
            }
            p->imageCache.clear();
            p->imageCacheQueue.clear();
        }
        if (p->mode != Manager::Show::SINGLE) {
            p->mode = Manager::Show::SINGLE;
            refreshGw(p);
        }
        return Err::NONE;
    }

    Err snapshot(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        p->redraw = false;
        if (parts.size() > 2) {
            return Err::TOO_MANY_OPTIONS;
        }
        std::string fname;
        p->currentVarTrack = &p->variantTracks[p->variantFileSelection];
        if (parts.size() == 1) {  // Choose filename automatically based on region
            if (p->mode == Manager::Show::SINGLE) {
                std::filesystem::path fname_path = Utils::makeFilenameFromRegions(p->regions);
#if defined(_WIN32) || defined(_WIN64)
                const wchar_t* pc = fname_path.filename().c_str();
                std::wstring ws(pc);
                std::string p(ws.begin(), ws.end());
                fname = p;
#else
                fname = fname_path.filename();
#endif
            } else if (p->currentVarTrack != nullptr) {
                fname = "index_" + std::to_string(p->currentVarTrack->blockStart) + "_" +
                        std::to_string(p->opts.number.x * p->opts.number.y) + ".png";
            }
        } else {
            std::string nameFormat = parts[1];
            // try and parse information from vcf record e.g. {pos}.png
            if (p->currentVarTrack != nullptr && p->currentVarTrack->type == HGW::TrackType::VCF && p->mode == Manager::Show::SINGLE) {
                if (p->mouseOverTileIndex == -1 || p->currentVarTrack->blockStart + p->mouseOverTileIndex > (int) p->currentVarTrack->multiLabels.size()) {
                    return Err::SILENT;
                }
                Utils::Label &lbl = p->currentVarTrack->multiLabels[p->currentVarTrack->blockStart + p->mouseOverTileIndex];
                p->currentVarTrack->vcf.get_samples();
                std::vector<std::string> sample_names_copy = p->currentVarTrack->vcf.sample_names;
                p->currentVarTrack->vcf.printTargetRecord(lbl.variantId, lbl.chrom, lbl.pos);
                std::string variantStringCopy = p->currentVarTrack->vcf.variantString;
                if (!variantStringCopy.empty() && variantStringCopy.back() == '\n') {
                    variantStringCopy.erase(variantStringCopy.length()-1);
                }
                std::vector<std::string> vcfCols = Utils::split(variantStringCopy, '\t');
                try {
                    Parse::parse_output_name_format(nameFormat, vcfCols, sample_names_copy, p->bam_paths, lbl.current(), out);
                } catch (...) {
                    return Err::PARSE_INPUT;
                }
            }
            fname = nameFormat;
            Utils::trim(fname);
        }
        std::filesystem::path outdir = p->opts.outdir;
        std::filesystem::path fname_path(fname);
        std::filesystem::path out_path = outdir / fname_path;
        if (!std::filesystem::exists(out_path.parent_path()) && !out_path.parent_path().empty()) {
            out << termcolor::red << "Error:" << termcolor::reset << " path not found " << out_path.parent_path() << std::endl;
            return Err::INVALID_PATH;
        } else {
            if (out_path.extension() == ".png") {
                if (!p->imageCacheQueue.empty()) {
                    Manager::imagePngToFile(p->imageCacheQueue.back().second, out_path.string());
                    Term::clearLine(out);
                    out << "\rSaved to " << out_path << std::endl;
                } else {
                    out << termcolor::red << "Error:" << termcolor::reset << " no image in image queue " << std::endl;
                }
            } else if (out_path.extension() == ".pdf" || out_path.extension() == ".svg") {
                std::string format_str = (out_path.extension() == ".pdf") ? "pdf" : "svg";

#if defined(_WIN32) || defined(_WIN64)
                const wchar_t* outp = out_path.c_str();
                std::wstring pw(outp);
                std::string outp_str(pw.begin(), pw.end());
                SkFILEWStream out_stream(outp_str.c_str());
#else
                SkFILEWStream out_stream(out_path.c_str());
#endif
                SkDynamicMemoryWStream buffer;

                for (auto &cl: p->collections) {
                    cl.resetDrawState();
                }
                if (format_str == "pdf") {
                    if (p->mode == Manager::TILED) {
                        out << termcolor::red << "Error:" << termcolor::reset << " file type not supported for tiled images, only .png is" << out_path.extension() << std::endl;
                        return Err::OPTION_NOT_SUPPORTED;
                    }
                    auto pdfDocument = SkPDF::MakeDocument(&buffer);
                    SkCanvas *pageCanvas = pdfDocument->beginPage(p->fb_width, p->fb_height);
                    p->runDrawOnCanvas(pageCanvas);
                    pdfDocument->close();
                    buffer.writeToStream(&out_stream);
                } else {
                    if (p->mode == Manager::TILED) {
                        out << termcolor::red << "Error:" << termcolor::reset << " file type not supported for tiled images, only .png is" << out_path.extension() << std::endl;
                        return Err::OPTION_NOT_SUPPORTED;
                    }
                    SkPictureRecorder recorder;
                    SkCanvas* canvas = recorder.beginRecording(SkRect::MakeWH(p->fb_width, p->fb_height));
                    p->runDrawOnCanvas(canvas);
                    sk_sp<SkPicture> picture = recorder.finishRecordingAsPicture();
                    std::unique_ptr<SkCanvas> svgCanvas = SkSVGCanvas::Make(SkRect::MakeWH(p->fb_width, p->fb_height), &out_stream);
                    if (svgCanvas) {
                        picture->playback(svgCanvas.get());
                    };
                }
                out << "\rSaved to " << out_path << std::endl;
            } else {
                out << termcolor::red << "Error:" << termcolor::reset << " file type not supported (Need one of .png|.pdf|.svg) " << out_path.extension() << std::endl;
                return Err::OPTION_NOT_SUPPORTED;
            }
        }
        return Err::NONE;
    }

    Err online(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        if (p->regions.empty()) {
            return Err::EMPTY_REGIONS;
        }
        std::string genome_tag;
        if (p->opts.genome_tag.empty() && parts.size() >= 2) {
            genome_tag = parts[1];
        } else {
            genome_tag = p->opts.genome_tag;
        }
        Term::printOnlineLinks(p->tracks, p->regions[p->regionSelection], genome_tag, out);
        return Err::NONE;
    }

    Err write_bam(Plot* p, std::string& o_str, std::vector< std::vector<size_t>> targets, std::ostream& out) {
        sam_hdr_t* hdr = p->headers[p->regionSelection];
        cram_fd* fc = nullptr;
        htsFile *h_out = nullptr;
        int res = 0;
        std::string full_path = Parse::tilde_to_home(o_str);
        const char* outf = full_path.c_str();
        std::string idx_path;
        int min_shift = 0;
        out << "Creating new file: " << outf << "\n";
        if (Utils::endsWith(o_str, ".sam")) {
            h_out = hts_open(outf, "w");
            res = sam_hdr_write(h_out, hdr);
            if (res < 0) {
                out << termcolor::red << "Error:" << termcolor::reset << " Failed to copy header\n";
                sam_close(h_out);
                return Err::SILENT;
            }
        } else if (Utils::endsWith(o_str, ".bam")) {
            h_out = hts_open(outf, "wb");
            res = hts_set_threads(h_out, p->opts.threads);
            res = sam_hdr_write(h_out, hdr);
            if (res < 0) {
                out << termcolor::red << "Error:" << termcolor::reset << " Failed to copy header\n";
                sam_close(h_out);
                return Err::SILENT;
            }
            idx_path = o_str + ".bai";
        } else {
            h_out = hts_open(outf, "wc");
            fc = h_out->fp.cram;
            cram_set_option(fc, CRAM_OPT_REFERENCE, p->reference.c_str());
            cram_set_option(fc, CRAM_OPT_NTHREADS, p->opts.threads);
            cram_fd_set_header(fc, hdr);
            res = sam_hdr_write(h_out, hdr);
            if (res < 0) {
                out << termcolor::red << "Error:" << termcolor::reset << " Failed to copy header\n";
                sam_close(h_out);
                return Err::SILENT;
            }
            idx_path = o_str + ".crai";
            min_shift = 14;
        }
        // start index
        if (!idx_path.empty()) {
            res = sam_idx_init(h_out, hdr, min_shift, idx_path.c_str());
            if (res < 0) {
                out << termcolor::red << "Error:" << termcolor::reset << " Failed to make index\n";
                idx_path.clear();
            }
        }

        // use region array from htlib
        std::vector<hts_itr_t *> region_iters;
        std::vector<htsFile *> file_ptrs;
        for (size_t j=0; j < p->bams.size(); ++j) {
            sam_hdr_t* hdr_ptr = p->headers[j];
            hts_idx_t* index = p->indexes[j];
            std::vector<std::string> region_names;
            for (size_t i=0; i < p->regions.size(); ++i) {
                const auto &r = p->regions[i];
                if (!targets.empty()) {
                    if (targets[j][i]) {
                        region_names.push_back(r.chrom + ":" + std::to_string(r.start) + "-" + std::to_string(r.end));
                    }
                } else {
                    region_names.push_back(r.chrom + ":" + std::to_string(r.start) + "-" + std::to_string(r.end));
                }
            }
            if (region_names.empty()) {
                continue;
            }
            std::vector<char*> c_regions(region_names.size());
            for (size_t i=0; i < region_names.size(); ++i) {
                c_regions[i] = const_cast<char*>(region_names[i].c_str());
            }
            hts_itr_t *iter = sam_itr_regarray(index, hdr_ptr, &c_regions[0], c_regions.size());
            region_iters.push_back(iter);
            file_ptrs.push_back(p->bams[j]);
        }

        struct qItem {
            Segs::Align align;
            htsFile* file_ptr;
            hts_itr_t* bam_iter;
            size_t from;
        };
        auto compare = [](qItem& a, qItem& b) -> bool {
            if (a.align.delegate->core.tid > b.align.delegate->core.tid) {
                return true;
            } else if (a.align.delegate->core.tid == b.align.delegate->core.tid) {
                return a.align.delegate->core.pos > b.align.delegate->core.pos;
            }
            return false;
        };
        // sort using priority queue
        std::priority_queue<qItem, std::vector<qItem>, decltype(compare)> pq(compare);
        for (size_t i=0; i < region_iters.size(); ++i) {
            bam1_t* a = bam_init1();
            if (sam_itr_next(file_ptrs[i], region_iters[i], a) >= 0) {
                Segs::Align alignment = Segs::Align(a);
                Segs::align_init(&alignment, 0, p->opts.soft_clip_threshold > 0);  // no need to parse mods/tags here
                pq.push({std::move(alignment), file_ptrs[i], region_iters[i], i});
            } else {
                bam_destroy1(a);
            }
        }
        // process queue and write to bam
        int count = 0;
        bool filter_reads = !p->filters.empty();
        while (!pq.empty()) {
            qItem item = pq.top();  // take copy
            if (filter_reads) {
                bool good = true;
                for (auto &f: p->filters) {
                    if (!f.eval(item.align, hdr, -1, -1)) {
                        good = false;
                        break;
                    }
                }
                if (good) {
                    res = sam_write1(h_out, hdr, item.align.delegate);
                    if (res < 0) {
                        out << termcolor::red << "Error:" << termcolor::reset << " Write alignment failed" << std::endl;
                        break;
                    }
                }
            } else {
                res = sam_write1(h_out, hdr, item.align.delegate);
                if (res < 0) {
                    out << termcolor::red << "Error:" << termcolor::reset << " Write alignment failed" << std::endl;
                    break;
                }
            }
            if (sam_itr_next(item.file_ptr, item.bam_iter, item.align.delegate) >= 0) {
                Segs::align_init(&item.align, 0, 1);
                pq.push(item);
            } else {
                bam_destroy1(item.align.delegate);
            }
            pq.pop();
            count += 1;
        }
        // clean up
        for (size_t i=0; i < region_iters.size(); ++i) {
            bam_itr_destroy(region_iters[i]);
        }
        if (!pq.empty()) {
            while (!pq.empty()) {
                bam_destroy1(pq.top().align.delegate);
                pq.pop();
            }
            hts_close(h_out);
            return Err::UNKNOWN;
        }
        if (!idx_path.empty()) {
            res = sam_idx_save(h_out);
            if (res < 0) {
                out << termcolor::red << "Error:" << termcolor::reset << " Write index failed" << std::endl;
            }
        }
        hts_close(h_out);
        out << count << " alignments written\n";
        return Err::NONE;
    }

    Err save_command(Plot* p, std::string& command, std::vector<std::string> parts, std::ostream& out) {
        if (parts.size() == 1) {
            return Err::OPTION_NOT_UNDERSTOOD;
        }
        auto as_alignments = [](std::string &s) -> bool { return (Utils::endsWith(s, ".sam") || Utils::endsWith(s, ".bam") || Utils::endsWith(s, ".cram")); };
        std::vector< std::vector<size_t>> targets;
        int res = Parse::parse_indexing(command, p->bams.size(), p->regions.size(), targets, out);
        if (res < 0) {
            return Err::SILENT;
        }
        if (res) {
            Utils::trim(command);
            parts = Utils::split(command, ' ');
        }
        if (as_alignments(parts.back())) {
            if (parts.size() == 2) {
                out << "Saved alignments: " << parts.back() << std::endl;
                write_bam(p, parts.back(), targets, out);
            } else if (parts.size() == 3) {
                if (parts[1] == ">") {
                    write_bam(p, parts.back(), targets, out);
                } else if (parts[1] == ">>") {
                    return Err::OPTION_NOT_SUPPORTED;
                } else {
                    return Err::OPTION_NOT_UNDERSTOOD;
                }
            } else {
                return Err::OPTION_NOT_UNDERSTOOD;
            }
        } else if (Utils::endsWith(command, ".png") || Utils::endsWith(command, ".pdf") || Utils::endsWith(command, ".svg")) {
            return snapshot(p, parts, out);
        } else if (Utils::endsWith(parts.back(), ".ini")) {
            out << "Saved session: " << parts.back() << std::endl;
            p->saveSession(parts.back());
        } else if (Utils::endsWith(parts.back(), ".tsv") || (Utils::endsWith(parts.back(), ".txt"))) {
            out << "Output label file set as: " << parts.back() << std::endl;
            p->setOutLabelFile(parts.back());
            p->saveLabels();
        } else {
            if (parts.back() == "labels") {
                out << "Current label path is: " << p->outLabelFile << std::endl;
                p->saveLabels();
                return Err::NONE;
            }
            return Err::INVALID_PATH;
        }
        return Err::NONE;
    }

    Err load_file(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        std::string filename;
        p->redraw = true;
        if (parts.empty()) {
            return Err::OPTION_NOT_UNDERSTOOD;
        }
        filename = Parse::tilde_to_home(parts.back());
        bool maybe_online = (Utils::startsWith(filename, "http") || Utils::startsWith(filename, "ftp"));
        if (parts.size() == 3) {
            std::string ext = std::filesystem::path(filename).extension().string();
            if (!maybe_online && std::filesystem::is_directory(filename)) {
                out << termcolor::red << "Error:" << termcolor::reset << " This is a folder path, not a file\n";
                return Err::SILENT;
            }
            if (parts[1] == "ideogram") {
                if (!maybe_online && !std::filesystem::exists(filename)) {
                    std::string g = p->opts.genome_tag;
                    p->opts.genome_tag = parts.back();
                    bool success = p->loadIdeogramTag();
                    p->opts.genome_tag = g;
                    if (success) {
                        refreshGw(p);
                        return Err::NONE;
                    } else {
                        return Err::INVALID_PATH;
                    }
                }
                if (ext != ".bed") {
                    out << termcolor::red << "Error:" << termcolor::reset << " Only .bed extension supported for ideograms\n";
                    return Err::SILENT;
                } else {
                    p->addIdeogram(filename);
                    refreshGw(p);
                    return Err::NONE;
                }
            } else if (parts[1] == "track") {
                if (!maybe_online && !std::filesystem::exists(filename)) {
                    return Err::INVALID_PATH;
                }
                if (ext == ".bam" || ext == ".cram") {
                    out << termcolor::red << "Error:" << termcolor::reset << " Bam/cram can not be loaded as a track\n";
                    return Err::SILENT;
                } else {
                    p->addTrack(filename, true, true, true);
                    refreshGw(p);
                    return Err::NONE;
                }

            } else if (parts[1] == "tiled") {
                if (!maybe_online && !std::filesystem::exists(filename)) {
                    return Err::INVALID_PATH;
                }
                if (ext == ".vcf" || ext == ".gz" || ext == ".bcf" || ext == ".bed") {
                    p->addTrack(filename, true, false, false);
                    refreshGw(p);
                    return Err::NONE;
                } else {
                    out << termcolor::red << "Error:" << termcolor::reset << " Image tiling only supported for .vcf|.vcf.gz|.bcf|.bed|.bed.gz file extensions\n";
                    return Err::SILENT;
                }
            } else if (parts[1] == "bam" || parts[1] == "cram") {
                if (!maybe_online && !std::filesystem::exists(filename)) {
                    return Err::INVALID_PATH;
                }
                p->addTrack(filename, true, p->opts.vcf_as_tracks, p->opts.bed_as_tracks);
                refreshGw(p);
                return Err::NONE;
            } else if (parts[1] == "genome") {
                p->loadGenome(filename, out);
                refreshGw(p);
                return Err::NONE;
            } else if (parts[1] == "labels") {
                if (!maybe_online && !std::filesystem::exists(filename)) {
                    return Err::INVALID_PATH;
                }
                p->seenLabels.clear();
                std::string img;
                std::vector<std::string> labels = Utils::split_keep_empty_str(p->opts.labels, ',');
                Utils::openLabels(parts.back(), img, p->inputLabels, labels, p->seenLabels);
                std::vector<std::string> current;
                std::vector<int> idx;
                for (auto &v : p->variantTracks) {
                    current.push_back(v.path);
                    idx.push_back(v.blockStart);
                }
                p->variantTracks.clear();

                int i = 0;
                for (auto &v : current) {
                    p->addVariantTrack(v, idx[i], false, false);  // dont cache, dont use full path as filename
                    i += 1;
                }
                refreshGw(p);
                return Err::NONE;
            } else if (parts[1] == "session") {
                if (maybe_online || std::filesystem::exists(filename)) {
                    p->opts.session_file = filename;
                    mINI::INIFile file(p->opts.session_file);
                    file.read(p->opts.seshIni);
                    if (!p->opts.seshIni.has("data") || !p->opts.seshIni.has("show")) {
                        out << "Error: session file is missing 'data' or 'show' headings. Invalid session file\n";
                        return Err::SILENT;
                    }
                    p->opts.getOptionsFromSessionIni(p->opts.seshIni);
                    p->opts.theme.setAlphas();
                    p->loadSession();
                    p->fetchRefSeqs();
                    refreshGw(p);
                } else {
                    return Err::INVALID_PATH;
                }
            } else {
                return Err::OPTION_NOT_UNDERSTOOD;
            }
        } else if (parts.size() != 2) {
            return Err::OPTION_NOT_UNDERSTOOD;
        }
        if (!maybe_online && !std::filesystem::exists(parts.back())) {
            if (parts.back() == "labels") {
                out << "Current label path is: " << p->outLabelFile << std::endl;
                refreshGw(p);
                return Err::NONE;
            } else if (parts.back() == "genome") {
                out << "Current genome path is: " << p->reference << std::endl;
                refreshGw(p);
                return Err::NONE;
            } else if (parts.back() == "ideogram") {
                out << "Current ideogram path is: " << p->ideogram_path << std::endl;
                refreshGw(p);
                return Err::NONE;
            } else if (parts.back() == "session") {
                out << "Current session path is: " << p->opts.session_file << std::endl;
                refreshGw(p);
                return Err::NONE;
            } else {
                p->redraw = true;
                return Err::INVALID_PATH;
            }
        }
        p->addTrack(filename, true, p->opts.vcf_as_tracks, p->opts.bed_as_tracks);
        refreshGw(p);
        return Err::NONE;
    }

    Err search_for_feature(Plot* p, std::string& command, Utils::Region& rgn) {
        p->redraw = true;
        if (p->tracks.empty()) {
            return Err::FEATURE_NOT_IN_TRACKS;
        }
        bool res = HGW::searchTracks(p->tracks, command, rgn);
        if (!res) {
            return Err::FEATURE_NOT_IN_TRACKS;
        }
        if (p->mode != Manager::Show::SINGLE) { p->mode = Manager::Show::SINGLE; }
        if (p->regions.empty()) {
            p->regions.push_back(rgn);
            p->fetchRefSeq(p->regions.back());
        } else if (p->regionSelection < (int)p->regions.size()) {
            p->regions[p->regionSelection] = rgn;
            p->fetchRefSeq(p->regions[p->regionSelection]);
        }
        return Err::NONE;
    }

    Err infer_region_or_feature(Plot* p, std::string& command, std::vector<std::string> parts, std::ostream& out) {
        Utils::Region rgn;
        Err reason = Err::NONE;
        try {
            rgn = Utils::parseRegion(command);
        } catch (...) {
            p->redraw = true;
            reason = Err::BAD_REGION;
        }
        if (reason == Err::NONE) {
            int res = faidx_has_seq(p->fai, rgn.chrom.c_str());
            if (res <= 0) {
                reason = search_for_feature(p, command, rgn);
                if (reason == Err::FEATURE_NOT_IN_TRACKS) {
                    p->redraw = true;
                    int num_sequences = faidx_nseq(p->fai);
                    out << "Chromosome names in the fasta index:" << std::endl;
                    for (int i = 0; i < num_sequences; ++i) {
                        const char* seq_name = faidx_iseq(p->fai, i);
                        out << seq_name;
                        if (i < num_sequences - 1) {
                            out << ", ";
                        }
                        if (i > 25) {
                            out << " ... ";
                            break;
                        }
                    }
                    out << std::endl << std::endl;
                    return Err::OPTION_NOT_UNDERSTOOD;
                }
            }
            if (p->mode != Manager::Show::SINGLE) { p->mode = Manager::Show::SINGLE; }
            if (p->regions.empty()) {
                p->regions.push_back(rgn);
                p->fetchRefSeq(p->regions.back());
                p->regions.back().chromLen = faidx_seq_len(p->fai, p->regions.back().chrom.c_str());
            } else {
                Utils::Region &old = p->regions[p->regionSelection];
                if (old.chrom == rgn.chrom) {
                    rgn.markerPos = old.markerPos;
                    rgn.markerPosEnd = old.markerPosEnd;
                    rgn.sortOption = old.sortOption;
                    rgn.sortPos = old.sortPos;
                    rgn.refBaseAtPos = old.refBaseAtPos;
                }
                p->regions[p->regionSelection] = rgn;
                p->fetchRefSeq(p->regions[p->regionSelection]);
                p->regions[p->regionSelection].chromLen = faidx_seq_len(p->fai, p->regions[p->regionSelection].chrom.c_str());
            }
        } else {  // search all tracks for matching name, slow but ok for small tracks
            reason = search_for_feature(p, command, rgn);
        }
        if (reason == Err::NONE && p->frameId >= 0) {
            p->processed = false;
            p->imageCache.clear();
            p->redraw = true;
            p->imageCacheQueue.clear();
        }
        return reason;
    }

    Err update_colour(Plot* p, std::string& command, std::vector<std::string> parts, std::ostream& out) {
        p->redraw = true;
        if (parts.size() != 6) {
            return Err::OPTION_NOT_UNDERSTOOD;
        }
        int alpha, red, green, blue;
        try {
            alpha = std::stoi(parts[2]);
            red = std::stoi(parts[3]);
            green = std::stoi(parts[4]);
            blue = std::stoi(parts[5]);
        } catch (...) {
            return Err::OPTION_NOT_UNDERSTOOD;
        }
        refreshGw(p);
        Themes::GwPaint e{};
        std::string &c = parts[1];
        if (c == "bgPaint") { e = Themes::GwPaint::bgPaint; }
        else if (c == "bgMenu") { e = Themes::GwPaint::bgMenu; }
        else if (c == "fcNormal") { e = Themes::GwPaint::fcNormal; }
        else if (c == "fcDel") { e = Themes::GwPaint::fcDel; }
        else if (c == "fcDup") { e = Themes::GwPaint::fcDup; }
        else if (c == "fcInvF") { e = Themes::GwPaint::fcInvF; }
        else if (c == "fcInvR") { e = Themes::GwPaint::fcInvR; }
        else if (c == "fcT ra") { e = Themes::GwPaint::fcTra; }
        else if (c == "fcIns") { e = Themes::GwPaint::fcIns; }
        else if (c == "fcSoftClip") { e = Themes::GwPaint::fcSoftClip; }
        else if (c == "fcA") { e = Themes::GwPaint::fcA; }
        else if (c == "fcT") { e = Themes::GwPaint::fcT; }
        else if (c == "fcC") { e = Themes::GwPaint::fcC; }
        else if (c == "fcG") { e = Themes::GwPaint::fcG; }
        else if (c == "fcN") { e = Themes::GwPaint::fcN; }
        else if (c == "fcCoverage") { e = Themes::GwPaint::fcCoverage; }
        else if (c == "fcTrack") { e = Themes::GwPaint::fcTrack; }
        else if (c == "fcNormal0") { e = Themes::GwPaint::fcNormal0; }
        else if (c == "fcDel0") { e = Themes::GwPaint::fcDel0; }
        else if (c == "fcDup0") { e = Themes::GwPaint::fcDup0; }
        else if (c == "fcInvF0") { e = Themes::GwPaint::fcInvF0; }
        else if (c == "fcInvR0") { e = Themes::GwPaint::fcInvR0; }
        else if (c == "fcTra0") { e = Themes::GwPaint::fcTra0; }
        else if (c == "fcSoftClip0") { e = Themes::GwPaint::fcSoftClip0; }
        else if (c == "fcBigWig") { e = Themes::GwPaint::fcBigWig; }
        else if (c == "mate_fc") { e = Themes::GwPaint::mate_fc; }
        else if (c == "mate_fc0") { e = Themes::GwPaint::mate_fc0; }
        else if (c == "ecMateUnmapped") { e = Themes::GwPaint::ecMateUnmapped; }
        else if (c == "ecSplit") { e = Themes::GwPaint::ecSplit; }
        else if (c == "ecSelected") { e = Themes::GwPaint::ecSelected; }
        else if (c == "lcJoins") { e = Themes::GwPaint::lcJoins; }
        else if (c == "lcCoverage") { e = Themes::GwPaint::lcCoverage; }
        else if (c == "lcLightJoins") { e = Themes::GwPaint::lcLightJoins; }
        else if (c == "lcGTFJoins") { e = Themes::GwPaint::lcGTFJoins; }
        else if (c == "lcLabel") { e = Themes::GwPaint::lcLabel; }
        else if (c == "lcBright") { e = Themes::GwPaint::lcBright; }
        else if (c == "tcDel") { e = Themes::GwPaint::tcDel; }
        else if (c == "tcIns") { e = Themes::GwPaint::tcIns; }
        else if (c == "tcLabels") { e = Themes::GwPaint::tcLabels; }
        else if (c == "tcBackground") { e = Themes::GwPaint::tcBackground; }
        else if (c == "fcMarkers") { e = Themes::GwPaint::fcMarkers; }
        else if (c == "fcRoi") { e = Themes::GwPaint::fcRoi; }
        else if (c =="fc5mc") {
            e = Themes::GwPaint::fc5mc;
            alpha = 63;
            for (size_t i=0; i < 4; ++i) {
                p->opts.theme.ModPaints[0][i].setARGB(alpha, red, green, blue);
                alpha += 64;
            }
        }
        else if (c == "fc5hmc") {
            e = Themes::GwPaint::fc5hmc;
            alpha = 63;
            for (size_t i=0; i < 4; ++i) {
                p->opts.theme.ModPaints[1][i].setARGB(alpha, red, green, blue);
                alpha += 64;
            }
        }
        else if (c == "fcOther") {
            e = Themes::GwPaint::fcOther;
            alpha = 63;
            for (size_t i=0; i < 4; ++i) {
                p->opts.theme.ModPaints[2][i].setARGB(alpha, red, green, blue);
                alpha += 64;
            }
        }
        else if (Utils::startsWith(c, "track") && c != "track") {
            c.erase(0, 5);
            int ind = 0;
            try {
                ind = std::stoi(c);
            } catch (...) {
                out << termcolor::red << "Error:" << termcolor::reset << " track index not understood\n";
                return Err::SILENT;
            }
            if (ind > (int)p->tracks.size()) {
                out << termcolor::red << "Error:" << termcolor::reset << " track index out of range\n";
                return Err::SILENT;
            }
            SkPaint new_paint;
            new_paint.setARGB(alpha, red, green, blue);
            p->tracks[ind].setPaint(new_paint);
            refreshGw(p);
            return Err::NONE;
        }
        else {
            return Err::OPTION_NOT_UNDERSTOOD;
        }
        p->opts.theme.setPaintARGB(e, alpha, red, green, blue);

        return Err::NONE;
    }

    Err add_roi(Plot* p, std::string& command, std::vector<std::string> parts, std::ostream& out) {
        p->redraw = true;
        if (p->regions.empty()) {
            return Err::SILENT;
        }
        std::string com = command;
        Utils::TrackBlock b;
        b.line = command;
        b.strand = 0;
        Utils::Region& rgn = p->regions[p->regionSelection];
        if (command == "roi") { // use active region as roi
            b.chrom = rgn.chrom;
            b.start = rgn.start;
            b.end = rgn.end;
            b.name = rgn.toString();
        } else {
            bool good = false;
            int s_idx = 4;
            try {
                Utils::Region r = Utils::parseRegion(parts[1]);
                if (r.chrom == rgn.chrom) {
                    b.chrom = r.chrom;
                    b.start = r.start;
                    b.end = r.end;
                    good = true;
                    s_idx += (int)parts[1].size();
                }
            } catch (...) {
            }
            if (!good) {
                b.chrom = rgn.chrom;
                b.start = rgn.start;
                b.end = rgn.end;
            }
            com.erase(0, s_idx);
            b.name = com;
        }
        bool added = false;
        for (auto& t : p->tracks) {
            if (t.kind == HGW::FType::ROI) {
                t.allBlocks[b.chrom].add(b.start, b.end, b);
                t.allBlocks[b.chrom].index();
                added = true;
                break;
            }
        }
        if (!added) {
            p->tracks.emplace_back() = HGW::GwTrack();
            p->tracks.back().kind = HGW::FType::ROI;
            p->tracks.back().add_to_dict = true;
            p->tracks.back().allBlocks[b.chrom].add(b.start, b.end, b);
            p->tracks.back().allBlocks[b.chrom].index();
            p->tracks.back().faceColour = p->opts.theme.fcRoi;
        }
        refreshGw(p);
        return Err::NONE;
    }

    Err sort_command(Plot* p, std::string& command, std::vector<std::string> parts, std::ostream& out) {
        if (parts.size() == 1 || (parts.size() == 2 && parts[1] == "none")) {
            refreshGw(p);
            return Err::NONE;
        }
        if (parts.size() > 3) {
            return Err::TOO_MANY_OPTIONS;
        }
        int pos;
        try {
            std::string n = parts.back();
            n.erase(std::remove(n.begin(), n.end(), ','), n.end());
            pos = std::stoi(n);
        } catch (...) {
            pos = -1;
        }
        if (parts[1] != "hap" && parts[1] != "strand" && pos == -1) {
            return Err::OPTION_NOT_SUPPORTED;
        }
        if (pos != -1) {
            p->regions[p->regionSelection].sortPos = pos;
            p->regions[p->regionSelection].setRefBaseAtPos();
        }
        if (parts[1] == "strand") {
            if (pos == -1) {
                p->regions[p->regionSelection].sortOption = Utils::SortType::STRAND;
            } else {
                p->regions[p->regionSelection].sortOption = Utils::SortType::STRAND_AND_POS;
            }
        } else if (parts[1] == "hap") {
            if (pos == -1) {
                p->regions[p->regionSelection].sortOption = Utils::SortType::HP;
            } else {
                p->regions[p->regionSelection].sortOption = Utils::SortType::HP_AND_POS;
            }
        } else {
            switch (p->regions[p->regionSelection].sortOption) {
                case (Utils::SortType::HP):
                    p->regions[p->regionSelection].sortOption = Utils::SortType::HP_AND_POS;
                case (Utils::SortType::STRAND):
                    p->regions[p->regionSelection].sortOption = Utils::SortType::STRAND_AND_POS;
                default:
                    p->regions[p->regionSelection].sortOption = Utils::SortType::POS;
            }
            if (p->regions[p->regionSelection].sortOption)
            p->regions[p->regionSelection].sortOption = Utils::SortType::POS;
        }
        p->redraw = true;
        p->processed = false;
        p->imageCache.clear();
        p->imageCacheQueue.clear();
        p->filters.clear();
        p->target_qname = "";
        for (auto &cl: p->collections) { cl.vScroll = 0; cl.resetDrawState(); }
        return Err::NONE;
    }

    Err header_command(Plot* p, std::string& command, std::vector<std::string> parts, std::ostream& out) {
        p->redraw = true;
        if (p->headers.empty() || p->regionSelection >= (int)p->headers.size()) {
            return Err::SILENT;
        }
        sam_hdr_t *hdr = p->headers[p->regionSelection];
        const char* header_text = sam_hdr_str(hdr);
        std::string header_string(header_text);
        if (parts.size() == 1) {
            out << header_string << std::endl;
        } else if (parts.size() > 3) {
            return Err::OPTION_NOT_UNDERSTOOD;
        } else if (parts.back() != "names") {
            return Err::OPTION_NOT_SUPPORTED;
        }
        std::istringstream header_stream(header_string);
        std::string line;
        while (std::getline(header_stream, line)) {
            if (line.rfind("@SQ", 0) == 0) {
                std::vector<std::string> sq_lines = Utils::split(line, '\t');
                int i = 0;
                for (auto & t: sq_lines) {
                    out << t << "\t";
                    if (i == 2) {
                        break;
                    } ++i;
                } out << "\n";
            }
        }
        return Err::NONE;
    }

    void cache_command_or_handle_err(Plot* p, Err result, std::ostream& out,
                                    std::vector<std::string>* applied, std::string& command) {
        if (result != NONE) {
            p->redraw = false;
        }
        switch (result) {
            case NONE:
                applied->push_back(command);
                return;
            case UNKNOWN:
                out << termcolor::red << "Error:" << termcolor::reset << " Unknown error\n";
                return;
            case SILENT:
                return;
            case TOO_MANY_OPTIONS:
                out << termcolor::red << "Error:" << termcolor::reset << " Too many options supplied\n";
                return;
            case CHROM_NOT_IN_REFERENCE:
                out << termcolor::red << "Error:" << termcolor::reset << " chromosome not in reference\n";
                return;
            case FEATURE_NOT_IN_TRACKS:
                out << termcolor::red << "Error:" << termcolor::reset << " Feature not in tracks\n";
                return;
            case BAD_REGION:
                out << termcolor::red << "Error:" << termcolor::reset << " Region not understood\n";
                return;
            case OPTION_NOT_SUPPORTED:
                out << termcolor::red << "Error:" << termcolor::reset << " Option not supported\n";
                return;
            case OPTION_NOT_UNDERSTOOD:
                out << termcolor::red << "Error:" << termcolor::reset << " Option not understood\n";
                return;
            case INVALID_PATH:
                out << termcolor::red << "Error:" << termcolor::reset << " Path was invalid\n";
                return;
            case EMPTY_TRACKS:
                out << termcolor::red << "Error:" << termcolor::reset << " tracks are empty (add a track first)\n";
                return;
            case EMPTY_BAMS:
                out << termcolor::red << "Error:" << termcolor::reset << " Bams are empty (add a bam first)\n";
                return;
            case EMPTY_REGIONS:
                out << termcolor::red << "Error:" << termcolor::reset << " Regions are empty (add a region first)\n";
                return;
            case EMPTY_VARIANTS:
                out << termcolor::red << "Error:" << termcolor::reset << " No variant file (add a variant file first)\n";
                return;
            case PARSE_VCF:
                out << termcolor::red << "Error:" << termcolor::reset << " Vcf parsing error\n";
                return;
            case PARSE_INPUT:
                out << termcolor::red << "Error:" << termcolor::reset << " Input could not be parsed\n";
                return;
        }
    }

    // Command functions capture these parameters only
    #define PARAMS [](Commands::Plot* p, std::string& command, std::vector<std::string>& parts, std::ostream& out) -> Err

    // Note the function map will be cached after first call. plt is bound, but parts are updated with each call
    void run_command_map(Plot* p, std::string& command, std::ostream& out) {
        if (Utils::startsWith(command, "'") || Utils::startsWith(command, "\"")) {
            command.erase(command.begin(), command.begin() + 1);
            out << command << std::endl;
            if (!p->variantTracks.empty() && p->currentVarTrack != nullptr) {
                int i = p->mouseOverTileIndex + p->currentVarTrack->blockStart;
                if (i < (int)p->currentVarTrack->multiLabels.size()) {
                    p->currentVarTrack->multiLabels[i].comment = command;
                    p->redraw = true;
                    p->processed = false;
                    for (auto &cl : p->collections) {
                        cl.resetDrawState();
                    };
                }
            }
            command = "";
            return;
        }
        std::vector<std::string> parts = Utils::split(command, ' ');
        static std::unordered_map<std::string,
                                  std::function<Err(Plot*, std::string&, std::vector<std::string>&, std::ostream& out)>> functionMap = {

                {":",        PARAMS { return noOp(p); }},
                {"/",        PARAMS { return noOp(p); }},
                {"q",        PARAMS { return triggerClose(p); }},
                {"quit",     PARAMS { return triggerClose(p); }},
                {"r",        PARAMS { return refreshGw(p); }},
                {"refresh",  PARAMS { return refreshGw(p); }},
                {"line",     PARAMS { return line(p); }},
                {"settings", PARAMS { return settings(p, "settings"); }},
                {"general", PARAMS { return settings(p, "general"); }},
                {"genomes", PARAMS { return settings(p, "genomes"); }},
                {"interaction", PARAMS { return settings(p, "interaction"); }},
                {"labelling", PARAMS { return settings(p, "labelling"); }},
                {"navigation", PARAMS { return settings(p, "navigation"); }},
                {"tracks", PARAMS { return settings(p, "tracks"); }},
                {"thresholds", PARAMS { return settings(p, "thresholds"); }},
                {"keymap", PARAMS { return settings(p, "keymap"); }},
                {"ins",      PARAMS { return insertions(p); }},
                {"insertions",  PARAMS { return insertions(p); }},
                {"mm",       PARAMS { return mismatches(p); }},
                {"mismatches",  PARAMS { return mismatches(p); }},
                {"mods",     PARAMS { return mods(p); }},
                {"edges",    PARAMS { return edges(p); }},
                {"soft-clips",  PARAMS { return soft_clips(p); }},
                {"log2-cov",  PARAMS { return log2_cov(p); }},
                {"expand-tracks", PARAMS { return expand_tracks(p); }},
                {"tlen-y",   PARAMS { return tlen_y(p); }},
                {"alignments",   PARAMS { return alignments(p); }},
                {"labels",   PARAMS { return labels(p); }},

                {"sam",      PARAMS { return sam(p, command, parts, out); }},
                {"h",        PARAMS { return getHelp(p, command, parts, out); }},
                {"help",     PARAMS { return getHelp(p, command, parts, out); }},
                {"man",      PARAMS { return getHelp(p, command, parts, out); }},
                {"link",     PARAMS { return link(p, command, parts); }},
                {"v",        PARAMS { return var_info(p, command, parts, out); }},
                {"var",      PARAMS { return var_info(p, command, parts, out); }},
                {"save",     PARAMS { return save_command(p, command, parts, out); }},
                {"colour",   PARAMS { return update_colour(p, command, parts, out); }},
                {"color",    PARAMS { return update_colour(p, command, parts, out); }},
                {"roi",      PARAMS { return add_roi(p, command, parts, out); }},
                {"sort",    PARAMS { return sort_command(p, command, parts, out); }},
                {"header",    PARAMS { return header_command(p, command, parts, out); }},

                {"count",    PARAMS { return count(p, command, out); }},
                {"filter",   PARAMS { return addFilter(p, command, out); }},
                {"tags",     PARAMS { return tags(p, command, out); }},
                {"mate",     PARAMS { return mate(p, command, out); }},

                {"f",        PARAMS { return findRead(p, parts, out); }},
                {"find",     PARAMS { return findRead(p, parts, out); }},
                {"ylim",     PARAMS { return setYlim(p, parts, out); }},
                {"indel-length", PARAMS { return indelLength(p, parts, out); }},
                {"rm",       PARAMS { return remove(p, parts, out); }},
                {"remove",   PARAMS { return remove(p, parts, out); }},
                {"cov",      PARAMS { return cov(p, parts, out); }},
                {"theme",    PARAMS { return theme(p, parts, out); }},
                {"goto",     PARAMS { return goto_command(p, parts); }},
                {"add",      PARAMS { return add_region(p, parts, out); }},
                {"s",        PARAMS { return snapshot(p, parts, out); }},
                {"snapshot", PARAMS { return snapshot(p, parts, out); }},
                {"online",   PARAMS { return online(p, parts, out); }},
                {"load",     PARAMS { return load_file(p, parts, out); }},
                {"grid",     PARAMS { return grid(p, parts); }},

        };

        auto it = functionMap.find(parts[0]);
        Err res;
        if (it != functionMap.end()) {
            res = it->second(p, command, parts, out);  // Execute the mapped function
        } else {
            res = infer_region_or_feature(p, command, parts, out);
        }
        cache_command_or_handle_err(p, res, out, &p->commandsApplied, command);
        p->inputText = "";

    }
}