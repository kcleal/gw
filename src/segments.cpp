#include <cassert>
#include <chrono>
#include <algorithm>
#include <vector>
#include <fstream>

#include "segments.h"
#include "utils.h"
#include "plot_manager.h"


namespace Segs {

//    void get_md_block(const char *md_tag, int md_idx, int md_l, MdBlock *res) {
//        int nmatches = 0;
//        int del_length = 0;
//        bool is_mm = false;
//        while (md_idx < md_l) {
//            if (48 <= md_tag[md_idx] && md_tag[md_idx] <= 57) {  // c is numerical
//                nmatches = nmatches * 10 + md_tag[md_idx] - 48;
//                md_idx += 1;
//            } else {
//                if (md_tag[md_idx] == 94) {  // del_sign is 94 from ord('^')
//                    md_idx += 1;
//                    while (65 <= md_tag[md_idx] && md_tag[md_idx] <= 90) {
//                        md_idx += 1;
//                        del_length += 1;
//                    }
//                } else {  // save mismatch
//                    is_mm = true;
//                    md_idx += 1;
//                }
//                break;
//            }
//        }
//        res->matches = nmatches;
//        res->md_idx = md_idx;
//        res->is_mm = is_mm;
//        res->del_length = del_length;
//    }

//    void get_md_block(const char *md_tag, int md_idx, MdBlock *res, bool *done) {
//        int nmatches = 0;
//        int del_length = 0;
//        bool is_mm = false;
//        while (true) {
//            if (md_tag[md_idx] == '\0') {
//                *done = true;
//                return;
//            }
//            if (48 <= md_tag[md_idx] && md_tag[md_idx] <= 57) {  // c is numerical
//                nmatches = nmatches * 10 + md_tag[md_idx] - 48;
//                md_idx += 1;
//            } else {
//                if (md_tag[md_idx] == 94) {  // del_sign is 94 from ord('^')
//                    md_idx += 1;
//                    while (65 <= md_tag[md_idx] && md_tag[md_idx] <= 90) {
//                        md_idx += 1;
//                        del_length += 1;
//                    }
//                } else {  // save mismatch
//                    is_mm = true;
//                    md_idx += 1;
//                }
//                break;
//            }
//        }
//        res->matches = nmatches;
//        res->md_idx = md_idx;
//        res->is_mm = is_mm;
//        res->del_length = del_length;
//    }
//
//    void get_mismatched_bases(std::vector<MMbase> &result,
//                              const char *md_tag, uint32_t r_pos,
//                              const uint32_t ct_l, uint32_t *cigar_p) {
//        uint32_t opp, c_idx, s_idx, c_s_idx;
//
//        bool done = false;
//        std::vector<QueueItem> ins_q;
//        MdBlock md_block{};
//        get_md_block(md_tag, 0, &md_block, &done);
//
//        if (done) {
//            return;
//        }
//
//        c_idx = 0;  // the cigar index
//        s_idx = 0;  // sequence index of mismatches
//        c_s_idx = 0;  // the index of the current cigar (c_idx) on the input sequence
//
//        opp = cigar_p[0] & BAM_CIGAR_MASK;
//        if (opp == 4) {
//            c_idx += 1;
//            s_idx += cigar_p[0] >> BAM_CIGAR_SHIFT;
//            c_s_idx = s_idx;
//        } else if (opp == 5) {
//            c_idx += 1;
//        }
//
//        int ins_q_idx = 0;
//
//        while (true) {
//            // consume cigar until deletion reached, collect positions of insertions
//            while (c_idx < ct_l) {
//                opp = cigar_p[c_idx] & BAM_CIGAR_MASK;
//                if (opp == 0 || opp == 8) {  // match
//                    c_s_idx += cigar_p[c_idx] >> BAM_CIGAR_SHIFT;
//                } else if (opp == 1) {  // insertion
//                    ins_q.push_back({c_s_idx, cigar_p[c_idx] >> BAM_CIGAR_SHIFT});
//                    c_s_idx += cigar_p[c_idx] >> BAM_CIGAR_SHIFT;
//                } else {  // opp == 2 or opp == 4 or opp == 5
//                    break;  // now process insertions
//                }
//                ++c_idx;
//            }
//            c_idx += 1;
//
//            while (true) {   // consume mismatches from md tag until deletion reached
//                if (!md_block.is_mm) {  // deletion or end or md tag
//                    s_idx = c_s_idx;
//                    r_pos += md_block.matches + md_block.del_length;
//                    while (ins_q_idx < ins_q.size() &&
//                           s_idx + md_block.matches >= ins_q[ins_q_idx].c_s_idx) {  // catch up insertions
//                        ins_q_idx += 1;
//                    }
//                    get_md_block(md_tag, md_block.md_idx, &md_block, &done);
//                    if (done) {
//                        return;
//                    }
//                    break;
//                }
//                // process mismatch
//                while (ins_q_idx < ins_q.size() && s_idx + md_block.matches >= ins_q[ins_q_idx].c_s_idx) {
//                    s_idx += ins_q[ins_q_idx].l;
//                    ins_q_idx += 1;
//                }
//                s_idx += md_block.matches;
//                r_pos += md_block.matches;
//                result.push_back({s_idx, r_pos});
//                s_idx += 1;
//                r_pos += 1;
//                get_md_block(md_tag, md_block.md_idx, &md_block, &done);
//                if (done) {
//                    return;
//                }
//            }
//        }
//    }
//
//    QueueItem ins_q_stA[1000];
//
//    void get_mismatched_bases_stA(std::vector<MMbase> &result,
//                              const char *md_tag, uint32_t r_pos,
//                              uint32_t ct_l, uint32_t *cigar_p) {
//        uint32_t opp, c_idx, s_idx, c_s_idx;
//
//        //std::vector<QueueItem> ins_q;
//        bool done = false;
//
//        MdBlock md_block{};
//        get_md_block(md_tag, 0, &md_block, &done);
//        if (done) {
//            return;
//        }
////        if (md_block.md_idx == (uint32_t)md_l) {
////            return;
////        }
//
//        c_idx = 0;  // the cigar index
//        s_idx = 0;  // sequence index of mismatches
//        c_s_idx = 0;  // the index of the current cigar (c_idx) on the input sequence
//
//        opp = cigar_p[0] & BAM_CIGAR_MASK;
//        if (opp == 4) {
//            c_idx += 1;
//            s_idx += cigar_p[0] >> BAM_CIGAR_SHIFT;
//            c_s_idx = s_idx;
//        } else if (opp == 5) {
//            c_idx += 1;
//        }
//
//        int ins_q_idx = 0;
//        int ins_q_len = 0;
//
//        while (true) {
//            // consume cigar until deletion reached, collect positions of insertions
//            while (c_idx < ct_l) {
//                opp = cigar_p[c_idx] & BAM_CIGAR_MASK;
//                if (opp == 0 || opp == 8) {  // match
//                    c_s_idx += cigar_p[c_idx] >> BAM_CIGAR_SHIFT;
//                } else if (opp == 1) {  // insertion
//                    //ins_q.push_back({c_s_idx, cigar_p[c_idx] >> BAM_CIGAR_SHIFT});
//                    ins_q_stA[ins_q_len].c_s_idx = c_s_idx;
//                    ins_q_stA[ins_q_len].l = cigar_p[c_idx] >> BAM_CIGAR_SHIFT;
//                    ins_q_len += 1;
//                    c_s_idx += cigar_p[c_idx] >> BAM_CIGAR_SHIFT;
//                } else {  // opp == 2 or opp == 4 or opp == 5
//                    break;  // now process insertions
//                }
//                ++c_idx;
//            }
//            c_idx += 1;
//
//            while (true) {   // consume mismatches from md tag until deletion reached
//                if (!md_block.is_mm) {  // deletion or end or md tag
////                    if (md_block.md_idx == (uint32_t)md_l) {
////                        return;
////                    }
//                    s_idx = c_s_idx;
//                    r_pos += md_block.matches + md_block.del_length;
//                    while (ins_q_idx < ins_q_len &&
//                           s_idx + md_block.matches >= ins_q_stA[ins_q_idx].c_s_idx) {  // catch up insertions
//                        ins_q_idx += 1;
//                    }
//                    get_md_block(md_tag, md_block.md_idx, &md_block, &done);
//                    if (done) {
//                        return;
//                    }
//                    break;
//                }
//                // process mismatch
//                while (ins_q_idx < ins_q_len && s_idx + md_block.matches >= ins_q_stA[ins_q_idx].c_s_idx) {
//                    s_idx += ins_q_stA[ins_q_idx].l;
//                    ins_q_idx += 1;
//                }
//                s_idx += md_block.matches;
//                r_pos += md_block.matches;
//                result.push_back({s_idx, r_pos});
//                s_idx += 1;
//                r_pos += 1;
//                get_md_block(md_tag, md_block.md_idx, &md_block, &done);
//                if (done) {
//                    return;
//                }
//            }
//        }
//    }


