//
// Created by kez on 14/02/25.
//
#include <fstream>
#include <string>
#include <string_view>
#include <charconv>
#include <optional>
#include <vector>
#include <stdexcept>
#include <memory>
#include <iostream>
#include <queue>
#include <set>
#include <algorithm>

#include "htslib/faidx.h"
#include "htslib/hfile.h"
#include "htslib/hts.h"
#include "htslib/vcf.h"
#include "htslib/sam.h"
#include "htslib/tbx.h"

#include "../include/export_definitions.h"
#include "../include/ankerl_unordered_dense.h"
#include "../include/superintervals.h"
#include "alignment_format.h"
#include "utils.h"


namespace AlignFormat {

    enum Matcher {
        STRAND,
        CHROM,
        START,
        END
    };

    void parseSegments(std::string &line, GAF_t *g, size_t ps, size_t pe,
                       std::vector<GAF_t*> &other_segments) {
//        std::cout << "\nparseSegments\n";
        /*
        Parse the path string e.g.:
        >chr1:0-10794>HG02572#2#JAHAOV010000291.1:5593-5651>chr1:10794-59600
        */
        GAF_t* cur = g;
        if (line[ps] != '>' && line[ps] != '<') {
            // No strand information, assume forward strand
            g->chrom = line.substr(ps, pe - ps);
            g->pos = g->path_start;
            g->end = g->path_end;
            return;
        }

        Matcher match = STRAND;
        size_t len;
        size_t j;
        for (size_t i=ps; i < pe; ++i) {
            // Break this path into alignment blocks on the reference genome
            switch (match) {
                case STRAND:
                    if (line[i] == '>' || line[i] == '<') {
                        if (i != ps) {
                            cur = cur->duplicate();
                            other_segments.push_back(cur);
                        }
                        cur->flag = (line[i] == '>') ? 0 : 16;
                        match = CHROM;
                    } else {  // No strand information, assume forward strand. Take whole string as chrom
                        cur->chrom = line.substr(i, pe - i);
                        cur->seg_start = cur->path_start;
                        cur->seg_end = cur->path_end;
                        i = pe;
                    }
                    break;
                case CHROM:
                    j = line.find(':', i);
                    if (j != std::string::npos) {
                        len = j - i;
                        cur->chrom = line.substr(i, len);
                    } else {
                        // This only occurs when one segment is in the path. Use the original pos and end
                        cur->chrom = line.substr(i, pe - i);  // If ':' not found, take the remaining part
                        cur->seg_start = g->pos;
                        cur->seg_end = g->end;
                        break;
                    }
                    match = START;
                    i += len;
                    break;
                case START:
                    j = line.find("-", i);
                    std::from_chars(line.data() + i, line.data() + j, cur->seg_start);
                    match = END;
                    len = j - i;
                    i += len;
                    break;
                case END:
                    j = line.find_first_of("><", i);
                    std::from_chars(line.data() + i, line.data() + j, cur->seg_end);
                    if (j == std::string::npos) {  // todo this needed?
                        i = pe;
                        break;
                    }
                    match = STRAND;
                    len = j - i - 1;
                    i += len;
                    break;
            }
        }
        // Set middle segments pos and end to the segment locations. Ignore first and last segments
        size_t offset = g->path_start; // Track position in the concatenated path
        g->pos = g->seg_start + offset;
        g->end = g->seg_end;
        int remaining = g->alignment_block_length - (g->end - g->pos);
//        std::cout << "block length=" << g->alignment_block_length << std::endl;
//        std::cout << " remaining after first block=" << remaining << std::endl;
//        std::cout << "seg_start,seg_end=" << g->seg_start << " " << g->seg_end << std::endl;
//        std::cout << "path_start,seg_start=" << g->path_start  << " " << g->seg_start << std::endl;
//        std::cout << "offset=" << offset << " " << g->chrom << ":" << g->pos << "-" << g->end << std::endl;
        for (size_t i=0; i < other_segments.size(); ++i) {
            other_segments[i]->pos = other_segments[i]->seg_start;
            if (i < other_segments.size() - 1) {
                other_segments[i]->end = other_segments[i]->seg_end;
                remaining -= (other_segments[i]->end - other_segments[i]->pos);
//                std::cout << " remaining after block=" << remaining << std::endl;
            } else {
                other_segments[i]->end = other_segments[i]->pos + remaining;
            }
//            std::cout << other_segments[i]->chrom << ":" << other_segments[i]->pos << "-" << other_segments[i]->end << std::endl;
        }
//        std::exit(0);

    }

