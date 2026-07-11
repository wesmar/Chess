// ChessGame.cpp
// Chess game controller: game state management, move execution and history,
// AI opponent wiring, PGN import/export, and user interaction helpers.
// The search engine itself lives in Engine/Search.cpp.

#define NOMINMAX
#include "ChessGame.h"
#include "../Engine/Evaluation.h"
#include "../Engine/MoveGenerator.h"
#include <algorithm>
#include <random>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>

namespace Chess
{

    // ---------- ChessGame Implementation ----------
    
    // Constructor - initialize game with default settings
    ChessGame::ChessGame()
    {
        // Initialize player info with default difficulty 3
        m_players[0] = { "Player 1", false, 3, 0 };
        m_players[1] = { "Player 2", false, 3, 0 };
        
        // Initialize AI players with difficulty 3
        m_aiWhite = std::make_unique<AIPlayer>(3);
        m_aiBlack = std::make_unique<AIPlayer>(3);
        
        // Start with Human vs Computer mode by default
        NewGame(GameMode::HumanVsComputer);
    }

    // Start a new game with specified mode
    // humanPlaysWhite parameter determines who plays white in Human vs Computer mode
    void ChessGame::NewGame(GameMode mode, bool humanPlaysWhite)
    {
        m_gameMode = mode;
        m_board.ResetToStartingPosition();
        m_selectedSquare = -1;
        m_moveHistory.clear();
        m_boardHistory.clear();
        m_boardHistory.push_back(m_board);
        m_currentHistoryIndex = 0;
        
        // Configure player AI status based on game mode
        if (mode == GameMode::HumanVsHuman)
        {
            m_players[0].isAI = false;
            m_players[1].isAI = false;
        }
        else if (mode == GameMode::HumanVsComputer)
        {
            // Support human playing either color
            // If humanPlaysWhite = true  -> White=Human(false), Black=AI(true)
            // If humanPlaysWhite = false -> White=AI(true), Black=Human(false)
            
            m_players[0].isAI = !humanPlaysWhite;  // White (Player 0)
            m_players[1].isAI = humanPlaysWhite;   // Black (Player 1)
            
            // Set default difficulty for AI player (whichever one is AI)
            int aiIndex = m_players[0].isAI ? 0 : 1;
            if (m_players[aiIndex].aiDifficulty == 5)
            {
                m_players[aiIndex].aiDifficulty = 3;
            }
        }
        else if (mode == GameMode::ComputerVsComputer)
        {
            m_players[0].isAI = true;
            m_players[1].isAI = true;
        }
        
        // Ensure AI players are initialized with correct difficulty
        if (m_players[0].isAI)
        {
            if (!m_aiWhite)
            {
                m_aiWhite = std::make_unique<AIPlayer>(m_players[0].aiDifficulty);
            }
            else
            {
                m_aiWhite->SetDifficulty(m_players[0].aiDifficulty);
            }
        }
        
        if (m_players[1].isAI)
        {
            if (!m_aiBlack)
            {
                m_aiBlack = std::make_unique<AIPlayer>(m_players[1].aiDifficulty);
            }
            else
            {
                m_aiBlack->SetDifficulty(m_players[1].aiDifficulty);
            }
        }
        
        UpdateLegalMoves();
        UpdateHighlightedSquares();
        UpdateGameRecord();
    }

    // Load game from FEN string
    void ChessGame::LoadGame(const std::string& fen)
    {
        if (m_board.LoadFEN(fen))
        {
            m_selectedSquare = -1;
            m_moveHistory.clear();
            m_boardHistory.clear();
            m_boardHistory.push_back(m_board);
            m_currentHistoryIndex = 0;
            
            UpdateLegalMoves();
            UpdateHighlightedSquares();
            UpdateGameRecord();
        }
    }

    // Save current game state to file
    void ChessGame::SaveGame(const std::string& filename) const
    {
        std::ofstream file(filename);
        if (file.is_open())
        {
            // Save current position as FEN
            file << m_board.GetFEN() << "\n";
            
            // Save move history
            file << m_moveHistory.size() << "\n";
            for (const auto& move : m_moveHistory)
            {
                file << move.GetFrom() << " " << move.GetTo() << " "
                     << static_cast<int>(move.GetPromotion()) << "\n";
            }
            
            file.close();
        }
    }

