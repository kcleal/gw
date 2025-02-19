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
#include "alignment_format.h"
#include "export_definitions.h"
#include "themes.h"
#include "utils.h"
#include "alignment_format.h"


namespace Segs {



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

//    struct EXPORT Align {
//        bam1_t *delegate;
//        int cov_start, cov_end, orient_pattern, left_soft_clip, right_soft_clip, y, edge_type, sort_tag;
//        uint32_t pos, reference_end;
//        bool has_SA;
//        std::vector<ABlock> blocks;
//        std::vector<InsItem> any_ins;
//        std::vector<ModItem> any_mods;
//
//        Align(bam1_t *src) { delegate = src; }
//    };




    struct EXPORT Mismatches {
        uint32_t A, T, C, G;
    };

    typedef ankerl::unordered_dense::map< std::string, std::vector< AlignFormat::Align* >> map_t;


    // A collection of read alignments that dynamically updates and caches items to be displayed
    class EXPORT ReadCollection {
    public:
        ReadCollection() = default;
        ~ReadCollection() = default;
        std::string name;
        AlignFormat::GwAlignment* alignmentFile;
        int bamIdx, regionIdx, vScroll{0};
        int maxCoverage, regionLen;
        Utils::Region *region;
        std::vector<int> covArr;
        std::vector<int> levelsStart, levelsEnd;
        std::vector<Mismatches> mmVector;
        std::vector<AlignFormat::Align> readQueue;
        std::vector<AlignFormat::GAF_t*> readQueueGAF;
        map_t linked;

        std::vector<int> sortLevels;
        float xScaling, xOffset, yOffset, yPixels, xPixels;
        float regionPixels;

        bool collection_processed{false};
        bool skipDrawingReads{false};
        bool skipDrawingCoverage{false};
        bool plotSoftClipAsBlock;
        bool plotPointedPolygons;
        bool drawEdges;

        void clear();
    };

    void align_init(AlignFormat::Align *self, int parse_mods_threshold);

    void align_clear(AlignFormat::Align *self);

    void init_parallel(std::vector<AlignFormat::Align> &aligns, int n, BS::thread_pool &pool, int parse_mods_threshold);

    void resetCovStartEnd(ReadCollection &cl);

    void addToCovArray(std::vector<int> &arr, const std::vector<AlignFormat::ABlock>& blocks, uint32_t begin, uint32_t end) noexcept;

    // Used to get sorting codes before using findY functions
    int getSortCodes(std::vector<AlignFormat::Align> &aligns, int n, BS::thread_pool &pool, Utils::Region *region);

    // Find Y for single alignment
    void alignFindYForward(AlignFormat::Align &a, std::vector<int> &ls, std::vector<int> &le, int vScroll);

    // Used for drawing in a stream only
    void findYNoSortForward(std::vector<AlignFormat::Align> &rQ, std::vector<int> &ls, std::vector<int> &le, int vScroll);

    // Works with buffered reads or stream of reads, needed if reads are to be re-sorted
    int findY(ReadCollection &rc, std::vector<AlignFormat::Align> &rQ, int linkType, Themes::IniOptions &opts, bool joinLeft, int sortReadsBy);

    void findMismatches(const Themes::IniOptions &opts, ReadCollection &collection);

    int findTrackY(std::vector<Utils::TrackBlock> &features, bool expanded, const Utils::Region &rgn);

}