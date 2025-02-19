//
// Created by Kez Cleal on 11/11/2022.
//
#include <algorithm>
#include <iostream>
#include <utility>
#include <string>
#include <regex>
#include <htslib/sam.h>
#include <glob_cpp.hpp>
#include "termcolor.h"
#include "term_out.h"
#include "utils.h"
#include "parser.h"
#include "segments.h"


namespace Parse {

    constexpr std::string_view numeric_like = "eq ne gt lt ge le = == != > < >= <=";
    constexpr std::string_view string_like = "eq ne contains = == != omit";

    Parser::Parser(std::ostream& errOutput) : out(errOutput) {
        opMap["mapq"] = MAPQ;
        opMap["flag"] = FLAG;
        opMap["~flag"] = NFLAG;
        opMap["qname"] = QNAME;
        opMap["tlen"] = TLEN;
        opMap["abs-tlen"] = ABS_TLEN;
        opMap["rname"] = RNAME;
        opMap["rnext"] = RNEXT;
        opMap["tid"] = TID;
        opMap["mid"] = MID;
        opMap["pos"] = POS;
        opMap["ref-end"] = REF_END;
        opMap["pnext"] = PNEXT;
        opMap["seq"] = SEQ;
        opMap["seq-len"] = SEQ_LEN;
        opMap["cigar"] = CIGAR;

        opMap["RG"] = RG;
        opMap["BC"] = BC;
        opMap["LB"] = LB;
        opMap["MD"] = MD;
        opMap["MI"] = MI;
        opMap["PU"] = PU;
        opMap["SA"] = SA;
        opMap["MC"] = MC;

        opMap["NM"] = NM;
        opMap["CM"] = CM;
        opMap["FI"] = FI;
        opMap["HO"] = HO;
        opMap["MQ"] = MQ;
        opMap["SM"] = SM;
        opMap["TC"] = TC;
        opMap["UQ"] = UQ;
        opMap["AS"] = AS;
        opMap["BX"] = BX;
        opMap["RX"] = RX;
        opMap["HP"] = HP;

        opMap["eq"] = EQ;
        opMap["ne"] = NE;
        opMap["gt"] = GT;
        opMap["lt"] = LT;
        opMap["ge"] = GE;
        opMap["le"] = LE;
        opMap["=="] = EQ;
        opMap["="] = EQ;
        opMap["!="] = NE;
        opMap[">"] = GT;
        opMap["<"] = LT;
        opMap[">="] = GE;
        opMap["<="] = LE;
        opMap["contains"] = CONTAINS;
        opMap["omit"] = OMIT;
        opMap["&"] = AND;

        opMap["paired"] = PAIRED;
        opMap["proper-pair"] = PROPER_PAIR;
        opMap["unmap"] = UNMAP;
        opMap["munmap"] = MUNMAP;
        opMap["reverse"] = REVERSE;
        opMap["mreverse"] = MREVERSE;
        opMap["read1"] = READ1;
        opMap["read2"] = READ2;
        opMap["secondary"] = SECONDARY;
        opMap["qcfail"] = QCFAIL;
        opMap["duplicate"] = FLAG_DUPLICATE;
        opMap["supplementary"] = SUPPLEMENTARY;

        opMap["del"] = Property::DEL;
        opMap["deletion"] = Property::DEL;
        opMap["inv_f"] = Property::INV_F;
        opMap["inversion_forward"] = Property::INV_F;
        opMap["inv_r"] = Property::INV_R;
        opMap["inversion_reverse"] = Property::INV_R;
        opMap["dup"] = Property::DUP;
        opMap["duplication"] = Property::DUP;
        opMap["tra"] = Property::TRA;
        opMap["translocation"] = Property::TRA;
        opMap["pattern"] = Property::PATTERN;

        permit[MAPQ] = numeric_like;
        permit[FLAG] = "&";
        permit[NFLAG] = "&";
        permit[QNAME] = string_like;
        permit[TLEN] = numeric_like;
        permit[ABS_TLEN] = numeric_like;
        permit[POS] = numeric_like;
        permit[REF_END] = numeric_like;
        permit[PNEXT] = numeric_like;
        permit[RNAME] = string_like;
        permit[RNEXT] = string_like;
        permit[TID] = numeric_like;
        permit[MID] = numeric_like;
        permit[SEQ] = string_like;
        permit[SEQ_LEN] = numeric_like;
        permit[CIGAR] = string_like;

        // tags
        permit[RG] = string_like;
        permit[BC] = string_like;
        permit[LB] = string_like;
        permit[MD] = string_like;
        permit[MI] = string_like;
        permit[PU] = string_like;
        permit[SA] = string_like;
        permit[MC] = string_like;
        permit[BX] = string_like;
        permit[RX] = string_like;
        permit[NM] = numeric_like;
        permit[CM] = numeric_like;
        permit[FI] = numeric_like;
        permit[HO] = numeric_like;
        permit[MQ] = numeric_like;
        permit[SM] = numeric_like;
        permit[TC] = numeric_like;
        permit[UQ] = numeric_like;
        permit[AS] = numeric_like;
        permit[HP] = numeric_like;

        permit[PATTERN] = string_like;

    }

