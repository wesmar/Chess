// FeatureExtractor.h
// Chess position to neural network feature mapping
//
// This module implements HalfKA (Half King-All) feature extraction for NNUE.
// Each position is encoded from both perspectives (white and black) to enable
// efficient incremental updates during search.
//
// Feature encoding:
// - King bucket: King position with horizontal mirroring (32 unique positions)
// - Piece square: Location of each piece on board (64 squares)
// - Piece type: 10 piece types (5 types x 2 colors, excluding perspective's king)
//
// Optimized for mailbox board representation with PieceList iteration

#pragma once

#include "../ChessConstants.h"
#include "../Piece.h"
#include "../Board.h"
#include <cstdint>
#include <array>

namespace Chess
{
namespace Neural
{
    // ========== FEATURE DIMENSIONS ==========

    // Number of unique king positions after horizontal mirroring
    // Kings on files a-d mirror to files e-h (32 buckets instead of 64)
    constexpr int KING_BUCKETS = 64;

    // Piece type indices for feature encoding
    // Excludes the perspective's own king (10 piece types total)
    constexpr int PIECE_TYPES_COUNT = 10;

    // Order: WhitePawn, BlackPawn, WhiteKnight, BlackKnight,
    //        WhiteBishop, BlackBishop, WhiteRook, BlackRook,
    //        WhiteQueen, BlackQueen (no kings in features)

    // Feature space dimensions
    constexpr int FEATURES_PER_KING = SQUARE_COUNT * PIECE_TYPES_COUNT;  // 64 * 10 = 640
    constexpr int TOTAL_FEATURES = KING_BUCKETS * FEATURES_PER_KING;     // 64 * 640 = 40960

    // Maximum active features per perspective (16 pieces max per side)
    constexpr int MAX_ACTIVE_FEATURES = 32;

    // ========== FEATURE INDEX COMPUTATION ==========

    // Convert piece type and color to feature piece index (0-9)
    // @param type: Piece type (Pawn through Queen, King excluded)
    // @param color: Piece color
    // @return: Feature index 0-9, or -1 for invalid (None or King)
    constexpr int GetPieceIndex(PieceType type, PlayerColor color)
    {
        if (type == PieceType::None || type == PieceType::King)
            return -1;

        // Piece type offset: Pawn=0, Knight=1, Bishop=2, Rook=3, Queen=4
        int typeOffset = static_cast<int>(type) - 1;  // Pawn is type 1

        // Color offset: White=0, Black=1
        int colorOffset = static_cast<int>(color);

        // Interleaved layout: WP, BP, WN, BN, WB, BB, WR, BR, WQ, BQ
        return typeOffset * 2 + colorOffset;
    }

    // Get king bucket index with optional horizontal mirroring
    // @param kingSquare: King position (0-63)
    // @param mirror: Whether to apply horizontal mirroring
    // @return: King bucket index (0-63 or 0-31 with mirroring)
    constexpr int GetKingBucket(int kingSquare, bool mirror = false)
    {
        if (!mirror)
            return kingSquare;

        // Apply horizontal mirroring for kings on files e-h
        int file = kingSquare % 8;
        int rank = kingSquare / 8;

        if (file >= 4)
            file = 7 - file;  // Mirror e-h to d-a

        return rank * 8 + file;
    }

    // Mirror square horizontally (for symmetric features)
    constexpr int MirrorSquare(int square)
    {
        int file = square % 8;
        int rank = square / 8;
        return rank * 8 + (7 - file);
    }

    // Flip square vertically (for black perspective)
    constexpr int FlipSquare(int square)
    {
        return square ^ 56;  // XOR with 56 flips rank
    }

