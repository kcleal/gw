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
#include <htslib/bgzf.h>
#include <htslib/hfile.h>
#include <htslib/cram.h>
#include <GLFW/glfw3.h>
#include <hts_funcs.h>
#include <filesystem>
#include <GLFW/glfw3.h>
#include <iostream>

#if defined(_WIN32)

#else
    #include <pwd.h>
#endif

#include "parser.h"
#include "plot_manager.h"
#include "term_out.h"
#include "themes.h"


namespace Commands {

    enum Err {
        NONE,
        UNKNOWN,
        SILENT,

        TOO_MANY_OPTIONS,
        CHROM_NOT_IN_REFERENCE,
        FEATURE_NOT_IN_TRACKS,
        BAD_REGION,
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
        p->imageCache.clear();
        p->filters.clear();
        p->target_qname = "";
        for (auto &cl: p->collections) { cl.vScroll = 0; }
        return Err::NONE;
    }

    Err line(Plot* p) {
        p->drawLine = !p->drawLine;
        p->redraw = true;
        p->processed = true;
        return Err::NONE;
    }

    Err settings(Plot* p) {
        p->last_mode = p->mode;
        p->mode = Manager::Show::SETTINGS;
        p->redraw = true;
        p->processed = true;
        return Err::NONE;
    }

    std::string tilde_to_home(std::string fpath) {
        if (fpath[0] != '~') {
            return fpath;
        }
        fpath.erase(0, 2);
#if defined(_WIN32) || defined(_WIN64)
        const char *homedrive_c = std::getenv("HOMEDRIVE");
        const char *homepath_c = std::getenv("HOMEPATH");
        std::string homedrive(homedrive_c ? homedrive_c : "");
        std::string homepath(homepath_c ? homepath_c : "");
        std::string home = homedrive + homepath;
#else
        struct passwd *pw = getpwuid(getuid());
        std::string home(pw->pw_dir);
#endif
        std::filesystem::path path;
        std::filesystem::path homedir(home);
        std::filesystem::path inputPath(fpath);
        path = homedir / inputPath;
        return path;
    }

