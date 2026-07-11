// ChessGame.h
// Chess game controller interface
// Provides game management, move execution, AI opponent wiring, and PGN support.
// The search engine itself (AIPlayer) lives in Engine/Search.h.
#pragma once

#include "../Engine/Board.h"
#include "../Engine/Search.h"
#include "VectorRenderer.h"
#include <chrono>
#include <memory>
#include <vector>
#include <string>
#include <optional>

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