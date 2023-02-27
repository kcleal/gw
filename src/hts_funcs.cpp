//
// Created by Kez Cleal on 04/08/2022.
//

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

#include "htslib/hts.h"
#include "htslib/sam.h"
#include "htslib/tbx.h"
#include "htslib/vcf.h"

#include "../include/BS_thread_pool.h"
#include "segments.h"
#include "themes.h"
#include "hts_funcs.h"


namespace HGW {

    Segs::Align make_align(bam1_t* src) {
        Segs::Align a;
        a.delegate = src;
        a.initialized = false;
        return a;
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
                                 std::vector<Parse::Parser> &filters) {
        bam1_t *src;
        hts_itr_t *iter_q;

        int tid = sam_hdr_name2tid(hdr_ptr, region->chrom.c_str());
        std::vector<Segs::Align>& readQueue = col.readQueue;
        readQueue.push_back(make_align(bam_init1()));
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
            readQueue.push_back(make_align(bam_init1()));
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

        Segs::init_parallel(readQueue, threads);
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
            if (item.cov_start > region->end + 1000) {
                if (item.y >= 0 && !col.levelsEnd.empty()) {
                    col.levelsEnd[item.y] = item.cov_start - 1;
                }
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
                                 hts_idx_t *index, Themes::IniOptions &opts, bool coverage, bool left, int *samMaxY,
                                std::vector<Parse::Parser> &filters) {
        bam1_t *src;
        hts_itr_t *iter_q;
        std::vector<Segs::Align>& readQueue = col.readQueue;
        Utils::Region *region = &col.region;
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

            Segs::init_parallel(newReads, opts.threads);

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
                col.levelsStart.clear();
                col.levelsEnd.clear();
                int maxY = Segs::findY(col, col.readQueue, opts.link_op, opts, region, left);
                if (maxY > *samMaxY) {
                    *samMaxY = maxY;
                }
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
            col.covArr.resize(region->end - region->start + 1);
            std::fill(col.covArr.begin(), col.covArr.end(), 0);
            int l_arr = (int)col.covArr.size() - 1;
            for (auto &i : readQueue) {
                Segs::addToCovArray(col.covArr, i, region->start, region->end, l_arr);
            }
        }
        col.processed = true;
    }

    VCFfile::~VCFfile() {
        if (fp && !path.empty()) {
            vcf_close(fp);
            bcf_destroy(v);
        }
        if (!lines.empty()) {
            for (auto &v: lines) {
                bcf_destroy1(v);
            }
        }
    }

