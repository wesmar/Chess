// FeatureAccumulator.h
// Incremental accumulator state management for NNUE
//
// This module manages the transformer output state (accumulator) that gets
// incrementally updated as pieces move. Maintains separate accumulators for
// both perspectives (white and black) to avoid recomputation during search.
//
// Key features:
// - Cache-line aligned storage for optimal memory access
// - Dirty flags for lazy refresh detection
// - Stack-based state for search tree traversal
// - Support for both incremental and full refresh modes

#pragma once

#include "SimdOperations.h"
#include "Transformer.h"
#include "FeatureExtractor.h"
#include <cstdint>
#include <cstring>
#include <array>

namespace Chess
{
namespace Neural
{
    // Maximum search depth for accumulator stack
    constexpr int MAX_ACCUMULATOR_STACK = 128;

    // ========== SINGLE ACCUMULATOR STATE ==========

    // Stores transformer output for one perspective
    // Cache-aligned for efficient SIMD operations
    template<int Dim = TRANSFORMER_OUTPUT_DIM>
    struct AccumulatorState
    {
        // Transformer output values
        alignas(CACHE_LINE_SIZE) int16_t values[Dim] = {};

        // PSQT accumulator for auxiliary evaluation
        alignas(CACHE_LINE_SIZE) int32_t psqt[PSQT_BUCKETS] = {};

        // Whether this accumulator needs refresh
        bool dirty = true;

        // Copy state from another accumulator
        void CopyFrom(const AccumulatorState& other)
        {
            CopyAligned(values, other.values, sizeof(values));
            CopyAligned(psqt, other.psqt, sizeof(psqt));
            dirty = other.dirty;
        }

        // Mark as needing full refresh
        void Invalidate()
        {
            dirty = true;
        }

        // Clear to zero (used before refresh)
        void Clear()
        {
            std::memset(values, 0, sizeof(values));
            std::memset(psqt, 0, sizeof(psqt));
            dirty = true;
        }
    };

    // ========== DUAL PERSPECTIVE ACCUMULATOR ==========

    // Combined accumulator for both white and black perspectives
    // Used as a single unit during position evaluation
    template<int Dim = TRANSFORMER_OUTPUT_DIM>
    struct DualAccumulator
    {
        AccumulatorState<Dim> white;
        AccumulatorState<Dim> black;

        // Get accumulator for specified perspective
        AccumulatorState<Dim>& Get(PlayerColor color)
        {
            return (color == PlayerColor::White) ? white : black;
        }

        const AccumulatorState<Dim>& Get(PlayerColor color) const
        {
            return (color == PlayerColor::White) ? white : black;
        }

        // Check if either perspective needs refresh
        bool NeedsRefresh() const
        {
            return white.dirty || black.dirty;
        }

        // Check if specific perspective needs refresh
        bool NeedsRefresh(PlayerColor color) const
        {
            return Get(color).dirty;
        }

        // Invalidate both perspectives (e.g., after king move)
        void InvalidateBoth()
        {
            white.Invalidate();
            black.Invalidate();
        }

        // Copy from another dual accumulator
        void CopyFrom(const DualAccumulator& other)
        {
            white.CopyFrom(other.white);
            black.CopyFrom(other.black);
        }
    };

    // ========== ACCUMULATOR STACK ==========

    // Stack of accumulators for search tree traversal
    // Allows efficient state restoration after move undo
    template<int Dim = TRANSFORMER_OUTPUT_DIM>
    class AccumulatorStack
    {
    public:
        AccumulatorStack() : m_ply(0) {}

        // Get current accumulator (top of stack)
        DualAccumulator<Dim>& Current()
        {
            return m_stack[m_ply];
        }

        const DualAccumulator<Dim>& Current() const
        {
            return m_stack[m_ply];
        }

        // Get accumulator at specific ply
        DualAccumulator<Dim>& At(int ply)
        {
            return m_stack[ply];
        }

        const DualAccumulator<Dim>& At(int ply) const
        {
            return m_stack[ply];
        }

        // Push new state (copy current to next level)
        // Called before making a move in search
        void Push()
        {
            if (m_ply + 1 < MAX_ACCUMULATOR_STACK)
            {
                m_stack[m_ply + 1].CopyFrom(m_stack[m_ply]);
                ++m_ply;
            }
        }

        // Pop state (restore previous level)
        // Called after undoing a move in search
        void Pop()
        {
            if (m_ply > 0)
                --m_ply;
        }

        // Reset to root position
        void Reset()
        {
            m_ply = 0;
            m_stack[0].InvalidateBoth();
        }

