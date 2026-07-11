// TranspositionTable.h
// Lock-free hash table for caching position evaluations.
//
// Uses Hyatt's XOR-key validation trick: each entry is a pair of 64-bit atomics
// (keyXorData, data). On store, write data first then key^data. On probe, the
// reader XORs the loaded keyXorData with the loaded data to recover the original
// key. If a concurrent writer interleaved with the read, the recovered key will
// not match the query key and the probe is treated as a miss.
//
// This eliminates mutex contention entirely. Each probe is two relaxed atomic
// loads + a XOR; each store is two relaxed atomic stores.
//
// Cache layout: entries are grouped into 64-byte buckets of 4 (one cache line).
// A probe touches exactly one cache line regardless of which of the 4 ways the
// entry lives in, and the 4-way associativity greatly reduces replacement
// collisions compared to a direct-mapped table.
//
// Layout (per entry, 16 bytes):
//   keyXorData : uint64_t   = zobristKey XOR packedData
//   packedData : uint64_t   = move(32) | score(16) | depth(8) | flag(8)
//
// Score range fits in int16_t (chess scores within ±32000, mate scores ±29000).
// Depth range fits in uint8_t (max search depth in practice well under 128).
#pragma once

#include "Move.h"
#include <cstdint>
#include <memory>
#include <atomic>
#include <intrin.h>

namespace Chess
{
    enum TTFlag : uint8_t
    {
        TT_EXACT = 0,  // Exact score from full-window search
        TT_ALPHA = 1,  // Upper bound: real score <= stored score
        TT_BETA  = 2   // Lower bound: real score >= stored score
    };

    class TranspositionTable
    {
    public:
        TranspositionTable();
        ~TranspositionTable() = default;

        // Resize to specified size in MB. Rounds down to nearest power of 2 buckets.
        void Resize(size_t sizeInMB);

        // Zero all entries. Caller must ensure no concurrent searches.
        void Clear();

        // Probe with depth/bound filtering. Always writes outBestMove if key matches
        // (for move ordering), returns true only if cached score is usable.
        bool Probe(uint64_t key, int depth, int alpha, int beta,
                   int& outScore, Move& outBestMove, int ply);

        // Unconditional probe: returns any match regardless of depth/bound.
        // Used by Singular Extensions for reference score retrieval.
        bool ProbeSE(uint64_t key, int& outScore, Move& outBestMove);

        // Store with in-bucket replacement (key match > empty slot > shallowest).
        void Store(uint64_t key, int depth, int score, uint8_t flag,
                   Move bestMove, int ply);

        // Prefetch the bucket for a position into L1. Call right after making a
        // move so the line is resident by the time the child node probes it.
        void Prefetch(uint64_t key) const noexcept
        {
            if (m_buckets)
            {
                _mm_prefetch(reinterpret_cast<const char*>(
                    &m_buckets[key & (m_numBuckets - 1)]), _MM_HINT_T0);
            }
        }

    private:
        struct Entry
        {
            std::atomic<uint64_t> keyXorData{0};
            std::atomic<uint64_t> data{0};
        };

        static constexpr int BUCKET_SIZE = 4;

        // One bucket == one 64-byte cache line
        struct alignas(64) Bucket
        {
            Entry entries[BUCKET_SIZE];
        };
        static_assert(sizeof(Bucket) == 64, "Bucket must be exactly one cache line");

        std::unique_ptr<Bucket[]> m_buckets;
        size_t m_numBuckets = 0;  // Always a power of 2

        static constexpr uint64_t PackData(int score, int depth, uint8_t flag, Move move) noexcept
        {
            uint32_t mvRaw = move.GetRawData();
            uint16_t s16 = static_cast<uint16_t>(static_cast<int16_t>(
                score < -32000 ? -32000 : (score > 32000 ? 32000 : score)));
            uint8_t d8 = static_cast<uint8_t>(
                depth < 0 ? 0 : (depth > 255 ? 255 : depth));
            return static_cast<uint64_t>(mvRaw)
                 | (static_cast<uint64_t>(s16) << 32)
                 | (static_cast<uint64_t>(d8) << 48)
                 | (static_cast<uint64_t>(flag) << 56);
        }

        static void UnpackData(uint64_t data, int& score, int& depth,
                               uint8_t& flag, Move& move) noexcept
        {
            uint32_t mvRaw = static_cast<uint32_t>(data & 0xFFFFFFFFULL);
            move = Move::FromRaw(mvRaw);
            int16_t s16 = static_cast<int16_t>((data >> 32) & 0xFFFFULL);
            score = static_cast<int>(s16);
            depth = static_cast<int>((data >> 48) & 0xFFULL);
            flag  = static_cast<uint8_t>((data >> 56) & 0xFFULL);
        }

        // Find the entry whose stored key matches, or nullptr.
        // Loads both words of the matching entry into kxd/data outputs.
        Entry* FindEntry(uint64_t key, uint64_t& outData)
        {
            Bucket& bucket = m_buckets[key & (m_numBuckets - 1)];
            for (int i = 0; i < BUCKET_SIZE; ++i)
            {
                Entry& e = bucket.entries[i];
                uint64_t kxd = e.keyXorData.load(std::memory_order_relaxed);
                uint64_t data = e.data.load(std::memory_order_relaxed);
                if ((kxd ^ data) == key)
                {
                    outData = data;
                    return &e;
                }
            }
            return nullptr;
        }
    };
}
