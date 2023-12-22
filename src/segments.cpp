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

    constexpr std::array<Pattern, 49> posFirst = {INV_F, u, u, u, u, u, u, u,
                                                  u, u, u, u, u, u, u, u,
                                                  DUP, u, u, u, u, u, u, u,
                                                  u, u, u, u, u, u, u, u,
                                                  DEL, u, u, u, u, u, u, u,
                                                  u, u, u, u, u, u, u, u,
                                                  INV_R};
    constexpr std::array<Pattern, 49> mateFirst = {INV_R, u, u, u, u, u, u, u,
                                                  u, u, u, u, u, u, u, u,
                                                  DEL, u, u, u, u, u, u, u,
                                                  u, u, u, u, u, u, u, u,
                                                  DUP, u, u, u, u, u, u, u,
                                                  u, u, u, u, u, u, u, u,
                                                  INV_F};

    void align_init(Align *self) { //noexcept {
//        auto start = std::chrono::high_resolution_clock::now();
        bam1_t *src = self->delegate;

        self->pos = src->core.pos;
        self->reference_end = bam_endpos(src);  // reference_end - already checked for 0 length cigar and mapped
        self->cov_start = (int)self->pos;
        self->cov_end = (int)self->reference_end;

        uint32_t pos, l, cigar_l, op, k;
        uint32_t *cigar_p;

        cigar_l = src->core.n_cigar;

        pos = src->core.pos;
        cigar_p = bam_get_cigar(src);

        self->left_soft_clip = 0;
        self->right_soft_clip = 0;

        uint32_t last_op = 0;

        self->any_ins.reserve(cigar_l);
        self->block_starts.reserve(cigar_l);
        self->block_ends.reserve(cigar_l);

        for (k = 0; k < cigar_l; k++) {
            op = cigar_p[k] & BAM_CIGAR_MASK;
            l = cigar_p[k] >> BAM_CIGAR_SHIFT;

            switch (op) {
                case BAM_CMATCH: case BAM_CEQUAL: case BAM_CDIFF:
                    if (last_op == BAM_CINS) {
                        if (!self->block_ends.empty() ) {
                            self->block_ends.back() = pos + l;
                        }
                    } else {
                        self->block_starts.push_back(pos);
                        self->block_ends.push_back(pos + l);
                    }
                    pos += l;
                    break;
                case BAM_CINS:
                    self->any_ins.push_back({pos, l});
                    break;
                case BAM_CDEL: case BAM_CREF_SKIP:
                    pos += l;
                    break;
                case BAM_CSOFT_CLIP:
                    if (k == 0) {
                        self->cov_start -= (int)l;
                        self->left_soft_clip = (int)l;
                    } else {
                        self->cov_end += l;
                        self->right_soft_clip = (int)l;
                    }
                    break;
//                case BAM_CHARD_CLIP: case BAM_CPAD: case BAM_CBACK:
//                    break;  // do something for these?
                default:  // Match case --> MATCH, EQUAL, DIFF
                    break;
            }
            last_op = op;
        }

        uint32_t flag = src->core.flag;

        if (bam_aux_get(self->delegate, "SA") != nullptr) {
            self->has_SA = true;
        } else {
            self->has_SA = false;
        }

        self->y = -1;

        if (flag & 1) {  // paired-end
            if (src->core.tid != src->core.mtid) {
                self->orient_pattern = TRA;
            } else {
                uint32_t info = flag & PP_RR_MR;  // PP_RR_MR = proper-pair, read-reverse, mate-reverse flags
                if (self->pos <= src->core.mpos) {
                    self->orient_pattern = posFirst[info];
                } else {
                    self->orient_pattern = mateFirst[info];
                }
            }
        } else { // single-end
            self->orient_pattern = Segs::Pattern::NORMAL;
        }

        if (flag & 2048 || self->has_SA) {
            self->edge_type = 2;  // "SPLIT"
        } else if (flag & 8) {
            self->edge_type = 3;  // "MATE_UNMAPPED"
        } else {
            self->edge_type = 1;  // "NORMAL"
        }
        self->initialized = true;
//        auto stop = std::chrono::high_resolution_clock::now();
//        auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(stop - start);
    }

    void align_clear(Align *self) {
        self->block_starts.clear();
        self->block_ends.clear();
        self->any_ins.clear();
    }

    void init_parallel(std::vector<Align> &aligns, int n,  BS::thread_pool &pool) {
        if (n == 1) {
            for (auto &aln : aligns) {
                align_init(&aln);
            }
        } else {
            pool.parallelize_loop(0, aligns.size(),
                                  [&aligns](const int a, const int b) {
                                      for (int i = a; i < b; ++i)
                                          align_init(&aligns[i]);
                                  })
                    .wait();
        }
    }

    ReadCollection::ReadCollection() {
        vScroll = 0;
        collection_processed = false;
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
        size_t n_blocks = align.block_starts.size();
        for (size_t idx=0; idx < n_blocks; ++idx) {
            uint32_t block_s = align.block_starts[idx];
            if (block_s >= end) { break; }
            uint32_t block_e = align.block_ends[idx];
            if (block_e < begin) { continue; }
//            uint32_t s = (block_s >= begin) ? block_s - begin : 0;
//            uint32_t e = (block_e < end) ? block_e - begin : l_arr;
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
                    v.y = -1;
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
            int max_bound = (opts.max_tlen) + (vScroll * 100);
            samMaxY = max_bound;
            for (auto &aln : rQ) {
                int tlen = (int)std::abs(aln.delegate->core.isize);
                if (tlen < max_bound) {
                    aln.y = tlen;
                } else {
                    aln.y = max_bound;
                    samMaxY = max_bound;
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

    void findMismatches(const Themes::IniOptions &opts, ReadCollection &collection) {

        std::vector<Segs::Mismatches> &mm_array = collection.mmVector;
        const Utils::Region *region = collection.region;
        int regionLen = region->end - region->start;
        if (opts.max_coverage == 0 || regionLen > opts.snp_threshold) {
            return;
        }
        const char *refSeq = region->refSeq;
        if (refSeq == nullptr) {
            return;
        }
        for (const auto &align: collection.readQueue) {
            if (align.y != -1) {
                continue;
            }
            uint32_t r_pos = align.pos;
            uint32_t cigar_l = align.delegate->core.n_cigar;
            uint8_t *ptr_seq = bam_get_seq(align.delegate);
            uint32_t *cigar_p = bam_get_cigar(align.delegate);
            int r_idx;
            uint32_t idx = 0;

            uint32_t rlen = region->end - region->start;
            auto rbegin = (uint32_t) region->start;
            auto rend = (uint32_t) region->end;
            uint32_t op, l;
            for (uint32_t k = 0; k < cigar_l; k++) {
                op = cigar_p[k] & BAM_CIGAR_MASK;
                l = cigar_p[k] >> BAM_CIGAR_SHIFT;

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
                        // Nothing to do here, just continue in the loop
                        break;
                    case BAM_CDIFF:
                        for (uint32_t i = 0; i < l; ++i) {
                            if (r_pos >= rbegin && r_pos < rend) {
                                char bam_base = bam_seqi(ptr_seq, idx);
                                switch (bam_base) {
                                    case 1:
                                        mm_array[r_pos - rbegin].A += 1;
                                        break;
                                    case 2:
                                        mm_array[r_pos - rbegin].C += 1;
                                        break;
                                    case 4:
                                        mm_array[r_pos - rbegin].G += 1;
                                        break;
                                    case 8:
                                        mm_array[r_pos - rbegin].T += 1;
                                        break;
                                    default:
                                        break;
                                }
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

                            char ref_base;
                            switch (refSeq[r_idx]) {
                                case 'A':
                                case 'a':
                                    ref_base = 1;
                                    break;
                                case 'C':
                                case 'c':
                                    ref_base = 2;
                                    break;
                                case 'G':
                                case 'g':
                                    ref_base = 4;
                                    break;
                                case 'T':
                                case 't':
                                    ref_base = 8;
                                    break;
                                case 'N':
                                default:
                                    ref_base = 15;
                                    break;
                            }

                            char bam_base = bam_seqi(ptr_seq, idx);
                            if (bam_base != ref_base) {
                                switch (bam_base) {
                                    case 1:
                                        mm_array[r_pos - rbegin].A += 1;
                                        break;
                                    case 2:
                                        mm_array[r_pos - rbegin].C += 1;
                                        break;
                                    case 4:
                                        mm_array[r_pos - rbegin].G += 1;
                                        break;
                                    case 8:
                                        mm_array[r_pos - rbegin].T += 1;
                                        break;
                                    default:
                                        break;
                                }
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