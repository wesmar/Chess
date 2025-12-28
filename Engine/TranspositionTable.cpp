// TranspositionTable.cpp
#include "TranspositionTable.h"

namespace Chess
{
    // Constructor - initialize with default size
    TranspositionTable::TranspositionTable()
        : m_size(0)
    {
        Resize(16); // Default 16 MB
    }

    // Resize transposition table to specified size in megabytes
    void TranspositionTable::Resize(size_t sizeInMB)
    {
        size_t numEntries = (sizeInMB * 1024 * 1024) / sizeof(TTEntry);

        // Round down to nearest power of 2 for efficient indexing
        size_t powerOf2 = 1;
        while (powerOf2 * 2 <= numEntries)
        {
            powerOf2 *= 2;
        }

        m_size = powerOf2;
        m_entries.resize(m_size);
        Clear();
    }

    // Clear all entries in the transposition table
    void TranspositionTable::Clear()
    {
        for (auto& entry : m_entries)
        {
            entry.key = 0;
            entry.score = 0;
            entry.depth = 0;
            entry.flag = TT_EXACT;
            entry.bestMove = Move();
        }
    }

    // Probe transposition table for cached position evaluation
    bool TranspositionTable::Probe(uint64_t key, int depth, int alpha, int beta, int& outScore, Move& outBestMove)
    {
        size_t index = key & (m_size - 1); // Fast modulo using bitwise AND
        TTEntry& entry = m_entries[index];

        // Check if entry matches current position
        if (entry.key != key)
            return false;

        outBestMove = entry.bestMove;

        // Only use cached score if search depth is sufficient
        if (entry.depth < depth)
            return false;

        // Return cached score based on bound type
        if (entry.flag == TT_EXACT)
        {
            outScore = entry.score;
            return true;
        }

        // Alpha bound (upper bound) - score is at most this value
        if (entry.flag == TT_ALPHA && entry.score <= alpha)
        {
            outScore = alpha;
            return true;
        }

        // Beta bound (lower bound) - score is at least this value
        if (entry.flag == TT_BETA && entry.score >= beta)
        {
            outScore = beta;
            return true;
        }

        return false;
    }

    // Store position evaluation in transposition table
    void TranspositionTable::Store(uint64_t key, int depth, int score, uint8_t flag, Move bestMove)
    {
        size_t index = key & (m_size - 1); // Fast modulo using bitwise AND
        TTEntry& entry = m_entries[index];

        // Replace entry if empty or if new search is deeper
        if (entry.key == 0 || depth >= entry.depth)
        {
            entry.key = key;
            entry.depth = static_cast<int16_t>(depth);
            entry.score = score;
            entry.flag = flag;
            entry.bestMove = bestMove;
        }
    }
}