#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace Online {
// This model contains no renderer, transport, or platform state.
struct Entry {
    std::string id, name, category, assembly, sample, technology, format;
    std::string url, indexURL, compressedIndexURL, source, release, attribution;
    std::string regionsURL, locus, provider, providerId, description, tableColumns;
    std::int64_t sizeBytes{-1};
    std::vector<std::string> bundle;
};
std::vector<Entry> parseCatalogue(const std::string& json, bool allowMountedPaths=false);
std::vector<Entry> bundledCatalogue();
std::string tableRowToBed(const std::string& row, const std::string& columns);
bool regionalFile(const Entry& entry);
std::string fileSizeLabel(std::int64_t bytes);
std::string ucscSearchURL(const std::string& assembly, const std::string& query);
std::vector<Entry> parseUcscSearch(const std::string& text, const std::string& assembly);
Entry resolveUcscTrack(const std::string& text, Entry entry);

std::string encodeEntry(const Entry& entry);
std::string encodeCatalogue(const std::vector<Entry>& entries);
std::vector<Entry> resolveBundle(const Entry& entry, const std::vector<Entry>& catalogue);
bool httpURL(const std::string& url);
std::string resourceName(const std::string& url);
std::vector<std::string> indexCandidates(const Entry& entry);
}
