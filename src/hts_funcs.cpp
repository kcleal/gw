//
// Created by Kez Cleal on 04/08/2022.
//

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "htslib/hts.h"
#include "htslib/sam.h"
#include "htslib/tbx.h"
#include "htslib/vcf.h"

#include "BS_thread_pool.h"
#include "termcolor.h"
#include "bigWig.h"
#include "glob_cpp.hpp"
#include "natsort.hpp"
#include "drawing.h"
#include "segments.h"
#include "themes.h"
#include "hts_funcs.h"


namespace HGW {

    class GenomeJob {
    public:
        bool success;
        std::string refName, path, longestName;
        int longest;
        faidx_t *fai;
        GenomeJob(std::string qRefName, std::string qPath) {
            refName = qRefName;
            path = qPath;
            success = false;
            longest = 0;
        }
    };

    bool guessGenomeJob(GenomeJob *j, sam_hdr_t *hdr_ptr) {
        j->fai = fai_load(j->path.c_str());
        if (!j->fai) {
            return false;
        }
        for (int tid=0; tid < hdr_ptr->n_targets; tid++) {
            const char *chrom_name = sam_hdr_tid2name(hdr_ptr, tid);
            int bam_length = (int)sam_hdr_tid2len(hdr_ptr, tid);
            if (bam_length > j->longest) {
                j->longestName = chrom_name;
                j->longest = bam_length;
            }
            if (!faidx_has_seq(j->fai, chrom_name) || (bam_length != faidx_seq_len(j->fai, chrom_name))) {
                j->success = false;
                return false;
            }
            if (tid > 23) {
                break;
            }
            j->success = true;
        }
        return false;
    }

    // takes an input bam (inputName) and sets the genome_tag in opts, puts inputName in bam_paths if bam_paths is empty,
    // adds a region if one not provided
    void guessRefGenomeFromBam(std::string &inputName, Themes::IniOptions &opts, std::vector<std::string> &bam_paths, std::vector<Utils::Region> &regions) {

        std::string query_bam;
        if (inputName.empty()) {
            if (bam_paths.empty()) {
                std::cerr << "Error: either a genome tag/file or a bam path must be provided\n";
            }
            query_bam = bam_paths[0];
        } else {
            query_bam = inputName;
        }
        htsFile* f = sam_open(query_bam.c_str(), "r");
        sam_hdr_t *hdr_ptr = sam_hdr_read(f);
        int online = 0;
        for (auto & refName : opts.myIni["genomes"]) {
            if (!Utils::startsWith(refName.second, "http")) {
                GenomeJob j = GenomeJob(refName.first, refName.second);
                guessGenomeJob(&j, hdr_ptr);
                if (j.success) {
                    if (bam_paths.empty()) {
                        bam_paths.push_back(query_bam);
                    }
                    inputName = j.refName;
                    if (regions.empty() && j.longest) {
                        std::cerr << j.longestName << std::endl;
                        regions.push_back(Utils::parseRegion(j.longestName));
                    }
                    return;
                }
            } else {
                online += 1;
            }
        }
        BS::thread_pool pool;
        std::vector<GenomeJob> jobs;
        std::vector<std::future<bool>> genomeFutures;
        jobs.reserve(online);
        for (auto & refName : opts.myIni["genomes"]) {
            if (Utils::startsWith(refName.second, "http")) {
                jobs.push_back(GenomeJob(refName.first, refName.second));
                genomeFutures.push_back(pool.submit(guessGenomeJob, &jobs.back(), hdr_ptr));
            }
        }
        pool.wait_for_tasks();
        for (auto & j : jobs) {
            if (j.success) {
                if (bam_paths.empty()) {
                    bam_paths.push_back(query_bam);
                }
                inputName = j.refName;
                if (regions.empty() && j.longest) {
                    regions.push_back(Utils::parseRegion(j.longestName));
                }
                return;
            }
        }
        std::cerr << "Error: could not find suitable reference genome in .gw.ini. Try a local file?\n";
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

    void applyFilters_noDelete(std::vector<Parse::Parser> &filters, std::vector<Segs::Align>& readQueue, const sam_hdr_t* hdr,
                      int bamIdx, int regionIdx) {
        for (auto &align: readQueue) {
            for (auto &f: filters) {
                bool passed = f.eval(align, hdr, bamIdx, regionIdx);
                if (!passed) {
                    align.y = -2;
                }
            }
        }
    }

    void collectReadsAndCoverage(Segs::ReadCollection &col, htsFile *b, sam_hdr_t *hdr_ptr,
                                 hts_idx_t *index, int threads, Utils::Region *region,
                                 bool coverage, std::vector<Parse::Parser> &filters, BS::thread_pool &pool,
                                 const int parse_mods_threshold, int sortReadsBy) {

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
            return;
//            throw std::runtime_error("");
        }

        while (sam_itr_next(b, iter_q, readQueue.back().delegate) >= 0) {
            src = readQueue.back().delegate;
            if (src->core.flag & 4 || src->core.n_cigar == 0) {
                continue;
            }
            readQueue.emplace_back(bam_init1());
        }
        src = readQueue.back().delegate;
        if (src->core.flag & 4 || src->core.n_cigar == 0) {
            bam_destroy1(src);
            readQueue.pop_back();
        }

        Segs::init_parallel(readQueue, threads, pool, parse_mods_threshold, sortReadsBy == 2);

        if (!filters.empty()) {
            applyFilters(filters, readQueue, hdr_ptr, col.bamIdx, col.regionIdx);
        }

        if (coverage) {
            int l_arr = (int)col.covArr.size() - 1;
            for (auto &i : readQueue) {
                Segs::addToCovArray(col.covArr, i, region->start, region->end, l_arr);
            }
        }
        col.collection_processed = false;
    }

