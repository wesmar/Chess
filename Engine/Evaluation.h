// Evaluation.h
// Position evaluation interface for chess engine
// Provides static evaluation scoring and piece value constants
#pragma once

#include "Board.h"
#include <vector>
#include <cstdint>
#include <atomic>
#include <memory>

namespace Chess
{
    // ========== PIECE VALUES (CENTIPAWNS) ==========
    // Standard material values used throughout the engine
    // Centipawn scale: 100 = one pawn
    constexpr int PAWN_VALUE = 100;
    constexpr int KNIGHT_VALUE = 320;
    constexpr int BISHOP_VALUE = 330;  // Slightly better than knight (bishop pair bonus)
    constexpr int ROOK_VALUE = 500;
    constexpr int QUEEN_VALUE = 900;
    constexpr int KING_VALUE = 20000;  // Effectively infinite (losing king = losing game)

    // ========== SEARCH BOUNDS ==========
    // Special score values for search algorithm
    constexpr int MATE_SCORE = 29000;      // Checkmate score threshold
    constexpr int INFINITY_SCORE = 31000;  // Alpha-beta infinity bound

    // ========== EVALUATION CACHE ==========
    // Optional cache for position evaluations to reduce redundant computation
    // Stores evaluation results keyed by Zobrist hash
    // Concurrent probes/stores use atomic XOR validation. Resize/Clear require idle search.
    
    struct EvalCacheEntry
    {
        std::atomic<uint64_t> keyXorData{0};
        std::atomic<uint64_t> data{0}; // generation and signed score
    };

    class EvalCache
    {
    public:
        // Small shared cache, independent of the UCI transposition-table Hash setting.
        // Default size in MB. One megabyte is 65536 entries of 16 bytes, which a
        // search doing hundreds of thousands of evaluations per second recycles
        // many times over. The cache returns exactly the value a full evaluation
        // would have produced, so its size is a pure speed knob: the search tree
        // is identical either way.
        //
        // Measured, and it is not a lever. Trees stayed identical at 4, 16 and 64
        // MB, as expected, but so did the clock: +1.0% on two positions at depth
        // 10 and -0.7% across 50 openings at depth 8, where the larger table's
        // worse locality cancelled its higher hit rate. One megabyte stays.
        static constexpr size_t DEFAULT_SIZE_MB = 1;
        EvalCache() { Resize(DEFAULT_SIZE_MB); }

        // Resize cache to specified size in megabytes
        // Size of 0 disables caching. Call only while all search threads are idle.
        void Resize(size_t sizeMB);

        // Attempt to retrieve cached evaluation score
        // Returns true if position found in cache, false otherwise
        // On success, writes score to output parameter
        bool Probe(uint64_t key, int& score) const;

        // Store evaluation score for given position
        // Uses Zobrist key for fast lookup
        void Store(uint64_t key, int score);

        // Clear cache by incrementing generation counter
        // Invalidates all entries without zeroing memory
        void Clear();

        // Get current cache size in entries
        [[nodiscard]] size_t GetSize() const noexcept { return m_size; }

        // Check if cache is enabled (non-zero size)
        [[nodiscard]] bool IsEnabled() const noexcept { return m_size != 0; }

    private:
        std::unique_ptr<EvalCacheEntry[]> m_table;
        size_t m_size = 0;
        uint32_t m_generation = 1;
    };

    // ========== EVALUATION FUNCTIONS ==========
    
    // Main evaluation function - comprehensive position assessment
    // Combines material, PST, king safety, mobility, pawn structure, passed pawns
    // Uses tapered eval to interpolate between middlegame and endgame
    // Returns score from side-to-move perspective (positive = good for side to move)
    // Optionally uses evaluation cache if provided and enabled
    int Evaluate(const Board& board);
    
    // Get base material value for piece type
    // Returns centipawn value without positional considerations
    int GetPieceValue(PieceType type);
    
    // Get middlegame positional bonus from piece-square table
    int GetPSTValue(PieceType type, int square, PlayerColor color);

    // Get endgame positional bonus from piece-square table
    int GetEGPSTValue(PieceType type, int square, PlayerColor color);

    // Compute game phase as continuous value from 0 (endgame) to 256 (opening)
    // Based on remaining material with weights: Q=4, R=2, B=1, N=1
    // Used for tapered evaluation (interpolating between middlegame and endgame scores)
    // Returns: 256 at start (full material), 0 in bare king endgame
    int ComputePhase(const Board& board);

    // ========== GLOBAL EVALUATION CACHE ==========
    // Single global cache instance for evaluation memoization
    // Can be resized/cleared via AIPlayer API methods
    // Default size: 1 MB, shared by the main search and its helpers.
    extern EvalCache g_evalCache;
}