    void extractAlignmentPath(std::string &line, GAF_t *g, size_t ps, size_t pe,
                              std::vector<GAF_t*> &other_segments, size_t cigar_start, size_t cigar_end
                              ) {


        bool have_cigar = cigar_start != std::string::npos && cigar_start < cigar_end;
        if (!have_cigar) {
            g->pos = g->path_start;
            g->end = g->path_end;
            g->blocks.emplace_back() = {(uint32_t)g->pos, (uint32_t)g->end};
            return;
        }
        GAF_t* cur = g;

//        std::cout << "extractPath\n";
        parseSegments(line, g, ps, pe, other_segments);

        // Now parse cigar
        cur = g;
        size_t next_segment = 0;
        uint32_t num = 0;
        uint32_t current_pos = g->pos;  // First block position
        uint32_t block_start = current_pos;
        bool in_match = false;

        auto flush_block = [&]() {
            if (in_match && cur) {
                // Ensure block doesn't extend beyond segment boundary
                uint32_t end_pos = (current_pos < (uint32_t)cur->seg_end) ?
                                   std::min(current_pos, (uint32_t)cur->seg_end) : (uint32_t)cur->end;
                if (block_start < end_pos) {
                    cur->blocks.emplace_back() = {block_start, end_pos};
                }
                in_match = false;
            }
        };

        auto advance_to_next_segment = [&]() -> bool {
            if (next_segment < other_segments.size()) {
                cur = other_segments[next_segment];
                next_segment++;
                current_pos = cur->seg_start;
                block_start = current_pos;
                in_match = false;
                return true;
            }
            return false;
        };

        for (size_t i = cigar_start; i < cigar_end && cur != nullptr; ++i) {
            if (std::isdigit(line[i])) {
                num = num * 10 + (line[i] - '0');
                continue;
            }

            switch (line[i]) {
                case '=':
                case 'X':
                case 'M': {
                    uint32_t remaining = num;
                    while (remaining > 0 && cur != nullptr) {
                        // Calculate how much of the operation fits in current segment
                        uint32_t segment_remaining = (current_pos < (uint32_t)cur->seg_end) ?
                                                     ((uint32_t)cur->seg_end - current_pos) : 0;

                        // Protect against overflow in advance calculation
                        uint32_t max_advance = UINT32_MAX - current_pos;
                        uint32_t advance = std::min({remaining, segment_remaining, max_advance});

                        if (!in_match) {
                            block_start = current_pos;
                            in_match = true;
                        }

                        current_pos += advance;
                        remaining -= advance;

                        // If we've reached the end of the segment
                        if (current_pos >= (uint32_t)cur->seg_end) {
                            flush_block();
                            if (!advance_to_next_segment()) {
                                break;  // No more segments
                            }
                        }
                    }
                    break;
                }

                case 'D':
                case 'N': {
                    flush_block();
                    uint32_t remaining = num;
                    while (remaining > 0 && cur != nullptr) {
                        uint32_t segment_remaining = (current_pos < (uint32_t)cur->seg_end) ?
                                                     ((uint32_t)cur->seg_end - current_pos) : 0;

                        uint32_t max_advance = UINT32_MAX - current_pos;
                        uint32_t advance = std::min({remaining, segment_remaining, max_advance});

                        current_pos += advance;
                        remaining -= advance;

                        if (current_pos >= (uint32_t)cur->seg_end) {
                            if (!advance_to_next_segment()) {
                                break;  // No more segments
                            }
                        }
                    }
                    block_start = current_pos;
                    break;
                }

                case 'I':
                    break;

                default:
                    break;
            }
            num = 0;
        }

        flush_block();


//        std::cout << "g is " << g->pos << " - " << g->end << std::endl;
//        for (auto item : g->blocks) {
//            std::cout << item.start << "-" << item.end << " ";
//        } std::cout << std::endl;
//        for (auto gg : other_segments) {
//            std::cout << "gg is " << gg->pos << " - " << gg->end << std::endl;
//            for (auto item : gg->blocks) {
//                std::cout << item.start << "-" << item.end << " ";
//            } std::cout << std::endl;
//        }

    }

