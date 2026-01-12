// Evaluation.h
// Position evaluation interface for chess engine
// Provides static evaluation scoring and piece value constants
#pragma once

#include "Board.h"
#include <vector>
#include <cstdint>

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
    // Thread-safe for single-threaded use; use thread_local for multi-threading
    
    struct EvalCacheEntry
    {
        uint64_t key = 0;      // Zobrist key of cached position
        int score = 0;         // Evaluation score for this position
        uint8_t age = 0;       // Generation counter for cache aging
    };

    class EvalCache
    {
    public:
        // Initialize cache to default size (disabled)
        EvalCache() = default;

        // Resize cache to specified size in megabytes
        // Size of 0 disables caching (default behavior)
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
        [[nodiscard]] size_t GetSize() const noexcept { return m_table.size(); }

        // Check if cache is enabled (non-zero size)
        [[nodiscard]] bool IsEnabled() const noexcept { return !m_table.empty(); }

    private:
        std::vector<EvalCacheEntry> m_table;
        uint8_t m_generation = 0;
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
    
    // Get positional bonus from piece-square table
    // Returns bonus/penalty for piece placement on specific square
    // PST values encourage good piece placement (center control, king safety, etc.)
    int GetPSTValue(PieceType type, int square, PlayerColor color);

    // Compute game phase as continuous value from 0 (endgame) to 256 (opening)
    // Based on remaining material with weights: Q=4, R=2, B=1, N=1
    // Used for tapered evaluation (interpolating between middlegame and endgame scores)
    // Returns: 256 at start (full material), 0 in bare king endgame
    int ComputePhase(const Board& board);

    // ========== GLOBAL EVALUATION CACHE ==========
    // Single global cache instance for evaluation memoization
    // Can be resized/cleared via AIPlayer API methods
    // Default state: disabled (zero size)
    extern EvalCache g_evalCache;
}