// TranspositionTable.cpp
// Lock-free transposition table implementation.
//
// Concurrency model: Hyatt XOR-key validation.
//   - Each entry holds (keyXorData, data) as two 64-bit atomics.
//   - Writer stores data first, then keyXorData = key XOR data.
//   - Reader loads both, computes loadedKeyXorData XOR loadedData = recoveredKey.
//   - If recoveredKey matches the queried key, the entry is consistent and
//     refers to that position. Any concurrent interleaving produces a mismatch
//     and is treated as a miss.
//
// Memory ordering: relaxed is sufficient. We do not need synchronization-with
// semantics — the XOR check IS the validity check. On x86 all aligned 8-byte
// stores are atomic; on weaker archs the std::atomic<uint64_t> guarantees it.
//
// Cache model: 4-way buckets aligned to 64-byte cache lines. One probe or
// store touches exactly one line. See header for replacement policy.

#include "TranspositionTable.h"

#include <cstring>
#include <cstdlib>
#include <climits>

namespace Chess
{
    TranspositionTable::TranspositionTable()
    {
        Resize(16);  // Default 16 MB
    }

    void TranspositionTable::Resize(size_t sizeInMB)
    {
        size_t numBuckets = (sizeInMB * 1024ULL * 1024ULL) / sizeof(Bucket);
        if (numBuckets < 2) numBuckets = 2;

        size_t powerOf2 = 1;
        while (powerOf2 * 2 <= numBuckets)
            powerOf2 *= 2;

        m_numBuckets = powerOf2;
        // alignas(64) on Bucket makes new[] return cache-line-aligned storage (C++17)
        m_buckets = std::unique_ptr<Bucket[]>(new Bucket[m_numBuckets]);
        Clear();
    }

    void TranspositionTable::Clear()
    {
        if (!m_buckets) return;
        for (size_t i = 0; i < m_numBuckets; ++i)
        {
            for (int j = 0; j < BUCKET_SIZE; ++j)
            {
                m_buckets[i].entries[j].keyXorData.store(0, std::memory_order_relaxed);
                m_buckets[i].entries[j].data.store(0, std::memory_order_relaxed);
            }
        }
    }

    bool TranspositionTable::Probe(uint64_t key, int depth, int alpha, int beta,
                                    int& outScore, Move& outBestMove, int ply)
    {
        uint64_t data;
        if (!FindEntry(key, data))
            return false;

        int storedScore;
        int storedDepth;
        uint8_t flag;
        Move move;
        UnpackData(data, storedScore, storedDepth, flag, move);

        // Always return best move (when key matches) for move ordering.
        outBestMove = move;

        if (storedDepth < depth)
            return false;

        // Adjust mate-distance scores back to current-ply-relative.
        // Mirror the storage criterion: only genuine mate window gets adjusted.
        constexpr int MATE = 29000;
        constexpr int MATE_IN_MAX_PLY = MATE - 250;
        if (storedScore >= MATE_IN_MAX_PLY && storedScore <= MATE + 250)
            storedScore -= ply;
        else if (storedScore <= -MATE_IN_MAX_PLY && storedScore >= -MATE - 250)
            storedScore += ply;

        if (flag == TT_EXACT)
        {
            outScore = storedScore;
            return true;
        }
        if (flag == TT_ALPHA && storedScore <= alpha)
        {
            outScore = storedScore;
            return true;
        }
        if (flag == TT_BETA && storedScore >= beta)
        {
            outScore = storedScore;
            return true;
        }
        return false;
    }

    bool TranspositionTable::ProbeSE(uint64_t key, int& outScore, Move& outBestMove)
    {
        uint64_t data;
        if (!FindEntry(key, data))
            return false;

        int storedScore, storedDepth;
        uint8_t flag;
        Move move;
        UnpackData(data, storedScore, storedDepth, flag, move);

        outScore = storedScore;
        outBestMove = move;
        return true;
    }

    void TranspositionTable::Store(uint64_t key, int depth, int score,
                                    uint8_t flag, Move bestMove, int ply)
    {
        Bucket& bucket = m_buckets[key & (m_numBuckets - 1)];

        // Replacement policy, in priority order:
        //   1. Same position already stored -> refresh that slot (unless the
        //      existing entry is strictly deeper and we bring nothing new).
        //   2. Empty slot.
        //   3. Shallowest entry in the bucket (always replaced - keeps the
        //      table fresh across long games without a separate aging field).
        Entry* victim = nullptr;
        int victimDepth = INT_MAX;

        for (int i = 0; i < BUCKET_SIZE; ++i)
        {
            Entry& e = bucket.entries[i];
            uint64_t kxd = e.keyXorData.load(std::memory_order_relaxed);
            uint64_t data = e.data.load(std::memory_order_relaxed);

            if (kxd == 0 && data == 0)
            {
                // Empty slot - use it unless we find a key match later
                if (victimDepth > -1)
                {
                    victim = &e;
                    victimDepth = -1;
                }
                continue;
            }

            if ((kxd ^ data) == key)
            {
                // Same position: keep the deeper of the two entries
                int existingDepth = static_cast<int>((data >> 48) & 0xFFULL);
                if (existingDepth > depth)
                    return;
                victim = &e;
                break;
            }

            int entryDepth = static_cast<int>((data >> 48) & 0xFFULL);
            if (entryDepth < victimDepth)
            {
                victim = &e;
                victimDepth = entryDepth;
            }
        }

        // Score canonicalization for TT storage:
        //   - True mate scores (|s| in [MATE-250, MATE]) get +/-ply adjustment
        //     so they encode "mate-distance from root" not "from current ply".
        //   - Scores above MATE (e.g. fail-hard returning beta=INFINITY at the
        //     root of an aspiration search) are NOT real mates. Cap them to
        //     MATE-251 so they don't poison future probes by hijacking search
        //     with bogus mate-class evaluations.
        constexpr int MATE = 29000;
        constexpr int MATE_IN_MAX_PLY = MATE - 250;

        int scoreToStore = score;
        if (score > MATE)
            scoreToStore = MATE_IN_MAX_PLY - 1;          // fail-hard overshoot
        else if (score >= MATE_IN_MAX_PLY)
            scoreToStore = score + ply;                  // real mate-for-us
        else if (score < -MATE)
            scoreToStore = -(MATE_IN_MAX_PLY - 1);       // fail-hard overshoot
        else if (score <= -MATE_IN_MAX_PLY)
            scoreToStore = score - ply;                  // real mate-against-us

        uint64_t packed = PackData(scoreToStore, depth, flag, bestMove);

        // Store data first, then key^data. Order is not strictly required by
        // the XOR-check protocol (any interleaving fails validation) but writing
        // data first means a reader is more likely to see a consistent entry.
        victim->data.store(packed, std::memory_order_relaxed);
        victim->keyXorData.store(key ^ packed, std::memory_order_relaxed);
    }
}
