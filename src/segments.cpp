
#include "segments.h"

namespace Segs {

    void get_md_block(char *md_tag, int md_idx, int md_l, MdBlock *res) {
        int nmatches = 0;
        int del_length = 0;
        bool is_mm = false;
        while (md_idx < md_l) {
            if (48 <= md_tag[md_idx] && md_tag[md_idx] <= 57) {  // c is numerical
                nmatches = nmatches * 10 + md_tag[md_idx] - 48;
                md_idx += 1;
            } else {
                if (md_tag[md_idx] == 94) {  // del_sign is 94 from ord('^')
                    md_idx += 1;
                    while (65 <= md_tag[md_idx] && md_tag[md_idx] <= 90) {
                        md_idx += 1;
                        del_length += 1;
                    }
                } else {  // save mismatch
                    is_mm = true;
                    md_idx += 1;
                }
                break;
            }
        }
        res->matches = nmatches;
        res->md_idx = md_idx;
        res->is_mm = is_mm;
        res->del_length = del_length;
    }


    void get_mismatched_bases(std::vector<MMbase> &result,
                              char *md_tag, uint32_t r_pos,
                              uint32_t ct_l, uint32_t *cigar_p) {

        uint32_t opp, l, c_idx, s_idx, c_s_idx;
        size_t md_l = strlen(md_tag);
        std::deque<QueueItem> ins_q;
        MdBlock md_block;

        get_md_block(md_tag, 0, md_l, &md_block);
        if (md_block.md_idx == md_l) {
            return;
        }

        c_idx = 0;  // the cigar index
        s_idx = 0;  // sequence index of mismatches
        c_s_idx = 0;  // the index of the current cigar (c_idx) on the input sequence

        opp = cigar_p[0] & BAM_CIGAR_MASK;
        if (opp == 4) {
            c_idx += 1;
            s_idx += cigar_p[0] >> BAM_CIGAR_SHIFT;
            c_s_idx = s_idx;
        } else if (opp == 5) {
            c_idx += 1;
        }

        while (true) {
            if (c_idx < ct_l) {  // consume cigar until deletion reached, collect positions of insertions
                while (c_idx < ct_l) {
                    opp = cigar_p[c_idx] & BAM_CIGAR_MASK;
                    l = cigar_p[c_idx] >> BAM_CIGAR_SHIFT;
                    if (opp == 0 || opp == 8) {  // match
                        c_s_idx += l;
                    } else if (opp == 1) {  // insertion
                        ins_q.push_back({c_s_idx, l});
                        c_s_idx += l;
                    } else {  // opp == 2 or opp == 4 or opp == 5
                        break;
                    }
                    ++c_idx;
                }
                c_idx += 1;
            }

            while (true) {   // consume md tag until deletion reached
                if (!md_block.is_mm) {  // deletion or end or md tag
                    if (md_block.md_idx == md_l) {
                        return;
                    }
                    s_idx = c_s_idx;
                    r_pos += md_block.matches + md_block.del_length;
                    while (!ins_q.empty() &&
                           s_idx + md_block.matches >= ins_q[0].c_s_idx) {  // catch up with insertions
                        ins_q.pop_front();
                    }
                    get_md_block(md_tag, md_block.md_idx, md_l, &md_block);
                    break;
                }

                // check if insertion occurs before mismatch
                while (!ins_q.empty() && s_idx + md_block.matches >= ins_q[0].c_s_idx) {
                    s_idx += ins_q[0].l;
                    ins_q.pop_front();
                }

                s_idx += md_block.matches;
                r_pos += md_block.matches;
                result.push_back({s_idx, r_pos});
                s_idx += 1;
                r_pos += 1;
                get_md_block(md_tag, md_block.md_idx, md_l, &md_block);
            }
        }
    }

