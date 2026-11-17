// Network.h
// Quantised NNUE inference, matching testing/nnue/train.py exactly.
//
// The float network the trainer optimises is
//
//     a_p      = W_ft . x_p + b_ft            for each perspective p
//     h        = W_h . [cr(a_stm), cr(a_opp)] + b_h
//     out      = W_o . cr(h) + b_o            with cr(v) = clamp(v, 0, 1)
//
// and the evaluation in centipawns is out * EVAL_SCALE.
//
// ========== QUANTISATION ==========
//
// Two scales, both fixed in the file header so a network can never be read with
// the wrong ones:
//
//   QA = 255  applies to activations and to the feature transformer
//   QB = 64   applies to the hidden and output weights
//
// A quantised value V represents the float v = V / QA for activations, and a
// quantised weight W represents w = W / QB. Carrying that through:
//
//   A_p[i]  = round(b_ft[i]*QA) + sum over active f of round(W_ft[f][i]*QA)
//             so A represents a_p at scale QA
//   cr(A)   = clamp(A, 0, QA)
//   H[j]    = round(b_h[j]*QA*QB) + sum_i cr(A)[i] * round(W_h[j][i]*QB)
//             so H represents h at scale QA*QB
//   cr(H)   = clamp(H / QB, 0, QA)            back to scale QA
//   O       = round(b_o*QA*QB) + sum_j cr(H)[j] * round(W_o[j]*QB)
//   eval    = O * EVAL_SCALE / (QA*QB)
//
// Every one of those steps is mirrored in train.py's export(). If the two ever
// disagree, testing/nnue/verify.py catches it: it runs the float network in
// Python and this code in C++ over the same positions and compares.
//
// Accumulators are deliberately recomputed in full here. Incremental updates
// belong on top of a version that is known correct, not before it - the point
// of this file is to be obviously right, not fast.
#pragma once

