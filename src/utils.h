//
// Created by Kez Cleal on 25/07/2022.
//

#pragma once

#include <string>
#include <vector>


namespace Utils {

    enum GwFileTypes {
        VCF,
        BED,
        BEDPE,
        None
    };

    bool endsWith(const std::string &mainStr, const std::string &toMatch);

    bool startsWith(const std::string &mainStr, const std::string &toMatch);

    std::vector<std::string> split(const std::string &s, char delim);

    // https://stackoverflow.com/questions/1528298/get-path-of-executable
    std::string getExecutableDir();

    bool is_file_exist(std::string FileName);

    struct Region {
        std::string chrom;
        long start, end;
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

    std::vector<BoundingBox> imageBoundingBoxes(Dims &dims, float wndowWidth, float windowHeight, float padX=5, float padY=5);

    class Label {
    public:
        Label(std::string &parsed, std::vector<std::string> &labels, std::string &variantId);
        ~Label() = default;
        std::string variantId;
        std::vector<std::string> labels;
        int i;
        bool clicked;

        void next();
        std::string& current();
    };

}