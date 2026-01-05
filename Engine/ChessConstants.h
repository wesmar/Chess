// ChessConstants.h
// Core chess definitions, constants, and utility functions
//
// This header provides fundamental types and functions used throughout the chess engine:
// - Enumerations for pieces, colors, game states
// - Board geometry constants and coordinate systems
// - Move type definitions
// - Utility functions for coordinate conversions
//
// All functions are constexpr for compile-time evaluation when possible

#pragma once

#include <cstdint>
#include <string>
#include <array>

namespace Chess
{
    // ========== CORE ENUMERATIONS ==========

    // Piece types in chess
    // None used for empty squares and as sentinel value
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

    // Player colors
    enum class PlayerColor : uint8_t
    {
        White,
        Black
    };

    // Game state enumeration
    // Used for UI feedback and game termination detection
    enum class GameState : uint8_t
    {
        Playing,    // Normal play continues
        Check,      // King in check but has legal moves
        Checkmate,  // King in check with no legal moves
        Stalemate,  // Not in check but no legal moves
        Draw        // Draw by repetition, fifty-move rule, or insufficient material
    };

    // ========== BOARD GEOMETRY ==========

    constexpr int BOARD_SIZE = 8;                          // 8x8 chess board
    constexpr int SQUARE_COUNT = BOARD_SIZE * BOARD_SIZE;  // 64 squares total

    // ========== ALGEBRAIC NOTATION ==========

    // File names (columns) from a-h
    constexpr std::array<char, BOARD_SIZE> FILE_NAMES = { 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h' };
    
    // Rank names (rows) from 1-8
    constexpr std::array<char, BOARD_SIZE> RANK_NAMES = { '1', '2', '3', '4', '5', '6', '7', '8' };

    // ========== COORDINATE CONVERSION UTILITIES ==========

    // Check if file and rank are within board bounds
    // Used for move generation and validation
    constexpr bool IsValidCoordinate(int file, int rank) noexcept
    {
        return file >= 0 && file < BOARD_SIZE && rank >= 0 && rank < BOARD_SIZE;
    }

    // Convert (file, rank) to linear square index (0-63)
    // Board layout: a1=0, b1=1, ..., h1=7, a2=8, ..., h8=63
    // This is the "rank-major" order used internally
    constexpr int CoordinateToIndex(int file, int rank) noexcept
    {
        return rank * BOARD_SIZE + file;
    }

    // Convert linear square index to (file, rank) pair
    // Inverse of CoordinateToIndex
    // Returns: pair<file, rank> where file=0-7 (a-h), rank=0-7 (1-8)
    constexpr std::pair<int, int> IndexToCoordinate(int index) noexcept
    {
        return { index % BOARD_SIZE, index / BOARD_SIZE };
    }

    // ========== MOVE TYPE DEFINITIONS ==========

    // Move type enumeration for special move handling
    // Each move type may require different execution logic (see Board::MakeMoveUnchecked)
    enum class MoveType : uint8_t
    {
        Normal,     // Standard move (quiet or capture detected separately)
        Capture,    // Explicit capture move
        EnPassant,  // Special pawn capture (captured pawn not on destination square)
        Castling,   // King-rook castling (two pieces move)
        Promotion   // Pawn reaches back rank and promotes
    };

    // ========== PIECE VALUES FOR EVALUATION ==========

    // Standard centipawn values used for material evaluation
    // These are also used in PST (Piece-Square Tables) as base values
    constexpr std::array<int, 7> PIECE_VALUES = { 
        0,      // None
        100,    // Pawn
        320,    // Knight
        330,    // Bishop (slightly better than knight due to pair bonus)
        500,    // Rook
        900,    // Queen
        20000   // King (effectively infinite - losing king = losing game)
    };
}