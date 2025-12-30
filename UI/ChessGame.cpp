// ChessGame.cpp
#define NOMINMAX
#include "ChessGame.h"
#include "../Engine/Evaluation.h"
#include "../Engine/OpeningBook.h"
#include <algorithm>
#include <random>
#include <chrono>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <future>
#include <limits>

namespace Chess
{
    // ---------- AIPlayer Implementation ----------
    
    // Constructor - initialize AI with difficulty level and transposition table
    AIPlayer::AIPlayer(DifficultyLevel difficulty)
        : m_difficulty(difficulty)
        , m_maxSearchTimeMs(5000)
    {
        // Clamp difficulty to valid range
        if (m_difficulty < Difficulty::MIN) m_difficulty = Difficulty::MIN;
        if (m_difficulty > Difficulty::MAX) m_difficulty = Difficulty::MAX;

        // Allocate transposition table based on difficulty
        if (m_difficulty <= 6)
        {
            m_transpositionTable.Resize(16); // 16 MB for lower levels
        }
        else
        {
            m_transpositionTable.Resize(64); // 64 MB for higher levels
        }

        // Initialize killer move heuristic storage
        for (int i = 0; i < MAX_PLY; ++i)
        {
            m_killerMoves[i][0] = Move();
            m_killerMoves[i][1] = Move();
        }

        // Initialize history heuristic tables (separate per side)
        for (int side = 0; side < 2; ++side)
        {
            for (int from = 0; from < 64; ++from)
            {
                for (int to = 0; to < 64; ++to)
                {
                    m_history[side][from][to] = 0;
                }
            }
        }
    }

    // Update AI difficulty and resize transposition table accordingly
    void AIPlayer::SetDifficulty(DifficultyLevel difficulty)
    {
        m_difficulty = difficulty;
        if (m_difficulty < Difficulty::MIN) m_difficulty = Difficulty::MIN;
        if (m_difficulty > Difficulty::MAX) m_difficulty = Difficulty::MAX;

        // Adjust table size based on new difficulty
        if (m_difficulty <= 6)
        {
            m_transpositionTable.Resize(16);
        }
        else
        {
            m_transpositionTable.Resize(64);
        }

        // Configure thread count based on difficulty
        if (m_difficulty >= 6) {
            m_numThreads = std::thread::hardware_concurrency();
            if (m_numThreads < 1) m_numThreads = 4; // Fallback
        } else {
            m_numThreads = 1;
        }
    }

    void AIPlayer::SetThreads(int threads) {
        if (threads >= 1 && threads <= 64) {
            m_numThreads = threads;
        }
    }

    // Assign score to move for move ordering optimization
    int AIPlayer::ScoreMove(const Move& move, const Board& board, Move ttMove, int ply)
    {
        // Highest priority: Principal Variation move from transposition table
        if (move == ttMove)
        {
            return 10000000;
        }

        // High priority: Captures using MVV-LVA (Most Valuable Victim - Least Valuable Aggressor)
        if (move.IsCapture())
        {
            Piece victim = move.GetCaptured();
            Piece aggressor = board.GetPieceAt(move.GetFrom());

            int victimValue = PIECE_VALUES[static_cast<int>(victim.GetType())];
            int aggressorValue = PIECE_VALUES[static_cast<int>(aggressor.GetType())];

            return 1000000 + victimValue * 10 - aggressorValue;
        }

        // Good priority: Promotions
        if (move.IsPromotion())
        {
            return 900000;
        }

        // Killer move heuristic (non-capture moves that caused beta cutoffs)
        if (move == m_killerMoves[ply][0]) return 800000;
        if (move == m_killerMoves[ply][1]) return 700000;

        // History heuristic: use table for the side that is making the move
        const Piece movingPiece = board.GetPieceAt(move.GetFrom());
        const int sideIndex = static_cast<int>(movingPiece.GetColor());
        return m_history[sideIndex][move.GetFrom()][move.GetTo()];
    }

