#pragma once
#include "online_catalogue.h"
#include <htslib/faidx.h>
#include <map>
#include <atomic>
#include <memory>
namespace Manager { class GwPlot; }
namespace Online {
using Contigs = std::map<std::string, long long>;
// Explicit companions are kept out of URL strings and do not change the GW API.
void rememberIndexes(const Entry& entry);
std::string formatFor(const std::string& path);
std::string referenceAssembly(const Manager::GwPlot* plot);
const char* indexPath(const std::string& path);
faidx_t* loadFai(const std::string& path);
Contigs contigsFor(const Manager::GwPlot* plot, bool replacingReference);
Entry validate(Entry entry, const Contigs& contigs, bool checkAssembly);
std::vector<Entry> validateGroup(std::vector<Entry> entries, const Contigs& reference, const Contigs& alignments, bool checkAssembly);
void openGroup(Manager::GwPlot* plot, const std::vector<Entry>& entries, bool asTrack, bool checkAssembly, bool validated=false, bool asTable=false);
void open(Manager::GwPlot* plot, const Entry& entry, bool asTrack, bool checkAssembly, bool validated=false);
#ifndef __EMSCRIPTEN__
std::int64_t fetchFileSize(const std::string& url);
std::string fetchCatalogue(const std::string& url, const std::shared_ptr<std::atomic<bool>>& cancelled);
#endif
}
