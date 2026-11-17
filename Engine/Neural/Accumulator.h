// Accumulator.h
// Incrementally updated feature transformer state.
//
// Recomputing the accumulator from scratch costs 30 features x 128 outputs of
// int16 addition per perspective at every node, which measured about 49k
// evaluations per second against the ~800k nodes per second the search wants.
// That is what the "efficiently updatable" in NNUE refers to, and without it a
// network cannot be used at all, however good it is.
//
// ========== HOW THE UPDATE IS DERIVED ==========
//
// The obvious implementation reads the move that was just made and works out
// which features it added and removed: quiet move, capture, promotion, castling
// (two pieces), en passant (captured pawn not on the destination), and a king
// move that changes the king bucket and therefore invalidates every feature of
// that perspective. Six cases, each with its own way of being subtly wrong, and
// a wrong accumulator does not crash - it evaluates badly, which is
// indistinguishable from a badly trained network.
//
// This does something simpler and correct by construction. Each level stores a
// CONSISTENT PAIR: a feature list and the accumulator that belongs to it. Given
// any such pair, the accumulator for a new position is obtained by applying the
// set difference between the stored features and the new ones. That is true
// whatever position the stored pair came from - parent, sibling, or something
// unrelated - because the pair is self-describing. Only the size of the
// difference changes, never the correctness.
//
// Two things follow, and they are the reason for this design:
//
//   * No make/undo hooks are needed anywhere. Levels are indexed by the board's
//     own history ply. The level one above is used when it can be PROVEN to
//     hold the predecessor position - its stored key equals the previous
//     zobrist key in the board's last move record - because a parent differs
//     from its child by one move rather than the two that separate siblings.
//     Otherwise the level's own pair is used, which is still correct, only a
//     larger difference. Measured: parent-preferred 1.72x over a full
//     recomputation, sibling-only 1.01x.
//   * A null move needs no special case at all. It changes no piece, so the
//     extracted features are identical, the difference is empty, and the
//     accumulator is reused for free.
//
// Extraction itself is cheap - it walks two piece lists - while the expensive
// part is the 128-wide addition per changed feature. The lists are kept sorted
// so the difference is a linear merge.
//
#pragma once

#include "Features.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace Chess
{
namespace Neural
{
    // Fixed at compile time so the accumulator is a plain array. The loader
    // refuses any network whose header disagrees.
    constexpr int NNUE_HIDDEN = 128;

    // Search depth plus quiescence, with headroom. One level is ~640 bytes.
    constexpr int NNUE_MAX_LEVELS = 160;

    // Prefixed to stay clear of the older skeleton in this directory, which
    // declares a template of the same plain name and wins the lookup.
    struct NnueAccumulatorLevel
    {
        int16_t  values[2][NNUE_HIDDEN];              // indexed by PlayerColor
        uint16_t features[2][NNUE_MAX_ACTIVE];        // sorted
        uint8_t  count[2];
        bool     valid;                               // values match features
        uint64_t key;                                 // position these belong to
    };

    // One per search thread. Holds nothing the search has to maintain: the
    // level for a position is chosen from the board's history ply, and every
    // level is either invalid or a self-consistent (features, accumulator) pair.
    class NnueAccumulatorStack
    {
    public:
        void Reset() noexcept
        {
            for (int i = 0; i < NNUE_MAX_LEVELS; ++i)
                m_levels[i].valid = false;
        }

        NnueAccumulatorLevel& At(int historyPly) noexcept
        {
            const int index = (historyPly < 0) ? 0
                            : (historyPly >= NNUE_MAX_LEVELS ? NNUE_MAX_LEVELS - 1 : historyPly);
            return m_levels[index];
        }

    private:
        NnueAccumulatorLevel m_levels[NNUE_MAX_LEVELS];
    };

    // Sorted feature list for one perspective.
    inline int ExtractSorted(const Board& board, PlayerColor perspective, uint16_t* out) noexcept
    {
        const int count = ExtractFeatures(board, perspective, out);
        std::sort(out, out + count);
        return count;
    }
}
}
