//
// Created by Kez Cleal on 04/08/2022.
//

#include <algorithm>
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
            bam_destroy1(src);
            readQueue.pop_back();
        }
        Segs::init_parallel(readQueue, opts.threads);
        if (coverage) {
            int l_arr = (int)col.covArr.size() - 1;
            for (auto &i : readQueue) {
                Segs::addToCovArray(col.covArr, i, region->start, region->end, l_arr);
            }
        }
        col.processed = true;
    }

    void trimToRegion(Segs::ReadCollection &col, bool coverage) {
        std::vector<Segs::Align>& readQueue = col.readQueue;
        Utils::Region *region = &col.region;
        while (!readQueue.empty()) {
            Segs::Align &item = readQueue.back();
            if (item.cov_start > region->end) {
                if (item.y != -1) {
                    col.levelsEnd[item.y] = item.cov_start;
                }
                readQueue.pop_back();
            } else {
                break;
            }
        }
        int idx = 0;
        for (auto &item : readQueue) {  // drop out of scope reads
            if (item.cov_end < region->start) {
                if (item.y != -1) {
                    col.levelsStart[item.y] = item.cov_end;
                }
                bam_destroy1(item.delegate);
                idx += 1;
            } else {
                break;
            }
        }
        if (idx > 0) {
            readQueue.erase(readQueue.begin(), readQueue.begin() + idx);
        }
        if (coverage) {  // re process coverage for all reads
            col.covArr.resize(col.region.end - col.region.start + 1);
            std::fill(col.covArr.begin(), col.covArr.end(), 0);
            int l_arr = (int)col.covArr.size() - 1;
            for (auto &i : col.readQueue) {
                Segs::addToCovArray(col.covArr, i, col.region.start, col.region.end, l_arr);
            }
        }
    }

    void appendReadsAndCoverage(Segs::ReadCollection &col, htsFile *b, sam_hdr_t *hdr_ptr,
                                 hts_idx_t *index, Themes::IniOptions &opts, bool coverage, bool left, int *vScroll, Segs::linked_t &linked, int *samMaxY) {

        bam1_t *src;
        hts_itr_t *iter_q;

        std::vector<Segs::Align>& readQueue = col.readQueue;
        Utils::Region *region = &col.region;

        int tid = sam_hdr_name2tid(hdr_ptr, region->chrom.c_str());

        int lastPos;
        if (!readQueue.empty()) {
            if (left) {
                lastPos = readQueue.front().pos; // + 1;
            } else {
                lastPos = readQueue.back().pos; // + 1;
            }
        } else {
            if (left) {
                lastPos = 1215752190;
            } else {
                lastPos = 0;
            }
        }

//        std::cout << "before " << readQueue.front().pos << " " << readQueue.back().pos << std::endl;

        std::vector<Segs::Align> newReads;
        if (left && readQueue.front().cov_end > region->start) {
            while (!readQueue.empty()) {  // remove items from RHS of queue, reduce levelsEnd
                Segs::Align &item = readQueue.back();
                if (item.cov_start > region->end) {
                    if (item.y != -1) {
                        col.levelsEnd[item.y] = item.cov_start - 1;
                        if (col.levelsStart[item.y] == col.levelsEnd[item.y]) {
                            col.levelsStart[item.y] = 1215752191;
                            col.levelsEnd[item.y] = 0;
                        }
                    }
                    bam_destroy1(readQueue.back().delegate);
                    readQueue.pop_back();
                } else {
                    break;
                }
            }
            if (readQueue.empty()) {
                std::fill(col.levelsStart.begin(), col.levelsStart.end(), 1215752191);
                std::fill(col.levelsEnd.begin(), col.levelsEnd.end(), 0);
            }

            iter_q = sam_itr_queryi(index, tid, region->start, readQueue.front().reference_end);
            newReads.push_back(make_align(bam_init1()));

            while (sam_itr_next(b, iter_q, newReads.back().delegate) >= 0) {
                src = newReads.back().delegate;
                if (src->core.flag & 4 || src->core.n_cigar == 0) {
                    continue;
                }
                if (src->core.pos >= lastPos) {
                    break;
                }
                newReads.push_back(make_align(bam_init1()));
            }
            src = newReads.back().delegate;
            if (src->core.flag & 4 || src->core.n_cigar == 0 || src->core.pos >= lastPos) {
                bam_destroy1(src);
                newReads.pop_back();
            }

        } else if (!left && lastPos < region->end) {
            int idx = 0;
            for (auto &item : readQueue) {  // drop out of scope reads
                if (item.cov_end < region->start) {
                    if (item.y != -1) {
                        col.levelsStart[item.y] = item.cov_end;
                        if (col.levelsStart[item.y] == col.levelsEnd[item.y]) {
                            col.levelsStart[item.y] = 1215752191;
                            col.levelsEnd[item.y] = 0;
                        }
                    }
                    bam_destroy1(item.delegate);
                    idx += 1;
                } else {
                    break;
                }
            }
            if (idx > 0) {
                readQueue.erase(readQueue.begin(), readQueue.begin() + idx);
            }



            if (readQueue.empty()) {
                std::fill(col.levelsStart.begin(), col.levelsStart.end(), 1215752191);
                std::fill(col.levelsEnd.begin(), col.levelsEnd.end(), 0);
            }
            iter_q = sam_itr_queryi(index, tid, lastPos, region->end);
            newReads.push_back(make_align(bam_init1()));
            while (sam_itr_next(b, iter_q, newReads.back().delegate) >= 0) {
                src = newReads.back().delegate;
                if (src->core.flag & 4 || src->core.n_cigar == 0 || src->core.pos <= lastPos) {
                    continue;
                }
                if (src->core.pos > region->end) {
                    break;
                }
                newReads.push_back(make_align(bam_init1()));
            }
            src = newReads.back().delegate;
            if (src->core.flag & 4 || src->core.n_cigar == 0 || src->core.pos <= lastPos || src->core.pos > region->end) {
                bam_destroy1(src);
                newReads.pop_back();
            }
        }
        if (!newReads.empty()) {
            Segs::init_parallel(newReads, 1);
            int maxY = Segs::findY(col.bamIdx, col, newReads, *vScroll, opts.link_op, opts, region, linked, left);
            if (maxY > *samMaxY) {
                *samMaxY = maxY;
            }
            if (!left) {
                std::move(newReads.begin(), newReads.end(), std::back_inserter(readQueue));
            } else {
                std::move(readQueue.begin(), readQueue.end(), std::back_inserter(newReads));
                col.readQueue = newReads;
            }
        }
        if (coverage) {  // re process coverage for all reads
            col.covArr.resize(region->end - region->start + 1);
            std::fill(col.covArr.begin(), col.covArr.end(), 0);
            int l_arr = (int)col.covArr.size() - 1;
            for (auto &i : readQueue) {
                Segs::addToCovArray(col.covArr, i, region->start, region->end, l_arr);
            }
        }
        col.processed = true;
    }


    VCF::~VCF() {
        if (fp != nullptr) {
            vcf_close(fp);
            bcf_destroy(v);
        }
    }

    void VCF::open(std::string f) {
        done = false;
        path = f;
        fp = bcf_open(f.c_str(), "r");
        hdr = bcf_hdr_read(fp);
        v = bcf_init1();

        std::string l2p(label_to_parse);
        v->max_unpack = BCF_UN_INFO;

        if (l2p.empty()) {
            parse = -1;
        } else if (l2p.rfind("info.", 0) == 0) {
            parse = 7;
            info_field_type = -1;
            tag = l2p.substr(5, l2p.size() - 5);
            bcf_idpair_t *id = hdr->id[0];
            for (int i=0; i< hdr->n[0]; ++i) {
                std::string key = id->key;
                if (key == tag) {
                    // this gives the info field type?! ouff
                    info_field_type = id->val->info[BCF_HL_INFO] >>4 & 0xf;
                    break;
                }
                ++id;
            }
            if (info_field_type == -1) {
                std::cerr << "Error: could not find --parse-label in info" << std::endl;
                std::terminate();
            }
        } else if (l2p.find("filter") != std::string::npos) {
            parse = 6;
        } else if (l2p.find("qual") != std::string::npos) {
            parse = 5;
        } else if (l2p.find("id") != std::string::npos) {
            parse = 2;
        } else {
            std::cerr << "Error: --label-to-parse was not understood, accepted fields are 'id / qual / filter / info.$NAME'";
            std::terminate();
        }
    }

    void VCF::next() {
        int res = bcf_read(fp, hdr, v);
        if (res < -1) {
            std::cerr << "Error: reading vcf resulted in error code " << res << std::endl;
            std::terminate();
        } else if (res == -1) {
            done = true;
        }
        bcf_unpack(v, BCF_UN_INFO);

        start = v->pos;
        stop = start + v->rlen;
        chrom = bcf_hdr_id2name(hdr, v->rid);
        rid = v->d.id;

        int variant_type = bcf_get_variant_types(v);
        char *strmem = nullptr;
        int mem = 0;
        if (variant_type == VCF_SNP || variant_type == VCF_INDEL || variant_type == VCF_OVERLAP) {
            chrom2 = chrom;
        } else {  // variant type is VCF_REF or VCF_OTHER or VCF_BND
            if (variant_type == VCF_BND) {
                chrom2 = chrom;  // todo deal with BND types here
            } else {
                bcf_info_t *info_field = bcf_get_info(hdr, v, "CHR2");  // try and find chrom2 in info
                if (info_field != nullptr) {
                    int resc = bcf_get_info_string(hdr,v,"CHR2",&strmem,&mem);
                    if (resc < 0) {
                        std::cerr << "Error: could not parse CHR2 field, error was " << resc << std::endl;
                        std::terminate();
                    }
                    chrom2 = strmem;
                } else {
                    chrom2 = chrom;  // todo deal should this raise an error?
                }
            }
        }

        label = "";
        char *strmem2 = nullptr;
        int resw = -1;
        int mem2 = 0;
        int32_t *ires = nullptr;
        float *fres = nullptr;
        std::string parsedVal;
        switch (parse) {
            case -1:
                label = ""; break;
            case 2:
                label = rid; break;
            case 5:
                label = std::to_string(v->qual); break;
            case 6:  // parse filter field
                if (v->d.n_flt == 0) {
                    label = "PASS";
                } else {
                    label = hdr->id[BCF_DT_ID][*v->d.flt].key;  // does this work for multiple filters?
//                    std::cout << "Filter key and value " << *v->d.flt << " " << hdr->id[BCF_DT_ID][*v->d.flt].key << std::endl;
                }
                break;
            case 7:
                switch (info_field_type) {
                    case BCF_HT_INT:
                        resw = bcf_get_info_int32(hdr,v,tag.c_str(),&ires,&mem2);
                        parsedVal = std::to_string(*ires);
                        break;
                    case BCF_HT_REAL:
                        resw = bcf_get_info_float(hdr,v,tag.c_str(),&fres,&mem2);
                        parsedVal = std::to_string(*fres);
                        break;
                    case BCF_HT_STR:
                        resw = bcf_get_info_string(hdr,v,tag.c_str(),&strmem2,&mem2);
                        parsedVal = strmem2;
                        break;
                    case BCF_HT_FLAG:
                        resw = bcf_get_info_flag(hdr,v,tag.c_str(),0,0);
                        break;
                    default:
                        resw = bcf_get_info_string(hdr,v,tag.c_str(),&strmem2,&mem2);
                        parsedVal = strmem2;
                        break;
                }
                if (resw == -1) {
                    std::cerr << "Error: could not parse tag " << tag << " from info field" << std::endl;
                    std::terminate();
                }
                break;
        }
    }
}