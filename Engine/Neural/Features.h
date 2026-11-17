// Features.h
// NNUE input features. This file is the single source of truth for the mapping
// from a position to network inputs.
//
// Nothing else - not the trainer, not the data packer, not the evaluator - is
// allowed to reimplement it. A trainer that computes features slightly
// differently from the evaluator produces a network that scores nonsense at
// runtime while its training loss looked fine, and there is no way to tell
// that apart from a bad network. Extracting the features once, in C++, and
// feeding the trainer the resulting indices removes that whole failure mode by
// construction.
//
// ========== THE MAPPING ==========
//
// Features are computed twice per position, once from each side's point of
// view. From a perspective, the board is transformed so that:
//
//   1. the perspective's own back rank is rank 1 (vertical flip for Black), so
//      "a pawn two squares from promotion" is one feature, not two;
//   2. the perspective's king stands on files a-d (horizontal flip otherwise),
//      so a kingside and a queenside structure share their weights;
//   3. pieces are labelled "ours" and "theirs" rather than white and black.
//
// After that the perspective's king square, which is now one of 32, selects one
// of 8 buckets. The king is not a feature itself: it is the context every other
// feature is expressed in, which is what lets the network learn king safety at
// all.
//
//   index = bucket * 640 + pieceIndex * 64 + square
//
//   bucket      0..7    from the mirrored king square
//   pieceIndex  0..9    ours P,N,B,R,Q = 0..4, theirs P,N,B,R,Q = 5..9
//   square      0..63   mirrored
//
// 8 * 10 * 64 = 5120 inputs per perspective. At most 30 are active at once,
// which is what makes the first layer cheap to update incrementally.
#pragma once

#include "../Board.h"

#include <cstdint>

namespace Chess
{
namespace Neural
{
    constexpr int FEATURE_KING_BUCKETS = 8;
    constexpr int FEATURE_PIECE_KINDS  = 10;   // five piece types, ours and theirs
    constexpr int FEATURE_SQUARES      = 64;
    constexpr int FEATURE_COUNT        = FEATURE_KING_BUCKETS * FEATURE_PIECE_KINDS * FEATURE_SQUARES;
    static_assert(FEATURE_COUNT == 5120, "feature layout changed");

    // Two kings are never features, so 30 pieces is the hard maximum.
    // Named apart from the old skeleton's MAX_ACTIVE_FEATURES, which is a
    // different value for a different feature set and would silently win the
    // name lookup - it did, and the packed record came out the wrong size.
    constexpr int NNUE_MAX_ACTIVE = 30;

    // King square -> bucket, after the square has already been mirrored into
    // files a-d. Two files and two ranks per bucket: coarse enough that every
    // bucket gets plenty of training samples, fine enough to separate a castled
    // king from a central one.
    inline int KingBucket(int mirroredKingSquare) noexcept
    {
        const int file = mirroredKingSquare % 8;   // 0..3 after the horizontal flip
        const int rank = mirroredKingSquare / 8;   // 0..7
        return (rank / 2) * 2 + (file / 2);
    }

    // Squares seen from a perspective: flipped vertically for Black so both
    // sides advance "up", and flipped horizontally when the perspective's king
    // sits on the kingside so both wings share weights.
    inline int TransformSquare(int square, PlayerColor perspective, bool mirrorFiles) noexcept
    {
        int s = (perspective == PlayerColor::White) ? square : (square ^ 56);
        if (mirrorFiles) s ^= 7;
        return s;
    }

    // True when the perspective's king stands on files e-h and the board should
    // therefore be mirrored left to right.
    inline bool ShouldMirrorFiles(const Board& board, PlayerColor perspective) noexcept
    {
        const int kingSquare = board.GetKingSquare(perspective);
        if (kingSquare < 0) return false;
        const int seen = (perspective == PlayerColor::White) ? kingSquare : (kingSquare ^ 56);
        return (seen % 8) >= 4;
    }

    // Writes the active feature indices for one perspective and returns how many
    // there were. `out` must have room for NNUE_MAX_ACTIVE.
    inline int ExtractFeatures(const Board& board, PlayerColor perspective,
                               uint16_t* out) noexcept
    {
        const int kingSquare = board.GetKingSquare(perspective);
        if (kingSquare < 0) return 0;

        const bool mirror = ShouldMirrorFiles(board, perspective);
        const int bucket = KingBucket(TransformSquare(kingSquare, perspective, mirror));
        const int bucketBase = bucket * (FEATURE_PIECE_KINDS * FEATURE_SQUARES);

        int count = 0;
        for (int colourIndex = 0; colourIndex < 2; ++colourIndex)
        {
            const PlayerColor colour = static_cast<PlayerColor>(colourIndex);
            const bool ours = (colour == perspective);
            const PieceList& list = board.GetPieceList(colour);

            for (int i = 0; i < list.count; ++i)
            {
                const int square = list.squares[i];
                const Piece piece = board.GetPieceAt(square);
                const PieceType type = piece.GetType();
                if (type == PieceType::King || type == PieceType::None) continue;

                // PieceType::Pawn is 1, so type - 1 gives 0..4 for P,N,B,R,Q.
                const int kind = (static_cast<int>(type) - 1) + (ours ? 0 : 5);
                const int square_ = TransformSquare(square, perspective, mirror);

                if (count < NNUE_MAX_ACTIVE)
                    out[count++] = static_cast<uint16_t>(bucketBase + kind * FEATURE_SQUARES + square_);
            }
        }
        return count;
    }
}
}
