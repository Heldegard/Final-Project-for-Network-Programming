#pragma once

#include <cstdint>
#include <string>

namespace lk {

struct Rect {
    int32_t x = 0;
    int32_t y = 0;
    int32_t w = 0;
    int32_t h = 0;

    bool empty() const { return w <= 0 || h <= 0; }
    int32_t right() const { return x + w; }
    int32_t bottom() const { return y + h; }
};

enum class Edge { Left = 0, Right = 1, Top = 2, Bottom = 3 };

inline const char* edge_name(Edge e) {
    switch (e) {
        case Edge::Left: return "left";
        case Edge::Right: return "right";
        case Edge::Top: return "top";
        case Edge::Bottom: return "bottom";
    }
    return "right";
}

inline bool edge_from_name(const std::string& s, Edge* out) {
    if (s == "left") { *out = Edge::Left; return true; }
    if (s == "right") { *out = Edge::Right; return true; }
    if (s == "top") { *out = Edge::Top; return true; }
    if (s == "bottom") { *out = Edge::Bottom; return true; }
    return false;
}

// Map a pixel coordinate on [origin, origin + size) to the 0..65535 range used
// by MOUSEEVENTF_ABSOLUTE. Pure function so it can be unit tested.
inline uint16_t normalize(int32_t pos, int32_t origin, int32_t size) {
    if (size <= 1) {
        return 0;
    }
    const int64_t span = size - 1;
    int64_t v = (static_cast<int64_t>(pos - origin) * 65535 + span / 2) / span;
    if (v < 0) {
        v = 0;
    }
    if (v > 65535) {
        v = 65535;
    }
    return static_cast<uint16_t>(v);
}

inline int32_t denormalize(uint16_t n, int32_t origin, int32_t size) {
    if (size <= 1) {
        return origin;
    }
    const int64_t span = size - 1;
    return origin + static_cast<int32_t>((static_cast<int64_t>(n) * span + 32767) / 65535);
}

inline uint16_t norm_x(int32_t px, const Rect& r) { return normalize(px, r.x, r.w); }
inline uint16_t norm_y(int32_t py, const Rect& r) { return normalize(py, r.y, r.h); }
inline int32_t denorm_x(uint16_t n, const Rect& r) { return denormalize(n, r.x, r.w); }
inline int32_t denorm_y(uint16_t n, const Rect& r) { return denormalize(n, r.y, r.h); }

struct ScreenInfo {
    Rect rc;
    uint32_t dpi = 96;
    std::string name;
};

}  // namespace lk
