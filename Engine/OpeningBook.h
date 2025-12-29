// OpeningBook.h
// Minimal hardcoded opening book for standard opening lines
#pragma once

#include "Board.h"
#include "Move.h"
#include <cstdint>
#include <optional>

namespace Chess
{
    // Maximum ply depth to use opening book
    constexpr int BOOK_MAX_PLIES = 8;

    // Compact move encoding (16 bits)
    // bits 0-5: from square (0..63)
    // bits 6-11: to square (0..63)
    // bits 12-14: promotion piece (PieceType 0..6)
    // bit 15: unused
    constexpr uint16_t PackMove(int from, int to, PieceType promo = PieceType::None)
    {
        return static_cast<uint16_t>(
            (from & 0x3F) |
            ((to & 0x3F) << 6) |
            (static_cast<int>(promo) << 12)
        );
    }

    // Unpack compact move to engine Move object
    inline Move UnpackToMove(uint16_t packed)
    {
        int from = packed & 0x3F;
        int to = (packed >> 6) & 0x3F;
        PieceType promo = static_cast<PieceType>((packed >> 12) & 0x7);

        // MoveType will be inferred as Normal; legality check will filter invalid moves
        return Move(from, to, MoveType::Normal, promo);
    }

    // Probe opening book for current position
    // Returns a book move if available, otherwise std::nullopt
    std::optional<Move> ProbeBook(const Board& board, int plyCount);

    // Initialize the opening book (call once at startup)
    void InitializeOpeningBook();
}
