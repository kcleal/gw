#include "online/online_catalogue.h"
#include <stdexcept>
extern "C" int catalogue_count(const char* text) {
    try { return int(Online::parseCatalogue(text).size()); }
    catch (const std::runtime_error&) { return -1; }
}
extern "C" int bundle_count() {
    const auto entries=Online::bundledCatalogue();
    for(const auto& e:entries)if(!e.bundle.empty())return int(Online::resolveBundle(e,entries).size());
    return 0;
}
