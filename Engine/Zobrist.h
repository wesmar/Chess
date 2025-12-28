// Zobrist.h
#pragma once

#include "ChessConstants.h"
#include <cstdint>
#include <array>

namespace Chess
{
    // Zobrist hashing for fast position comparison and transposition table
    class Zobrist
    {
    public:
        // Initialize all Zobrist keys with random values (call once at startup)
        static void Initialize();

        // Hash keys for pieces: [piece type][color][square]
        static uint64_t pieceKeys[7][2][64];

        // Hash key for side to move (XOR only when black to move)
        static uint64_t sideToMoveKey;

        // Hash keys for castling rights: [white kingside, queenside, black kingside, queenside]
        static uint64_t castlingKeys[4];

        // Hash keys for en passant file: [file 0-7]
        static uint64_t enPassantKeys[8];

    private:
        static bool s_initialized;
    };
}