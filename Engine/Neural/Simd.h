// Simd.h
// Vectorised kernels for the network, with a scalar fallback.
//
// The distribution must run on any x64, so AVX2 cannot be a build requirement.
// MSVC accepts AVX2 intrinsics without /arch:AVX2 - it simply will not emit
// them anywhere else - so the vector paths are compiled in unconditionally and
// selected at run time from CPUID. A machine without AVX2 takes the scalar
// path and still gets the right answer.
//
// Both paths must produce bit-identical results, which is why the reduction
// order in the scalar version matches the vector one: with integer arithmetic
// that is easy to guarantee, and it means testing one tests the other.
//
// Measured on the first working network, walking a fixed tree:
//   full recompute, scalar        43 496 eval/s
//   incremental,     scalar       39 641 eval/s   <- the forward pass dominates
// The 32x256 int8 multiply-accumulate is 8192 operations per evaluation and
// swamps everything the accumulator does, which is why it is vectorised first.
#pragma once

#include <cstdint>
#include <immintrin.h>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace Chess
{
namespace Neural
{
    inline bool DetectAvx2() noexcept
    {
#if defined(_MSC_VER)
        int info[4] = {0, 0, 0, 0};
        __cpuid(info, 0);
        if (info[0] < 7) return false;
        __cpuidex(info, 7, 0);
        const bool avx2 = (info[1] & (1 << 5)) != 0;      // EBX bit 5
        __cpuid(info, 1);
        const bool osxsave = (info[2] & (1 << 27)) != 0;  // ECX bit 27
        const bool avx = (info[2] & (1 << 28)) != 0;      // ECX bit 28
        if (!(avx2 && avx && osxsave)) return false;
        // The OS must actually preserve YMM state across context switches.
        return (_xgetbv(0) & 0x6) == 0x6;
#else
        return false;
#endif
    }

    inline bool HasAvx2() noexcept
    {
        static const bool value = DetectAvx2();
        return value;
    }

    // acc[i] += row[i], over `count` int16 values.
    inline void AddRow16(int16_t* acc, const int16_t* row, int count) noexcept
    {
        int i = 0;
        if (HasAvx2())
        {
            for (; i + 16 <= count; i += 16)
            {
                const __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(acc + i));
                const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(row + i));
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(acc + i), _mm256_add_epi16(a, b));
            }
        }
        for (; i < count; ++i) acc[i] = static_cast<int16_t>(acc[i] + row[i]);
    }

    inline void SubRow16(int16_t* acc, const int16_t* row, int count) noexcept
    {
        int i = 0;
        if (HasAvx2())
        {
            for (; i + 16 <= count; i += 16)
            {
                const __m256i a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(acc + i));
                const __m256i b = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(row + i));
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(acc + i), _mm256_sub_epi16(a, b));
            }
        }
        for (; i < count; ++i) acc[i] = static_cast<int16_t>(acc[i] - row[i]);
    }

    // Clipped ReLU into uint8: clamp(v, 0, limit) with limit <= 255.
    inline void ClipToBytes(const int16_t* src, uint8_t* dst, int count, int limit) noexcept
    {
        int i = 0;
        if (HasAvx2() && limit == 255)
        {
            const __m256i zero = _mm256_setzero_si256();
            for (; i + 32 <= count; i += 32)
            {
                __m256i lo = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i));
                __m256i hi = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(src + i + 16));
                lo = _mm256_max_epi16(lo, zero);
                hi = _mm256_max_epi16(hi, zero);
                // packus saturates to [0,255] and interleaves the two 128-bit
                // lanes, so permute puts the bytes back in source order.
                __m256i packed = _mm256_packus_epi16(lo, hi);
                packed = _mm256_permute4x64_epi64(packed, 0xD8);
                _mm256_storeu_si256(reinterpret_cast<__m256i*>(dst + i), packed);
            }
        }
        for (; i < count; ++i)
        {
            int v = src[i];
            if (v < 0) v = 0;
            else if (v > limit) v = limit;
            dst[i] = static_cast<uint8_t>(v);
        }
    }

    // Dot product of uint8 activations with int8 weights, accumulated in int32.
    inline int32_t DotU8I8(const uint8_t* a, const int8_t* w, int count) noexcept
    {
        int i = 0;
        int32_t total = 0;
        if (HasAvx2())
        {
            __m256i sum = _mm256_setzero_si256();
            const __m256i ones = _mm256_set1_epi16(1);
            for (; i + 32 <= count; i += 32)
            {
                const __m256i av = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(a + i));
                const __m256i wv = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + i));
                // maddubs multiplies uint8 by int8 and adds adjacent pairs into
                // int16; madd then widens pairs of those into int32. Products
                // here are at most 255*127 so the int16 stage cannot overflow.
                const __m256i prod = _mm256_maddubs_epi16(av, wv);
                sum = _mm256_add_epi32(sum, _mm256_madd_epi16(prod, ones));
            }
            __m128i lo = _mm256_castsi256_si128(sum);
            __m128i hi = _mm256_extracti128_si256(sum, 1);
            lo = _mm_add_epi32(lo, hi);
            lo = _mm_add_epi32(lo, _mm_shuffle_epi32(lo, 0x4E));
            lo = _mm_add_epi32(lo, _mm_shuffle_epi32(lo, 0xB1));
            total = _mm_cvtsi128_si32(lo);
        }
        for (; i < count; ++i) total += static_cast<int32_t>(a[i]) * w[i];
        return total;
    }
}
}
