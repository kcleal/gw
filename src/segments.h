#pragma once

#include <cstdint>
#include <cstring>
#include <vector>
#include <deque>
#include <iostream>
#include <vector>

#include <deque>
#include <unordered_map>

#include "../include/BS_thread_pool.h"
#include "../include/unordered_dense.h"
#include "htslib/sam.h"

#include "themes.h"
#include "utils.h"


namespace Segs {

    enum Pattern {
        NORMAL,
        DEL,
        INV_F,
        INV_R,
        DUP,
        TRA,
    };

    typedef int64_t hts_pos_t;

    struct InsItem {
        uint32_t pos, length;
    };

//    struct QueueItem {
//        uint32_t c_s_idx, l;
//    };
//
//    struct MMbase {
//        uint32_t idx, pos;
//        uint8_t qual, base;
//    };
//
//    struct MdBlock {
//        uint32_t matches, md_idx, del_length;
//        bool is_mm;
//    };
//
//    void get_md_block(const char *md_tag, int md_idx, MdBlock *res);
//
//    void get_mismatched_bases(std::vector<MMbase> &result, const char *md_tag, uint32_t r_pos, uint32_t ct_l, uint32_t *cigar_p);

    struct Align {
        bam1_t *delegate;
        int cov_start, cov_end, orient_pattern, left_soft_clip, right_soft_clip, y, edge_type;
        uint32_t pos, reference_end, cigar_l;
        bool has_SA, initialized;
        std::vector<uint32_t> block_starts, block_ends;
        std::vector<InsItem> any_ins;
//        std::vector<MMbase> mismatches;
    };

    typedef ankerl::unordered_dense::map< std::string, std::vector< Align* >> map_t;

    class ReadCollection {
    public:
       ReadCollection();
        ~ReadCollection() = default;
        int bamIdx, regionIdx, vScroll;
        Utils::Region region;
        std::vector<int> covArr;
        std::vector<int> levelsStart, levelsEnd;
        std::vector<Align> readQueue;
        map_t linked;
        float xScaling, xOffset, yOffset, yPixels;
        bool processed;

        void clear();
    };

    void align_init(Align *self) noexcept;

    void align_clear(Align *self);

    void init_parallel(std::vector<Align> &aligns, int n);

    void resetCovStartEnd(ReadCollection &cl);

    void addToCovArray(std::vector<int> &arr, Align &align, uint32_t begin, uint32_t end, uint32_t l_arr) noexcept;

    int findY(ReadCollection &rc, std::vector<Align> &rQ, int linkType, Themes::IniOptions &opts, Utils::Region *region, bool joinLeft);

}