// Zobrist.cpp
// Zobrist hashing implementation for chess position identification
// Zobrist hashing is a technique for creating unique 64-bit hash keys for board positions
// enabling efficient transposition table lookups and threefold repetition detection
//
// The algorithm works by XORing together random 64-bit numbers for each aspect of the position:
// - Each piece type on each square
// - Side to move
// - Castling rights
// - En passant file
//
// Key property: incremental updates - when making a move, we only need to XOR out the old
// state and XOR in the new state, making hash updates O(1) instead of O(n)
#include "Zobrist.h"
#include <random>

namespace Chess
{
    // ========== STATIC MEMBER DEFINITIONS ==========
    // These arrays store the random 64-bit keys used for hashing
    // Zero-initialized here, populated by Initialize()

    // Piece keys: [piece type 0-6][color 0-1][square 0-63]
    // Total: 7 * 2 * 64 = 896 unique keys for all piece/square combinations
    uint64_t Zobrist::pieceKeys[7][2][64] = {};

    // Side to move key - XORed into hash when black is to move
    // This ensures same position with different side to move has different hash
    uint64_t Zobrist::sideToMoveKey = 0;

    // Castling rights keys: [white kingside, white queenside, black kingside, black queenside]
    // Each right that is available is XORed into the hash
    uint64_t Zobrist::castlingKeys[4] = {};

    // En passant file keys: [file a-h as 0-7]
    // Only the file matters for en passant (rank is implied by side to move)
    uint64_t Zobrist::enPassantKeys[8] = {};

    // Initialization flag to prevent redundant initialization
    bool Zobrist::s_initialized = false;

    // Initialize all Zobrist hash keys with pseudo-random 64-bit values
    // Uses fixed seed for reproducibility - same keys every run ensures
    // consistent hash values for the same positions across program restarts
    // This is important for debugging and testing
    void Zobrist::Initialize()
    {
        // Guard against multiple initialization
        if (s_initialized) return;

        // Mersenne Twister 64-bit PRNG with fixed seed
        // Seed 20241227 chosen arbitrarily (date-based)
        // Fixed seed ensures deterministic key generation
        std::mt19937_64 rng(20241227);
        std::uniform_int_distribution<uint64_t> dist;

        // Generate piece keys for all combinations
        // pieceType: 0=None, 1=Pawn, 2=Knight, 3=Bishop, 4=Rook, 5=Queen, 6=King
        // color: 0=White, 1=Black
        // square: 0-63 (a1=0, h8=63)
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

        // Generate side to move key
        // Convention: key is XORed only when BLACK is to move
        // This means white-to-move positions don't include this key
        sideToMoveKey = dist(rng);

        // Generate castling rights keys
        // Index mapping:
        //   0 = White kingside (O-O)
        //   1 = White queenside (O-O-O)
        //   2 = Black kingside (O-O)
        //   3 = Black queenside (O-O-O)
        for (int i = 0; i < 4; ++i)
        {
            castlingKeys[i] = dist(rng);
        }

        // Generate en passant file keys
        // Only need 8 keys (one per file) since:
        // - En passant square rank is always 3 (white) or 6 (black)
        // - Side to move is already encoded separately
        // Index 0 = a-file, Index 7 = h-file
        for (int i = 0; i < 8; ++i)
        {
            enPassantKeys[i] = dist(rng);
        }

        s_initialized = true;
    }
}
