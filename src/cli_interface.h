//
// Created by Kez Cleal on 09/02/2025.
//
#pragma once

#include <cassert>
#include <algorithm>
#include <filesystem>
#include <htslib/faidx.h>
#include <iostream>
#include <mutex>
#include <string>
#include "argparse.h"
#include "BS_thread_pool.h"
#include "glob_cpp.hpp"
#include "hts_funcs.h"
#include "plot_manager.h"
#include "themes.h"
#include "utils.h"

#include "termcolor.h"
#include "GLFW/glfw3.h"


namespace CLIInterface {

    void print_gw_banner();

    struct CLIOptions {
        argparse::ArgumentParser program;
        std::string genome;
        std::vector<std::string> bamPaths;
        std::vector<std::string> tracks;
        std::vector<Utils::Region> regions;
        std::vector<std::string> filters;
        std::vector<std::string> extra_commands;
        bool showBanner;
        bool useSession;
        std::string outdir;

    };

    void parseArguments(int argc, char* argv[], Themes::IniOptions& iopts, CLIOptions& options);

}
