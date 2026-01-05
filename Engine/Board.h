// Board.h
// Chess board representation and game state management interface
//
// The Board class is the core data structure of the chess engine, representing
// the complete game state including piece positions, castling rights, en passant,
// move counters, and game history for undo functionality.
//
// Key features:
// - 64-square array representation with piece lists for fast iteration
// - Incremental Zobrist hashing for transposition detection
// - Incremental score maintenance (material + PST)
// - Complete undo/redo support via move history
// - FEN import/export for position serialization
// - Legal move generation and validation
// - Game state detection (checkmate, stalemate, draw)
//
// The Board is designed for high-performance search with:
// - O(1) king position lookup
// - O(pieces) move generation via piece lists
// - Incremental updates for hash and evaluation

#pragma once

#include "Move.h"
#include "MoveGenerator.h"
#include <array>
#include <memory>
#include <string>
#include <optional>
#include <stack>

namespace Chess
{
    // ========== PIECE LIST ==========
    // Compact structure for tracking positions of all pieces of one color
    // Enables fast iteration over pieces without scanning entire board
    // Maximum 16 pieces per side (initial setup has 16: 8 pawns + 8 pieces)
    struct PieceList
    {
        std::array<int, 16> squares;  // Square indices of pieces
        int count = 0;                // Number of pieces currently in list

        // Add piece at square to list
        constexpr void Add(int square) noexcept
        {
            if (count < 16) squares[count++] = square;
        }

        // Remove piece at square from list
        // Uses swap-with-last for O(1) removal
        constexpr void Remove(int square) noexcept
        {
            for (int i = 0; i < count; ++i)
            {
                if (squares[i] == square)
                {
                    squares[i] = squares[--count];  // Swap with last element
                    return;
                }
            }
        }

        // Update piece position (for moves)
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

        // Clear all pieces from list
        constexpr void Clear() noexcept
        {
            count = 0;
        }
    };

    // Forward declaration for FEN parser
    class FENParser;

    // ========== BOARD CLASS ==========
    // Main chess board representation and game logic
    class Board
    {
    public:
        // ========== CONSTRUCTION & INITIALIZATION ==========
        
        Board();                            // Initialize to starting position
        explicit Board(const std::string& fen);  // Initialize from FEN string

        // ========== ACCESSORS ==========

        // Get piece array (read-only)
        [[nodiscard]] constexpr const std::array<Piece, SQUARE_COUNT>& GetPieces() const noexcept
        {
            return m_board;
        }

        // Get piece at square index (0-63)
        [[nodiscard]] constexpr Piece GetPieceAt(int square) const
        {
            if (square < 0 || square >= SQUARE_COUNT)
                return EMPTY_PIECE;
            return m_board[square];
        }

        // Get piece list for color (for fast iteration)
        [[nodiscard]] constexpr const PieceList& GetPieceList(PlayerColor color) const noexcept
        {
            return m_pieceLists[static_cast<int>(color)];
        }

        // Get piece at (file, rank) coordinates
        [[nodiscard]] constexpr Piece GetPieceAt(int file, int rank) const
        {
            return GetPieceAt(CoordinateToIndex(file, rank));
        }

        // Get side to move
        [[nodiscard]] constexpr PlayerColor GetSideToMove() const noexcept
        {
            return m_sideToMove;
        }

        // Get en passant target square (-1 if none)
        [[nodiscard]] constexpr int GetEnPassantSquare() const noexcept
        {
            return m_enPassantSquare;
        }

        // Check castling rights for color
        [[nodiscard]] constexpr bool CanCastleKingside(PlayerColor color) const noexcept
        {
            return m_castlingRights[static_cast<int>(color) * 2];
        }

        [[nodiscard]] constexpr bool CanCastleQueenside(PlayerColor color) const noexcept
        {
            return m_castlingRights[static_cast<int>(color) * 2 + 1];
        }

        // Get move counters
        [[nodiscard]] constexpr int GetHalfMoveClock() const noexcept
        {
            return m_halfMoveClock;
        }

        [[nodiscard]] constexpr int GetFullMoveNumber() const noexcept
        {
            return m_fullMoveNumber;
        }

        // Get king position for color
        [[nodiscard]] constexpr int GetKingSquare(PlayerColor color) const noexcept
        {
            return m_kingSquares[static_cast<int>(color)];
        }

        // Get Zobrist hash key
        [[nodiscard]] constexpr uint64_t GetZobristKey() const noexcept
        {
            return m_zobristKey;
        }

        // Get incrementally maintained score (material + PST)
        [[nodiscard]] constexpr int GetIncrementalScore() const noexcept
        {
            return m_incrementalScore;
        }

        // ========== MOVE GENERATION & VALIDATION ==========

        // Generate all legal moves in current position
        [[nodiscard]] std::vector<Move> GenerateLegalMoves() const;
        
        // Check if specific move is legal
        [[nodiscard]] bool IsMoveLegal(const Move& move) const;

        // ========== MOVE EXECUTION ==========

        // Make move with legality checking (returns false if illegal)
        bool MakeMove(const Move& move);
        
        // Make move without legality checking (used in search)
        bool MakeMoveUnchecked(const Move& move);
        
        // Undo last move (perfect undo with complete state restoration)
        bool UndoMove();

        // Null move pruning support
        bool MakeNullMoveUnchecked();
        bool UndoNullMove();

