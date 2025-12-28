// MoveGenerator.h
#pragma once

#include "ChessConstants.h"
#include "Piece.h"
#include "Move.h"
#include <vector>
#include <array>

namespace Chess
{
    // Static class for generating chess moves
    class MoveGenerator
    {
    public:
        // Generate all pseudo-legal moves (may leave king in check)
        static std::vector<Move> GeneratePseudoLegalMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            PlayerColor sideToMove,
            int enPassantSquare = -1,
            const std::array<bool, 4>* castlingRights = nullptr
        );

        // Check if a square is under attack by specified color
        static bool IsSquareAttacked(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor attackerColor
        );

    private:
        // Generate moves for specific piece types
        static std::vector<Move> GeneratePawnMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor color,
            int enPassantSquare
        );

        static std::vector<Move> GenerateKnightMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor color
        );

        static std::vector<Move> GenerateBishopMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor color
        );

        static std::vector<Move> GenerateRookMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor color
        );

        static std::vector<Move> GenerateQueenMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor color
        );

        static std::vector<Move> GenerateKingMoves(
            const std::array<Piece, SQUARE_COUNT>& board,
            int square,
            PlayerColor color,
            const std::array<bool, 4>* castlingRights
        );

        // Movement offset tables for different piece types
        static constexpr std::array<std::pair<int, int>, 8> KNIGHT_OFFSETS = {{
            {2, 1}, {1, 2}, {-1, 2}, {-2, 1},
            {-2, -1}, {-1, -2}, {1, -2}, {2, -1}
        }};

        static constexpr std::array<std::pair<int, int>, 8> KING_OFFSETS = {{
            {1, 0}, {1, 1}, {0, 1}, {-1, 1},
            {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
        }};

        static constexpr std::array<std::pair<int, int>, 4> BISHOP_DIRECTIONS = {{
            {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
        }};

        static constexpr std::array<std::pair<int, int>, 4> ROOK_DIRECTIONS = {{
            {1, 0}, {0, 1}, {-1, 0}, {0, -1}
        }};
    };
}