    // Load game from PGN file
    void ChessGame::LoadPGN(const std::string& filename)
    {
        std::ifstream file(filename);
        if (!file.is_open()) return;
        
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string pgn = buffer.str();
        
        m_gameRecord = PGNParser::ParsePGN(pgn);
        
        // Replay the game from PGN moves
        NewGame(GameMode::HumanVsHuman);
        for (const auto& [whiteMove, blackMove] : m_gameRecord.moves)
        {
            // Implementation would require SAN to Move conversion
        }
    }

    // Save game to PGN file
    void ChessGame::SavePGN(const std::string& filename) const
    {
        std::ofstream file(filename);
        if (file.is_open())
        {
            std::string pgn = PGNParser::GeneratePGN(m_gameRecord);
            file << pgn;
            file.close();
        }
    }

    // Make move using square indices and optional promotion
    bool ChessGame::MakeMove(int from, int to, PieceType promotion)
    {
        // Find matching legal move
        for (const auto& move : m_legalMoves)
        {
            if (move.GetFrom() == from && move.GetTo() == to && 
                move.GetPromotion() == promotion)
            {
                return MakeMove(move);
            }
        }
        return false;
    }

    // Make move using Move object
    bool ChessGame::MakeMove(const Move& move)
    {
        // Verify move is in legal moves list
        if (std::find(m_legalMoves.begin(), m_legalMoves.end(), move) == m_legalMoves.end())
        {
            return false; // Move not legal
        }
        
        // Execute move on board
        if (!m_board.MakeMove(move))
        {
            return false;
        }
        
        // Add to history
        AddMoveToHistory(move);
        
        // Update game state
        m_selectedSquare = -1;
        UpdateLegalMoves();
        UpdateHighlightedSquares();
        UpdateGameRecord();
        
        return true;
    }

    // Undo last move
    bool ChessGame::UndoMove()
    {
        if (m_currentHistoryIndex == 0)
        {
            return false; // No moves to undo - at game start
        }

        // Check undo depth limit - count from current position backwards
        size_t movesFromStart = m_currentHistoryIndex;
        size_t movesFromLatest = m_boardHistory.size() - 1 - m_currentHistoryIndex;
        
        // Can only undo if we haven't already undone max depth moves
        if (movesFromLatest >= static_cast<size_t>(m_maxUndoDepth))
        {
            return false; // Already undone max depth moves
        }

        m_currentHistoryIndex--;
        m_board = m_boardHistory[m_currentHistoryIndex];

        UpdateLegalMoves();
        UpdateHighlightedSquares();

        return true;
    }

    // Redo previously undone move
    bool ChessGame::RedoMove()
    {
        if (m_currentHistoryIndex >= m_boardHistory.size() - 1)
        {
            return false; // No moves to redo
        }
        
        m_currentHistoryIndex++;
        m_board = m_boardHistory[m_currentHistoryIndex];
        
        UpdateLegalMoves();
        UpdateHighlightedSquares();
        
        return true;
    }

    // Set AI difficulty for specified color
    void ChessGame::SetAIDifficulty(PlayerColor color, DifficultyLevel difficulty)
    {
        int index = static_cast<int>(color);
        m_players[index].aiDifficulty = difficulty;
        
        // Update or create AI player with new difficulty
        if (color == PlayerColor::White)
        {
            if (!m_aiWhite)
            {
                m_aiWhite = std::make_unique<AIPlayer>(difficulty);
            }
            else
            {
                m_aiWhite->SetDifficulty(difficulty);
            }
        }
        else
        {
            if (!m_aiBlack)
            {
                m_aiBlack = std::make_unique<AIPlayer>(difficulty);
            }
            else
            {
                m_aiBlack->SetDifficulty(difficulty);
            }
        }
    }

    // Calculate and execute AI move
    void ChessGame::MakeAIMove()
    {
        if (!IsAITurn()) return;
        
        auto& aiPlayer = GetCurrentAIPlayer();
        Move bestMove = aiPlayer->CalculateBestMove(m_board);
        
        if (bestMove.IsValid()) 
        {
            MakeMove(bestMove);
        }
    }

