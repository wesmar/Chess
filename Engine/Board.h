// Board.h
#pragma once

#include "Move.h"
#include "MoveGenerator.h"
#include <array>
#include <vector>
#include <memory>
#include <string>
#include <optional>
#include <stack>

namespace Chess
{
    // ---------- Piece List Structure ----------
    struct PieceList
    {
        std::array<int, 16> squares;
        int count = 0;

        constexpr void Add(int square) noexcept
        {
            if (count < 16) squares[count++] = square;
        }

        constexpr void Remove(int square) noexcept
        {
            for (int i = 0; i < count; ++i)
            {
                if (squares[i] == square)
                {
                    squares[i] = squares[--count];
                    return;
                }
            }
        }

        constexpr void Update(int oldSquare, int newSquare) noexcept
        {
            for (int i = 0; i < count; ++i)
            {
                if (squares[i] == oldSquare)
                {
                    squares[i] = newSquare;
                    return;
                }
            }
        }

        constexpr void Clear() noexcept
        {
            count = 0;
        }
    };

    // Forward declarations
    class FENParser;

    // ---------- Board Representation ----------
    // Main chess board class managing position, moves, and game state
    class Board
    {
    public:
        Board();
        explicit Board(const std::string& fen);
        
        // ---------- Board State Accessors ----------
        [[nodiscard]] constexpr const std::array<Piece, SQUARE_COUNT>& GetPieces() const noexcept
        {
            return m_board;
        }
        
        [[nodiscard]] constexpr Piece GetPieceAt(int square) const
        {
            if (square < 0 || square >= SQUARE_COUNT)
                return EMPTY_PIECE;
            return m_board[square];
        }

        // Accessor for optimized iteration in Evaluation and MoveGeneration
        [[nodiscard]] constexpr const PieceList& GetPieceList(PlayerColor color) const noexcept
        {
            return m_pieceLists[static_cast<int>(color)];
        }

        [[nodiscard]] constexpr Piece GetPieceAt(int file, int rank) const
        {
            return GetPieceAt(CoordinateToIndex(file, rank));
        }
        
        [[nodiscard]] constexpr PlayerColor GetSideToMove() const noexcept
        {
            return m_sideToMove;
        }
        
        [[nodiscard]] constexpr int GetEnPassantSquare() const noexcept
        {
            return m_enPassantSquare;
        }
        
        [[nodiscard]] constexpr bool CanCastleKingside(PlayerColor color) const noexcept
        {
            return m_castlingRights[static_cast<int>(color) * 2];
        }
        
        [[nodiscard]] constexpr bool CanCastleQueenside(PlayerColor color) const noexcept
        {
            return m_castlingRights[static_cast<int>(color) * 2 + 1];
        }
        
        [[nodiscard]] constexpr int GetHalfMoveClock() const noexcept
        {
            return m_halfMoveClock;
        }
        
        [[nodiscard]] constexpr int GetFullMoveNumber() const noexcept
        {
            return m_fullMoveNumber;
        }
        
        [[nodiscard]] constexpr int GetKingSquare(PlayerColor color) const noexcept
        {
            return m_kingSquares[static_cast<int>(color)];
        }

        [[nodiscard]] constexpr uint64_t GetZobristKey() const noexcept
        {
            return m_zobristKey;
        }

        [[nodiscard]] constexpr int GetIncrementalScore() const noexcept
        {
            return m_incrementalScore;
        }

        // ---------- Move Generation & Execution ----------
        [[nodiscard]] std::vector<Move> GenerateLegalMoves() const;
        [[nodiscard]] bool IsMoveLegal(const Move& move) const;
        
        bool MakeMove(const Move& move);
        bool MakeMoveUnchecked(const Move& move);
        bool UndoMove();

        bool MakeNullMoveUnchecked();
        bool UndoNullMove();
        
        // ---------- Game State Queries ----------
        [[nodiscard]] GameState GetGameState() const;
        [[nodiscard]] bool IsInCheck(PlayerColor color) const;
        [[nodiscard]] bool IsCheckmate() const;
        [[nodiscard]] bool IsStalemate() const;
        [[nodiscard]] bool IsDraw() const;
        [[nodiscard]] int CountRepetitions() const;
        [[nodiscard]] bool IsInsufficientMaterial() const;

