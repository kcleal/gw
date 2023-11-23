//
// Created by Kez Cleal on 04/08/2022.
//

#include <algorithm>
#include <string>
#include <vector>

#include "htslib/hts.h"
#include "htslib/sam.h"
#include "htslib/tbx.h"
#include "htslib/vcf.h"

#include "../include/BS_thread_pool.h"
#include "../include/termcolor.h"
#include "drawing.h"
#include "segments.h"
#include "themes.h"
#include "hts_funcs.h"


namespace HGW {

    void guessRefGenomeFromBam(std::string &inputName, Themes::IniOptions &opts, std::vector<std::string> &bam_paths, std::vector<Utils::Region> &regions) {
        htsFile* f = sam_open(inputName.c_str(), "r");
        sam_hdr_t *hdr_ptr = sam_hdr_read(f);
        bool success = false;
        std::string longestName;
        int longest = 0;
        for (auto & refName : opts.myIni["genomes"]) {
            faidx_t *fai = fai_load(refName.second.c_str());
            if (!fai) {
                continue;
            }
            success = false;
            for (int tid=0; tid < hdr_ptr->n_targets; tid++) {
                const char *chrom_name = sam_hdr_tid2name(hdr_ptr, tid);
                int bam_length = (int)sam_hdr_tid2len(hdr_ptr, tid);
                if (bam_length > longest) {
                    longestName = chrom_name;
                    longest = bam_length;
                }
                if (!faidx_has_seq(fai, chrom_name) || (bam_length != faidx_seq_len(fai, chrom_name))) {
                    success = false;
                    break;
                }
                if (tid > 23) {
                    break;
                }
                success = true;
            }
            if (success) {
                bam_paths.push_back(inputName);
                inputName = refName.second;
                regions.push_back(Utils::parseRegion(longestName));
                break;
            }
        }
        if (!success) {
            std::cerr << termcolor::red << "Error:" << termcolor::reset << " could not find a matching reference genome in .gw.ini file";
        }
    }

    void applyFilters(std::vector<Parse::Parser> &filters, std::vector<Segs::Align>& readQueue, const sam_hdr_t* hdr,
                      int bamIdx, int regionIdx) {
        auto end = readQueue.end();
        auto rm_iter = readQueue.begin();
        const auto pred = [&](const Segs::Align &align){
            if (rm_iter == end) { return false; }
            bool drop = false;
            for (auto &f: filters) {
                bool passed = f.eval(align, hdr, bamIdx, regionIdx);
                if (!passed) {
                    drop = true;
                    break;
                }
            }
            return drop;
        };
        readQueue.erase(std::remove_if(readQueue.begin(), readQueue.end(), pred), readQueue.end());
    }

    void collectReadsAndCoverage(Segs::ReadCollection &col, htsFile *b, sam_hdr_t *hdr_ptr,
                                 hts_idx_t *index, int threads, Utils::Region *region,
                                 bool coverage, bool low_mem,
                                 std::vector<Parse::Parser> &filters, BS::thread_pool &pool) {
        bam1_t *src;
        hts_itr_t *iter_q;
        int tid = sam_hdr_name2tid(hdr_ptr, region->chrom.c_str());
        std::vector<Segs::Align>& readQueue = col.readQueue;
        if (region->end - region->start < 1000000) {
            try {
                readQueue.reserve((region->end - region->start) * 60);
            } catch (const std::bad_alloc&) {
            }
        }
        readQueue.emplace_back(bam_init1());
        iter_q = sam_itr_queryi(index, tid, region->start, region->end);
        if (iter_q == nullptr) {
            std::cerr << "\nError: Null iterator when trying to fetch from HTS file in collectReadsAndCoverage " << region->chrom << " " << region->start << " " << region->end << std::endl;
            std::terminate();
        }

        while (sam_itr_next(b, iter_q, readQueue.back().delegate) >= 0) {
            src = readQueue.back().delegate;
            if (src->core.flag & 4 || src->core.n_cigar == 0) {
                continue;
            }
            readQueue.emplace_back(bam_init1());
            if (low_mem) {
                size_t new_len = (src->core.n_cigar << 2 ) + src->core.l_qname + ((src->core.l_qseq + 1) >> 1);
                src->data = (uint8_t*)realloc(src->data, new_len);
                src->l_data = (int)new_len;
            }
        }
        src = readQueue.back().delegate;
        if (src->core.flag & 4 || src->core.n_cigar == 0) {
            bam_destroy1(src);
            readQueue.pop_back();
        }

        if (!filters.empty()) {
            applyFilters(filters, readQueue, hdr_ptr, col.bamIdx, col.regionIdx);
        }

        Segs::init_parallel(readQueue, threads, pool);

        if (coverage) {
            int l_arr = (int)col.covArr.size() - 1;
            for (auto &i : readQueue) {
                Segs::addToCovArray(col.covArr, i, region->start, region->end, l_arr);
            }
        }
        col.collection_processed = false;
    }


    void iterDraw(std::vector< Segs::ReadCollection > &cols, int idx, htsFile *b, sam_hdr_t *hdr_ptr,
                                 hts_idx_t *index, int threads, Utils::Region *region,
                                 bool coverage, bool low_mem,
                                 std::vector<Parse::Parser> &filters, Themes::IniOptions &opts, SkCanvas *canvas,
                                 float trackY, float yScaling, Themes::Fonts &fonts, float refSpace) {

        bam1_t *src;
        hts_itr_t *iter_q;
        Segs::ReadCollection &col = cols[idx];
        int tid = sam_hdr_name2tid(hdr_ptr, region->chrom.c_str());
        std::vector<Segs::Align>& readQueue = col.readQueue;
        if (!readQueue.empty()) {
            for (auto &item: readQueue) {
                bam_destroy1(item.delegate);
            }
            readQueue.clear();
        }
        readQueue.emplace_back(bam_init1());
        iter_q = sam_itr_queryi(index, tid, region->start, region->end);
        if (iter_q == nullptr) {
            std::cerr << "\nError: Null iterator when trying to fetch from HTS file in collectReadsAndCoverage " << region->chrom << " " << region->start << " " << region->end << std::endl;
            std::terminate();
        }

        while (sam_itr_next(b, iter_q, readQueue.back().delegate) >= 0) {
            src = readQueue.back().delegate;
            if (src->core.flag & 4 || src->core.n_cigar == 0) {
                continue;
            }
            if (!filters.empty()) {
                applyFilters(filters, readQueue, hdr_ptr, col.bamIdx, col.regionIdx);
            }
            if (!readQueue.empty()) {
                Segs::align_init(&readQueue.back());
                if (coverage) {
                    int l_arr = (int)col.covArr.size() - 1;
                    Segs::addToCovArray(col.covArr, readQueue.back(), region->start, region->end, l_arr);

                }
                Segs::findY(col, readQueue, opts.link_op, opts, region, false);
                Drawing::drawBams(opts, cols, canvas, trackY, yScaling, fonts, opts.link_op, refSpace);
                Segs::align_clear(&readQueue.back());
            }
        }
    }


