// DenseLayer.h
// Fully connected neural network layer with quantized weights
//
// This module implements dense (fully connected) layers for NNUE inference.
// Uses int8 weights and int32 biases for memory efficiency and fast
// integer arithmetic on modern CPUs.
//
// Key features:
// - Quantized weights (int8) for cache efficiency
// - SIMD-accelerated matrix-vector multiplication
// - Support for both dense and sparse input modes
// - Cache-line aligned storage

#pragma once

#include "SimdOperations.h"
#include "Activations.h"
#include <cstdint>
#include <vector>
#include <memory>

namespace Chess
{
namespace Neural
{
    // Forward declaration for weight loading
    class WeightLoader;

    // ========== DENSE LAYER ==========
    // Fully connected layer: output = activation(weights * input + bias)
    // Template parameters define compile-time dimensions for optimization
    template<int InputDim, int OutputDim>
    class DenseLayer
    {
    public:
        // Dimension accessors
        static constexpr int INPUT_SIZE = InputDim;
        static constexpr int OUTPUT_SIZE = OutputDim;

        // Padded input size for SIMD alignment (multiple of 32)
        static constexpr int PADDED_INPUT = ((InputDim + 31) / 32) * 32;

        DenseLayer() = default;

        // Initialize weights and biases to zero
        void Initialize()
        {
            std::fill(std::begin(m_weights), std::end(m_weights), 0);
            std::fill(std::begin(m_biases), std::end(m_biases), 0);
        }

        // Forward pass with uint8 input (from previous activation)
        // @param input: Input activations [InputDim]
        // @param output: Output values before activation [OutputDim]
        void Forward(const uint8_t* input, int32_t* output) const
        {
            for (int o = 0; o < OutputDim; ++o)
            {
                int32_t sum = m_biases[o];

                #if defined(NNUE_USE_AVX2)
                {
                    __m256i acc = _mm256_setzero_si256();
                    const int8_t* weights = &m_weights[o * PADDED_INPUT];

                    for (int i = 0; i + 32 <= InputDim; i += 32)
                    {
                        // Load 32 uint8 inputs
                        __m256i in = _mm256_loadu_si256(
                            reinterpret_cast<const __m256i*>(input + i));
                        // Load 32 int8 weights
                        __m256i w = _mm256_loadu_si256(
                            reinterpret_cast<const __m256i*>(weights + i));

                        // Multiply with sign extension and accumulate
                        __m256i product = _mm256_maddubs_epi16(in, w);
                        acc = _mm256_add_epi32(acc, _mm256_madd_epi16(product,
                            _mm256_set1_epi16(1)));
                    }

                    // Horizontal sum
                    __m128i low = _mm256_castsi256_si128(acc);
                    __m128i high = _mm256_extracti128_si256(acc, 1);
                    __m128i sum128 = _mm_add_epi32(low, high);
                    sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, 0x4E));
                    sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, 0xB1));
                    sum += _mm_cvtsi128_si32(sum128);

                    // Handle remainder
                    for (int i = (InputDim / 32) * 32; i < InputDim; ++i)
                    {
                        sum += static_cast<int32_t>(input[i]) *
                               static_cast<int32_t>(weights[i]);
                    }
                }
                #else
                {
                    const int8_t* weights = &m_weights[o * PADDED_INPUT];
                    for (int i = 0; i < InputDim; ++i)
                    {
                        sum += static_cast<int32_t>(input[i]) *
                               static_cast<int32_t>(weights[i]);
                    }
                }
                #endif

                output[o] = sum;
            }
        }

        // Forward pass with activation (combined for efficiency)
        // @param input: Input activations [InputDim]
        // @param output: Output activations after ReLU [OutputDim]
        void ForwardWithActivation(const uint8_t* input, uint8_t* output) const
        {
            alignas(CACHE_LINE_SIZE) int32_t buffer[OutputDim];
            Forward(input, buffer);
            ApplyClampedRelu(buffer, output, OutputDim);
        }

        // Get raw weight array (for loading)
        int8_t* GetWeights() { return m_weights; }
        const int8_t* GetWeights() const { return m_weights; }

        // Get raw bias array (for loading)
        int32_t* GetBiases() { return m_biases; }
        const int32_t* GetBiases() const { return m_biases; }

    private:
        // Weights stored in row-major order: [OutputDim][PaddedInput]
        // Padded for SIMD alignment
        alignas(CACHE_LINE_SIZE) int8_t m_weights[OutputDim * PADDED_INPUT] = {};

        // Biases for each output neuron
        alignas(CACHE_LINE_SIZE) int32_t m_biases[OutputDim] = {};
    };

    // ========== OUTPUT LAYER ==========
    // Final layer that produces single scalar output
    // Specialized for efficiency with known output size of 1
    template<int InputDim>
    class OutputLayer
    {
    public:
        static constexpr int INPUT_SIZE = InputDim;
        static constexpr int OUTPUT_SIZE = 1;
        static constexpr int PADDED_INPUT = ((InputDim + 31) / 32) * 32;

        OutputLayer() = default;

        void Initialize()
        {
            std::fill(std::begin(m_weights), std::end(m_weights), 0);
            m_bias = 0;
        }

        // Compute final evaluation score
        // @param input: Input activations [InputDim]
        // @return: Raw network output (needs scaling to centipawns)
        int32_t Forward(const uint8_t* input) const
        {
            int32_t sum = m_bias;

            #if defined(NNUE_USE_AVX2)
            {
                __m256i acc = _mm256_setzero_si256();

                for (int i = 0; i + 32 <= InputDim; i += 32)
                {
                    __m256i in = _mm256_loadu_si256(
                        reinterpret_cast<const __m256i*>(input + i));
                    __m256i w = _mm256_loadu_si256(
                        reinterpret_cast<const __m256i*>(m_weights + i));

                    __m256i product = _mm256_maddubs_epi16(in, w);
                    acc = _mm256_add_epi32(acc, _mm256_madd_epi16(product,
                        _mm256_set1_epi16(1)));
                }

                __m128i low = _mm256_castsi256_si128(acc);
                __m128i high = _mm256_extracti128_si256(acc, 1);
                __m128i sum128 = _mm_add_epi32(low, high);
                sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, 0x4E));
                sum128 = _mm_add_epi32(sum128, _mm_shuffle_epi32(sum128, 0xB1));
                sum += _mm_cvtsi128_si32(sum128);

                for (int i = (InputDim / 32) * 32; i < InputDim; ++i)
                {
                    sum += static_cast<int32_t>(input[i]) *
                           static_cast<int32_t>(m_weights[i]);
                }
            }
            #else
            {
                for (int i = 0; i < InputDim; ++i)
                {
                    sum += static_cast<int32_t>(input[i]) *
                           static_cast<int32_t>(m_weights[i]);
                }
            }
            #endif

            return sum;
        }

        int8_t* GetWeights() { return m_weights; }
        const int8_t* GetWeights() const { return m_weights; }

        int32_t& GetBias() { return m_bias; }
        int32_t GetBias() const { return m_bias; }

    private:
        alignas(CACHE_LINE_SIZE) int8_t m_weights[PADDED_INPUT] = {};
        int32_t m_bias = 0;
    };

} // namespace Neural
} // namespace Chess
