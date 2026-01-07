// Neural.h
// Main include header for NNUE neural network evaluation system
//
// This header provides convenient access to all NNUE components.
// Include this single header to use the neural network evaluation system.
//
// Usage example:
//   #include "Neural/Neural.h"
//
//   Chess::Neural::HybridEvaluator evaluator;
//   evaluator.LoadNnue("nn-small.nnue");
//   int score = evaluator.Evaluate(board);
//
// Components:
// - SimdOperations: Low-level SIMD acceleration
// - Activations: Neural network activation functions
// - DenseLayer: Fully connected network layers
// - FeatureExtractor: Chess position to feature mapping
// - Transformer: Sparse input feature transformation
// - FeatureAccumulator: Incremental update management
// - WeightLoader: Network weight file parsing
// - NeuralEvaluator: Core NNUE evaluation logic
// - HybridEvaluator: Unified classical/NNUE interface

#pragma once

// Core SIMD and math utilities
#include "SimdOperations.h"
#include "Activations.h"

// Network layer implementations
#include "DenseLayer.h"
#include "Transformer.h"

// Feature extraction and accumulator management
#include "FeatureExtractor.h"
#include "FeatureAccumulator.h"

// Weight loading and network interface
#include "WeightLoader.h"
#include "NeuralEvaluator.h"
#include "HybridEvaluator.h"

namespace Chess
{
namespace Neural
{
    // Version information for NNUE implementation
    constexpr int NNUE_VERSION_MAJOR = 1;
    constexpr int NNUE_VERSION_MINOR = 0;

    // Network architecture description
    inline const char* GetArchitectureDescription()
    {
        return "Small NNUE: HalfKA[40960] -> Transformer[128] -> "
               "FC[256->16] -> FC[32->32] -> Output[32->1]";
    }

    // SIMD capability detection
    inline const char* GetSimdCapability()
    {
        #if defined(NNUE_USE_AVX2)
            return "AVX2 (256-bit)";
        #elif defined(NNUE_USE_SSE2)
            return "SSE2 (128-bit)";
        #else
            return "Scalar (no SIMD)";
        #endif
    }

} // namespace Neural
} // namespace Chess
