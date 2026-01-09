// ChessGame.h
// Chess game controller and AI player interface
// Provides game management, move execution, AI opponents, and PGN support
#pragma once

#include "../Engine/Board.h"
#include "VectorRenderer.h"
#include "../Engine/TranspositionTable.h"
#include "../Engine/Neural/HybridEvaluator.h"
#include <chrono>
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <atomic>

namespace Chess
{
    // ========== GAME MODE ==========
    // Defines player configuration for game
    enum class GameMode
    {
        HumanVsHuman,        // Both players controlled by user
        HumanVsComputer,     // Human plays white, AI plays black
        ComputerVsComputer   // Both players are AI (demo/analysis mode)
    };

    // ========== AI DIFFICULTY ==========
    // AI strength levels 1-10
    // Controls search depth, time allocation, and evaluation complexity
    using DifficultyLevel = int;

    namespace Difficulty
    {
        constexpr int MIN = 1;      // Random moves
        constexpr int MAX = 10;     // Deep search with all optimizations
        constexpr int EASY = 2;     // Basic evaluation
        constexpr int MEDIUM = 5;   // Standard search
        constexpr int HARD = 7;     // Advanced pruning
        constexpr int EXPERT = 9;   // Maximum strength
    }

    // ========== AI PLAYER ==========
    // Chess engine with alpha-beta search and iterative deepening
    // Uses transposition table, move ordering, and parallel search
    class AIPlayer
    {
    public:
        // Initialize AI with specified difficulty level
        // Higher difficulty = deeper search + more aggressive pruning
        AIPlayer(DifficultyLevel difficulty);

        // Calculate best move for current position
        // Uses iterative deepening with time management
        // @param board: Current position to analyze
        // @param maxTimeMs: Maximum thinking time in milliseconds
        // @return: Best move found within time limit
        Move CalculateBestMove(const Board& board, int maxTimeMs = 5000);

        // Update AI difficulty and transposition table size
        void SetDifficulty(DifficultyLevel difficulty);
        DifficultyLevel GetDifficulty() const { return m_difficulty; }

        // Configure number of search threads (root-parallel search)
        void SetThreads(int threads);

        // Abort current search immediately
        // Used when user stops analysis or game ends
        void AbortSearch();

        // Load NNUE network for neural evaluation
        // @param filename: Path to .nnue file
        // @return: true if loading succeeded
        bool LoadNnue(const std::string& filename);

        // Check if NNUE is available
        bool IsNnueAvailable() const { return m_evaluator.IsNnueAvailable(); }

        // Get evaluator for direct access
        Neural::HybridEvaluator& GetEvaluator() { return m_evaluator; }

    private:
        DifficultyLevel m_difficulty;
        std::chrono::steady_clock::time_point m_searchStartTime;
        int m_maxSearchTimeMs;

        // Transposition table for position caching
        TranspositionTable m_transpositionTable;

        static constexpr int MAX_PLY = 64;  // Maximum search depth

        // Move ordering heuristics for main thread
        Move m_killerMoves[MAX_PLY][2];     // Killer move heuristic
        std::atomic<int> m_history[2][64][64];           // History heuristic (per side)

        int m_numThreads = 1;               // Parallel search threads
        std::atomic<bool> m_abortSearch{false};  // Search abort flag

        // NNUE evaluator (hybrid mode with classical fallback)
        Neural::HybridEvaluator m_evaluator;

        // Thread-local heuristics to prevent data races
        struct ThreadLocalData {
            Move killerMoves[MAX_PLY][2];

            ThreadLocalData() {
                for (int i = 0; i < MAX_PLY; ++i) {
                    killerMoves[i][0] = Move();
                    killerMoves[i][1] = Move();
                }
            }
        };

        // ========== SEARCH ALGORITHMS ==========
        
        // Alpha-beta negamax with pruning optimizations
        int AlphaBeta(Board& board, int depth, int alpha, int beta, int ply);
        
        // Quiescence search - tactical move resolution
        int QuiescenceSearch(Board& board, int alpha, int beta, int ply, int qDepth);

        // Worker thread search with thread-local data
        int WorkerAlphaBeta(Board& board, int depth, int alpha, int beta, int ply,
                            ThreadLocalData& tld);
        int WorkerQuiescence(Board& board, int alpha, int beta, int ply, int qDepth,
                             ThreadLocalData& tld);

