//
// Created by Kez Cleal on 04/08/2022.
//

#include <chrono>
#include <string>
#include <vector>

#include "htslib/hfile.h"
#include "htslib/hts.h"
#include "htslib/sam.h"

#include "../inc/BS_thread_pool.h"

#include "plot_manager.h"
#include "segments.h"
#include "themes.h"


namespace HTS {

    Segs::Align make_align(bam1_t* src) {
        Segs::Align a;
        a.delegate = src;
        return a;
    }

    void collectReadsAndCoverage(Segs::ReadCollection &col, htsFile *b, sam_hdr_t *hdr_ptr,
                                 hts_idx_t *index, Themes::IniOptions &opts, Utils::Region *region, bool coverage) {

        bam1_t *src;
        hts_itr_t *iter_q;

        int tid = sam_hdr_name2tid(hdr_ptr, "chr1");
        int l_arr = (!opts.coverage) ? 0 : col.covArr.size() - 1;

        std::vector<Segs::Align>& readQueue = col.readQueue;
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

        Segs::init_parallel(readQueue, opts.threads);

        if (coverage) {
            for (size_t i=0; i < readQueue.size(); ++i) {
                Segs::addToCovArray(col.covArr, &readQueue[i], region->start, l_arr);
            }
        }
    }
}