        // Get current ply depth
        int GetPly() const { return m_ply; }

        // Direct access to white/black accumulators at current ply
        AccumulatorState<Dim>& WhiteAccumulator()
        {
            return m_stack[m_ply].white;
        }

        AccumulatorState<Dim>& BlackAccumulator()
        {
            return m_stack[m_ply].black;
        }

    private:
        std::array<DualAccumulator<Dim>, MAX_ACCUMULATOR_STACK> m_stack;
        int m_ply;
    };

    // ========== ACCUMULATOR MANAGER ==========

    // High-level manager that coordinates accumulator updates with transformer
    // Handles decision between incremental update and full refresh
    template<int Dim = TRANSFORMER_OUTPUT_DIM>
    class AccumulatorManager
    {
    public:
        AccumulatorManager() = default;

        // Set transformer reference (must be called before use)
        void SetTransformer(const Transformer<Dim>* transformer)
        {
            m_transformer = transformer;
        }

        // Get accumulator stack for search
        AccumulatorStack<Dim>& GetStack() { return m_stack; }
        const AccumulatorStack<Dim>& GetStack() const { return m_stack; }

        // Refresh accumulator from scratch for given position
        // @param board: Current board state
        // @param perspective: Which perspective to refresh
        void RefreshAccumulator(const Board& board, PlayerColor perspective)
        {
            if (!m_transformer)
                return;

            FeatureList features;
            FeatureExtractor::ExtractFeatures(board, perspective, features);

            auto& acc = m_stack.Current().Get(perspective);
            m_transformer->RefreshAccumulator(features, acc.values, acc.psqt);
            acc.dirty = false;
        }

        // Refresh both perspectives
        void RefreshBoth(const Board& board)
        {
            if (!m_transformer)
                return;

            FeatureList whiteFeatures, blackFeatures;
            FeatureExtractor::ExtractBothPerspectives(board, whiteFeatures, blackFeatures);

            auto& whiteAcc = m_stack.Current().white;
            auto& blackAcc = m_stack.Current().black;

            m_transformer->RefreshAccumulator(whiteFeatures, whiteAcc.values, whiteAcc.psqt);
            m_transformer->RefreshAccumulator(blackFeatures, blackAcc.values, blackAcc.psqt);

            whiteAcc.dirty = false;
            blackAcc.dirty = false;
        }

        // Ensure accumulator is up-to-date for evaluation
        // Performs refresh if dirty flag is set
        void EnsureReady(const Board& board, PlayerColor perspective)
        {
            if (m_stack.Current().NeedsRefresh(perspective))
            {
                RefreshAccumulator(board, perspective);
            }
        }

        // Ensure both perspectives are ready
        void EnsureBothReady(const Board& board)
        {
            if (m_stack.Current().NeedsRefresh())
            {
                RefreshBoth(board);
            }
        }

        // Update accumulator incrementally after a move
        // Returns false if full refresh is needed (e.g., king moved)
        bool UpdateIncremental(const Board& board, const Move& move,
                                PlayerColor perspective)
        {
            if (!m_transformer)
                return false;

            auto& acc = m_stack.Current().Get(perspective);

            // Check if king moved - requires full refresh
            Piece movedPiece = board.GetPieceAt(move.GetTo());
            if (movedPiece.GetType() == PieceType::King &&
                movedPiece.GetColor() == perspective)
            {
                acc.Invalidate();
                return false;
            }

            FeatureList added, removed;
            FeatureExtractor::ComputeFeatureChanges(board, move, perspective,
                                                     added, removed);

            // Empty lists indicate need for full refresh
            if (added.count == 0 && removed.count == 0)
            {
                acc.Invalidate();
                return false;
            }

            // Apply incremental updates
            for (int i = 0; i < removed.count; ++i)
            {
                m_transformer->RemoveFeature(removed.indices[i], acc.values, acc.psqt);
            }

            for (int i = 0; i < added.count; ++i)
            {
                m_transformer->AddFeature(added.indices[i], acc.values, acc.psqt);
            }

            return true;
        }

        // Prepare for move in search tree
        void PrepareMove()
        {
            m_stack.Push();
        }

        // Restore after move undo in search tree
        void UndoMove()
        {
            m_stack.Pop();
        }

        // Reset to initial state
        void Reset()
        {
            m_stack.Reset();
        }

    private:
        AccumulatorStack<Dim> m_stack;
        const Transformer<Dim>* m_transformer = nullptr;
    };

    // Type alias for standard small network manager
    using SmallAccumulatorManager = AccumulatorManager<TRANSFORMER_OUTPUT_DIM>;

} // namespace Neural
} // namespace Chess