        // Check if search time limit exceeded
        bool ShouldStop() const;

        // ========== MOVE ORDERING ==========
        // Better move ordering = more beta cutoffs = faster search
        
        void OrderMoves(MoveList& moves, const Board& board, Move ttMove, int ply);
        int ScoreMove(const Move& move, const Board& board, Move ttMove, int ply);

        void OrderMovesWorker(MoveList& moves, const Board& board, Move ttMove,
                              int ply, const ThreadLocalData& tld);
        int ScoreMoveWorker(const Move& move, const Board& board, Move ttMove, int ply,
                            const ThreadLocalData& tld);
		
        void OrderMovesSimple(MoveList& moves, const Board& board, Move ttMove);

		// Filter pseudo-legal moves to legal moves by verifying king safety
        MoveList FilterLegalMoves(Board& board, const MoveList& pseudoMoves,
                                  PlayerColor sideToMove, PlayerColor opponentColor);

        // Static Exchange Evaluation - evaluate capture sequences
        int SEE(const Board& board, const Move& move) const;
        std::vector<int> GetSmallestAttacker(const std::array<Piece, SQUARE_COUNT>& pieces,
                                             int square, PlayerColor attackerColor) const;
    };

    // ========== PGN GAME RECORD ==========
    // Stores game metadata and move history in PGN format
    struct GameRecord
    {
        std::string event = "Casual Game";
        std::string site = "Local";
        std::string date;
        std::string white = "Player 1";
        std::string black = "Player 2";
        std::string result = "*";  // * = in progress, 1-0 / 0-1 / 1/2-1/2
        
        std::vector<std::pair<std::string, std::string>> moves;  // (white_move, black_move)
        std::string finalFEN;
    };

    // ========== CHESS GAME CONTROLLER ==========
    // Main game logic controller - manages board state, move execution,
    // player interaction, AI opponents, and game history
    class ChessGame
    {
    public:
        ChessGame();
        
        // ========== GAME MANAGEMENT ==========
        
		// Start new game with specified mode
		// @param mode: Game mode (Human vs Human, Human vs Computer, etc.)
		// @param humanPlaysWhite: true = human plays white, false = human plays black (Computer mode only)
		void NewGame(GameMode mode = GameMode::HumanVsHuman, bool humanPlaysWhite = true);
        
        // Load position from FEN string
        void LoadGame(const std::string& fen);
        
        // Save game to file (custom format)
        void SaveGame(const std::string& filename) const;
        
        // Load game from PGN file
        void LoadPGN(const std::string& filename);
        
        // Save game to PGN file
        void SavePGN(const std::string& filename) const;
        
        // ========== STATE ACCESSORS ==========
        
        [[nodiscard]] const Board& GetBoard() const { return m_board; }
        [[nodiscard]] GameState GetGameState() const { return m_board.GetGameState(); }
        [[nodiscard]] PlayerColor GetCurrentPlayer() const { return m_board.GetCurrentPlayer(); }
        [[nodiscard]] const MoveList& GetLegalMoves() const { return m_legalMoves; }
        [[nodiscard]] const std::vector<int>& GetHighlightedSquares() const { return m_highlightedSquares; }
        [[nodiscard]] GameMode GetGameMode() const { return m_gameMode; }
        
        // ========== MOVE EXECUTION ==========
        
        // Make move using square indices
        bool MakeMove(int from, int to, PieceType promotion = PieceType::None);
        
        // Make move using Move object
        bool MakeMove(const Move& move);
        
        // Undo last move (perfect undo with full state restoration)
        bool UndoMove();
        
        // Redo previously undone move
        bool RedoMove();
        
        // ========== AI CONTROL ==========
        
        // Set AI difficulty for specified color
        void SetAIDifficulty(PlayerColor color, DifficultyLevel difficulty);
        
        // Calculate and execute AI move for current player
        void MakeAIMove();
        
        // ========== MOVE HISTORY ==========
        
        [[nodiscard]] const std::vector<Move>& GetMoveHistory() const { return m_moveHistory; }
        
        // Get move history as formatted text (algebraic notation)
        [[nodiscard]] std::string GetMoveHistoryText() const;
        
        [[nodiscard]] const GameRecord& GetGameRecord() const { return m_gameRecord; }
        