    // Compute feature index for a piece from given perspective
    // @param perspective: Viewing color (White or Black)
    // @param kingSquare: King position for this perspective
    // @param pieceSquare: Piece position on board
    // @param pieceType: Type of piece
    // @param pieceColor: Color of piece
    // @return: Feature index in [0, TOTAL_FEATURES), or -1 if invalid
    constexpr int ComputeFeatureIndex(PlayerColor perspective, int kingSquare,
                                       int pieceSquare, PieceType pieceType,
                                       PlayerColor pieceColor)
    {
        // Skip kings (they define perspective, not features)
        if (pieceType == PieceType::King)
            return -1;

        int pieceIndex = GetPieceIndex(pieceType, pieceColor);
        if (pieceIndex < 0)
            return -1;

        // Transform coordinates for black perspective
        int orientedKing = kingSquare;
        int orientedPiece = pieceSquare;

        if (perspective == PlayerColor::Black)
        {
            orientedKing = FlipSquare(kingSquare);
            orientedPiece = FlipSquare(pieceSquare);
            // Also flip piece color from black's view
            pieceIndex ^= 1;  // Swap white/black index
        }

        int kingBucket = GetKingBucket(orientedKing);

        return kingBucket * FEATURES_PER_KING + orientedPiece * PIECE_TYPES_COUNT + pieceIndex;
    }

    // ========== FEATURE LIST STRUCTURE ==========

    // Compact storage for active feature indices
    struct FeatureList
    {
        std::array<int, MAX_ACTIVE_FEATURES> indices;
        int count = 0;

        constexpr void Clear() { count = 0; }

        constexpr void Add(int index)
        {
            if (index >= 0 && count < MAX_ACTIVE_FEATURES)
                indices[count++] = index;
        }
    };

    // ========== FEATURE EXTRACTOR CLASS ==========

    // Extracts neural network features from chess position
    // Designed for efficient use with mailbox board and PieceList
    class FeatureExtractor
    {
    public:
        // Extract all active features for given perspective
        // Uses PieceList for O(pieces) complexity instead of O(64)
        // @param board: Current board position
        // @param perspective: Color perspective (White or Black)
        // @param output: Feature list to populate
        static void ExtractFeatures(const Board& board, PlayerColor perspective,
                                     FeatureList& output)
        {
            output.Clear();

            int kingSquare = board.GetKingSquare(perspective);

            // Iterate over both piece lists (white and black pieces)
            for (int colorIdx = 0; colorIdx < 2; ++colorIdx)
            {
                PlayerColor pieceColor = static_cast<PlayerColor>(colorIdx);
                const PieceList& pieces = board.GetPieceList(pieceColor);

                for (int i = 0; i < pieces.count; ++i)
                {
                    int square = pieces.squares[i];
                    Piece piece = board.GetPieceAt(square);
                    PieceType type = piece.GetType();

                    // Skip empty squares and kings
                    if (type == PieceType::None || type == PieceType::King)
                        continue;

                    int featureIndex = ComputeFeatureIndex(
                        perspective, kingSquare, square, type, pieceColor);

                    output.Add(featureIndex);
                }
            }
        }

        // Extract features for both perspectives simultaneously
        // More efficient when both are needed (typical case)
        // @param board: Current board position
        // @param whiteFeatures: Output for white perspective
        // @param blackFeatures: Output for black perspective
        static void ExtractBothPerspectives(const Board& board,
                                             FeatureList& whiteFeatures,
                                             FeatureList& blackFeatures)
        {
            whiteFeatures.Clear();
            blackFeatures.Clear();

            int whiteKing = board.GetKingSquare(PlayerColor::White);
            int blackKing = board.GetKingSquare(PlayerColor::Black);

            // Single pass over all pieces for both perspectives
            for (int colorIdx = 0; colorIdx < 2; ++colorIdx)
            {
                PlayerColor pieceColor = static_cast<PlayerColor>(colorIdx);
                const PieceList& pieces = board.GetPieceList(pieceColor);

                for (int i = 0; i < pieces.count; ++i)
                {
                    int square = pieces.squares[i];
                    Piece piece = board.GetPieceAt(square);
                    PieceType type = piece.GetType();

                    if (type == PieceType::None || type == PieceType::King)
                        continue;

                    // White perspective feature
                    int whiteIdx = ComputeFeatureIndex(
                        PlayerColor::White, whiteKing, square, type, pieceColor);
                    whiteFeatures.Add(whiteIdx);

                    // Black perspective feature
                    int blackIdx = ComputeFeatureIndex(
                        PlayerColor::Black, blackKing, square, type, pieceColor);
                    blackFeatures.Add(blackIdx);
                }
            }
        }