    // Generate move history as formatted text
    std::string ChessGame::GetMoveHistoryText() const
    {
        std::stringstream ss;
        int moveNumber = 1;
        
        // Display moves in pairs (white, black)
        for (size_t i = 0; i < m_moveHistory.size(); i += 2)
        {
            ss << moveNumber << ". ";
            ss << m_moveHistory[i].ToAlgebraic();
            
            if (i + 1 < m_moveHistory.size())
            {
                ss << " " << m_moveHistory[i + 1].ToAlgebraic();
            }
            
            ss << "\n";
            moveNumber++;
        }
        
        return ss.str();
    }

    // Handle square selection for move input
    void ChessGame::SelectSquare(int square)
    {
        if (square < 0 || square >= SQUARE_COUNT) return;
        
        Piece piece = m_board.GetPieceAt(square);
        
        // Select piece if it belongs to current player
        if (piece && piece.GetColor() == m_board.GetCurrentPlayer())
        {
            m_selectedSquare = square;
        }
        // Try to make move if square was already selected
        else if (m_selectedSquare != -1)
        {
            MakeMove(m_selectedSquare, square);
            m_selectedSquare = -1;
        }
        
        UpdateHighlightedSquares();
    }

    // Clear current square selection
    void ChessGame::ClearSelection()
    {
        m_selectedSquare = -1;
        UpdateHighlightedSquares();
    }

    // Get all valid destination squares from a given square
    std::vector<int> ChessGame::GetValidTargetSquares(int fromSquare) const
    {
        std::vector<int> targets;
        
        for (const auto& move : m_legalMoves)
        {
            if (move.GetFrom() == fromSquare)
            {
                targets.push_back(move.GetTo());
            }
        }
        
        return targets;
    }

    // Set player name for specified color
    void ChessGame::SetPlayerName(PlayerColor color, const std::string& name)
    {
        int index = static_cast<int>(color);
        m_players[index].name = name;
        UpdateGameRecord();
    }

    // Configure time control settings
    void ChessGame::SetTimeControl(int minutes, int incrementSeconds)
    {
        m_timeControl.baseTimeMs = minutes * 60 * 1000;
        m_timeControl.incrementMs = incrementSeconds * 1000;
    }

    // Set maximum undo depth (1-3 moves)
    void ChessGame::SetMaxUndoDepth(int depth)
    {
        if (depth < 1) depth = 1;
        if (depth > 3) depth = 3;
        m_maxUndoDepth = depth;
    }

    // Update internal legal moves cache
    void ChessGame::UpdateLegalMoves()
    {
        m_legalMoves = m_board.GenerateLegalMoves();
    }

    // Update squares to highlight based on selection
    void ChessGame::UpdateHighlightedSquares()
    {
        m_highlightedSquares.clear();
        
        if (m_selectedSquare != -1)
        {
            // Highlight all possible destination squares for selected piece
            for (const auto& move : m_legalMoves)
            {
                if (move.GetFrom() == m_selectedSquare)
                {
                    m_highlightedSquares.push_back(move.GetTo());
                }
            }
        }
    }

    // Add move to history and save board state
    void ChessGame::AddMoveToHistory(const Move& move)
    {
        // If we make a new move after undoing, remove the unnecessary branch of history
        if (m_currentHistoryIndex < m_boardHistory.size() - 1)
        {
            m_moveHistory.erase(m_moveHistory.begin() + m_currentHistoryIndex, m_moveHistory.end());
            m_boardHistory.erase(m_boardHistory.begin() + m_currentHistoryIndex + 1, m_boardHistory.end());
        }
        
        m_moveHistory.push_back(move);
        m_boardHistory.push_back(m_board);
        m_currentHistoryIndex = m_boardHistory.size() - 1;
    }

    // Update PGN game record with current game state
    void ChessGame::UpdateGameRecord()
    {
        m_gameRecord.date = GetCurrentDateTime();
        m_gameRecord.white = m_players[0].name;
        m_gameRecord.black = m_players[1].name;
        m_gameRecord.finalFEN = m_board.GetFEN();
        
        // Determine game result based on current state
        GameState state = m_board.GetGameState();
        switch (state)
        {
        case GameState::Checkmate:
            if (m_board.GetCurrentPlayer() == PlayerColor::White)
                m_gameRecord.result = "0-1"; // White was checkmated
            else
                m_gameRecord.result = "1-0"; // Black was checkmated
            break;
        case GameState::Stalemate:
        case GameState::Draw:
            m_gameRecord.result = "1/2-1/2";
            break;
        default:
            m_gameRecord.result = "*"; // Game in progress
            break;
        }
    }

