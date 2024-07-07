//
// Created by Kez Cleal on 07/12/2022.
//

#pragma once

#include <htslib/sam.h>
#include <string>
#include <thread>
#include <vector>
#include "htslib/hts.h"
#include "drawing.h"
#include "hts_funcs.h"
#include "plot_manager.h"
#include "segments.h"
#include "ankerl_unordered_dense.h"
#include "termcolor.h"
#include "term_out.h"
#include "themes.h"

namespace Term {

    void help(Themes::IniOptions &opts, std::ostream& out);

    void manuals(std::string &s, std::ostream& out);

    void clearLine(std::ostream& out);

    void editInputText(std::string &inputText, const char *letter, int &charIndex);

    void printRead(std::vector<Segs::Align>::iterator r, const sam_hdr_t* hdr, std::string &sam, const char *refSeq,
                   int refStart, int refEnd, bool low_mem, std::ostream& out, int pos, int indel_length, bool show_mod);

    void printSelectedSam(std::string &sam, std::ostream& out);

    void printKeyFromValue(int v, std::ostream& out);

    std::string intToStringCommas(int pos);

    void printRefSeq(Utils::Region *region, float x, float xOffset, float xScaling, std::ostream& out);

	void printCoverage(int pos, Segs::ReadCollection &cl, std::ostream& out);

	void printTrack(float x, HGW::GwTrack &track, Utils::Region *rgn, bool mouseOver, int targetLevel, int trackIdx,
                    std::string &target_name, int *target_pos, std::ostream& out);

    void printVariantFileInfo(Utils::Label *label, int index, std::ostream& out);

    void printOnlineLinks(std::vector<HGW::GwTrack> &tracks, Utils::Region &rgn, std::string &genome_tag,
                          std::ostream& out);

    void updateRefGenomeSeq(Utils::Region *region, float xW, float xOffset, float xScaling, std::ostream& out);
}
