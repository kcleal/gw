//
// Created by Kez Cleal on 25/07/2022.
//

#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include "../include/unordered_dense.h"
#include "../include/robin_hood.h"

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

    // https://stackoverflow.com/questions/1528298/get-path-of-executable
    std::string getExecutableDir();

    bool is_file_exist(std::string FileName);

    struct TrackBlock {
        std::string chrom, name, line, vartype;
        int start, end;
        int strand;  // 0 is none, 1 forward, 2 reverse
        std::vector<int> s;  // block starts and block ends for bed12
        std::vector<int> e;
    };

    struct Region {
        std::string chrom;
        int start, end;
        int markerPos, markerPosEnd;
        const char *refSeq;
        Region() {
            chrom = "";
            start = -1;
            end = -1;
            refSeq = nullptr;
        }
    };

    Region parseRegion(std::string &r);

    struct Dims {
        int x, y;
    };

    Dims parseDimensions(std::string &s);

    int intervalOverlap(int start1, int end1, int start2, int end2);

    bool isOverlapping(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2);

    struct BoundingBox {
        float xStart, yStart, xEnd, yEnd, width, height;
    };

    std::vector<BoundingBox> imageBoundingBoxes(Dims &dims, float wndowWidth, float windowHeight, float padX=15, float padY=15);

    class Label {
    public:
        Label() = default;
        ~Label() = default;
        std::string chrom, variantId, savedDate, vartype;
        std::vector<std::string> labels;
        int i, pos;
        bool clicked;
        bool mouseOver;

        void next();
        std::string& current();
    };

    std::string dateTime();

    Label makeLabel(std::string &chrom, int pos, std::string &parsed, std::vector<std::string> &inputLabels, std::string &variantId, std::string &vartype,
                    std::string savedDate, bool clicked);

    void labelToFile(std::ofstream &f, Utils::Label &l, std::string &dateStr);

    void saveLabels(std::vector<Utils::Label> &multiLabels, std::string path);

    void openLabels(std::string path, ankerl::unordered_dense::map< std::string, Utils::Label> &label_dict,
                    std::vector<std::string> &inputLabels, robin_hood::unordered_set<std::string> &seenLabels);

    std::string getSize(long num);

    void parseMateLocation(std::string &selectedAlign, std::string &mate, std::string &target_qname);

    int get_terminal_width();	

	void ltrim(std::string &s);
	void rtrim(std::string &s);
	void trim(std::string &s);
}
