#pragma once

#include <cstddef>
#include "include/core/SkPathEffect.h"
#include "include/core/SkFont.h"
#include "include/core/SkPath.h"
#include "include/effects/SkDashPathEffect.h"

namespace SkiaCompat {
namespace Detail {
// Select the available API without tying compatibility to a Skia milestone.
template <typename Effect>
auto makeDash(const SkScalar* intervals, int count, SkScalar phase, int)
    -> decltype(Effect::Make(intervals, count, phase)) {
    return Effect::Make(intervals, count, phase);
}

template <typename Effect>
auto makeDash(const SkScalar* intervals, int count, SkScalar phase, long)
    -> decltype(Effect::Make({intervals, static_cast<std::size_t>(count)}, phase)) {
    return Effect::Make({intervals, static_cast<std::size_t>(count)}, phase);
}

template <typename Path>
auto addPoly(Path& path, const SkPoint* points, int count, bool close, int)
    -> decltype(path.addPoly(points, count, close), void()) {
    path.addPoly(points, count, close);
}
template <typename Path>
auto addPoly(Path& path, const SkPoint* points, int count, bool close, long)
    -> decltype(path.addPoly({points, static_cast<std::size_t>(count)}, close), void()) {
    path.addPoly({points, static_cast<std::size_t>(count)}, close);
}

template <typename Font>
auto glyphBounds(const Font& font, SkGlyphID glyph, const SkPaint* paint, int)
    -> decltype(font.getBounds(glyph, paint)) {
    return font.getBounds(glyph, paint);
}
template <typename Font>
SkRect glyphBounds(const Font& font, SkGlyphID glyph, const SkPaint* paint, long) {
    SkRect bounds;
    font.getBounds(&glyph, 1, &bounds, paint);
    return bounds;
}

}

inline sk_sp<SkPathEffect> makeDashPathEffect(const SkScalar* intervals, int count,
                                            SkScalar phase) {
    return Detail::makeDash<SkDashPathEffect>(intervals, count, phase, 0);
}

inline void addPoly(SkPath& path, const SkPoint* points, int count, bool close) {
    Detail::addPoly(path, points, count, close, 0);
}
inline SkRect glyphBounds(const SkFont& font, SkGlyphID glyph, const SkPaint* paint) {
    return Detail::glyphBounds(font, glyph, paint, 0);
}
}
