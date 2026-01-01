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
        // Called only during initialization when no search is running
        size_t numEntries = (sizeInMB * 1024 * 1024) / sizeof(TTEntry);

        // Round down to nearest power of 2 for efficient indexing
        size_t powerOf2 = 1;
        while (powerOf2 * 2 <= numEntries)
        {
            powerOf2 *= 2;
        }

        m_size = powerOf2;
        m_entries.resize(m_size);

        for (auto& entry : m_entries)
        {
            entry.key = 0;
            entry.score = 0;
            entry.depth = 0;
            entry.flag = TT_EXACT;
            entry.bestMove = Move();
        }
    }

    // Clear all entries in the transposition table
    void TranspositionTable::Clear()
    {
        // Called only when search is not running (guaranteed by AIPlayer::CalculateBestMove)
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
    bool TranspositionTable::Probe(uint64_t key, int depth, int alpha, int beta, int& outScore, Move& outBestMove, int ply)
    {
        size_t index = key & (m_size - 1); // Fast modulo using bitwise AND
        size_t lockIdx = GetLockIndex(index);

        std::lock_guard<std::mutex> lock(m_mutexes[lockIdx]);
        TTEntry& entry = m_entries[index];

        // Check if entry matches current position
        if (entry.key != key)
            return false;

        outBestMove = entry.bestMove;

        // Only use cached score if search depth is sufficient
        if (entry.depth < depth)
            return false;

        int score = entry.score;

        if (score > 28000)
            score -= ply;
        else if (score < -28000)
            score += ply;

        // Exact score from previous search
        if (entry.flag == TT_EXACT)
        {
            outScore = score;
            return true;
        }

        // Upper bound (fail-low) - score <= alpha
        if (entry.flag == TT_ALPHA && score <= alpha)
        {
            outScore = score;
            return true;
        }

        // Lower bound (fail-high) - score >= beta
        if (entry.flag == TT_BETA && score >= beta)
        {
            outScore = score;
            return true;
        }

        return false;
    }

    // Store position evaluation in transposition table
    void TranspositionTable::Store(uint64_t key, int depth, int score, uint8_t flag, Move bestMove, int ply)
    {
        size_t index = key & (m_size - 1); // Fast modulo using bitwise AND
        size_t lockIdx = GetLockIndex(index);

        std::lock_guard<std::mutex> lock(m_mutexes[lockIdx]);
        TTEntry& entry = m_entries[index];

        // Replace-by-depth strategy - prefer deeper searches
        if (entry.key == 0 || depth >= entry.depth)
        {
            int scoreToStore = score;

            if (score > 28000)
                scoreToStore += ply;
            else if (score < -28000)
                scoreToStore -= ply;

            entry.key = key;
            entry.depth = static_cast<int16_t>(depth);
            entry.score = scoreToStore;
            entry.flag = flag;
            entry.bestMove = bestMove;
        }
    }
}