    int parse_indexing(std::string &s, size_t nBams, size_t nRegions, std::vector< std::vector<size_t> > &v, std::ostream& out) {
        // check for indexing. Makes a lookup table which describes which panels a filter should be applied to
        std::string::iterator iStart = s.end();
        std::string::iterator iEnd = s.end();
        bool open = false;
        bool close = false;
        while (iStart != s.begin()) {
            --iStart;
            if (*iStart == ' ') {
                continue;
            } else if (!open && *iStart == ']') {
                open = true;
                iEnd = iStart;
            } else if (open && *iStart == '[') {
                close = true;
                ++iStart;
                break;
            } else if (!open) {
                break;
            }
        }
        if (!open) {
            return 0;
        }
        if (!open != !close) {  // xor
            out << "Error: expression not understood: " << s << std::endl;
            return -1;
        }
        auto indexStr = std::string(iStart, iEnd);
        --iStart;
        s.erase(iStart, s.end());
        if (indexStr == ":") {
            return 1;
        }
        if (nBams == 0 || nRegions == 0) {
            out << "Error: No bam/region to filter. Trying to apply a filter to nBams==" << nBams << " and nRegions==" << nRegions << std::endl;
            return -1;
        }
        std::string lhs, rhs;
        indexStr.erase(std::remove(indexStr.begin(), indexStr.end(), ' '), indexStr.end());
        std::string::iterator itr = indexStr.begin();
        iStart = indexStr.begin();
        while (itr != indexStr.end()) {
            if (*itr == ',') {
                lhs = std::string(iStart, itr);
                iStart = itr; ++iStart;
                ++itr;
                continue;
            }
            ++itr;
        }
        rhs = std::string(iStart, itr);
        if (nBams > 1 && lhs.empty()) {
            out << "Error: if multiple bams are present you need to specify the [row,column] e.g. [:, 0] or [0,1] etc\n";
            return -1;
        }
        bool allRows = lhs == ":";
        bool allColumns = rhs == ":";
        int iRow, iCol;
        try {
            iRow = (lhs.empty()) ? 0 : (allRows) ? -1 : std::stoi(lhs);
            iCol = (allColumns) ? 0 : std::stoi(rhs);
        } catch (...) {
            out << "Error: string to integer failed for left-hand side=" << lhs << ", or right-hand side=" << rhs << std::endl;
            return -1;
        }
        iRow = (iRow < 0) ? nBams + iRow : iRow;  // support negative indexing
        iCol = (iCol < 0) ? nRegions + iCol : iCol;
        if (std::abs(iRow) >= (int)nBams) {
            out << "Error: row index is > nBams\n";
            return -1;
        }
        if (std::abs(iCol) >= (int)nRegions) {
            out << "Error: column index is > nRegions\n";
            return -1;
        }
        v.resize(nBams, std::vector<size_t>(nRegions));
        for (size_t r=0; r < nBams; ++r) {
            for (size_t c=0; c < nRegions; ++c) {
                if (allRows && c==(size_t)iCol) {
                    v[r][c] = 1;
                } else if (allColumns && r==(size_t)iRow) {
                    v[r][c] = 1;
                } else if (c==(size_t)iCol && r==(size_t)iRow) {
                    v[r][c] = 1;
                }
            }
        }
        return 1;
    }

