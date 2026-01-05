// Move.h
// Move representation and UCI conversion utilities
// Compact 32-bit move encoding with from/to squares, type, promotion, and captured piece
#pragma once

#include "Piece.h"
#include <cstdint>
#include <string>
#include <array>
#include <optional>
#include <memory>

// Forward declaration to avoid circular include with Board.h
namespace Chess { class Board; }

namespace Chess
{
    // ========== MOVE REPRESENTATION ==========
    // Efficiently stores move data in 32 bits plus captured piece
    // Bit layout: [promotion:3][type:3][to:6][from:6] = 18 bits used
    class Move
    {
    public:
        // Default constructor - creates null move
        constexpr Move() = default;

        // Construct move with all parameters
        // @param from: Source square (0-63)
        // @param to: Destination square (0-63)
        // @param type: Move type (normal, capture, castling, etc.)
        // @param promotion: Piece type for pawn promotion (None if not promoting)
        // @param captured: Piece being captured (for move history)
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

        // ========== ACCESSORS ==========
        
        // Get source square index (0-63)
        [[nodiscard]] constexpr int GetFrom() const noexcept
        {
            return m_data & 0x3F; // Bits 0-5
        }

        // Get destination square index (0-63)
        [[nodiscard]] constexpr int GetTo() const noexcept
        {
            return (m_data >> 6) & 0x3F; // Bits 6-11
        }

        // Get move type (normal, capture, castling, en passant, promotion)
        [[nodiscard]] constexpr MoveType GetType() const noexcept
        {
            return static_cast<MoveType>((m_data >> 12) & 0x07); // Bits 12-14
        }

        // Get promotion piece type (None if not a promotion)
        [[nodiscard]] constexpr PieceType GetPromotion() const noexcept
        {
            return static_cast<PieceType>((m_data >> 15) & 0x07); // Bits 15-17
        }

        // Get captured piece (for move history and undo)
        [[nodiscard]] constexpr Piece GetCaptured() const noexcept
        {
            return m_captured;
        }

        // Get raw packed data (for debugging)
        [[nodiscard]] constexpr uint32_t GetRawData() const noexcept
        {
            return m_data;
        }

        // ========== UTILITY METHODS ==========
        
        // Check if this is a capture move (includes en passant)
        [[nodiscard]] constexpr bool IsCapture() const noexcept
        {
            return GetType() == MoveType::Capture || GetType() == MoveType::EnPassant;
        }

        // Check if this is a pawn promotion
        [[nodiscard]] constexpr bool IsPromotion() const noexcept
        {
            return GetPromotion() != PieceType::None;
        }

        // Check if this is a castling move
        [[nodiscard]] constexpr bool IsCastling() const noexcept
        {
            return GetType() == MoveType::Castling;
        }

        // Check if this is an en passant capture
        [[nodiscard]] constexpr bool IsEnPassant() const noexcept
        {
            return GetType() == MoveType::EnPassant;
        }

        // ========== STRING CONVERSION ==========
        
        // Convert to algebraic notation with capture indicator
        // Format: "e2e4", "e7xf8=Q", "O-O", "O-O-O"
        // Includes 'x' for captures and '=' for promotions
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

        // Convert to UCI format (Universal Chess Interface standard)
        // Format: "e2e4", "e7e8q", "e1g1" (castling as king move)
        // Promotion uses lowercase piece letter (q/r/b/n)
        [[nodiscard]] std::string ToUCI() const;

        // Parse UCI move string and find matching legal move
        // Returns nullopt if move string is invalid or illegal in given position
        // Format: "e2e4", "e7e8q", "e1g1"
        static std::optional<Move> FromUCI(const std::string& uci, const Board& board);

        // ========== COMPARISON ==========
        
        // Equality comparison - compares packed data only
        // Does not compare captured piece (not part of move identity)
        constexpr bool operator==(const Move& other) const noexcept
        {
            return m_data == other.m_data;
        }

    private:
        uint32_t m_data = 0;    // Packed move data (from, to, type, promotion)
        Piece m_captured;       // Captured piece (stored separately for move history)
    };

    // ========== MOVE LIST ==========
    // Stack-allocated move buffer for efficient move generation
    // Avoids heap allocations in hot path (millions of calls during search)
    // Maximum possible moves in chess position is ~220 (rare), 256 is safe upper bound
    struct MoveList
    {
        std::array<Move, 256> moves;
        int count = 0;

        void push_back(const Move& move) { moves[count++] = move; }

        Move* begin() { return &moves[0]; }
        Move* end() { return &moves[count]; }
        const Move* begin() const { return &moves[0]; }
        const Move* end() const { return &moves[count]; }

        Move& operator[](int index) { return moves[index]; }
        const Move& operator[](int index) const { return moves[index]; }

        int size() const { return count; }
        bool empty() const { return count == 0; }
        void reserve(int) { }
        void clear() { count = 0; }
    };
}