    // WIP, still running in to segfaults strlen? Not sure what is causing it

//    void iterDrawParallel(Segs::ReadCollection &col,
//                          htsFile *b,
//                          sam_hdr_t *hdr_ptr,
//                          hts_idx_t *index,
//                          int threads,
//                          Utils::Region *region,
//                          bool coverage,
//                          std::vector<Parse::Parser> &filters,
//                          Themes::IniOptions &opts,
//                          SkCanvas *canvas,
//                          float trackY,
//                          float yScaling,
//                          Themes::Fonts &fonts,
//                          float refSpace,
//                          BS::thread_pool &pool,
//                          float pointSlop,
//                          float textDrop,
//                          float pH) {
//
//        int step = (region->end - region->start) / threads;
//        std::vector<Utils::Region> tmpRegions;
//        tmpRegions.resize(threads);
//        int start = region->start;
//        int end = start + step;
//
//        std::vector<htsFile* > bams;
//        std::vector<sam_hdr_t* > headers;
//        std::vector<hts_idx_t* > indexes;
////        std::vector<sk_sp<SkSurface>> surfaces;
////        std::vector<SkCanvas*> canvases;
//
//        bams.reserve(threads);
//        headers.reserve(threads);
//        indexes.reserve(threads);
////        surfaces.reserve(threads);
////        canvases.reserve(threads);
//        for (int i=0; i < threads; ++i) {
//            // todo monitorScale
//
////            surfaces.emplace_back(SkSurface::MakeRasterN32Premul(opts.dimensions.x * 2, opts.dimensions.y * 2));
////            canvases.push_back(surfaces[i]->getCanvas());
//
//            htsFile* f = sam_open("HG002.bam", "r");
////            hts_set_fai_filename(f, "/Users/sbi8kc2/Documents/data/db/hg19/ucsc.hg19.fa");
//            hts_set_threads(f, 1);
//            bams.push_back(f);
//            sam_hdr_t *hdr_ptr = sam_hdr_read(f);
//            headers.push_back(hdr_ptr);
//            hts_idx_t* idx = sam_index_load(f, "HG002.bam");
//            indexes.push_back(idx);
//
//            tmpRegions[i].chrom = col.region->chrom;
//            tmpRegions[i].start = start;
//            tmpRegions[i].end = end;
//            start += step;
//            end += step;
//        }
//
//        std::vector<Segs::ReadCollection> collections;
//        collections.resize(threads);
//        std::vector<std::future<void>> jobs;
//        float offset = 0;
//
//        std::vector<sk_sp<SkImage>> images;
//        images.resize(threads);
//
//        for (int idx=0; idx < threads; ++idx) {
//            collections[idx] = col;
//            Segs::ReadCollection &tCol = collections[idx];
//            tCol.readQueue.clear();
//            tCol.xOffset += offset;
//            offset += 300;
//            tCol.region = &tmpRegions[idx];
//            if (opts.max_coverage) {
//                tCol.covArr.resize(tmpRegions[idx].end - tmpRegions[idx].start, 0);
//                if (opts.snp_threshold > tmpRegions[idx].end - tmpRegions[idx].start) {
//                    // todo not allowed
////                    std::cerr << " ERROR \n";
//                }
//            }
//            std::future<void> my_future = pool.submit(
//                    [&]
//                    {
//                        sk_sp<SkSurface> surface = SkSurface::MakeRasterN32Premul(opts.dimensions.x * 2, opts.dimensions.y * 2);
//                        SkCanvas * canv = surface->getCanvas();
//
//                        iterDraw(tCol, bams[idx], headers[idx],
//                                          indexes[idx], &tmpRegions[idx],
//                                          coverage,
//                                          filters, opts, canv,
//                                          trackY, yScaling, fonts, refSpace,
//                                          pointSlop, textDrop, pH);
//
//                        canv->flush();
//                        surface->flush();
////                        images[idx] = surface->makeImageSnapshot();
////                        Manager::imageToPng(images[idx], "test.png");
//
//                    });
////            my_future.get();
//            jobs.push_back(std::move(my_future));
//        }
//        for (auto &f: jobs) {
//            f.get();
//        }
//        for (auto &im : images) {
//            canvas->drawImage(im, 0, 0);
//        }
//
//        for (auto &bm : bams) {
//            hts_close(bm);
//        }
//        for (auto &hd: headers) {
//            bam_hdr_destroy(hd);
//        }
//        for (auto &idx: indexes) {
//            hts_idx_destroy(idx);
//        }
//        std::cerr << " got here\n";
//
//    }