        // ========== USER INTERACTION ==========
        
        // Handle square selection for move input
        void SelectSquare(int square);
        
        // Clear current square selection
        void ClearSelection();
        
        [[nodiscard]] int GetSelectedSquare() const { return m_selectedSquare; }
        
        // Get valid destination squares for piece on given square
        [[nodiscard]] std::vector<int> GetValidTargetSquares(int fromSquare) const;
        
        // ========== CONFIGURATION ==========

        void SetPlayerName(PlayerColor color, const std::string& name);
        void SetGameMode(GameMode mode) { m_gameMode = mode; }
        void SetTimeControl(int minutes, int incrementSeconds = 0);
        void SetMaxUndoDepth(int depth);
        
        // ========== HELPER METHODS ==========
        
        // Check if it's AI's turn to move
        [[nodiscard]] bool IsAITurn() const;
        
        // Get AI player for current side to move
        [[nodiscard]] std::unique_ptr<AIPlayer>& GetCurrentAIPlayer();
        
        // Check if pawn promotion is required for this move
        [[nodiscard]] bool IsPromotionRequired(int from, int to) const;
        
    private:
        Board m_board;
        GameMode m_gameMode = GameMode::HumanVsHuman;
        
        // ========== PLAYER INFORMATION ==========
        struct PlayerInfo
        {
            std::string name;
            bool isAI = false;
            DifficultyLevel aiDifficulty = 5;
            int timeRemainingMs = 0;
        };
        
        PlayerInfo m_players[2];  // [White, Black]

        // ========== UI STATE ==========
        int m_selectedSquare = -1;
        MoveList m_legalMoves;
        std::vector<int> m_highlightedSquares;

        // ========== MOVE HISTORY ==========
        std::vector<Move> m_moveHistory;
        std::vector<Board> m_boardHistory;  // For undo/redo
        size_t m_currentHistoryIndex = 0;
        int m_maxUndoDepth = 3;  // Maximum undo moves (1-3)

        GameRecord m_gameRecord;

        // ========== AI PLAYERS ==========
        std::unique_ptr<AIPlayer> m_aiWhite;
        std::unique_ptr<AIPlayer> m_aiBlack;

        DifficultyLevel m_currentDifficulty = 5;
        
        // ========== TIME CONTROL ==========
        struct TimeControl
        {
            int baseTimeMs = 600000;   // 10 minutes default
            int incrementMs = 0;       // Fischer increment
        };
        
        TimeControl m_timeControl;
        
        // ========== PRIVATE HELPERS ==========
        
        // Update internal legal moves cache
        void UpdateLegalMoves();
        
        // Update squares to highlight (valid destinations)
        void UpdateHighlightedSquares();
        
        // Add move to history and save board state
        void AddMoveToHistory(const Move& move);
        
        // Update PGN game record with current state
        void UpdateGameRecord();
        
        // Get user's promotion choice (interactive)
        PieceType GetPromotionChoice() const;
        
        // Timer management for time controls
        void StartPlayerTimer(PlayerColor color);
        void StopPlayerTimer(PlayerColor color);
    };

    // ========== PGN PARSER ==========
    // Portable Game Notation (PGN) import/export
    class PGNParser
    {
    public:
        // Parse PGN string into GameRecord
        static GameRecord ParsePGN(const std::string& pgn);
        
        // Generate PGN string from GameRecord
        static std::string GeneratePGN(const GameRecord& record);
        
    private:
        // Parse PGN header tag (e.g., [White "Kasparov"])
        static bool ParseHeader(const std::string& line, GameRecord& record);
        
        // Parse move in Standard Algebraic Notation
        static bool ParseMove(const std::string& moveStr, Board& board, std::string& sanMove);
        
        // Convert internal Move to SAN
        static std::string MoveToSAN(const Move& move, const Board& board);
        
        // Convert SAN string to internal Move
        static Move SANToMove(const std::string& san, const Board& board);
    };

    // ========== UTILITY FUNCTIONS ==========
    
    // Get current date/time formatted for PGN (YYYY.MM.DD)
    std::string GetCurrentDateTime();
    
    // Convert player color to string ("White" / "Black")
    std::string ColorToString(PlayerColor color);
    
    // Convert game state to string ("Playing" / "Check" / "Checkmate" / etc.)
    std::string GameStateToString(GameState state);
}