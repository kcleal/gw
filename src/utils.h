//
// Created by Kez Cleal on 25/07/2022.
//

#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include "ankerl_unordered_dense.h"
#include "export_definitions.h"
#include "ideogram.h"
#include "htslib/faidx.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define VC_EXTRALEAN
#include <Windows.h>
#elif defined(__linux__)
#include <sys/ioctl.h>
#endif // Windows/Linux

namespace Utils {


    bool endsWith(const std::string &mainStr, const std::string &toMatch);

    bool startsWith(const std::string &mainStr, const std::string &toMatch);

    std::vector<std::string> split(const std::string &s, char delim);

    std::vector<std::string> split(const std::string &s, char delim, std::vector<std::string> &elems);

    std::vector<std::string> split_keep_empty_str(const std::string &s, const char delim);

    // https://stackoverflow.com/questions/1528298/get-path-of-executable
    std::string getExecutableDir();

    bool is_file_exist(std::string FileName);

    class EXPORT TrackBlock {
    public:
        std::string chrom, name, line, vartype, parent;
        int start, end;
        int coding_start, coding_end;
        int strand;  // 0 is none, 1 forward, 2 reverse
        int level;
        float value;
        bool anyToDraw;
        std::vector<std::string> parts;
        std::vector<int> s;  // block starts and block ends for bed12/GFF
        std::vector<int> e;
        std::vector<uint8_t> drawThickness;  // 0 no line, 1 is thin line, 2 fat line
        TrackBlock() {
            coding_start = -1;
            coding_end = -1;
            value = 0;
            level = 0;
            start = 0;
            end = 0;
            strand = 0;
            anyToDraw = false;
        }
    };

    class EXPORT GFFTrackBlock {
    public:
        std::string chrom, name, line, vartype;
        std::vector<std::string> parts;
        int start, end;
        int strand;  // 0 is none, 1 forward, 2 reverse
    };

    class EXPORT Region {
    public:
        std::string chrom;
        int start, end;
        int markerPos, markerPosEnd;
        int chromLen;
        int refSeqLen;
        int regionLen;
        const char *refSeq;
        std::vector<uint8_t> refSeq_nibbled;
        std::vector<std::vector<Utils::TrackBlock>> featuresInView;  // one vector for each Track
        std::vector<int> featureLevels;
        Region() {
            chrom = "";
            start = -1;
            end = -1;
            markerPos = -1;
            markerPosEnd = -1;
            chromLen = 0;
            refSeq = nullptr;
            refSeqLen = 0;
        }
        std::string toString();
    };

    EXPORT Region parseRegion(std::string &r);

    bool parseFilenameToRegions(std::filesystem::path &path, std::vector<Region> &regions, faidx_t* fai, int pad, int split_size);

    struct FileNameInfo {
        std::string chrom, chrom2, rid, fileName, varType;
        int pos, pos2;
        bool valid;
    };

    std::filesystem::path makeFilenameFromRegions(std::vector<Utils::Region> &regions);

    FileNameInfo parseFilenameInfo(std::filesystem::path &path);

    struct EXPORT Dims {
        int x, y;
    };

    EXPORT_FUNCTION Dims parseDimensions(std::string &s);

    int intervalOverlap(int start1, int end1, int start2, int end2);

    bool isOverlapping(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2);

    struct BoundingBox {
        float xStart, yStart, xEnd, yEnd, width, height;
    };

    std::vector<BoundingBox> imageBoundingBoxes(Dims &dims, float wndowWidth, float windowHeight, float padX=15, float padY=15, float ySpace=0);

    class EXPORT Label {
    public:
        Label() = default;
        ~Label() = default;
        std::string chrom, variantId, savedDate, vartype, comment;
        std::vector<std::string> labels;
        int i, pos, ori_i;
        bool clicked;
        bool mouseOver;
        bool contains_parsed_label;

        void next();
        std::string& current();
    };

    std::string dateTime();

    Label makeLabel(std::string &chrom, int pos, std::string &parsed, std::vector<std::string> &inputLabels, std::string &variantId, std::string &vartype,
                    std::string savedDate, bool clicked, bool add_empty_label, std::string& comment);

    void labelToFile(std::ofstream &f, Utils::Label &l, std::string &dateStr, std::string &variantFileName);

    void saveLabels(Utils::Label &l, std::ofstream &fileOut, std::string &dateStr, std::string &variantFileName);

    void openLabels(std::string path, std::string &image_glob_path,
                    ankerl::unordered_dense::map< std::string, ankerl::unordered_dense::map< std::string, Utils::Label>> &label_dict,
                    std::vector<std::string> &inputLabels,
                    ankerl::unordered_dense::map< std::string, ankerl::unordered_dense::set<std::string>> &seenLabels
                    );

    std::string removeZeros(float value);
    std::string getSize(long num);

    void parseMateLocation(std::string &selectedAlign, std::string &mate, std::string &target_qname);

    int get_terminal_width();	

	void ltrim(std::string &s);
	void rtrim(std::string &s);
	void trim(std::string &s);

}
