// ChessGame.h
#pragma once

#include "../Engine/Board.h"
#include "VectorRenderer.h"
#include "../Engine/TranspositionTable.h"
#include <chrono>
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <atomic>

namespace Chess
{
    // Game mode enumeration
    enum class GameMode
    {
        HumanVsHuman,
        HumanVsComputer,
        ComputerVsComputer
    };

    // AI difficulty level (1-10)
    using DifficultyLevel = int;

    namespace Difficulty
    {
        constexpr int MIN = 1;
        constexpr int MAX = 10;
        constexpr int EASY = 2;
        constexpr int MEDIUM = 5;
        constexpr int HARD = 7;
        constexpr int EXPERT = 9;
    }

    // AI player using alpha-beta search with iterative deepening
    class AIPlayer
    {
    public:
        AIPlayer(DifficultyLevel difficulty);

        // Calculate best move for current position
        Move CalculateBestMove(const Board& board, int maxTimeMs = 5000);

        void SetDifficulty(DifficultyLevel difficulty);
        DifficultyLevel GetDifficulty() const { return m_difficulty; }

        void SetThreads(int threads);

    private:
        DifficultyLevel m_difficulty;
        std::chrono::steady_clock::time_point m_searchStartTime;
        int m_maxSearchTimeMs;

        TranspositionTable m_transpositionTable;

        static constexpr int MAX_PLY = 64;
        Move m_killerMoves[MAX_PLY][2]; // Killer move heuristic
        int m_history[2][64][64]; // History heuristic: separate tables for each side to move

        int m_numThreads = 1;
        std::atomic<bool> m_abortSearch{false};

        // Search algorithms
        int AlphaBeta(Board& board, int depth, int alpha, int beta, int ply);
        int QuiescenceSearch(Board& board, int alpha, int beta, int ply, int qDepth);

        bool ShouldStop() const;

        // Move ordering for better pruning
        void OrderMoves(std::vector<Move>& moves, const Board& board, Move ttMove, int ply);
        int ScoreMove(const Move& move, const Board& board, Move ttMove, int ply);

        void HelperSearch(Board board, int depth);
        int HelperAlphaBeta(Board& board, int depth, int alpha, int beta, int ply);
        int HelperQuiescenceSearch(Board& board, int alpha, int beta, int ply, int qDepth);
        void OrderMovesSimple(std::vector<Move>& moves, const Board& board, Move ttMove);
    };

    // PGN game record structure
    struct GameRecord
    {
        std::string event = "Casual Game";
        std::string site = "Local";
        std::string date;
        std::string white = "Player 1";
        std::string black = "Player 2";
        std::string result = "*";
        
        std::vector<std::pair<std::string, std::string>> moves;
        std::string finalFEN;
    };

    // Main chess game controller
    class ChessGame
    {
    public:
        ChessGame();
        
        // Game management
        void NewGame(GameMode mode = GameMode::HumanVsHuman);
        void LoadGame(const std::string& fen);
        void SaveGame(const std::string& filename) const;
        void LoadPGN(const std::string& filename);
        void SavePGN(const std::string& filename) const;
        
        // State accessors
        [[nodiscard]] const Board& GetBoard() const { return m_board; }
        [[nodiscard]] GameState GetGameState() const { return m_board.GetGameState(); }
        [[nodiscard]] PlayerColor GetCurrentPlayer() const { return m_board.GetCurrentPlayer(); }
        [[nodiscard]] const std::vector<Move>& GetLegalMoves() const { return m_legalMoves; }
        [[nodiscard]] const std::vector<int>& GetHighlightedSquares() const { return m_highlightedSquares; }
        [[nodiscard]] GameMode GetGameMode() const { return m_gameMode; }
        
        // Move execution
        bool MakeMove(int from, int to, PieceType promotion = PieceType::None);
        bool MakeMove(const Move& move);
        bool UndoMove();
        bool RedoMove();
        
        // AI control
        void SetAIDifficulty(PlayerColor color, DifficultyLevel difficulty);
        void MakeAIMove();
        
        // Move history
        [[nodiscard]] const std::vector<Move>& GetMoveHistory() const { return m_moveHistory; }
        [[nodiscard]] std::string GetMoveHistoryText() const;
        [[nodiscard]] const GameRecord& GetGameRecord() const { return m_gameRecord; }
        
        // User interaction
        void SelectSquare(int square);
        void ClearSelection();
        [[nodiscard]] int GetSelectedSquare() const { return m_selectedSquare; }
        [[nodiscard]] std::vector<int> GetValidTargetSquares(int fromSquare) const;
        
        // Configuration
        void SetPlayerName(PlayerColor color, const std::string& name);
        void SetGameMode(GameMode mode) { m_gameMode = mode; }
        void SetTimeControl(int minutes, int incrementSeconds = 0);
        
        // Public helper methods
        [[nodiscard]] bool IsAITurn() const;
        [[nodiscard]] std::unique_ptr<AIPlayer>& GetCurrentAIPlayer();
        [[nodiscard]] bool IsPromotionRequired(int from, int to) const;
        
    private:
        Board m_board;
        GameMode m_gameMode = GameMode::HumanVsHuman;
        
        // Player information
        struct PlayerInfo
        {
            std::string name;
            bool isAI = false;
            DifficultyLevel aiDifficulty = 5;
            int timeRemainingMs = 0;
        };
        
        PlayerInfo m_players[2];

        // UI state
        int m_selectedSquare = -1;
        std::vector<Move> m_legalMoves;
        std::vector<int> m_highlightedSquares;

        // Move history for undo/redo
        std::vector<Move> m_moveHistory;
        std::vector<Board> m_boardHistory;
        size_t m_currentHistoryIndex = 0;

        GameRecord m_gameRecord;

        // AI players
        std::unique_ptr<AIPlayer> m_aiWhite;
        std::unique_ptr<AIPlayer> m_aiBlack;

        DifficultyLevel m_currentDifficulty = 5;
        
        // Time control
        struct TimeControl
        {
            int baseTimeMs = 600000;   // 10 minutes default
            int incrementMs = 0;
        };
        
        TimeControl m_timeControl;
        
        // Private helper methods
        void UpdateLegalMoves();
        void UpdateHighlightedSquares();
        void AddMoveToHistory(const Move& move);
        void UpdateGameRecord();
        
        PieceType GetPromotionChoice() const;
        
        void StartPlayerTimer(PlayerColor color);
        void StopPlayerTimer(PlayerColor color);
    };

    // PGN (Portable Game Notation) parser
    class PGNParser
    {
    public:
        static GameRecord ParsePGN(const std::string& pgn);
        static std::string GeneratePGN(const GameRecord& record);
        
    private:
        static bool ParseHeader(const std::string& line, GameRecord& record);
        static bool ParseMove(const std::string& moveStr, Board& board, std::string& sanMove);
        static std::string MoveToSAN(const Move& move, const Board& board);
        static Move SANToMove(const std::string& san, const Board& board);
    };

    // Utility functions
    std::string GetCurrentDateTime();
    std::string ColorToString(PlayerColor color);
    std::string GameStateToString(GameState state);
}