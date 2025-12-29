// TranspositionTable.h
#pragma once

#include "Move.h"
#include <cstdint>
#include <vector>
#include <mutex>

namespace Chess
{
    // Transposition table entry flags for alpha-beta bounds
    enum TTFlag : uint8_t
    {
        TT_EXACT = 0,  // Exact score (PV node)
        TT_ALPHA = 1,  // Upper bound (all-node)
        TT_BETA = 2    // Lower bound (cut-node)
    };

    // Single entry in transposition table
    struct TTEntry
    {
        uint64_t key = 0;       // Zobrist hash key for position verification
        int score = 0;          // Position evaluation score
        int16_t depth = 0;      // Search depth at which this was evaluated
        uint8_t flag = TT_EXACT; // Bound type for this score
        Move bestMove;          // Best move found for this position
    };

    // Hash table for storing previously evaluated positions
    class TranspositionTable
    {
    public:
        TranspositionTable();

        // Resize table to specified size in megabytes
        void Resize(size_t sizeInMB);
        
        // Clear all entries
        void Clear();

        // Lookup position in table, returns true if usable entry found
        bool Probe(uint64_t key, int depth, int alpha, int beta, int& outScore, Move& outBestMove);
        
        // Store position evaluation in table
        void Store(uint64_t key, int depth, int score, uint8_t flag, Move bestMove);

    private:
        std::vector<TTEntry> m_entries;
        size_t m_size;
        mutable std::mutex m_mutex;
    };
}