#include "Accumulator.h"
#include "Simd.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace Chess
{
namespace Neural
{
    // Upper bound on the second layer, so the hot path can use a stack buffer.
    constexpr int NNUE_MAX_L1 = 64;

    class Network
    {
    public:
        bool Load(const std::string& path)
        {
            m_ready = false;
            m_error.clear();

            std::FILE* f = std::fopen(path.c_str(), "rb");
            if (!f) { m_error = "cannot open " + path; return false; }

            char magic[8] = {};
            int32_t header[6] = {};
            float k = 0.0f;
            bool ok = std::fread(magic, 1, 8, f) == 8
                   && std::fread(header, sizeof(int32_t), 6, f) == 6
                   && std::fread(&k, sizeof(float), 1, f) == 1;
            if (!ok || std::memcmp(magic, "NNUE1\0\0\0", 8) != 0)
            {
                m_error = "not an NNUE1 file: " + path;
                std::fclose(f);
                return false;
            }

            m_features = header[0];
            m_hidden   = header[1];
            m_l1       = header[2];
            m_qa       = header[3];
            m_qb       = header[4];
            m_scale    = header[5];
            m_k        = k;

            // A network built for a different feature set would silently produce
            // garbage, so refuse it rather than evaluate it.
            if (m_features != FEATURE_COUNT)
            {
                m_error = "network expects " + std::to_string(m_features) +
                          " features, this build has " + std::to_string(FEATURE_COUNT);
                std::fclose(f);
                return false;
            }
            if (m_l1 > NNUE_MAX_L1)
            {
                m_error = "network second layer is " + std::to_string(m_l1) +
                          ", this build allows at most " + std::to_string(NNUE_MAX_L1);
                std::fclose(f);
                return false;
            }
            if (m_hidden != NNUE_HIDDEN)
            {
                m_error = "network has " + std::to_string(m_hidden) +
                          " hidden units, this build is compiled for " + std::to_string(NNUE_HIDDEN);
                std::fclose(f);
                return false;
            }
            if (m_hidden <= 0 || m_l1 <= 0 || m_qa <= 0 || m_qb <= 0)
            {
                m_error = "invalid header dimensions";
                std::fclose(f);
                return false;
            }

            m_ftWeights.resize(static_cast<size_t>(m_features) * m_hidden);
            m_ftBias.resize(m_hidden);
            m_hiddenWeights.resize(static_cast<size_t>(m_l1) * 2 * m_hidden);
            m_hiddenBias.resize(m_l1);
            m_outWeights.resize(m_l1);

            ok = std::fread(m_ftWeights.data(), sizeof(int16_t), m_ftWeights.size(), f) == m_ftWeights.size()
              && std::fread(m_ftBias.data(), sizeof(int16_t), m_ftBias.size(), f) == m_ftBias.size()
              && std::fread(m_hiddenWeights.data(), sizeof(int8_t), m_hiddenWeights.size(), f) == m_hiddenWeights.size()
              && std::fread(m_hiddenBias.data(), sizeof(int32_t), m_hiddenBias.size(), f) == m_hiddenBias.size()
              && std::fread(m_outWeights.data(), sizeof(int8_t), m_outWeights.size(), f) == m_outWeights.size()
              && std::fread(&m_outBias, sizeof(int32_t), 1, f) == 1;

            // A trailing byte means the file does not match this layout, which is
            // exactly the sort of mismatch that is otherwise invisible.
            char extra = 0;
            const bool trailing = std::fread(&extra, 1, 1, f) == 1;
            std::fclose(f);

            if (!ok)      { m_error = "truncated network file"; return false; }
            if (trailing) { m_error = "network file longer than its header describes"; return false; }

            m_ready = true;
            return true;
        }

        bool IsReady() const noexcept { return m_ready; }
        const std::string& GetError() const noexcept { return m_error; }
        float GetTrainingK() const noexcept { return m_k; }

        // Centipawns from the point of view of the side to move.
        int Evaluate(const Board& board) const
        {
            if (!m_ready) return 0;

            const PlayerColor stm = board.GetSideToMove();
            const PlayerColor opp = (stm == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White;

            int16_t accStm[NNUE_HIDDEN], accOpp[NNUE_HIDDEN];
            Accumulate(board, stm, accStm);
            Accumulate(board, opp, accOpp);
            return Combine(accStm, accOpp);
        }

        // Incremental evaluation. The caller owns the stack, one per search
        // thread; nothing else has to be maintained. See Accumulator.h for why
        // this needs no make/undo hooks and no special case for a null move.
        int Evaluate(const Board& board, NnueAccumulatorStack& stack) const
        {
            if (!m_ready) return 0;

            const int historyPly = board.GetHistoryPly();
            NnueAccumulatorLevel& level = stack.At(historyPly);

            // Prefer the level above when it provably holds this position's
            // predecessor: a parent differs from its child by one move, a
            // sibling by two. Falling back to the level's own pair is always
            // correct, just a larger difference.
            const NnueAccumulatorLevel* source = &level;
            if (historyPly > 0)
            {
                const NnueAccumulatorLevel& parent = stack.At(historyPly - 1);
                if (parent.valid && parent.key == board.GetLastMoveRecord().previousZobristKey)
                    source = &parent;
            }
            const bool fromParent = (source != &level);

            for (int c = 0; c < 2; ++c)
            {
                const PlayerColor perspective = static_cast<PlayerColor>(c);
                uint16_t current[NNUE_MAX_ACTIVE];
                const int count = ExtractSorted(board, perspective, current);

                if (fromParent)
                {
                    std::memcpy(level.values[c], source->values[c], sizeof(level.values[c]));
                    ApplyDifference(source->features[c], source->count[c],
                                    current, count, level.values[c]);
                }
                else if (level.valid)
                {
                    ApplyDifference(level.features[c], level.count[c],
                                    current, count, level.values[c]);
                }
                else
                {
                    AccumulateIndices(current, count, level.values[c]);
                }

                std::memcpy(level.features[c], current,
                            static_cast<size_t>(count) * sizeof(uint16_t));
                level.count[c] = static_cast<uint8_t>(count);
            }
            level.valid = true;
            level.key = board.GetZobristKey();

            const PlayerColor stm = board.GetSideToMove();
            const PlayerColor opp = (stm == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White;
            return Combine(level.values[static_cast<int>(stm)], level.values[static_cast<int>(opp)]);
        }

        // Same evaluation, but from feature indices supplied directly. Used by
        // the verifier so that the comparison against the trainer isolates the
        // quantised arithmetic: both sides start from identical features.
        int EvaluateFeatures(const uint16_t* stmIdx, int stmCount,
                             const uint16_t* oppIdx, int oppCount) const
        {
            if (!m_ready) return 0;

            int16_t accStm[NNUE_HIDDEN], accOpp[NNUE_HIDDEN];
            AccumulateIndices(stmIdx, stmCount, accStm);
            AccumulateIndices(oppIdx, oppCount, accOpp);
            return Combine(accStm, accOpp);
        }

    private:
        int Combine(const int16_t* accStm, const int16_t* accOpp) const
        {
            // Clipped ReLU of both perspectives into one uint8 vector, side to
            // move first, which is the order the hidden weights were trained in.
            alignas(32) uint8_t input[2 * NNUE_HIDDEN];
            ClipToBytes(accStm, input, m_hidden, m_qa);
            ClipToBytes(accOpp, input + m_hidden, m_hidden, m_qa);

            alignas(32) uint8_t activated[NNUE_MAX_L1];
            const int inputs = 2 * m_hidden;
            for (int j = 0; j < m_l1; ++j)
            {
                const int8_t* row = &m_hiddenWeights[static_cast<size_t>(j) * inputs];
                int32_t sum = m_hiddenBias[j] + DotU8I8(input, row, inputs);
                // Back from scale QA*QB to scale QA before the next multiply.
                sum /= m_qb;
                activated[j] = static_cast<uint8_t>(sum < 0 ? 0 : (sum > m_qa ? m_qa : sum));
            }

            const int32_t out = m_outBias + DotU8I8(activated, m_outWeights.data(), m_l1);
            return static_cast<int>(static_cast<int64_t>(out) * m_scale / (static_cast<int64_t>(m_qa) * m_qb));
        }

        int32_t Clip(int32_t v) const noexcept
        {
            return v < 0 ? 0 : (v > m_qa ? m_qa : v);
        }

        void AccumulateIndices(const uint16_t* indices, int count, int16_t* acc) const
        {
            std::memcpy(acc, m_ftBias.data(), static_cast<size_t>(m_hidden) * sizeof(int16_t));
            for (int a = 0; a < count; ++a)
            {
                if (indices[a] >= m_features) continue;   // padding slot
                AddRow16(acc, &m_ftWeights[static_cast<size_t>(indices[a]) * m_hidden], m_hidden);
            }
        }

        // Sorted set difference: features only in `before` are subtracted,
        // features only in `after` are added, shared features are untouched.
        void ApplyDifference(const uint16_t* before, int beforeCount,
                             const uint16_t* after, int afterCount,
                             int16_t* acc) const
        {
            int i = 0, j = 0;
            while (i < beforeCount && j < afterCount)
            {
                if (before[i] == after[j]) { ++i; ++j; }
                else if (before[i] < after[j]) SubFeature(before[i++], acc);
                else                          AddFeature(after[j++], acc);
            }
            while (i < beforeCount) SubFeature(before[i++], acc);
            while (j < afterCount)  AddFeature(after[j++], acc);
        }

        void AddFeature(uint16_t index, int16_t* acc) const
        {
            if (index >= m_features) return;
            AddRow16(acc, &m_ftWeights[static_cast<size_t>(index) * m_hidden], m_hidden);
        }

        void SubFeature(uint16_t index, int16_t* acc) const
        {
            if (index >= m_features) return;
            SubRow16(acc, &m_ftWeights[static_cast<size_t>(index) * m_hidden], m_hidden);
        }

        void Accumulate(const Board& board, PlayerColor perspective, int16_t* acc) const
        {
            uint16_t indices[NNUE_MAX_ACTIVE];
            const int count = ExtractFeatures(board, perspective, indices);
            AccumulateIndices(indices, count, acc);
        }

        bool m_ready = false;
        std::string m_error;
        int m_features = 0, m_hidden = 0, m_l1 = 0;
        int m_qa = 0, m_qb = 0, m_scale = 0;
        float m_k = 0.0f;

        std::vector<int16_t> m_ftWeights;      // [features][hidden]
        std::vector<int16_t> m_ftBias;         // [hidden]
        std::vector<int8_t>  m_hiddenWeights;  // [l1][2*hidden]
        std::vector<int32_t> m_hiddenBias;     // [l1]
        std::vector<int8_t>  m_outWeights;     // [l1]
        int32_t m_outBias = 0;
    };
}
}