    /* NOTE PARSING MODS SECTION BELOW IS FROM HTS-LIB
     *
     * Count frequency of A, C, G, T and N canonical bases in the sequence
     */
    #define MAX_BASE_MOD 256
    struct hts_base_mod_state {
        int type[MAX_BASE_MOD];     // char or minus-CHEBI
        int canonical[MAX_BASE_MOD];// canonical base, as seqi (1,2,4,8,15)
        char strand[MAX_BASE_MOD];  // strand of modification; + or -
        int MMcount[MAX_BASE_MOD];  // no. canonical bases left until next mod
        char *MM[MAX_BASE_MOD];     // next pos delta (string)
        char *MMend[MAX_BASE_MOD];  // end of pos-delta string
        uint8_t *ML[MAX_BASE_MOD];  // next qual
        int MLstride[MAX_BASE_MOD]; // bytes between quals for this type
        int implicit[MAX_BASE_MOD]; // treat unlisted positions as non-modified?
        int seq_pos;                // current position along sequence
        int nmods;                  // used array size (0 to MAX_BASE_MOD-1).
        uint32_t flags;             // Bit-field: see HTS_MOD_REPORT_UNCHECKED
    };

    static void seq_freq(const bam1_t *b, int freq[16]) {
        int i;
        memset(freq, 0, 16*sizeof(*freq));
        uint8_t *seq = bam_get_seq(b);
        for (i = 0; i < b->core.l_qseq; i++)
            freq[bam_seqi(seq, i)]++;
        freq[15] = b->core.l_qseq; // all bases count as N for base mods
    }

    //0123456789ABCDEF
    //=ACMGRSVTWYHKDBN  aka seq_nt16_str[]
    //=TGKCYSBAWRDMHVN  comp1ement of seq_nt16_str
    //084C2A6E195D3B7F
    static int seqi_rc[] = { 0,8,4,12,2,10,6,14,1,9,5,13,3,11,7,15 };

