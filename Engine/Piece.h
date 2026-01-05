// Piece.h
// Compact piece representation for chess engine
//
// This module provides efficient piece storage using bit-packing:
// - Type (3 bits): None/Pawn/Knight/Bishop/Rook/Queen/King (0-6)
// - Color (1 bit): White/Black
// - HasMoved flag (1 bit): Track piece movement for castling/pawn rules
// Total: 5 bits used of 8-bit byte
//
// Advantages over enum-based representation:
// - Single byte storage per piece (vs 4+ bytes for enum)
// - Cache-friendly for board array (64 bytes total)
// - Fast bitwise operations for queries
// - Type-safe with C++ strong typing
#pragma once

#include "ChessConstants.h"
#include <cstdint>
#include <optional>
#include <string>

namespace Chess
{
    // ---------- Piece Representation ----------
    // Compact piece storage using single byte (type, color, moved flag)
    // Bit layout: [unused:3][hasMoved:1][color:1][type:3]
    class Piece
    {
    public:
        // Default constructor creates empty piece (PieceType::None)
        constexpr Piece() = default;
        
        // Construct piece with type and color
        // @param type: Piece type (Pawn, Knight, Bishop, etc.)
        // @param color: White or Black
        // @param hasMoved: Whether piece has moved (for castling/pawn rules)
        constexpr Piece(PieceType type, PlayerColor color, bool hasMoved = false)
            : m_data(static_cast<uint8_t>(type) | (static_cast<uint8_t>(color) << 3) | (static_cast<uint8_t>(hasMoved) << 4))
        {
        }

        // ---------- Getters ----------
        
        // Get piece type (Pawn, Knight, etc.)
        [[nodiscard]] constexpr PieceType GetType() const noexcept
        {
            return static_cast<PieceType>(m_data & 0x07);
        }

        // Get piece color (White or Black)
        [[nodiscard]] constexpr PlayerColor GetColor() const noexcept
        {
            return static_cast<PlayerColor>((m_data >> 3) & 0x01);
        }

        // Check if piece has moved (used for castling and pawn two-square moves)
        [[nodiscard]] constexpr bool HasMoved() const noexcept
        {
            return (m_data >> 4) & 0x01;
        }

        // Check if this is an empty square (PieceType::None)
        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            return GetType() == PieceType::None;
        }

        // Implicit bool conversion for easy empty check
        // Allows usage: if (piece) { ... }
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return !IsEmpty();
        }

        // ---------- Setters ----------
        
        // Mark piece as moved (for castling and pawn move rules)
        constexpr void SetMoved(bool moved = true) noexcept
        {
            if (moved)
                m_data |= (1 << 4);
            else
                m_data &= ~(1 << 4);
        }

        // ---------- Utilities ----------
        
        // Check if piece matches specified color
        [[nodiscard]] constexpr bool IsColor(PlayerColor color) const noexcept
        {
            return !IsEmpty() && GetColor() == color;
        }

        // Check if piece matches specified type
        [[nodiscard]] constexpr bool IsType(PieceType type) const noexcept
        {
            return GetType() == type;
        }

        // Check if piece is opposite color to another piece
        // Used for capture detection
        [[nodiscard]] constexpr bool IsOppositeColor(Piece other) const noexcept
        {
            return !IsEmpty() && !other.IsEmpty() && GetColor() != other.GetColor();
        }

        // Get algebraic symbol for piece (P, N, B, R, Q, K)
        // Returns lowercase for black pieces, uppercase for white
        [[nodiscard]] std::string GetSymbol() const
        {
            if (IsEmpty()) return " ";

            constexpr std::array<char, 7> symbols = { ' ', 'P', 'N', 'B', 'R', 'Q', 'K' };
            char symbol = symbols[static_cast<int>(GetType())];
            
            if (GetColor() == PlayerColor::Black)
                symbol = static_cast<char>(tolower(static_cast<unsigned char>(symbol)));
            
            return std::string(1, symbol);
        }

        // Get raw byte data (for debugging/serialization)
        [[nodiscard]] constexpr uint8_t GetRawData() const noexcept { return m_data; }

    private:
        // Packed data: Bits 0-2: type, Bit 3: color, Bit 4: hasMoved, Bits 5-7: reserved
        uint8_t m_data = 0;
    };

    // ---------- Piece Constants ----------
    // Predefined piece constants for convenience
    
    // Empty square marker
    constexpr Piece EMPTY_PIECE = Piece(PieceType::None, PlayerColor::White);
    
    // White pieces
    constexpr Piece WHITE_PAWN   = Piece(PieceType::Pawn, PlayerColor::White);
    constexpr Piece WHITE_KNIGHT = Piece(PieceType::Knight, PlayerColor::White);
    constexpr Piece WHITE_BISHOP = Piece(PieceType::Bishop, PlayerColor::White);
    constexpr Piece WHITE_ROOK   = Piece(PieceType::Rook, PlayerColor::White);
    constexpr Piece WHITE_QUEEN  = Piece(PieceType::Queen, PlayerColor::White);
    constexpr Piece WHITE_KING   = Piece(PieceType::King, PlayerColor::White);
    
    // Black pieces
    constexpr Piece BLACK_PAWN   = Piece(PieceType::Pawn, PlayerColor::Black);
    constexpr Piece BLACK_KNIGHT = Piece(PieceType::Knight, PlayerColor::Black);
    constexpr Piece BLACK_BISHOP = Piece(PieceType::Bishop, PlayerColor::Black);
    constexpr Piece BLACK_ROOK   = Piece(PieceType::Rook, PlayerColor::Black);
    constexpr Piece BLACK_QUEEN  = Piece(PieceType::Queen, PlayerColor::Black);
    constexpr Piece BLACK_KING   = Piece(PieceType::King, PlayerColor::Black);
}