//
// Created by Kez Cleal on 07/12/2022.
//

#pragma once

namespace Term {

    void help(Themes::IniOptions &opts);

    void manuals(std::string &s);

    void clearLine();

    void editInputText(std::string &inputText, const char *letter, int &charIndex);

    void printRead(std::vector<Segs::Align>::iterator r, const sam_hdr_t* hdr, std::string &sam, const char *refSeq, int refStart, int refEnd);

    void printSelectedSam(std::string &sam);

    void printKeyFromValue(int v);

    void printRefSeq(float x, std::vector<Segs::ReadCollection> &collections);

	  void printTargetRecord(std::string &id_string, std::string &chrom, int pos);

    void updateRefGenomeSeq(float xW, std::vector<Segs::ReadCollection> &collections);
}
