//
// Created by Kez Cleal on 04/08/2022.
//

#include <chrono>
#include <string>
#include <vector>

#include "htslib/hfile.h"
#include "htslib/hts.h"
#include "htslib/sam.h"
#include "htslib/vcf.h"

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

        int tid = sam_hdr_name2tid(hdr_ptr, region->chrom.c_str());
        int l_arr = (!opts.coverage) ? 0 : col.covArr.size() - 1;

        std::vector<Segs::Align>& readQueue = col.readQueue;
        readQueue.push_back(make_align(bam_init1()));

        iter_q = sam_itr_queryi(index, tid, region->start, region->end);
        if (iter_q == nullptr) {
            std::cerr << "Error: Null iterator when trying to fetch from HTS file\n";
            std::terminate();
        }
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
            std::cout << region->start << std::endl;
            for (size_t i=0; i < readQueue.size(); ++i) {
                Segs::addToCovArray(col.covArr, &readQueue[i], region->start, region->end, l_arr);
            }
        }
    }

    VCF::~VCF() {
        vcf_close(fp);
        bcf_destroy(v);
    }

    void VCF::open(std::string f) {
        path = f;
        fp = bcf_open(f.c_str(), "r");
        hdr = bcf_hdr_read(fp);
        v = bcf_init1();
        v->max_unpack = BCF_UN_SHR;  // don't unpack more data than neeeded
    }

    void VCF::next() {
        int res = bcf_read(fp, hdr, v);
        if (res < -1) {
            std::cerr << "Error: reading vcf resulted in error code " << res << std::endl;
            std::terminate();
        } else if (res == -1) {
            done = true;
        }

        start = v->pos;
        stop = start + v->rlen;
        chrom = bcf_hdr_id2name(hdr, v->rid);


//        int res2 = bcf_unpack(v, BCF_UN_SHR);
//        if (res2 < -1) {
//            std::cerr << "Error: unpacking vcf resulted in error code " << res << std::endl;
//            std::terminate();
//        }



    }
}