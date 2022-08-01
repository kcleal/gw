//
// Created by Kez Cleal on 25/07/2022.
//

#pragma once

#include <string>


namespace Utils {
    // https://stackoverflow.com/questions/1528298/get-path-of-executable
    std::string getExecutableDir();

    bool is_file_exist( std::string FileName );

    struct Region {
        std::string chrom;
        int start, end;
        Region() {
            chrom = "";
            start = -1;
            end = -1;
        }
    };

    Region parseRegion(std::string &r);

    struct Dims {
        unsigned int x, y;
    };

    Dims parseDimensions(std::string &s);
}