    Err sam(Plot* p, std::string& command, std::vector<std::string>& parts, std::ostream& out) {
        if (!p->selectedAlign.empty()) {
            if (command == "sam") {
                Term::printSelectedSam(p->selectedAlign, out);
            } else if (parts.size() == 3 && (Utils::endsWith(parts[2], ".sam") || Utils::endsWith(parts[2], ".bam") || Utils::endsWith(parts[2], ".cram"))) {
                std::string o_str = parts[2];
                sam_hdr_t* hdr = p->headers[p->regionSelection];
                cram_fd* fc = nullptr;
                htsFile *h_out = nullptr;
                bool write_cram = false;
                int res;
                std::string full_path = tilde_to_home(parts[2]);
                const char* outf = full_path.c_str();
                if (parts[1] == ">") {
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
        p->redraw = false;
        p->processed = true;
        return Err::NONE;
    }

    Err insertions(Plot* p) {
        p->opts.small_indel_threshold = (p->opts.small_indel_threshold == 0) ? std::stoi(p->opts.myIni["view_thresholds"]["small_indel"]) : 0;
        if (p->mode == Manager::Show::SINGLE) {
            p->processed = true;
        } else {
            p->processed = false;
        }
        p->redraw = true;
        p->imageCache.clear();
        return Err::NONE;
    }

    Err mismatches(Plot* p) {
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
        return Err::NONE;
    }

    Err edges(Plot* p) {
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
        return Err::NONE;
    }

    Err soft_clips(Plot* p) {
        p->opts.soft_clip_threshold = (p->opts.soft_clip_threshold == 0) ? std::stoi(p->opts.myIni["view_thresholds"]["soft_clip"]) : 0;
        if (p->mode == Manager::Show::SINGLE) {
            p->processed = true;
        } else {
            p->processed = false;
        }
        p->redraw = true;
        p->imageCache.clear();
        return Err::NONE;
    }

    Err log2_cov(Plot* p) {
        p->opts.log2_cov = !(p->opts.log2_cov);
        p->redraw = true;
        if (p->mode == Manager::Show::SINGLE) {
            p->processed = true;
            for (auto &cl : p->collections) {
                cl.skipDrawingReads = false;
                cl.skipDrawingCoverage = false;
            }
        } else {
            p->processed = false;
        }
        p->imageCache.clear();
        return Err::NONE;
    }

    Err expand_tracks(Plot* p) {
        p->opts.expand_tracks = !(p->opts.expand_tracks);
        p->redraw = true;
        if (p->mode == Manager::Show::SINGLE) {
            p->processed = true;
        } else {
            p->processed = false;
        }
        p->imageCache.clear();
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
        return Err::NONE;
    }

    Err link(Plot* p, std::string& command, std::vector<std::string>& parts) {
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
        return Err::NONE;
    }

    Err var_info(Plot* p, std::string& command, std::vector<std::string>& parts, std::ostream& out) {
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
                            Parse::parse_vcf_split(result, vcfCols, parts[i], sample_names_copy);
                        } catch (...) {
                            out << termcolor::red << "Error:" << termcolor::reset << " could not parse " << parts[i] << std::endl;
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
        std::string str = command;
        str.erase(0, 6);
        Parse::countExpression(p->collections, str, p->headers, p->bam_paths, (int)p->bams.size(), (int)p->regions.size(), out);
        p->redraw = false;
        p->processed = true;
        return Err::NONE;
    }

    Err addFilter(Plot* p, std::string& command, std::ostream& out) {
        std::string str = command;
        str.erase(0, 7);
        if (str.empty()) {
            return Err::NONE;
        }
        for (auto &s: Utils::split(str, ';')) {
            Parse::Parser ps = Parse::Parser();
            int rr = ps.set_filter(s, (int)p->bams.size(), (int)p->regions.size());
            if (rr > 0) {
                p->filters.push_back(ps);
                out << command << std::endl;
            }
        }
        p->imageCache.clear();
        p->redraw = true;
        p->processed = false;
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
        p->redraw = false;
        p->processed = true;
        return Err::NONE;
    }

    Err mate(Plot* p, std::string& command, std::ostream& out) {
        std::string mate;
        Utils::parseMateLocation(p->selectedAlign, mate, p->target_qname);
        if (mate.empty()) {
            out << termcolor::red << "Error:" << termcolor::reset << " could not parse mate location\n";
            return Err::NONE;
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
            } else if (command == "mate add" && p->mode == Manager::Show::SINGLE) {
                p->regions.push_back(Utils::parseRegion(mate));
                p->fetchRefSeq(p->regions.back());
                p->processed = false;
                p->processBam();
                p->highlightQname();
                p->redraw = true;
                p->processed = true;
                p->imageCache.clear();
            }
        }
        return Err::NONE;
    }

    Err findRead(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        if (!p->target_qname.empty() && parts.size() == 1) {
            return Err::NONE;
        } else if (parts.size() == 2) {
            p->target_qname = parts.back();
        } else {
            out << termcolor::red << "Error:" << termcolor::reset << " please provide one qname\n";
            return Err::NONE;
        }
        p->highlightQname();
        p->redraw = true;
        p->processed = true;
        p->imageCache.clear();
        return Err::NONE;
    }

    Err setYlim(Plot* p, std::vector<std::string> parts, std::ostream& out) {
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
            out << termcolor::red << "Error:" << termcolor::reset << " ylim invalid value\n";
            return Err::NONE;
        }
        p->imageCache.clear();
        HGW::refreshLinked(p->collections, p->opts, &p->samMaxY);
        p->processed = true;
        p->redraw = true;
        p->opts.ylim = ylim;
        p->samMaxY = samMaxY;
        p->opts.max_tlen = max_tlen;
        return Err::NONE;
    }

    Err indelLength(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        int indel_length = p->opts.indel_length;
        try {
            indel_length = std::stoi(parts.back());
        } catch (...) {
            out << termcolor::red << "Error:" << termcolor::reset << " indel-length invalid value\n";
            return Err::NONE;
        }
        p->opts.indel_length = indel_length;
        if (p->mode == Manager::Show::SINGLE) {
            p->processed = true;
        } else {
            p->processed = false;
        }
        p->redraw = true;
        p->imageCache.clear();
        return Err::NONE;
    }

    Err remove(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        int ind = 0;
        if (Utils::startsWith(parts.back(), "bam")) {
            parts.back().erase(0, 3);
            try {
                ind = std::stoi(parts.back());
            } catch (...) {
                out << termcolor::red << "Error:" << termcolor::reset << " bam index not understood\n";
                return Err::NONE;
            }
            p->removeBam(ind);
        } else if (Utils::startsWith(parts.back(), "track")) {
            parts.back().erase(0, 5);
            try {
                ind = std::stoi(parts.back());
            } catch (...) {
                out << termcolor::red << "Error:" << termcolor::reset << " track index not understood\n";
                return Err::NONE;
            }
            p->removeTrack(ind);
        } else {
            try {
                ind = std::stoi(parts.back());
            } catch (...) {
                out << termcolor::red << "Error:" << termcolor::reset << " region index not understood\n";
                return Err::NONE;
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
        return Err::NONE;
    }

    Err cov(Plot* p, std::vector<std::string> parts, std::ostream& out) {
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
        for (auto &cl : p->collections) {
            cl.skipDrawingReads = false;
            cl.skipDrawingCoverage = false;
        }
        p->opts.max_coverage = std::max(0, p->opts.max_coverage);
        p->redraw = true;
        p->processed = false;
        p->imageCache.clear();
        return Err::NONE;
    }

    Err theme(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        if (parts.size() != 2) {
            out << termcolor::red << "Error:" << termcolor::reset << " theme must be either 'igv', 'dark' or 'slate'\n";
            return Err::NONE;
        }
        if (parts.back() == "dark") {
            p->opts.theme = Themes::DarkTheme();  p->opts.theme.setAlphas(); p->imageCache.clear(); p->opts.theme_str = "dark";
        } else if (parts.back() == "igv") {
            p->opts.theme = Themes::IgvTheme(); p->opts.theme.setAlphas(); p->imageCache.clear(); p->opts.theme_str = "igv";
        } else if (parts.back() == "slate") {
            p->opts.theme = Themes::SlateTheme(); p->opts.theme.setAlphas(); p->imageCache.clear(); p->opts.theme_str = "slate";
        } else {
            return Err::OPTION_NOT_UNDERSTOOD;
        }
        p->processed = false;
        p->redraw = true;
        return Err::NONE;
    }

    Err goto_command(Plot* p, std::vector<std::string> parts) {
        Err reason = Err::NONE;
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
            p->redraw = true;
            p->processed = false;
        }
        return Err::NONE;
    }

    Err grid(Plot* p, std::vector<std::string> parts) {
        try {
            p->opts.number = Utils::parseDimensions(parts[1]);
        } catch (...) {
            return Err::PARSE_INPUT;
        }
        p->redraw = true;
        p->processed = false;
        p->imageCache.clear();
        return Err::NONE;
    }

    Err add_region(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        if (p->mode != Manager::Show::SINGLE) {
            return Err::NONE;
        }
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
                    new_regions.push_back(dummy_region);
                    p->fetchRefSeq(new_regions.back());
                }
            } catch (...) {
                out << termcolor::red << "Error parsing :add" << termcolor::reset;
                return Err::PARSE_INPUT;
            }
        }
        p->regions.insert(p->regions.end(), new_regions.begin(), new_regions.end());
        p->redraw = true;
        p->processed = false;
        p->imageCache.clear();
        return Err::NONE;
    }

    Err snapshot(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        if (parts.size() > 2) {
            return Err::TOO_MANY_OPTIONS;
        }
        std::string fname;
        p->currentVarTrack = &p->variantTracks[p->variantFileSelection];
        if (parts.size() == 1) {
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
                    Parse::parse_output_name_format(nameFormat, vcfCols, sample_names_copy, p->bam_paths, lbl.current());
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
            if (!p->imageCacheQueue.empty()) {
                Manager::imagePngToFile(p->imageCacheQueue.back().second, out_path.string());
                Term::clearLine(out);
                out << "\rSaved to " << out_path << std::endl;
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

    //    Err write_bam()

    Err save_command(Plot* p, std::vector<std::string> parts, std::ostream& out) {
        if (parts.size() == 1) {
            return Err::OPTION_NOT_UNDERSTOOD;
        }
        if (parts.size() == 2) {
            if (Utils::endsWith(parts.back(), ".bam") || Utils::endsWith(parts.back(), ".cram")) {

            }
        } else if (parts.size() == 3 && (parts[1] == ">" || parts[1] == ">>")) {

        } else {
            return Err::OPTION_NOT_UNDERSTOOD;
        }
        return Err::NONE;
    }

    Err infer_region_or_feature(Plot* p, std::string& command, std::vector<std::string> parts) {
        Utils::Region rgn;
        Err reason = Err::NONE;
        try {
            rgn = Utils::parseRegion(command);
        } catch (...) {
            reason = Err::BAD_REGION;
        }
        if (reason == Err::NONE) {
            int res = faidx_has_seq(p->fai, rgn.chrom.c_str());
            if (res <= 0) {
                reason = Err::CHROM_NOT_IN_REFERENCE;
            }
            if (p->mode != Manager::Show::SINGLE) { p->mode = Manager::Show::SINGLE; }
            if (p->regions.empty()) {
                p->regions.push_back(rgn);
                p->fetchRefSeq(p->regions.back());
            } else {
                if (p->regions[p->regionSelection].chrom == rgn.chrom) {
                    rgn.markerPos = p->regions[p->regionSelection].markerPos;
                    rgn.markerPosEnd = p->regions[p->regionSelection].markerPosEnd;
                }
                p->regions[p->regionSelection] = rgn;
                p->fetchRefSeq(p->regions[p->regionSelection]);
            }
        } else {  // search all tracks for matching name, slow but ok for small tracks
            if (!p->tracks.empty()) {
                bool res = HGW::searchTracks(p->tracks, command, rgn);
                if (res) {
                    if (p->mode != Manager::Show::SINGLE) { p->mode = Manager::Show::SINGLE; }
                    if (p->regions.empty()) {
                        p->regions.push_back(rgn);
                        p->fetchRefSeq(p->regions.back());
                    } else {
                        if (p->regionSelection < (int)p->regions.size()) {
                            p->regions[p->regionSelection] = rgn;
                            p->fetchRefSeq(p->regions[p->regionSelection]);
                        }
                    }
                } else {
                    reason = Err::SILENT;
                }
            }
        }
        if (reason == Err::NONE) {
            p->redraw = true;
            p->processed = false;
            p->imageCache.clear();
        }
        return reason;
    }

    void handle_err(Err result, std::ostream& out) {
        switch (result) {
            case NONE: break;
            case UNKNOWN:
                out << termcolor::red << "Error:" << termcolor::reset << " Unknown error\n";
                break;
            case SILENT: break;
            case TOO_MANY_OPTIONS:
                out << termcolor::red << "Error:" << termcolor::reset << " Too many options supplied\n";
                break;
            case CHROM_NOT_IN_REFERENCE:
                out << termcolor::red << "Error:" << termcolor::reset << " chromosome not in reference\n";
                break;
            case FEATURE_NOT_IN_TRACKS:
                out << termcolor::red << "Error:" << termcolor::reset << " Feature not in tracks\n";
                break;
            case BAD_REGION:
                out << termcolor::red << "Error:" << termcolor::reset << " Region not understood\n";
                break;
            case OPTION_NOT_UNDERSTOOD:
                out << termcolor::red << "Error:" << termcolor::reset << " Option not understood\n";
                break;
            case INVALID_PATH:
                out << termcolor::red << "Error:" << termcolor::reset << " Path was invalid\n";
                break;
            case EMPTY_TRACKS:
                out << termcolor::red << "Error:" << termcolor::reset << " tracks are empty (add a track first)\n";
                break;
            case EMPTY_BAMS:
                out << termcolor::red << "Error:" << termcolor::reset << " Bams are empty (add a bam first)\n";
                break;
            case EMPTY_REGIONS:
                out << termcolor::red << "Error:" << termcolor::reset << " Regions are empty (add a region first)\n";
                break;
            case EMPTY_VARIANTS:
                out << termcolor::red << "Error:" << termcolor::reset << " No variant file (add a variant file first)\n";
                break;
            case PARSE_VCF:
                out << termcolor::red << "Error:" << termcolor::reset << " Vcf parsing error\n";
                break;
            case PARSE_INPUT:
                out << termcolor::red << "Error:" << termcolor::reset << " Input could not be parsed\n";
                break;
        }
    }

    // Command functions capture these parameters only
    #define PARAMS [](Commands::Plot* p, std::string& command, std::vector<std::string>& parts, std::ostream& out) -> Err

    // Note the function map will be cached after first call. plt is bound, but parts are updated with each call
    void run_command_map(Plot* p, std::string& command, std::ostream& out) {

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
                {"settings", PARAMS { return settings(p); }},
                {"ins",      PARAMS { return insertions(p); }},
                {"insertions",  PARAMS { return insertions(p); }},
                {"mm",       PARAMS { return mismatches(p); }},
                {"mismatches",  PARAMS { return mismatches(p); }},
                {"edges",    PARAMS { return edges(p); }},
                {"soft_clips",  PARAMS { return soft_clips(p); }},
                {"log2-cov",  PARAMS { return log2_cov(p); }},
                {"expand-tracks", PARAMS { return expand_tracks(p); }},
                {"tlen-y",   PARAMS { return tlen_y(p); }},
                {"sam",      PARAMS { return sam(p, command, parts, out); }},
                {"h",        PARAMS { return getHelp(p, command, parts, out); }},
                {"help",     PARAMS { return getHelp(p, command, parts, out); }},
                {"man",      PARAMS { return getHelp(p, command, parts, out); }},
                {"link",     PARAMS { return link(p, command, parts); }},
                {"v",        PARAMS { return var_info(p, command, parts, out); }},
                {"var",      PARAMS { return var_info(p, command, parts, out); }},
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
                {"grid",     PARAMS { return grid(p, parts); }},
                {"add",      PARAMS { return add_region(p, parts, out); }},
                {"s",        PARAMS { return snapshot(p, parts, out); }},
                {"snapshot", PARAMS { return snapshot(p, parts, out); }},
                {"online",   PARAMS { return online(p, parts, out); }},
                {"save",     PARAMS { return save_command(p, parts, out); }},

        };

        auto it = functionMap.find(parts[0]);
        Err res;
        if (it != functionMap.end()) {
            res = it->second(p, command, parts, out);  // Execute the mapped function
        } else {
            res = infer_region_or_feature(p, command, parts);
        }
        p->inputText = "";
        handle_err(res, out);
    }
}