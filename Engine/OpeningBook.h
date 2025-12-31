// OpeningBook.h
// Hardcoded opening book for standard chess opening lines
//
// Opening books provide pre-computed moves for the early game phase, allowing
// the chess engine to play theoretically sound openings without spending
// computation time. This implementation uses:
//
// 1. ZOBRIST HASHING: Positions are identified by their Zobrist hash key,
//    enabling O(n) lookup where n is the number of book entries
//
// 2. MOVE VARIETY: Each position can store up to 4 alternative moves,
//    with random selection providing variety in play
//
// 3. COMPACT ENCODING: Moves are packed into 16 bits to minimize memory usage
//
// 4. TRANSPOSITION SUPPORT: Same position reached via different move orders
//    shares the same book entry (handled automatically via hash lookup)
//
// The book exits after BOOK_MAX_PLIES (default 8) half-moves, transitioning
// to calculated play in the middlegame phase.
#pragma once

#include "Board.h"
#include "Move.h"
#include <cstdint>
#include <optional>

namespace Chess
{
    // Maximum ply depth for opening book usage
    // After this many half-moves, ProbeBook() returns nullopt
    // 8 plies = 4 full moves (e.g., 1.e4 e5 2.Nf3 Nc6 3.Bb5 a6 4.Ba4 Nf6)
    constexpr int BOOK_MAX_PLIES = 8;

    // ========== COMPACT MOVE ENCODING ==========
    // Moves are packed into 16 bits for memory efficiency in book storage
    // Bit layout:
    //   bits 0-5:   from square (0-63, requires 6 bits)
    //   bits 6-11:  to square (0-63, requires 6 bits)
    //   bits 12-14: promotion piece type (0-6, requires 3 bits)
    //   bit 15:     unused/reserved
    //
    // This encoding is sufficient for book moves since special move flags
    // (castling, en passant) can be inferred from the piece and squares

    // Pack move data into compact 16-bit representation
    // @param from: Source square index (0-63)
    // @param to: Destination square index (0-63)
    // @param promo: Promotion piece type (default None for non-promotion moves)
    // @return: Packed 16-bit move value
    constexpr uint16_t PackMove(int from, int to, PieceType promo = PieceType::None)
    {
        return static_cast<uint16_t>(
            (from & 0x3F) |                    // Bits 0-5: from square
            ((to & 0x3F) << 6) |               // Bits 6-11: to square
            (static_cast<int>(promo) << 12)   // Bits 12-14: promotion
        );
    }

    // Unpack compact 16-bit move to full Move object
    // Creates a Normal move type - actual move type (castling, etc.)
    // is determined during legal move matching in ProbeBook()
    // @param packed: 16-bit packed move value
    // @return: Move object with from/to/promotion extracted
    inline Move UnpackToMove(uint16_t packed)
    {
        int from = packed & 0x3F;                                    // Extract bits 0-5
        int to = (packed >> 6) & 0x3F;                               // Extract bits 6-11
        PieceType promo = static_cast<PieceType>((packed >> 12) & 0x7); // Extract bits 12-14

        // MoveType::Normal is placeholder - ProbeBook validates against legal moves
        // which have correct type flags set
        return Move(from, to, MoveType::Normal, promo);
    }

    // ========== PUBLIC API ==========

    // Query opening book for a move in the given position
    // @param board: Current board position to look up
    // @param plyCount: Number of half-moves played (for book depth limit)
    // @return: A book move if position is found and within depth limit,
    //          std::nullopt if position not in book or past BOOK_MAX_PLIES
    //
    // When multiple moves are available for a position, one is selected randomly
    // The returned move is validated against current legal moves for safety
    std::optional<Move> ProbeBook(const Board& board, int plyCount);

    // Initialize the opening book data structures
    // Called automatically on first ProbeBook() call, but can be called
    // explicitly at startup to avoid first-probe latency
    // Populates book with standard opening lines (Ruy Lopez, Sicilian, etc.)
    void InitializeOpeningBook();
}