    /*
     * Parse the MM and ML tags to populate the base mod state.
     * This structure will have been previously allocated via
     * hts_base_mod_state_alloc, but it does not need to be repeatedly
     * freed and allocated for each new bam record. (Although obviously
     * it requires a new call to this function.)
     *
     * Flags are copied into the state and used to control reporting functions.
     * Currently the only flag is HTS_MOD_REPORT_UNCHECKED, to control whether
     * explicit "C+m?" mods report quality HTS_MOD_UNCHECKED for the bases
     * outside the explicitly reported region.
     */
    int bam_parse_basemod_gw(const bam1_t *b, hts_base_mod_state *state,
                           uint32_t flags) {
        // Reset position, else upcoming calls may fail on
        // seq pos - length comparison
        state->seq_pos = 0;
        state->nmods = 0;
        state->flags = flags;

        // Read MM and ML tags
        uint8_t *mm = bam_aux_get(b, "MM");
        if (!mm) mm = bam_aux_get(b, "Mm");
        if (!mm)
            return 0;
        if (mm[0] != 'Z') {
#ifdef DEBUG
            hts_log_error("%s: MM tag is not of type Z", bam_get_qname(b));
#endif
            return -1;
        }

        uint8_t *mi = bam_aux_get(b, "MN");
        if (mi && bam_aux2i(mi) != b->core.l_qseq && b->core.l_qseq) {
            // bam_aux2i with set errno = EINVAL and return 0 if the tag
            // isn't integer, but 0 will be a seq-length mismatch anyway so
            // triggers an error here too.
#ifdef DEBUG
            hts_log_error("%s: MM/MN data length is incompatible with"
                          " SEQ length", bam_get_qname(b));
#endif
            return -1;
        }

        uint8_t *ml = bam_aux_get(b, "ML");
        if (!ml) ml = bam_aux_get(b, "Ml");
        if (ml && (ml[0] != 'B' || ml[1] != 'C')) {
#ifdef DEBUG
            hts_log_error("%s: ML tag is not of type B,C", bam_get_qname(b));
#endif
            return -1;
        }
        uint8_t *ml_end = ml ? ml+6 + le_to_u32(ml+2) : NULL;
        if (ml) ml += 6;

        // Aggregate freqs of ACGTN if reversed, to get final-delta (later)
        int freq[16];
        if (b->core.flag & BAM_FREVERSE)
            Segs::seq_freq(b, freq);

        char *cp = (char *)mm+1;
        int mod_num = 0;
        int implicit = 1;
        while (*cp) {
            for (; *cp; cp++) {
                // cp should be [ACGTNU][+-]([a-zA-Z]+|[0-9]+)[.?]?(,\d+)*;
                unsigned char btype = *cp++;

                if (btype != 'A' && btype != 'C' &&
                    btype != 'G' && btype != 'T' &&
                    btype != 'U' && btype != 'N')
                    return -1;
                if (btype == 'U') btype = 'T';

                btype = seq_nt16_table[btype];

                // Strand
                if (*cp != '+' && *cp != '-')
                    return -1; // malformed
                char strand = *cp++;

                // List of modification types
                char *ms = cp, *me; // mod code start and end
                char *cp_end = NULL;
                int chebi = 0;
                if (std::isdigit(*cp)) {
                    chebi = strtol(cp, &cp_end, 10);
                    cp = cp_end;
                    ms = cp-1;
                } else {
                    while (*cp && std::isalpha(*cp))
                        cp++;
                    if (*cp == '\0')
                        return -1;
                }

                me = cp;

                // Optional explicit vs implicit marker
                implicit = 1;
                if (*cp == '.') {
                    // default is implicit = 1;
                    cp++;
                } else if (*cp == '?') {
                    implicit = 0;
                    cp++;
                } else if (*cp != ',' && *cp != ';') {
                    // parse error
                    return -1;
                }

                long delta;
                int n = 0; // nth symbol in a multi-mod string
                int stride = me-ms;
                int ndelta = 0;

                if (b->core.flag & BAM_FREVERSE) {
                    // We process the sequence in left to right order,
                    // but delta is successive count of bases to skip
                    // counting right to left.  This also means the number
                    // of bases to skip at left edge is unrecorded (as it's
                    // the remainder).
                    //
                    // To output mods in left to right, we step through the
                    // MM list in reverse and need to identify the left-end
                    // "remainder" delta.
                    int total_seq = 0;
                    for (;;) {
                        cp += (*cp == ',');
                        if (*cp == 0 || *cp == ';')
                            break;

                        delta = strtol(cp, &cp_end, 10);
                        if (cp_end == cp) {
#ifdef DEBUG
                            hts_log_error("%s: Hit end of MM tag. Missing "
                                          "semicolon?", bam_get_qname(b));
#endif
                            return -1;
                        }

                        cp = cp_end;
                        total_seq += delta+1;
                        ndelta++;
                    }
                    delta = freq[seqi_rc[btype]] - total_seq; // remainder
                } else {
                    delta = *cp == ','
                            ? strtol(cp+1, &cp_end, 10)
                            : 0;
                    if (!cp_end) {
                        // empty list
                        delta = INT_MAX;
                        cp_end = cp;
                    }
                }
                // Now delta is first in list or computed remainder,
                // and cp_end is either start or end of the MM list.
                while (ms < me) {
                    state->type     [mod_num] = chebi ? -chebi : *ms;
                    state->strand   [mod_num] = (strand == '-');
                    state->canonical[mod_num] = btype;
                    state->MLstride [mod_num] = stride;
                    state->implicit [mod_num] = implicit;

                    if (delta < 0) {
#ifdef DEBUG
                        hts_log_error("%s: MM tag refers to bases beyond sequence "
                                      "length", bam_get_qname(b));
#endif
                        return -1;
                    }
                    state->MMcount  [mod_num] = delta;
                    if (b->core.flag & BAM_FREVERSE) {
                        state->MM   [mod_num] = me+1;
                        state->MMend[mod_num] = cp_end;
                        state->ML   [mod_num] = ml ? ml+n +(ndelta-1)*stride: NULL;
                    } else {
                        state->MM   [mod_num] = cp_end;
                        state->MMend[mod_num] = NULL;
                        state->ML   [mod_num] = ml ? ml+n : NULL;
                    }

                    if (++mod_num >= MAX_BASE_MOD) {
#ifdef DEBUG
                        hts_log_error("%s: Too many base modification types",
                                      bam_get_qname(b));
#endif
                        return -1;
                    }
                    ms++; n++;
                }

                // Skip modification deltas
                if (ml) {
                    if (b->core.flag & BAM_FREVERSE) {
                        ml += ndelta*stride;
                    } else {
                        while (*cp && *cp != ';') {
                            if (*cp == ',')
                                ml+=stride;
                            cp++;
                        }
                    }
                    if (ml > ml_end) {
#ifdef DEBUG
                        hts_log_error("%s: Insufficient number of entries in ML "
                                      "tag", bam_get_qname(b));
#endif
                        return -1;
                    }
                } else {
                    // cp_end already known if FREVERSE
                    if (cp_end && (b->core.flag & BAM_FREVERSE))
                        cp = cp_end;
                    else
                        while (*cp && *cp != ';')
                            cp++;
                }
                if (!*cp) {
#ifdef DEBUG
                    hts_log_error("%s: Hit end of MM tag. Missing semicolon?",
                                  bam_get_qname(b));
#endif
                    return -1;
                }
            }
        }
        if (ml && ml != ml_end) {
#ifdef DEBUG
            hts_log_error("%s: Too many entries in ML tag", bam_get_qname(b));
#endif
            return -1;
        }

        state->nmods = mod_num;

        return 0;
    }

    int bam_mods_at_next_pos(const bam1_t *b, hts_base_mod_state *state,
                             hts_base_mod *mods, int n_mods) {

        if (b->core.flag & BAM_FREVERSE) {
            if (state->seq_pos < 0)
                return -1;
        } else {
            if (state->seq_pos >= b->core.l_qseq)
                return -1;
        }

        int i, j, n = 0;
        unsigned char base = bam_seqi(bam_get_seq(b), state->seq_pos);
        state->seq_pos++;
        if (b->core.flag & BAM_FREVERSE)
            base = seqi_rc[base];

        for (i = 0; i < state->nmods; i++) {
            int unchecked = 0;
            if (state->canonical[i] != base && state->canonical[i] != 15/*N*/)
                continue;

            if (state->MMcount[i]-- > 0) {
                if (!state->implicit[i] &&
                    (state->flags & HTS_MOD_REPORT_UNCHECKED))
                    unchecked = 1;
                else
                    continue;
            }

            char *MMptr = state->MM[i];
            if (n < n_mods) {
                mods[n].modified_base = state->type[i];
                mods[n].canonical_base = seq_nt16_str[state->canonical[i]];
                mods[n].strand = state->strand[i];
                mods[n].qual = unchecked
                               ? HTS_MOD_UNCHECKED
                               : (state->ML[i] ? *state->ML[i] : HTS_MOD_UNKNOWN);
            }
            n++;

            if (unchecked)
                continue;

            if (state->ML[i])
                state->ML[i] += (b->core.flag & BAM_FREVERSE)
                                ? -state->MLstride[i]
                                : +state->MLstride[i];

            if (b->core.flag & BAM_FREVERSE) {
                // process MM list backwards
                char *cp;
                if (state->MMend[i]-1 < state->MM[i]) {
                    // Should be impossible to hit if coding is correct
#ifdef DEBUG
                    hts_log_error("Assert failed while processing base modification states");
#endif
                    return -1;
                }
//                Old code here:
//                for (cp = state->MMend[i]-1; cp != state->MM[i]; cp--)
//                    if (*cp == ',')
//                        break;

                if (i >= 0) {
                    char *cp_begin = state->MMend[0];
                    char *cp2 = state->MMend[i];
                    if (cp_begin == nullptr || cp2 == nullptr) {
                        return -1;
                    }
                    cp = state->MMend[i]-1;
                    while (cp != cp_begin && (cp == cp2 || *cp != ',')) {
                        --cp;
                        --cp2;
                    }
                } else {
                    return -1;
                }

                state->MMend[i] = cp;
                if (cp != state->MM[i])
                    state->MMcount[i] = strtol(cp+1, NULL, 10);
                else
                    state->MMcount[i] = INT_MAX;
            } else {
                if (*state->MM[i] == ',')
                    state->MMcount[i] = strtol(state->MM[i]+1, &state->MM[i], 10);
                else
                    state->MMcount[i] = INT_MAX;
            }

            // Multiple mods at the same coords.
            for (j=i+1; j < state->nmods && state->MM[j] == MMptr; j++) {
                if (n < n_mods) {
                    mods[n].modified_base = state->type[j];
                    mods[n].canonical_base = seq_nt16_str[state->canonical[j]];
                    mods[n].strand = state->strand[j];
                    mods[n].qual = state->ML[j] ? *state->ML[j] : -1;
                }
                n++;
                state->MMcount[j] = state->MMcount[i];
                state->MM[j]      = state->MM[i];
                if (state->ML[j])
                    state->ML[j] += (b->core.flag & BAM_FREVERSE)
                                    ? -state->MLstride[j]
                                    : +state->MLstride[j];
            }
            i = j-1;
        }
        return n;
    }

