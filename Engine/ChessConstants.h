// ChessConstants.h
#pragma once

#include <cstdint>
#include <string>
#include <array>

namespace Chess
{
    // ---------- Core Enums ----------
    enum class PieceType : uint8_t
    {
        None = 0,
        Pawn,
        Knight,
        Bishop,
        Rook,
        Queen,
        King
    };

    enum class PlayerColor : uint8_t
    {
        White,
        Black
    };

    enum class GameState : uint8_t
    {
        Playing,
        Check,
        Checkmate,
        Stalemate,
        Draw
    };

    // ---------- Board Definitions ----------
    constexpr int BOARD_SIZE = 8;
    constexpr int SQUARE_COUNT = BOARD_SIZE * BOARD_SIZE;

    // ---------- Algebraic Notation ----------
    constexpr std::array<char, BOARD_SIZE> FILE_NAMES = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h' };
    constexpr std::array<char, BOARD_SIZE> RANK_NAMES = { '1', '2', '3', '4', '5', '6', '7', '8' };

    // ---------- Utility Functions ----------
    // Check if file and rank are within board bounds
    constexpr bool IsValidCoordinate(int file, int rank) noexcept
    {
        return file >= 0 && file < BOARD_SIZE && rank >= 0 && rank < BOARD_SIZE;
    }

    // Convert file and rank to linear square index (0-63)
    constexpr int CoordinateToIndex(int file, int rank) noexcept
    {
        return rank * BOARD_SIZE + file;
    }

    // Convert linear square index to file and rank
    constexpr std::pair<int, int> IndexToCoordinate(int index) noexcept
    {
        return { index % BOARD_SIZE, index / BOARD_SIZE };
    }

    // ---------- Move Constants ----------
    enum class MoveType : uint8_t
    {
        Normal,
        Capture,
        EnPassant,
        Castling,
        Promotion
    };

    // ---------- Piece Values (for AI evaluation) ----------
    constexpr std::array<int, 7> PIECE_VALUES = { 0, 100, 320, 330, 500, 900, 20000 };
}