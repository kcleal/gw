//
// Created by Kez Cleal on 25/07/2022.
//
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <string>

#include "utils.h"

#if defined(_WIN32)
    #include <windows.h>
    #include <Shlwapi.h>
    #include <io.h>

    #define access _access_s
#endif

#ifdef __APPLE__
    #include <libgen.h>
    #include <limits.h>
    #include <mach-o/dyld.h>
    #include <unistd.h>
#endif

#ifdef __linux__
#include <limits.h>
    #include <libgen.h>
    #include <unistd.h>

    #if defined(__sun)
        #define PROC_SELF_EXE "/proc/self/path/a.out"
    #else
        #define PROC_SELF_EXE "/proc/self/exe"
    #endif

#endif

namespace Utils {

    #if defined(_WIN32)
        std::string getExecutablePath() {
            char rawPathName[MAX_PATH];
            GetModuleFileNameA(NULL, rawPathName, MAX_PATH);
            return std::string(rawPathName);
        }

        std::string getExecutableDir() {
            std::string executablePath = getExecutablePath();
            char* exePath = new char[executablePath.length()];
            strcpy(exePath, executablePath.c_str());
            PathRemoveFileSpecA(exePath);
            std::string directory = std::string(exePath);
            delete[] exePath;
            return directory;
        }
    #endif

    #ifdef __linux__
        std::string getExecutablePath() {
           char rawPathName[PATH_MAX];
           realpath(PROC_SELF_EXE, rawPathName);
           return  std::string(rawPathName);
        }

        std::string getExecutableDir() {
            std::string executablePath = getExecutablePath();
            char *executablePathStr = new char[executablePath.length() + 1];
            strcpy(executablePathStr, executablePath.c_str());
            char* executableDir = dirname(executablePathStr);
            delete [] executablePathStr;
            return std::string(executableDir);
        }
    #endif

    #ifdef __APPLE__
        std::string getExecutablePath() {
            char rawPathName[PATH_MAX];
            char realPathName[PATH_MAX];
            uint32_t rawPathSize = (uint32_t) sizeof(rawPathName);

            if (!_NSGetExecutablePath(rawPathName, &rawPathSize)) {
                realpath(rawPathName, realPathName);
            }
            return std::string(realPathName);
        }

        std::string getExecutableDir() {
            std::string executablePath = getExecutablePath();
            char *executablePathStr = new char[executablePath.length() + 1];
            strcpy(executablePathStr, executablePath.c_str());
            char *executableDir = dirname(executablePathStr);
            delete[] executablePathStr;
            return std::string(executableDir);
        }
    #endif

    bool is_file_exist( std::string FileName ) {
        const std::filesystem::path p = FileName ;
        return ( std::filesystem::exists(p) );
    }

    void strToRegion(Region *r, std::string& s, const char delim){
        unsigned int start = 0;
        unsigned int end = s.find(delim);
        r->chrom = s.substr(start, end - start);
        start = end + 1;
        end = s.find(delim, start);
        r->start = std::stoi(s.substr(start, end - start));
        start = end + 1;
        end = s.find(delim, start);
        r->end = std::stoi(s.substr(start, end - start));
    }

    Region parseRegion(std::string& s) {
        Region reg;
        std::string s2;
        if (s.find(":") != std::string::npos) {
            s.erase(std::remove(s.begin(), s.end(), ','), s.end());
            std::replace(s.begin(), s.end(), '-', ':');
            Utils::strToRegion(&reg, s, ':');
        } else if (s.find(",") != std::string::npos) {
            Utils::strToRegion(&reg, s, ',');
        } else if (s.find("\t") != std::string::npos) {
            Utils::strToRegion(&reg, s, '\t');
        } else if (s.find("_") != std::string::npos) {
            Utils::strToRegion(&reg, s, '_');
        } else if (s.find(" ") != std::string::npos) {
            Utils::strToRegion(&reg, s, ' ');
        }
        if (reg.chrom.length() == 0 || reg.start == -1 || reg.end == -1) {
            std::cerr << "Error: unable to parse region";
            std::abort();
        }
        return reg;
    }

    Dims parseDimensions(std::string &s) {
        Dims d = {0, 0};
        unsigned int start = 0;
        unsigned int end = s.find('x');
        d.x = std::stoi(s.substr(start, end - start));
        start = end + 1;
        end = s.find('x', start);
        d.y = std::stoi(s.substr(start, end - start));
        if (d.x == 0) {
            std::cerr << "Error dimension x was 0" << std::endl;
            std::abort();
        }
        return d;
    }

    int intervalOverlap(int start1, int end1, int start2, int end2) {
        return std::max(0, std::min(end1, end2) - std::max(start1, start2));
    }

    bool isOverlapping(int start1, int end1, int start2, int end2) {
        return start1 <= end2 && start2 <= end1;
    }
}