    void trimToRegion(Segs::ReadCollection &col, bool coverage, int snp_threshold) {
        std::vector<Segs::Align>& readQueue = col.readQueue;
        Utils::Region *region = col.region;
        while (!readQueue.empty()) {
            Segs::Align &item = readQueue.back();
            if (item.cov_start > region->end + 1000) {
                if (item.y >= 0 && !col.levelsEnd.empty()) {
                    col.levelsEnd[item.y] = item.cov_start - 1;
                }
                bam_destroy1(item.delegate);
                readQueue.pop_back();
            } else {
                break;
            }
        }
        int idx = 0;
        for (auto &item : readQueue) {  // drop out of scope reads
            if (item.cov_end < region->start - 1000) {
                if (item.y >= 0 && !col.levelsStart.empty()) {
                    col.levelsStart[item.y] = item.cov_end + 1;
                }
                bam_destroy1(item.delegate);
                idx += 1;
            } else {
                break;
            }
        }
        if (idx > 0) {
            readQueue.erase(readQueue.begin(), readQueue.begin() + idx);
            readQueue.shrink_to_fit();
        }
        if (coverage) {  // re process coverage for all reads
            col.covArr.resize(region->end - region->start + 1);
            std::fill(col.covArr.begin(), col.covArr.end(), 0);
            int l_arr = (int)col.covArr.size() - 1;
            for (auto &i : col.readQueue) {
                Segs::addToCovArray(col.covArr, i, region->start, region->end, l_arr);
            }
            if (snp_threshold > region->end - region->start) {
                col.mmVector.resize(region->end - region->start + 1);
                Segs::Mismatches empty_mm{};
                std::fill(col.mmVector.begin(), col.mmVector.end(), empty_mm);
            }
        }
    }

    void refreshLinkedCollection(Segs::ReadCollection &cl, Themes::IniOptions &opts, int *samMaxY) {
        Segs::resetCovStartEnd(cl);
        cl.levelsStart.clear();
        cl.levelsEnd.clear();
        cl.linked.clear();
        for (auto &itm: cl.readQueue) { itm.y = -1; }
        int maxY = Segs::findY(cl, cl.readQueue, opts.link_op, opts, cl.region, false);
        *samMaxY = (maxY > *samMaxY || opts.tlen_yscale) ? maxY : *samMaxY;
    }

    void refreshLinked(std::vector<Segs::ReadCollection> &collections, Themes::IniOptions &opts, int *samMaxY) {
        for (auto &cl : collections) {
            refreshLinkedCollection(cl, opts, samMaxY);
        }
    }

