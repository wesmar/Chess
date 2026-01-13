// OpeningBook.cpp
// Hardcoded opening book implementation with common chess opening lines
// Provides known good moves for early game positions to improve AI play quality
// Uses Zobrist hashing for fast position lookup in the book database
#include "OpeningBook.h"
#include <algorithm>
#include <random>
#include <vector>

namespace Chess
{
    // Book entry structure mapping position hash to available moves
    // Each position can store up to 4 alternative book moves for variety
    struct BookEntry
    {
        uint64_t zobristKey;        // Position hash for lookup
        uint16_t moves[4];          // Up to 4 alternative moves per position (packed format)
        uint8_t moveCount;          // Number of valid moves in the array
    };

    // Global storage for opening book entries
    // Populated once at runtime by InitializeOpeningBook()
    static std::vector<BookEntry> g_bookEntries;
    static bool g_bookInitialized = false;

    // Convert algebraic square notation to internal index
    // Example: 'e', '2' -> 12 (e2 square)
    // Board layout: a1=0, b1=1, ..., h1=7, a2=8, ..., h8=63
    constexpr int AlgebraicToSquare(char file, char rank)
    {
        return (rank - '1') * 8 + (file - 'a');
    }

    // Add an opening line to the book database
    // Plays through the move sequence, recording each position with its next move
    // This allows transpositions - same position reached via different move orders
    // shares the same book entry
    static void AddBookLine(const std::vector<std::pair<int, int>>& moves)
    {
        Board board;
        board.ResetToStartingPosition();

        // Process each move in the line
        for (size_t i = 0; i < moves.size(); ++i)
        {
            // Get current position hash before making the move
            uint64_t key = board.GetZobristKey();
            int from = moves[i].first;
            int to = moves[i].second;
            uint16_t packedMove = PackMove(from, to);

            // Search for existing entry with this position hash
            auto it = std::find_if(g_bookEntries.begin(), g_bookEntries.end(),
                [key](const BookEntry& e) { return e.zobristKey == key; });

            if (it != g_bookEntries.end())
            {
                // Position already exists in book - add move as alternative if not duplicate
                bool found = false;
                for (uint8_t j = 0; j < it->moveCount; ++j)
                {
                    if (it->moves[j] == packedMove)
                    {
                        found = true;
                        break;
                    }
                }
                // Add new move variant if space available (max 4 moves per position)
                if (!found && it->moveCount < 4)
                {
                    it->moves[it->moveCount++] = packedMove;
                }
            }
            else
            {
                // New position - create fresh book entry
                BookEntry entry;
                entry.zobristKey = key;
                entry.moves[0] = packedMove;
                entry.moveCount = 1;
                g_bookEntries.push_back(entry);
            }

			// Advance board to next position in the line
			// Find the legal move matching from/to to get correct MoveType flags
			// (capture, castle, en passant, etc.) for proper move execution
			auto legalMoves = board.GenerateLegalMoves();
			auto moveIt = std::find_if(legalMoves.begin(), legalMoves.end(),
				[from, to](const Move& m) {
					return m.GetFrom() == from && m.GetTo() == to;
				});

			if (moveIt == legalMoves.end())
			{
				break; // Invalid move sequence - stop processing this line
			}

			// Use the fully-formed legal move with correct type flags
			if (!board.MakeMove(*moveIt))
			{
				break; // Move execution failed - stop processing this line
			}
        }
    }