    // GAF format example
// 0 - m21009_241011_231051/37098846/ccs
// 1 - 21316
// 2 - 3435
// 3 - 20930
// 4 - +
// 5 - >chr1:0-10794>HG02572#2#JAHAOV010000291.1:5593-5651>chr1:10794-59600
// 6 - 59658
// 7 - 10455
// 8 - 27958
// 9 - 17359
// 10 - 17531
// 11 - 2
// 12 - tp:A:P  NM:i:172        cm:i:2143       s1:i:16392      s2:i:16291      dv:f:0.0165
// cg:Z:36=1X129=2X2=8I1=1X228=2I19=2I4=1X11=1X29D21=1X4=1X7=1X29=1X3=1X2=1X1=1X32=1I33=1X14=1X1=1X15=1X28=1I11=1X3=1X1I33=1X20=1X19=1X12=1X223=1X17=1X47=1X50=1X33=1X22=1X206=1X400=1D24=1X184=1X11=1I388=1X211=1D120=1X1=1X183=1X24=1X329=2D237=1X61=1D21=1X365=1D109=5I217=1X25=1X42=1I165=1X22=1X187=1X92=1X62=1X542=1X2=1X85=1X1I15=1X33=1X111=1X34=1X194=1X79=1X155=1X36=1X74=1X160=1X1=1X46=1X1307=1X684=1X322=1X68=1X534=1X351=1X92=1X5=1X796=1X89=1D465=1X577=1X23=1X586=1I302=1X83=1X18=1X25=1X102=1I56=1X35=1X312=1X1=1X170=1X93=1X156=1X136=1X453=1X108=1X67=1X11=1X3=1X17=1X4=1X1=1X15=1X347=1I8=1X10=2X21=1X93=1I292=1X142=1X107=1X281=1X444=1X284=1X57=1X77=1X309=1X166=1I267=1X24=
// ds:Z::36*ct:129*tg*tc:2+[ag]gcgcag:1*ag:228+[ag]:19+ca:4*ta:11*ga-[caggcgcagaga]ggcgcgccgcgcc[ggcg]:21*cg:4*tc:7*ta:29*ag:3*ct:2*ct:1*gc:32+[g]:33*ac:14*gt:1*tg:15*ga:28+[g]:11*ag:3*tg+[g]:33*tc:20*ag:19*ac:12*ca:223*ta:17*ag:47*cg:50*ag:33*at:22*gt:206*ga:400-[g]:24*gc:184*ga:11+[g]:388*ga:211-[c]:120*tg:1*ag:183*ct:24*gc:329-ag:237*ca:61-[c]:21*tc:365-[c]:109+[aaga]g:217*gc:25*cg:42+[g]:165*ag:22*ag:187*ag:92*tg:62*at:542*gt:2*gt:85*ac+[g]:15*ag:33*ga:111*tc:34*tg:194*ct:79*tc:155*ct:36*ga:74*ct:160*gc:1*cg:46*ag:1307*tc:684*cg:322*ag:68*ct:534*ag:351*ga:92*gc:5*tc:796*ct:89-[c]:465*ct:577*ag:23*tc:586+[g]:302*tc:83*ga:18*gt:25*ac:102+[g]:56*cg:35*ag:312*at:1*gc:170*ca:93*ga:156*ct:136*ct:453*ag:108*ag:67*cg:11*ga:3*gc:17*gt:4*gc:1*ca:15*cg:347+[g]:8*ag:10*cg*cg:21*ac:93+[a]:292*ag:142*ct:107*tc:281*ct:444*ga:284*ag:57*ga:77*ga:309*ga:166+[t]:267*ta:24