    void align_init(Align *self) {
        uint8_t *v;
        char *value;

        self->pos = self->delegate->core.pos;
        self->reference_end = bam_endpos(
                self->delegate);  // reference_end - already checked for 0 length cigar and mapped
        self->cov_start = self->pos;
        self->cov_end = self->reference_end;

        uint32_t pos, l, cigar_l, op, k;
        uint32_t *cigar_p;
        bam1_t *src;
        src = self->delegate;
        cigar_l = src->core.n_cigar;

        pos = src->core.pos;
        cigar_p = bam_get_cigar(src);
        l = 0;

        self->left_soft_clip = 0;
        self->left_hard_clip = 0;
        self->right_soft_clip = 0;

        for (k = 0; k < cigar_l; k++) {
            op = cigar_p[k] & BAM_CIGAR_MASK;
            l = cigar_p[k] >> BAM_CIGAR_SHIFT;
            if (op == 4) {
                self->cov_end += l;
                self->right_soft_clip = l;
                if (k == 0) {
                    self->cov_start -= l;
                    self->left_soft_clip = l;

                } else {
                    self->cov_end += l;
                    self->right_soft_clip = l;
                }
            } else if (op == 1) {
                self->any_ins.push_back({pos, l});
            } else if (k == 0 && op == 5) {
                self->left_hard_clip = l;
            }

            if (op == BAM_CMATCH || op == BAM_CEQUAL || op == BAM_CDIFF) {
                self->block_starts.push_back(pos);
                self->block_ends.push_back(pos + l);
                pos += l;
            } else if (op == BAM_CDEL || op == BAM_CREF_SKIP) {
                pos += l;
            }
        }

        if (src->core.flag & 16) {  // reverse strand
            self->cov_start -= 1;   // pad between alignments
        } else {
            self->cov_end += 1;
        }

        v = bam_aux_get(self->delegate, "MD");
        if (v == NULL) {
            self->has_MD = false;
        } else {
            value = (char *) bam_aux2Z(v);
            self->MD = value;
            self->has_MD = true;
        }
        if (bam_aux_get(self->delegate, "SA") != NULL) {
            self->has_SA = true;
        }

        self->y = -1;
        self->polygon_height = 0.8;

        int ptrn = NORMAL;
        uint32_t flag = src->core.flag;
        if (flag & 1 && ~flag & 14) {  // not; proper-pair, unmapped, mate-unmapped
            if (src->core.tid == src->core.mtid) {
                if (self->pos <= src->core.mpos) {
                    if (~flag & 16) {
                        if (flag & 32) {
                            if (~flag & 2) {
                                ptrn = DEL;
                            }
                        } else {
                            ptrn = INV_F;
                        }
                    } else {
                        if (flag & 32) {
                            ptrn = INV_R;
                        } else {
                            ptrn = DUP;
                        }
                    }
                } else {
                    if (flag & 16) {
                        if (~flag & 32) {
                            if (~flag & 2) {
                                ptrn = DEL;
                            }
                        } else {
                            ptrn = INV_F;
                        }
                    } else {
                        if (~flag & 32) {
                            ptrn = INV_R;
                        } else {
                            ptrn = DUP;
                        }
                    }
                }
            } else {
                ptrn = TRA;
            }
        }

        self->orient_pattern = ptrn;

        if (flag & 2048 || self->has_SA) {
            self->edge_type = 2;  // "SPLIT"
        } else if (flag & 8) {
            self->edge_type = 3;  // "MATE_UNMAPPED"
        } else {
            self->edge_type = 1;  // "NORMAL"
        }

        get_mismatched_bases(self->mismatches, self->MD, self->pos, cigar_l, cigar_p);

        self->initialized = true;
    }

    void init_parallel(std::vector<Align> *aligns, int n) {

        if (n == 1) {
            for (size_t i = 0; i < (*aligns).size(); ++i)
                align_init(&(*aligns)[i]);

        } else {
            BS::thread_pool pool(n);
            pool.parallelize_loop(0, (*aligns).size(),
                                  [aligns](const int a, const int b) {
                                      for (int i = a; i < b; ++i)
                                          align_init(&(*aligns)[i]);
                                  })
                    .wait();
        }
    }

}