    // Initialize the opening book with standard chess opening lines
    // Called automatically on first book probe, or can be called explicitly at startup
    // Book includes major openings: Ruy Lopez, Italian, Sicilian, Queen's Gambit, etc.
    void InitializeOpeningBook()
    {
        // Prevent double initialization
        if (g_bookInitialized) return;

        g_bookEntries.clear();
        g_bookEntries.reserve(50); // Preallocate for ~50 unique positions

		// Pre-calculate square indices for cleaner move notation
        // These are computed at compile time (constexpr)
        constexpr int e2 = AlgebraicToSquare('e', '2');
        constexpr int e4 = AlgebraicToSquare('e', '4');
        constexpr int e5 = AlgebraicToSquare('e', '5');
        constexpr int e6 = AlgebraicToSquare('e', '6');
        constexpr int e7 = AlgebraicToSquare('e', '7');
        constexpr int d2 = AlgebraicToSquare('d', '2');
        constexpr int d4 = AlgebraicToSquare('d', '4');
        constexpr int d5 = AlgebraicToSquare('d', '5');
        constexpr int d6 = AlgebraicToSquare('d', '6');
        constexpr int d7 = AlgebraicToSquare('d', '7');
        constexpr int c2 = AlgebraicToSquare('c', '2');
        constexpr int c3 = AlgebraicToSquare('c', '3');
        constexpr int c4 = AlgebraicToSquare('c', '4');
        constexpr int c5 = AlgebraicToSquare('c', '5');
        constexpr int c6 = AlgebraicToSquare('c', '6');
        constexpr int c7 = AlgebraicToSquare('c', '7');
        constexpr int c8 = AlgebraicToSquare('c', '8');
        constexpr int f1 = AlgebraicToSquare('f', '1');
        constexpr int f3 = AlgebraicToSquare('f', '3');
        constexpr int f6 = AlgebraicToSquare('f', '6');
        constexpr int f8 = AlgebraicToSquare('f', '8');
        constexpr int g1 = AlgebraicToSquare('g', '1');
        constexpr int g6 = AlgebraicToSquare('g', '6');
        constexpr int g7 = AlgebraicToSquare('g', '7');
        constexpr int g8 = AlgebraicToSquare('g', '8');
        constexpr int b1 = AlgebraicToSquare('b', '1');
        constexpr int b4 = AlgebraicToSquare('b', '4');
        constexpr int b5 = AlgebraicToSquare('b', '5');
        constexpr int b8 = AlgebraicToSquare('b', '8');

        // ========== 1.e4 OPENINGS ==========

        // Ruy Lopez (Spanish Game): 1.e4 e5 2.Nf3 Nc6 3.Bb5
        // One of the oldest and most respected openings, aiming for long-term pressure
        AddBookLine({
            {e2, e4},   // 1.e4
            {e7, e5},   // 1...e5
            {g1, f3},   // 2.Nf3 - attacks e5 pawn
            {b8, c6},   // 2...Nc6 - defends e5
            {f1, b5}    // 3.Bb5 - pins knight, pressures center
        });

        // Italian Game: 1.e4 e5 2.Nf3 Nc6 3.Bc4
        // Classical development aiming at f7 weakness
        AddBookLine({
            {e2, e4},   // 1.e4
            {e7, e5},   // 1...e5
            {g1, f3},   // 2.Nf3
            {b8, c6},   // 2...Nc6
            {f1, c4}    // 3.Bc4 - targets f7 square
        });

        // Sicilian Defense (Open Sicilian): 1.e4 c5 2.Nf3 d6 3.d4 cxd4 4.Nxd4
        // Most popular response to 1.e4 at master level, asymmetrical and fighting
        AddBookLine({
            {e2, e4},   // 1.e4
            {c7, c5},   // 1...c5 - Sicilian Defense
            {g1, f3},   // 2.Nf3
            {d7, d6},   // 2...d6 - prepares ...Nf6
            {d2, d4},   // 3.d4 - challenges center
            {c5, d4},   // 3...cxd4
            {f3, d4}    // 4.Nxd4 - Open Sicilian position
        });

        // French Defense: 1.e4 e6 2.d4 d5
        // Solid but somewhat passive, leads to closed positions
        AddBookLine({
            {e2, e4},   // 1.e4
            {e7, e6},   // 1...e6 - French Defense
            {d2, d4},   // 2.d4
            {d7, d5}    // 2...d5 - challenges e4 pawn
        });

        // Caro-Kann Defense: 1.e4 c6 2.d4 d5
        // Solid defense similar to French but avoids blocked light-squared bishop
        AddBookLine({
            {e2, e4},   // 1.e4
            {c7, c6},   // 1...c6 - Caro-Kann Defense
            {d2, d4},   // 2.d4
            {d7, d5}    // 2...d5 - challenges center with c6 support
        });

        // ========== 1.d4 OPENINGS ==========

        // Queen's Gambit: 1.d4 d5 2.c4
        // Classic opening offering pawn sacrifice for central control
        AddBookLine({
            {d2, d4},   // 1.d4
            {d7, d5},   // 1...d5
            {c2, c4}    // 2.c4 - Queen's Gambit offered
        });

        // King's Indian Defense: 1.d4 Nf6 2.c4 g6 3.Nc3
        // Hypermodern defense allowing White center then counterattacking
        AddBookLine({
            {d2, d4},   // 1.d4
            {g8, f6},   // 1...Nf6 - Indian Defense
            {c2, c4},   // 2.c4
            {g7, g6},   // 2...g6 - King's Indian setup
            {b1, c3}    // 3.Nc3
        });

		// Giuoco Piano (Italian Game continuation): 1.e4 e5 2.Nf3 Nc6 3.Bc4 Bc5 4.c3
        // Classical setup with strong center control
        AddBookLine({
            {e2, e4},   // 1.e4
            {e7, e5},   // 1...e5
            {g1, f3},   // 2.Nf3
            {b8, c6},   // 2...Nc6
            {f1, c4},   // 3.Bc4
            {f8, c5},   // 3...Bc5
            {c2, c3}    // 4.c3 - prepares d4 central thrust
        });

        // Nimzo-Indian Defense: 1.d4 Nf6 2.c4 e6 3.Nc3 Bb4
        // Hypermodern defense with piece pressure on center
        AddBookLine({
            {d2, d4},   // 1.d4
            {g8, f6},   // 1...Nf6
            {c2, c4},   // 2.c4
            {e7, e6},   // 2...e6
            {b1, c3},   // 3.Nc3
            {f8, b4}    // 3...Bb4 - pins knight
        });

        // Scotch Game: 1.e4 e5 2.Nf3 Nc6 3.d4
        // Sharp tactical opening with early central tension
        AddBookLine({
            {e2, e4},   // 1.e4
            {e7, e5},   // 1...e5
            {g1, f3},   // 2.Nf3
            {b8, c6},   // 2...Nc6
            {d2, d4}    // 3.d4 - aggressive center break
        });

        // ========== ADDITIONAL OPENING LINES ==========

        // Additional square definitions for new lines
        constexpr int a3 = AlgebraicToSquare('a', '3');
        constexpr int a6 = AlgebraicToSquare('a', '6');
        constexpr int b3 = AlgebraicToSquare('b', '3');
        constexpr int b6 = AlgebraicToSquare('b', '6');
        constexpr int e3 = AlgebraicToSquare('e', '3');
        constexpr int d3 = AlgebraicToSquare('d', '3');
        constexpr int f4 = AlgebraicToSquare('f', '4');
        constexpr int f5 = AlgebraicToSquare('f', '5');
        constexpr int h3 = AlgebraicToSquare('h', '3');
        constexpr int a7 = AlgebraicToSquare('a', '7');

        // Sicilian Defense - Najdorf Variation: 1.e4 c5 2.Nf3 d6 3.d4 cxd4 4.Nxd4 Nf6 5.Nc3 a6
        AddBookLine({
            {e2, e4},   // 1.e4
            {c7, c5},   // 1...c5
            {g1, f3},   // 2.Nf3
            {d7, d6},   // 2...d6
            {d2, d4},   // 3.d4
            {c5, d4},   // 3...cxd4
            {f3, d4},   // 4.Nxd4
            {g8, f6},   // 4...Nf6
            {b1, c3},   // 5.Nc3
            {a7, a6}    // 5...a6 - Najdorf move
        });

        // Sicilian Defense - Dragon Variation: 1.e4 c5 2.Nf3 d6 3.d4 cxd4 4.Nxd4 Nf6 5.Nc3 g6
        AddBookLine({
            {e2, e4},   // 1.e4
            {c7, c5},   // 1...c5
            {g1, f3},   // 2.Nf3
            {d7, d6},   // 2...d6
            {d2, d4},   // 3.d4
            {c5, d4},   // 3...cxd4
            {f3, d4},   // 4.Nxd4
            {g8, f6},   // 4...Nf6
            {b1, c3},   // 5.Nc3
            {g7, g6}    // 5...g6 - Dragon setup
        });

        // French Defense - Winawer Variation: 1.e4 e6 2.d4 d5 3.Nc3 Bb4
        AddBookLine({
            {e2, e4},   // 1.e4
            {e7, e6},   // 1...e6
            {d2, d4},   // 2.d4
            {d7, d5},   // 2...d5
            {b1, c3},   // 3.Nc3
            {f8, b4}    // 3...Bb4 - Winawer
        });

        // French Defense - Classical Variation: 1.e4 e6 2.d4 d5 3.Nc3 Nf6
        AddBookLine({
            {e2, e4},   // 1.e4
            {e7, e6},   // 1...e6
            {d2, d4},   // 2.d4
            {d7, d5},   // 2...d5
            {b1, c3},   // 3.Nc3
            {g8, f6}    // 3...Nf6 - Classical
        });

        // Caro-Kann Defense - Classical Variation: 1.e4 c6 2.d4 d5 3.Nc3 dxe4 4.Nxe4 Bf5
        AddBookLine({
            {e2, e4},   // 1.e4
            {c7, c6},   // 1...c6
            {d2, d4},   // 2.d4
            {d7, d5},   // 2...d5
            {b1, c3},   // 3.Nc3
            {d5, e4},   // 3...dxe4
            {c3, e4},   // 4.Nxe4
            {c8, f5}    // 4...Bf5 - Classical development
        });

        // Caro-Kann Defense - Advance Variation: 1.e4 c6 2.d4 d5 3.e5 Bf5
        AddBookLine({
            {e2, e4},   // 1.e4
            {c7, c6},   // 1...c6
            {d2, d4},   // 2.d4
            {d7, d5},   // 2...d5
            {e4, e5},   // 3.e5 - Advance
            {c8, f5}    // 3...Bf5 - Standard response
        });

        // Slav Defense: 1.d4 d5 2.c4 c6
        AddBookLine({
            {d2, d4},   // 1.d4
            {d7, d5},   // 1...d5
            {c2, c4},   // 2.c4
            {c7, c6}    // 2...c6 - Slav
        });

        // Slav Defense - Main Line: 1.d4 d5 2.c4 c6 3.Nf3 Nf6 4.Nc3 dxc4
        AddBookLine({
            {d2, d4},   // 1.d4
            {d7, d5},   // 1...d5
            {c2, c4},   // 2.c4
            {c7, c6},   // 2...c6
            {g1, f3},   // 3.Nf3
            {g8, f6},   // 3...Nf6
            {b1, c3},   // 4.Nc3
            {d5, c4}    // 4...dxc4
        });

        // Nimzo-Indian Defense - Rubinstein Variation: 1.d4 Nf6 2.c4 e6 3.Nc3 Bb4 4.e3
        AddBookLine({
            {d2, d4},   // 1.d4
            {g8, f6},   // 1...Nf6
            {c2, c4},   // 2.c4
            {e7, e6},   // 2...e6
            {b1, c3},   // 3.Nc3
            {f8, b4},   // 3...Bb4
            {e2, e3}    // 4.e3 - Rubinstein
        });

        // Nimzo-Indian Defense - Classical Variation: 1.d4 Nf6 2.c4 e6 3.Nc3 Bb4 4.Qc2
        constexpr int d1 = AlgebraicToSquare('d', '1');
        constexpr int c2_sq = AlgebraicToSquare('c', '2');
        AddBookLine({
            {d2, d4},   // 1.d4
            {g8, f6},   // 1...Nf6
            {c2, c4},   // 2.c4
            {e7, e6},   // 2...e6
            {b1, c3},   // 3.Nc3
            {f8, b4},   // 3...Bb4
            {d1, c2_sq} // 4.Qc2 - Classical
        });

        // English Opening: 1.c4
        AddBookLine({
            {c2, c4}    // 1.c4 - English Opening
        });

        // English Opening - Symmetrical: 1.c4 c5 2.Nc3 Nc6 3.g3
        constexpr int g2 = AlgebraicToSquare('g', '2');
        constexpr int g3_sq = AlgebraicToSquare('g', '3');
        AddBookLine({
            {c2, c4},   // 1.c4
            {c7, c5},   // 1...c5
            {b1, c3},   // 2.Nc3
            {b8, c6},   // 2...Nc6
            {g2, g3_sq} // 3.g3 - Fianchetto setup
        });

        // English Opening - Reversed Sicilian: 1.c4 e5 2.Nc3 Nf6 3.Nf3
        AddBookLine({
            {c2, c4},   // 1.c4
            {e7, e5},   // 1...e5
            {b1, c3},   // 2.Nc3
            {g8, f6},   // 2...Nf6
            {g1, f3}    // 3.Nf3
        });

        // Queen's Gambit Declined: 1.d4 d5 2.c4 e6 3.Nc3 Nf6
        AddBookLine({
            {d2, d4},   // 1.d4
            {d7, d5},   // 1...d5
            {c2, c4},   // 2.c4
            {e7, e6},   // 2...e6 - QGD
            {b1, c3},   // 3.Nc3
            {g8, f6}    // 3...Nf6
        });

        // Queen's Gambit Accepted: 1.d4 d5 2.c4 dxc4 3.Nf3 Nf6
        AddBookLine({
            {d2, d4},   // 1.d4
            {d7, d5},   // 1...d5
            {c2, c4},   // 2.c4
            {d5, c4},   // 2...dxc4 - QGA
            {g1, f3},   // 3.Nf3
            {g8, f6}    // 3...Nf6
        });

        // King's Indian Defense - Classical: 1.d4 Nf6 2.c4 g6 3.Nc3 Bg7 4.e4 d6
        AddBookLine({
            {d2, d4},   // 1.d4
            {g8, f6},   // 1...Nf6
            {c2, c4},   // 2.c4
            {g7, g6},   // 2...g6
            {b1, c3},   // 3.Nc3
            {f8, g7},   // 3...Bg7
            {e2, e4},   // 4.e4
            {d7, d6}    // 4...d6
        });

        g_bookInitialized = true;
    }

