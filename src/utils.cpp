//
// Created by Kez Cleal on 25/07/2022.
//
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <cmath>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <sstream>
#include <string>

#include "ankerl_unordered_dense.h"
#include "utils.h"

#include "htslib/faidx.h"

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    //#include <Shlwapi.h>
    #include <pathcch.h>
    #include <io.h>

    #define access _access_s
#endif

#ifdef __APPLE__
    #include <libgen.h>
    #include <limits.h>
    #include <mach-o/dyld.h>
    #include <unistd.h>
    #include <termios.h>
    #include <sys/ioctl.h>
//    #include <poll.h>
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

#if defined(_WIN32) || defined(_WIN64)
    std::string getExecutablePath() {
        char rawPathName[MAX_PATH];
        GetModuleFileNameA(NULL, rawPathName, MAX_PATH);
        return std::string(rawPathName);
    }

    std::string getExecutableDir() {
        std::string executablePath = getExecutablePath();
        std::string directory = std::filesystem::path(executablePath).parent_path().u8string();
        return directory;
    }
#endif

#ifdef __linux__

    std::string getExecutableDir() {
        auto pth = std::filesystem::canonical("/proc/self/exe").parent_path().string();
        return pth;
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

    bool is_file_exist(std::string FileName) {
        const std::filesystem::path p = FileName;
        return (std::filesystem::exists(p));
    }

    bool endsWith(const std::string &mainStr, const std::string &toMatch) {
        if (mainStr.size() >= toMatch.size() &&
            mainStr.compare(mainStr.size() - toMatch.size(), toMatch.size(), toMatch) == 0)
            return true;
        else
            return false;
    }

    bool startsWith(const std::string &mainStr, const std::string &toMatch) {
        if (mainStr.size() >= toMatch.size() && mainStr.compare(0, toMatch.size(), toMatch) == 0)
            return true;
        else
            return false;
    }

    template<typename Out>
    void split(const std::string &s, char delim, Out result) {
        std::istringstream iss(s);
        std::string item;
        while (std::getline(iss, item, delim)) {
            *result++ = item;
        }
    }

    std::vector<std::string> split(const std::string &s, char delim) {
        std::vector<std::string> elems;
        split(s, delim, std::back_inserter(elems));
        elems.erase(std::remove_if(elems.begin(), elems.end(),  // remove empty strings
                                   [&](std::string const &cmp) -> bool {
                                       return cmp == "";
                                   }), elems.end());
        return elems;
    }

    std::vector<std::string> split(const std::string &s, char delim, std::vector<std::string> &elems) {
        split(s, delim, std::back_inserter(elems));
        elems.erase(std::remove_if(elems.begin(), elems.end(),  // remove empty strings
                                   [&](std::string const &cmp) -> bool {
                                       return cmp == "";
                                   }), elems.end());
        return elems;
    }

    std::unique_ptr<std::vector<std::string>> split_keep_empty(const std::string &s, const char delim) {
        std::unique_ptr<std::vector<std::string>> elems = std::make_unique<std::vector<std::string>>();
        split(s, delim, std::back_inserter(*elems));
        return elems;
    }

    std::vector<std::string> split_keep_empty_str(const std::string &s, const char delim) {
        std::vector<std::string> elems;
        split(s, delim, std::back_inserter(elems));
        return elems;
    }

    void strToRegion(Region *r, std::string &s, const char delim) {
        size_t start = 0;
        size_t end = s.find(delim);
        r->chrom = s.substr(start, end - start);
        start = end + 1;
        end = s.find(delim, start);
        if (end != std::string::npos) {
            r->start = std::stoi(s.substr(start, end - start));
            start = end + 1;
            end = s.find(delim, start);
            r->end = std::stoi(s.substr(start, end - start));
            if (end != std::string::npos) {
                start = end + 1;
                end = s.find(delim, start);
                r->markerPos = std::stoi(s.substr(start, end - start));
                start = end + 1;
                end = s.find(delim, start);
                r->markerPosEnd = std::stoi(s.substr(start, end - start));
            }
        } else {
            r->start = std::stoi(s.substr(start, s.size()));
            r->end = r->start + 1;
        }
    }

    EXPORT Region parseRegion(std::string &s) {
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
        } else if (s.find(" ") != std::string::npos) {
            Utils::strToRegion(&reg, s, ' ');
        } else {
            reg.chrom = s;
            reg.start = 1;
            reg.end = 20000;
        }
        if (reg.chrom.length() == 0 || reg.start > reg.end) {
            throw std::runtime_error("Error: unable to parse region");
        }
        if (reg.start == reg.end) {
            reg.end += 1;
        }
        return reg;
    }

    std::string Region::toString() {
        return chrom + ":" + std::to_string(start) + "-" + std::to_string(end)
           + ((markerPos >= 0) ? ":" + std::to_string(markerPos) + ":" + std::to_string(markerPosEnd) : "");
    }

    std::filesystem::path makeFilenameFromRegions(std::vector<Utils::Region> &regions) {
        std::filesystem::path fname = "GW~";
        for (auto &rgn: regions) {
            fname += rgn.chrom + "~" + std::to_string(rgn.start) + "~" + std::to_string(rgn.end) + "~";
        }
        fname += ".png";
        return fname;
    }

    bool parseFilenameToRegions(std::filesystem::path &path, std::vector<Region> &regions, faidx_t* fai, int pad=500, int split_size=20000) {

#if defined(_WIN32) || defined(_WIN64)
        const wchar_t* pc = path.filename().c_str();
		std::wstring ws(pc);
        std::string fn(ws.begin(), ws.end());
#else
        std::string fn = path.filename();
#endif
        size_t l = fn.find_last_of('.');
        fn = fn.substr(0, l);
        if (fn.back() != '~') {  // variant image
            size_t p = fn.find('~');
            if (p == 0) {
                return false;
            }
            p += 1;
            std::vector<std::string> parts = split(fn.substr(p, fn.size()), '~');
            if (parts.size() == 5) {
                int start, end;
                try {
                    start = std::stoi(parts[1]);
                } catch (...) {
                    return false;
                }
                try {
                    end = std::stoi(parts[3]);
                } catch (...) {
                    return false;
                }
                if (parts[0] != parts[2] || std::abs(end - start) > split_size) {
                    regions.resize(2);
                    regions[0].chrom = parts[0];
                    regions[0].start = (start - pad > 0) ? start - pad : 1;
                    regions[0].end = start + pad;
                    regions[0].markerPos = start;
                    regions[0].markerPosEnd = end;
                    regions[1].chrom = parts[2];
                    regions[1].start = (end - pad > 0) ? end - pad : 1;
                    regions[1].end = end + pad;
                    regions[1].markerPos = end;
                    regions[1].markerPosEnd = start;
                } else {
                    regions.resize(1);
                    regions[0].chrom = parts[0];
                    regions[0].start = (start - pad > 0) ? start - pad : 1;
                    regions[0].end = end + pad;
                    regions[0].markerPos = start;
                    regions[0].markerPosEnd = end;
                }
            }
        } else {
            size_t p = fn.find("GW~");  // single image
            if (p != std::string::npos) {
                p += 3;
                std::vector<std::string> parts = split(fn.substr(p, fn.size()), '~');
                int i = 0;
                while (i < (int)parts.size()) {
                    Utils::Region N;
                    int start, end;
                    try {
                        start = std::stoi(parts[i+1]);
                    } catch (...) {
                        return false;
                    }
                    try {
                        end = std::stoi(parts[i+2]);
                    } catch (...) {
                        return false;
                    }
                    N.chrom = parts[i];
                    N.start = start;
                    N.end = end;
                    N.markerPos = -1;
                    N.markerPosEnd = -1;
                    regions.push_back(N);
                    i += 3;
                }
            }
        }
        if (regions.empty()) {
            return false;
        }
        for (auto &rgn: regions) {
            if (rgn.chrom.empty()) {
                return false;
            }
            if (fai != nullptr && faidx_has_seq(fai, rgn.chrom.c_str()) <= 0) {
                std::cerr << "Error: could not find " << rgn.chrom << " in fasta index\n";
                return false;
            }
        }
        return true;
    }

    // These are the name formats supported. If the filename has multiple regions encoded, then the first region is
    // returned in the results
    // 1. fname = varType + "~" + chrom + "~" + start + "~" + chrom2 + "~" + stop + "~" + rid + ".png";
    // 2. fname = "GW~" + chrom + "~" + start + "~" + end + "~.png";
    FileNameInfo parseFilenameInfo(std::filesystem::path &path) {
        FileNameInfo r;
        r.valid = false;
        r.pos = -1;
        r.pos2 = -1;
#if defined(_WIN32) || defined(_WIN64)
        const wchar_t* pc = path.filename().c_str();
		std::wstring ws(pc);
        std::string fn(ws.begin(), ws.end());
#else
        std::string fn = path.filename();
#endif
        r.fileName = fn;
        fn = fn.substr(0, fn.find_last_of('.'));
        std::vector<std::string> parts = split(fn, '~');
        if (parts.size() < 4) {
            return r;
        }
        int start, end;
        if (fn.back() == '~') {  // variant image, case 2.
            try {
                start = std::stoi(parts[2]);
                end = std::stoi(parts[3]);
            } catch (...) {
                return r;
            }
            r.valid = true;
            r.chrom = parts[1];
            r.pos = start;
            r.chrom2 = parts[1];
            r.pos2 = end;
        } else {
            try {
                start = std::stoi(parts[2]);
                end = std::stoi(parts[4]);
            } catch (...) {
                return r;
            }
            r.valid = true;
            r.chrom = parts[1];
            r.pos = start;
            r.chrom2 = parts[3];
            r.pos2 = end;
            r.varType = parts[0];
            r.rid = parts[5];
        }
        return r;
    }


    EXPORT Dims parseDimensions(std::string &s) {
        Dims d = {0, 0};
        int start = 0;
        int end = (int)s.find('x');
        if (end == (int)s.size()) {
            end = (int)s.find(',');
            if (end == (int)s.size()) {
                throw std::runtime_error("Error: 'x' or ',' not in dimensions");
            }
        }
        d.x = std::stoi(s.substr(start, end - start));
        start = end + 1;
        end = (int)s.find('x', start);
        if (end == (int)s.size()) {
            end = (int)s.find(',');
        }
        d.y = std::stoi(s.substr(start, end - start));
//        if (d.x == 0) {
//            throw std::runtime_error("Error: dimension x was 0");
//        }
        return d;
    }

    int intervalOverlap(int start1, int end1, int start2, int end2) {
        return std::max(0, std::min(end1, end2) - std::max(start1, start2));
    }

    bool isOverlapping(uint32_t start1, uint32_t end1, uint32_t start2, uint32_t end2) {
        return start1 <= end2 && start2 <= end1;
    }

    std::vector<BoundingBox>
    imageBoundingBoxes(Dims &dims, float windowWidth, float windowHeight, float padX, float padY, float ySpace) {
        float w = windowWidth / dims.x;
        float h = (windowHeight - ySpace) / dims.y;
        std::vector<BoundingBox> bboxes;
        bboxes.resize(dims.x * dims.y);
        int i = 0;
        for (int x = 0; x < dims.x; ++x) {
            for (int y = 0; y < dims.y; ++y) {
                BoundingBox &b = bboxes[i];
                b.xStart = (w * (float) x) + padX;
                b.yStart = (h * (float) y) + padY + ySpace;
                b.xEnd = b.xStart + w - padX;
                b.yEnd = b.yStart + h - padY;
                b.width = w - padX * 2;
                b.height = h - padY * 2;
                ++i;
            }
        }
        return bboxes;
    }

    Label makeLabel(std::string &chrom, int pos, std::string &parsed, std::vector<std::string> &inputLabels, std::string &variantId, std::string &vartype,
                    std::string savedDate, bool clicked, bool add_empty_label, std::string& comment) {
        Label l;
        l.chrom = chrom;
        l.pos = pos;
        l.variantId = variantId;
        l.vartype = vartype;
        l.savedDate = savedDate;
        l.comment = comment;
        l.i = 0;
        l.ori_i = 0;
        l.clicked = clicked;
        l.mouseOver = false;
        l.contains_parsed_label = false;
        l.labels.reserve(inputLabels.size() + (int)add_empty_label);
        if (add_empty_label) {
            l.labels.emplace_back("");
        }
        bool add_parsed = true;
        int i = 0;
        for (auto &v : inputLabels) {
            l.labels.push_back(v);
            if (v == parsed) {
                add_parsed = false;
                l.i = i;
                l.ori_i = i;
                l.contains_parsed_label = true;
            }
            i += 1;
        }
        if (add_parsed) {
            l.labels.push_back(parsed);
            l.i = (int)l.labels.size() - 1;
            l.ori_i = (int)l.labels.size() - 1;
            l.contains_parsed_label = true;
        }
        return l;
    }

    void Label::next() {
        savedDate = "";
        if (i == (int)labels.size() - 1) {
            i = 0;
        } else {
            i += 1;
        }
    }

    std::string & Label::current() {
        if (labels.empty()) {
            throw std::runtime_error("Label::current tried to use an empty label list");
        }
        return labels[i];
    }

    std::string dateTime() {
        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%d-%m-%Y_%H-%M-%S");
        auto str = oss.str();
        return str;
    }

    void labelToFile(std::ofstream &f, Utils::Label &l, std::string &dateStr, std::string &variantFileName) {
        f << l.chrom << "\t" << l.pos << "\t" << l.variantId << "\t" << l.current() << "\t" << l.vartype << "\t" <<
        (((l.contains_parsed_label && l.i == l.ori_i) || (!l.contains_parsed_label && l.i > 0)) ? l.savedDate : dateStr) <<
        "\t" << variantFileName << "\t" << l.comment <<
        std::endl;
    }

    void saveLabels(Utils::Label &l, std::ofstream &fileOut, std::string &dateStr, std::string &variantFileName) {
        fileOut << l.chrom << "\t" << l.pos << "\t" << l.variantId << "\t" << l.current() << "\t" << l.vartype << "\t" <<
        (((l.contains_parsed_label && l.i == l.ori_i) || (!l.contains_parsed_label && l.i > 0)) ? l.savedDate : dateStr) <<
        "\t" << variantFileName << "\t" << l.comment <<
        std::endl;
    }

    void openLabels(std::string path, std::string &image_glob_path,
                    ankerl::unordered_dense::map< std::string, ankerl::unordered_dense::map< std::string, Utils::Label>> &label_dict,
                    std::vector<std::string> &inputLabels,
                    ankerl::unordered_dense::map< std::string, ankerl::unordered_dense::set<std::string>> &seenLabels
                    ) {
        std::ifstream f;
        std::string s;
        std::string savedDate, variantFilename;
        f.open(path);
        int idx = 0;
        while (std::getline(f, s)) {
            if (idx > 0) {
                std::unique_ptr<std::vector<std::string>> v = split_keep_empty(s, '\t');
                bool clicked = !v->at(5).empty();
                int pos = std::stoi(v->at(1));
                if (v->size() == 5) {
                    savedDate = "";
                } else {
                    savedDate = v->at(5);
                }

                if (image_glob_path.empty()) {
                    variantFilename = v->at(6);
                } else {
                    variantFilename = image_glob_path;
                }
                if (!seenLabels[variantFilename].contains(v->at(3))) {
                    seenLabels[variantFilename].insert(v->at(3));
                }
                std::string comment;
                if (v->size() >= 8) {
                    comment = v->at(7);
                }
                Label l = makeLabel(v->at(0), pos, v->at(3), inputLabels, v->at(2), v->at(4), savedDate, clicked, false, comment);

                std::string key;
                key = v->at(2);
                label_dict[variantFilename][key] = l;
            }
            idx += 1;
        }
    }

    std::string removeZeros(float value) {  // https://stackoverflow.com/questions/57882748/remove-trailing-zero-in-c
        std::stringstream ss;
        ss << std::fixed << std::setprecision(2) << value;
        std::string str = ss.str();
        if(str.find('.') != std::string::npos) {
            str = str.substr(0, str.find_last_not_of('0')+1);
            if(str.find('.') == str.size()-1) {
                str = str.substr(0, str.size()-1);
            }
        }
        return str;
    }

    std::string getSize(long num) {
        int chars_needed = std::ceil(std::log10(num));
        double d;
        std::string a;
        std::string b = " bp";
        if (chars_needed > 3) {
            if (chars_needed > 6) {
                d = (double)num / 1e6;
                d = std::ceil(d * 10) / 10;
                a = removeZeros((float)d);
                b = " mb";
            } else {
                d = (double)num / 1e3;
                d = std::ceil(d * 10) / 10;
                a = removeZeros((float)d);
                b = " kb";
            }
        } else {
            a = std::to_string(num);
        }
        return a + b;
    }

    void parseMateLocation(std::string &selectedAlign, std::string &mate, std::string &target_qname) {
        if (selectedAlign.empty()) {
            return;
        }
        std::vector<std::string> s = Utils::split(selectedAlign, '\t');
        if (!s[6].empty() && s[6] != "*") {
            int p;
            try {
                p = std::stoi(s[7]);
            } catch (...) {
                return;
            }
            int b = (p - 500 > 0) ? p - 500 : 0;
            int e = p + 500;
            mate = (s[6] == "=") ? (s[2] + ":" + std::to_string(b) + "-" + std::to_string(e)) : (s[6] + ":" + std::to_string(b) + "-" + std::to_string(e));
            target_qname = s[0];
        }
    }

    int get_terminal_width() {
        // https://stackoverflow.com/questions/23369503/get-size-of-terminal-window-rows-columns
#if defined(_WIN32) || defined(_WIN64)
        CONSOLE_SCREEN_BUFFER_INFO csbi;
        GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi);
        int width = (int)(csbi.srWindow.Right-csbi.srWindow.Left+1);

#elif defined(__linux__)
        struct winsize w;
        ioctl(fileno(stdout), TIOCGWINSZ, &w);
        int width = (int)(w.ws_col);
#elif !defined(__EMSCRIPTEN__)
        struct winsize termDim;
        ioctl(0, TIOCGWINSZ, &termDim); // Grab terminal dimensions
        int width = termDim.ws_col;
#else
        int width = 80;
#endif
        width = (width > 1000 || width <= 0) ? 80 : width;
        return width;  // when reading from stdin width ends up being >1000 or 0? not sure about this fix
    }


	void ltrim(std::string &s) {
		s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) {
			return !std::isspace(ch);
    	}));
	}


 	void rtrim(std::string &s) {
    	s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) {
        	return !std::isspace(ch);
    	}).base(), s.end());
	}

	void trim(std::string &s) {
    	rtrim(s);
    	ltrim(s);
	}

}
