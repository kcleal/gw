//
// Created by kez on 14/02/25.
//

#include <string>
#include <vector>
#include <stdexcept>

#include "htslib/faidx.h"
#include "htslib/hfile.h"
#include "htslib/hts.h"
#include "htslib/vcf.h"
#include "htslib/sam.h"
#include "htslib/tbx.h"

#include "export_definitions.h"
#include "alignment_format.h"
#include "utils.h"


namespace AlignFormat {

    void GwAlignment::open(const std::string& file_path, const std::string& reference, int threads) {
        path = file_path;
        if (Utils::endsWith(path, "bam") || Utils::endsWith(path, "cram")) {
            type = AlignmentType::HTSLIB_t;
            bam = sam_open(path.c_str(), "r");
            if (!bam) {
                throw std::runtime_error("Failed to open BAM file: " + path);
            }
            hts_set_fai_filename(bam, reference.c_str());
            hts_set_threads(bam, threads);
            header = sam_hdr_read(bam);
            index = sam_index_load(bam, path.c_str());
        } else if (Utils::endsWith(path, "gaf")) {
            type = AlignmentType::GAF_t;
        }

    }

    GwAlignment::~GwAlignment() {
        if (index) hts_idx_destroy(index);
        if (header) sam_hdr_destroy(header);
        if (bam) sam_close(bam);
    }

}