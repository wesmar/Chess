// TranspositionTable.cpp
// Hash table implementation for caching chess position evaluations
//
// The transposition table (TT) is a critical optimization for chess engines.
// It stores previously evaluated positions to avoid re-searching identical positions
// that can be reached through different move orders (transpositions).
//
// Key concepts:
// - ZOBRIST HASHING: Each position has unique 64-bit hash key
// - REPLACEMENT STRATEGY: Always-replace with depth preference
// - BOUND TYPES: Exact scores, alpha bounds (upper), beta bounds (lower)
// - MATE SCORE ADJUSTMENT: Store mate distance from root, not current position
// - THREAD SAFETY: Striped locking for concurrent access
//
// Performance impact:
// - Reduces search tree by 10-100x in typical positions
// - Stores best move for move ordering (PV move first)
// - Essential for iterative deepening to work effectively
//
// Memory usage:
// - Each entry: 32 bytes (key + score + depth + flags + move)
// - Typical size: 16-256 MB (500K - 8M entries)

#include "TranspositionTable.h"

namespace Chess
{
    // Constructor - initialize with default size
    // Default 16 MB provides good performance without excessive memory use
    TranspositionTable::TranspositionTable()
        : m_size(0)
    {
        Resize(16); // Default 16 MB
    }

    // Resize transposition table to specified size in megabytes
    // This function is called only during initialization or between games
    // Never called during active search (guaranteed by AIPlayer)
    //
    // Uses power-of-2 sizing for efficient modulo operations:
    // index = hash & (size - 1) is faster than hash % size
    void TranspositionTable::Resize(size_t sizeInMB)
    {
        // Calculate number of entries that fit in requested memory
        size_t numEntries = (sizeInMB * 1024 * 1024) / sizeof(TTEntry);

        // Round down to nearest power of 2 for efficient indexing
        // This allows using bitwise AND instead of modulo for hash indexing
        size_t powerOf2 = 1;
        while (powerOf2 * 2 <= numEntries)
        {
            powerOf2 *= 2;
        }

        m_size = powerOf2;
        m_entries.resize(m_size);

        // Initialize all entries to empty state
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
    // Called at start of new game or when position is reset
    // NOT thread-safe - must only be called when no search is active
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
    // Returns true if usable entry found, false otherwise
    //
    // Entry is usable if:
    // 1. Hash key matches (same position)
    // 2. Search depth is sufficient (depth >= requested depth)
    // 3. Bound type is applicable to current alpha/beta window
    //
    // Parameters:
    // - key: Zobrist hash of position
    // - depth: Minimum search depth required
    // - alpha, beta: Current search window
    // - outScore: [out] Cached score if entry usable
    // - outBestMove: [out] Best move from cached search
    // - ply: Distance from root (for mate score adjustment)
    //
    // Returns: true if cache hit with usable data
    bool TranspositionTable::Probe(uint64_t key, int depth, int alpha, int beta, 
                                    int& outScore, Move& outBestMove, int ply)
    {
        size_t index = key & (m_size - 1); // Fast modulo using bitwise AND
        size_t lockIdx = GetLockIndex(index);

        // Lock this entry's stripe for thread-safe access
        std::lock_guard<std::mutex> lock(m_mutexes[lockIdx]);
        TTEntry& entry = m_entries[index];

        // Check if entry matches current position
        if (entry.key != key)
            return false; // Hash collision or empty entry

        // Always return best move for move ordering even if depth insufficient
        outBestMove = entry.bestMove;

        // Only use cached score if search depth is sufficient
        // Shallow searches can't replace deep searches
        if (entry.depth < depth)
            return false;

        int score = entry.score;

        // Adjust mate scores from table perspective to current position perspective
        // Mate scores are stored relative to root, must adjust for current ply
        // This ensures mate-in-N is correctly represented at any point in search tree
        if (score > 28000) // Mate score for us
            score -= ply;  // Closer to mate at deeper ply
        else if (score < -28000) // Mate score for opponent
            score += ply;  // Further from mate at deeper ply

        // Check if cached bound type is useful for current search window
        
        // Exact score from previous search at this depth
        if (entry.flag == TT_EXACT)
        {
            outScore = score;
            return true;
        }

        // Upper bound (fail-low) - actual score <= cached score
        // Useful if cached score <= alpha (confirms fail-low)
        if (entry.flag == TT_ALPHA && score <= alpha)
        {
            outScore = score;
            return true;
        }

        // Lower bound (fail-high) - actual score >= cached score
        // Useful if cached score >= beta (confirms fail-high)
        if (entry.flag == TT_BETA && score >= beta)
        {
            outScore = score;
            return true;
        }

        return false; // Bound type not useful for current window
    }

    // Store position evaluation in transposition table
    // Uses always-replace with depth-preferred strategy
    //
    // Parameters:
    // - key: Zobrist hash of position
    // - depth: Search depth at which position was evaluated
    // - score: Evaluation score (from side-to-move perspective)
    // - flag: Bound type (TT_EXACT, TT_ALPHA, TT_BETA)
    // - bestMove: Best move found in this position
    // - ply: Distance from root (for mate score adjustment)
    //
    // Replacement strategy:
    // - Always replace if entry is empty (key == 0)
    // - Always replace if new search is deeper (depth >= old depth)
    // - This prefers keeping deeply-searched positions over shallow ones
    void TranspositionTable::Store(uint64_t key, int depth, int score, 
                                    uint8_t flag, Move bestMove, int ply)
    {
        size_t index = key & (m_size - 1); // Fast modulo using bitwise AND
        size_t lockIdx = GetLockIndex(index);

        // Lock this entry's stripe for thread-safe access
        std::lock_guard<std::mutex> lock(m_mutexes[lockIdx]);
        TTEntry& entry = m_entries[index];

        // Replace-by-depth strategy: prefer deeper searches
        // Empty entries (key == 0) are always replaced
        // Existing entries only replaced if new search is deeper
        if (entry.key == 0 || depth >= entry.depth)
        {
            int scoreToStore = score;

            // Adjust mate scores from current position to root perspective
            // Store mate distance from root so it's consistent across tree
            if (score > 28000) // Mate score for us
                scoreToStore += ply;  // Store as "mate in N from root"
            else if (score < -28000) // Mate score for opponent
                scoreToStore -= ply;  // Store as "mated in N from root"

            // Write new entry
            entry.key = key;
            entry.depth = static_cast<int16_t>(depth);
            entry.score = scoreToStore;
            entry.flag = flag;
            entry.bestMove = bestMove;
        }
        // Otherwise keep existing entry (deeper search already cached)
    }
}