    // Sort moves by score for better alpha-beta pruning efficiency
    void AIPlayer::OrderMoves(std::vector<Move>& moves, const Board& board, Move ttMove, int ply)
    {
        std::vector<std::pair<int, Move>> scoredMoves;

        // Score all moves
        for (const auto& move : moves)
        {
            int score = ScoreMove(move, board, ttMove, ply);
            scoredMoves.push_back({score, move});
        }

        // Sort moves by score (highest first)
        std::sort(scoredMoves.begin(), scoredMoves.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        // Extract sorted moves
        moves.clear();
        for (const auto& [score, move] : scoredMoves)
        {
            moves.push_back(move);
        }
    }

    // Check if search time limit has been reached
    bool AIPlayer::ShouldStop() const
    {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_searchStartTime
        ).count();
        return elapsed >= m_maxSearchTimeMs;
    }

    // Main search function - find best move using iterative deepening with root-parallel search
    Move AIPlayer::CalculateBestMove(const Board& board, int maxTimeMs)
    {
        m_abortSearch.store(false, std::memory_order_release);
        m_searchStartTime = std::chrono::steady_clock::now();
        m_maxSearchTimeMs = maxTimeMs;

        // Set search time based on difficulty level
        if (m_difficulty <= 2)
            m_maxSearchTimeMs = 100;
        else if (m_difficulty <= 4)
            m_maxSearchTimeMs = 1000;
        else if (m_difficulty <= 6)
            m_maxSearchTimeMs = 3000;
        else if (m_difficulty <= 8)
            m_maxSearchTimeMs = 5000;
        else
            m_maxSearchTimeMs = 10000;

        Board searchBoard = board;
        auto legalMoves = searchBoard.GenerateLegalMoves();
        if (legalMoves.empty())
            return Move();

        // Try opening book first for higher difficulties
        if (m_difficulty >= 3)
        {
            int piecesOnBoard = 0;
            for (int sq = 0; sq < 64; ++sq)
            {
                if (!board.GetPieceAt(sq).IsEmpty())
                    piecesOnBoard++;
            }
            int estimatedPly = (32 - piecesOnBoard) * 2;
            auto bookMove = ProbeBook(board, estimatedPly);
            if (bookMove.has_value())
                return bookMove.value();
        }

        const int MAX_DEPTH = 30;

        // Level 1: Pure random move selection
        if (m_difficulty == 1)
        {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<> dis(0, static_cast<int>(legalMoves.size()) - 1);
            return legalMoves[dis(gen)];
        }

        // Level 2: Random from top moves
        if (m_difficulty == 2)
        {
            std::random_device rd;
            std::mt19937 gen(rd());
            OrderMoves(legalMoves, searchBoard, Move(), 0);
            std::uniform_int_distribution<> dis(0, std::min(5, static_cast<int>(legalMoves.size()) - 1));
            return legalMoves[dis(gen)];
        }

        // Clear TT and heuristics - safe since no search is running yet
        m_transpositionTable.Clear();

        for (int i = 0; i < MAX_PLY; ++i)
        {
            m_killerMoves[i][0] = Move();
            m_killerMoves[i][1] = Move();
        }
        for (int side = 0; side < 2; ++side)
        {
            for (int from = 0; from < 64; ++from)
            {
                for (int to = 0; to < 64; ++to)
                {
                    m_history[side][from][to] = 0;
                }
            }
        }

        Move bestMoveSoFar = legalMoves[0];
        int bestScore = -INFINITY_SCORE;

        // Iterative deepening with root-parallel search
        for (int depth = 1; depth <= MAX_DEPTH; ++depth)
        {
            if (ShouldStop()) break;

            // Step 1: Search PV move in main thread first
            OrderMoves(legalMoves, searchBoard, bestMoveSoFar, 0);

            searchBoard.MakeMoveUnchecked(legalMoves[0]);
            int pvScore = -AlphaBeta(searchBoard, depth - 1, -INFINITY_SCORE, INFINITY_SCORE, 1);
            searchBoard.UndoMove();

            if (ShouldStop()) break;

            bestMoveSoFar = legalMoves[0];
            bestScore = pvScore;

			// Step 2: Root-parallel search for remaining moves with dynamic load balancing
            if (m_numThreads > 1 && legalMoves.size() > 1 && depth >= 4)
            {
                int actualThreads = std::min(m_numThreads, static_cast<int>(legalMoves.size() - 1));
                std::vector<std::future<std::pair<Move, int>>> futures;

                // Stable copy for all worker threads to prevent data races
                const Board rootBoard = searchBoard;
                const int currentAlpha = bestScore;
                const size_t totalMoves = legalMoves.size();

                // Shared atomic counter for dynamic work distribution
                auto nextMoveIndex = std::make_shared<std::atomic<size_t>>(1);

                for (int t = 0; t < actualThreads; ++t)
                {
                    futures.push_back(std::async(std::launch::async,
                        [=, &legalMoves]() mutable -> std::pair<Move, int> {
                            // Thread-local heuristics to avoid data races
                            ThreadLocalData tld;
                            Board localBoard = rootBoard;
                            Move localBest;
                            int localScore = -INFINITY_SCORE;

                            // Each thread grabs next available move dynamically
                            while (true)
                            {
                                size_t i = nextMoveIndex->fetch_add(1);
                                if (i >= totalMoves) break;
                                if (m_abortSearch.load(std::memory_order_acquire)) break;

                                localBoard.MakeMoveUnchecked(legalMoves[i]);

                                // PV-search: null-window first, re-search if it beats alpha
                                int score = -WorkerAlphaBeta(localBoard, depth - 1,
                                                             -currentAlpha - 1, -currentAlpha, 1, tld);
                                if (score > currentAlpha && !m_abortSearch.load(std::memory_order_acquire))
                                {
                                    score = -WorkerAlphaBeta(localBoard, depth - 1,
                                                             -INFINITY_SCORE, -currentAlpha, 1, tld);
                                }

                                localBoard.UndoMove();

                                if (score > localScore)
                                {
                                    localScore = score;
                                    localBest = legalMoves[i];
                                }
                            }
                            return std::make_pair(localBest, localScore);
                        }
                    ));
                }

				// Collect results from worker threads
                for (auto& f : futures)
                {
                    auto [move, score] = f.get();
                    if (m_abortSearch.load(std::memory_order_acquire)) continue;
                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestMoveSoFar = move;
                    }
                }
            }
            else
            {
                // Single-threaded fallback for remaining moves
                for (size_t i = 1; i < legalMoves.size(); ++i)
                {
                    if (ShouldStop()) break;

                    searchBoard.MakeMoveUnchecked(legalMoves[i]);

                    // PV-search with null window
                    int score = -AlphaBeta(searchBoard, depth - 1, -bestScore - 1, -bestScore, 1);
                    if (score > bestScore && !ShouldStop())
                    {
                        score = -AlphaBeta(searchBoard, depth - 1, -INFINITY_SCORE, -bestScore, 1);
                    }

                    searchBoard.UndoMove();

                    if (score > bestScore)
                    {
                        bestScore = score;
                        bestMoveSoFar = legalMoves[i];
                    }
                }
            }
        }

