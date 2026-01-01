// UCIEngine.h
// Universal Chess Interface (UCI) protocol implementation
//
// UCI is the standard communication protocol between chess GUIs and engines.
// Popular GUIs like Arena, Cutechess, and Fritz all use UCI to communicate.
//
// The protocol is text-based and line-oriented:
// - GUI sends commands like "uci", "position", "go", "stop"
// - Engine responds with "id", "uciok", "bestmove", etc.
//
// This implementation wraps our AIPlayer class and exposes it as a UCI engine.
// It runs as a separate console executable (ChessEngineUCI.exe) that reads
// commands from stdin and writes responses to stdout.
//
// Key UCI commands we handle:
// - uci          : Engine identification and option list
// - isready      : Synchronization (engine must respond "readyok")
// - ucinewgame   : Reset for new game
// - position     : Set up board position (startpos or FEN + moves)
// - go           : Start calculating (with time controls or depth limits)
// - stop         : Abort current search and output best move
// - setoption    : Configure engine parameters
// - quit         : Exit the engine process

#pragma once

#define NOMINMAX  // Prevent Windows.h from defining min/max macros that conflict with std::min/std::max

#include "../Engine/Board.h"
#include "../Engine/Move.h"
#include "../Engine/Zobrist.h"
#include "../UI/ChessGame.h"

#include <string>
#include <sstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <mutex>
#include <optional>
#include <algorithm>

namespace Chess
{
    class UCIEngine
    {
    public:
        UCIEngine()
            : m_ai(5) // Default difficulty level 5
        {
            // Initialize board to standard starting position
            m_board.ResetToStartingPosition();
        }

        ~UCIEngine()
        {
            // Ensure search thread is properly terminated before destruction
            StopSearchAndJoin();
        }

        // Main loop - reads UCI commands from stdin until "quit"
        void Run()
        {
            std::string line;
            while (std::getline(std::cin, line))
            {
                // Handle Windows line endings (pipes may include \r)
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();

                ProcessCommand(line);

                if (line == "quit")
                    break;
            }
        }

    private:
        // Current board position
        // Protected by mutex because GUI can send commands while search is running
        Board m_board;
        std::mutex m_boardMutex;

        // Our chess engine wrapped in AIPlayer
        AIPlayer m_ai;

        // Search control
        std::atomic<bool> m_stopRequested{false};
        std::thread m_searchThread;

        // Engine configuration
        int m_level = 5;

        // ---------- Command Dispatcher ----------
        // Routes incoming UCI commands to appropriate handlers
        void ProcessCommand(const std::string& line)
        {
            std::istringstream iss(line);
            std::string cmd;
            iss >> cmd;

            if (cmd == "uci")
            {
                HandleUCI();
            }
            else if (cmd == "isready")
            {
                // Synchronization command - GUI waits for this before proceeding
                std::cout << "readyok\n" << std::flush;
            }
            else if (cmd == "ucinewgame")
            {
                // New game starting - reset everything
                StopSearchAndJoin();
                std::lock_guard<std::mutex> lock(m_boardMutex);
                m_board.ResetToStartingPosition();
            }
            else if (cmd == "position")
            {
                // Position updates should stop any ongoing search
                StopSearchAndJoin();
                HandlePosition(iss);
            }
            else if (cmd == "go")
            {
                StopSearchAndJoin();
                HandleGo(iss);
            }
            else if (cmd == "stop")
            {
                StopSearchAndJoin();
            }
            else if (cmd == "setoption")
            {
                HandleSetOption(iss);
            }
            else if (cmd == "quit")
            {
                StopSearchAndJoin();
            }
            // Ignored commands: "ponderhit", "debug", "register", etc.
            // These are either not applicable or not implemented
        }

