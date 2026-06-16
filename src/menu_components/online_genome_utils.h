#pragma once

#include <string>
#include <vector>

namespace Menu {

#if !defined(__EMSCRIPTEN__)
// Simple JSON entry for IGV online genomes.
struct OnlineGenome {
    std::string id;
    std::string name;
    std::string fastaURL;
};

// Parse the IGV genomes JSON.  We only need id, name and fastaURL from each
// top-level object in the array.  Objects may contain nested arrays/objects
// (e.g. "tracks"), so we split by tracking brace depth rather than regex.
std::vector<OnlineGenome> parseIgvGenomes(const std::string& jsonText);
#endif

}