    int bam_next_basemod(const bam1_t *b, hts_base_mod_state *state,
                         hts_base_mod *mods, int n_mods, int *pos) {
        // Look through state->MMcount arrays to see when the next lowest is
        // per base type;

        int next[16], freq[16] = {0}, i;
        memset(next, 0x7f, 16*sizeof(*next));
        const int unchecked = state->flags & HTS_MOD_REPORT_UNCHECKED;
        if (b->core.flag & BAM_FREVERSE) {
            for (i = 0; i < state->nmods; i++) {
                if (unchecked && !state->implicit[i])
                    next[seqi_rc[state->canonical[i]]] = 1;
                else if (next[seqi_rc[state->canonical[i]]] > state->MMcount[i])
                    next[seqi_rc[state->canonical[i]]] = state->MMcount[i];
            }
        } else {
            for (i = 0; i < state->nmods; i++) {
                if (unchecked && !state->implicit[i])
                    next[state->canonical[i]] = 0;
                else if (next[state->canonical[i]] > state->MMcount[i])
                    next[state->canonical[i]] = state->MMcount[i];
            }
        }

        // Now step through the sequence counting off base types.
        for (i = state->seq_pos; i < b->core.l_qseq; i++) {
            unsigned char bc = bam_seqi(bam_get_seq(b), i);
            if (next[bc] <= freq[bc] || next[15] <= freq[15])
                break;
            freq[bc]++;
            if (bc != 15) // N
                freq[15]++;
        }
        *pos = state->seq_pos = i;

        if (b->core.flag & BAM_FREVERSE) {
            for (i = 0; i < state->nmods; i++)
                state->MMcount[i] -= freq[seqi_rc[state->canonical[i]]];
        } else {
            for (i = 0; i < state->nmods; i++)
                state->MMcount[i] -= freq[state->canonical[i]];
        }

        if (b->core.l_qseq && state->seq_pos >= b->core.l_qseq &&
            !(b->core.flag & BAM_FREVERSE)) {
            // Spots +ve orientation run-overs.
            // The -ve orientation is spotted in bam_parse_basemod2
            int i;
            for (i = 0; i < state->nmods; i++) {
                // Check if any remaining items in MM after hitting the end
                // of the sequence.
                if (state->MMcount[i] < 0x7f000000 ||
                    (*state->MM[i]!=0 && *state->MM[i]!=';')) {
#ifdef DEBUG
                    hts_log_warning("MM tag refers to bases beyond sequence length");
#endif
                    return -1;
                }
            }
            return 0;
        }
        int r = bam_mods_at_next_pos(b, state, mods, n_mods);
        return r > 0 ? r : 0;
    }

    constexpr uint32_t PP_RR_MR = 50;

    alignas(64) constexpr std::array<Pattern, 49> posFirst = {INV_F, u, u, u, u, u, u, u,
                                                  u, u, u, u, u, u, u, u,
                                                  DUP, u, u, u, u, u, u, u,
                                                  u, u, u, u, u, u, u, u,
                                                  DEL, u, u, u, u, u, u, u,
                                                  u, u, u, u, u, u, u, u,
                                                  INV_R};
    alignas(64) constexpr std::array<Pattern, 49> mateFirst = {INV_R, u, u, u, u, u, u, u,
                                                  u, u, u, u, u, u, u, u,
                                                  DEL, u, u, u, u, u, u, u,
                                                  u, u, u, u, u, u, u, u,
                                                  DUP, u, u, u, u, u, u, u,
                                                  u, u, u, u, u, u, u, u,
                                                  INV_F};

    void align_init(Align *self, const int parse_mods_threshold, const bool add_clip_space) {
//        auto start = std::chrono::high_resolution_clock::now();
        bam1_t *src = self->delegate;

        self->pos = src->core.pos;

        uint32_t pos, l, cigar_l, op, k;
        uint32_t *cigar_p;

        cigar_l = src->core.n_cigar;

        pos = src->core.pos;
        cigar_p = bam_get_cigar(src);

        self->left_soft_clip = 0;
        self->right_soft_clip = 0;

        uint32_t seq_index = 0;

        self->any_ins.reserve(cigar_l);
        self->blocks.reserve(cigar_l);

        for (k = 0; k < cigar_l; k++) {
            op = cigar_p[k] & BAM_CIGAR_MASK;
            l = cigar_p[k] >> BAM_CIGAR_SHIFT;

            switch (op) {
                case BAM_CMATCH: case BAM_CEQUAL: case BAM_CDIFF:
                    self->blocks.emplace_back() = {pos, pos+l, seq_index};
                    pos += l;
                    seq_index += l;
                    break;
                case BAM_CINS:
                    self->any_ins.push_back({pos, l});
                    seq_index += l;
                    break;
                case BAM_CDEL:
                    pos += l;
                    break;
                case BAM_CREF_SKIP:
                    op = BAM_CDEL;
                    pos += l;
                    break;
                case BAM_CSOFT_CLIP:
                    if (k == 0) {
                        self->left_soft_clip = (int)l;
                        seq_index += l;
                    } else {
                        self->right_soft_clip = (int)l;
                    }
                    break;
//                case BAM_CHARD_CLIP: case BAM_CPAD: case BAM_CBACK:
//                    break;  // do something for these?
                default:
                    break;
            }
        }
        self->reference_end = self->blocks.back().end;
        if (add_clip_space) {
            self->cov_start = (int)self->pos - self->left_soft_clip;
            self->cov_end = (int)self->reference_end + self->right_soft_clip;
        } else {
            self->cov_start = (int)self->pos;
            self->cov_end = (int)self->reference_end;
        }

        uint32_t flag = src->core.flag;

        if (bam_aux_get(self->delegate, "SA") != nullptr) {
            self->has_SA = true;
        } else {
            self->has_SA = false;
        }

        if (parse_mods_threshold > 0) {
            hts_base_mod_state* mod_state = new hts_base_mod_state;
            int res = bam_parse_basemod_gw(src, mod_state, 0);
            if (res >= 0) {
                hts_base_mod mods[10];
                int pos = 0;  // position on read, not reference
                int nm = bam_next_basemod(src, mod_state, mods, 10, &pos);
                while (nm > 0) {
                    self->any_mods.emplace_back() = ModItem();
                    ModItem& mi = self->any_mods.back();
                    mi.index = pos;
                    size_t j=0;
                    for (size_t m=0; m < std::min((size_t)4, (size_t)nm); ++m) {
                        if (mods[m].qual >= parse_mods_threshold) {
                            mi.mods[j] = (char)mods[m].modified_base;
                            mi.quals[j] = (uint8_t)mods[m].qual;
                            mi.strands[j] = (bool)mods[m].strand;
                            j += 1;
                        }
                    }
                    mi.n_mods = (uint8_t)j;
                    nm = bam_next_basemod(src, mod_state, mods, 10, &pos);
                }
            }
            delete mod_state;
        }

        self->y = -1;  // -1 has no level, -2 means initialized but filtered
        if (self->blocks.empty()) {
            self->y = -2;
        }

        if (flag & 1) {  // paired-end
            if (src->core.tid != src->core.mtid) {
                self->orient_pattern = TRA;
            } else {
                // PP_RR_MR = proper-pair, read-reverse, mate-reverse flags
                // 00110010 = 0010       , 00010000    , 00100000
                uint32_t info = flag & PP_RR_MR;
                if (self->pos <= src->core.mpos) {
                    self->orient_pattern = posFirst[info];
                } else {
                    self->orient_pattern = mateFirst[info];
                }
            }
        } else { // single-end
            self->orient_pattern = Segs::Pattern::NORMAL;
        }

        if (self->has_SA || flag & 2048) {
            self->edge_type = 2;  // "SPLIT"
        } else if (flag & 8) {
            self->edge_type = 3;  // "MATE_UNMAPPED"
        } else {
            self->edge_type = 1;  // "NORMAL"
        }
//        auto stop = std::chrono::high_resolution_clock::now();
//        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start);
    }

