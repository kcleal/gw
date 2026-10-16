#pragma once

#include <cstddef>
#include "include/core/SkPathEffect.h"
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
}

inline sk_sp<SkPathEffect> makeDashPathEffect(const SkScalar* intervals, int count,
                                            SkScalar phase) {
    return Detail::makeDash<SkDashPathEffect>(intervals, count, phase, 0);
}
}