    int Parser::split_into_or(std::string &s, std::vector<Eval> &evaluations, int nBams, int nRegions) {
        std::string delim;
        if (s.find("and") != std::string::npos) {
            delim = "and";
            orBlock = false;
        } else {
            delim = "or";
            orBlock = true;
        }

        int res = parse_indexing(s, nBams, nRegions, targetIndexes, out);
        if (res < 0) {
            return res;
        }

        auto start = 0U;
        auto end = s.find(delim);
        std::string token;

        // list of 'or' evaluations
        std::vector<std::vector<std::string> > allTokens;

        if (end == std::string::npos) {
            token = std::regex_replace(s, std::regex("^ +| +$|( ) +"), "$1");
            auto output = Utils::split(token, ' ');
            if (output.size() != 3) {
                if (output.size() == 1) {
                    if (output[0].at(0) != '~') {
                        output = {"flag", "&", output[0]};
                    } else {
                        output[0].erase(0, 1);
                        output = {"~flag", "&", output[0]};
                    }
                } else {
                    out << "Expression not understood, need three components as {property} {operator} {value}, or a named value for flag. Found: " << token << std::endl;
                    return -1;
                }
            }
            allTokens.push_back(output);

        } else {
            while (end != std::string::npos)
            {
                token = s.substr(start, end - start);
                token = std::regex_replace(token, std::regex("^ +| +$|( ) +"), "$1");
                auto output = Utils::split(token, ' ');
                if (output.size() != 3) {
                    if (output.size() == 1) {
                        if (output[0].at(0) != '~') {
                            output = {"flag", "&", output[0]};
                        } else {
                            output[0].erase(0, 1);
                            output = {"~flag", "&", output[0]};
                        }
                    } else {
                        out << "Expression not understood, need three components as {property} {operator} {value}, or a named value for flag. Found: " << token << std::endl;
                        return -1;
                    }
                }
                allTokens.push_back(output);
                start = end + delim.length();
                end = s.find(delim, start);
            }

            token = s.substr(start, end - start);
            token = std::regex_replace(token, std::regex("^ +| +$|( ) +"), "$1");
            auto output = Utils::split(token, ' ');
            if (output.size() != 3) {
                if (output.size() == 1) {
                    if (output[0].at(0) != '~') {
                        output = {"flag", "&", output[0]};
                    } else {
                        output[0].erase(0, 1);
                        output = {"~flag", "&", output[0]};
                    }
                } else {
                    out << "Expression not understood, need three components as {property} {operator} {value}, or a named value for flag. Found: " << token << std::endl;
                    return -1;
                }
            }
            allTokens.push_back(output);

        }
        for (auto &output: allTokens) {
            int result = prep_evaluations(evaluations, output);
            if (result < 0) {
                return result;
            }
        }
        return 1;
    }

    int Parser::prep_evaluations(std::vector<Eval> &evaluations, std::vector<std::string> &output) {
        if (! opMap.contains(output[0])) {
            out << "Left-hand side property not available: " << output[0] << std::endl;
            return -1;
        }
        if (! opMap.contains(output[1])) {
            out << "Middle operation not available: " << output[1] << std::endl;
            return -1;
        }
        Property lhs = opMap[output[0]];
        Property mid = opMap[output[1]];

        std::string allowed = permit[lhs];
        if (allowed.find(output[1]) == std::string::npos) {
            out << output[0] << " is only compatible with: " << allowed << std::endl;
            return -1;
        }

        Eval e;
        if (lhs >= 3000 && lhs < 4000) {
            e.property = lhs;
            e.op = mid;
            try {
                e.ival = std::stoi(output.back());
            } catch (...) {
                if (output.back() == "del" || output.back() == "deletion") {
                    e.ival = AlignFormat::Pattern::DEL;
                } else if (output.back() == "inv_f" || output.back() == "inversion_forward") {
                    e.ival = AlignFormat::Pattern::INV_F;
                } else if (output.back() == "inv_r" || output.back() == "inversion_reverse") {
                    e.ival = AlignFormat::Pattern::INV_R;
                } else if (output.back() == "dup" || output.back() == "duplication") {
                    e.ival = AlignFormat::Pattern::DUP;
                } else if (output.back() == "tra" || output.back() == "translocation") {
                    e.ival = AlignFormat::Pattern::TRA;
                } else if (output.back() == "paired") {
                    e.ival = Property::PAIRED;
                } else if (output.back() == "proper-pair") {
                    e.ival = Property::PROPER_PAIR;
                } else if (output.back() == "unmapped") {
                    e.ival = Property::UNMAP;
                } else if (output.back() == "munmap") {
                    e.ival = Property::MUNMAP;
                } else if (output.back() == "reverse") {
                    e.ival = Property::REVERSE;
                } else if (output.back() == "mreverse") {
                    e.ival = Property::MREVERSE;
                } else if (output.back() == "read1") {
                    e.ival = Property::READ1;
                } else if (output.back() == "read2") {
                    e.ival = Property::READ2;
                } else if (output.back() == "secondary") {
                    e.ival = Property::SECONDARY;
                } else if (output.back() == "qcfail") {
                    e.ival = Property::QCFAIL;
                } else if (output.back() == "duplicate") {
                    e.ival = Property::FLAG_DUPLICATE;
                } else if (output.back() == "supplementary") {
                    e.ival = Property::SUPPLEMENTARY;
                } else {
                    out << "Right-hand side value must be an integer or named-value: " << output[2] << std::endl;
                    out << "Named values can be one of: paired, proper-pair, unmapped, munmap, reverse, mreverse, read1, read2, secondary, qcfail, dup, supplementary\n";
                    return -1;
                }
            }
        } else if (lhs >= 4000) {
            e.property = lhs;
            e.op = mid;
            e.sval = output.back();
        } else {
            out << "Left-hand side operation not available: " << output[0] << std::endl; return -1;
        }
        evaluations.push_back(e);
        return 1;
    }