    void align_clear(Align *self) {
        self->blocks.clear();
        self->any_ins.clear();
    }

    void init_parallel(std::vector<Align> &aligns, const int n, BS::thread_pool &pool,
        const int parse_mods_threshold, const bool add_clip_space) {
        if (n == 1) {
            for (auto &aln : aligns) {
                align_init(&aln, parse_mods_threshold, add_clip_space);
            }
        } else {
            pool.parallelize_loop(0, aligns.size(),
                                  [&aligns, parse_mods_threshold, add_clip_space]
                                  (const int a, const int b) {
                                      for (int i = a; i < b; ++i)
                                          align_init(&aligns[i], parse_mods_threshold, add_clip_space);
                                  })
                    .wait();
        }
    }

    constexpr int POS_MASK = 0b0000111;

    void setAlignSortCode(Align &a, Utils::SortType sort_state, int target_pos, char ref_base) {
        // strand and hap codes are stored in 2nd and 3rd bit
        // If the base is non-reference, then its number encoding is added after 6th bit
        // Makes it possible to sort using this bit field
        bam1_t* b = a.delegate;
        a.sort_tag = Utils::SortType::NONE;
        if (b->core.l_qseq == 0) {
            return;
        }
        if (sort_state == Utils::SortType::HP) {
            uint8_t *HP_tag = bam_aux_get(b, "HP");
            a.sort_tag = (HP_tag != nullptr) ? (int) bam_aux2i(HP_tag) : 0;
            return;
        } else if (sort_state == Utils::SortType::STRAND) {
            a.sort_tag = (int) (b->core.flag & BAM_FREVERSE) ? 1 : 0;
            return;
        } else if (sort_state == Utils::SortType::HP_AND_POS) {
            uint8_t *HP_tag = bam_aux_get(b, "HP");
            a.sort_tag = (HP_tag != nullptr) ? (int) bam_aux2i(HP_tag) : 0;
        } else if (sort_state == Utils::SortType::STRAND_AND_POS)  {
            a.sort_tag = (int) (b->core.flag & BAM_FREVERSE) ? 1 : 0;
        }
        if (((sort_state & Utils::SortType::POS) == 0) ||  b->core.pos > target_pos || target_pos < 0 || ref_base == '\0') {
            return;
        }

        uint32_t *cigar = bam_get_cigar(b);
        if (!cigar) {
            return;
        }
        uint8_t *seq = bam_get_seq(b);
        int pos = b->core.pos;
        int seq_index = 0;
        for (uint32_t k = 0; k < b->core.n_cigar; ++k) {
            uint32_t op = bam_cigar_op(cigar[k]);
            int l = bam_cigar_oplen(cigar[k]);
            switch (op) {
                case BAM_CMATCH: case BAM_CEQUAL: case BAM_CDIFF:
                    if (pos + l > target_pos) {
                        int offset = target_pos - pos - 1;
                        char this_base = seq_nt16_str[bam_seqi(seq, seq_index + offset)];
                        if (this_base == ref_base) {
                            return;
                        }
                        a.sort_tag |= (int)this_base << 6; // preserve first 6 bits
                        return;
                    }
                    pos += l;
                    seq_index += l;
                    break;
                case BAM_CINS:
                    seq_index += l;
                    if (pos == target_pos - 1) {
                        a.sort_tag |= (l + 10000000) << 6;
                    }
                    break;
                case BAM_CDEL: case BAM_CREF_SKIP:
                    pos += l;
                    if (pos >= target_pos) {
                        a.sort_tag |= (l + 100) << 6;
                    }
                    break;
                case BAM_CSOFT_CLIP:
                    seq_index += l;
                    break;
                default: //case BAM_CHARD_CLIP: case BAM_CPAD: case BAM_CBACK:
                    break;
            }
            if (pos > target_pos) {
                return;
            }
        }
    }

    int getSortCodes(std::vector<Align> &aligns, const int threads, BS::thread_pool &pool, Utils::Region *region) {
        // Lazily returns a sort code for POS, or returns other sort code
        Utils::SortType sort_state = region->getSortOption();
        if (sort_state == 0) {
            return 0;
        }
//        if (n == 1) {
            for (auto &aln : aligns) {
                setAlignSortCode(aln, sort_state, region->sortPos, region->refBaseAtPos);
            }
//        } else {
//        }
        return sort_state;
    }

//    EXPORT ReadCollection::ReadCollection() {
//        vScroll = 0;
//        bamIdx = 0;
//        regionIdx = 0;
//        collection_processed = false;
//        skipDrawingReads = false;
//        skipDrawingCoverage = false;
//    }

    void ReadCollection::makeEmptyMMArray() {
        if (region == nullptr) {
            return;
        }
        mmVector.resize(region->end - region->start + 1);
        Mismatches empty_mm = {0, 0, 0, 0};
        std::fill(mmVector.begin(), mmVector.end(), empty_mm);
    }