        // Compute feature changes for incremental update after a move
        // This is the critical function for efficient NNUE updates during search.
        // Instead of regenerating all features, we only track what changed.
        //
        // @param board: Board state AFTER move was made
        // @param move: The move that was made
        // @param perspective: Color perspective for feature computation
        // @param added: Output - features to add to accumulator
        // @param removed: Output - features to remove from accumulator
        static void ComputeFeatureChanges(const Board& board, const Move& move,
                                           PlayerColor perspective,
                                           FeatureList& added, FeatureList& removed)
        {
            added.Clear();
            removed.Clear();

            int kingSquare = board.GetKingSquare(perspective);
            int from = move.GetFrom();
            int to = move.GetTo();

            // Get piece info - piece is now at 'to' square after move
            Piece movedPiece = board.GetPieceAt(to);
            PieceType pieceType = movedPiece.GetType();
            PlayerColor pieceColor = movedPiece.GetColor();

            // King moves invalidate all features (king bucket changes everything)
            // Signal full refresh by returning empty lists
            if (pieceType == PieceType::King && pieceColor == perspective)
            {
                return;
            }

            // STEP 1: Remove piece from old square
            if (pieceType != PieceType::King)
            {
                int oldIdx = ComputeFeatureIndex(
                    perspective, kingSquare, from, pieceType, pieceColor);
                removed.Add(oldIdx);
            }

            // STEP 2: Handle promotion (changes piece type)
            if (move.IsPromotion())
            {
                // Add promoted piece (Queen/Rook/Bishop/Knight) to target square
                PieceType promotedType = move.GetPromotion();
                int newIdx = ComputeFeatureIndex(
                    perspective, kingSquare, to, promotedType, pieceColor);
                added.Add(newIdx);
            }
            else if (pieceType != PieceType::King)
            {
                // Normal move: add same piece to new square
                int newIdx = ComputeFeatureIndex(
                    perspective, kingSquare, to, pieceType, pieceColor);
                added.Add(newIdx);
            }

            // STEP 3: Handle captures (remove captured piece)
            if (move.IsCapture() && !move.IsEnPassant())
            {
                Piece capturedPiece = move.GetCaptured();
                if (!capturedPiece.IsEmpty())
                {
                    // Captured piece was at target square
                    int capturedIdx = ComputeFeatureIndex(
                        perspective, kingSquare, to,
                        capturedPiece.GetType(), capturedPiece.GetColor());
                    removed.Add(capturedIdx);
                }
            }

            // STEP 4: Handle en passant (captured pawn is not on target square)
            if (move.IsEnPassant())
            {
                // Calculate actual position of captured pawn (one rank behind)
                int capturedPawnSquare = to + ((pieceColor == PlayerColor::White) ? -8 : 8);
                PlayerColor enemyColor = (pieceColor == PlayerColor::White)
                    ? PlayerColor::Black : PlayerColor::White;

                // Remove enemy pawn from its actual square
                int capturedIdx = ComputeFeatureIndex(
                    perspective, kingSquare, capturedPawnSquare,
                    PieceType::Pawn, enemyColor);
                removed.Add(capturedIdx);
            }

            // STEP 5: Handle castling (move rook as well)
            if (move.IsCastling())
            {
                int rookFrom, rookTo;
                int row = (pieceColor == PlayerColor::White) ? 0 : 7;

                // Determine kingside or queenside
                if (move.GetTo() % 8 == 6) // Kingside: king to g-file
                {
                    rookFrom = CoordinateToIndex(7, row); // h-file
                    rookTo = CoordinateToIndex(5, row);   // f-file
                }
                else // Queenside: king to c-file
                {
                    rookFrom = CoordinateToIndex(0, row); // a-file
                    rookTo = CoordinateToIndex(3, row);   // d-file
                }

                // Remove rook from starting square
                int rookOldIdx = ComputeFeatureIndex(
                    perspective, kingSquare, rookFrom, PieceType::Rook, pieceColor);
                removed.Add(rookOldIdx);

                // Add rook to new square
                int rookNewIdx = ComputeFeatureIndex(
                    perspective, kingSquare, rookTo, PieceType::Rook, pieceColor);
                added.Add(rookNewIdx);
            }
        }
    };

} // namespace Neural
} // namespace Chess
