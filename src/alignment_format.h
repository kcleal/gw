//
// Created by kez on 14/02/25.
//

#pragma once

#include <string>
#include <vector>

#include "htslib/faidx.h"
#include "htslib/hfile.h"
#include "htslib/hts.h"
#include "htslib/vcf.h"
#include "htslib/sam.h"
#include "htslib/tbx.h"

#include "ankerl_unordered_dense.h"
#include "superintervals.h"
#include "export_definitions.h"

namespace AlignFormat {

    enum EXPORT Pattern {
        u = 0,
        NORMAL = 0,
        DEL = 1,
        INV_F = 2,
        INV_R = 3,
        DUP = 4,
        TRA = 5,
    };

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


    struct EXPORT Align {
        bam1_t *delegate;
        int cov_start, cov_end, orient_pattern, left_soft_clip, right_soft_clip, y, edge_type, sort_tag;
        uint32_t pos, reference_end;
        bool has_SA;
        std::vector<AlignFormat::ABlock> blocks;
        std::vector<AlignFormat::InsItem> any_ins;
        std::vector<AlignFormat::ModItem> any_mods;

        // Constructor
        Align() {}
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

    EXPORT enum class AlignmentType {
        HTSLIB_t,
        GAF_t
    };

    EXPORT class GAF_t {
    public:
        std::string qname, chrom;
        int qlen, qstart, qend;
        int pos{0}, end{0}, qual, flag{0};
        char strand;  // this is strand of the query, not reference
        int y{-1};

        std::vector<ABlock> blocks;
        GAF_t() = default;
        ~GAF_t() = default;

        GAF_t* duplicate() const {
            return new GAF_t(*this);
        }
    };

    EXPORT class GwAlignment {
    public:
        AlignmentType type;
        std::string path;
        // HTS files use indexes for loading alignments
        htsFile* bam{nullptr};
        sam_hdr_t* header{nullptr};
        hts_idx_t* index{nullptr};

        // GAF files are transformed to bam format!
        ankerl::unordered_dense::map< std::string, SuperIntervals<int, GAF_t *>> cached_alignments;

        GwAlignment() = default;
        ~GwAlignment();

        void open(const std::string& path, const std::string& reference, int threads);
    };

    void gafToAlign(AlignFormat::GAF_t* gaf, AlignFormat::Align* align);

}