    // Probe the opening book for a move in the current position
    // Returns a randomly selected book move if position is found, nullopt otherwise
    // Random selection from available moves provides variety in play
    std::optional<Move> ProbeBook(const Board& board, int plyCount)
    {
        // Lazy initialization - ensure book is ready on first probe
        if (!g_bookInitialized)
        {
            InitializeOpeningBook();
        }

        // Exit book after configured ply depth (default 8 half-moves)
        // This ensures AI starts thinking independently in middlegame
        if (plyCount >= BOOK_MAX_PLIES)
        {
            return std::nullopt;
        }

        // Search book for current position using Zobrist hash
        uint64_t key = board.GetZobristKey();
        auto it = std::find_if(g_bookEntries.begin(), g_bookEntries.end(),
            [key](const BookEntry& e) { return e.zobristKey == key; });

        // Position not in book
        if (it == g_bookEntries.end() || it->moveCount == 0)
        {
            return std::nullopt;
        }

        // Randomly select one of the available book moves
        // Using static RNG to maintain state across calls
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, it->moveCount - 1);
        int selectedIndex = dis(gen);

        // Unpack the compact 16-bit move representation
        Move bookMove = UnpackToMove(it->moves[selectedIndex]);

        // Validate book move against current legal moves
        // This guards against book corruption or hash collisions
        auto legalMoves = board.GenerateLegalMoves();
        for (const auto& legal : legalMoves)
        {
            if (legal.GetFrom() == bookMove.GetFrom() &&
                legal.GetTo() == bookMove.GetTo() &&
                legal.GetPromotion() == bookMove.GetPromotion())
            {
                return legal; // Return fully-formed legal move object
            }
        }

        // Book move not legal - hash collision or corrupted book
        return std::nullopt;
    }
}
