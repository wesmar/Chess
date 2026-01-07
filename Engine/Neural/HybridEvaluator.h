// HybridEvaluator.h
// Unified evaluation interface combining NNUE and classical evaluation
//
// This module provides a seamless integration layer that uses NNUE evaluation
// when available and falls back to classical evaluation otherwise. Designed
// for drop-in replacement in existing search code.
//
// Key features:
// - Automatic fallback to classical evaluation
// - Lazy NNUE initialization (load on first use)
// - Search integration hooks (MakeMove/UndoMove)
// - Configurable evaluation mode

#pragma once

#include "NeuralEvaluator.h"
#include "../Evaluation.h"
#include <memory>
#include <string>

namespace Chess
{
namespace Neural
{
    // Evaluation mode selection
    enum class EvalMode
    {
        Auto,       // Use NNUE if loaded, else classical
        NnueOnly,   // Force NNUE (returns 0 if not loaded)
        Classical   // Force classical evaluation
    };

    // ========== HYBRID EVALUATOR ==========

    // Unified evaluation interface supporting both NNUE and classical modes
    class HybridEvaluator
    {
    public:
        HybridEvaluator();
        ~HybridEvaluator() = default;

        // ========== INITIALIZATION ==========

        // Load NNUE network from file
        // @param filename: Path to .nnue file
        // @return: true if loading succeeded
        bool LoadNnue(const std::string& filename);

        // Check if NNUE is available
        bool IsNnueAvailable() const { return m_nnue && m_nnue->IsReady(); }

        // Get NNUE load error message
        const std::string& GetNnueError() const
        {
            static const std::string empty;
            return m_nnue ? m_nnue->GetLastError() : empty;
        }

        // Set evaluation mode
        void SetMode(EvalMode mode) { m_mode = mode; }
        EvalMode GetMode() const { return m_mode; }

        // ========== EVALUATION ==========

        // Evaluate position using configured mode
        // @param board: Position to evaluate
        // @return: Score in centipawns from side-to-move perspective
        int Evaluate(const Board& board);

        // Evaluate with explicit mode override
        int EvaluateClassical(const Board& board);
        int EvaluateNnue(const Board& board);

        // ========== SEARCH INTEGRATION ==========

        // Prepare for new search (reset NNUE accumulator state)
        void PrepareSearch();

        // Called when making a move during search
        void OnMakeMove();

        // Called when undoing a move during search
        void OnUndoMove();

        // ========== STATISTICS ==========

        // Get evaluation statistics
        struct Stats
        {
            uint64_t nnueEvals = 0;
            uint64_t classicalEvals = 0;
        };

        const Stats& GetStats() const { return m_stats; }
        void ResetStats() { m_stats = Stats{}; }

        // ========== DIRECT ACCESS ==========

        // Get underlying NNUE evaluator (may be null)
        NeuralEvaluator* GetNnue() { return m_nnue.get(); }
        const NeuralEvaluator* GetNnue() const { return m_nnue.get(); }

    private:
        std::unique_ptr<NeuralEvaluator> m_nnue;
        EvalMode m_mode = EvalMode::Auto;
        Stats m_stats;
    };

    // ========== IMPLEMENTATION ==========

    inline HybridEvaluator::HybridEvaluator()
        : m_nnue(std::make_unique<NeuralEvaluator>())
    {
    }

    inline bool HybridEvaluator::LoadNnue(const std::string& filename)
    {
        if (!m_nnue)
            m_nnue = std::make_unique<NeuralEvaluator>();

        return m_nnue->LoadNetwork(filename);
    }

    inline int HybridEvaluator::Evaluate(const Board& board)
    {
        switch (m_mode)
        {
        case EvalMode::NnueOnly:
            return EvaluateNnue(board);

        case EvalMode::Classical:
            return EvaluateClassical(board);

        case EvalMode::Auto:
        default:
            if (IsNnueAvailable())
                return EvaluateNnue(board);
            else
                return EvaluateClassical(board);
        }
    }

    inline int HybridEvaluator::EvaluateClassical(const Board& board)
    {
        ++m_stats.classicalEvals;
        return Chess::Evaluate(board);
    }

    inline int HybridEvaluator::EvaluateNnue(const Board& board)
    {
        if (!IsNnueAvailable())
            return 0;

        ++m_stats.nnueEvals;
        return m_nnue->Evaluate(board);
    }

    inline void HybridEvaluator::PrepareSearch()
    {
        if (m_nnue)
            m_nnue->PrepareSearch();
    }

    inline void HybridEvaluator::OnMakeMove()
    {
        if (m_nnue)
            m_nnue->OnMakeMove();
    }

    inline void HybridEvaluator::OnUndoMove()
    {
        if (m_nnue)
            m_nnue->OnUndoMove();
    }

    // ========== GLOBAL INSTANCE ==========

    // Global hybrid evaluator instance for convenience
    // Can be replaced with per-thread instances for multi-threaded search
    inline HybridEvaluator& GetGlobalEvaluator()
    {
        static HybridEvaluator instance;
        return instance;
    }

    // Convenience function matching original Evaluate signature
    inline int EvaluateHybrid(const Board& board)
    {
        return GetGlobalEvaluator().Evaluate(board);
    }

} // namespace Neural
} // namespace Chess