    int Parser::set_filter(std::string &s, int nBams, int nRegions) {
        filter_str = s;
        if ( (s.find("or") != std::string::npos) && (s.find("and") != std::string::npos) ) {
            out << "Filter block must be either composed of 'or' expressions, or 'and' expressions, not both\n";
            return -1;
        }
        int res1 = split_into_or(s, evaluations_block, nBams, nRegions);
        return res1;
    }

    bool seq_contains(const uint8_t *seq, uint32_t len, const std::string &fstr) {
        auto slen = (int)fstr.size();
        int target = slen - 1;
        int j;
        for (int i=0; i < (int)len; i++){
            for (j=0; j < slen; j++) {
                if (fstr[j] != seq_nt16_str[bam_seqi(seq, i + j)]) {
                    break;
                }
                if (j == target) {
                    return true;
                }
            }
        }
        return false;
    }

    bool seq_eq(const uint8_t *seq, uint32_t len, const std::string &fstr) {
        if (len != fstr.size()) {
            return false;
        }
        for (int i=0; i< (int)len ; i++){
            if ((char)fstr[i] != (char)seq_nt16_str[bam_seqi(seq, i)]) {
                return false;
            }
        }
        return true;
    }

    void process_ival(bool &this_result, Eval &e, int int_val) {
        switch (e.op) {
            case GT: this_result = int_val > e.ival; break;
            case GE: this_result = int_val >= e.ival; break;
            case LT: this_result = int_val < e.ival; break;
            case LE: this_result = int_val <= e.ival; break;
            case EQ: this_result = int_val == e.ival; break;
            case NE: this_result = int_val != e.ival; break;
            case AND: this_result = int_val & e.ival; break;
            default: break;
        }
    }

    void getStrTag(const char* tag, std::string &str_val, const AlignFormat::Align &aln) {
        const uint8_t *tag_ptr;
        tag_ptr = bam_aux_get(aln.delegate, tag);
        if (tag_ptr == nullptr) {
            return;
        }
        str_val = std::string(bam_aux2Z(tag_ptr));
    }

    void getIntTag(const char* tag, int &int_val, const AlignFormat::Align &aln) {
        const uint8_t *tag_ptr;
        tag_ptr = bam_aux_get(aln.delegate, tag);
        if (tag_ptr == nullptr) {
            return;
        }
        int_val = bam_aux2i(tag_ptr);
    }

    void getCigarStr(std::string &str_val, const AlignFormat::Align &aln) {
        uint32_t l, cigar_l, op, k;
        uint32_t *cigar_p;
        cigar_l = aln.delegate->core.n_cigar;
        cigar_p = bam_get_cigar(aln.delegate);
        for (k = 0; k < cigar_l; k++) {
            op = cigar_p[k] & BAM_CIGAR_MASK;
            l = cigar_p[k] >> BAM_CIGAR_SHIFT;
            str_val += std::to_string(l);
            switch (op) {
                case 0:
                    str_val += "M"; break;
                case 1:
                    str_val += "I"; break;
                case 2:
                    str_val += "D"; break;
                case 4:
                    str_val += "S"; break;
                case 5:
                    str_val += "H"; break;

                default: break;
            }
        }
    }

