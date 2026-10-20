#pragma once

// Optional wire DTOs. No dependencies on GwPlot, Skia, GLFW or a JSON library.
// JSON field names and discriminated message bodies are defined in schema.json.
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <map>
#include <variant>
#include <vector>

namespace GwViewerProtocol::V1 {
inline constexpr const char* name = "gw-viewer";
inline constexpr int version = 1;
inline constexpr std::size_t maxMessageBytes = 65536;

struct Size { int width, height; };
struct Viewport {
    int width = 900, height = 500;  // logical canvas pixels, not browser window size
    double pixel_ratio = 1;
    Size framebuffer() const {
        if (width < 1 || height < 1 || width > 8192 || height > 8192 ||
            !std::isfinite(pixel_ratio) || pixel_ratio < .25 || pixel_ratio > 8)
            throw std::invalid_argument("Invalid viewport");
        const int w = int(std::floor(width * pixel_ratio + .5));
        const int h = int(std::floor(height * pixel_ratio + .5));
        if (w < 200 || h < 100 || w > 8192 || h > 8192 || std::int64_t(w)*h > 33554432)
            throw std::invalid_argument("Invalid framebuffer size");
        return {w, h};
    }
};

// Serialize uint64 counters as canonical decimal JSON strings, never JSON numbers.
inline std::uint64_t counter(const std::string& value) {
    if (value.empty() || value[0] == '0' || value.size() > 20)
        throw std::invalid_argument("Invalid counter");
    std::uint64_t result = 0;
    for (const char c : value) {
        if (c < '0' || c > '9' || result > (UINT64_MAX - (c-'0')) / 10)
            throw std::invalid_argument("Invalid counter");
        result = result * 10 + (c-'0');
    }
    return result;
}

struct Identity {
    std::string session_id, client_id, connection_id;
};
struct Envelope : Identity {
    std::string request_id, sequence;
};
struct Join {
    std::string request_id, session_id;
    Viewport viewport;
    std::string client_id, resume_token;  // both omitted for a new client
};
struct Navigate { std::string region; int region_index = -1; };
struct Command { std::string text; int region_index = -1; };
struct Pointer {
    std::string phase, button;  // press/move/release/wheel/cancel; left/middle/right/none
    double x = 0, y = 0, dx = 0, dy = 0;  // logical pixels; wheel deltas in steps
    std::vector<std::string> modifiers;  // shift/control/alt/meta
};
struct Key { std::string key; std::vector<std::string> modifiers; int region_index = -1; };
struct VScroll { std::string collection_id; int offset = 0; };
struct TrackResize { std::string track_id; double y = 0; };
using Setting = std::variant<bool, double, std::string>;
struct Settings { std::map<std::string, Setting> values; };
using Action = std::variant<Navigate, Command, Settings, Pointer, Key, VScroll, TrackResize>;
struct ActionRequest : Envelope {
    std::string expected_revision, frame_id;
    Action action;
};
struct Frame {
    std::string frame_id, revision;
    Viewport viewport;
    int width = 0, height = 0;  // actual PNG pixels
    // layout is a JSON object owned by the implementation, not serialized text.
};
} // namespace GwViewerProtocol::V1
