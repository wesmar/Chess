// Transformer.h
// Feature transformer layer for NNUE evaluation
//
// This module implements the first layer of the NNUE network that transforms
// sparse input features into dense hidden representations. The transformer
// exploits input sparsity (only ~32 of 40960 features active) for efficiency.
//
// Key features:
// - Sparse input handling (only accumulate active feature weights)
// - Incremental updates (add/remove single features)
// - Dual perspective output (white and black views)
// - PSQT (Piece-Square Table) auxiliary output

#pragma once

#include "SimdOperations.h"
#include "FeatureExtractor.h"
#include <cstdint>
#include <cstring>

namespace Chess
{
namespace Neural
{
    // ========== TRANSFORMER DIMENSIONS ==========

    // Small network transformer output size
    constexpr int TRANSFORMER_OUTPUT_DIM = 128;

    // PSQT buckets for auxiliary evaluation
    constexpr int PSQT_BUCKETS = 8;

    // ========== TRANSFORMER CLASS ==========

    // Feature transformer: sparse features -> dense accumulator
    // Template parameter defines output dimensionality
    template<int OutputDim = TRANSFORMER_OUTPUT_DIM>
    class Transformer
    {
    public:
        static constexpr int INPUT_DIM = TOTAL_FEATURES;
        static constexpr int OUTPUT_SIZE = OutputDim;
        static constexpr int PADDED_OUTPUT = ((OutputDim + 15) / 16) * 16;

        Transformer() = default;

        // Initialize weights and biases to zero
        void Initialize()
        {
            std::memset(m_biases, 0, sizeof(m_biases));
            std::memset(m_weights, 0, sizeof(m_weights));
            std::memset(m_psqtWeights, 0, sizeof(m_psqtWeights));
        }

        // Refresh accumulator from scratch using active features
        // Called when position changes significantly (e.g., king moves)
        // @param features: List of active feature indices
        // @param output: Accumulator to populate [OutputDim]
        // @param psqt: PSQT accumulator output [PSQT_BUCKETS]
        void RefreshAccumulator(const FeatureList& features, int16_t* output,
                                 int32_t* psqt) const
        {
            // Start with biases
            CopyAligned(output, m_biases, OutputDim * sizeof(int16_t));
            std::memset(psqt, 0, PSQT_BUCKETS * sizeof(int32_t));

            // Accumulate weights for each active feature
            for (int i = 0; i < features.count; ++i)
            {
                int featureIdx = features.indices[i];
                if (featureIdx < 0 || featureIdx >= INPUT_DIM)
                    continue;

                const int16_t* featureWeights = &m_weights[featureIdx * PADDED_OUTPUT];
                AddVectors(output, featureWeights, OutputDim);

                // Accumulate PSQT
                const int32_t* featurePsqt = &m_psqtWeights[featureIdx * PSQT_BUCKETS];
                for (int b = 0; b < PSQT_BUCKETS; ++b)
                {
                    psqt[b] += featurePsqt[b];
                }
            }
        }

        // Add single feature to accumulator (incremental update)
        // @param featureIdx: Feature index to add
        // @param accumulator: Current accumulator values
        // @param psqt: Current PSQT values
        void AddFeature(int featureIdx, int16_t* accumulator, int32_t* psqt) const
        {
            if (featureIdx < 0 || featureIdx >= INPUT_DIM)
                return;

            const int16_t* featureWeights = &m_weights[featureIdx * PADDED_OUTPUT];
            AddVectors(accumulator, featureWeights, OutputDim);

            const int32_t* featurePsqt = &m_psqtWeights[featureIdx * PSQT_BUCKETS];
            for (int b = 0; b < PSQT_BUCKETS; ++b)
            {
                psqt[b] += featurePsqt[b];
            }
        }

        // Remove single feature from accumulator (incremental update)
        // @param featureIdx: Feature index to remove
        // @param accumulator: Current accumulator values
        // @param psqt: Current PSQT values
        void RemoveFeature(int featureIdx, int16_t* accumulator, int32_t* psqt) const
        {
            if (featureIdx < 0 || featureIdx >= INPUT_DIM)
                return;

            const int16_t* featureWeights = &m_weights[featureIdx * PADDED_OUTPUT];
            SubVectors(accumulator, featureWeights, OutputDim);

            const int32_t* featurePsqt = &m_psqtWeights[featureIdx * PSQT_BUCKETS];
            for (int b = 0; b < PSQT_BUCKETS; ++b)
            {
                psqt[b] -= featurePsqt[b];
            }
        }

        // Move feature: combined remove + add operation
        // More efficient than separate calls when changing piece position
        // @param oldIdx: Feature index being removed
        // @param newIdx: Feature index being added
        // @param accumulator: Current accumulator values
        // @param psqt: Current PSQT values
        void MoveFeature(int oldIdx, int newIdx, int16_t* accumulator, int32_t* psqt) const
        {
            if (oldIdx >= 0 && oldIdx < INPUT_DIM)
            {
                const int16_t* oldWeights = &m_weights[oldIdx * PADDED_OUTPUT];
                SubVectors(accumulator, oldWeights, OutputDim);

                const int32_t* oldPsqt = &m_psqtWeights[oldIdx * PSQT_BUCKETS];
                for (int b = 0; b < PSQT_BUCKETS; ++b)
                    psqt[b] -= oldPsqt[b];
            }

            if (newIdx >= 0 && newIdx < INPUT_DIM)
            {
                const int16_t* newWeights = &m_weights[newIdx * PADDED_OUTPUT];
                AddVectors(accumulator, newWeights, OutputDim);

                const int32_t* newPsqt = &m_psqtWeights[newIdx * PSQT_BUCKETS];
                for (int b = 0; b < PSQT_BUCKETS; ++b)
                    psqt[b] += newPsqt[b];
            }
        }

        // Get pointer to biases (for weight loading)
        int16_t* GetBiases() { return m_biases; }
        const int16_t* GetBiases() const { return m_biases; }

        // Get pointer to weight matrix (for weight loading)
        // Layout: weights[featureIdx][outputIdx]
        int16_t* GetWeights() { return m_weights; }
        const int16_t* GetWeights() const { return m_weights; }

        // Get pointer to PSQT weights (for weight loading)
        int32_t* GetPsqtWeights() { return m_psqtWeights; }
        const int32_t* GetPsqtWeights() const { return m_psqtWeights; }

        // Get dimensions for weight loading
        static constexpr int GetInputDim() { return INPUT_DIM; }
        static constexpr int GetOutputDim() { return OutputDim; }
        static constexpr int GetPaddedOutputDim() { return PADDED_OUTPUT; }

    private:
        // Biases: one per output dimension
        alignas(CACHE_LINE_SIZE) int16_t m_biases[PADDED_OUTPUT] = {};

        // Weights: sparse lookup table indexed by feature
        // Layout enables efficient single-feature accumulation
        // Size: TOTAL_FEATURES * PADDED_OUTPUT * sizeof(int16_t) ≈ 10 MB for small net
        alignas(CACHE_LINE_SIZE) int16_t m_weights[TOTAL_FEATURES * PADDED_OUTPUT] = {};

        // PSQT weights for auxiliary piece-square evaluation
        alignas(CACHE_LINE_SIZE) int32_t m_psqtWeights[TOTAL_FEATURES * PSQT_BUCKETS] = {};
    };

    // Type alias for standard small network transformer
    using SmallTransformer = Transformer<TRANSFORMER_OUTPUT_DIM>;

} // namespace Neural
} // namespace Chess
