#pragma once

#include <cstdint>
#include <cstring>
#include <deque>
#include <future>
#include <iostream>
#include <vector>

#include <deque>
#include <unordered_map>

#include "BS_thread_pool.h"
#include "ankerl_unordered_dense.h"
#include "htslib/sam.h"

#include "export_definitions.h"
#include "themes.h"
#include "utils.h"


namespace Segs {

    enum EXPORT Pattern {
        u = 0,
        NORMAL = 0,
        DEL = 1,
        INV_F = 2,
        INV_R = 3,
        DUP = 4,
        TRA = 5,
    };

//    typedef int64_t hts_pos_t;

    struct EXPORT InsItem {
        uint32_t pos, length;
    };

    struct EXPORT ModItem {  // up to 4 modifications
        int index;
        char mods[4];  // 0 is used to indicate no more mods
        uint8_t quals[4];
        bool strands[4];
        ModItem () {
            index = -1;
            mods[0] = 0;
            mods[1] = 0;
            mods[2] = 0;
            mods[3] = 0;
        }
    };

//
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

    struct EXPORT Align {
        bam1_t *delegate;
        int cov_start, cov_end, orient_pattern, left_soft_clip, right_soft_clip, y, edge_type;
        uint32_t pos, reference_end;
        bool has_SA; //, initialized;
        std::vector<uint32_t> block_starts, block_ends;
        std::vector<InsItem> any_ins;
        std::vector<ModItem> any_mods;

        Align(bam1_t *src) { delegate = src; }
    };

    struct EXPORT Mismatches {
        uint32_t A, T, C, G;
    };

    typedef ankerl::unordered_dense::map< std::string, std::vector< Align* >> map_t;

    class EXPORT ReadCollection {
    public:
       ReadCollection();
        ~ReadCollection() = default;
        int bamIdx, regionIdx, vScroll;
        int maxCoverage, regionLen;
        Utils::Region *region;
        std::vector<int> covArr;
        std::vector<int> levelsStart, levelsEnd;
        std::vector<Mismatches> mmVector;
        std::vector<Align> readQueue;
        map_t linked;
        float xScaling, xOffset, yOffset, yPixels;
        float regionPixels;

        bool collection_processed;
        bool skipDrawingReads;
        bool skipDrawingCoverage;
        bool plotSoftClipAsBlock;
        bool plotPointedPolygons;
        bool drawEdges;

        void clear();
    };

    void align_init(Align *self, const bool parse_mods);

    void align_clear(Align *self);

    void init_parallel(std::vector<Align> &aligns, int n, BS::thread_pool &pool, const bool parse_mods);

    void resetCovStartEnd(ReadCollection &cl);

    void addToCovArray(std::vector<int> &arr, const Align &align, const uint32_t begin, const uint32_t end, const uint32_t l_arr) noexcept;

    int findY(ReadCollection &rc, std::vector<Align> &rQ, int linkType, Themes::IniOptions &opts, Utils::Region *region, bool joinLeft);

    void findMismatches(const Themes::IniOptions &opts, ReadCollection &collection);

    int findTrackY(std::vector<Utils::TrackBlock> &features, bool expanded, const Utils::Region &rgn);

}