    bool Parser::eval(const AlignFormat::Align& aln, const sam_hdr_t* hdr, int bamIdx, int regionIdx) {

        bool block_result = true;

        if (bamIdx >= 0 && !targetIndexes.empty() && targetIndexes[bamIdx][regionIdx] == 0) {
            return true;
        }

        if (!evaluations_block.empty()) {
            for (auto &e : evaluations_block) {
                int int_val = 0;
                std::string str_val;
                bool this_result = false;
                const char *char_ptr;
                switch (e.property) {
                    case PATTERN:
                        int_val = aln.orient_pattern;
                        break;
                    case MAPQ:
                        int_val = aln.delegate->core.qual;
                        break;
                    case SEQ_LEN:
                        int_val = aln.delegate->core.l_qseq;
                        break;
                    case FLAG:
                        int_val = aln.delegate->core.flag;
                        break;
                    case TLEN:
                        int_val = aln.delegate->core.isize;

                        break;
                    case ABS_TLEN:
                        int_val = std::abs(aln.delegate->core.isize);
                        break;
                    case NFLAG:
                        int_val = ~aln.delegate->core.flag;
                        break;
                    case QNAME:
                        str_val = bam_get_qname(aln.delegate);
                        break;
                    case POS:
                        int_val = aln.delegate->core.pos;
                        break;
                    case REF_END:
                        int_val = bam_endpos(aln.delegate);
                    case PNEXT:
                        int_val = aln.delegate->core.mpos;
                        break;
                    case RNAME:
                        char_ptr = sam_hdr_tid2name(hdr, aln.delegate->core.tid);
                        str_val = char_ptr;
                        break;
                    case RNEXT:
                        char_ptr = sam_hdr_tid2name(hdr, aln.delegate->core.mtid);
                        str_val = char_ptr;
                        break;
                    case TID:
                        int_val = aln.delegate->core.tid;
                        break;
                    case MID:
                        int_val = aln.delegate->core.mtid;
                        break;
                    case RG:
                        getStrTag("RG", str_val, aln);
                        break;
                    case BC:
                        getStrTag("BC", str_val, aln);
                        break;
                    case LB:
                        getStrTag("LB", str_val, aln);
                        break;
                    case MD:
                        getStrTag("MD", str_val, aln);
                        break;
                    case PU:
                        getStrTag("PU", str_val, aln);
                        break;
                    case SA:
                        getStrTag("SA", str_val, aln);
                        break;
                    case MC:
                        getStrTag("MC", str_val, aln);
                        break;
                    case BX:
                        getStrTag("BX", str_val, aln);
                        break;
                    case RX:
                        getStrTag("RX", str_val, aln);
                        break;
                    case MI:
                        getStrTag("MI", str_val, aln);
                        break;
                    case NM:
                        getIntTag("NM", int_val, aln);
                        break;
                    case CM:
                        getIntTag("CM", int_val, aln);
                        break;
                    case FI:
                        getIntTag("FI", int_val, aln);
                        break;
                    case HO:
                        getIntTag("HO", int_val, aln);
                        break;
                    case MQ:
                        getIntTag("MQ", int_val, aln);
                        break;
                    case SM:
                        getIntTag("SM", int_val, aln);
                        break;
                    case TC:
                        getIntTag("TC", int_val, aln);
                        break;
                    case UQ:
                        getIntTag("UQ", int_val, aln);
                        break;
                    case AS:
                        getIntTag("AS", int_val, aln);
                        break;
                    case HP:
                        getIntTag("HP", int_val, aln);
                        break;
                    case CIGAR:
                         getCigarStr(str_val, aln);
                         break;
                    default:
                        break;

                }
                if (e.property == SEQ) {
                    switch (e.op) {
                        case EQ: this_result = str_val == e.sval; break;
                        case NE: this_result = str_val != e.sval; break;
                        case CONTAINS: this_result = seq_contains(bam_get_seq(aln.delegate), aln.delegate->core.l_qseq, e.sval); break;
                        case OMIT: this_result = !seq_contains(bam_get_seq(aln.delegate), aln.delegate->core.l_qseq, e.sval); break;
                        default: break;
                    }

                } else if (str_val.empty()) {
                    switch (e.op) {
                        case GT: this_result = int_val > e.ival; break;
                        case GE: this_result = int_val >= e.ival; break;
                        case LT: this_result = int_val < e.ival; break;
                        case LE: this_result = int_val <= e.ival; break;
                        case EQ: this_result = int_val == e.ival; break;
                        case NE: this_result = int_val != e.ival; break;
                        case AND: this_result = int_val & e.ival; break;
                        default: break;
                    }
                } else {
                    switch (e.op) {
                        case EQ: this_result = str_val == e.sval; break;
                        case NE: this_result = str_val != e.sval; break;
                        case CONTAINS: this_result = str_val.find(e.sval) != std::string::npos; break;
                        case OMIT: this_result = str_val.find(e.sval) == std::string::npos; break;
                        default: break;
                    }
                }

                if (orBlock && this_result) {
                    return this_result;
                } else {
                    block_result &= this_result;
                }
            }
        }
        return block_result;
    }


