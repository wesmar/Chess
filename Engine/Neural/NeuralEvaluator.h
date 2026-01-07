// NeuralEvaluator.h
// Main NNUE evaluation interface for chess engine
//
// This module provides the primary interface for neural network-based position
// evaluation. Coordinates all NNUE components (transformer, layers, accumulator)
// to produce evaluation scores compatible with the existing search framework.
//
// Key features:
// - Drop-in replacement for classical evaluation
// - Automatic accumulator management during search
// - Hybrid mode support (fallback to classical when NNUE unavailable)
// - Thread-safe evaluation with per-thread accumulators

#pragma once

#include "Transformer.h"
#include "DenseLayer.h"
#include "FeatureAccumulator.h"
#include "WeightLoader.h"
#include "../Board.h"
#include <memory>
#include <string>

namespace Chess
{
namespace Neural
{
    // ========== NETWORK DIMENSIONS ==========

    // Small network architecture: 128 -> 16 -> 32 -> 32 -> 1
    constexpr int SMALL_TRANSFORMER_DIM = 128;
    constexpr int SMALL_LAYER1_IN = 256;   // 128 * 2 perspectives
    constexpr int SMALL_LAYER1_OUT = 16;
    constexpr int SMALL_LAYER2_IN = 32;    // 16 * 2 (paired activation)
    constexpr int SMALL_LAYER2_OUT = 32;
    constexpr int SMALL_OUTPUT_IN = 32;

    // Output scaling to convert network output to centipawns
    constexpr int NNUE_OUTPUT_SCALE = 16;
    constexpr int NNUE_EVAL_SCALE = 400;  // Scale factor for final output

    // ========== NEURAL EVALUATOR ==========

    // Main NNUE evaluation class
    // Manages network weights and provides evaluation interface
    class NeuralEvaluator
    {
    public:
        NeuralEvaluator();
        ~NeuralEvaluator() = default;

        // Non-copyable (owns large weight arrays)
        NeuralEvaluator(const NeuralEvaluator&) = delete;
        NeuralEvaluator& operator=(const NeuralEvaluator&) = delete;

        // Movable
        NeuralEvaluator(NeuralEvaluator&&) = default;
        NeuralEvaluator& operator=(NeuralEvaluator&&) = default;

        // ========== INITIALIZATION ==========

        // Load network weights from .nnue file
        // @param filename: Path to Stockfish .nnue file
        // @return: true if loading succeeded
        bool LoadNetwork(const std::string& filename);

        // Check if network is loaded and ready
        bool IsReady() const { return m_loaded; }

        // Get last error message (if loading failed)
        const std::string& GetLastError() const { return m_lastError; }

        // ========== EVALUATION ==========

        // Evaluate position using NNUE
        // @param board: Position to evaluate
        // @return: Evaluation in centipawns from side-to-move perspective
        int Evaluate(const Board& board);

        // Evaluate with explicit accumulator (for search integration)
        // @param board: Position to evaluate
        // @param accManager: Accumulator manager with current state
        // @return: Evaluation in centipawns
        int EvaluateWithAccumulator(const Board& board,
                                     AccumulatorManager<SMALL_TRANSFORMER_DIM>& accManager);

        // ========== SEARCH INTEGRATION ==========

        // Get accumulator manager for search thread
        // Each search thread should have its own manager
        AccumulatorManager<SMALL_TRANSFORMER_DIM>& GetAccumulatorManager()
        {
            return m_accManager;
        }

        // Prepare for new search (reset accumulator state)
        void PrepareSearch()
        {
            m_accManager.Reset();
        }

        // Notify of move being made in search
        void OnMakeMove()
        {
            m_accManager.PrepareMove();
        }

        // Notify of move being undone in search
        void OnUndoMove()
        {
            m_accManager.UndoMove();
        }

        // ========== NETWORK ACCESS ==========

        // Get transformer (for external accumulator management)
        const SmallTransformer& GetTransformer() const { return m_transformer; }

    private:
        // Forward pass through network layers
        // @param whiteAcc: White perspective accumulator values
        // @param blackAcc: Black perspective accumulator values
        // @param sideToMove: Current side to move
        // @return: Raw network output
        int32_t ForwardPass(const int16_t* whiteAcc, const int16_t* blackAcc,
                            PlayerColor sideToMove);