    void gafParser(std::string& line,
                   ankerl::unordered_dense::map< std::string, SuperIntervals<int, GAF_t *>>& cached_alignments) {
        GAF_t *g = new GAF_t();

        size_t pos = 0;
        size_t next;
        size_t ps, pe;
//        int path_start, path_end, path_length;

        auto parse_int = [&](int &v) -> void {
            next = line.find('\t', pos);
            std::from_chars(line.data() + pos, line.data() + next, v);
            pos = next + 1;
        };

        next = line.find('\t', pos);
//        std::string qname = line.substr(0, next);
//        if (qname != "m21009_241011_231051/263063172/ccs") {
//            return;
//        }
//        std::cout << "gaf parse\n";
//        std::cout << line << std::endl;
        pos = next + 1;

        parse_int(g->qlen);
        parse_int(g->qstart);
        parse_int(g->qend);

        //next = line.find('\t', pos);
        g->strand = line[pos];
        pos += 2;

        //Path matching /([><][^\s><]+(:\d+-\d+)?)+|([^\s><]+)/
        ps = pos;
        next = line.find('\t', pos);
        pe = next;
        pos = next + 1;

        //         std::string qname, chrom;
        //        int qlen, qstart, qend, path_length, path_start, path_end, matches, alignment_block_length, qual;
        //        int pos{0}, end{0}, flag{0}, y{-1};
        //        char strand;  // this is strand of the query, not reference

        parse_int(g->path_length);
        parse_int(g->path_start);
        parse_int(g->path_end);
        parse_int(g->matches);
        parse_int(g->alignment_block_length);
        parse_int(g->qual);

        size_t cigar_start = line.find("cg:Z:");
        size_t cigar_end = std::string::npos;
        if (cigar_start != std::string::npos) {
            cigar_start += 5;  // ignor cg:Z: prefix
            cigar_end = line.find('\t', cigar_start);
        }

        std::vector<GAF_t*> other_segments;
        extractAlignmentPath(line, g, ps, pe, other_segments, cigar_start, cigar_end);

        std::cout << g->chrom << ":" << g->pos << "-" << g->end << std::endl;
        cached_alignments[g->chrom].add(g->pos, g->end, g);

        for (auto & v : other_segments) {
            cached_alignments[v->chrom].add(v->pos, v->end, g);
            std::cout << " " << v->chrom << ":" << v->pos << "-" << v->end << std::endl;
        }

        if (g->end == 0 || g->chrom.empty()) {


            // Debug:
        std::cout << line << std::endl;
        auto print_blocks = [](auto& gg){
            if (gg->blocks.empty()) {
                std::cout << " NO CIGAR \n";
            }
            for (auto v : gg->blocks) {
                std::cout << "(" << v.start << "," << v.end << ") ";
            } std::cout << std::endl;
        };
        std::cout << "=========================\n";
        std::cout << "Path start: " << g->chrom << ":" << g->pos << "-" << g->end << " ";
        print_blocks(g);

        for (size_t i = 0; i < other_segments.size(); ++i) {
            std::cout << "Seg " << other_segments[i]->chrom << ":" << other_segments[i]->pos << "-" << other_segments[i]->end << " ";
            print_blocks(other_segments[i]);
        }
        std::cout << std::endl;

        std::cout << g->flag << " " << g->chrom << " " << g->pos << " " << g->end << std::endl;

        std::exit(0);
        }
    }

    struct TrackRange {
        int start, end;
    };

    void gafFindY(std::vector<AlignFormat::GAF_t *>& gafAlignments) {
        if (gafAlignments.empty()) return;

        // Min-heap to track active segments
        std::priority_queue<
        std::pair<int, int>,  // (end, Y level)
                std::vector<std::pair<int, int>>,
                std::greater<std::pair<int, int>>
                > active_ends;

        // Track next available Y level and the maximum Y level used
        int next_y = 0;
        int max_y_used = -1;

        // Array to track which Y levels are in use
        std::vector<bool> y_in_use;
        constexpr int pad = 5;

        for (auto* align : gafAlignments) {
            // Clear ended alignments and mark their Y levels as available
            while (!active_ends.empty() && active_ends.top().first < align->pos) {
                int freed_y = active_ends.top().second;
                y_in_use[freed_y] = false;
                if (freed_y < next_y) next_y = freed_y;
                active_ends.pop();
            }

            // Find the next available Y level, linear probing
            while (next_y < (int)y_in_use.size() && y_in_use[next_y]) {
                next_y++;
            }

            // If we need a new Y level
            if (next_y >= (int)y_in_use.size()) {
                y_in_use.push_back(false);
                max_y_used = next_y;
            }

            // Assign Y level
            align->y = next_y;
            y_in_use[next_y] = true;
            active_ends.push({align->end + pad, next_y});

            if (next_y == max_y_used) {
                next_y++;
            } else {
                while (next_y < (int)y_in_use.size() && y_in_use[next_y]) {
                    next_y++;
                }
            }
        }
    }