    void countExpression(std::vector<Segs::ReadCollection> &collections, std::string &str, std::vector<sam_hdr_t*> hdrs,
                         std::vector<std::string> &bam_paths, int nBams, int nRegions, std::ostream& out) {

        std::vector<Parser> filters;
        for (auto &s: Utils::split(str, ';')) {
            Parse::Parser p = Parse::Parser(out);
            int rr = p.set_filter(s, nBams, nRegions);
            if (rr > 0) {
                filters.push_back(p);
            }
        }
        for (auto &col: collections) {
            int tot = 0;
            int paired = 0;
            int proper_pair = 0;
            int read_unmapped = 0;
            int mate_unmapped = 0;
            int read_reverse = 0;
            int mate_reverse = 0;
            int first = 0;
            int second = 0;
            int not_primary = 0;
            int fails_qc = 0;
            int duplicate = 0;
            int supp = 0;
            int del = 0;
            int dup = 0;
            int inv_f = 0;
            int inv_r = 0;
            int tra = 0;
            for (auto &align: col.readQueue) {
                bool drop = false;
                sam_hdr_t* hdr = hdrs[col.bamIdx];
                for (auto &f : filters) {
                    if (!f.eval(align, hdr, col.bamIdx, col.regionIdx)) {
                        drop = true;
                        break;
                    }
                }
                if (drop) {
                    continue;
                }
                uint32_t flag = align.delegate->core.flag;
                tot += 1;
                paired += bool(flag & 1);
                proper_pair += bool(flag & 2);
                read_unmapped += bool(flag & 4);
                mate_unmapped += bool(flag & 8);
                read_reverse += bool(flag & 16);
                mate_reverse += bool(flag & 32);
                first += bool(flag & 64);
                second += bool(flag & 128);
                not_primary += bool(flag & 256);
                fails_qc += bool(flag & 512);
                duplicate += bool(flag & 1024);
                supp += bool(flag & 2048);
                if (align.orient_pattern == AlignFormat::DEL) {
                    del += 1;
                } else if (align.orient_pattern == AlignFormat::DUP) {
                    dup += 1;
                } else if (align.orient_pattern == AlignFormat::TRA) {
                    tra += 1;
                } else if (align.orient_pattern == AlignFormat::INV_F) {
                    inv_f += 1;
                } else if (align.orient_pattern == AlignFormat::INV_R) {
                    inv_r += 1;
                }
            }
            out << termcolor::bright_blue << "File\t" << bam_paths[col.bamIdx] << termcolor::reset << std::endl;
            out << "Region\t" << col.region->chrom << ":" << col.region->start << "-" << col.region->end << std::endl;
            if (!str.empty()) {
                out << "Filter\t" << str << std::endl;
            }
            out << "Total\t" << tot << std::endl;
            out << "Paired\t" << paired << std::endl;
            out << "Proper-pair\t" << proper_pair << std::endl;
            out << "Read-unmapped\t" << read_unmapped << std::endl;
            out << "Mate-unmapped\t" << mate_unmapped << std::endl;
            out << "Read-reverse\t" << read_reverse << std::endl;
            out << "Mate-reverse\t" << mate_reverse << std::endl;
            out << "First-in-pair\t" << first << std::endl;
            out << "Second-in-pair\t" << second << std::endl;
            out << "Not-primary\t" << not_primary << std::endl;
            out << "Fails-qc\t" << fails_qc << std::endl;
            out << "Duplicate\t" << duplicate << std::endl;
            out << "Supplementary\t" << supp << std::endl;
            if (del > 0)
                out << "Deletion-pattern\t" << del << std::endl;
            if (dup > 0)
                out << "Duplication-pattern\t" << dup << std::endl;
            if (tra > 0)
                out << "Translocation-pattern\t" << tra << std::endl;
            if (inv_f > 0)
                out << "F-inversion-pattern\t" << inv_f << std::endl;
            if (inv_r > 0)
                out << "R-inversion-pattern\t" << inv_r << std::endl;
        }
    }

	void get_value_in_brackets(std::string &requestBracket, std::ostream& out) {
		const std::regex singleBracket("\\[");
		if (std::regex_search(requestBracket, singleBracket)) {
			const std::regex bracketRegex("\\[(.*?)\\]");
			std::smatch bracketMatch;
			if (std::regex_search(requestBracket, bracketMatch, bracketRegex)) {
				requestBracket = bracketMatch[1].str();
			} else {
				out << "Cannot parse sample inside [] from input string. \n";
			}
		} else {
			requestBracket = "0";
		}
	}

