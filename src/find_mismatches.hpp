#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
#include <memory>

// Keep the rest of GW at its baseline ISA. AVX2 is confined to target-attributed
// functions, and AArch64 guarantees NEON. Other targets retain the word scanner.
#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#define GW_MISMATCH_AVX2 1
#include <immintrin.h>
#endif
#if defined(__aarch64__) && defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define GW_MISMATCH_NEON 1
#include <arm_neon.h>
#endif

namespace Utils { class Region; }

namespace FindMismatches {

// Preserve GW's existing ASCII lookup, including lowercase and ambiguity handling.
inline constexpr auto referenceCodes = [] {
    std::array<uint8_t, 256> codes{};
    codes['A'] = codes['a'] = 1;
    codes['C'] = codes['c'] = 2;
    codes['G'] = codes['g'] = 4;
    codes['T'] = codes['t'] = 8;
    codes['N'] = codes['n'] = 15;
    return codes;
}();

class PackedReference {
public:
    PackedReference(const char* bases, size_t length);
    size_t size() const { return length_; }
    const uint8_t* pairs(size_t referenceOffset) const {
        return phases_[referenceOffset & 1].data() + referenceOffset / 2;
    }
    uint8_t base(size_t offset) const {
        return (phases_[0][offset / 2] >> ((offset & 1) ? 0 : 4)) & 15;
    }
private:
    size_t length_;
    std::array<std::vector<uint8_t>, 2> phases_;
};

enum class Backend { Packed, AVX2, Neon };
bool available(Backend backend);
Backend selectedBackend();

const PackedReference& prepareReference(const Utils::Region& region);

// Enumerate only differing nibbles, preserving BAM's high-nibble-first order.
template<class Emit>
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
inline void emitWord(uint64_t diff, const uint8_t* qp, size_t q, size_t r, Emit& emit) {
#if (defined(__GNUC__) || defined(__clang__)) && \
    ((defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) || defined(_WIN32))
    // One flag per differing nibble; swap flags within each byte so
    // trailing-zero enumeration emits high-nibble bases first.
    uint64_t flags = (diff | (diff >> 1) | (diff >> 2) | (diff >> 3)) & UINT64_C(0x1111111111111111);
    flags = ((flags & UINT64_C(0x1010101010101010)) >> 4) |
            ((flags & UINT64_C(0x0101010101010101)) << 4);
    if (flags == UINT64_C(0x1111111111111111)) {
        // Fully different chunks need no position search.
        for (size_t i = 0; i < 8; ++i) {
            emit(q + i * 2, r + i * 2, qp[i] >> 4);
            emit(q + i * 2 + 1, r + i * 2 + 1, qp[i] & 15);
        }
    } else {
        while (flags) {
            size_t lane = (size_t)__builtin_ctzll(flags) / 4;
            uint8_t base = (qp[lane / 2] >> ((lane & 1) ? 0 : 4)) & 15;
            emit(q + lane, r + lane, base);
            flags &= flags - 1;
        }
    }
#else
    // Portable byte order for targets without the little-endian bit scan path.
    uint8_t bytes[8];
    std::memcpy(bytes, &diff, sizeof(diff));
    for (size_t i = 0; i < 8; ++i) {
        if (bytes[i] & 0xf0) emit(q + i * 2, r + i * 2, qp[i] >> 4);
        if (bytes[i] & 0x0f) emit(q + i * 2 + 1, r + i * 2 + 1, qp[i] & 15);
    }
#endif
}

template<class Emit>
#if defined(__GNUC__) || defined(__clang__)
__attribute__((always_inline))
#endif
inline void scanPacked(const uint8_t* query, const PackedReference& reference,
                       size_t q, size_t r, size_t length, Emit&& emit) {
    if (!length) return;
    if (q & 1) {
        uint8_t base = query[q / 2] & 15;
        if (base != reference.base(r)) emit(q, r, base);
        ++q; ++r; --length;
        if (!length) return;
    }
    const uint8_t* qp = query + q / 2;
    const uint8_t* rp = reference.pairs(r);
    auto comparePair = [&](uint8_t a, uint8_t b, size_t qi, size_t ri) {
        uint8_t diff = a ^ b;
        if (diff & 0xf0) emit(qi, ri, a >> 4);
        if (diff & 0x0f) emit(qi + 1, ri + 1, a & 15);
    };
    while (length >= 16) {
        uint64_t a, b;
        // memcpy permits unaligned loads and avoids strict-aliasing violations.
        std::memcpy(&a, qp, sizeof(a));
        std::memcpy(&b, rp, sizeof(b));
        if (a != b) emitWord(a ^ b, qp, q, r, emit);
        qp += 8; rp += 8; q += 16; r += 16; length -= 16;
    }
    while (length >= 2) {
        if (*qp != *rp) comparePair(*qp, *rp, q, r);
        ++qp; ++rp; q += 2; r += 2; length -= 2;
    }
    if (length && (*qp >> 4) != (*rp >> 4)) emit(q, r, *qp >> 4);
}

#if GW_MISMATCH_AVX2
template<class Emit>
__attribute__((target("avx2")))
inline void scanAVX2(const uint8_t* query, const PackedReference& reference,
                     size_t q, size_t r, size_t length, Emit&& emit) {
    if (length && (q & 1)) {
        scanPacked(query, reference, q, r, 1, emit);
        ++q; ++r; --length;
    }
    while (length >= 64) {
        const uint8_t* qp = query + q / 2;
        const uint8_t* rp = reference.pairs(r);
        __m256i diff = _mm256_xor_si256(
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(qp)),
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rp)));
        if (!_mm256_testz_si256(diff, diff)) {
            alignas(32) uint64_t words[4];
            _mm256_store_si256(reinterpret_cast<__m256i*>(words), diff);
            for (size_t i = 0; i < 4; ++i)
                if (words[i]) emitWord(words[i], qp + i * 8, q + i * 16, r + i * 16, emit);
        }
        q += 64; r += 64; length -= 64;
    }
    scanPacked(query, reference, q, r, length, emit);
}
#endif

