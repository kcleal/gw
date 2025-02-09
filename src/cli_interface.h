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


struct CLIOptions {
    argparse::ArgumentParser program;
    std::string genome;
    std::vector<std::string> bamPaths;
    std::vector<std::string> tracks;
    std::vector<Utils::Region> regions;
    std::vector<std::string> filters;
    std::vector<std::string> extra_commands;
    bool noShow;
    bool showBanner;
    bool useSession;
    std::string outdir;

};


class CLIInterface {
    public:
    static CLIOptions parseArguments(int argc, char* argv[], Themes::IniOptions& iopts);

    private:
    static void setupArgumentParser(argparse::ArgumentParser& program);
    static void validateArguments(const argparse::ArgumentParser& program);
    static void handleGenomeSelection(CLIOptions& options,
    argparse::ArgumentParser& program,
    Themes::IniOptions& iopts);
    static void processBamPaths(CLIOptions& options,
    const argparse::ArgumentParser& program);
    // ... other helper methods
};


void print_gw_banner();