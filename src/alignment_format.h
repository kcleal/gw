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

#include "export_definitions.h"

namespace AlignFormat {

    EXPORT enum class AlignmentType {
        HTSLIB_t,
        GAF_t
    };

    EXPORT class GwAlignment {
    public:
        AlignmentType type;
        std::string path;
        htsFile* bam{nullptr};
        sam_hdr_t* header{nullptr};
        hts_idx_t* index{nullptr};

        GwAlignment() = default;
        ~GwAlignment();

        void open(const std::string& path, const std::string& reference, int threads);
    };


}
