//
// Created by Kez Cleal on 11/11/2022.
//

#pragma once

#include <functional>
#include <string>
#include <vector>
#include <iostream>

#include <htslib/sam.h>

#include "segments.h"
#include "ankerl_unordered_dense.h"


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
        FLAG_DUPLICATE = 1024,
        SUPPLEMENTARY = 2048,

        // int values
        FLAG = 3000,
        NFLAG = 3001,
        POS = 3002,
        REF_END = 3003,
        MAPQ = 3004,
        PNEXT = 3005,
        TLEN = 3006,
        ABS_TLEN = 3007,
        SEQ_LEN = 3008,
        NM = 3009, CM = 3010, FI = 3011, HO = 3012, MQ = 3013, SM = 3014, TC = 3015, UQ = 3016, AS = 3017,
        TID = 3018, MID = 3019,

        // Patterns
        PATTERN = 3500,
        DEL = 3501,
        INV_F = 3502,
        INV_R = 3503,
        DUP = 3504,
        TRA = 3505,

        // str values
        QNAME = 4000,
        RNAME = 4001,
        CIGAR = 4002,
        RNEXT = 4003,
        SEQ = 4004,
        RG = 4005, BC = 4006, LB = 4007, MD = 4008, PU = 4010, SA = 4011, MC = 4012, BX = 4013, MI = 4014, RX = 4015,

        EQ = -1,
        NE = -2,
        GT = -3,
        LT = -4,
        GE = -5,
        LE = -6,
        CONTAINS = -7,
        OMIT = -8,
        AND = -9
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
        Parser(std::ostream& errOutput);
        ~Parser() = default;

        bool orBlock;
        std::string filter_str;
        ankerl::unordered_dense::map< std::string, Property> opMap;
        ankerl::unordered_dense::map< Property, std::string> permit;
        std::vector<Eval> evaluations_block;
        std::vector< std::vector<size_t> > targetIndexes;
        std::ostream& out;

        int set_filter(std::string &f, int nBams, int nRegions);
        bool eval(const Segs::Align &aln, const sam_hdr_t* hdr, int bamIdx, int regionIdx);

    private:
        int prep_evaluations(std::vector<Eval> &results, std::vector<std::string> &tokens);
        int split_into_or(std::string &f, std::vector<Eval> &results, int nBams, int nRegions);
    };

    void countExpression(std::vector<Segs::ReadCollection> &collections, std::string &str, std::vector<sam_hdr_t*> hdrs,
                         std::vector<std::string> &bam_paths, int nBams, int nRegions, std::ostream& out);

	void parse_INFO(std::string &line, std::string &infoCol, std::string &request, std::ostream& out);

	void parse_FORMAT(std::string &line, std::vector<std::string> &vcfCols, std::string &request, std::vector<std::string> &sample_names, std::ostream& out);

	void parse_vcf_split(std::string &line, std::vector<std::string> &vcfCols, std::string &request, std::vector<std::string> &sample_names, std::ostream& out);

	void parse_output_name_format(std::string &nameFormat, std::vector<std::string> &vcfCols, std::vector<std::string> &sample_names,
                                  std::vector<std::string> &bam_paths, std::string &label, std::ostream& out);

	void parse_sample_variable(std::string &fname, std::vector<std::string> &bam_paths);

    int parse_indexing(std::string &s, size_t nBams, size_t nRegions, std::vector< std::vector<size_t> > &v, std::ostream& out);

    std::string tilde_to_home(std::string fpath);

    void tryTabCompletion(std::string &inputText, std::ostream& out, int& charIndex);
}


