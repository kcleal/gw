//
// Created by Kez Cleal on 04/08/2022.
//

#pragma once

#include <string>
#include <vector>

#include "htslib/faidx.h"
#include "htslib/hfile.h"
#include "htslib/hts.h"
#include "htslib/vcf.h"
#include "htslib/sam.h"
#include "htslib/tbx.h"

#include "parser.h"
#include "../include/unordered_dense.h"
#include "segments.h"
#include "themes.h"


namespace HGW {

     enum FType {
        BED_IDX,
        VCF_IDX,
        BCF_IDX,
        VCF_NOI,  // NOI for no index
        BED_NOI,
        GFF3,
        GW_LABEL,
        STDIN,
    };

    void print_BCF_IDX(hts_idx_t *idx_v, bcf_hdr_t *hdr, std::string &chrom, int pos, htsFile *fp, std::string &id_str, std::string &variantString);

    void print_VCF_NOI(std::string &path, std::string &id_str, std::string &varinatString);

    void print_VCF_IDX(std::string &path, std::string &id_str, std::string &chrom, int pos, std::string &varinatString);

	void print_cached(std::vector<Utils::TrackBlock> &vals, std::string &chrom, int pos, bool flat, std::string &varinatString);

    /*
    * VCF or BCF file reader only. Cache's lines from stdin or non-indexed file. Can parse labels from file
    */
    class VCFfile {
    public:
        VCFfile () = default;
        ~VCFfile();
        htsFile *fp;
        bcf_hdr_t *hdr;
        hts_idx_t *idx_v;
        tbx_t *idx_t;
        std::vector<bcf1_t*> lines;
        bcf1_t *v;
        FType kind;
        std::string path;
        std::string chrom, chrom2, rid, vartype, label, tag;
//        robin_hood::unordered_set<std::string> *seenLabels;
        ankerl::unordered_dense::set<std::string> *seenLabels;
        int parse;
        int info_field_type;
        const char *label_to_parse;
        long start, stop;
        bool done;
        bool cacheStdin;
		bool samples_loaded;
		std::string variantString;
		std::vector<std::string> sample_names;

        void open(std::string f);
        void next();
        void printTargetRecord(std::string &id_str, std::string &chrom, int pos);
		void get_samples();

    };

    void collectReadsAndCoverage(Segs::ReadCollection &col, htsFile *bam, sam_hdr_t *hdr_ptr,
                                 hts_idx_t *index, int threads, Utils::Region *region,
                                 bool coverage, bool low_mem,
                                 std::vector<Parse::Parser> &filters, BS::thread_pool &pool);

    void iterDraw(std::vector<Segs::ReadCollection> &cols, int idx, htsFile *b, sam_hdr_t *hdr_ptr,
                        hts_idx_t *index, int threads, Utils::Region *region,
                        bool coverage, bool low_mem,
                        std::vector<Parse::Parser> &filters, Themes::IniOptions &opts, SkCanvas *canvas,
                        float trackY, float yScaling, Themes::Fonts &fonts, float refSpace);

    void trimToRegion(Segs::ReadCollection &col, bool coverage);

    void appendReadsAndCoverage(Segs::ReadCollection &col, htsFile *bam, sam_hdr_t *hdr_ptr,
                                hts_idx_t *index, Themes::IniOptions &opts, bool coverage, bool left, int *samMaxY,
                                std::vector<Parse::Parser> &filters, BS::thread_pool &pool);

    /*
    * VCF/BCF/BED/LABEL file reader. No line cacheing or label parsing.
    * BED files with no tab index will be cached, as will LABEL files.
    */
    class GwTrack {
    public:
        GwTrack() = default;
        ~GwTrack();

        std::string path;
        std::string chrom, chrom2, rid, vartype;
        int start, stop;
        int fileIndex;
        FType kind;

        htsFile *fp;
        tbx_t *idx_t;
        hts_idx_t *idx_v;
        bcf_hdr_t *hdr;

        bcf1_t *v;
        tbx_t *t;
        hts_itr_t *iter_q;

        int region_end;
        std::vector<std::string> parts;
        std::vector<Utils::TrackBlock> vals;
        std::vector<Utils::TrackBlock>::iterator vals_end;
        std::vector<Utils::TrackBlock>::iterator iter_blk;

        ankerl::unordered_dense::map< std::string, std::vector<Utils::TrackBlock>>  allBlocks;
        std::vector<Utils::TrackBlock> allBlocks_flat;
        Utils::TrackBlock block;
        bool done;
		std::string variantString;

        void open(std::string &p, bool add_to_dict);
        void fetch(const Utils::Region *rgn);
        void next();
        bool findFeature(std::string &feature, Utils::Region &region);
        void parseVcfRecord(Utils::TrackBlock &b);
        void parseVcfRecord();
        void printTargetRecord(std::string &id_str, std::string &chrm, int pos);
    };

    bool searchTracks(std::vector<GwTrack> &tracks, std::string &feature, Utils::Region &region);

    void saveVcf(VCFfile &input_vcf, std::string path, std::vector<Utils::Label> multiLabels);

}