    void ReadCollection::clear() {
        std::fill(levelsStart.begin(), levelsStart.end(), 1215752191);
        std::fill(levelsEnd.begin(), levelsEnd.end(), 0);
        std::fill(covArr.begin(), covArr.end(), 0);
        linked.clear();
        collection_processed = false;
        if (ownsBamPtrs) {
            for (auto &item: readQueue) {
                bam_destroy1(item.delegate);
            }
        }
        readQueue.clear();
    }

    void ReadCollection::resetDrawState() {
        skipDrawingReads = false;
        skipDrawingCoverage = false;
    }

    // Add or remove soft-clip space for alignments
    void ReadCollection::modifySOftClipSpace(bool add_soft_clip_space) {
        for (auto &align : readQueue) {
            if (align.left_soft_clip) {
                if (add_soft_clip_space && (int)align.pos == align.cov_start) {
                    align.cov_start -= align.left_soft_clip;
                } else if (!add_soft_clip_space && (int)align.pos > align.cov_start) {
                    align.cov_start = (int)align.pos;
                }
            }
            if (align.right_soft_clip) {
                if (add_soft_clip_space && (int)align.reference_end == align.cov_end) {
                    align.cov_end += align.right_soft_clip;
                } else if (!add_soft_clip_space && (int)align.reference_end < align.cov_end) {
                    align.cov_end = align.reference_end;
                }
            }
        }
    }

    void resetCovStartEnd(ReadCollection &cl) {
        for (auto &a: cl.readQueue) {
            a.cov_start = (a.left_soft_clip == 0) ? (int)a.pos : (int)a.pos - a.left_soft_clip;
            a.cov_end = (a.right_soft_clip == 0) ? (int)a.reference_end : (int)a.reference_end + a.right_soft_clip;
            if (a.delegate->core.flag & 16) {
                a.cov_start -= 1;
            } else {
                a.cov_end += 1;
            }
        }
    }

    void addToCovArray(std::vector<int> &arr, const Align &align, const uint32_t begin, const uint32_t end) noexcept {
        size_t n_blocks = align.blocks.size();
        for (size_t idx=0; idx < n_blocks; ++idx) {
            uint32_t block_s = align.blocks[idx].start;
            if (block_s >= end) { break; }
            uint32_t block_e = align.blocks[idx].end;
            if (block_e < begin) { continue; }
            uint32_t s = std::max(block_s, begin) - begin;
            uint32_t e = std::min(block_e, end) - begin;
            arr[s] += 1;
            arr[e] -= 1;
        }
    }

    void findYWithSort(ReadCollection &rc, std::vector<Align> &rQ, std::vector<int> &ls, std::vector<int> &le, bool joinLeft,
                       int vScroll, Segs::map_t &lm, ankerl::unordered_dense::map< std::string, int >& linkedSeen,
                       int linkType, int ylim) {
        // sorting by strand or haplotype is a categorical sort that separates reads into groups
        bool re_sort = false;  // sort the level names (strand/haplotype)

        for (const auto& r : rc.readQueue) {
            bool has = false;
            for (const auto & c : rc.sortLevels) {
                if (c == (r.sort_tag & POS_MASK)) {
                    has = true;
                    break;
                }
            }
            if (!has) {
                rc.sortLevels.push_back(r.sort_tag & POS_MASK);
                re_sort = true;
            }
        }
        int n_cats = (int)rc.sortLevels.size();
        if (n_cats == 0) {
            return;
        }
        if (re_sort) {
            std::sort(rc.sortLevels.begin(), rc.sortLevels.end());
        }
        if ((int)ls.size() != ylim + (vScroll * n_cats)) {
            ls.resize(ylim + (vScroll * n_cats), 1215752191);
            le.resize(ylim + (vScroll * n_cats), 0);
        }

        size_t step = ls.size() / n_cats;
        if ( step == 0) {
            return;
        }

        ankerl::unordered_dense::map< int, int > to_level;
        int v = 0;
        for (const auto & c: rc.sortLevels) {
            to_level[c] = v * step;
            ++v;
        }

        // The ls and le arrays will be partitioned into each sort level.
        int qLen = (int)rQ.size();
        int stopCondition, move, si;
        Align *q_ptr;
        const char *qname = nullptr;
        if (!joinLeft) {
            si = 0;
            stopCondition = qLen;
            move = 1;
            q_ptr = &rQ.front();
        } else {
            si = qLen - 1;
            stopCondition = -1;
            move = -1;
            q_ptr = &rQ.back();
        }
        int i;
        assert (q_ptr->y < 0);
        while (si != stopCondition) {
            si += move;
            if (q_ptr->y == -2) {
                q_ptr += move;
                continue;
            }
            if (linkType > 0) {
                qname = bam_get_qname(q_ptr->delegate);
                if (qname != nullptr && linkedSeen.find(qname) != linkedSeen.end()) {
                    q_ptr->y = linkedSeen[qname];
                    q_ptr += move;
                    continue;
                }
            }

            int cat = q_ptr->sort_tag & POS_MASK;

            int start_i = to_level[cat];  // start of the range
            int vScroll_level = start_i + vScroll;  // if y >= this level then they will be displayed
            int end_i = start_i + step - 1;  // end of the range for this cat

            if (!joinLeft) {

                for (i=start_i; i < end_i; ++i) {
                    if (q_ptr->cov_start > le[i]) {
                        le[i] = q_ptr->cov_end;
                        if (q_ptr->cov_start < ls[i]) {
                            ls[i] = q_ptr->cov_start;
                        }
                        if (i >= vScroll_level) {
                            q_ptr->y = i - (vScroll*(cat+1));
                        }
                        if (linkType > 0 && qname != nullptr && lm.find(qname) != lm.end()) {
                            linkedSeen[qname] = q_ptr->y;
                        }
                        break;
                    }
                }
                if (i == end_i && linkType > 0 && qname != nullptr && lm.find(qname) != lm.end()) {
                    linkedSeen[qname] = q_ptr->y;  // y is out of range i.e. -1
                }
                q_ptr += move;

            } else {
                for (i=start_i; i < end_i; ++i) {
                    if (q_ptr->cov_end < ls[i]) {
                        ls[i] = q_ptr->cov_start;
                        if (q_ptr->cov_end > le[i]) {
                            le[i] = q_ptr->cov_end;
                        }
                        if (i >= vScroll_level) {
                            q_ptr->y = i - (vScroll*(cat+1));
                        }
                        if (linkType > 0 && qname != nullptr && lm.find(qname) != lm.end()) {
                            linkedSeen[qname] = q_ptr->y;
                        }
                        break;
                    }
                }
                if (i == end_i && linkType > 0 && qname != nullptr && lm.find(qname) != lm.end()) {
                    linkedSeen[qname] = q_ptr->y;  // y is out of range i.e. -1
                }
                q_ptr += move;
            }
        }
    }

