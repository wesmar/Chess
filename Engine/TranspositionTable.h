// TranspositionTable.h
// Hash table for caching position evaluations with Zobrist keys
// Stores search results to avoid re-analyzing transpositions
#pragma once

#include "Move.h"
#include <cstdint>
#include <vector>
#include <mutex>
#include <array>

namespace Chess
{
    // ========== TRANSPOSITION TABLE ENTRY FLAGS ==========
    // Indicate type of bound stored for alpha-beta search
    enum TTFlag : uint8_t
    {
        TT_EXACT = 0,  // Exact score (PV node - searched with full window)
        TT_ALPHA = 1,  // Upper bound (all-node - failed low, score <= alpha)
        TT_BETA = 2    // Lower bound (cut-node - failed high, score >= beta)
    };

    // ========== TRANSPOSITION TABLE ENTRY ==========
    // Single cached position evaluation (32 bytes)
    // Stores position hash, score, depth, bound type, and best move
    struct TTEntry
    {
        uint64_t key = 0;       // Zobrist hash key for position verification
        int score = 0;          // Evaluation score (from side-to-move perspective)
        int16_t depth = 0;      // Search depth at which position was evaluated
        uint8_t flag = TT_EXACT; // Bound type (exact, alpha, beta)
        Move bestMove;          // Best move found during search
    };

    // ========== TRANSPOSITION TABLE ==========
    // Hash table for storing position evaluations
    // Uses always-replace with depth-preferred strategy
    // Thread-safe with striped locking for concurrent access
    class TranspositionTable
    {
    public:
        // Constructor - initializes with default 16 MB size
        TranspositionTable();

        // Resize table to specified size in megabytes
        // Clears all entries during resize
        // Size is rounded down to nearest power of 2 for efficient indexing
        // Typical sizes: 16 MB (beginner), 64 MB (intermediate), 256 MB (advanced)
        void Resize(size_t sizeInMB);

        // Clear all entries in table
        // Called at start of new game or position reset
        // NOT thread-safe - must be called when no search is active
        void Clear();

        // Probe table for cached position evaluation
        // Returns true if usable entry found, false otherwise
        // @param key: Zobrist hash of position
        // @param depth: Minimum search depth required
        // @param alpha: Current alpha bound (lower bound)
        // @param beta: Current beta bound (upper bound)
        // @param outScore: [out] Cached score if entry usable
        // @param outBestMove: [out] Best move from cached search (always returned for move ordering)
        // @param ply: Distance from root (for mate score adjustment)
        // @return: true if cache hit with usable score, false otherwise
        //
        // Entry is usable if:
        // 1. Hash key matches (same position)
        // 2. Search depth is sufficient (depth >= requested depth)
        // 3. Bound type is applicable to current alpha/beta window
        bool Probe(uint64_t key, int depth, int alpha, int beta, int& outScore, Move& outBestMove, int ply);

        // Store position evaluation in table
        // Uses always-replace with depth-preferred strategy:
        // - Always replaces empty entries (key == 0)
        // - Replaces existing entries only if new search is deeper or equal depth
        // @param key: Zobrist hash of position
        // @param depth: Search depth at which position was evaluated
        // @param score: Evaluation score (from side-to-move perspective)
        // @param flag: Bound type (TT_EXACT, TT_ALPHA, TT_BETA)
        // @param bestMove: Best move found in this position
        // @param ply: Distance from root (for mate score adjustment)
        //
        // Mate scores are adjusted to be stored relative to root position
        // This ensures mate-in-N is correctly represented at any search depth
        void Store(uint64_t key, int depth, int score, uint8_t flag, Move bestMove, int ply);

    private:
        std::vector<TTEntry> m_entries;  // Entry storage array
        size_t m_size;                   // Number of entries (power of 2)

        // Striped locking for concurrent access
        // Multiple mutexes reduce contention in multi-threaded search
        static constexpr size_t NUM_LOCKS = 128;  // Power of 2 for fast modulo
        std::array<std::mutex, NUM_LOCKS> m_mutexes;

        // Get lock index from entry index for striped locking
        // Uses bitwise AND for fast modulo operation
        size_t GetLockIndex(size_t entryIndex) const {
            return entryIndex & (NUM_LOCKS - 1);
        }
    };
}