        // ---------- Board Operations ----------
        void ResetToStartingPosition();
        bool LoadFEN(const std::string& fen);
        [[nodiscard]] std::string GetFEN() const;
        
        // ---------- Utility Methods ----------
        [[nodiscard]] std::string ToString() const;
        [[nodiscard]] int EvaluateMaterial() const;
        [[nodiscard]] PlayerColor GetCurrentPlayer() const noexcept { return m_sideToMove; }

        void RecomputeZobristKey();

    private:
        // Friend declarations for FENParser
        friend class FENParser;
        friend class MoveGenerator;

        // ---------- Private Helper Methods ----------
        void UpdateKingSquare(PlayerColor color, int newSquare);
        void UpdateCastlingRights(int movedFrom, int movedTo, Piece movedPiece);
        void HandleEnPassant(const Move& move);
        void HandleCastling(const Move& move);

        bool WouldMoveCauseCheck(const Move& move) const;
        bool IsSquareAttacked(int square, PlayerColor attackerColor) const;

        void UpdateIncrementalScore(int square, Piece piece, bool add);
        void RecomputeIncrementalScore();

        // ---------- Board State ----------
        std::array<Piece, SQUARE_COUNT> m_board;
        PlayerColor m_sideToMove = PlayerColor::White;

        // Castling rights: [White Kingside, White Queenside, Black Kingside, Black Queenside]
        std::array<bool, 4> m_castlingRights = {true, true, true, true};

        int m_enPassantSquare = -1;       // Target square for en passant capture
        int m_halfMoveClock = 0;          // Fifty-move rule counter
        int m_fullMoveNumber = 1;         // Full move number (increments after black's move)

        // King positions for quick check detection
        int m_kingSquares[2] = {-1, -1};

        // Zobrist hash for position comparison
        uint64_t m_zobristKey = 0;

        // Piece lists for efficient iteration
        PieceList m_pieceLists[2];

        // Incremental evaluation (Material + PST)
        int m_incrementalScore = 0;

        // ---------- Move History ----------
        struct MoveRecord
        {
            Move move;
            Piece capturedPiece = EMPTY_PIECE;
            Piece movedPiece = EMPTY_PIECE;
            int previousEnPassant = -1;
            std::array<bool, 4> previousCastlingRights = {true, true, true, true};
            int previousHalfMoveClock = 0;
            int previousKingSquares[2] = {-1, -1};
            uint64_t previousZobristKey = 0;
            int previousIncrementalScore = 0;
        };

        std::vector<MoveRecord> m_moveHistory;

        // Null move undo information
        struct NullMoveRecord
        {
            int previousEnPassant = -1;
            uint64_t previousZobristKey = 0;
        };

        std::vector<NullMoveRecord> m_nullMoveHistory;
    };

    // ---------- Standard Positions ----------
    namespace Positions
    {
        inline const std::string STARTING_POSITION = 
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
        
        inline const std::string EMPTY_BOARD = 
            "8/8/8/8/8/8/8/8 w - - 0 1";
            
        inline const std::string KINGS_ONLY = 
            "4k3/8/8/8/8/8/8/4K3 w - - 0 1";
    }

    // ---------- FEN Parsing ----------
    // Forsyth-Edwards Notation parser for loading/saving positions
    class FENParser
    {
    public:
        static bool Parse(const std::string& fen, Board& board);
        static std::string Generate(const Board& board);
        
    private:
        static bool ParsePiecePlacement(const std::string& placement, std::array<Piece, SQUARE_COUNT>& board);
        static bool ParseActiveColor(const std::string& colorStr, PlayerColor& color);
        static bool ParseCastlingRights(const std::string& castlingStr, std::array<bool, 4>& rights);
        static bool ParseEnPassant(const std::string& epStr, int& enPassantSquare);
        static bool ParseMoveClocks(const std::string& halfMoveStr, const std::string& fullMoveStr,
                                   int& halfMoveClock, int& fullMoveNumber);
    };
}