
#include <htslib/faidx.h>
#include <htslib/hfile.h>
#include <htslib/sam.h>

#include <string>
#include <vector>

#include "plot_manager.h"
#include "themes.h"

namespace Manager {

    GwPlot::GwPlot (const char* reference, std::vector<std::string>& bam_paths, unsigned int threads, Themes::BaseTheme& theme) {

        this->reference_str = reference;
        this->bam_paths = bam_paths;
        this->threads = threads;
        this->theme = theme;


        htsFile* f;
        for (auto& fn: this->bam_paths) {
            f = sam_open(fn.c_str(), "r");
            bams.push_back(f);
        }

    }

}