    void alignFindYForward(Align &a, std::vector<int> &ls, std::vector<int> &le, int vScroll) {
        if (a.y == -2) {
            return;
        }
        for (int i=0; i < (int)le.size(); ++i) {
            if (a.cov_start > le[i]) {
                le[i] = a.cov_end;
                if (a.cov_start < ls[i]) {
                    ls[i] = a.cov_start;
                }
                if (i >= vScroll) {
                    a.y = i - vScroll;
                }
                return;
            }
        }
    }

    void findYNoSortForward(std::vector<Align> &rQ, std::vector<int> &ls, std::vector<int> &le, int vScroll) {
        for (auto &a : rQ) {
            alignFindYForward(a, ls, le, vScroll);
        }
    }


    void findYNoSort(std::vector<Align> &rQ, std::vector<int> &ls, std::vector<int> &le, bool joinLeft,
                     int vScroll, Segs::map_t &lm, ankerl::unordered_dense::map< std::string, int >& linkedSeen,
                     int linkType) {
        int qLen = (int)rQ.size();
        int stopCondition, move, si;
        int memLen = (int)ls.size();
        Align *q_ptr;
        const char *qname = nullptr;
        if (!joinLeft) {
            si = 0;
            stopCondition = qLen;
            move = 1;
            q_ptr = &rQ.front();
        } else {
            si = qLen - 1;
            stopCondition = -1;
            move = -1;
            q_ptr = &rQ.back();
        }
        int i;
        while (si != stopCondition) {
            si += move;
            if (q_ptr->y == -2) {
                q_ptr += move;
                continue;
            }
            if (linkType > 0) {
                qname = bam_get_qname(q_ptr->delegate);
                if (qname != nullptr && linkedSeen.find(qname) != linkedSeen.end()) {
                    q_ptr->y = linkedSeen[qname];
                    q_ptr += move;
                    continue;
                }
            }
            if (!joinLeft) {
                for (i=0; i < memLen; ++i) {
                    if (q_ptr->cov_start > le[i]) {
                        le[i] = q_ptr->cov_end;
                        if (q_ptr->cov_start < ls[i]) {
                            ls[i] = q_ptr->cov_start;
                        }
                        if (i >= vScroll) {
                            q_ptr->y = i - vScroll;
                        }
                        if (linkType > 0 && qname != nullptr && lm.find(qname) != lm.end()) {
                            linkedSeen[qname] = q_ptr->y;
                        }
                        break;
                    }
                }
                if (i == memLen && linkType > 0 && qname != nullptr && lm.find(qname) != lm.end()) {
                    linkedSeen[qname] = q_ptr->y;  // y is out of range i.e. -1
                }
                q_ptr += move;

            } else {
                for (i=0; i < memLen; ++i) {
                    if (q_ptr->cov_end < ls[i]) {
                        ls[i] = q_ptr->cov_start;
                        if (q_ptr->cov_end > le[i]) {
                            le[i] = q_ptr->cov_end;
                        }
                        if (i >= vScroll) {
                            q_ptr->y = i - vScroll;
                        }
                        if (linkType > 0 && qname != nullptr && lm.find(qname) != lm.end()) {
                            linkedSeen[qname] = q_ptr->y;
                        }
                        break;
                    }
                }
                if (i == memLen && linkType > 0 && qname != nullptr && lm.find(qname) != lm.end()) {
                    linkedSeen[qname] = q_ptr->y;  // y is out of range i.e. -1
                }
                q_ptr += move;
            }
        }
    }

    int findY(ReadCollection &rc, std::vector<Align> &rQ, int linkType, Themes::IniOptions &opts, bool joinLeft, int sortReadsBy) {
        if (rQ.empty()) {
            return 0;
        }
        int samMaxY;

        int vScroll = rc.vScroll;
        Align *q_ptr;
        const char *qname = nullptr;
        Segs::map_t &lm = rc.linked;  // pointers to alignments with same qname
        ankerl::unordered_dense::map< std::string, int > linkedSeen;  // Mapping of qname to y value

        int i;
        // first find reads that should be linked together using qname
        if (linkType > 0) {
            lm.clear();
            q_ptr = &rQ.front();
            // find the start and end coverage locations of aligns with same name
            for (i=0; i < (int)rQ.size(); ++i) {
                if (!(q_ptr->delegate->core.flag & 1)) {
                    ++q_ptr;
                    continue;
                }
                qname = bam_get_qname(q_ptr->delegate);
                if (qname == nullptr) {
                    ++q_ptr;
                    continue;
                }
                if (linkType == 1) {
                    uint32_t flag = q_ptr->delegate->core.flag;
                    if (q_ptr->has_SA || ~flag & 2) {
                        lm[qname].push_back(q_ptr);
                    }
                } else {
                    lm[qname].push_back(q_ptr);
                }
                ++q_ptr;
            }

            if (opts.link_op > 0) {
                for (auto &v: rc.readQueue) {  // y value will be reset
                    if (v.y != -2) {
                        v.y = -1;
                    }
                }
            }

            // set all aligns with same name to have the same start and end coverage locations
            for (auto const &keyVal : lm) {
                const std::vector<Align *> &ind = keyVal.second;
                int size = (int)ind.size();
                if (size > 1) {
                    uint32_t cs = ind.front()->cov_start;
                    uint32_t ce = ind.back()->cov_end;
                    for (auto const &j : ind) {
                        j->cov_start = cs;
                        j->cov_end = ce;
                    }
                }
            }
        }

        if (opts.tlen_yscale) {
            int max_bound = opts.max_tlen;
            samMaxY = max_bound;
            for (auto &aln : rQ) {
                int tlen = (int)std::abs(aln.delegate->core.isize);
                if (tlen < max_bound) {
                    aln.y = tlen;
                } else {
                    aln.y = max_bound;
                }
            }
            return samMaxY;
        }

        std::vector<int> &ls = rc.levelsStart;
        std::vector<int> &le = rc.levelsEnd;
        if (sortReadsBy == Utils::SortType::NONE) {
            if (ls.empty()) {
                ls.resize(opts.ylim + vScroll, 1215752191);
                le.resize(opts.ylim + vScroll, 0);
            }
            findYNoSort(rQ, ls, le, joinLeft, vScroll, lm, linkedSeen, linkType);
        } else if (sortReadsBy >= Utils::SortType::POS) {
            // sorting by position maintains all reads in the same plot region
            // Use the encoded positional information to find new sort order
            std::stable_sort(rQ.begin(), rQ.end(), [](const Align &a, const Align &b) {
                int v1 = a.sort_tag >> 6;
                int v2 = b.sort_tag >> 6;
                if (v1 != v2) {
                    if (a.cov_start <= b.cov_end && b.cov_start <= a.cov_end) {
                        return v1 > v2;
                    }
                }
                return a.pos < b.pos;
            });

            if (sortReadsBy == Utils::SortType::POS) {
                if (ls.empty()) {
                    ls.resize(opts.ylim + vScroll, 1215752191);
                    le.resize(opts.ylim + vScroll, 0);
                }
                findYNoSort(rQ, ls, le, joinLeft, vScroll, lm, linkedSeen, linkType);
            } else {
                findYWithSort(rc, rQ, ls, le, joinLeft, vScroll, lm, linkedSeen, linkType, opts.ylim);
            }

            // This is a bit annoying, but the queue must remain pos-sorted for the appending algorithm to work
            std::stable_sort(rQ.begin(), rQ.end(), [](const Align &a, const Align &b) {
                return a.pos < b.pos;
            });

        } else {
            findYWithSort(rc, rQ, ls, le, joinLeft, vScroll, lm, linkedSeen, linkType, opts.ylim);
        }

        samMaxY = opts.ylim;
        return samMaxY;
    }