    // Check if it's AI's turn to move
    bool ChessGame::IsAITurn() const
    {
        if (m_gameMode == GameMode::HumanVsHuman) return false;
        
        PlayerColor currentPlayer = m_board.GetCurrentPlayer();
        int index = static_cast<int>(currentPlayer);
        
        return m_players[index].isAI;
    }

    // Get AI player for current side to move
    std::unique_ptr<AIPlayer>& ChessGame::GetCurrentAIPlayer()
    {
        PlayerColor currentPlayer = m_board.GetCurrentPlayer();
        
        // Ensure AI player exists before returning reference
        if (currentPlayer == PlayerColor::White)
        {
            if (!m_aiWhite)
            {
                m_aiWhite = std::make_unique<AIPlayer>(m_players[0].aiDifficulty);
            }
            return m_aiWhite;
        }
        else
        {
            if (!m_aiBlack)
            {
                m_aiBlack = std::make_unique<AIPlayer>(m_players[1].aiDifficulty);
            }
            return m_aiBlack;
        }
    }

    // Check if pawn promotion is required for this move
    bool ChessGame::IsPromotionRequired(int from, int to) const
    {
        Piece piece = m_board.GetPieceAt(from);
        if (!piece.IsType(PieceType::Pawn)) return false;
        
        int rank = to / 8;
        return (piece.GetColor() == PlayerColor::White && rank == 7) ||
               (piece.GetColor() == PlayerColor::Black && rank == 0);
    }

    // Start timer for specified player (placeholder)
    void ChessGame::StartPlayerTimer(PlayerColor)
    {
        // TODO: Implement timer logic using timer thread or system timers
    }

    // Stop timer for specified player (placeholder)
    void ChessGame::StopPlayerTimer(PlayerColor)
    {
        // TODO: Implement timer logic
    }

    // ---------- Utility Functions ----------

    // Get current date/time formatted for PGN
    std::string GetCurrentDateTime()
    {
        auto now = std::chrono::system_clock::now();
        auto in_time_t = std::chrono::system_clock::to_time_t(now);
        
        std::stringstream ss;
        std::tm timeinfo;
        
        #ifdef _WIN32
            localtime_s(&timeinfo, &in_time_t);
        #else
            localtime_r(&in_time_t, &timeinfo);
        #endif
        
        ss << std::put_time(&timeinfo, "%Y.%m.%d");
        return ss.str();
    }

    // Convert player color to string
    std::string ColorToString(PlayerColor color)
    {
        return color == PlayerColor::White ? "White" : "Black";
    }

    // Convert game state to string
    std::string GameStateToString(GameState state)
    {
        switch (state)
        {
        case GameState::Playing: return "Playing";
        case GameState::Check: return "Check";
        case GameState::Checkmate: return "Checkmate";
        case GameState::Stalemate: return "Stalemate";
        case GameState::Draw: return "Draw";
        default: return "Unknown";
        }
    }

    // ---------- PGNParser Implementation ----------

    // Parse PGN string into game record (placeholder)
    GameRecord PGNParser::ParsePGN(const std::string& /*pgn*/)
    {
        return GameRecord();
    }

    // Generate PGN string from game record
    std::string PGNParser::GeneratePGN(const GameRecord& record)
    {
        std::stringstream ss;
        ss << "[Event \"" << record.event << "\"]\n";
        ss << "[Site \"" << record.site << "\"]\n";
        ss << "[Date \"" << record.date << "\"]\n";
        ss << "[White \"" << record.white << "\"]\n";
        ss << "[Black \"" << record.black << "\"]\n";
        ss << "[Result \"" << record.result << "\"]\n\n";
        return ss.str();
    }

    // Parse PGN header line (placeholder)
    bool PGNParser::ParseHeader(const std::string&, GameRecord&)
    {
        return false;
    }

    // Parse move in Standard Algebraic Notation (placeholder)
    bool PGNParser::ParseMove(const std::string&, Board&, std::string&)
    {
        return false;
    }

    // Convert Move to Standard Algebraic Notation (placeholder)
    std::string PGNParser::MoveToSAN(const Move&, const Board&)
    {
        return "";
    }

    // Convert Standard Algebraic Notation to Move (placeholder)
    Move PGNParser::SANToMove(const std::string&, const Board&)
    {
        return Move();
    }
}