//
// Created by Kez Cleal on 04/08/2022.
//

#include <chrono>
#include <string>
#include <vector>

#include "htslib/hfile.h"
#include "htslib/hts.h"
#include "htslib/sam.h"

#include "plot_manager.h"
#include "segments.h"
#include "themes.h"


namespace HTS {

    Segs::Align make_align(bam1_t* src) {
        Segs::Align a;
        a.delegate = src;
        return a;
    }

    void collectReadsAndCoverage(Segs::ReadCollection& col, htsFile* b, sam_hdr_t *hdr_ptr,
                                 hts_idx_t *index,
                                 Themes::IniOptions &opts, Utils::Region* region) {

        bam1_t *src;
//        sam_hdr_t *hdr_ptr = sam_hdr_read(b);
        hts_itr_t *iter_q;
//        hts_idx_t *index;

        const char* chrom = region->chrom.c_str();
        std::cout << chrom << std::endl;
        auto start = std::chrono::high_resolution_clock::now();

        int tid = sam_hdr_name2tid(hdr_ptr, "chr1");
        int l_arr = (!opts.coverage) ? 0 : col.covArr.size() - 1;

        std::vector<Segs::Align>& readQueue = col.readQueue;

//        Segs::Align a;
//        a.delegate = bam_init1();
//        col.readQueue.push_back(std::move(a));  // is move needed?

        readQueue.push_back(make_align(bam_init1()));

        iter_q = sam_itr_queryi(index, tid, region->start, region->end);

        while (sam_itr_next(b, iter_q, readQueue.back().delegate) >= 0) {

            src = readQueue.back().delegate;
            if (src->core.flag & 4 || src->core.n_cigar == 0) {
                continue;
            }
            readQueue.push_back(make_align(bam_init1()));

        }

        src = readQueue.back().delegate;
        if (src->core.flag & 4 || src->core.n_cigar == 0) {
            readQueue.pop_back();
        }

        auto finish = std::chrono::high_resolution_clock::now();
        auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(finish - start);

        std::cout << "Elapsed Time: " << milliseconds.count() << " milli seconds" << std::endl;

        std::cout << col.covArr.empty() << " " << col.covArr.size() << std::endl;
        std::cout << col.readQueue.empty() << " " << col.readQueue.size() << std::endl;

        std::cout << "hi\n";
    }


}