    constexpr std::array<char, 256> make_lookup_ref_base() {
        std::array<char, 256> a{};
        for (auto& elem : a) {
            elem = 15;  // Initialize all elements to 15
        }
        a['A'] = 1; a['a'] = 1;
        a['C'] = 2; a['c'] = 2;
        a['G'] = 4; a['g'] = 4;
        a['T'] = 8; a['t'] = 8;
        a['N'] = 15; a['n'] = 15;
        return a;
    }
    constexpr std::array<char, 256> lookup_ref_base = make_lookup_ref_base();

    void update_A(Mismatches& elem) { elem.A += 1; }
    void update_C(Mismatches& elem) { elem.C += 1; }
    void update_G(Mismatches& elem) { elem.G += 1; }
    void update_T(Mismatches& elem) { elem.T += 1; }
    void update_pass(Mismatches& elem) {}  // For N bases

    // Lookup table for function pointers, initialized statically
    void (*lookup_table_mm[16])(Mismatches&) = {
            update_pass,     // 0
            update_A,        // 1
            update_C,        // 2
            update_pass,     // 3
            update_G,        // 4
            update_pass,     // 5
            update_pass,     // 6
            update_pass,     // 7
            update_T,        // 8
            update_pass,     // 9
            update_pass,     // 10
            update_pass,     // 11
            update_pass,     // 12
            update_pass,     // 13
            update_pass,     // 14
            update_pass      // 15
    };

    // used for drawing mismatches over coverage track
    void findMismatches(const Themes::IniOptions &opts, ReadCollection &collection) {

        std::vector<Segs::Mismatches> &mm_array = collection.mmVector;
        size_t mm_array_len = mm_array.size();
        const Utils::Region *region = collection.region;
        if (region == nullptr) {
            return;
        }
        int regionLen = region->end - region->start;
        if (opts.max_coverage == 0 || regionLen > opts.snp_threshold) {
            return;
        }

        const char *refSeq = region->refSeq;
        if (refSeq == nullptr) {
            return;
        }
        for (const auto &align: collection.readQueue) {
            if (align.y >= 0 || align.delegate == nullptr) {
                continue;
            }
            uint32_t r_pos = align.pos;
            uint32_t cigar_l = align.delegate->core.n_cigar;
            uint8_t *ptr_seq = bam_get_seq(align.delegate);
            uint32_t *cigar_p = bam_get_cigar(align.delegate);
            if (cigar_l == 0 || ptr_seq == nullptr || cigar_p == nullptr) {
                continue;
            }
            int r_idx;
            uint32_t idx = 0;
            uint32_t qseq_len = align.delegate->core.l_qseq;
            uint32_t rlen = region->end - region->start;
            auto rbegin = (uint32_t) region->start;
            auto rend = (uint32_t) region->end;
            uint32_t op, l;
            for (uint32_t k = 0; k < cigar_l; k++) {
                op = cigar_p[k] & BAM_CIGAR_MASK;
                l = cigar_p[k] >> BAM_CIGAR_SHIFT;
                if (idx >= qseq_len) {  // shouldn't happen
                    break;
                }
                switch (op) {
                    case BAM_CSOFT_CLIP:
                    case BAM_CINS:
                        idx += l;
                        break;

                    case BAM_CDEL:
                    case BAM_CREF_SKIP:
                        r_pos += l;
                        break;

                    case BAM_CHARD_CLIP:
                    case BAM_CEQUAL:
                        // Nothing to do here
                        break;
                    case BAM_CDIFF:
                        for (uint32_t i = 0; i < l; ++i) {
                            if (r_pos >= rbegin && r_pos < rend && r_pos - rbegin < mm_array_len) {
                                char bam_base = bam_seqi(ptr_seq, idx);
                                lookup_table_mm[(size_t)bam_base](mm_array[r_pos - rbegin]);
//                                switch (bam_base) {
//                                    case 1:
//                                        mm_array[r_pos - rbegin].A += 1;
//                                        break;
//                                    case 2:
//                                        mm_array[r_pos - rbegin].C += 1;
//                                        break;
//                                    case 4:
//                                        mm_array[r_pos - rbegin].G += 1;
//                                        break;
//                                    case 8:
//                                        mm_array[r_pos - rbegin].T += 1;
//                                        break;
//                                    default:
//                                        break;
//                                }
                            }
                            idx += 1;
                            r_pos += 1;
                        }
                        break;

                    default:
                        for (uint32_t i = 0; i < l; ++i) {
                            r_idx = (int) r_pos - region->start;
                            if (r_idx < 0) {
                                idx += 1;
                                r_pos += 1;
                                continue;
                            }
                            if (r_idx >= (int)rlen) {
                                break;
                            }
                            if (r_pos - rbegin >= mm_array_len) {
                                break;
                            }

                            char ref_base = lookup_ref_base[(unsigned char)refSeq[r_idx]];

                            char bam_base = bam_seqi(ptr_seq, idx);
                            if (bam_base != ref_base) {
                                lookup_table_mm[(size_t)bam_base](mm_array[r_pos - rbegin]);
//                                switch (bam_base) {
//                                    case 1:
//                                        mm_array[r_pos - rbegin].A += 1;
//                                        break;
//                                    case 2:
//                                        mm_array[r_pos - rbegin].C += 1;
//                                        break;
//                                    case 4:
//                                        mm_array[r_pos - rbegin].G += 1;
//                                        break;
//                                    case 8:
//                                        mm_array[r_pos - rbegin].T += 1;
//                                        break;
//                                    default:
//                                        break;
//                                }
                            }
                            idx += 1;
                            r_pos += 1;
                        }
                        break;
                }
            }
        }
    }


    struct TrackRange {
        int start, end;
    };

    int findTrackY(std::vector<Utils::TrackBlock> &features, bool expanded, const Utils::Region &rgn) {
        if (!expanded || features.empty()) {
            return 1;
        }
        std::vector<TrackRange> levels;
        levels.reserve(100);
        for (auto &b : features) {
            if (!b.anyToDraw) {
                b.level = -1;
                continue;
            }
            size_t memLen = levels.size();
            size_t i = 0;
            for (; i < memLen; ++i) {
                if (b.start > levels[i].end) {
                    levels[i].end = b.end + 5;
                    b.level = (int)i;
                    break;
                }
            }
            if (i == memLen) {
                levels.emplace_back() = {b.start, b.end + 5};
                b.level = memLen;
            }
        }
//        assert (levels.size() >= 1);
        return (int)levels.size();
    }
}