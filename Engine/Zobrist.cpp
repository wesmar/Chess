// Zobrist.cpp
#include "Zobrist.h"
#include <random>

namespace Chess
{
    // Static member initialization - Zobrist hash keys for all game states
    uint64_t Zobrist::pieceKeys[7][2][64] = {};
    uint64_t Zobrist::sideToMoveKey = 0;
    uint64_t Zobrist::castlingKeys[4] = {};
    uint64_t Zobrist::enPassantKeys[8] = {};
    bool Zobrist::s_initialized = false;

    // Initialize all Zobrist hash keys with random 64-bit values
    void Zobrist::Initialize()
    {
        if (s_initialized) return;

        // Use fixed seed for reproducible hashing across runs
        std::mt19937_64 rng(20241227);
        std::uniform_int_distribution<uint64_t> dist;

        // Initialize piece keys [piece type][color][square]
        for (int pieceType = 0; pieceType < 7; ++pieceType)
        {
            for (int color = 0; color < 2; ++color)
            {
                for (int square = 0; square < 64; ++square)
                {
                    pieceKeys[pieceType][color][square] = dist(rng);
                }
            }
        }

        // Initialize side to move key (only used for black)
        sideToMoveKey = dist(rng);

        // Initialize castling rights keys (white kingside, queenside, black kingside, queenside)
        for (int i = 0; i < 4; ++i)
        {
            castlingKeys[i] = dist(rng);
        }

        // Initialize en passant file keys (one for each file a-h)
        for (int i = 0; i < 8; ++i)
        {
            enPassantKeys[i] = dist(rng);
        }

        s_initialized = true;
    }
}