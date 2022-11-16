//
// Created by Kez Cleal on 11/11/2022.
//

#include <functional>
#include <string>
#include <vector>
#include <iostream>

#include <htslib/sam.h>

#include "segments.h"
#include "../include/robin_hood.h"

#pragma once

namespace Parse {

    enum Property {

        PAIRED = 1,
        PROPER_PAIR = 2,
        UNMAP = 4,
        MUNMAP = 8,
        REVERSE = 16,
        MREVERSE = 32,
        READ1 = 64,
        READ2 = 128,
        SECONDARY = 256,
        QCFAIL = 512,
        DUP = 1024,
        SUPPLEMENTARY = 2048,

        QNAME,
        FLAG,
        NFLAG,
        RNAME,
        POS,
        REF_END,
        MAPQ,
        CIGAR,
        RNEXT,
        PNEXT,
        TLEN,
        ABS_TLEN,
        SEQ,
        SEQ_LEN,

        TAG,

        EQ,
        NE,
        GT,
        LT,
        GE,
        LE,
        CONTAINS,
        OMIT,
        AND
    };

    class Eval {
    public:
        int ival;
        std::string sval;
        Property property;
        Property op;
        bool result;
        void eval();
    };

    class Parser {
    public:
        Parser();
        ~Parser() {};

        bool orBlock;
        std::string filter_str;

        robin_hood::unordered_map< std::string, Property> opMap;
        robin_hood::unordered_map< Property, std::string> permit;
        std::vector<Eval> evaluations_block;
        std::vector< std::vector<int> > targetIndexes;

        int set_filter(std::string &f, int nBams, int nRegions);
        bool eval(const Segs::Align &aln, const sam_hdr_t* hdr, int bamIdx, int regionIdx);

    private:
        int prep_evaluations(std::vector<Eval> &results, std::vector<std::string> &tokens);
        int split_into_or(std::string &f, std::vector<Eval> &results, int nBams, int nRegions);

    };

    void countExpression(std::vector<Segs::ReadCollection> &collections, std::string &str, std::vector<sam_hdr_t*> hdrs,
                         std::vector<std::string> &bam_paths, int nBams, int nRegions);

}


