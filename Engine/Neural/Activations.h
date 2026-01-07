// Activations.h
// Neural network activation functions for NNUE evaluation
//
// This module provides quantized activation functions optimized for integer
// arithmetic. All functions operate on fixed-point representations to avoid
// floating-point overhead during inference.
//
// Key features:
// - ClampedRelu: Standard ReLU with upper bound clipping
// - SquaredClampedRelu: Squared activation for increased non-linearity
// - SIMD-accelerated batch processing
// - Quantization-aware scaling

#pragma once

#include "SimdOperations.h"
#include <cstdint>
#include <algorithm>

namespace Chess
{
namespace Neural
{
    // Quantization parameters matching Stockfish NNUE format
    constexpr int WEIGHT_SCALE_BITS = 6;                  // Weight quantization shift
    constexpr int OUTPUT_SCALE = 16;                      // Final output scaling
    constexpr int ACTIVATION_MAX = 127;                   // Clipping upper bound (int8 range)
    constexpr int TRANSFORMER_SCALE = 64;                 // Feature transformer output scale

    // ========== SCALAR ACTIVATION FUNCTIONS ==========

    // Clamped ReLU activation: clamp(max(0, x >> shift), 0, 127)
    // Converts int32 accumulator output to uint8 activation
    // @param x: Input value (typically int32 accumulator)
    // @param shift: Right shift for quantization (default: WEIGHT_SCALE_BITS)
    // @return: Clamped activation value in [0, 127]
    inline uint8_t ClampedRelu(int32_t x, int shift = WEIGHT_SCALE_BITS)
    {
        int32_t shifted = x >> shift;
        return static_cast<uint8_t>(std::clamp(shifted, 0, ACTIVATION_MAX));
    }

    // Squared clamped ReLU: (clamp(x, 0, 127))^2 >> 7
    // Provides stronger non-linearity for first layer
    // Output range: [0, 127] after scaling
    // @param x: Input value (int16 from transformer)
    // @return: Squared activation in [0, 127]
    inline uint8_t SquaredClampedRelu(int16_t x)
    {
        int32_t clamped = std::clamp(static_cast<int32_t>(x), 0, ACTIVATION_MAX);
        // Square and scale down to fit in uint8
        return static_cast<uint8_t>((clamped * clamped) >> 7);
    }

    // ========== BATCH ACTIVATION FUNCTIONS ==========

    // Apply ClampedRelu to array of int32 values, output to uint8 array
    // @param input: Source int32 accumulator values
    // @param output: Destination uint8 activation values
    // @param count: Number of elements to process
    // @param shift: Quantization shift (default: WEIGHT_SCALE_BITS)
    inline void ApplyClampedRelu(const int32_t* input, uint8_t* output, int count,
                                  int shift = WEIGHT_SCALE_BITS)
    {
        for (int i = 0; i < count; ++i)
        {
            output[i] = ClampedRelu(input[i], shift);
        }
    }

    // Apply SquaredClampedRelu to array of int16 values
    // Used for transformer output processing
    // @param input: Source int16 transformer outputs
    // @param output: Destination uint8 activation values
    // @param count: Number of elements to process
    inline void ApplySquaredClampedRelu(const int16_t* input, uint8_t* output, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            output[i] = SquaredClampedRelu(input[i]);
        }
    }

    // Apply ClampedRelu to int16 array with SIMD acceleration
    // Processes transformer accumulator for network input
    // @param input: Source int16 values (aligned)
    // @param output: Destination uint8 values
    // @param count: Number of elements (should be multiple of SIMD width)
    inline void ApplyClampedReluInt16(const int16_t* input, uint8_t* output, int count)
    {
        #if defined(NNUE_USE_AVX2)
        {
            __m256i zero = _mm256_setzero_si256();
            __m256i max_val = _mm256_set1_epi16(ACTIVATION_MAX);

            int i = 0;
            for (; i + 16 <= count; i += 16)
            {
                __m256i v = _mm256_load_si256(reinterpret_cast<const __m256i*>(input + i));
                v = _mm256_max_epi16(v, zero);
                v = _mm256_min_epi16(v, max_val);

                // Pack 16-bit to 8-bit with saturation
                __m256i packed = _mm256_packus_epi16(v, v);
                packed = _mm256_permute4x64_epi64(packed, 0xD8);  // Fix lane ordering
                _mm_storeu_si128(reinterpret_cast<__m128i*>(output + i),
                                 _mm256_castsi256_si128(packed));
            }
            for (; i < count; ++i)
            {
                output[i] = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(input[i]), 0, ACTIVATION_MAX));
            }
        }
        #elif defined(NNUE_USE_SSE2)
        {
            __m128i zero = _mm_setzero_si128();
            __m128i max_val = _mm_set1_epi16(ACTIVATION_MAX);

            int i = 0;
            for (; i + 8 <= count; i += 8)
            {
                __m128i v = _mm_load_si128(reinterpret_cast<const __m128i*>(input + i));
                v = _mm_max_epi16(v, zero);
                v = _mm_min_epi16(v, max_val);

                __m128i packed = _mm_packus_epi16(v, v);
                _mm_storel_epi64(reinterpret_cast<__m128i*>(output + i), packed);
            }
            for (; i < count; ++i)
            {
                output[i] = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(input[i]), 0, ACTIVATION_MAX));
            }
        }
        #else
        {
            for (int i = 0; i < count; ++i)
            {
                output[i] = static_cast<uint8_t>(
                    std::clamp(static_cast<int>(input[i]), 0, ACTIVATION_MAX));
            }
        }
        #endif
    }

    // Combine two perspectives with squared ReLU for network input
    // Processes both white and black accumulator perspectives
    // @param accWhite: White perspective accumulator (int16)
    // @param accBlack: Black perspective accumulator (int16)
    // @param output: Combined output for dense layer input
    // @param halfDim: Size of each perspective (e.g., 128)
    inline void CombinePerspectives(const int16_t* accWhite, const int16_t* accBlack,
                                     uint8_t* output, int halfDim)
    {
        // First half: white perspective with squared ReLU
        ApplySquaredClampedRelu(accWhite, output, halfDim);

        // Second half: black perspective with squared ReLU
        ApplySquaredClampedRelu(accBlack, output + halfDim, halfDim);
    }

    // Pairwise product of activations (used in some architectures)
    // Computes element-wise product of two activation vectors
    // @param a: First input vector
    // @param b: Second input vector
    // @param output: Product output
    // @param count: Number of elements
    inline void PairwiseProduct(const uint8_t* a, const uint8_t* b,
                                 int16_t* output, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            output[i] = static_cast<int16_t>(a[i]) * static_cast<int16_t>(b[i]);
        }
    }

} // namespace Neural
} // namespace Chess
