// MoveGenerator.h
// Chess move generation engine
// Generates pseudo-legal and tactical moves, validates square attacks
#pragma once

#include "ChessConstants.h"
#include "Piece.h"
#include "Move.h"
#include <vector>
#include <array>

namespace Chess
{
    struct PieceList;

    // Static move generator - no instance needed, all methods are static
    // Provides pseudo-legal move generation (follows piece rules but may leave king in check)
    class MoveGenerator
    {
    public:
        // Generate all pseudo-legal moves for given position
        // Returns moves that follow piece movement rules but may leave king in check
        // Legal move validation requires checking if king is attacked after move
        static std::vector<Move> GeneratePseudoLegalMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            PlayerColor sideToMove,
            int enPassantSquare = -1,
            const std::array<bool, 4>* castlingRights = nullptr,
            const PieceList* pieceList = nullptr
        );

        // Generate only tactical moves (captures and promotions)
        // Used in quiescence search to resolve tactical complications
        // Excludes quiet moves and castling for performance
        static std::vector<Move> GenerateTacticalMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            PlayerColor sideToMove,
            int enPassantSquare = -1,
            const PieceList* pieceList = nullptr
        );

        // Check if square is attacked by specified color
        // Tests all possible attack patterns (pawns, knights, sliding pieces, king)
        // Used for castling validation and check detection
        static bool IsSquareAttacked(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor attackerColor
        );

    private:
        // Piece-specific move generators
        // Each generates moves following piece movement rules
        
        // Pawn moves: forward, two-square advance, captures, en passant, promotion
        static std::vector<Move> GeneratePawnMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor color,
            int enPassantSquare
        );

        // Knight moves: L-shaped jumps (2+1 or 1+2 squares)
        static std::vector<Move> GenerateKnightMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor color
        );

        // Bishop moves: diagonal sliding until blocked
        static std::vector<Move> GenerateBishopMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor color
        );

        // Rook moves: orthogonal sliding until blocked
        static std::vector<Move> GenerateRookMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor color
        );

        // Queen moves: combination of bishop and rook movement
        static std::vector<Move> GenerateQueenMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor color
        );

        // King moves: one square in any direction, plus castling
        static std::vector<Move> GenerateKingMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor color,
            const std::array<bool, 4>* castlingRights
        );

        // Movement direction offsets for piece types
        // Used to iterate through possible moves efficiently
        
        // Knight L-shaped moves: 8 possible positions
        static constexpr std::array<std::pair<int, int>, 8> KNIGHT_OFFSETS = {{
            {2, 1}, {1, 2}, {-1, 2}, {-2, 1},
            {-2, -1}, {-1, -2}, {1, -2}, {2, -1}
        }};

        // King and general one-square moves: 8 directions
        static constexpr std::array<std::pair<int, int>, 8> KING_OFFSETS = {{
            {1, 0}, {1, 1}, {0, 1}, {-1, 1},
            {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
        }};

        // Bishop diagonal directions: 4 diagonals
        static constexpr std::array<std::pair<int, int>, 4> BISHOP_DIRECTIONS = {{
            {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
        }};

        // Rook orthogonal directions: 4 directions
        static constexpr std::array<std::pair<int, int>, 4> ROOK_DIRECTIONS = {{
            {1, 0}, {0, 1}, {-1, 0}, {0, -1}
        }};
    };
}