        // ---------- UCI Command: uci ----------
        // Engine must identify itself and list available options
        void HandleUCI()
        {
            // Engine identification
            std::cout << "id name Modern Chess 1.0\n";
            std::cout << "id author Marek Wesolowski\n";

            // Supported UCI options
            // These allow GUIs to configure the engine through their interface
            std::cout << "option name Threads type spin default 4 min 1 max 64\n";
            std::cout << "option name Hash type spin default 64 min 1 max 1024\n";
            std::cout << "option name Level type spin default 5 min 1 max 10\n";

            // Standard options that GUIs expect (even if we don't fully implement them)
            std::cout << "option name Ponder type check default false\n";
            std::cout << "option name UCI_AnalyseMode type check default false\n";

            // Signal that UCI initialization is complete
            std::cout << "uciok\n" << std::flush;
        }

        // ---------- UCI Command: position ----------
        // Format: position [startpos | fen <fenstring>] [moves <move1> <move2> ...]
        void HandlePosition(std::istringstream& iss)
        {
            std::string token;
            iss >> token;

            Board newBoard;

            if (token == "startpos")
            {
                // Standard starting position
                newBoard.ResetToStartingPosition();
                iss >> token; // Might be "moves" or end of command
            }
            else if (token == "fen")
            {
                // Custom position specified in FEN notation
                // FEN has 6 space-separated fields
                std::string fen;
                for (int i = 0; i < 6; ++i)
                {
                    std::string part;
                    if (!(iss >> part))
                        break;

                    if (!fen.empty())
                        fen += ' ';
                    fen += part;
                }

                if (!newBoard.LoadFEN(fen))
                {
                    // Invalid FEN - fall back to starting position
                    // This is safer than leaving the board in undefined state
                    newBoard.ResetToStartingPosition();
                }

                iss >> token; // Might be "moves" or end of command
            }
            else
            {
                // Unrecognized format - default to starting position
                newBoard.ResetToStartingPosition();
            }

            // Apply move sequence if present
            if (token == "moves")
            {
                std::string moveStr;
                while (iss >> moveStr)
                {
                    auto moveOpt = Move::FromUCI(moveStr, newBoard);
                    if (!moveOpt.has_value())
                    {
                        // Invalid move - skip it rather than crash
                        // This can happen with corrupted input or unsupported variants
                        continue;
                    }
                    newBoard.MakeMoveUnchecked(moveOpt.value());
                }
            }

            // Commit the new position
            std::lock_guard<std::mutex> lock(m_boardMutex);
            m_board = newBoard;
        }

        // ---------- UCI Command: go ----------
        // Start searching with specified time/depth constraints
        // Format: go [wtime <ms>] [btime <ms>] [winc <ms>] [binc <ms>] 
        //            [movetime <ms>] [depth <n>] [infinite] [nodes <n>] ...
        void HandleGo(std::istringstream& iss)
        {
            // Clear any previous stop request
            m_stopRequested.store(false, std::memory_order_release);

            // Parse time control parameters
            int wtime = 0, btime = 0, winc = 0, binc = 0;
            int movetime = 0;
            int depth = 0;
            bool infinite = false;

            std::string token;
            while (iss >> token)
            {
                if (token == "wtime") iss >> wtime;
                else if (token == "btime") iss >> btime;
                else if (token == "winc") iss >> winc;
                else if (token == "binc") iss >> binc;
                else if (token == "movetime") iss >> movetime;
                else if (token == "depth") iss >> depth;
                else if (token == "infinite") infinite = true;
                // Ignored: "nodes", "mate", "movestogo", "searchmoves", "ponder"
            }

            // Calculate time budget for this move
            int timeMs = CalculateSearchTime(movetime, depth, infinite, 
                                              wtime, btime, winc, binc);

            // Take a snapshot of current position for the search thread
            Board boardCopy;
            {
                std::lock_guard<std::mutex> lock(m_boardMutex);
                boardCopy = m_board;
            }

            // Launch search in background thread
            // This allows GUI to send "stop" command while we're thinking
			m_searchThread = std::thread([this, boardCopy, timeMs]() mutable
			{
				m_ai.SetDifficulty(m_level);

				// Check for mate/stalemate - no legal moves available
				// UCI protocol requires "0000" notation for null move
				// Arena and other GUIs expect this in game-ending positions
				auto legalMoves = boardCopy.GenerateLegalMoves();
				if (legalMoves.empty())
				{
					std::cout << "bestmove 0000\n" << std::flush;
					return;
				}

				Move bestMove = m_ai.CalculateBestMove(boardCopy, timeMs);

				// UCI protocol requires bestmove output even after "stop" command
				// GUIs like Arena wait for bestmove to confirm search completion
				std::cout << "bestmove " << bestMove.ToUCI() << "\n" << std::flush;
			});
        }

