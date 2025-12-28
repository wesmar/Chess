// Piece.h
#pragma once

#include "ChessConstants.h"
#include <cstdint>
#include <optional>
#include <string>

namespace Chess
{
    // ---------- Piece Representation ----------
    // Compact piece storage using single byte (type, color, moved flag)
    class Piece
    {
    public:
        constexpr Piece() = default;
        
        constexpr Piece(PieceType type, PlayerColor color, bool hasMoved = false)
            : m_data(static_cast<uint8_t>(type) | (static_cast<uint8_t>(color) << 3) | (static_cast<uint8_t>(hasMoved) << 4))
        {
        }

        // ---------- Getters ----------
        [[nodiscard]] constexpr PieceType GetType() const noexcept
        {
            return static_cast<PieceType>(m_data & 0x07);
        }

        [[nodiscard]] constexpr PlayerColor GetColor() const noexcept
        {
            return static_cast<PlayerColor>((m_data >> 3) & 0x01);
        }

        [[nodiscard]] constexpr bool HasMoved() const noexcept
        {
            return (m_data >> 4) & 0x01;
        }

        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            return GetType() == PieceType::None;
        }

        // Implicit bool conversion for easy empty check
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return !IsEmpty();
        }

        // ---------- Setters ----------
        constexpr void SetMoved(bool moved = true) noexcept
        {
            if (moved)
                m_data |= (1 << 4);
            else
                m_data &= ~(1 << 4);
        }

        // ---------- Utilities ----------
        [[nodiscard]] constexpr bool IsColor(PlayerColor color) const noexcept
        {
            return !IsEmpty() && GetColor() == color;
        }

        [[nodiscard]] constexpr bool IsType(PieceType type) const noexcept
        {
            return GetType() == type;
        }

        [[nodiscard]] constexpr bool IsOppositeColor(Piece other) const noexcept
        {
            return !IsEmpty() && !other.IsEmpty() && GetColor() != other.GetColor();
        }

        // Get algebraic symbol for piece (P, N, B, R, Q, K)
        [[nodiscard]] std::string GetSymbol() const
        {
            if (IsEmpty()) return " ";

            constexpr std::array<char, 7> symbols = { ' ', 'P', 'N', 'B', 'R', 'Q', 'K' };
            char symbol = symbols[static_cast<int>(GetType())];
            
            if (GetColor() == PlayerColor::Black)
                symbol = static_cast<char>(tolower(static_cast<unsigned char>(symbol)));
            
            return std::string(1, symbol);
        }

        [[nodiscard]] constexpr uint8_t GetRawData() const noexcept { return m_data; }

    private:
        uint8_t m_data = 0; // Bits: 0-2: type, 3: color, 4: hasMoved, 5-7: reserved
    };

    // ---------- Piece Constants ----------
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