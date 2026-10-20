#pragma once
#include "online_catalogue.h"
#include <atomic>
#include <memory>
namespace Online {
std::string downloadDirectory(const std::string& iniPath);
#ifndef __EMSCRIPTEN__
struct DownloadProgress {
    std::atomic<bool> cancelled{false};
    std::atomic<long long> received{0}, total{0};
};
Entry download(Entry entry, const std::string& directory, const std::shared_ptr<DownloadProgress>& progress);
#endif
}
