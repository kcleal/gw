#include <cassert>
#include <chrono>
#include <algorithm>
#include <vector>
#include <fstream>

#include "segments.h"
#include "utils.h"


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

    void align_init(Align *self, const bool parse_mods) {
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
                    seq_index += l;
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
                default:  // Match case --> MATCH, EQUAL, DIFF
                    break;
            }
        }
        self->reference_end = self->blocks.back().end;
        self->cov_start = (int)self->pos - self->left_soft_clip;
        self->cov_end = (int)self->reference_end + self->right_soft_clip;

        uint32_t flag = src->core.flag;

        if (bam_aux_get(self->delegate, "SA") != nullptr) {
            self->has_SA = true;
        } else {
            self->has_SA = false;
        }
        bool has_mods = (src->core.l_qseq > 0 && (bam_aux_get(self->delegate, "MM") != nullptr || bam_aux_get(self->delegate, "Mm") != nullptr));
        if (has_mods && parse_mods) {
            hts_base_mod_state * mod_state = hts_base_mod_state_alloc();
            int res = bam_parse_basemod(src, mod_state);
            if (res >= 0) {
                hts_base_mod mods[10];
                int pos = 0;  // position on read, not reference
                int nm = bam_next_basemod(src, mod_state, mods, 10, &pos);
                while (nm > 0) {
                self->any_mods.emplace_back() = ModItem();
                ModItem& mi = self->any_mods.back();
                mi.index = pos;
                size_t j=0;
                int thresh = 50;
                for (size_t m=0; m < std::min((size_t)4, (size_t)nm); ++m) {
                    if (mods[m].qual > thresh) {
                        mi.mods[j] = (char)mods[m].modified_base;
                        mi.quals[j] = (uint8_t)mods[m].qual;
                        mi.strands[j] = (bool)mods[m].strand;
                        j += 1;
                    }
                }
                mi.n_mods = (uint8_t)j;
//                std::cout << nm << " " << mods[0].modified_base
//                << " " << mods[0].canonical_base
//                << " " << mods[0].strand
//                << " " << mods[0].qual
//                << std::endl;
                nm = bam_next_basemod(src, mod_state, mods, 10, &pos);
                }
//                std::cout << std::endl;
            }
            hts_base_mod_state_free(mod_state);
        }


        self->y = -1;  // -1 has no level, -2 means initialized but filtered

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
//        self->block_starts.clear();
//        self->block_ends.clear();
        self->blocks.clear();
        self->any_ins.clear();
    }

    void init_parallel(std::vector<Align> &aligns, int n, BS::thread_pool &pool, const bool parse_mods) {
        if (n == 1) {
            for (auto &aln : aligns) {
                align_init(&aln, parse_mods);
            }
        } else {
            pool.parallelize_loop(0, aligns.size(),
                                  [&aligns, parse_mods](const int a, const int b) {
                                      for (int i = a; i < b; ++i)
                                          align_init(&aligns[i], parse_mods);
                                  })
                    .wait();
        }
    }

    EXPORT ReadCollection::ReadCollection() {
        vScroll = 0;
        collection_processed = false;
        skipDrawingReads = false;
        skipDrawingCoverage = false;
    }

    void ReadCollection::clear() {
        std::fill(levelsStart.begin(), levelsStart.end(), 1215752191);
        std::fill(levelsEnd.begin(), levelsEnd.end(), 0);
        std::fill(covArr.begin(), covArr.end(), 0);
        linked.clear();
        collection_processed = false;
        for (auto &item: readQueue) {
            bam_destroy1(item.delegate);
        }
        readQueue.clear();
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

    void addToCovArray(std::vector<int> &arr, const Align &align, const uint32_t begin, const uint32_t end, const uint32_t l_arr) noexcept {
        size_t n_blocks = align.blocks.size();
//        size_t n_blocks = align.block_starts.size();
        for (size_t idx=0; idx < n_blocks; ++idx) {
//            uint32_t block_s = align.block_starts[idx];
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

    int findY(ReadCollection &rc, std::vector<Align> &rQ, int linkType, Themes::IniOptions &opts, Utils::Region *region, bool joinLeft) {
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
            q_ptr = &rc.readQueue.front();
            // find the start and end coverage locations of aligns with same name
            for (i=0; i < (int)rc.readQueue.size(); ++i) {
                qname = bam_get_qname(q_ptr->delegate);
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
            int max_bound = opts.max_tlen; //(opts.max_tlen) + (vScroll * 100);
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

        if (ls.empty()) {
            ls.resize(opts.ylim + vScroll, 1215752191);
            le.resize(opts.ylim + vScroll, 0);
        }

        int qLen = (int)rQ.size();
        int stopCondition, move, si;
        int memLen = (int)ls.size();

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

        while (si != stopCondition) {
            si += move;
            if (q_ptr->y == -2) {
                q_ptr += move;
                continue;
            }
            if (linkType > 0) {
                qname = bam_get_qname(q_ptr->delegate);
                if (linkedSeen.find(qname) != linkedSeen.end()) {
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
                        if (linkType > 0 && lm.find(qname) != lm.end()) {
                            linkedSeen[qname] = q_ptr->y;
                        }
                        break;
                    }
                }
                if (i == memLen && linkType > 0 && lm.find(qname) != lm.end()) {
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
                        if (linkType > 0 && lm.find(qname) != lm.end()) {
                            linkedSeen[qname] = q_ptr->y;
                        }
                        break;
                    }
                }
                if (i == memLen && linkType > 0 && lm.find(qname) != lm.end()) {
                    linkedSeen[qname] = q_ptr->y;  // y is out of range i.e. -1
                }
                q_ptr += move;
            }
        }
        samMaxY = memLen - vScroll;
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