#if GW_MISMATCH_NEON
template<class Emit>
inline void scanNeon(const uint8_t* query, const PackedReference& reference,
                     size_t q, size_t r, size_t length, Emit&& emit) {
    if (length && (q & 1)) {
        scanPacked(query, reference, q, r, 1, emit);
        ++q; ++r; --length;
    }
    // Amortize the horizontal reduction across four vectors on long matches.
    while (length >= 128) {
        const uint8_t* qp = query + q / 2;
        const uint8_t* rp = reference.pairs(r);
        uint8x16_t a = veorq_u8(vld1q_u8(qp), vld1q_u8(rp));
        uint8x16_t b = veorq_u8(vld1q_u8(qp + 16), vld1q_u8(rp + 16));
        uint8x16_t c = veorq_u8(vld1q_u8(qp + 32), vld1q_u8(rp + 32));
        uint8x16_t d = veorq_u8(vld1q_u8(qp + 48), vld1q_u8(rp + 48));
        if (vmaxvq_u8(vorrq_u8(vorrq_u8(a, b), vorrq_u8(c, d)))) {
            scanPacked(query, reference, q, r, length, emit);
            return;
        }
        q += 128; r += 128; length -= 128;
    }
    while (length >= 32) {
        const uint8_t* qp = query + q / 2;
        const uint8_t* rp = reference.pairs(r);
        uint8x16_t diff = veorq_u8(vld1q_u8(qp), vld1q_u8(rp));
        if (vmaxvq_u8(diff)) {
            // Once differences are encountered, word enumeration is cheaper on
            // AArch64 than extracting vector lanes for every subsequent chunk.
            scanPacked(query, reference, q, r, length, emit);
            return;
        }
        q += 32; r += 32; length -= 32;
    }
    scanPacked(query, reference, q, r, length, emit);
}
#endif

// All backends share the same reference cache and emit mismatches in sequence
// order, with query index, reference index and query base. Spans must be in bounds.
template<class Emit>
inline void scan(const uint8_t* query, const PackedReference& reference,
                 size_t q, size_t r, size_t length, Emit&& emit,
                 Backend backend = selectedBackend()) {
    switch (backend) {
#if GW_MISMATCH_AVX2
        case Backend::AVX2:
            if (length >= 64 + (q & 1) && available(backend)) {
                scanAVX2(query, reference, q, r, length, emit);
                return;
            }
            break;
#endif
#if GW_MISMATCH_NEON
        case Backend::Neon:
            if (length >= 32 + (q & 1)) {
                scanNeon(query, reference, q, r, length, emit);
                return;
            }
            break;
#endif
        default: break;
    }
    scanPacked(query, reference, q, r, length, emit);
}

} // namespace FindMismatches
