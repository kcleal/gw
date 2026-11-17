#pragma once

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Transport-independent API. No Skia, GLFW, oat++, Python or JSON types escape here.
namespace GwViewer {
struct State {
    std::uint64_t revision = 1;
    int width = 900, height = 500;
    std::string region, theme = "dark", settings;
    // Escaped popup records from the latest action; clients deduplicate by revision.
    std::string popups, output, scrollbars, tracks;
    double pixelRatio = 1;
    std::vector<std::string> alignments;
    std::vector<std::string> regions;
    int activeRegion = 0;
};
struct Action {
    enum class Type { Navigate, Command, Resize, Settings, Input };
    Type type;
    std::string value;
    int width = 0, height = 0;
    double pixelRatio = 1;
    int regionIndex = -1;
};
struct Config {
    std::string reference, region;
    std::vector<std::string> tracks;
    std::vector<std::string> alignments;
    int width = 900, height = 500;
};
// Build an isolated upload view using a compatible locus from the BAM.
Config alignmentConfig(const Config& base, const std::string& path);
class StaleView : public std::runtime_error {
public:
    StaleView() : std::runtime_error("View changed; fetch the latest state and retry") {}
};
// One persistent dataset/view. The caller serializes access, including frame reads.
// PNG storage is owned by Session and remains valid until the next render.
class Session {
public:
    explicit Session(const Config& config);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    State state() const;
    State apply(std::uint64_t expectedRevision, const Action& action);
    const std::vector<std::uint8_t>& frame(std::uint64_t expectedRevision);
    // Optional remote-session replication. File handles, viewport, read selection,
    // popups and in-progress gestures stay private to this renderer.
    void synchronizeSharedFrom(const Session& source, bool renderNow = true);
    void cancelInput();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}
