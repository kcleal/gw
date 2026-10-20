#include "menu_components/online_genome_utils.h"
#include "online/online_catalogue.h"
#include <stdexcept>
namespace Menu {
std::vector<OnlineGenome> parseIgvGenomes(const std::string& text) {
    std::vector<OnlineGenome> result;
    try { for (const auto& entry : Online::parseCatalogue(text)) {
        if (entry.category != "reference") continue;
        result.push_back({entry.id.rfind("igv:",0)==0?entry.id.substr(4):entry.id,
                          entry.name, entry.url, entry.indexURL, entry.compressedIndexURL});
    }
    } catch (const std::runtime_error&) { return {}; }
    return result;
}
}
