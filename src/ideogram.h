//
// Created by Kez Cleal on 20/06/2024.
//

#pragma once

#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "export_definitions.h"

#include "include/core/SkPaint.h"


/* A collection of embedded ideograms for the preset reference genome tags
 * collected from ucsc table browser
 */
namespace Ideo {

    struct EXPORT Band {
        int start, end, alpha, red, green, blue;
        SkPaint paint;
        std::string name;
    };

    void get_hg19_cytoBand_bed(const unsigned char*& ptr, size_t& size);

    void get_hg38_cytoBand_bed(const unsigned char*& ptr, size_t& size);

    void get_t2t_cytoBand_bed(const unsigned char*& ptr, size_t& size);

    void get_mm39_cytoBand_bed(const unsigned char*& ptr, size_t& size);

    void get_ce11_cytoBand_bed(const unsigned char*& ptr, size_t& size);

    void get_danrer11_cytoBand_bed(const unsigned char*& ptr, size_t& size);

    // An ideogram GW can load for an assembly: an embedded tag (see GwPlot::loadIdeogramTag)
    // or a cytoband URL. grch37/grch38 are the chr-stripped hg19/hg38.
    struct EXPORT Source {
        const char* assembly;
        const char* label;
        const char* source;
    };

    const std::vector<Source>& embeddedIdeograms();

    // Cytobands from the IGV genome catalogue that are not embedded; fetched on demand
    const std::vector<Source>& onlineIdeograms();

    // Ideogram source for an IGV/UCSC assembly id (e.g. hs1, mm10), or "" when none is known
    std::string ideogramForAssembly(const std::string& assembly);


} // namespace Cyto