    void iterDrawParallel(Segs::ReadCollection &col,
                          htsFile *b,
                          sam_hdr_t *hdr_ptr,
                          hts_idx_t *index,
                          int threads,
                          Utils::Region *region,
                          bool coverage,
                          std::vector<Parse::Parser> &filters,
                          Themes::IniOptions &opts,
                          SkCanvas *canvas,
                          float trackY,
                          float yScaling,
                          Themes::Fonts &fonts,
                          float refSpace,
                          BS::thread_pool &pool,
                          float pointSlop,
                          float textDrop,
                          float pH,
                          float monitorScale) {
        const int BATCH = 1500;
        bam1_t *src;
        hts_itr_t *iter_q;
        int tid = sam_hdr_name2tid(hdr_ptr, region->chrom.c_str());
        std::vector<Segs::Align>& readQueue = col.readQueue;
        if (!readQueue.empty()) {
            for (auto &item: readQueue) {
                bam_destroy1(item.delegate);
            }
            readQueue.clear();
        }
        readQueue.reserve(BATCH);
        for (int i=0; i < BATCH; ++i) {
            readQueue.emplace_back(bam_init1());
        }
        iter_q = sam_itr_queryi(index, tid, region->start, region->end);
        if (iter_q == nullptr) {
            std::cerr << "\nError: Null iterator when trying to fetch from HTS file in collectReadsAndCoverage " << region->chrom << " " << region->start << " " << region->end << std::endl;
//            throw std::runtime_error("");
            return;
        }
        bool filter = !filters.empty();
        const int parse_mods_threshold = (opts.parse_mods) ? 50 : 0;

        int j = 0;
        while (sam_itr_next(b, iter_q, readQueue[j].delegate) >= 0) {
            src = readQueue[j].delegate;
            if (src->core.flag & 4 || src->core.n_cigar == 0) {
                continue;
            }
            j += 1;
            if (j < BATCH) {
                continue;
            }
            Segs::init_parallel(readQueue, threads, pool, parse_mods_threshold, false);
            if (filter) {
                applyFilters_noDelete(filters, readQueue, hdr_ptr, col.bamIdx, col.regionIdx);
            }
            if (coverage) {
                int l_arr = (int)col.covArr.size() - 1;
                for (int i=0; i < BATCH; ++ i) {
                    if (readQueue[i].y != -2) {
                        Segs::addToCovArray(col.covArr, readQueue[i], region->start, region->end, l_arr);
                    }
                }
            }
            Segs::findY(col, readQueue, opts.link_op, opts, false, 0);
            Drawing::drawCollection(opts, col, canvas, trackY, yScaling, fonts, opts.link_op, refSpace, pointSlop, textDrop, pH, monitorScale);

            for (int i=0; i < BATCH; ++ i) {
                Segs::align_clear(&readQueue[i]);
            }
            j = 0;
        }

        if (j < BATCH) {
            readQueue.erase(readQueue.begin() + j, readQueue.end());
            if (!readQueue.empty()) {
                Segs::init_parallel(readQueue, threads, pool, parse_mods_threshold, false);
                if (!filters.empty()) {
                    applyFilters_noDelete(filters, readQueue, hdr_ptr, col.bamIdx, col.regionIdx);
                }
                if (coverage) {
                    int l_arr = (int)col.covArr.size() - 1;
                    for (int i=0; i < BATCH; ++ i) {
                        if (readQueue[i].y != -2) {
                            Segs::addToCovArray(col.covArr, readQueue[i], region->start, region->end, l_arr);
                        }
                    }
                }
                Segs::findY(col, readQueue, opts.link_op, opts, false, 0);
                Drawing::drawCollection(opts, col, canvas, trackY, yScaling, fonts, opts.link_op, refSpace, pointSlop, textDrop, pH, monitorScale);
                for (int i=0; i < BATCH; ++ i) {
                    Segs::align_clear(&readQueue[i]);
                }
            }
        }
    }

    void iterDraw(Segs::ReadCollection &col, htsFile *b, sam_hdr_t *hdr_ptr,
                  hts_idx_t *index, Utils::Region *region,
                  bool coverage,
                  std::vector<Parse::Parser> &filters, Themes::IniOptions &opts, SkCanvas *canvas,
                  float trackY, float yScaling, Themes::Fonts &fonts, float refSpace,
                  float pointSlop, float textDrop, float pH, float monitorScale) {

        bam1_t *src;
        hts_itr_t *iter_q;
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
//            throw std::runtime_error("");
            return;
        }
        bool filter = !filters.empty();
        const int parse_mods_threshold = (opts.parse_mods) ? 50 : 0;