    void AlignFormat::GwAlignment::open(const std::string& file_path, const std::string& reference, int threads) {
        path = file_path;
        if (Utils::endsWith(path, "bam") || Utils::endsWith(path, "cram")) {
            type = AlignmentType::HTSLIB_t;
            bam = sam_open(path.c_str(), "r");
            if (!bam) {
                throw std::runtime_error("Failed to open BAM file: " + path);
            }
            hts_set_fai_filename(bam, reference.c_str());
            hts_set_threads(bam, threads);
            header = sam_hdr_read(bam);
            index = sam_index_load(bam, path.c_str());
        } else if (Utils::endsWith(path, "gaf")) {
            type = AlignmentType::GAF_t;

            std::unique_ptr<std::istream> fpu;
#if !defined(__EMSCRIPTEN__)

            if (Utils::startsWith(path, "http") || Utils::startsWith(path, "ftp")) {
                std::string content = Utils::fetchOnlineFileContent(path);
                fpu = std::make_unique<std::istringstream>(content);
            } else {
                auto file_stream = std::make_unique<std::ifstream>(path);
                if (!file_stream->is_open()) {
                    std::cerr << "Error: opening track file " << path << std::endl;
                    throw std::runtime_error("Error opening file");
                }
                fpu = std::move(file_stream);
            }

#else
            fpu = std::make_unique<std::ifstream>();
            fpu->open(p);
            if (!fpu->is_open()) {
                std::cerr << "Error: opening track file " << path << std::endl;
                throw std::exception();
            }
#endif

            std::string tp;
            int count  = 0;
            while (true) {
                if (!(bool)getline(*fpu, tp)) {
                    break;
                }
                if (tp[0] == '#') {
                    continue;
                }
                gafParser(tp, this->cached_alignments);
                if (count > 10000) {
                    break;
                }
//                if (count % 1000 == 0) {
//                    std::cout << count << std::endl;
//                }
                count +=  1;
            }
//            std::cout << " Finding Y " << std::endl;
            for (auto& item : cached_alignments) {
                item.second.startSorted = false;
                item.second.index();
//                gafFindY(item.second.data);
            }
//            std::cout << " Done finding Y\n";
        }
    }

    AlignFormat::GwAlignment::~GwAlignment() {
        if (index) hts_idx_destroy(index);
        if (header) sam_hdr_destroy(header);
        if (bam) sam_close(bam);
        if (!cached_alignments.empty()) {
            for (auto& item : cached_alignments) {
                for (auto &g : item.second.data) {
                    delete g;
                }
            }
        }
    }

    void gafToAlign(AlignFormat::GAF_t* gaf, AlignFormat::Align* align) {

        align->cov_start = gaf->pos;
        align->cov_end = gaf->end;
        align->orient_pattern = AlignFormat::Pattern::NORMAL;
        align->left_soft_clip = 0;
        align->right_soft_clip = 0;
        align->y = gaf->y;
        align->edge_type = 1;
        align->pos = gaf->pos;
        align->reference_end = gaf->end;
        align->has_SA = false;
        align->blocks = gaf->blocks;
//        std::cout << gaf->pos << " " << gaf->end << " " << gaf->y << std::endl;
        align->delegate = bam_init1();
        const char* qname = gaf->qname.c_str();
        size_t l_qname = gaf->qname.size();
        // int bam_set1(bam1_t *bam,
        //             size_t l_qname, const char *qname,
        //             uint16_t flag, int32_t tid, hts_pos_t pos, uint8_t mapq,
        //             size_t n_cigar, const uint32_t *cigar,
        //             int32_t mtid, hts_pos_t mpos, hts_pos_t isize,
        //             size_t l_seq, const char *seq, const char *qual,
        //             size_t l_aux);
        int res = bam_set1(align->delegate, l_qname, qname, (uint64_t)gaf->flag, 0,
                     (hts_pos_t)align->pos, (uint8_t)gaf->qual, 0, nullptr, 0, 0, 0, 0, nullptr, nullptr, 0);
        if (res < 0) {
            return;
        }
    }

}