        // Scale network output to centipawns
        int ScaleOutput(int32_t rawOutput);

        // Network components
        SmallTransformer m_transformer;
        DenseLayer<SMALL_LAYER1_IN, SMALL_LAYER1_OUT> m_layer1;
        DenseLayer<SMALL_LAYER2_IN, SMALL_LAYER2_OUT> m_layer2;
        OutputLayer<SMALL_OUTPUT_IN> m_outputLayer;

        // Accumulator management
        AccumulatorManager<SMALL_TRANSFORMER_DIM> m_accManager;

        // State
        bool m_loaded = false;
        std::string m_lastError;
    };

    // ========== IMPLEMENTATION ==========

    inline NeuralEvaluator::NeuralEvaluator()
    {
        m_transformer.Initialize();
        m_layer1.Initialize();
        m_layer2.Initialize();
        m_outputLayer.Initialize();
        m_accManager.SetTransformer(&m_transformer);
    }

    inline bool NeuralEvaluator::LoadNetwork(const std::string& filename)
    {
        WeightLoader loader;
        WeightLoader::LoadResult result = loader.LoadSmallNetwork(
            filename, m_transformer, m_layer1, m_layer2, m_outputLayer);

        if (result != WeightLoader::LoadResult::Success)
        {
            m_lastError = WeightLoader::GetErrorMessage(result);
            m_loaded = false;
            return false;
        }

        m_loaded = true;
        m_lastError.clear();
        return true;
    }

    inline int NeuralEvaluator::Evaluate(const Board& board)
    {
        if (!m_loaded)
            return 0;

        // Ensure accumulators are up to date
        m_accManager.EnsureBothReady(board);

        auto& current = m_accManager.GetStack().Current();
        int32_t raw = ForwardPass(current.white.values, current.black.values,
                                   board.GetSideToMove());

        return ScaleOutput(raw);
    }

    inline int NeuralEvaluator::EvaluateWithAccumulator(
        const Board& board,
        AccumulatorManager<SMALL_TRANSFORMER_DIM>& accManager)
    {
        if (!m_loaded)
            return 0;

        accManager.EnsureBothReady(board);

        auto& current = accManager.GetStack().Current();
        int32_t raw = ForwardPass(current.white.values, current.black.values,
                                   board.GetSideToMove());

        return ScaleOutput(raw);
    }

    inline int32_t NeuralEvaluator::ForwardPass(const int16_t* whiteAcc,
                                                  const int16_t* blackAcc,
                                                  PlayerColor sideToMove)
    {
        // Combine perspectives based on side to move
        // Side to move goes first, opponent second
        alignas(CACHE_LINE_SIZE) uint8_t input[SMALL_LAYER1_IN];

        const int16_t* stmAcc = (sideToMove == PlayerColor::White) ? whiteAcc : blackAcc;
        const int16_t* oppAcc = (sideToMove == PlayerColor::White) ? blackAcc : whiteAcc;

        // Apply squared clamped ReLU to both perspectives
        CombinePerspectives(stmAcc, oppAcc, input, SMALL_TRANSFORMER_DIM);

        // Layer 1: 256 -> 16
        alignas(CACHE_LINE_SIZE) uint8_t hidden1[SMALL_LAYER1_OUT];
        m_layer1.ForwardWithActivation(input, hidden1);

        // Prepare input for layer 2 (paired activation)
        alignas(CACHE_LINE_SIZE) uint8_t hidden1_paired[SMALL_LAYER2_IN];
        for (int i = 0; i < SMALL_LAYER1_OUT; ++i)
        {
            hidden1_paired[i] = hidden1[i];
            hidden1_paired[i + SMALL_LAYER1_OUT] = hidden1[i];
        }

        // Layer 2: 32 -> 32
        alignas(CACHE_LINE_SIZE) uint8_t hidden2[SMALL_LAYER2_OUT];
        m_layer2.ForwardWithActivation(hidden1_paired, hidden2);

        // Output layer: 32 -> 1
        return m_outputLayer.Forward(hidden2);
    }

    inline int NeuralEvaluator::ScaleOutput(int32_t rawOutput)
    {
        // Scale to centipawns
        // The exact scaling depends on training data normalization
        return (rawOutput * NNUE_EVAL_SCALE) / (NNUE_OUTPUT_SCALE * 64);
    }

} // namespace Neural
} // namespace Chess
