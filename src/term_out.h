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
#include "../include/unordered_dense.h"
#include "../include/termcolor.h"
#include "term_out.h"
#include "themes.h"

namespace Term {

    void help(Themes::IniOptions &opts);

    void manuals(std::string &s);

    void clearLine();

    void editInputText(std::string &inputText, const char *letter, int &charIndex);

    void printRead(std::vector<Segs::Align>::iterator r, const sam_hdr_t* hdr, std::string &sam, const char *refSeq, int refStart, int refEnd, bool low_mem);

    void printSelectedSam(std::string &sam);

    void printKeyFromValue(int v);

    std::string intToStringCommas(int pos);

    void printRefSeq(Utils::Region *region, float x, float xOffset, float xScaling);

	void printCoverage(int pos, Segs::ReadCollection &cl);

	void printTrack(float x, HGW::GwTrack &track, Utils::Region *rgn, bool mouseOver, int targetLevel, int trackIdx);

    void printVariantFileInfo(Utils::Label *label, int index);

    void printOnlineLinks(std::vector<HGW::GwTrack> &tracks, Utils::Region &rgn, std::string &genome_tag);

    void updateRefGenomeSeq(Utils::Region *region, float xW, float xOffset, float xScaling);
}
