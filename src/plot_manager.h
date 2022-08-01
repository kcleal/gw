//
// Created by Kez Cleal on 25/07/2022.
//

#pragma once

#include <string>
#include <vector>

#include "themes.h"


namespace Manager {


    /*
     * Deals with plotting images to screen or file
     */
    class GwPlot {
    public:
        GwPlot(const char* reference, std::vector<std::string>& bams, unsigned int threads, Themes::BaseTheme& theme);

        ~GwPlot() = default;

        bool init;
        const char* reference_str;
        std::vector<std::string> bam_paths;
        std::vector<htsFile* > bams;
        unsigned int threads;
        Themes::BaseTheme theme;



    };
}