	bool is_number(const std::string& s) {
		std::string::const_iterator it = s.begin();
		while (it != s.end() && std::isdigit(*it)) ++it;
		return !s.empty() && it == s.end();
	}

	void convert_name_index(std::string &requestBracket, int &i, std::vector<std::string> &sample_names, std::ostream& out) {
		get_value_in_brackets(requestBracket, out);
		Utils::trim(requestBracket);
		if (is_number(requestBracket)) {
			i = std::stoi(requestBracket);
			i += 9;
		} else {
			ptrdiff_t index = std::distance(sample_names.begin(), std::find(sample_names.begin(), sample_names.end(), requestBracket));
			if ((int)index >= (int)sample_names.size()) {
				out << "Sample not in file: " << requestBracket << std::endl;
                out << "Samples listed in file are: ";
                for (auto &samp : sample_names) {
                    out << samp << " ";
                }
                out << std::endl;
				return;
			}
			i = (int)index + 9;
		}
	}

	void parse_INFO (std::string &result, std::string &infoColString, std::string &request) {
		if (request == "info") {
			result = infoColString;
			return;
		}
		request = Utils::split(request, '.')[1];
		std::vector<std::string> infoCol = Utils::split(infoColString, ';');
		for (auto it = begin (infoCol); it != end (infoCol); ++it) {
			std::string tmpLine = *it;
			std::vector<std::string> tmpVar = Utils::split(tmpLine, '=');
			if (tmpVar[0] == request) {
				if (tmpVar.size() == 2) {
					result = tmpVar[1];
				} else {
					result = "";
				}
			}
		}
	}

	void parse_FORMAT (std::string &result, std::vector<std::string> &vcfCols, std::string &request, std::vector<std::string> &sample_names, std::ostream& out) {
		if (request == "format" || request == "genome" || request == "format[0]") {
			result = vcfCols[9];
			return;
		}
		std::string requestBracket = request;
		int i = 0;
        convert_name_index(requestBracket, i, sample_names, out);
        if (i == 0 || i >= (int)vcfCols.size()) {
            throw std::invalid_argument("request was invalid");
		}

		const std::regex dot("\\.");
		std::smatch dotMatch;
		if (!std::regex_search(request, dot)) {
			result = vcfCols[i];
			return;
		}	
		request = Utils::split(request, '.')[1];
		std::vector<std::string> formatVars = Utils::split(vcfCols[8], ':');
		std::vector<std::string> formatVals = Utils::split(vcfCols[i], ':');
		int nvars = formatVars.size();
		for (int j = 0; j < nvars; ++j) {
			if (formatVars[j] == request) {
                result = formatVals[j];
                break;
            }
		}
	}


	void parse_vcf_split(std::string &result, std::vector<std::string> &vcfCols, std::string &request, std::vector<std::string> &sample_names, std::ostream& out) {
		if (request == "chrom") {
			result = vcfCols[0];
		} else if (request == "pos") {
			result = vcfCols[1];
		} else if (request == "id") {
			result = vcfCols[2];
		} else if (request == "ref") {
			result = vcfCols[3];
		} else if (request == "alt") {
			result = vcfCols[4];
		} else if (request == "qual") {
			result = vcfCols[5];
		} else if (request == "filter") {
			result = vcfCols[6];
		} else if (request == "info" || Utils::startsWith(request, "info.")) {
			parse_INFO(result, vcfCols[7], request);
		} else if (request == "format" || Utils::startsWith(request, "format.") || Utils::startsWith(request, "format[")) {
			parse_FORMAT(result, vcfCols, request, sample_names, out);
		} else {
            out << termcolor::red << "Error:" << termcolor::reset << " could not parse. Valid fields are chrom, pos, id, ref, alt, qual, filter, info, format\n";
        }
	}

	void create_expression(std::string &rexpr) {
		rexpr.insert(0, "\\");
		std::regex form("format");
		if (std::regex_search(rexpr, form)) {
			int open = rexpr.find("\\[");  // should be \\ ?
			rexpr.insert(open, "\\");
			int close = rexpr.find("\\]");
			rexpr.insert(close, "\\");
		}
		int rexprs = rexpr.size();
		rexpr.insert(rexprs-1, "\\");
	}