    void appendReadsAndCoverage(Segs::ReadCollection &col, htsFile *b, sam_hdr_t *hdr_ptr,
                                 hts_idx_t *index, Themes::IniOptions &opts, bool coverage, bool left, int *samMaxY,
                                std::vector<Parse::Parser> &filters, BS::thread_pool &pool) {
        bam1_t *src;
        hts_itr_t *iter_q;
        std::vector<Segs::Align>& readQueue = col.readQueue;
        Utils::Region *region = col.region;
        bool tlen_y = opts.tlen_yscale;
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

        std::vector<Segs::Align> newReads;
        if (left && (readQueue.empty() || readQueue.front().cov_end > region->start)) {
            while (!readQueue.empty()) {  // remove items from RHS of queue, reduce levelsEnd
                Segs::Align &item = readQueue.back();
                if (item.cov_start > region->end) {
                    if (item.y != -1 && !tlen_y) {
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
            int end_r;
            if (readQueue.empty()) {
                std::fill(col.levelsStart.begin(), col.levelsStart.end(), 1215752191);
                std::fill(col.levelsEnd.begin(), col.levelsEnd.end(), 0);
                end_r = region->end;
            } else {
                end_r = readQueue.front().reference_end;
                if (end_r < region->start) {
                    return; // reads are already in the queue
                }
            }

            // not sure why this is needed. Without the left pad, some alignments are not collected for small regions??
            long begin = (region->start - 1000) > 0 ? region->start - 1000 : 0;
            iter_q = sam_itr_queryi(index, tid, begin, end_r);
            if (iter_q == nullptr) {
                std::cerr << "\nError: Null iterator when trying to fetch from HTS file in appendReadsAndCoverage (left) " << region->chrom << " " << region->start<< " " << end_r << " " << region->end << std::endl;
                std::terminate();
            }
            newReads.emplace_back(Segs::Align(bam_init1()));

            while (sam_itr_next(b, iter_q, newReads.back().delegate) >= 0) {
                src = newReads.back().delegate;
                if (src->core.flag & 4 || src->core.n_cigar == 0) {
                    continue;
                }
                if (src->core.pos >= lastPos) {
                    break;
                }
                newReads.emplace_back(Segs::Align(bam_init1()));
                if (opts.low_mem) {
                    size_t new_len = (src->core.n_cigar << 2 ) + src->core.l_qname + ((src->core.l_qseq + 1) >> 1);
                    src->data = (uint8_t*)realloc(src->data, new_len);
                    src->l_data = (int)new_len;
                }
            }
            src = newReads.back().delegate;
            if (src->core.flag & 4 || src->core.n_cigar == 0 || src->core.pos >= lastPos) {
                bam_destroy1(src);
                newReads.pop_back();
            }

        } else if (!left && lastPos < region->end) {
            int idx = 0;
            for (auto &item : readQueue) {  // drop out of scope reads
                if (item.cov_end < region->start - 1000) {
                    if (item.y != -1 && !tlen_y) {
                        col.levelsStart[item.y] = item.cov_end + 1;
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
                iter_q = sam_itr_queryi(index, tid, region->start, region->end);
            } else {
                iter_q = sam_itr_queryi(index, tid, lastPos, region->end);
            }
            if (iter_q == nullptr) {
                std::cerr << "\nError: Null iterator when trying to fetch from HTS file in appendReadsAndCoverage (!left) " << region->chrom << " " << lastPos << " " << region->end << std::endl;
                std::terminate();
            }
            newReads.emplace_back(Segs::Align(bam_init1()));

            while (sam_itr_next(b, iter_q, newReads.back().delegate) >= 0) {
                src = newReads.back().delegate;
                if (src->core.flag & 4 || src->core.n_cigar == 0 || src->core.pos <= lastPos) {
                    continue;
                }
                if (src->core.pos > region->end) {
                    break;
                }
                newReads.emplace_back(Segs::Align(bam_init1()));
                if (opts.low_mem) {
                    size_t new_len = (src->core.n_cigar << 2 ) + src->core.l_qname + ((src->core.l_qseq + 1) >> 1);
                    src->data = (uint8_t*)realloc(src->data, new_len);
                    src->l_data = (int)new_len;
                }
            }
            src = newReads.back().delegate;
            if (src->core.flag & 4 || src->core.n_cigar == 0 || src->core.pos <= lastPos || src->core.pos > region->end) {
                bam_destroy1(src);
                newReads.pop_back();
            }
        }

        if (!newReads.empty()) {
            if (!filters.empty()) {
                applyFilters(filters, newReads, hdr_ptr, col.bamIdx, col.regionIdx);
            }
            Segs::init_parallel(newReads, opts.threads, pool);
            bool findYall = false;
            if (col.vScroll == 0 && opts.link_op == 0) {  // only new reads need findY, otherwise, reset all below
                int maxY = Segs::findY(col, newReads, opts.link_op, opts, region,  left);
                if (maxY > *samMaxY) {
                    *samMaxY = maxY;
                }
            } else {
                findYall = true;
            }

            if (!left) {
                std::move(newReads.begin(), newReads.end(), std::back_inserter(readQueue));
            } else {
                std::move(readQueue.begin(), readQueue.end(), std::back_inserter(newReads));
                col.readQueue = newReads;
            }
            if (findYall) {
                refreshLinkedCollection(col, opts, samMaxY);
            }
            if (opts.link_op > 0) {
                // move of data will invalidate some pointers, so reset
                col.linked.clear();
                int linkType = opts.link_op;
                for (auto &v : col.readQueue) {
                    if (linkType == 1) {
                        if (v.has_SA || ~v.delegate->core.flag & 2) {
                            col.linked[bam_get_qname(v.delegate)].push_back(&v);
                        }
                    } else {
                        col.linked[bam_get_qname(v.delegate)].push_back(&v);
                    }
                }
            }

        }
        if (coverage) {  // re process coverage for all reads
//            auto start = std::chrono::high_resolution_clock::now();
            col.covArr.resize(region->end - region->start + 1);
            std::fill(col.covArr.begin(), col.covArr.end(), 0);
            int l_arr = (int)col.covArr.size() - 1;
            for (auto &i : readQueue) {
                Segs::addToCovArray(col.covArr, i, region->start, region->end, l_arr);
            }
            if (opts.snp_threshold > region->end - region->start) {
                col.mmVector.resize(region->end - region->start + 1);
                Segs::Mismatches empty_mm{};
                std::fill(col.mmVector.begin(), col.mmVector.end(), empty_mm);
            } else {
                col.mmVector.clear();
            }
//            auto stop = std::chrono::high_resolution_clock::now();
//            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start).count();
//            std::cerr << " resizing and stuff! " << duration << std::endl;
        }
        col.collection_processed = false;
    }

    VCFfile::~VCFfile() {
        // Using these cause memory freeing issues?
//        if (fp && !path.empty()) {
//            vcf_close(fp);
//            bcf_destroy(v);
//        }
//        if (!lines.empty()) {
//            for (auto &v: lines) {
//                bcf_destroy1(v);
//            }
//        }
    }

    void VCFfile::open(const std::string &f) {
        done = false;
        path = f;
        if (Utils::endsWith(path, ".vcf")) {
            kind = VCF_NOI;
        } else if (cacheStdin) {
            kind = STDIN;
        } else if (Utils::endsWith(path, ".bcf")) {
            kind = BCF_IDX;
            idx_v = bcf_index_load(path.c_str());
        } else {
            kind = VCF_IDX;
            idx_t = tbx_index_load(path.c_str());
        }

        fp = bcf_open(f.c_str(), "r");
        hdr = bcf_hdr_read(fp);
		samples_loaded = false;
        v = bcf_init1();

        std::string l2p(label_to_parse);
        v->max_unpack = BCF_UN_INFO;
		if (cacheStdin) {
			v->max_unpack = BCF_UN_ALL;
		}
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

    void VCFfile::next() {
        int res = bcf_read(fp, hdr, v);
        if (cacheStdin) {
            lines.push_back(bcf_dup(v));
        }

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
        int *intmem = nullptr;
        int mem = 0;
        int imem = 0;
        bcf_info_t *info_field;

        switch (variant_type) {
            case VCF_SNP: vartype = "SNP"; break;
            case VCF_INDEL: vartype = "INDEL"; break;
            case VCF_OVERLAP: vartype = "OVERLAP"; break;
            case VCF_BND: vartype = "BND"; break;
            case VCF_REF: vartype = "REF"; break;
            case VCF_OTHER: vartype = "OTHER"; break;
            case VCF_MNP: vartype = "MNP"; break;
            default: vartype = "NA"; break;
        }

        if (variant_type == VCF_SNP || variant_type == VCF_INDEL || variant_type == VCF_OVERLAP) {
            chrom2 = chrom;
        } else {  // variant type is VCF_REF or VCF_OTHER or VCF_BND
            if (variant_type == VCF_BND) {
                chrom2 = chrom;  // todo deal with BND types here
            } else {
                info_field = bcf_get_info(hdr, v, "CHR2");  // try and find chrom2 in info
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
                info_field = bcf_get_info(hdr, v, "CHR2_POS");  // try and find chrom2 in info
                if (info_field != nullptr) {
                    int resc = bcf_get_info_int32(hdr, v, "CHR2_POS", &intmem, &imem);
                    if (resc < 0) {
                        std::cerr << "Error: could not parse CHR2 field, error was " << resc << std::endl;
                        std::terminate();
                    }
                    stop = *intmem;
                }
            }
        }

        info_field = bcf_get_info(hdr, v, "SVTYPE");  // try and find chrom2 in info
        if (info_field != nullptr) {
            char *svtmem = nullptr;
            mem = 0;
            int resc = bcf_get_info_string(hdr, v, "SVTYPE", &svtmem,&mem);
            if (resc < 0) {
            } else {
                vartype = svtmem;
            }
        }

        label = "";
        char *strmem2 = nullptr;
        int resw = -1;
        int mem2 = 0;
        int32_t *ires = nullptr;
        float *fres = nullptr;
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
                }
                break;
            case 7:
                switch (info_field_type) {
                    case BCF_HT_INT:
                        resw = bcf_get_info_int32(hdr,v,tag.c_str(),&ires,&mem2);
                        label = std::to_string(*ires);
                        break;
                    case BCF_HT_REAL:
                        resw = bcf_get_info_float(hdr,v,tag.c_str(),&fres,&mem2);
                        label = std::to_string(*fres);
                        break;
                    case BCF_HT_STR:
                        resw = bcf_get_info_string(hdr,v,tag.c_str(),&strmem2,&mem2);
                        label = strmem2;
                        break;
                    case BCF_HT_FLAG:
                        resw = bcf_get_info_flag(hdr,v,tag.c_str(),0,0);
                        break;
                    default:
                        resw = bcf_get_info_string(hdr,v,tag.c_str(),&strmem2,&mem2);
                        label = strmem2;
                        break;
                }
                if (resw == -1) {
                    std::cerr << "Error: could not parse tag " << tag << " from info field" << std::endl;
                    std::terminate();
                }
                break;
        }
        if (!(*seenLabels).empty() && !seenLabels->contains(label)) {
            seenLabels->insert(label);
        }
    }

	void VCFfile::printTargetRecord(std::string &id_str, std::string &chrom, int pos) {
        if (kind == BCF_IDX) {
            return print_BCF_IDX(idx_v, hdr, chrom, pos, fp, id_str, variantString);
        }
        else if (kind == VCF_NOI) {
            return print_VCF_NOI(path, id_str, variantString);
        }
        else if (kind == VCF_IDX) {
            return print_VCF_IDX(path, id_str, chrom, pos, variantString);
        }
        else if (kind == STDIN || cacheStdin) {
            kstring_t kstr = {0,0,0};
            std::string tmp_id;
            for (auto &it : lines) {
                bcf_unpack(it, BCF_UN_ALL);
                tmp_id = it->d.id;
                if (tmp_id == id_str) {
                    vcf_format(hdr, it, &kstr);
					variantString = kstr.s;
					break;
                } else if (pos < it->pos) {
                    break;
                }
            }
        }
    }

	void VCFfile::get_samples() {
		if (!samples_loaded) {
			char ** tmpArr = hdr->samples;
			std::vector<std::string> tmp_sample_names(tmpArr, tmpArr + bcf_hdr_nsamples(hdr));
			sample_names = tmp_sample_names;
			samples_loaded = true;
		}
	}

    void print_BCF_IDX(hts_idx_t *idx_v, bcf_hdr_t *hdr, std::string &chrom, int pos, htsFile *fp, std::string &id_str, std::string &variantString) {
        htsFile *fp2 = fp;
        kstring_t kstr = {0,0,0};
        bcf1_t *tv = bcf_init1();
        std::string tmp_id;
        hts_itr_t *iter_q = bcf_itr_queryi(idx_v, bcf_hdr_name2id(hdr, chrom.c_str()), pos-10, pos+10);
        while (true) {
            int res = bcf_itr_next(fp, iter_q, tv);
            if (res < 0) {
                if (res < -1) {
                    std::cerr << "Error: iterating bcf file returned " << res << std::endl;
                }
                break;
            }
            bcf_unpack(tv, BCF_UN_STR);
            tmp_id = tv->d.id;
            if (tmp_id == id_str) {
                vcf_format1(hdr, tv, &kstr);
				variantString = kstr.s;
                break;
            }
        }
        fp = fp2;
    }

    void print_VCF_NOI(std::string &path, std::string &id_str, std::string &variantString) {
        kstring_t kstr = {0,0,0};
        bcf1_t *tv = bcf_init1();
        std::string tmp_id;
        htsFile *_fp = bcf_open(path.c_str(), "r");
        bcf_hdr_t *_hdr = bcf_hdr_read(_fp);
        while (bcf_read(_fp, _hdr, tv) == 0) {
            bcf_unpack(tv, BCF_UN_STR);
            tmp_id = tv->d.id;
            if (tmp_id == id_str) {
                vcf_format1(_hdr, tv, &kstr);
				variantString = kstr.s;
                break;
            }
        }
    }

    void print_VCF_IDX(std::string &path, std::string &id_str, std::string &chrom, int pos, std::string &variantString) {
        kstring_t kstr = {0,0,0};
        bcf1_t *tv = bcf_init1();
        std::string tmp_id;
        htsFile *_fp = bcf_open(path.c_str(), "r");
        bcf_hdr_t *_hdr = bcf_hdr_read(_fp);
        tbx_t *_idx_t = tbx_index_load(path.c_str());
        hts_itr_t *_iter_q = tbx_itr_queryi(_idx_t, tbx_name2id(_idx_t, chrom.c_str()), pos-10, pos+10);
        while (true) {
            int res = tbx_itr_next(_fp, _idx_t, _iter_q, &kstr);
            if (res < 0) {
                if (res < -1) {
                    std::cerr << "Error: iterating vcf file returned " << res << std::endl;
                }
                break;
            }
            std::string l = kstr.s;
            res = vcf_parse(&kstr, _hdr, tv);
            if (res < 0) {
                std::cerr << "Error: parsing vcf record returned " << res << std::endl;
            }
            bcf_unpack(tv, BCF_UN_STR);
            tmp_id = tv->d.id;
            if (tmp_id == id_str) {
				variantString = l;
                break;
            }
        }
    }

	void print_BED_IDX(std::string &path, std::string &chrom, int pos, std::string &variantString) {
		kstring_t kstr = {0,0,0};
		htsFile *fp = hts_open(path.c_str(), "r");
		tbx_t * idx_t = tbx_index_load(path.c_str());
		int tid = tbx_name2id(idx_t, chrom.c_str());
		hts_itr_t *iter_q = tbx_itr_queryi(idx_t, tid, pos, pos + 1);
		if (iter_q == nullptr) {
			std::cerr << "\nError: Null iterator when trying to fetch from indexed bed file in print "
			          << chrom
			          << " " << pos << std::endl;
			std::terminate();
			return;
		}
		int res = tbx_itr_next(fp, idx_t, iter_q, &kstr);
		if (res < 0) {
			if (res < -1) {
				std::cerr << "Error: iterating vcf file returned " << res << std::endl;
				return;
			}
		}
		variantString = kstr.s;
	}

	void print_cached(std::vector<Utils::TrackBlock> &vals, std::string &chrom, int pos, bool flat, std::string &variantString) {
		auto vals_end = vals.end();
		std::vector<Utils::TrackBlock>::iterator iter_blk;
		if (flat) {
			iter_blk = vals.begin();
		} else {
			iter_blk = std::lower_bound(vals.begin(), vals.end(), pos,
			                            [](Utils::TrackBlock &a, int x)-> bool { return a.start < x;});
			if (iter_blk != vals.begin()) {
				--iter_blk;
			}
		}
		while (true) {
			if (iter_blk == vals_end) {
				break;
			}
			if (flat && iter_blk->chrom == chrom) {
				if (iter_blk->start <= pos && iter_blk->end > pos) {
					variantString = iter_blk->line;
				} else if (iter_blk->start > pos) {
					break;
				}
			} else {
				if (iter_blk->start <= pos && iter_blk->end > pos) {
					variantString = iter_blk->line;
				} else if (iter_blk->start > pos) {
					break;
				}
			}
			iter_blk++;
		}
	}


    GwTrack::~GwTrack() {
        // these cause segfaults?
//        if (fp != nullptr) {
//            hts_close(fp);
//        }
//        if (idx_v != nullptr) {
//            hts_idx_destroy(idx_v);
//        }
//        if (hdr != nullptr) {
//            bcf_hdr_destroy(hdr);
//        }
//        if (v != nullptr) {
//            bcf_destroy1(v);
//        }
//        if (t != nullptr) {
//            tbx_destroy(t);
//        }
    }


    void GwTrack::parseVcfRecord(Utils::TrackBlock &b) {
        kstring_t kstr = {0,0,0};
        bcf_unpack(v, BCF_UN_INFO);
        vcf_format(hdr, v, &kstr);
        b.line = kstr.s;
        b.chrom = bcf_hdr_id2name(hdr, v->rid);
        b.start = (int)v->pos;
        b.end = b.start + v->rlen;
        b.name = v->d.id;
        switch (bcf_get_variant_types(v)) {
            case VCF_SNP: b.vartype = "SNP"; break;
            case VCF_INDEL: b.vartype = "INDEL"; break;
            case VCF_OVERLAP: b.vartype = "OVERLAP"; break;
            case VCF_BND: b.vartype = "BND"; break;
            case VCF_OTHER: b.vartype = "OTHER"; break;
            case VCF_MNP: b.vartype = "MNP"; break;
            default: b.vartype = "REF";
        }
        bcf_info_t * info_field = bcf_get_info(hdr, v, "SVTYPE");  // try and find chrom2 in info
        if (info_field != nullptr) {
            char *svtmem = nullptr;
            int mem = 0;
            int resc = bcf_get_info_string(hdr, v, "SVTYPE", &svtmem,&mem);
            if (resc < 0) {
            } else {
                b.vartype = svtmem;
            }
        }
    }

    void GwTrack::parseVcfRecord() {
        bcf_unpack(v, BCF_UN_INFO);
        chrom = bcf_hdr_id2name(hdr, v->rid);
        start = (int)v->pos;
        stop = (int)start + v->rlen;
        rid = v->d.id;
        switch (bcf_get_variant_types(v)) {
            case VCF_SNP: vartype = "SNP"; break;
            case VCF_INDEL: vartype = "INDEL"; break;
            case VCF_OVERLAP: vartype = "OVERLAP"; break;
            case VCF_BND: vartype = "BND"; break;
            case VCF_OTHER: vartype = "OTHER"; break;
            case VCF_MNP: vartype = "MNP"; break;
            default: vartype = "REF";
        }
        bcf_info_t * info_field = bcf_get_info(hdr, v, "SVTYPE");  // try and find chrom2 in info
        if (info_field != nullptr) {
            char *svtmem = nullptr;
            int mem = 0;
            int resc = bcf_get_info_string(hdr, v, "SVTYPE", &svtmem,&mem);
            if (resc < 0) {
            } else {
                vartype = svtmem;
            }
        }
    }

    void GwTrack::open(const std::string &p, bool add_to_dict=true) {
        fileIndex = 0;
        path = p;
        if (Utils::endsWith(p, ".bed")) {
            kind = BED_NOI;
        } else if (Utils::endsWith(p, ".bed.gz")) {
            kind = BED_IDX;
        } else if (Utils::endsWith(p, ".vcf")) {
            kind = VCF_NOI;
        } else if (Utils::endsWith(p, ".vcf.gz")) {
            kind = VCF_IDX;
        } else if (Utils::endsWith(p, ".bcf")) {
            kind = BCF_IDX;
        } else if (Utils::endsWith(p, ".gff3.gz")) {
            kind = GFF3_IDX;
        } else if (Utils::endsWith(p, ".gff3")) {
            kind = GFF3_NOI;
        } else if (Utils::endsWith(p, ".gtf.gz")) {
            kind = GTF_IDX;
        } else if (Utils::endsWith(p, ".gtf")) {
            kind = GTF_NOI;
        } else {
            kind = GW_LABEL;
        }

        if (kind == VCF_NOI) {
            fp = bcf_open(path.c_str(), "r");
            hdr = bcf_hdr_read(fp);
            v = bcf_init1();
            v->max_unpack = BCF_UN_INFO;
            while (true) {
                int res = bcf_read(fp, hdr, v);
                if (res < -1) {
                    std::cerr << "Error: reading vcf resulted in error code " << res << std::endl;
                    std::terminate();
                } else if (res == -1) {
                    done = true;
                    break;
                }
                Utils::TrackBlock b;
                parseVcfRecord(b);
                allBlocks[b.chrom].add(b.start, b.end, b);
            }
        } else if (kind == BED_NOI || kind == GW_LABEL) {
            std::fstream fpu;
            fpu.open(p, std::ios::in);
            if (!fpu.is_open()) {
                std::cerr << "Error: opening track file " << path << std::endl;
                std::terminate();
            }
            std::string tp;
            const char delim = '\t';
            int count = 0;
            while (getline(fpu, tp)) {
                count += 1;
                if (tp[0] == '#') {
                    continue;
                }
                std::vector<std::string> parts = Utils::split(tp, delim);
                if (parts.size() < 9) {
                    std::cerr << "Error: parsing file, not enough columns in line split by tab. n columns = "
                              << parts.size() << ", line was: " << tp << ", at file index " << count << std::endl;
                }
                Utils::TrackBlock b;
                b.line = tp;
                b.chrom = parts[0];
                b.start = std::stoi(parts[1]);
                b.strand = 0;
                if (kind == BED_NOI) {  // bed
                    b.end = std::stoi(parts[2]);
                    if (parts.size() > 3) {
                        b.name = parts[3];
                        if (parts.size() >= 6) {
                            if (parts[5] == "+") {
                                b.strand = 1;
                            } else if (parts[5] == "-") {
                                b.strand = 2;
                            }
                        }
                    } else {
                        b.name = std::to_string(fileIndex);
                        fileIndex += 1;
                    }
                } else { // assume gw_label file
                    b.end = b.start + 1;
                }
                allBlocks[b.chrom].add(b.start, b.end, b);
            }
        } else if (kind == GFF3_IDX || kind == GTF_IDX) {
            fp = hts_open(p.c_str(), "r");
            idx_t = tbx_index_load(p.c_str());
        }
        else if (kind == GFF3_NOI || kind == GTF_NOI) {
            std::fstream fpu;
            fpu.open(p, std::ios::in);
            if (!fpu.is_open()) {
                std::cerr << "Error: opening track file " << path << std::endl;
                std::terminate();
            }
            std::string tp;
            const char delim = '\t';
            int count = 0;
            std::vector<Utils::TrackBlock> track_blocks;
            ankerl::unordered_dense::map< std::string, int> name_to_track_block_idx;

            while (getline(fpu, tp)) {
                count += 1;
                if (tp[0] == '#') {
                    continue;
                }
                Utils::TrackBlock b;
                b.parts = Utils::split(tp, delim);
                if (b.parts.size() < 9) {
                    std::cerr << "Error: parsing file, not enough columns in line split by tab. n columns = "
                              << b.parts.size() << ", line was: " << tp << ", at file index " << count << std::endl;
                }
                b.line = tp;
                b.chrom = b.parts[0];
                b.vartype = b.parts[2];
                b.start = std::stoi(b.parts[3]);
                b.end = std::stoi(b.parts[4]);
                if (b.parts[6] == "+") {
                    b.strand = 1;
                } else if (b.parts[6] == "-") {
                    b.strand = 2;
                } else {
                    b.strand = 0;
                }
                for (const auto &item :  Utils::split(b.parts[8], ';')) {
                    if (kind == GFF3_NOI) {
                        std::vector<std::string> keyval = Utils::split(item, '=');
                        if (keyval[0] == "ID") {
                            b.name = keyval[1];
                        }
                        else if (keyval[0] == "Parent") {
                            b.parent = keyval[1];
                            break;
                        }
                    } else {
                        std::vector<std::string> keyval = Utils::split(item, ' ');
                        if (keyval[0] == "transcript_id") {
                            b.name = keyval[1];
                        }
                        else if (keyval[0] == "gene_id") {
                            b.parent = keyval[1];
                            break;
                        }
                    }

                }
                allBlocks[b.chrom].add(b.start, b.end, b);
            }
        } else if (kind == BED_IDX) {
            fp = hts_open(p.c_str(), "r");
            idx_t = tbx_index_load(p.c_str());
        } else if (kind == BCF_IDX) {
            fp = bcf_open(p.c_str(), "r");
            hdr = bcf_hdr_read(fp);
            idx_v = bcf_index_load(p.c_str());
            v = bcf_init1();
            v->max_unpack = BCF_UN_INFO;
        } else if (kind == VCF_IDX) {
            fp = bcf_open(path.c_str(), "r");
            hdr = bcf_hdr_read(fp);
            idx_t = tbx_index_load(path.c_str());
            v = bcf_init1();
            v->max_unpack = BCF_UN_INFO;
        } else {
            std::cerr << "Error: file stype not supported for " << path << std::endl;
            std::terminate();
        }

        for (auto &item : allBlocks) {
            item.second.index();
        }
    }

    void GwTrack::fetch(const Utils::Region *rgn) {
        if (kind > BCF_IDX) {  // non-indexed
            if (rgn == nullptr) {
//                iter_blk = allBlocks_flat.begin();
//                vals_end = allBlocks_flat.end();
//                region_end = 2000000000;
//                done = false;
            } else {
                if (allBlocks.contains(rgn->chrom)) {
                    std::vector<size_t> a;
                    if (kind == GFF3_NOI || kind == GTF_NOI) {
                        allBlocks[rgn->chrom].overlap(rgn->start - 100000, rgn->end + 100000, a);
                    } else {
                        allBlocks[rgn->chrom].overlap(rgn->start, rgn->end, a);
                    }
                    overlappingBlocks.clear();
                    if (a.empty()) {
                        done = true;
                        return;
                    }
                    done = false;
                    overlappingBlocks.resize(a.size());
                    for (size_t i = 0; i < a.size(); ++i) {
                        overlappingBlocks[i] = allBlocks[rgn->chrom].data(a[i]);
                    }
                    iter_blk = overlappingBlocks.begin();
                    vals_end = overlappingBlocks.end();
                } else {
                    done = true;
                }
            }
        } else {
            if (kind == BED_IDX || kind == VCF_IDX || kind == GFF3_IDX || kind == GTF_IDX) {
                if (rgn == nullptr) {
                    iter_q = nullptr;
                    done = false;
                } else {
                    int tid = tbx_name2id(idx_t, rgn->chrom.c_str());
                    if (kind == GFF3_IDX || kind == GTF_IDX) {
                        iter_q = tbx_itr_queryi(idx_t, tid, std::max(1, rgn->start - 100000), rgn->end + 100000);
                    } else {
                        iter_q = tbx_itr_queryi(idx_t, tid, rgn->start, rgn->end);
                    }
                    if (iter_q == nullptr) {
                        done = true;
                    } else {
                        done = false;
                    }
                }
            } else if (kind == BCF_IDX) {
                int tid = bcf_hdr_name2id(hdr, rgn->chrom.c_str());
                iter_q = bcf_itr_queryi(idx_v, tid, rgn->start, rgn->end);
                if (iter_q == nullptr) {
                    done = true;
                } else {
                    done = false;
                }
            }
        }
    }

    void GwTrack::next() {
        int res;
        if (done) {
            return;
        }
        if (kind == BCF_IDX) {
            res = bcf_itr_next(fp, iter_q, v);
            if (res < 0) {
                if (res < -1) {
                    std::cerr << "Error: iterating bcf file returned " << res << std::endl;
                }
                done = true;
                return;
            }
            parseVcfRecord();
        } else if (kind == BED_IDX || kind == VCF_IDX || kind == GFF3_IDX || kind == GTF_IDX) {
            kstring_t str = {0,0, nullptr};
            if (iter_q != nullptr) {
                res = tbx_itr_next(fp, idx_t, iter_q, &str);
                if (res < 0) {
                    if (res < -1) {
                        std::cerr << "Error: iterating returned code: " << res << " from file: " << path  << std::endl;
                    }
                    done = true;
                    return;
                }
            } else {
                res = hts_getline(fp, '\n', &str);
                if (res < 0) {
                    if (res < -1) {
                        std::cerr << "Error: iterating returned code: " << res << " from file: " << path  << std::endl;
                    }
                    done = true;
                    return;
                }
            }
            if (kind == BED_IDX) {
                parts.clear();
                parts = Utils::split(str.s, '\t');
                chrom = parts[0];
                start = std::stoi(parts[1]);
                stop = std::stoi(parts[2]);
                if (parts.size() > 2) {
                    rid = parts[3];
                } else {
                    rid = std::to_string(fileIndex);
                    fileIndex += 1;
                }
                vartype = "";
            } else if (kind == GFF3_IDX || kind == GTF_IDX) {
                parts.clear();
                parts = Utils::split(str.s, '\t');
                chrom = parts[0];
                start = std::stoi(parts[3]);
                stop = std::stoi(parts[4]);
                vartype = parts[2];
                rid.clear();
                parent.clear();
                for (const auto &item :  Utils::split(parts[8], ';')) {
                    if (kind == GFF3_NOI) {
                        std::vector<std::string> keyval = Utils::split(item, '=');
                        if (keyval[0] == "ID") {
                            rid = keyval[1];
                        }
                        else if (keyval[0] == "Parent") {
                            parent = keyval[1];
                            break;
                        }
                    } else {
                        std::vector<std::string> keyval = Utils::split(item, ' ');
                        if (keyval[0] == "transcript_id") {
                            rid = keyval[1];
                        }
                        else if (keyval[0] == "gene_id") {
                            parent = keyval[1];
                            break;
                        }
                    }
                }
            } else {
                res = vcf_parse(&str, hdr, v);
                if (res < 0) {
                    if (res < -1) {
                        std::cerr << "Error: iterating returned code: " << res << " from file: " << path  << std::endl;
                    }
                    done = true;
                    return;
                }
                parseVcfRecord();
            }
        } else if (kind > BCF_IDX) {  // non indexed but cached VCF_NOI / BED_NOI / GFF3 (todo) / GW_LABEL / STDIN?
            // first line of the region is cached here and resolved during drawing / mouse-clicks
            if (iter_blk != vals_end) {
                chrom = iter_blk->chrom;
                start = iter_blk->start;
                stop = iter_blk->end;
                rid = iter_blk->name;
                parent = iter_blk->parent;
                vartype = iter_blk->vartype;
                variantString = iter_blk->line;
                parts = iter_blk->parts;
                ++iter_blk;
            } else {
                done = true;
            }
        }
    }

    bool GwTrack::findFeature(std::string &feature, Utils::Region &region) {
        if (kind == BED_IDX) {
            htsFile *fp_temp = hts_open(path.c_str(), "r");
            kstring_t str = {0,0, nullptr};
            int fileIndex_tmp = 0;
            while (hts_getline(fp_temp, '\n', &str) >= 0) {
                parts.clear();
                parts = Utils::split(str.s, '\t');
                if (parts.size() > 2) {
                    rid = parts[3];
                } else {
                    rid = std::to_string(fileIndex_tmp);
                    fileIndex_tmp += 1;
                }
                if (rid == feature) {
                    region.chrom = parts[0];
                    region.start = std::stoi(parts[1]);
                    region.end = std::stoi(parts[2]);
                    region.markerPos = region.start;
                    region.markerPosEnd = region.end;
                    return true;
                }
            }
        } else if (kind == BCF_IDX || kind == VCF_IDX) {
            htsFile *fp2 = bcf_open(path.c_str(), "r");
            bcf_hdr_t *hdr2 = bcf_hdr_read(fp2);
            bcf1_t *v2 = bcf_init1();
            v2->max_unpack = BCF_UN_INFO;
            while (bcf_read(fp2, hdr2, v2) >= 0) {
                std::string id_temp;
                bcf_unpack(v2, BCF_UN_INFO);
                id_temp = v2->d.id;
                if (id_temp == feature) {
                    region.chrom = bcf_hdr_id2name(hdr2, v2->rid);
                    region.start = (int)v2->pos;
                    region.end = region.start + v2->rlen;
                    region.markerPos = region.start;
                    region.markerPosEnd = region.end;
                    return true;
                }
            }
        } else if (kind > BCF_IDX) {
//            if (!allBlocks_flat.empty()) {
//                for (auto &b : allBlocks_flat) {
//                    if (b.name == feature) {
//                        region.chrom = b.chrom;
//                        region.start = b.start;
//                        region.end = b.end;
//                        region.markerPos = b.start;
//                        region.markerPosEnd = b.end;
//                        return true;
//                    }
//                }
//            } else {
                for (auto &chrom_blocks : allBlocks) {
                    for (size_t i=0; i < chrom_blocks.second.size(); ++i) {
                        const Utils::TrackBlock &b = chrom_blocks.second.data(i);
                        if (b.name == feature) {
                            region.chrom = b.chrom;
                            region.start = b.start;
                            region.end = b.end;
                            region.markerPos = b.start;
                            region.markerPosEnd = b.end;
                            return true;
                        }
                    }
                }
//            }
        }
        return false;
    }

    void GwTrack::printTargetRecord(std::string &id_str, std::string &chrm, int pos) {
        if (kind == BCF_IDX) {
            return print_BCF_IDX(idx_v, hdr, chrm, pos, fp, id_str, variantString);
        } else if (kind == VCF_NOI) {
            return print_VCF_NOI(path, id_str, variantString);
        } else if (kind == VCF_IDX) {
            return print_VCF_IDX(path, id_str, chrm, pos, variantString);
        } else if (kind == BED_IDX) {
            return print_BED_IDX(path, chrm, pos, variantString);
        } else {
			if (allBlocks.contains(chrm)) {
				return print_cached(overlappingBlocks, chrm, pos, false, variantString);
			} else {
//				return print_cached(allBlocks_flat, chrm, pos, true, variantString);
			}
		}
    }

    bool searchTracks(std::vector<GwTrack> &tracks, std::string &feature, Utils::Region &region) {
        for (auto &track : tracks) {
            if (track.findFeature(feature, region)) {
                region.start = std::max(0, region.start - 100);
                region.end = region.end + 100;
                return true;
            }
        }
        return false;
    }

    void saveVcf(VCFfile &input_vcf, std::string path, std::vector<Utils::Label> multiLabels) {
        std::cout << "\nSaving output vcf\n";
        if (multiLabels.empty()) {
            std::cerr << "Error: no labels detected\n";
            return;
        }

        ankerl::unordered_dense::map< std::string, Utils::Label> label_dict;
        for (auto &l : multiLabels) {
            label_dict[l.variantId] = l;
        }

        int res;

        bcf_hdr_t *new_hdr = bcf_hdr_init("w");

        new_hdr = bcf_hdr_merge(new_hdr, input_vcf.hdr);
        if (bcf_hdr_sync(new_hdr) < 0) {
            std::cerr << "bcf_hdr_sync(hdr2) after merge\n";
            std::terminate();
        }

        const char *lg = "##source=GW>";
        res = bcf_hdr_append(new_hdr, lg);
        for (auto &l: multiLabels[0].labels) {
            if (l != "PASS") {
                std::string str = "##FILTER=<ID=" + l + ",Description=\"GW custom label\">";
                res = bcf_hdr_append(new_hdr, str.c_str());
                if (res < 0) {
                    std::cerr << "bcf_hdr_append(new_hdr) failed\n";
                    std::terminate();
                }
            }
        }

        const char *l0 = "##INFO=<ID=GW_DATE,Number=1,Type=String,Description=\"Date of GW label\">";
        const char *l1 = "##INFO=<ID=GW_PREV,Number=1,Type=String,Description=\"Previous GW label\">";

        bcf_hdr_append(new_hdr, l0);
        bcf_hdr_append(new_hdr, l1);

        htsFile *fp_out = bcf_open(path.c_str(), "w");
        res = bcf_hdr_write(fp_out, new_hdr);
        if (res < 0) {
            std::cerr << "Error: Unable to write new header\n";
            std::terminate();
        }
        if (!input_vcf.lines.empty()) {
            // todo loop over cached lines here
        } else {
            // reset to start of file
            input_vcf.open(input_vcf.path);
        }
        while (true) {
            input_vcf.next();
            if (input_vcf.done) {
                break;
            }
            if (label_dict.contains(input_vcf.rid)) {
                Utils::Label &l =  label_dict[input_vcf.rid];
                const char *prev_label = new_hdr->id[BCF_DT_ID][*input_vcf.v->d.flt].key;
                int filter_id = bcf_hdr_id2int(new_hdr, BCF_DT_ID, l.current().c_str());
                res = bcf_update_filter(new_hdr, input_vcf.v, &filter_id, 1);
                if (res < 0) {
                    std::cerr << "Error: Failed to update filter, id " << input_vcf.v->rid << std::endl;
                }
                if (!l.savedDate.empty()) {
                    res = bcf_update_info_string(new_hdr, input_vcf.v, "GW_PREV", prev_label);
                    if (res < 0) {
                        std::cerr << "Error: Updating GW_PREV failed, id " << input_vcf.v->rid << std::endl;
                    }

                    res = bcf_update_info_string(new_hdr, input_vcf.v, "GW_DATE", l.savedDate.c_str());
                    if (res < 0) {
                        std::cerr << "Error: Updating GW_DATE failed, id " << input_vcf.v->rid << std::endl;
                    }
                }
                res = bcf_write(fp_out, new_hdr, input_vcf.v);
                if (res < 0) {
                    std::cerr << "Error: Writing new vcf record failed, id " << input_vcf.v->rid << std::endl;
                }

            } else {
                res = bcf_write(fp_out, new_hdr, input_vcf.v);
                if (res < 0) {
                    std::cerr << "Error: Writing new vcf record failed, id " << input_vcf.v->rid << std::endl;
                    break;
                }
            }
        }
        bcf_hdr_destroy(new_hdr);
        bcf_close(fp_out);
    }

    GwVariantTrack::GwVariantTrack(std::string &path, bool cacheStdin, Themes::IniOptions *t_opts, int startIndex,
                                   std::vector<std::string> &t_labelChoices,
                                   ankerl::unordered_dense::map< std::string, Utils::Label> *t_inputLabels,
                                   ankerl::unordered_dense::set<std::string> *t_seenLabels) {

        labelChoices = t_labelChoices;
        inputLabels = t_inputLabels;
        mouseOverTileIndex = -1;
        blockStart = 0;
        m_opts = t_opts;

        if (cacheStdin || Utils::endsWith(path, ".vcf") ||
            Utils::endsWith(path, ".vcf.gz") ||
            Utils::endsWith(path, ".bcf")) {
            variantTrack.done = true;
            vcf.done = false;
            useVcf = true;
            vcf.seenLabels = t_seenLabels;
            vcf.cacheStdin = cacheStdin;
            vcf.label_to_parse = m_opts->parse_label.c_str();
            vcf.open(path);
            trackDone = &vcf.done;
            if (startIndex > 0) {
                nextN(startIndex);
            }
            this->path = vcf.path;
        } else {  // bed file or labels file, or some other tsv
            useVcf = false;
            vcf.done = true;
            variantTrack.done = false;
            variantTrack.open(path, false);
            trackDone = &variantTrack.done;
            variantTrack.fetch(nullptr);  // initialize iterators
            if (startIndex > 0) {
                nextN(startIndex);
            }
            this->path = variantTrack.path;
        }
        init = true;
    }

    GwVariantTrack::~GwVariantTrack() {

    }

    void GwVariantTrack::nextN(int number) {
        if (number == 0) {
            return;
        }
        if (useVcf) {
            trackDone = &vcf.done;
        } else {
            trackDone = &variantTrack.done;
        }
        while (!(*trackDone)) {
            for (int i=0; i < number; ++i) {
                if (useVcf) {
                    vcf.next();
                    if (*trackDone) {
                        break;
                    }
                    this->appendVariantSite(vcf.chrom, vcf.start, vcf.chrom2, vcf.stop, vcf.rid, vcf.label, vcf.vartype);
                } else {
                    variantTrack.next();
                    if (*trackDone) {
                        break;
                    }
                    std::string label = "";
                    this->appendVariantSite(variantTrack.chrom, variantTrack.start, variantTrack.chrom2,
                                      variantTrack.stop, variantTrack.rid, label, variantTrack.vartype);
                }
            }
            break;
        }
    }

    void GwVariantTrack::iterateToIndex(int targetIndex) {
        int currentIndex = multiRegions.size();
        if (targetIndex > currentIndex) {
            nextN(targetIndex - currentIndex);
        }
    }

    void GwVariantTrack::appendVariantSite(std::string &chrom, long start, std::string &chrom2, long stop, std::string &rid, std::string &label, std::string &vartype) {
        long rlen = stop - start;
        std::vector<Utils::Region> v;
        bool isTrans = chrom != chrom2;
        if (!isTrans && rlen <= m_opts->split_view_size) {
            Utils::Region r;
            v.resize(1);
            v[0].chrom = chrom;
            v[0].start = (1 > start - m_opts->pad) ? 1 : start - m_opts->pad;
            v[0].end = stop + m_opts->pad;
            v[0].markerPos = start;
            v[0].markerPosEnd = stop;
        } else {
            v.resize(2);
            v[0].chrom = chrom;
            v[0].start = (1 > start - m_opts->pad) ? 1 : start - m_opts->pad;
            v[0].end = start + m_opts->pad;
            v[0].markerPos = start;
            v[0].markerPosEnd = start;
            v[1].chrom = chrom2;
            v[1].start = (1 > stop - m_opts->pad) ? 1 : stop - m_opts->pad;
            v[1].end = stop + m_opts->pad;
            v[1].markerPos = stop;
            v[1].markerPosEnd = stop;
        }
        multiRegions.push_back(v);
        if (inputLabels->contains(rid)) {
            multiLabels.push_back((*inputLabels)[rid]);
        } else {
            multiLabels.push_back(Utils::makeLabel(chrom, start, label, labelChoices, rid, vartype, "", 0));
        }
    }

    void collectGFFTrackData(HGW::GwTrack &trk, std::vector<Utils::TrackBlock> &features) {
        // For GFF convert all child features into TrackBlocks
        ankerl::unordered_dense::map< std::string, std::vector< std::shared_ptr<Utils::GFFTrackBlock> > > gffParentMap;
        std::vector< std::shared_ptr<Utils::GFFTrackBlock> > gffBlocks;
        while (true) {
            trk.next();
            if (trk.done) {
                break;
            }
            if (trk.parent.empty()) {
                continue;
            }
            gffBlocks.push_back(std::make_shared<Utils::GFFTrackBlock>());
            std::shared_ptr<Utils::GFFTrackBlock> g = gffBlocks.back();
            g->chrom = trk.chrom;
            g->start = trk.start;
            g->end = trk.stop;
            g->name = trk.rid;
            g->vartype = trk.vartype;
            g->strand = (trk.parts[6] == "-") ? 2 : 1; // assume all on same strand
            g->parts.insert(g->parts.end(), trk.parts.begin(), trk.parts.end());
            gffParentMap[trk.parent].push_back(g);
        }
        // assume gff is sorted

        features.resize(gffParentMap.size());
        int i = 0;
        for (auto &pg : gffParentMap) {
            int j = 0;
            Utils::TrackBlock &track = features[i];
            std::sort(pg.second.begin(), pg.second.end(),
                      [](const std::shared_ptr<Utils::GFFTrackBlock> &a, const std::shared_ptr<Utils::GFFTrackBlock> &b)-> bool
                      { return a->start < b->start || (a->start == b->start && a->end < b->end);});

            track.anyToDraw = false;
            bool restAreThin = false;
            for (auto &g: pg.second) {
                if (j == 0) {
                    track.chrom = g->chrom;
                    track.start = g->start;
                    track.name = pg.first;
                    if (track.name.front() == '"') {
                        track.name.erase(0, 1);
                    }
                    if (track.name.back() == '"') {
                        track.name.erase(track.name.size() - 1, 1);
                    }
                    track.parent = pg.first;
                    if (track.parent.front() == '"') {
                        track.parent.erase(0, 1);
                    }
                    if (track.name.back() == '"') {
                        track.parent.erase(track.parent.size() - 1, 1);
                    }
                    track.end = g->end;
                    track.strand = g->strand;
                } else if (g->end > track.end) {
                    track.end = g->end;
                }
                track.parts.insert(track.parts.end(), g->parts.begin(), g->parts.end());
                track.parts.push_back("\n");
                track.s.push_back(g->start);
                track.e.push_back(g->end);
                if (restAreThin) {
                    track.drawThickness.push_back(1);
                } else if (g->vartype == "exon" || g->vartype == "CDS") {
                    track.drawThickness.push_back(2);  // fat line
                    if (!track.anyToDraw) { track.anyToDraw = true; }
                } else if (g->vartype == "mRNA" || g->vartype == "gene") {
                    track.drawThickness.push_back(0);  // no line
                } else if (g->vartype == "start_codon") {
                    track.coding_start = g->start;
                    std::fill(track.drawThickness.begin(), track.drawThickness.end(), 1);
                    track.drawThickness.push_back(2);
                } else if (g->vartype == "stop_codon") {
                    track.coding_end = g->end;
                    restAreThin = true;
                    track.drawThickness.push_back(2);
                } else {
                    track.drawThickness.push_back(1);
                    if (!track.anyToDraw) { track.anyToDraw = true; }
                }
                j += 1;
            }
            i += 1;
        }
    }
    void collectTrackData(HGW::GwTrack &trk, std::vector<Utils::TrackBlock> &features) {
        while (true) {
            trk.next();
            if (trk.done) {
                break;
            }
            // check for big bed. BED_IDX will already be split, BED_NOI is split here
            if (trk.kind == HGW::BED_NOI) {
                trk.parts.clear();
                Utils::split(trk.variantString, '\t', trk.parts);
            }
            features.resize(features.size() + 1);
            Utils::TrackBlock *b = &features.back();
            b->chrom = trk.chrom;
            b->name = trk.rid;
            b->start = trk.start;
            b->end = trk.stop;
            b->line = trk.variantString;
            b->parts = trk.parts;
//            b->parent = trk.parent;
            b->anyToDraw = true;
            if (trk.parts.size() >= 5) {
                b->strand = (trk.parts[5] == "+") ? 1 : (trk.parts[5] == "-") ? -1 : 0;
            }
            bool tryBed12 = !trk.parts.empty() && trk.parts.size() >= 12;
            if (tryBed12) {
                std::vector<std::string> lens, starts;
                Utils::split(trk.parts[10], ',', lens);
                Utils::split(trk.parts[11], ',', starts);
                if (starts.size() != lens.size()) {
                    continue;
                }
                int target = (int)lens.size();
                int gene_start = trk.start;
                int thickStart, thickEnd;
                try {
                    thickStart = std::stoi(trk.parts[6]);
                    thickEnd = (std::stoi(trk.parts[7]));
                } catch (...) {
                    continue;
                }
                b->drawThickness.resize(target, 0);
                thickEnd = (thickEnd == thickStart) ? trk.stop : thickEnd;
                for (int i=0; i < target; ++i) {
                    int s, e;
                    try {
                        s = gene_start + std::stoi(starts[i]);
                        e = s + std::stoi(lens[i]);
                    } catch (...) {
                        break;
                    }
                    if (s >= thickStart && e <= thickEnd) {
                        b->drawThickness[i] = 2;
                    } else {
                        b->drawThickness[i] = 1;
                    }
                    b->s.push_back(s);
                    b->e.push_back(e);

                }
            }
        }
    }

}