        // Wait for all threads to complete before returning
        m_abortSearch.store(true, std::memory_order_release);

        return bestMoveSoFar;
    }

    // Alpha-beta negamax search with transposition table
    int AIPlayer::AlphaBeta(Board& board, int depth, int alpha, int beta, int ply)
    {
        // Check time limit
        if (ShouldStop()) return 0;

        // Probe transposition table for cached result
        uint64_t zobristKey = board.GetZobristKey();
        Move ttMove;
        int ttScore;
        if (m_transpositionTable.Probe(zobristKey, depth, alpha, beta, ttScore, ttMove))
        {
            return ttScore;
        }

        // Terminal depth reached - switch to quiescence search
        if (depth == 0)
        {
            return QuiescenceSearch(board, alpha, beta, ply, 0);
        }

        // Generate legal moves for current position
        auto moves = board.GenerateLegalMoves();
        if (moves.empty())
        {
            // No legal moves - checkmate or stalemate
            if (board.IsInCheck(board.GetCurrentPlayer()))
            {
                return -MATE_SCORE + ply; // Checkmate (prefer shorter mates)
            }
            return 0; // Stalemate
        }

        // Null move pruning
        if (depth >= 3 && !board.IsInCheck(board.GetCurrentPlayer()))
        {
            const int R = 2;
            board.MakeNullMoveUnchecked();
            int score = -AlphaBeta(board, depth - 1 - R, -beta, -beta + 1, ply + 1);
            board.UndoNullMove();

            if (score >= beta)
            {
                return beta;
            }
        }

        const PlayerColor sideToMove = board.GetCurrentPlayer();
        const int sideIndex = static_cast<int>(sideToMove);

        OrderMoves(moves, board, ttMove, ply);

        Move bestMove;
        int bestScore = -INFINITY_SCORE;
        uint8_t flag = TT_ALPHA;

        bool sideInCheck = board.IsInCheck(board.GetCurrentPlayer());
        int moveIndex = 0;

        // Search all legal moves
        for (const auto& move : moves)
        {
            board.MakeMoveUnchecked(move);

            int score;

            // Check if move is quiet (not tactical)
            bool isQuiet = !move.IsCapture() &&
                           !move.IsPromotion() &&
                           !move.IsEnPassant() &&
                           !move.IsCastling();

            // Check if we should apply late move reduction
            bool applyLMR = depth >= 3 &&
                            moveIndex >= 4 &&
                            !sideInCheck &&
                            isQuiet;

            if (applyLMR)
            {
                // Search with reduced depth
                score = -AlphaBeta(board, depth - 2, -beta, -alpha, ply + 1);

                // If reduced search beat alpha, re-search at full depth
                if (score > alpha)
                {
                    score = -AlphaBeta(board, depth - 1, -beta, -alpha, ply + 1);
                }
            }
            else
            {
                // Normal full depth search
                score = -AlphaBeta(board, depth - 1, -beta, -alpha, ply + 1);
            }

            board.UndoMove();

            if (score > bestScore)
            {
                bestScore = score;
                bestMove = move;
            }

            // Beta cutoff - this move is too good, opponent won't allow it
            if (score >= beta)
            {
                // Update history and killer moves for quiet moves
                if (isQuiet)
                {
                    // Update history heuristic (side-specific table)
                    m_history[sideIndex][move.GetFrom()][move.GetTo()] += depth * depth;

                    // Store killer move
                    if (ply < MAX_PLY)
                    {
                        m_killerMoves[ply][1] = m_killerMoves[ply][0];
                        m_killerMoves[ply][0] = move;
                    }
                }
                m_transpositionTable.Store(zobristKey, depth, beta, TT_BETA, bestMove);
                return beta;
            }

            // Alpha improvement - new best move found
            if (score > alpha)
            {
                alpha = score;
                flag = TT_EXACT;
            }

            moveIndex++;
        }

        m_transpositionTable.Store(zobristKey, depth, bestScore, flag, bestMove);
        return bestScore;
    }

    // Quiescence search - search only tactical moves to avoid horizon effect
    int AIPlayer::QuiescenceSearch(Board& board, int alpha, int beta, int ply, int qDepth)
    {
        // Check time and depth limits
        if (ShouldStop() || qDepth >= 8)
        {
            return Evaluate(board);
        }

        const PlayerColor sideToMove = board.GetCurrentPlayer();

        // If in check, we must search all evasions (stand-pat is illegal in check)
        if (board.IsInCheck(sideToMove))
        {
            auto evasions = board.GenerateLegalMoves();
            if (evasions.empty())
            {
                return -MATE_SCORE + ply; // Checkmated
            }

            OrderMoves(evasions, board, Move(), ply);

            int best = -INFINITY_SCORE;

            for (const auto& move : evasions)
            {
                board.MakeMoveUnchecked(move);

                int score = -QuiescenceSearch(board, -beta, -alpha, ply + 1, qDepth + 1);

                board.UndoMove();

                if (score > best) best = score;

                if (score >= beta)
                {
                    return beta; // Beta cutoff
                }
                if (score > alpha)
                {
                    alpha = score; // Alpha improvement
                }
            }

            return alpha;
        }

        // Not in check: normal stand-pat logic
        int standPat = Evaluate(board);

        if (standPat >= beta)
        {
            return beta;
        }

        if (standPat > alpha)
        {
            alpha = standPat;
        }

        // Generate only tactical moves (captures/promotions)
        auto moves = board.GenerateLegalMoves();
        std::vector<Move> tacticalMoves;
        tacticalMoves.reserve(moves.size());

        for (const auto& move : moves)
        {
            if (move.IsCapture() || move.IsPromotion())
            {
                tacticalMoves.push_back(move);
            }
        }

        OrderMoves(tacticalMoves, board, Move(), ply);

        for (const auto& move : tacticalMoves)
        {
            board.MakeMoveUnchecked(move);

            int score = -QuiescenceSearch(board, -beta, -alpha, ply + 1, qDepth + 1);

            board.UndoMove();

            if (score >= beta)
            {
                return beta;
            }
            if (score > alpha)
            {
                alpha = score;
            }
        }

        return alpha;
    }

    // Worker thread alpha-beta search - uses thread-local heuristics to avoid data races
    int AIPlayer::WorkerAlphaBeta(Board& board, int depth, int alpha, int beta, int ply,
                                   ThreadLocalData& tld)
    {
        // Frequent time checking for responsiveness
        static thread_local int nodeCounter = 0;
        if ((++nodeCounter & 1023) == 0)
        {
            if (ShouldStop()) return 0;
        }

        uint64_t zobristKey = board.GetZobristKey();
        Move ttMove;
        int ttScore;
        if (m_transpositionTable.Probe(zobristKey, depth, alpha, beta, ttScore, ttMove))
        {
            return ttScore;
        }

        if (depth == 0)
        {
            return WorkerQuiescence(board, alpha, beta, ply, 0, tld);
        }

        auto moves = board.GenerateLegalMoves();
        if (moves.empty())
        {
            if (board.IsInCheck(board.GetCurrentPlayer()))
            {
                return -MATE_SCORE + ply;
            }
            return 0;
        }

        // Null move pruning for non-check positions
        if (depth >= 3 && !board.IsInCheck(board.GetCurrentPlayer()))
        {
            const int R = 2;
            board.MakeNullMoveUnchecked();
            int score = -WorkerAlphaBeta(board, depth - 1 - R, -beta, -beta + 1, ply + 1, tld);
            board.UndoNullMove();

            if (score >= beta)
            {
                return beta;
            }
        }

        const PlayerColor sideToMove = board.GetCurrentPlayer();
        const int sideIndex = static_cast<int>(sideToMove);

        OrderMovesWorker(moves, board, ttMove, ply, tld);

        Move bestMove;
        int bestScore = -INFINITY_SCORE;
        uint8_t flag = TT_ALPHA;

        bool sideInCheck = board.IsInCheck(board.GetCurrentPlayer());
        int moveIndex = 0;

        for (const auto& move : moves)
        {
            if ((moveIndex & 15) == 0 && ShouldStop()) break;

            board.MakeMoveUnchecked(move);

            int score;
            bool isQuiet = !move.IsCapture() && !move.IsPromotion() &&
                           !move.IsEnPassant() && !move.IsCastling();

            bool applyLMR = depth >= 3 && moveIndex >= 4 && !sideInCheck && isQuiet;

            if (applyLMR)
            {
                score = -WorkerAlphaBeta(board, depth - 2, -beta, -alpha, ply + 1, tld);
                if (score > alpha)
                {
                    score = -WorkerAlphaBeta(board, depth - 1, -beta, -alpha, ply + 1, tld);
                }
            }
            else
            {
                score = -WorkerAlphaBeta(board, depth - 1, -beta, -alpha, ply + 1, tld);
            }

            board.UndoMove();

            if (score > bestScore)
            {
                bestScore = score;
                bestMove = move;
            }

            if (score >= beta)
            {
                // Update thread-local heuristics only
                if (isQuiet)
                {
                    tld.history[sideIndex][move.GetFrom()][move.GetTo()] += depth * depth;

                    if (ply < MAX_PLY)
                    {
                        tld.killerMoves[ply][1] = tld.killerMoves[ply][0];
                        tld.killerMoves[ply][0] = move;
                    }
                }
                m_transpositionTable.Store(zobristKey, depth, beta, TT_BETA, bestMove);
                return beta;
            }

            if (score > alpha)
            {
                alpha = score;
                flag = TT_EXACT;
            }

            moveIndex++;
        }

        m_transpositionTable.Store(zobristKey, depth, bestScore, flag, bestMove);
        return bestScore;
    }

    // Worker thread quiescence search - handles only tactical moves
    int AIPlayer::WorkerQuiescence(Board& board, int alpha, int beta, int ply, int qDepth,
                                    ThreadLocalData& tld)
    {
        if (ShouldStop() || qDepth >= 8)
        {
            return Evaluate(board);
        }

        const PlayerColor sideToMove = board.GetCurrentPlayer();

        if (board.IsInCheck(sideToMove))
        {
            auto evasions = board.GenerateLegalMoves();
            if (evasions.empty())
            {
                return -MATE_SCORE + ply;
            }

            OrderMovesWorker(evasions, board, Move(), ply, tld);

            int best = -INFINITY_SCORE;

            for (const auto& move : evasions)
            {
                board.MakeMoveUnchecked(move);
                int score = -WorkerQuiescence(board, -beta, -alpha, ply + 1, qDepth + 1, tld);
                board.UndoMove();

                if (score > best) best = score;
                if (score >= beta) return beta;
                if (score > alpha) alpha = score;
            }

            return alpha;
        }

        int standPat = Evaluate(board);
        if (standPat >= beta) return beta;
        if (standPat > alpha) alpha = standPat;

        auto moves = board.GenerateLegalMoves();
        std::vector<Move> tacticalMoves;
        tacticalMoves.reserve(moves.size());

        for (const auto& move : moves)
        {
            if (move.IsCapture() || move.IsPromotion())
            {
                tacticalMoves.push_back(move);
            }
        }

        OrderMovesWorker(tacticalMoves, board, Move(), ply, tld);

        for (const auto& move : tacticalMoves)
        {
            board.MakeMoveUnchecked(move);
            int score = -WorkerQuiescence(board, -beta, -alpha, ply + 1, qDepth + 1, tld);
            board.UndoMove();

            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        }

        return alpha;
    }

    // Move ordering for worker threads using thread-local data
    void AIPlayer::OrderMovesWorker(std::vector<Move>& moves, const Board& board, Move ttMove,
                                     int ply, const ThreadLocalData& tld)
    {
        std::vector<std::pair<int, Move>> scoredMoves;

        for (const auto& move : moves)
        {
            int score = ScoreMoveWorker(move, board, ttMove, ply, tld);
            scoredMoves.push_back({score, move});
        }

        std::sort(scoredMoves.begin(), scoredMoves.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        moves.clear();
        for (const auto& [score, move] : scoredMoves)
        {
            moves.push_back(move);
        }
    }

    // Score move for worker threads using thread-local heuristics
    int AIPlayer::ScoreMoveWorker(const Move& move, const Board& board, Move ttMove, int ply,
                                   const ThreadLocalData& tld)
    {
        if (move == ttMove) return 10000000;

        if (move.IsCapture())
        {
            Piece victim = move.GetCaptured();
            Piece aggressor = board.GetPieceAt(move.GetFrom());
            int victimValue = PIECE_VALUES[static_cast<int>(victim.GetType())];
            int aggressorValue = PIECE_VALUES[static_cast<int>(aggressor.GetType())];
            return 1000000 + victimValue * 10 - aggressorValue;
        }

        if (move.IsPromotion()) return 900000;

        // Use thread-local killer moves
        if (ply < MAX_PLY)
        {
            if (move == tld.killerMoves[ply][0]) return 800000;
            if (move == tld.killerMoves[ply][1]) return 700000;
        }

        // Use thread-local history
        const Piece movingPiece = board.GetPieceAt(move.GetFrom());
        const int sideIndex = static_cast<int>(movingPiece.GetColor());
        return tld.history[sideIndex][move.GetFrom()][move.GetTo()];
    }

    void AIPlayer::OrderMovesSimple(std::vector<Move>& moves, const Board& board, Move ttMove)
    {
        std::vector<std::pair<int, Move>> scoredMoves;

        for (const auto& move : moves)
        {
            int score = 0;

            // Highest priority: TT move
            if (move == ttMove)
            {
                score = 10000000;
            }
            // High priority: Captures using MVV-LVA
            else if (move.IsCapture())
            {
                Piece victim = move.GetCaptured();
                Piece aggressor = board.GetPieceAt(move.GetFrom());
                int victimValue = PIECE_VALUES[static_cast<int>(victim.GetType())];
                int aggressorValue = PIECE_VALUES[static_cast<int>(aggressor.GetType())];
                score = 1000000 + victimValue * 10 - aggressorValue;
            }
            // Good priority: Promotions
            else if (move.IsPromotion())
            {
                score = 900000;
            }

            scoredMoves.push_back({score, move});
        }

        std::sort(scoredMoves.begin(), scoredMoves.end(),
            [](const auto& a, const auto& b) { return a.first > b.first; });

        moves.clear();
        for (const auto& [score, move] : scoredMoves)
        {
            moves.push_back(move);
        }
    }

    int AIPlayer::HelperAlphaBeta(Board& board, int depth, int alpha, int beta, int ply)
    {
        if (m_abortSearch.load()) return 0;

        uint64_t zobristKey = board.GetZobristKey();
        Move ttMove;
        int ttScore;
        if (m_transpositionTable.Probe(zobristKey, depth, alpha, beta, ttScore, ttMove))
        {
            return ttScore;
        }

        if (depth == 0)
        {
            return HelperQuiescenceSearch(board, alpha, beta, ply, 0);
        }

        auto moves = board.GenerateLegalMoves();
        if (moves.empty())
        {
            if (board.IsInCheck(board.GetCurrentPlayer()))
            {
                return -MATE_SCORE + ply;
            }
            return 0;
        }

        if (depth >= 3 && !board.IsInCheck(board.GetCurrentPlayer()))
        {
            const int R = 2;
            board.MakeNullMoveUnchecked();
            int score = -HelperAlphaBeta(board, depth - 1 - R, -beta, -beta + 1, ply + 1);
            board.UndoNullMove();

            if (score >= beta)
            {
                return beta;
            }
        }

        OrderMovesSimple(moves, board, ttMove);

        Move bestMove;
        int bestScore = -INFINITY_SCORE;
        uint8_t flag = TT_ALPHA;

        bool sideInCheck = board.IsInCheck(board.GetCurrentPlayer());
        int moveIndex = 0;

        for (const auto& move : moves)
        {
            board.MakeMoveUnchecked(move);

            int score;

            bool isQuiet = !move.IsCapture() &&
                           !move.IsPromotion() &&
                           !move.IsEnPassant() &&
                           !move.IsCastling();

            bool applyLMR = depth >= 3 &&
                            moveIndex >= 4 &&
                            !sideInCheck &&
                            isQuiet;

            if (applyLMR)
            {
                score = -HelperAlphaBeta(board, depth - 2, -beta, -alpha, ply + 1);

                if (score > alpha)
                {
                    score = -HelperAlphaBeta(board, depth - 1, -beta, -alpha, ply + 1);
                }
            }
            else
            {
                score = -HelperAlphaBeta(board, depth - 1, -beta, -alpha, ply + 1);
            }

            board.UndoMove();

            if (score > bestScore)
            {
                bestScore = score;
                bestMove = move;
            }

            if (score >= beta)
            {
                m_transpositionTable.Store(zobristKey, depth, beta, TT_BETA, bestMove);
                return beta;
            }

            if (score > alpha)
            {
                alpha = score;
                flag = TT_EXACT;
            }

            moveIndex++;
        }

        m_transpositionTable.Store(zobristKey, depth, bestScore, flag, bestMove);
        return bestScore;
    }

    int AIPlayer::HelperQuiescenceSearch(Board& board, int alpha, int beta, int ply, int qDepth)
    {
        if (m_abortSearch.load() || qDepth >= 8)
        {
            return Evaluate(board);
        }

        const PlayerColor sideToMove = board.GetCurrentPlayer();

        if (board.IsInCheck(sideToMove))
        {
            auto evasions = board.GenerateLegalMoves();
            if (evasions.empty())
            {
                return -MATE_SCORE + ply;
            }

            OrderMovesSimple(evasions, board, Move());

            int best = -INFINITY_SCORE;

            for (const auto& move : evasions)
            {
                board.MakeMoveUnchecked(move);

                int score = -HelperQuiescenceSearch(board, -beta, -alpha, ply + 1, qDepth + 1);

                board.UndoMove();

                if (score > best) best = score;

                if (score >= beta)
                {
                    return beta;
                }
                if (score > alpha)
                {
                    alpha = score;
                }
            }

            return alpha;
        }

        int standPat = Evaluate(board);

        if (standPat >= beta)
        {
            return beta;
        }

        if (standPat > alpha)
        {
            alpha = standPat;
        }

        auto moves = board.GenerateLegalMoves();
        std::vector<Move> tacticalMoves;
        tacticalMoves.reserve(moves.size());

        for (const auto& move : moves)
        {
            if (move.IsCapture() || move.IsPromotion())
            {
                tacticalMoves.push_back(move);
            }
        }

        OrderMovesSimple(tacticalMoves, board, Move());

        for (const auto& move : tacticalMoves)
        {
            board.MakeMoveUnchecked(move);

            int score = -HelperQuiescenceSearch(board, -beta, -alpha, ply + 1, qDepth + 1);

            board.UndoMove();

            if (score >= beta)
            {
                return beta;
            }
            if (score > alpha)
            {
                alpha = score;
            }
        }

        return alpha;
    }

    void AIPlayer::HelperSearch(Board board, int maxDepth) {
        for (int depth = 1; depth <= maxDepth; ++depth) {
            if (m_abortSearch.load()) return;

            auto moves = board.GenerateLegalMoves();
            if (moves.empty()) return;

            OrderMovesSimple(moves, board, Move());

            for (size_t i = 0; i < moves.size(); ++i) {
                if (m_abortSearch.load()) return;

                const Move& move = moves[i];
                board.MakeMoveUnchecked(move);

                int reduction = 0;
                if (i >= 4 && depth >= 3 &&
                    !move.IsCapture() &&
                    !move.IsPromotion() &&
                    !board.IsInCheck(board.GetCurrentPlayer())) {
                    reduction = 1;
                }

                int score = -HelperAlphaBeta(board, depth - 1 - reduction,
                                      -INFINITY_SCORE, INFINITY_SCORE, 1);

                board.UndoMove();
            }
        }
    }

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
    void ChessGame::NewGame(GameMode mode)
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
            m_players[0].isAI = false;  // Human plays white
            m_players[1].isAI = true;   // Computer plays black
            
            // Set default difficulty if not configured
            if (m_players[1].aiDifficulty == 5)
            {
                m_players[1].aiDifficulty = 3;
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
            return false; // No moves to undo
        }
        
        m_currentHistoryIndex--;
        m_board = m_boardHistory[m_currentHistoryIndex];
        
        // Remove last move from history
        if (!m_moveHistory.empty())
        {
            m_moveHistory.pop_back();
        }
        
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
        
        // Note: Full implementation would need to rebuild move history for redo
        
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
        
        if (bestMove.GetFrom() != bestMove.GetTo()) // Valid move check
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
    bool PGNParser::ParseHeader(const std::string& /*line*/, GameRecord& /*record*/)
    {
        return false;
    }

    // Parse move in Standard Algebraic Notation (placeholder)
    bool PGNParser::ParseMove(const std::string& /*moveStr*/, Board& /*board*/, std::string& /*sanMove*/)
    {
        return false;
    }

    // Convert Move to Standard Algebraic Notation (placeholder)
    std::string PGNParser::MoveToSAN(const Move& /*move*/, const Board& /*board*/)
    {
        return "";
    }

    // Convert Standard Algebraic Notation to Move (placeholder)
    Move PGNParser::SANToMove(const std::string& /*san*/, const Board& /*board*/)
    {
        return Move();
    }
}