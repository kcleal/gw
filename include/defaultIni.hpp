//
// Created by Kez Cleal on 12/10/2023.
//

#include <string.h>
#ifndef GW_DEFAULTINI_H
#define GW_DEFAULTINI_H


namespace DefaultIni {
    std::string defaultIniString() {
        return "# GW ini file\n"
               "# -----------\n"
               "# Here you can set reference genomes and tracks and change the default appearance and behavior of GW\n"
               "\n"
               "# add reference genome paths here. Note environment variables will also work as values\n"
               "[genomes]\n"
               "t2t=https://s3-us-west-2.amazonaws.com/human-pangenomics/T2T/CHM13/assemblies/chm13v2.0.fa\n"
               "hg38=http://ftp.1000genomes.ebi.ac.uk/vol1/ftp/technical/reference/GRCh38_reference_genome/GRCh38_full_analysis_set_plus_decoy_hla.fa\n"
               "hg19=/Users/sbi8kc2/Data/db/hg19/ucsc.hg19.fa\n"
               "pantro6=https://s3.amazonaws.com/igv.org.genomes/panTro6/panTro6.fa\n"
               "mm10=https://s3.amazonaws.com/igv.broadinstitute.org/genomes/seq/mm10/mm10.fa\n"
               "\n"
               "[tracks]\n"
               "# add comma separated list of bed/gtf files to add to each reference genome listed above:\n"
               "# hg38=/home/hg38.refseq_genes.gtf.gz,/home/hg38.repeats.bed.gz\n"
               "\n"
               "[general]\n"
               "theme=dark\n"
               "dimensions=1366x768\n"
               "indel_length=10\n"
               "ylim=50\n"
               "coverage=true\n"
               "log2_cov=false\n"
               "link=none\n"
               "split_view_size=10000\n"
               "threads=4\n"
               "pad=500\n"
               "scroll_speed=0.15\n"
               "tabix_track_height=0.05\n"
               "font=Menlo\n"
               "font_size=14\n"
               "\n"
               "[view_thresholds]\n"
               "soft_clip=20000\n"
               "small_indel=100000\n"
               "snp=500000\n"
               "edge_highlights=100000\n"
               "\n"
               "[navigation]\n"
               "scroll_right=RIGHT\n"
               "scroll_left=LEFT\n"
               "zoom_out=DOWN\n"
               "zoom_in=UP\n"
               "scroll_down=PAGE_DOWN\n"
               "scroll_up=PAGE_UP\n"
               "\n"
               "[interaction]\n"
               "cycle_link_mode=L\n"
               "print_screen=PRINT_SCREEN\n"
               "find_alignments=F\n"
               "\n"
               "[labelling]\n"
               "number=3x3\n"
               "parse_label=filter\n"
               "labels=PASS,FAIL\n"
               "delete_labels=DELETE\n"
               "enter_interactive_mode=ENTER";
    }
}

#endif //GW_DEFAULTINI_H