        // Calculate how much time to spend on this move
        int CalculateSearchTime(int movetime, int depth, bool infinite,
                                int wtime, int btime, int winc, int binc)
        {
            // Priority 1: Explicit movetime
            if (movetime > 0)
                return movetime;

            // Priority 2: Infinite analysis mode
            if (infinite)
                return 60 * 60 * 1000; // 1 hour (effectively infinite)

            // Priority 3: Depth limit without time (rough conversion)
            if (depth > 0)
                return std::clamp(depth * 300, 100, 10000);

            // Priority 4: Clock-based time management
            Board snapshot;
            {
                std::lock_guard<std::mutex> lock(m_boardMutex);
                snapshot = m_board;
            }

            int myTime = (snapshot.GetSideToMove() == PlayerColor::White) ? wtime : btime;
            int myInc = (snapshot.GetSideToMove() == PlayerColor::White) ? winc : binc;

            if (myTime > 0)
            {
                // Simple time management: time/40 + 75% of increment
                // This assumes roughly 40 moves remaining in the game
                int budgetMs = myTime / 40 + (myInc * 3) / 4;

                // Don't spend more than half our remaining time on one move
                // and ensure we have at least 50ms to make a move
                return std::max(50, std::min(budgetMs, myTime / 2));
            }

            // Fallback: 3 seconds (reasonable default for analysis)
            return 3000;
        }

        // ---------- UCI Command: setoption ----------
        // Format: setoption name <id> [value <x>]
        void HandleSetOption(std::istringstream& iss)
        {
            std::string token;
            iss >> token;
            if (token != "name")
                return;

            // Parse option name (may contain spaces)
            std::string name;
            while (iss >> token && token != "value")
            {
                if (!name.empty()) name += ' ';
                name += token;
            }

            // Parse option value (rest of line)
            std::string valueStr;
            if (token == "value")
            {
                std::getline(iss, valueStr);
                // Remove leading space
                if (!valueStr.empty() && valueStr.front() == ' ')
                    valueStr.erase(valueStr.begin());
            }

            // Apply the option
            if (name == "Threads")
            {
                try
                {
                    int threads = std::stoi(valueStr);
                    m_ai.SetThreads(threads);
                }
                catch (...) { /* Invalid value - ignore */ }
            }
            else if (name == "Hash")
            {
                try
                {
                    int mb = std::stoi(valueStr);
                    // Hash size change requires AIPlayer::SetHashSize()
                    // which will be implemented separately
                    (void)mb;
                }
                catch (...) { /* Invalid value - ignore */ }
            }
            else if (name == "Level")
            {
                try
                {
                    int level = std::stoi(valueStr);
                    m_level = std::clamp(level, 1, 10);
                }
                catch (...) { /* Invalid value - ignore */ }
            }
            // Unknown options are silently ignored for compatibility
        }

        // ---------- Search Control ----------
        // Stop any running search and wait for thread to finish
        void StopSearchAndJoin()
        {
            // Signal that we want to stop
            m_stopRequested.store(true, std::memory_order_release);

            // Tell the AI to abort its search loop
            m_ai.AbortSearch();

            // Wait for search thread to finish
            if (m_searchThread.joinable())
            {
                m_searchThread.join();
            }
        }
    };
}
