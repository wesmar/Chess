// SimdOperations.h
// SIMD vector operations for neural network acceleration
//
// This module provides portable SIMD abstractions for efficient neural network
// inference. Supports AVX2 (256-bit), SSE2 (128-bit), and scalar fallback.
//
// Key features:
// - Automatic detection of best available instruction set
// - Unified interface across all SIMD widths
// - Cache-line aligned memory operations
// - Optimized dot product and accumulation primitives

#pragma once

#include <cstdint>
#include <algorithm>

// Platform detection and SIMD includes
#if defined(_MSC_VER)
    #include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
    #include <immintrin.h>
#endif

// Feature detection macros
#if defined(__AVX2__)
    #define NNUE_USE_AVX2 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
    #define NNUE_USE_SSE2 1
#endif

namespace Chess
{
namespace Neural
{
    // SIMD register width constants
    #if defined(NNUE_USE_AVX2)
        constexpr int SIMD_WIDTH = 32;          // 256 bits = 32 bytes
        constexpr int SIMD_LANES_16 = 16;       // 16 x int16_t per register
        constexpr int SIMD_LANES_32 = 8;        // 8 x int32_t per register
    #elif defined(NNUE_USE_SSE2)
        constexpr int SIMD_WIDTH = 16;          // 128 bits = 16 bytes
        constexpr int SIMD_LANES_16 = 8;        // 8 x int16_t per register
        constexpr int SIMD_LANES_32 = 4;        // 4 x int32_t per register
    #else
        constexpr int SIMD_WIDTH = 8;           // Scalar fallback
        constexpr int SIMD_LANES_16 = 1;
        constexpr int SIMD_LANES_32 = 1;
    #endif

    // Cache line size for alignment
    constexpr int CACHE_LINE_SIZE = 64;

    // ========== AVX2 OPERATIONS ==========
    #if defined(NNUE_USE_AVX2)

    // Load 16 int16_t values from aligned memory
    inline __m256i LoadVector256(const int16_t* ptr)
    {
        return _mm256_load_si256(reinterpret_cast<const __m256i*>(ptr));
    }

    // Store 16 int16_t values to aligned memory
    inline void StoreVector256(int16_t* ptr, __m256i vec)
    {
        _mm256_store_si256(reinterpret_cast<__m256i*>(ptr), vec);
    }

    // Add two vectors of int16_t
    inline __m256i AddVector256_16(const __m256i a, const __m256i b)
    {
        return _mm256_add_epi16(a, b);
    }

    // Subtract two vectors of int16_t
    inline __m256i SubVector256_16(const __m256i a, const __m256i b)
    {
        return _mm256_sub_epi16(a, b);
    }

    // Clamp int16_t values to [0, max]
    inline __m256i ClampVector256_16(const __m256i vec, const __m256i zero, const __m256i max)
    {
        return _mm256_min_epi16(_mm256_max_epi16(vec, zero), max);
    }

    // Multiply and add adjacent pairs (int16 -> int32)
    // Result[i] = a[2i] * b[2i] + a[2i+1] * b[2i+1]
    inline __m256i MultiplyAddAdjacent256(const __m256i a, const __m256i b)
    {
        return _mm256_madd_epi16(a, b);
    }

