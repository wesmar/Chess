// Move.h
#pragma once

#include "Piece.h"
#include <cstdint>
#include <string>
#include <optional>
#include <vector>
#include <memory>

// Forward declaration to avoid circular include with Board.h
namespace Chess { class Board; }

namespace Chess
{
    // ---------- Move Representation (32-bit packed) ----------
    // Efficiently stores move data: from/to squares, type, promotion, captured piece
    class Move
    {
    public:
        constexpr Move() = default;

        constexpr Move(int from, int to, MoveType type = MoveType::Normal,
                      PieceType promotion = PieceType::None,
                      Piece captured = EMPTY_PIECE) noexcept
            : m_data(static_cast<uint32_t>(from) |
                    (static_cast<uint32_t>(to) << 6) |
                    (static_cast<uint32_t>(type) << 12) |
                    (static_cast<uint32_t>(promotion) << 15))
            , m_captured(captured)
        {
        }

        // ---------- Accessors ----------
        [[nodiscard]] constexpr int GetFrom() const noexcept
        {
            return m_data & 0x3F; // Bits 0-5: from square (0-63)
        }

        [[nodiscard]] constexpr int GetTo() const noexcept
        {
            return (m_data >> 6) & 0x3F; // Bits 6-11: to square (0-63)
        }

        [[nodiscard]] constexpr MoveType GetType() const noexcept
        {
            return static_cast<MoveType>((m_data >> 12) & 0x07); // Bits 12-14: move type
        }

        [[nodiscard]] constexpr PieceType GetPromotion() const noexcept
        {
            return static_cast<PieceType>((m_data >> 15) & 0x07); // Bits 15-17: promotion piece
        }

        [[nodiscard]] constexpr Piece GetCaptured() const noexcept
        {
            return m_captured;
        }

        [[nodiscard]] constexpr uint32_t GetRawData() const noexcept
        {
            return m_data;
        }

        // ---------- Utility Methods ----------
        [[nodiscard]] constexpr bool IsCapture() const noexcept
        {
            return GetType() == MoveType::Capture || GetType() == MoveType::EnPassant;
        }

        [[nodiscard]] constexpr bool IsPromotion() const noexcept
        {
            return GetPromotion() != PieceType::None;
        }

        [[nodiscard]] constexpr bool IsCastling() const noexcept
        {
            return GetType() == MoveType::Castling;
        }

        [[nodiscard]] constexpr bool IsEnPassant() const noexcept
        {
            return GetType() == MoveType::EnPassant;
        }

        // Convert move to algebraic notation (e.g., "e2e4", "e7e8Q")
        [[nodiscard]] std::string ToAlgebraic() const
        {
            if (IsCastling())
            {
                return (GetTo() % 8 == 6) ? "O-O" : "O-O-O";
            }

            auto [fromFile, fromRank] = IndexToCoordinate(GetFrom());
            auto [toFile, toRank] = IndexToCoordinate(GetTo());
            
            std::string result;
            result += FILE_NAMES[fromFile];
            result += RANK_NAMES[fromRank];
            
            if (IsCapture())
            {
                result += 'x';
            }
            
            result += FILE_NAMES[toFile];
            result += RANK_NAMES[toRank];

            if (IsPromotion())
            {
                constexpr std::array<char, 7> promoSymbols = { ' ', 'P', 'N', 'B', 'R', 'Q', 'K' };
                result += '=';
                result += promoSymbols[static_cast<int>(GetPromotion())];
            }

            return result;
        }

        // Convert move to UCI format (e.g., "e2e4", "e7e8q", "e1g1")
        // This is the standard format used by chess GUIs.
        [[nodiscard]] std::string ToUCI() const;

        // Parse UCI move string and find matching legal move.
        // Returns nullopt if the move string is invalid or illegal.
        static std::optional<Move> FromUCI(const std::string& uci, const Board& board);

        // Equality comparison
        constexpr bool operator==(const Move& other) const noexcept
        {
            return m_data == other.m_data;
        }

    private:
        uint32_t m_data = 0;    // Packed move data
        Piece m_captured;       // Captured piece (for move history)
    };
}