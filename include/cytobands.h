//
// Created by Kez Cleal on 20/06/2024.
//

#pragma once

#include <iostream>
#include <string>
#include <fstream>
#include <sstream>
#include <utility>
#include <unordered_map>


struct Band {
    int start, end, alpha, red, green, blue;
    std::string name;
};


void printIdeogram(std::vector<Band>& bands) {
    std::cout << "{\n";
    for (const auto& b : bands) {
        std::cout <<
        "{" << b.start << ", "
        << b.end << ", "
        << b.alpha << ", "
        << b.red << ", "
        << b.green << ", "
        << b.blue << ", "
        << b.name << "}\n"
    }
    std::cout << "};\n\n";
}


#define next std::getline(iss, token, '\t')

void readCytoBandFile(std::string file_path, std::vector<Band>& ideogram) {
    std::ifstream band_file(file_path);
    if (!band_file) {
        throw std::runtime_error("Failed to open input files");
    }
    std::string line, token, name, property;
    while (std::getline(band_file, line)) {
        std::istringstream iss(line);
        next; next;
        int start = std::stoi(token);
        next;
        int end = std::stoi(token);
        next;
        name = token;
        next;
        property = name;

        if (property == "gneg") {
            ideogram.emplace_back() = {start, end, 0, 0, 0, 0, name};
        } else if (property == "gpos25") {
            ideogram.emplace_back() = {start, end, 0.5, 235, 235, 235, name};
        } else if (property == "gpos50") {
            ideogram.emplace_back() = {start, end, 0.5, 185, 185, 185, name};
        } else if (property == "gpos75") {
            ideogram.emplace_back() = {start, end, 0.5, 110, 110, 110, name};
        } else if (property == "gpos100") {
            ideogram.emplace_back() = {start, end, 0.5, 60, 60, 60, name};
        } else if (property == "acen") {
            ideogram.emplace_back() = {start, end, 0.5, 220, 10, 10, name};
        } else if (property == "gvar") {
            ideogram.emplace_back() = {start, end, 0.5, 10, 10, 220, name};
        } else {  // try custom color scheme
            try {
                std::string<std::string> a = Utils::split(property, ',');
                ideogram.emplace_back() = {start, end, std::stoi(a[0]), std::stoi(a[1]), std::stoi(a[2]), std::stoi(a[3]), name};
            } except {
            };

        }
    }
    printIdeogram(ideogram);
}