        while (sam_itr_next(b, iter_q, readQueue.back().delegate) >= 0) {
            src = readQueue.back().delegate;
            if (src->core.flag & 4 || src->core.n_cigar == 0) {
                continue;
            }
            Segs::align_init(&readQueue.back(), parse_mods_threshold, false);
            if (filter) {
                applyFilters_noDelete(filters, readQueue, hdr_ptr, col.bamIdx, col.regionIdx);
                if (readQueue.back().y == -2) {
                    Segs::align_clear(&readQueue.back());
                    continue;
                }
            }
            if (coverage) {
                int l_arr = (int)col.covArr.size() - 1;
                Segs::addToCovArray(col.covArr, readQueue.back(), region->start, region->end, l_arr);
            }
            Segs::findY(col, readQueue, opts.link_op, opts, false, 0);
            Drawing::drawCollection(opts, col, canvas, trackY, yScaling, fonts, opts.link_op, refSpace, pointSlop, textDrop, pH, monitorScale);
            Segs::align_clear(&readQueue.back());
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

    void refreshLinkedCollection(Segs::ReadCollection &cl, Themes::IniOptions &opts, int *samMaxY, int sortReadsBy) {
        Segs::resetCovStartEnd(cl);
        cl.levelsStart.clear();
        cl.levelsEnd.clear();
        cl.linked.clear();
        cl.skipDrawingReads = false;
        for (auto &itm: cl.readQueue) { itm.y = -1; }
        int maxY = Segs::findY(cl, cl.readQueue, opts.link_op, opts, false, sortReadsBy);
        *samMaxY = (maxY > *samMaxY || opts.tlen_yscale) ? maxY : *samMaxY;
    }

    void refreshLinked(std::vector<Segs::ReadCollection> &collections, Themes::IniOptions &opts, int *samMaxY, int sortReadsBy) {
        for (auto &cl : collections) {
            refreshLinkedCollection(cl, opts, samMaxY, sortReadsBy);
        }
    }

    void appendReadsAndCoverage(Segs::ReadCollection &col, htsFile *b, sam_hdr_t *hdr_ptr,
                                 hts_idx_t *index, Themes::IniOptions &opts, bool coverage, bool left, int *samMaxY,
                                std::vector<Parse::Parser> &filters, BS::thread_pool &pool, int sortReadsBy) {
        bam1_t *src;
        hts_itr_t *iter_q;
        std::vector<Segs::Align>& readQueue = col.readQueue;
        Utils::Region *region = col.region;
        bool tlen_y = opts.tlen_yscale;
        int tid = sam_hdr_name2tid(hdr_ptr, region->chrom.c_str());
        if (tid < 0) {
            return;
        }
        int lastPos;
        const int parse_mods_threshold = (opts.parse_mods) ? 50 : 0;

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
//                throw std::runtime_error("");
                return;
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
//                throw std::runtime_error("");
                return;
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
            Segs::init_parallel(newReads, opts.threads, pool, parse_mods_threshold, sortReadsBy == 2);
            bool findYall = false;
            if (col.vScroll == 0 && opts.link_op == 0) {  // only new reads need findY, otherwise, reset all below
                int maxY = Segs::findY(col, newReads, opts.link_op, opts, left, sortReadsBy);
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
                refreshLinkedCollection(col, opts, samMaxY, sortReadsBy);
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

    VCFfile::VCFfile() {
        label_to_parse = nullptr;
        cacheStdin = false;
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
        std::string l2p;
        if (label_to_parse != nullptr) {
            l2p = label_to_parse;
        }
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
                throw std::runtime_error("Error: could not find --parse-label in info");
            }
        } else if (l2p.find("filter") != std::string::npos) {
            parse = 6;
        } else if (l2p.find("qual") != std::string::npos) {
            parse = 5;
        } else if (l2p.find("id") != std::string::npos) {
            parse = 2;
        } else {
            throw std::runtime_error("Error: --label-to-parse was not understood, accepted fields are 'id / qual / filter / info.$NAME'");
        }
    }

    void VCFfile::next() {

        int res = bcf_read(fp, hdr, v);
        if (cacheStdin) {
            lines.push_back(bcf_dup(v));
        }

        if (res < -1) {
            std::cerr << "Error: reading vcf resulted in error code " << res << std::endl;
            throw std::runtime_error("");
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
                        throw std::runtime_error("");
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
                        throw std::runtime_error("");
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
                    throw std::runtime_error("");
                }
                break;
        }
        if (seenLabels != nullptr && !(*seenLabels).empty() && !seenLabels->contains(label)) {
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

    void GwTrack::close() {
        if ((kind == BIGWIG || kind == BIGBED) && bigWig_fp) {
            bwClose(bigWig_fp);
            if (kind == BIGWIG) {
                bwDestroyOverlappingIntervals(bigWig_intervals);
            } else {
                bbDestroyOverlappingEntries(bigBed_entries);
            }
        }
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
//        bcf_unpack(v, BCF_UN_INFO);
        kstring_t kstr = {0,0,0};
        bcf_unpack(v, BCF_UN_INFO);
        vcf_format(hdr, v, &kstr);
        variantString = kstr.s;

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
        done = false;
        this->add_to_dict = add_to_dict;
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
        } else if (Utils::endsWith(p, ".bigwig") || Utils::endsWith(p, ".bw")) {
            kind = BIGWIG;
        } else if (Utils::endsWith(p, ".bigbed") || Utils::endsWith(p, ".bb")) {
            kind = BIGBED;
        } else {
            kind = GW_LABEL;
        }
        // if drawing image tiles, VCF/BCF files are opened with VCFfile class
        // only tracks are processed here:
        if (kind == VCF_NOI) {
            fp = bcf_open(path.c_str(), "r");
            if (!fp) {
                std::cerr << "Error: could not open " << path << std::endl;
                throw std::exception();
            }
            hdr = bcf_hdr_read(fp);
            if (!hdr) {
                std::cerr << "Error: could not open header of " << path << std::endl;
                throw std::exception();
            }
            v = bcf_init1();
            v->max_unpack = BCF_UN_INFO;
            while (true) {
                int res = bcf_read(fp, hdr, v);
                if (res < -1) {
                    std::cerr << "Error: reading vcf resulted in error code " << res << std::endl;
                    throw std::runtime_error("bcf_read error");
                } else if (res == -1) {
                    done = true;
                    break;
                }
                Utils::TrackBlock b;
                parseVcfRecord(b);
                allBlocks[b.chrom].add(b.start, b.end, b);
            }
        } else if (kind == BED_NOI || kind == GW_LABEL) {
            fpu = std::make_shared<std::ifstream>();
            fpu->open(p);
            if (!fpu->is_open()) {
                std::cerr << "Error: opening track file " << path << std::endl;
                throw std::exception();
            }
            if (!add_to_dict) {
                return;
            }
            while (true) {
                auto got_line = (bool)getline(*fpu, tp);
                if (!got_line) {
                    done = true;
                    break;
                }
                if (tp[0] == '#') {
                    continue;
                }
                std::vector<std::string> parts = Utils::split(tp, '\t');
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
            if (!fp) {
                std::cerr << "Error: could not open " << path << std::endl;
                throw std::exception();
            }
            idx_t = tbx_index_load(p.c_str());
            if (!idx_t) {
                std::cerr << "Error: could not open index of " << path << std::endl;
                throw std::exception();
            }
        } else if (kind == GFF3_NOI || kind == GTF_NOI) {
            fpu = std::make_shared<std::ifstream>();
            fpu->open(p);
            if (!fpu->is_open()) {
                std::cerr << "Error: opening track file " << path << std::endl;
                throw std::exception();
            }
            if (!add_to_dict) {
                return;
            }
            int count = 0;
            std::vector<Utils::TrackBlock> track_blocks;
            ankerl::unordered_dense::map< std::string, int> name_to_track_block_idx;

            while (true) {
                auto got_line = (bool)getline(*fpu, tp);
                if (!got_line) {
                    done = true;
                    break;
                }
                count += 1;
                if (tp[0] == '#') {
                    continue;
                }
                Utils::TrackBlock b;
                b.parts = Utils::split(tp, '\t');
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
            if (!fp) {
                std::cerr << "Error: could not open " << path << std::endl;
                throw std::exception();
            }
            idx_t = tbx_index_load(p.c_str());
            if (!idx_t) {
                std::cerr << "Error: could not open index of " << path << std::endl;
                throw std::exception();
            }
        } else if (kind == BCF_IDX) {
            fp = bcf_open(p.c_str(), "r");
            if (!fp) {
                std::cerr << "Error: could not open " << path << std::endl;
                throw std::exception();
            }
            hdr = bcf_hdr_read(fp);
            if (!hdr) {
                std::cerr << "Error: could not open header of " << path << std::endl;
                throw std::exception();
            }
            idx_v = bcf_index_load(p.c_str());
            v = bcf_init1();
            v->max_unpack = BCF_UN_INFO;
        } else if (kind == VCF_IDX) {
            fp = bcf_open(path.c_str(), "r");
            if (!fp) {
                std::cerr << "Error: could not open " << path << std::endl;
                throw std::exception();
            }
            hdr = bcf_hdr_read(fp);
            if (!hdr) {
                std::cerr << "Error: could not open header of " << path << std::endl;
                throw std::exception();
            }
            idx_t = tbx_index_load(path.c_str());
            v = bcf_init1();
            v->max_unpack = BCF_UN_INFO;
        } else if (kind == BIGWIG) {
            bigWig_fp = bwOpen(path.c_str(), NULL, "r");
            if(bwInit(1<<17) != 0) {
                std::cerr << "Error: bw init error from file: " << path << std::endl;
                throw std::exception();
            }
            if (!bigWig_fp) {
                std::cerr << "Error: could not open " << path << std::endl;
                throw std::exception();
            }
        } else if (kind == BIGBED) {
            bigWig_fp = bbOpen(path.c_str(), NULL);
            if(bwInit(1<<17) != 0) {
                std::cerr << "Error: bw init error from file: " << path << std::endl;
                throw std::exception();
            }
            if (!bigWig_fp) {
                std::cerr << "Error: could not open " << path << std::endl;
                throw std::exception();
            }
        } else {
            std::cerr << "Error: file stype not supported for " << path << std::endl;
            throw std::exception();
        }

        for (auto &item : allBlocks) {
            item.second.index();
        }
    }

    void GwTrack::fetch(const Utils::Region *rgn) {
        if (kind > BCF_IDX) {  // non-indexed
            if (rgn == nullptr) {

            } else {
                if (allBlocks.contains(rgn->chrom)) {
                    std::vector<size_t> a;
                    if (kind == GFF3_NOI || kind == GTF_NOI) {
                        allBlocks[rgn->chrom].overlap(std::max(1, rgn->start - 100000), rgn->end + 100000, a);
                    } else {
                        allBlocks[rgn->chrom].overlap(rgn->start, rgn->end, a);
                    }
                    overlappingBlocks.clear();
                    if (a.empty()) {
                        done = true;
                        return;
                    }
                    done = false;
                    fetch_start = rgn->start;
                    fetch_end = rgn->end;
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
                        fetch_start = rgn->start;
                        fetch_end = rgn->end;
                    }
                }
            } else if (kind == BCF_IDX) {
                int tid = bcf_hdr_name2id(hdr, rgn->chrom.c_str());
                iter_q = bcf_itr_queryi(idx_v, tid, rgn->start, rgn->end);
                if (iter_q == nullptr) {
                    done = true;
                } else {
                    done = false;
                    fetch_start = rgn->start;
                    fetch_end = rgn->end;
                }
            } else if (kind == BIGWIG) {
                bigWig_intervals = bwGetValues(bigWig_fp, rgn->chrom.c_str(), (uint32_t)std::max(1, rgn->start - 100000), (uint32_t)rgn->end + 100000, 0);
                done = true;

            } else if (kind == BIGBED) {
                bigBed_entries = bbGetOverlappingEntries(bigWig_fp, rgn->chrom.c_str(), (uint32_t)std::max(1, rgn->start), (uint32_t)rgn->end, 1);
                chrom = rgn->chrom;
                current_iter_index = 0;
                done = false;
                num_intervals = (int)bigBed_entries->l;
                if (current_iter_index == num_intervals) {
                    done = true;
                }
            }
        }
    }

    void GwTrack::next() {
        int res;
        if (done) {
            return;
        }

        if (kind > BCF_IDX) {  // non indexed cached VCF_NOI / BED_NOI / GFF3 (todo) / GW_LABEL / STDIN?
            // add_to_dict==false, only BED and GW_LABEL files supported (iterate whole file)
            if (!add_to_dict) {
                // nullptr is an indication to iterate over everything
                while (true) {
                    auto got_line = (bool)getline(*fpu, tp);
                    if (!got_line) {
                        done = true;
                        return;
                    }
                    if (tp[0] == '#') {
                        continue;
                    }
                    std::vector<std::string> parts = Utils::split_keep_empty_str(tp, '\t');
                    chrom = parts[0];
                    chrom2 = chrom;
                    start = std::stoi(parts[1]);
                    if (kind == BED_NOI) {  // bed
                        stop = std::stoi(parts[2]);
                        if (parts.size() > 3) {
                            rid = parts[3];
                        } else {
                            rid = std::to_string(fileIndex);
                        }
                    } else { // assume gw_label file
                        if (kind != GW_LABEL) {
                            throw std::runtime_error("Only BED or GW_LABEL files supported");
                        }

                        stop = start + 1;
                        rid = parts[2];
                    }
                    fileIndex += 1;
                    break;
                }
                return;
            }
            // Values fetched from interval tree
            else {
                while (true) {
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
                        if (kind == VCF_NOI && (start < fetch_start - *variant_distance && stop > fetch_end + *variant_distance)) {
                            continue;
                        }
                        break;
                    } else {
                        done = true;
                        break;
                    }
                }
                return;
            }

        }
        // Indexed formats below:
        if (kind == BCF_IDX) {
            while (true) {
                res = bcf_itr_next(fp, iter_q, v);
                if (res < 0) {
                    if (res < -1) {
                        std::cerr << "Error: iterating bcf file returned " << res << std::endl;
                    }
                    done = true;
                    return;
                }
                parseVcfRecord();
                if (start < fetch_start - *variant_distance && stop > fetch_end + *variant_distance) {
                    continue;
                }
                break;
            }
        } else if (kind == VCF_IDX) {
            if (iter_q == nullptr) {
                done = true;
                return;
            }
            kstring_t str = {0,0, nullptr};
            while (true) {
                res = tbx_itr_next(fp, idx_t, iter_q, &str);
                if (res < 0) {
                    if (res < -1) {
                        std::cerr << "Error: iterating returned code: " << res << " from file: " << path  << std::endl;
                    }
                    done = true;
                    return;
                }
                res = vcf_parse(&str, hdr, v);
                if (res < 0) {
                    if (res < -1) {
                        std::cerr << "Error: iterating returned code: " << res << " from file: " << path  << std::endl;
                    }
                    done = true;
                    return;
                }
                parseVcfRecord();
                if (start < fetch_start - *variant_distance && stop > fetch_end + *variant_distance) {
                    continue;
                }
                break;
            }

        } else if (kind == BED_IDX || kind == GFF3_IDX || kind == GTF_IDX) {
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
                    if (kind == GFF3_IDX) {
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
            }
        } else if (kind == BIGBED) {
            if (current_iter_index == num_intervals) {
                done = true;
//                bbDestroyOverlappingEntries(bigBed_entries);
                return;
            }
            start = (int)bigBed_entries->start[current_iter_index];
            stop = (int)bigBed_entries->end[current_iter_index];
            if (bigBed_entries->str != nullptr) {
                parts = Utils::split(bigBed_entries->str[current_iter_index], '\t');
                rid = parts[0];
            }
            current_iter_index += 1;
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

    void saveVcf(std::string &input_vcf_path, std::string &output_vcf_path, std::string &labels_path) {
        std::string variantFilename;
        std::filesystem::path fsp(input_vcf_path);
    #if defined(_WIN32) || defined(_WIN64)
        const wchar_t* pc = fsp.filename().c_str();
            std::wstring ws(pc);
            std::string p(ws.begin(), ws.end());
            variantFilename = p;
    #else
        variantFilename = fsp.filename();
    #endif

        ankerl::unordered_dense::map< std::string, std::vector<std::string>> label_dict;
        ankerl::unordered_dense::set<std::string> seen_labels;
        std::ifstream fs;
        fs.open(labels_path);
        std::string s;
        while (std::getline(fs, s)) {
            if (Utils::startsWith(s, "#")) {
                continue;
            }
            std::vector<std::string> v = Utils::split_keep_empty_str(s, '\t');
            if (variantFilename == v[6]) {
                label_dict[v[2]] = v;
                if (v[3] != "PASS") {
                    seen_labels.insert(v[3]);
                }
            }
        }

        HGW::VCFfile input_vcf;
        input_vcf.open(input_vcf_path);

        // prepare new header
        bcf_hdr_t *new_hdr = bcf_hdr_init("w");
        new_hdr = bcf_hdr_merge(new_hdr, input_vcf.hdr);
        if (bcf_hdr_sync(new_hdr) < 0) {
            throw std::runtime_error("bcf_hdr_sync(hdr2) after merge");
        }
        const char *lg = "##source=GW";
        if (bcf_hdr_append(new_hdr, lg) < 0) {
            throw std::runtime_error("Error: Unable to write new header line");
        }
        for (auto &l: seen_labels) {
            if (l != "PASS") {
                std::string str = "##FILTER=<ID=" + l + ",Description=\"GW custom label\">";
                if (bcf_hdr_append(new_hdr, str.c_str()) < 0) {
                    throw std::runtime_error("bcf_hdr_append(new_hdr) failed");
                }
            }
        }
        const char *l0 = "##INFO=<ID=GW_DATE,Number=1,Type=String,Description=\"Date of GW label\">";
        const char *l1 = "##INFO=<ID=GW_PREV,Number=1,Type=String,Description=\"Previous GW label\">";
        bcf_hdr_append(new_hdr, l0);
        bcf_hdr_append(new_hdr, l1);

        htsFile *fp_out = bcf_open(output_vcf_path.c_str(), "w");
        if (bcf_hdr_write(fp_out, new_hdr) < 0) {
            throw std::runtime_error("Error: Unable to write new header");
        }

        while (true) {
            input_vcf.next();
            if (input_vcf.done) {
                break;
            }
            if (label_dict.contains(input_vcf.rid)) {
                std::vector<std::string> &label_parts =  label_dict[input_vcf.rid];
                const char *prev_label = new_hdr->id[BCF_DT_ID][*input_vcf.v->d.flt].key;
                std::string prev_label_str = prev_label;
                if (prev_label_str != label_parts[3]) {
                    int filter_id = bcf_hdr_id2int(new_hdr, BCF_DT_ID, label_parts[3].c_str());
                    if (bcf_update_filter(new_hdr, input_vcf.v, &filter_id, 1) < 0) {
                        std::cerr << "Error: Failed to update filter, id " << input_vcf.v->rid << std::endl;
                    }
                }
                if (!label_parts[5].empty()) {  // saveDate was set
                    if (bcf_update_info_string(new_hdr, input_vcf.v, "GW_PREV", prev_label) < 0) {
                        std::cerr << "Error: Updating GW_PREV failed, id " << input_vcf.v->rid << std::endl;
                    }
                    if (bcf_update_info_string(new_hdr, input_vcf.v, "GW_DATE", label_parts[5].c_str()) < 0) {
                        std::cerr << "Error: Updating GW_DATE failed, id " << input_vcf.v->rid << std::endl;
                    }
                }
                if (bcf_write(fp_out, new_hdr, input_vcf.v) < 0) {
                    std::cerr << "Error: Writing new vcf record failed, id " << input_vcf.v->rid << std::endl;
                    throw std::runtime_error("");
                }

            } else {
                if (bcf_write(fp_out, new_hdr, input_vcf.v) < 0) {
                    std::cerr << "Error: Writing new vcf record failed, id " << input_vcf.v->rid << std::endl;
                    throw std::runtime_error("");
                }
            }
        }
        bcf_hdr_destroy(new_hdr);
        bcf_close(fp_out);
    }

    GwVariantTrack::GwVariantTrack(std::string &path, bool cacheStdin, Themes::IniOptions *t_opts, int endIndex,
                                   std::vector<std::string> &t_labelChoices,
                                   std::shared_ptr< ankerl::unordered_dense::map< std::string, Utils::Label>>  t_inputLabels,
                                   std::shared_ptr< ankerl::unordered_dense::set<std::string>> t_seenLabels) {

        labelChoices = t_labelChoices;
        inputLabels = t_inputLabels;
        mouseOverTileIndex = -1;
        blockStart = 0;
        m_opts = t_opts;

        std::filesystem::path fsp(path);
#if defined(_WIN32) || defined(_WIN64)
        const wchar_t* pc = fsp.filename().c_str();
        std::wstring ws(pc);
        std::string p(ws.begin(), ws.end());
        fileName = p;
#else
        fileName = fsp.filename();
#endif


        if (cacheStdin || Utils::endsWith(path, ".vcf") ||
            Utils::endsWith(path, ".vcf.gz") ||
            Utils::endsWith(path, ".bcf")) {
            variantTrack.done = true;
            vcf.done = false;
            type = VCF;
            vcf.seenLabels = t_seenLabels;
            vcf.cacheStdin = cacheStdin;
            vcf.label_to_parse = m_opts->parse_label.c_str();
            vcf.open(path);
            trackDone = &vcf.done;
            if (endIndex > 0) {
                nextN(endIndex);
            }
            blockStart = endIndex;
        } else if (Utils::endsWith(path, ".png") || Utils::endsWith(path, ".png'") || Utils::endsWith(path, ".png\"")) {
            type = IMAGES;
            image_glob = glob_cpp::glob(path);
//#if defined(_WIN32) || defined(_WIN64)
//            std::sort(paths.begin(), paths.end());
//#else
//            std::sort(image_glob.begin(), image_glob.end(), glob_cpp::compareNat);
//            std::sort(image_glob.begin(), image_glob.end(), SI::natural::sort);
//            SI::natural::sort(image_glob);
//#endif

            if (image_glob.empty()) {
                std::cerr << "Warning: no images found with pattern: " << path << std::endl;
            }

        } else {  // bed file or labels file, or some other tsv
            type =  GW_TRACK;
            vcf.done = true;
            variantTrack.done = false;
            variantTrack.open(path, false);
            trackDone = &variantTrack.done;
            variantTrack.fetch(nullptr);  // initialize iterators
            if (endIndex > 0) {
                nextN(endIndex);
            }
            blockStart = endIndex;
        }
        this->path = path;
        init = true;
    }

    GwVariantTrack::~GwVariantTrack() {

    }

    void GwVariantTrack::nextN(int number) {
        if (number == 0 || type == IMAGES) {
            return;
        }
        if (type == VCF) {
            trackDone = &vcf.done;
        } else if (type == GW_TRACK) {
            trackDone = &variantTrack.done;
        }
        while (!(*trackDone)) {
            for (int i=0; i < number; ++i) {
                if (type == VCF) {
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
                    std::string label;
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

    // gets called when new image tiles are loaded (pngs), labels are parsed from filenames if possible
    // variant id is either recorded in the filename, or else is the whole filename
    void GwVariantTrack::appendImageLabels(int startIdx, int number) {
        // rid is the file name for an image
        std::string empty_comment;
        for (int i=startIdx; i < startIdx + number; ++i) {
            if (i < (int)multiLabels.size()) {
                continue;
            }
            if (i >= (int)image_glob.size()) {
                break;
            }
            std::vector<Utils::Region> rt;
            Utils::FileNameInfo info = Utils::parseFilenameInfo(image_glob[i]);
            std::string key = (info.rid.empty()) ? info.fileName : info.rid;
            std::string label;
            if (inputLabels->contains(key)) {
                multiLabels.push_back((*inputLabels)[key]);
            } else {
                multiLabels.push_back(Utils::makeLabel(info.chrom, info.pos, label, labelChoices, key, info.varType, "", false, false, empty_comment));
            }
        }
    }

    void GwVariantTrack::appendVariantSite(std::string &chrom, long start, std::string &chrom2, long stop, std::string &rid, std::string &label, std::string &vartype) {
        long rlen = stop - start;
        std::vector<Utils::Region> v;
        bool isTrans = chrom != chrom2;
        Utils::Region* r1; Utils::Region* r2;
        std::string empty_comment;
        if (!isTrans && rlen <= m_opts->split_view_size) {
            v.resize(1);
            r1 = & v[0];
            r1->chrom = chrom;
            r1->start = (1 > start - m_opts->pad) ? 1 : start - m_opts->pad;
            r1->end = stop + m_opts->pad;
            r1->markerPos = start;
            r1->markerPosEnd = stop;
        } else {
            v.resize(2);
            r1 = &v[0];
            r1->chrom = chrom;
            r1->start = (1 > start - m_opts->pad) ? 1 : start - m_opts->pad;
            r1->end = start + m_opts->pad;
            r1->markerPos = start;
            r1->markerPosEnd = start;
            r2 = &v[1];
            r2->chrom = chrom2;
            r2->start = (1 > stop - m_opts->pad) ? 1 : stop - m_opts->pad;
            r2->end = stop + m_opts->pad;
            r2->markerPos = stop;
            r2->markerPosEnd = stop;
        }
        multiRegions.push_back(v);
        if (inputLabels->contains(rid)) {
            multiLabels.push_back((*inputLabels)[rid]);
        } else {
            multiLabels.push_back(Utils::makeLabel(chrom, start, label, labelChoices, rid, vartype, "", false, false, empty_comment));
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
        bool isVCF = trk.kind == HGW::VCF_NOI || trk.kind == HGW::BCF_IDX || trk.kind == HGW::VCF_IDX;
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
            b->anyToDraw = true;
            if (trk.parts.size() >= 5) {
                b->strand = (trk.parts[5] == "+") ? 1 : (trk.parts[5] == "-") ? -1 : 0;
            }
            if (isVCF) {
                b->vartype = trk.vartype;
            }
            if (trk.kind == BIGBED) {
                if (trk.parts[2] == "-") {
                    b->strand = -1;
                } else if (trk.parts[2] == "+") {
                    b->strand = 1;
                } else {
                    b->strand = 0;
                }
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