    // Horizontal sum of int32_t vector
    inline int32_t HorizontalSum256_32(const __m256i vec)
    {
        __m128i low = _mm256_castsi256_si128(vec);
        __m128i high = _mm256_extracti128_si256(vec, 1);
        __m128i sum = _mm_add_epi32(low, high);
        sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0x4E));  // 01001110
        sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0xB1));  // 10110001
        return _mm_cvtsi128_si32(sum);
    }

    // Set all elements to zero
    inline __m256i ZeroVector256()
    {
        return _mm256_setzero_si256();
    }

    // Set all int16_t elements to same value
    inline __m256i SetVector256_16(int16_t value)
    {
        return _mm256_set1_epi16(value);
    }

    #endif // NNUE_USE_AVX2

    // ========== SSE2 OPERATIONS ==========
    #if defined(NNUE_USE_SSE2)

    // Load 8 int16_t values from aligned memory
    inline __m128i LoadVector128(const int16_t* ptr)
    {
        return _mm_load_si128(reinterpret_cast<const __m128i*>(ptr));
    }

    // Store 8 int16_t values to aligned memory
    inline void StoreVector128(int16_t* ptr, __m128i vec)
    {
        _mm_store_si128(reinterpret_cast<__m128i*>(ptr), vec);
    }

    // Add two vectors of int16_t
    inline __m128i AddVector128_16(const __m128i a, const __m128i b)
    {
        return _mm_add_epi16(a, b);
    }

    // Subtract two vectors of int16_t
    inline __m128i SubVector128_16(const __m128i a, const __m128i b)
    {
        return _mm_sub_epi16(a, b);
    }

    // Clamp int16_t values to [0, max] using SSE2
    inline __m128i ClampVector128_16(const __m128i vec, const __m128i zero, const __m128i max)
    {
        // SSE2 doesn't have min/max for signed int16, use saturating arithmetic
        __m128i clamped = _mm_max_epi16(vec, zero);
        return _mm_min_epi16(clamped, max);
    }

    // Multiply and add adjacent pairs (int16 -> int32)
    inline __m128i MultiplyAddAdjacent128(const __m128i a, const __m128i b)
    {
        return _mm_madd_epi16(a, b);
    }

    // Horizontal sum of int32_t vector
    inline int32_t HorizontalSum128_32(const __m128i vec)
    {
        __m128i sum = _mm_add_epi32(vec, _mm_shuffle_epi32(vec, 0x4E));
        sum = _mm_add_epi32(sum, _mm_shuffle_epi32(sum, 0xB1));
        return _mm_cvtsi128_si32(sum);
    }

    // Set all elements to zero
    inline __m128i ZeroVector128()
    {
        return _mm_setzero_si128();
    }

    // Set all int16_t elements to same value
    inline __m128i SetVector128_16(int16_t value)
    {
        return _mm_set1_epi16(value);
    }

    #endif // NNUE_USE_SSE2

    // ========== PORTABLE OPERATIONS ==========

    // Dot product of two int16_t arrays (scalar fallback)
    inline int32_t DotProductScalar(const int16_t* a, const int16_t* b, int count)
    {
        int32_t sum = 0;
        for (int i = 0; i < count; ++i)
        {
            sum += static_cast<int32_t>(a[i]) * static_cast<int32_t>(b[i]);
        }
        return sum;
    }

    // Vectorized dot product with automatic dispatch
    inline int32_t DotProduct(const int16_t* a, const int16_t* b, int count)
    {
        int32_t sum = 0;

        #if defined(NNUE_USE_AVX2)
        {
            __m256i acc = ZeroVector256();
            int i = 0;
            for (; i + 16 <= count; i += 16)
            {
                __m256i va = LoadVector256(a + i);
                __m256i vb = LoadVector256(b + i);
                acc = _mm256_add_epi32(acc, MultiplyAddAdjacent256(va, vb));
            }
            sum = HorizontalSum256_32(acc);
            // Handle remainder
            for (; i < count; ++i)
            {
                sum += static_cast<int32_t>(a[i]) * static_cast<int32_t>(b[i]);
            }
        }
        #elif defined(NNUE_USE_SSE2)
        {
            __m128i acc = ZeroVector128();
            int i = 0;
            for (; i + 8 <= count; i += 8)
            {
                __m128i va = LoadVector128(a + i);
                __m128i vb = LoadVector128(b + i);
                acc = _mm_add_epi32(acc, MultiplyAddAdjacent128(va, vb));
            }
            sum = HorizontalSum128_32(acc);
            for (; i < count; ++i)
            {
                sum += static_cast<int32_t>(a[i]) * static_cast<int32_t>(b[i]);
            }
        }
        #else
        {
            sum = DotProductScalar(a, b, count);
        }
        #endif

        return sum;
    }

    // Add vector b to vector a (in-place)
    inline void AddVectors(int16_t* a, const int16_t* b, int count)
    {
        #if defined(NNUE_USE_AVX2)
        {
            int i = 0;
            for (; i + 16 <= count; i += 16)
            {
                __m256i va = LoadVector256(a + i);
                __m256i vb = LoadVector256(b + i);
                StoreVector256(a + i, AddVector256_16(va, vb));
            }
            for (; i < count; ++i)
            {
                a[i] += b[i];
            }
        }
        #elif defined(NNUE_USE_SSE2)
        {
            int i = 0;
            for (; i + 8 <= count; i += 8)
            {
                __m128i va = LoadVector128(a + i);
                __m128i vb = LoadVector128(b + i);
                StoreVector128(a + i, AddVector128_16(va, vb));
            }
            for (; i < count; ++i)
            {
                a[i] += b[i];
            }
        }
        #else
        {
            for (int i = 0; i < count; ++i)
            {
                a[i] += b[i];
            }
        }
        #endif
    }

    // Subtract vector b from vector a (in-place)
    inline void SubVectors(int16_t* a, const int16_t* b, int count)
    {
        #if defined(NNUE_USE_AVX2)
        {
            int i = 0;
            for (; i + 16 <= count; i += 16)
            {
                __m256i va = LoadVector256(a + i);
                __m256i vb = LoadVector256(b + i);
                StoreVector256(a + i, SubVector256_16(va, vb));
            }
            for (; i < count; ++i)
            {
                a[i] -= b[i];
            }
        }
        #elif defined(NNUE_USE_SSE2)
        {
            int i = 0;
            for (; i + 8 <= count; i += 8)
            {
                __m128i va = LoadVector128(a + i);
                __m128i vb = LoadVector128(b + i);
                StoreVector128(a + i, SubVector128_16(va, vb));
            }
            for (; i < count; ++i)
            {
                a[i] -= b[i];
            }
        }
        #else
        {
            for (int i = 0; i < count; ++i)
            {
                a[i] -= b[i];
            }
        }
        #endif
    }

    // Copy aligned memory block
    inline void CopyAligned(void* dst, const void* src, size_t bytes)
    {
        std::copy(
            static_cast<const char*>(src),
            static_cast<const char*>(src) + bytes,
            static_cast<char*>(dst)
        );
    }

} // namespace Neural
} // namespace Chess