	void parse_sample_variable(std::string &fname, std::vector<std::string> &bam_paths) {
		for (auto it = begin (bam_paths); it != end (bam_paths); ++it) {
			std::string tmp_path = *it;
			std::string tmp_base_filename = tmp_path.substr(tmp_path.find_last_of("/\\") + 1);
			std::string::size_type const tmp_p(tmp_base_filename.find_last_of('.'));
			std::string tmp_file_without_extension = tmp_base_filename.substr(0, tmp_p);
			if (it != bam_paths.end()-1) {
				fname += tmp_file_without_extension + "_";
			} else {
				fname += tmp_file_without_extension;
			}
		}
	}

	void parse_output_name_format(std::string &nameFormat, std::vector<std::string> &vcfCols, std::vector<std::string> &sample_names, std::vector<std::string> &bam_paths, std::string &label, std::ostream& out) {
		std::regex bash("\\{(.*?)\\}");
		std::smatch matches;
		std::string test = nameFormat;
		auto b = std::sregex_iterator(test.begin(), test.end(), bash);
		auto e = std::sregex_iterator();
		std::string value;
		std::string rexprString;
		for (std::sregex_iterator i = b; i != e; ++i) {
			value = "";
			matches = *i;
			if (matches.size() == 2) {
				std::string tmpVal = matches[1];
				if (tmpVal != "sample" && tmpVal != "label") {
					parse_vcf_split(value, vcfCols, tmpVal, sample_names, out);
				} else if (tmpVal == "sample") {
					parse_sample_variable(value, bam_paths);
				} else if (tmpVal == "label") {
					value = label;
				}
                if (value.empty() || tmpVal == "info" || tmpVal == "format" || tmpVal == "format[0]") {
                    throw std::invalid_argument("Argument was invalid");
                }
				rexprString = matches[0];
				create_expression(rexprString);
				std::regex rexpr(rexprString);
				nameFormat = std::regex_replace(nameFormat, rexpr, value);
			}
		}
	}

    std::string tilde_to_home(std::string fpath) {
//        if (Utils::startsWith(fpath, "./")) {
//            fpath.erase(0, 2);
//            return fpath;
//        }
        if (!Utils::startsWith(fpath, "~/")) {
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
        std::string stringpath = path.generic_string();
        return stringpath;
    }

    std::string longestCommonPrefix(std::vector<std::string> ar) {
        if (ar.empty())
            return "";
        if (ar.size() == 1)
            return ar[0];
        std::sort(ar.begin(), ar.end());
        std::string first = ar.front(), last = ar.back();
        int end = std::min(first.size(), last.size());
        int i = 0;
        while (i < end && first[i] == last[i])
            ++i;
        std::string pre = first.substr(0, i);
        return pre;
    }

    void tryTabCompletion(std::string &inputText, std::ostream& out, int& charIndex) {

        std::vector<std::string> parts = Utils::split(inputText, ' ');
        if (parts.size() == 3) {  // chunk first two options
            std::string tmp = parts[0];
            std::string tmp2 = parts[1];
            std::string tmp3 = parts[2];
            parts[0] = tmp + " " + tmp2;
            parts[1] = tmp3;
            parts.resize(2);
        }
        if (parts.back()[0] == '~') {
            parts.back() = tilde_to_home(parts.back());
            charIndex = parts.front().size() + parts.back().size();
        }
        std::string globstr = parts.back() + "*";
        parts.back() = tilde_to_home(parts.back());
        std::vector<std::filesystem::path> glob_paths = glob_cpp::glob(globstr);
        if (glob_paths.size() == 1) {
            inputText = parts[0] + " " + glob_paths[0].generic_string();
            if (std::filesystem::is_directory(glob_paths[0])) {
                inputText += std::filesystem::path::preferred_separator;
            }
            charIndex = inputText.size();
            return;
        }
        std::vector<std::string> path_str;
        for (auto &item : glob_paths) {
            path_str.push_back(item.generic_string());
        }
        std::string lcp = longestCommonPrefix(path_str);
        if (lcp.empty()) {
            return;
        }
        inputText = parts[0] + " " + lcp;

        charIndex = inputText.size();
        size_t width = (size_t)Utils::get_terminal_width() - 1;
        size_t i = 0;
        for (auto s_path : glob_paths) {
            std::string s = s_path.filename().generic_string();
            if (s.size() < width) {
                out << s;
                width -= s.size();
            } else {
                if (width > 6 && s.size() > 6) {
                    s.erase(s.begin() + width - 4, s.end());
                    out << s << " ..."; out.flush();
                }
                break;
            }
            if (i < glob_paths.size() - 2 && width > 2) {
                out << "  "; out.flush();
                width -= 2;
            } else {
                break;
            }
        }
        return;
    }
}
