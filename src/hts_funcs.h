//
// Created by Kez Cleal on 04/08/2022.
//

#pragma once

#include <string>
#include <vector>

#include "htslib/hfile.h"
#include "htslib/sam.h"
#include "htslib/vcf.h"

#include "plot_manager.h"
#include "segments.h"
#include "themes.h"


namespace HTS {



//
//    void collectReadsAndCoverage(Segs::ReadCollection &col, htsFile *bam, sam_hdr_t *hdr_ptr,
//                                 hts_idx_t *index, Themes::IniOptions &opts, Utils::Region *region, bool coverage);
//
//    class VCF {
//    public:
////        VCF (const char *label_to_parse) {
////            done = false;
////            this->label_to_parse = label_to_parse;
////        };
//        VCF () {};
//        ~VCF();
//
//        htsFile *fp;
//        const bcf_hdr_t *hdr;
//        bcf1_t *v;
//        std::string path;
//        std::string chrom, chrom2, rid, vartype, label, tag;
//
//        int parse;
//        int info_field_type;
//        const char *label_to_parse;
//        long start, stop;
//        bool done;
//
//        void open(std::string f);
//
//        void next();
//    };
//
}