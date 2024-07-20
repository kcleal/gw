//
// Created by Kez Cleal on 20/06/2024.
//

#pragma once

#include <iostream>
#include <string>
#include <unordered_map>

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


} // namespace Cyto