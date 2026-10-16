#include "find_mismatches.hpp"
#include "utils.h"

namespace FindMismatches {

PackedReference::PackedReference(const char* bases, size_t length) : length_(length) {
    phases_[0].resize((length + 1) / 2);
    phases_[1].resize(length / 2);
    for (size_t i = 0; i < length; ++i) {
        uint8_t high = referenceCodes[(unsigned char)bases[i]];
        uint8_t low = i + 1 < length ? referenceCodes[(unsigned char)bases[i + 1]] : 0;
        phases_[i & 1][i / 2] = (high << 4) | low;
    }
}

const PackedReference& prepareReference(const Utils::Region& region) {
    if (!region.packedReference) {
        const char* bases = region.refSeqBuffer ? region.refSeqBuffer.get() : region.refSeq;
        size_t length = region.refSeqBuffer ? region.refBufferLen : region.refSeqLen;
        region.packedReference = std::make_shared<PackedReference>(bases, length);
    }
    return *region.packedReference;
}

bool available(Backend backend) {
    switch (backend) {
        case Backend::Packed: return true;
        case Backend::AVX2:
#if GW_MISMATCH_AVX2
            // Compiler CPU detection also checks OS support for AVX register state.
            static const bool avx2 = __builtin_cpu_supports("avx2");
            return avx2;
#else
            return false;
#endif
        case Backend::Neon:
#if GW_MISMATCH_NEON
            return true;
#else
            return false;
#endif
    }
    return false;
}

Backend selectedBackend() {
    static const Backend backend = [] {
        if (available(Backend::AVX2)) return Backend::AVX2;
        if (available(Backend::Neon)) return Backend::Neon;
        return Backend::Packed;
    }();
    return backend;
}

} // namespace FindMismatches