        // ========== GAME STATE QUERIES ==========

        // Get current game state
        [[nodiscard]] GameState GetGameState() const;
        
        // Check specific conditions
        [[nodiscard]] bool IsInCheck(PlayerColor color) const;
        [[nodiscard]] bool IsCheckmate() const;
        [[nodiscard]] bool IsStalemate() const;
        [[nodiscard]] bool IsDraw() const;
        
        // Count position repetitions (for threefold repetition)
        [[nodiscard]] int CountRepetitions() const;
        
        // Check if material is insufficient for checkmate
        [[nodiscard]] bool IsInsufficientMaterial() const;

        // ========== POSITION MANAGEMENT ==========

        // Reset to standard starting position
        void ResetToStartingPosition();
        
        // Load position from FEN string
        bool LoadFEN(const std::string& fen);
        
        // Generate FEN string from current position
        [[nodiscard]] std::string GetFEN() const;

        // ========== DISPLAY & DEBUGGING ==========

        // Get human-readable board string
        [[nodiscard]] std::string ToString() const;
        
        // Simple material evaluation (for display)
        [[nodiscard]] int EvaluateMaterial() const;
        
        // Legacy alias for GetSideToMove
        [[nodiscard]] PlayerColor GetCurrentPlayer() const noexcept { return m_sideToMove; }

        // Recompute Zobrist hash from scratch (for validation)
        void RecomputeZobristKey();

    private:
        // FENParser and MoveGenerator need access to internal state
        friend class FENParser;
        friend class MoveGenerator;

		// ========== INTERNAL HELPER METHODS ==========

        // Update king position tracking when king moves
        void UpdateKingSquare(PlayerColor color, int newSquare);
        
        // Update castling rights after piece moves (king/rook movement or rook capture)
        void UpdateCastlingRights(int movedFrom, int movedTo, Piece movedPiece);
        
        // Execute special moves (en passant capture, castling rook move)
        void HandleEnPassant(const Move& move);
        void HandleCastling(const Move& move);

        // Check if move would leave king in check (used for move validation)
        bool WouldMoveCauseCheck(const Move& move) const;
        
        // Check if square is attacked by specified color
        bool IsSquareAttacked(int square, PlayerColor attackerColor) const;

        // Incremental evaluation maintenance
        void UpdateIncrementalScore(int square, Piece piece, bool add);
        void RecomputeIncrementalScore();

        // ========== MEMBER VARIABLES ==========

        // Board state: 64 squares with pieces (aligned for cache efficiency)
        alignas(64) std::array<Piece, SQUARE_COUNT> m_board;
        
        // Current side to move
        PlayerColor m_sideToMove = PlayerColor::White;

        // Castling rights: [White KS, White QS, Black KS, Black QS]
        std::array<bool, 4> m_castlingRights = {true, true, true, true};

        // En passant target square (-1 if none available)
        int m_enPassantSquare = -1;
        
        // Fifty-move rule counter (half-moves since last capture/pawn move)
        int m_halfMoveClock = 0;
        
        // Full move counter (incremented after Black's move)
        int m_fullMoveNumber = 1;

        // King position cache for fast check detection [White, Black]
        int m_kingSquares[2] = {-1, -1};

        // Zobrist hash key for position (for transposition table)
        uint64_t m_zobristKey = 0;

        // Piece lists for fast iteration [White, Black]
        PieceList m_pieceLists[2];

        // Incrementally maintained evaluation score (material + PST)
        int m_incrementalScore = 0;

        // Move history for undo functionality
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

        // Move history stack (512 moves should be sufficient)
        std::array<MoveRecord, 512> m_moveHistory;
        int m_historyPly = 0;

        // Null move history for null move pruning
        struct NullMoveRecord
        {
            int previousEnPassant = -1;
            uint64_t previousZobristKey = 0;
        };

        std::array<NullMoveRecord, 512> m_nullMoveHistory;
        int m_nullMovePly = 0;
    };

    // ========== PREDEFINED POSITIONS ==========
    
    namespace Positions
    {
        // Standard chess starting position FEN
        inline const std::string STARTING_POSITION =
            "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";

        // Empty board for testing
        inline const std::string EMPTY_BOARD =
            "8/8/8/8/8/8/8/8 w - - 0 1";

        // Minimal position (kings only)
        inline const std::string KINGS_ONLY =
            "4k3/8/8/8/8/8/8/4K3 w - - 0 1";
    }

    // ========== FEN PARSER ==========
    
    // FEN (Forsyth-Edwards Notation) parser/generator
    // Converts between FEN strings and Board state
    class FENParser
    {
    public:
        // Parse FEN string into Board state
        static bool Parse(const std::string& fen, Board& board);
        
        // Generate FEN string from Board state
        static std::string Generate(const Board& board);

    private:
        // Parse individual FEN fields
        static bool ParsePiecePlacement(const std::string& placement, std::array<Piece, SQUARE_COUNT>& board);
        static bool ParseActiveColor(const std::string& colorStr, PlayerColor& color);
        static bool ParseCastlingRights(const std::string& castlingStr, std::array<bool, 4>& rights);
        static bool ParseEnPassant(const std::string& epStr, int& enPassantSquare);
        static bool ParseMoveClocks(const std::string& halfMoveStr, const std::string& fullMoveStr,
                                   int& halfMoveClock, int& fullMoveNumber);
    };
}