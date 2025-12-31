// Zobrist.h
// Zobrist hashing system for chess position identification
//
// Zobrist hashing is a technique invented by Albert Zobrist in 1970 for efficiently
// creating hash keys for game positions. It has become the standard hashing method
// for chess engines due to its key properties:
//
// 1. INCREMENTAL UPDATE: Hash can be updated in O(1) when making/unmaking moves
//    by XORing out old values and XORing in new values (XOR is self-inverse)
//
// 2. COLLISION RESISTANCE: With 64-bit keys and proper random initialization,
//    probability of two different positions having same hash is ~1/2^64
//
// 3. POSITION INDEPENDENCE: Same position always produces same hash regardless
//    of how it was reached (important for transposition detection)
//
// Usage in chess engine:
// - Transposition table lookups (avoid re-searching known positions)
// - Threefold repetition detection (compare position hashes in game history)
// - Opening book lookups (fast position matching)
#pragma once

#include "ChessConstants.h"
#include <cstdint>
#include <array>

namespace Chess
{
    // Static class providing Zobrist hash key generation and storage
    // All members are static - no instances needed, just call Zobrist::Initialize() once
    class Zobrist
    {
    public:
        // Initialize all Zobrist keys with random values
        // Must be called once at program startup before any hashing operations
        // Uses deterministic seed for reproducibility across runs
        static void Initialize();

        // ========== HASH KEY ARRAYS ==========
        
        // Piece position keys: [piece type][color][square]
        // Dimensions: [7][2][64] = 896 unique 64-bit keys
        // - pieceType: 0=None, 1=Pawn, 2=Knight, 3=Bishop, 4=Rook, 5=Queen, 6=King
        // - color: 0=White, 1=Black
        // - square: 0-63 in standard board representation (a1=0, h8=63)
        static uint64_t pieceKeys[7][2][64];

        // Side to move key - XORed into hash only when BLACK is to move
        // Ensures identical positions with different side to move have different hashes
        // White-to-move positions simply don't include this key
        static uint64_t sideToMoveKey;

        // Castling rights keys: [white kingside, white queenside, black kingside, black queenside]
        // Each available castling right XORs its key into the position hash
        // When a right is lost (king/rook moves), XOR it out of the hash
        static uint64_t castlingKeys[4];

        // En passant file keys: [file 0-7 corresponding to a-h]
        // Only the file needs to be encoded - the rank is implied:
        // - White en passant target is always rank 6
        // - Black en passant target is always rank 3
        // Side to move (already in hash) determines which applies
        static uint64_t enPassantKeys[8];

    private:
        // Initialization guard flag - prevents redundant key generation
        static bool s_initialized;
    };
}
