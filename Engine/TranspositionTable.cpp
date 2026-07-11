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
        //   1. Same position already stored -> refresh that slot (a strictly
        //      deeper existing entry is kept, but its generation is renewed
        //      so it doesn't look stale to future replacements).
        //   2. Empty slot.
        //   3. Stale-generation entries before current-generation ones,
        //      shallowest first - deep leftovers from earlier searches stop
        //      hogging their buckets in long games with a preserved TT.
        Entry* victim = nullptr;
        int victimScore = INT_MAX; // lower = more replaceable

        for (int i = 0; i < BUCKET_SIZE; ++i)
        {
            Entry& e = bucket.entries[i];
            uint64_t kxd = e.keyXorData.load(std::memory_order_relaxed);
            uint64_t data = e.data.load(std::memory_order_relaxed);

            if (kxd == 0 && data == 0)
            {
                // Empty slot - use it unless we find a key match later
                if (victimScore > INT_MIN)
                {
                    victim = &e;
                    victimScore = INT_MIN;
                }
                continue;
            }

            if ((kxd ^ data) == key)
            {
                // Same position: keep the deeper of the two entries, but
                // refresh its generation so it survives future evictions
                int existingDepth = static_cast<int>((data >> 48) & 0xFFULL);
                if (existingDepth > depth)
                {
                    if (GenerationOf(data) != m_generation)
                    {
                        uint64_t refreshed =
                            (data & ~(0xFCULL << 56)) |
                            (static_cast<uint64_t>(m_generation) << 58);
                        e.data.store(refreshed, std::memory_order_relaxed);
                        e.keyXorData.store(key ^ refreshed, std::memory_order_relaxed);
                    }
                    return;
                }
                victim = &e;
                break;
            }

            // Replaceability: current-generation entries are worth +256 depth
            // so any stale entry is evicted before any fresh one
            int entryDepth = static_cast<int>((data >> 48) & 0xFFULL);
            int score2 = entryDepth + ((GenerationOf(data) == m_generation) ? 256 : 0);
            if (score2 < victimScore)
            {
                victim = &e;
                victimScore = score2;
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

        uint64_t packed = PackData(scoreToStore, depth, flag, bestMove, m_generation);

        // Store data first, then key^data. Order is not strictly required by
        // the XOR-check protocol (any interleaving fails validation) but writing
        // data first means a reader is more likely to see a consistent entry.
        victim->data.store(packed, std::memory_order_relaxed);
        victim->keyXorData.store(key ^ packed, std::memory_order_relaxed);
    }
}