    void VCFfile::open(std::string f) {
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
//        std::string parsedVal;
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
        if (seenLabels != nullptr && !seenLabels->contains(label)) {
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
                    // std::cout << std::endl << kstr.s << std::endl;
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
        stop = start + v->rlen;
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

    void GwTrack::open(std::string &p, bool add_to_dict=true) {
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
        } else {
            kind = GW_LABEL;
        }

        bool sorted = true;
        int lastb = -1;

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
                if (add_to_dict) {
                    if (sorted) {
                        if (allBlocks.find(b.chrom) == allBlocks.end()) {
                            lastb = -1;
                        }
                        allBlocks[b.chrom].push_back(b);
                        if (b.start < lastb) {
                            sorted = false;
                        }
                        lastb = b.start;
                    }
                } else {
                    allBlocks_flat.push_back(b);
                }
            }
        }
        else if (kind == BED_NOI || kind == GW_LABEL) {
            std::fstream fpu;
            fpu.open(p, std::ios::in);
            if (!fpu.is_open()) {
                std::cerr << "Error: opening track file " << path << std::endl;
                std::terminate();
            }
            std::string tp;
            const char delim = '\t';
            int count = 0;
            while(getline(fpu, tp)) {
                count += 1;
                if (tp[0] == '#') {
                    continue;
                }
                std::vector<std::string> parts = Utils::split(tp, delim);
                if (parts.size() < 3) {
                    std::cerr << "Error: parsing file, not enough columns in line split by tab. n columns = " << parts.size() << ", line was: " << tp << ", at file index " << count << std::endl;
                }
                Utils::TrackBlock b;
                b.line = tp;
                b.chrom = parts[0];

                if (add_to_dict && !allBlocks.contains(b.chrom)) {
                    lastb = -1;
                }
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
                if (add_to_dict) {
                    if (allBlocks.find(b.chrom) == allBlocks.end()) {
                        lastb = -1;
                    }
                    allBlocks[b.chrom].push_back(b);

                    if (b.start < lastb) {
                        sorted = false;
                    }
                    lastb = b.start;
                } else {
                    allBlocks_flat.push_back(b);
                }
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

        if (!sorted) {
            std::cout << "Unsorted file: sorting blocks from " << path << std::endl;
            for (auto &item : allBlocks) {
                std::sort(item.second.begin(), item.second.end(),
                          [](const Utils::TrackBlock &a, const Utils::TrackBlock &b)-> bool { return a.start < b.start || (a.start == b.start && a.end > b.end);});
            }
        }
    }

    void GwTrack::fetch(const Utils::Region *rgn) {
        if (kind > BCF_IDX) {  // non-indexed
            if (rgn == nullptr) {
                iter_blk = allBlocks_flat.begin();
                vals_end = allBlocks_flat.end();
                region_end = 2000000000;
                done = false;
            } else {
                if (allBlocks.contains(rgn->chrom)) {
                    vals = allBlocks[rgn->chrom];
                    vals_end = vals.end();
                    iter_blk = std::lower_bound(vals.begin(), vals.end(), rgn->start,
                                                [](Utils::TrackBlock &a, int x)-> bool { return a.start < x;});
                    if (iter_blk != vals.begin()) {
                        --iter_blk;
                        while (iter_blk != vals.begin()) {
                            if (iter_blk->end > rgn->start) {
                                --iter_blk;
                            } else {
                                break;
                            }
                        }
                    }
                    region_end = rgn->end;
                    if (iter_blk == vals_end) {
                        done = true;
                    } else {
                        done = false;
                    }
                } else {
                    done = true;
                }
            }
        } else {
            if (kind == BED_IDX || kind == VCF_IDX) {
                if (rgn == nullptr) {
                    iter_q = nullptr;
                    done = false;
                } else {
                    int tid = tbx_name2id(idx_t, rgn->chrom.c_str());
                    iter_q = tbx_itr_queryi(idx_t, tid, rgn->start, rgn->end);
                    if (iter_q == nullptr) {
                        std::cerr << "\nError: Null iterator when trying to fetch from indexed bed file in fetch "
                                  << rgn->chrom
                                  << " " << rgn->start << " " << rgn->end << std::endl;
                        std::terminate();
                    }
                    done = false;
                }
            } else if (kind == BCF_IDX) {
                int tid = bcf_hdr_name2id(hdr, rgn->chrom.c_str());
                iter_q = bcf_itr_queryi(idx_v, tid, rgn->start, rgn->end);
                if (iter_q == nullptr) {
                    std::cerr << "\nError: Null iterator when trying to fetch from vcf file in fetch " << rgn->chrom << " " << rgn->start << " " << rgn->end << std::endl;
                    std::terminate();
                }
                done = false;
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
        } else if (kind == BED_IDX || kind == VCF_IDX) {
            kstring_t str = {0,0,0};
            if (iter_q != nullptr) {
                res = tbx_itr_next(fp, idx_t, iter_q, &str);
                if (res < 0) {
                    if (res < -1) {
                        std::cerr << "Error: iterating vcf file returned " << res << std::endl;
                    }
                    done = true;
                    return;
                }
            } else {
                res = hts_getline(fp, '\n', &str);
                if (res < 0) {
                    if (res < -1) {
                        std::cerr << "Error: iterating bed.gz file returned " << res << std::endl;
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
            } else {
                res = vcf_parse(&str, hdr, v);
                if (res < 0) {
                    if (res < -1) {
                        std::cerr << "Error: iterating vcf file returned " << res << std::endl;
                    }
                    done = true;
                    return;
                }
                parseVcfRecord();
            }
        } else if (kind > BCF_IDX) {  // non indexed but cached
            if (iter_blk != vals_end) {
                if (iter_blk->start < region_end) {
                    chrom = iter_blk->chrom;
                    start = iter_blk->start;
                    stop = iter_blk->end;
                    rid = iter_blk->name;
                    vartype = iter_blk->vartype;
                    variantString = iter_blk->line;
                    ++iter_blk;
                } else {
                    done = true;
                }
            } else {
                done = true;
            }
        }
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
				return print_cached(allBlocks[chrm], chrm, pos, false, variantString);
			} else {
				return print_cached(allBlocks_flat, chrm, pos, true, variantString);
			}
		}
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
}
