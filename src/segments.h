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

    struct EXPORT ABlock {
        uint32_t start, end; // on reference
        uint32_t seq_index;
    };

    struct EXPORT InsItem {
        uint32_t pos, length;
    };

    struct EXPORT ModItem {  // up to 4 modifications
        int index;
        uint8_t n_mods;
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

    struct EXPORT Align {
        bam1_t *delegate;
        int cov_start, cov_end, orient_pattern, left_soft_clip, right_soft_clip, y, edge_type, sort_tag;
        uint32_t pos, reference_end;
        bool has_SA;
        std::vector<ABlock> blocks;
        std::vector<InsItem> any_ins;
        std::vector<ModItem> any_mods;

        // Constructor
        Align(bam1_t *src) { delegate = src; }

        // Destructor
        ~Align() {}

        // Copy constructor
        Align(const Align& other) : cov_start(other.cov_start), cov_end(other.cov_end),
                                    orient_pattern(other.orient_pattern), left_soft_clip(other.left_soft_clip),
                                    right_soft_clip(other.right_soft_clip), y(other.y), edge_type(other.edge_type),
                                    sort_tag(other.sort_tag), pos(other.pos), reference_end(other.reference_end),
                                    has_SA(other.has_SA), blocks(other.blocks), any_ins(other.any_ins),
                                    any_mods(other.any_mods) {
            delegate = other.delegate ? bam_dup1(other.delegate) : nullptr;
        }

        // Move constructor
        Align(Align&& other) noexcept : delegate(other.delegate), cov_start(other.cov_start),
                                        cov_end(other.cov_end), orient_pattern(other.orient_pattern),
                                        left_soft_clip(other.left_soft_clip), right_soft_clip(other.right_soft_clip),
                                        y(other.y), edge_type(other.edge_type), sort_tag(other.sort_tag),
                                        pos(other.pos), reference_end(other.reference_end), has_SA(other.has_SA),
                                        blocks(std::move(other.blocks)), any_ins(std::move(other.any_ins)),
                                        any_mods(std::move(other.any_mods)) {
            other.delegate = nullptr;
        }

        // Copy assignment operator
        Align& operator=(const Align& other) {
            if (this != &other) {
                Align tmp(other);
                std::swap(*this, tmp);
            }
            return *this;
        }

        // Move assignment operator
        Align& operator=(Align&& other) noexcept {
            if (this != &other) {
                delegate = other.delegate;
                other.delegate = nullptr;
                cov_start = other.cov_start;
                cov_end = other.cov_end;
                orient_pattern = other.orient_pattern;
                left_soft_clip = other.left_soft_clip;
                right_soft_clip = other.right_soft_clip;
                y = other.y;
                edge_type = other.edge_type;
                sort_tag = other.sort_tag;
                pos = other.pos;
                reference_end = other.reference_end;
                has_SA = other.has_SA;
                blocks = std::move(other.blocks);
                any_ins = std::move(other.any_ins);
                any_mods = std::move(other.any_mods);
            }
            return *this;
        }
    };


    struct EXPORT Mismatches {
        uint32_t A, T, C, G;
    };

    typedef ankerl::unordered_dense::map< std::string, std::vector< Align* >> map_t;

    class EXPORT ReadCollection {
    public:
        ReadCollection() {};
        ~ReadCollection() = default;
        std::string name;
        int bamIdx{0}, regionIdx{0}, vScroll{0};
        int maxCoverage, regionLen;
        Utils::Region *region;
        std::vector<int> covArr;
        std::vector<int> levelsStart, levelsEnd;
        std::vector<Mismatches> mmVector;
        std::vector<Align> readQueue;
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
        bool ownsBamPtrs{true};

        void makeEmptyMMArray();
        void clear();
        void resetDrawState();
        void modifySOftClipSpace(bool add_soft_clip_space);
    };

    void EXPORT align_init(Align *self, const int parse_mods_threshold, const bool add_clip_space);

    void EXPORT align_clear(Align *self);

    void init_parallel(std::vector<Align> &aligns, const int n, BS::thread_pool &pool,
        const int parse_mods_threshold, const bool add_clip_space);

    void resetCovStartEnd(ReadCollection &cl);

    void EXPORT addToCovArray(std::vector<int> &arr, const Align &align, const uint32_t begin, const uint32_t end) noexcept;

    // Used to get sorting codes before using findY functions
    int getSortCodes(std::vector<Align> &aligns, int n, BS::thread_pool &pool, Utils::Region *region);

    // Find Y for single alignment
    void alignFindYForward(Align &a, std::vector<int> &ls, std::vector<int> &le, int vScroll);

    // Used for drawing in a stream only
    void findYNoSortForward(std::vector<Align> &rQ, std::vector<int> &ls, std::vector<int> &le, int vScroll);

    // Works with buffered reads or stream of reads, needed if reads are to be re-sorted
    int EXPORT findY(ReadCollection &rc, std::vector<Align> &rQ, int linkType, Themes::IniOptions &opts, bool joinLeft, int sortReadsBy);

    void findMismatches(const Themes::IniOptions &opts, ReadCollection &collection);

    int findTrackY(std::vector<Utils::TrackBlock> &features, bool expanded, const Utils::Region &rgn);

}