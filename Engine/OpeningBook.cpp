// OpeningBook.cpp
// Hardcoded opening book with main line positions
#include "OpeningBook.h"
#include <algorithm>
#include <random>
#include <vector>

namespace Chess
{
    // Book entry: position hash -> list of possible moves
    struct BookEntry
    {
        uint64_t zobristKey;
        uint16_t moves[4];      // Up to 4 alternative moves per position
        uint8_t moveCount;
    };

    // Storage for computed book entries (populated at runtime)
    static std::vector<BookEntry> g_bookEntries;
    static bool g_bookInitialized = false;

    // Helper: convert algebraic notation to square index (e.g., "e2" -> 12)
    constexpr int AlgebraicToSquare(char file, char rank)
    {
        return (rank - '1') * 8 + (file - 'a');
    }

    // Helper: play a sequence of moves and record position -> next move mapping
    static void AddBookLine(const std::vector<std::pair<int, int>>& moves)
    {
        Board board;
        board.ResetToStartingPosition();

        for (size_t i = 0; i < moves.size(); ++i)
        {
            uint64_t key = board.GetZobristKey();
            int from = moves[i].first;
            int to = moves[i].second;
            uint16_t packedMove = PackMove(from, to);

            // Find or create entry for this position
            auto it = std::find_if(g_bookEntries.begin(), g_bookEntries.end(),
                [key](const BookEntry& e) { return e.zobristKey == key; });

            if (it != g_bookEntries.end())
            {
                // Position exists, add move if not already present
                bool found = false;
                for (uint8_t j = 0; j < it->moveCount; ++j)
                {
                    if (it->moves[j] == packedMove)
                    {
                        found = true;
                        break;
                    }
                }
                if (!found && it->moveCount < 4)
                {
                    it->moves[it->moveCount++] = packedMove;
                }
            }
            else
            {
                // New position, create entry
                BookEntry entry;
                entry.zobristKey = key;
                entry.moves[0] = packedMove;
                entry.moveCount = 1;
                g_bookEntries.push_back(entry);
            }

            // Make the move to advance to next position
            Move move(from, to);
            if (!board.MakeMove(move))
            {
                break; // Invalid move sequence, stop this line
            }
        }
    }

    void InitializeOpeningBook()
    {
        if (g_bookInitialized) return;

        g_bookEntries.clear();
        g_bookEntries.reserve(50); // Preallocate for efficiency

        // Square notation helpers
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
        constexpr int g1 = AlgebraicToSquare('g', '1');
        constexpr int g6 = AlgebraicToSquare('g', '6');
        constexpr int g7 = AlgebraicToSquare('g', '7');
        constexpr int g8 = AlgebraicToSquare('g', '8');
        constexpr int b1 = AlgebraicToSquare('b', '1');
        constexpr int b5 = AlgebraicToSquare('b', '5');
        constexpr int b8 = AlgebraicToSquare('b', '8');

        // Ruy Lopez: 1.e4 e5 2.Nf3 Nc6 3.Bb5
        AddBookLine({
            {e2, e4},   // 1.e4
            {e7, e5},   // 1...e5
            {g1, f3},   // 2.Nf3
            {b8, c6},   // 2...Nc6
            {f1, b5}    // 3.Bb5
        });

        // Italian Game: 1.e4 e5 2.Nf3 Nc6 3.Bc4
        AddBookLine({
            {e2, e4},   // 1.e4
            {e7, e5},   // 1...e5
            {g1, f3},   // 2.Nf3
            {b8, c6},   // 2...Nc6
            {f1, c4}    // 3.Bc4
        });

        // Sicilian Defense: 1.e4 c5 2.Nf3 d6 3.d4 cxd4 4.Nxd4
        AddBookLine({
            {e2, e4},   // 1.e4
            {c7, c5},   // 1...c5
            {g1, f3},   // 2.Nf3
            {d7, d6},   // 2...d6
            {d2, d4},   // 3.d4
            {c5, d4},   // 3...cxd4
            {f3, d4}    // 4.Nxd4
        });

        // Queen's Gambit: 1.d4 d5 2.c4
        AddBookLine({
            {d2, d4},   // 1.d4
            {d7, d5},   // 1...d5
            {c2, c4}    // 2.c4
        });

        // King's Indian Defense: 1.d4 Nf6 2.c4 g6 3.Nc3
        AddBookLine({
            {d2, d4},   // 1.d4
            {g8, f6},   // 1...Nf6
            {c2, c4},   // 2.c4
            {g7, g6},   // 2...g6
            {b1, c3}    // 3.Nc3
        });

        // French Defense: 1.e4 e6 2.d4 d5
        AddBookLine({
            {e2, e4},   // 1.e4
            {e7, e6},   // 1...e6
            {d2, d4},   // 2.d4
            {d7, d5}    // 2...d5
        });

        // Caro-Kann Defense: 1.e4 c6 2.d4 d5
        AddBookLine({
            {e2, e4},   // 1.e4
            {c7, c6},   // 1...c6
            {d2, d4},   // 2.d4
            {d7, d5}    // 2...d5
        });

        g_bookInitialized = true;
    }

    std::optional<Move> ProbeBook(const Board& board, int plyCount)
    {
        // Ensure book is initialized
        if (!g_bookInitialized)
        {
            InitializeOpeningBook();
        }

        // Only use book in early game
        if (plyCount >= BOOK_MAX_PLIES)
        {
            return std::nullopt;
        }

        // Find position in book
        uint64_t key = board.GetZobristKey();
        auto it = std::find_if(g_bookEntries.begin(), g_bookEntries.end(),
            [key](const BookEntry& e) { return e.zobristKey == key; });

        if (it == g_bookEntries.end() || it->moveCount == 0)
        {
            return std::nullopt; // Position not in book
        }

        // Select random move from available book moves
        static std::random_device rd;
        static std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, it->moveCount - 1);
        int selectedIndex = dis(gen);

        Move bookMove = UnpackToMove(it->moves[selectedIndex]);

        // Verify the book move is legal in current position
        auto legalMoves = board.GenerateLegalMoves();
        for (const auto& legal : legalMoves)
        {
            if (legal.GetFrom() == bookMove.GetFrom() &&
                legal.GetTo() == bookMove.GetTo() &&
                legal.GetPromotion() == bookMove.GetPromotion())
            {
                return legal; // Return the legal move object
            }
        }

        return std::nullopt; // Book move not legal (shouldn't happen with correct book)
    }
}
