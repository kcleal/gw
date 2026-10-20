#pragma once

#include <string>
#include <vector>

namespace Menu {

// Simple JSON entry for IGV online genomes.
struct OnlineGenome {
    std::string id;
    std::string name;
    std::string fastaURL;
    std::string indexURL;
    std::string compressedIndexURL;
};

// Shared JSON decoder handles companion URLs, escaped strings and nested tracks.
std::vector<OnlineGenome> parseIgvGenomes(const std::string& jsonText);

}
