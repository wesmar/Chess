// ChessGame.cpp
// Chess game controller and AI player implementation
// Handles game logic, move history, AI search with alpha-beta pruning,
// transposition tables, move ordering heuristics, and PGN import/export

#define NOMINMAX
#include "ChessGame.h"
#include "../Engine/Evaluation.h"
#include "../Engine/OpeningBook.h"
#include "../Engine/MoveGenerator.h"
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

        // Allocate transposition table and threads based on difficulty
        if (m_difficulty >= 6)
        {
            m_transpositionTable.Resize(64);
            m_numThreads = std::thread::hardware_concurrency();
            if (m_numThreads < 1) m_numThreads = 4;
        }
        else
        {
            m_transpositionTable.Resize(16);
            m_numThreads = 1;
        }

        // Initialize killer move heuristic storage
        for (int i = 0; i < MAX_PLY; ++i)
        {
            m_killerMoves[i][0] = Move();
            m_killerMoves[i][1] = Move();
        }

        // Initialize shared history heuristic tables and counter moves (separate per side)
        for (int side = 0; side < 2; ++side)
        {
            for (int from = 0; from < 64; ++from)
            {
                for (int to = 0; to < 64; ++to)
                {
                    m_history[side][from][to].store(0, std::memory_order_relaxed);
                    m_counterMoves[side][from][to] = Move();
                }
            }
        }

        // Load NNUE neural network for enhanced evaluation if available
        if (m_evaluator.LoadNnue("nn-small.nnue"))
        {
            m_evaluator.SetMode(Neural::EvalMode::Classical);
        }
    }

    // Update AI difficulty and resize transposition table accordingly
    void AIPlayer::SetDifficulty(DifficultyLevel difficulty)
    {
        m_difficulty = difficulty;
        if (m_difficulty < Difficulty::MIN) m_difficulty = Difficulty::MIN;
        if (m_difficulty > Difficulty::MAX) m_difficulty = Difficulty::MAX;

        // Adjust table size and threads based on difficulty
        if (m_difficulty >= 6)
        {
            m_transpositionTable.Resize(64);
            m_numThreads = std::thread::hardware_concurrency();
            if (m_numThreads < 1) m_numThreads = 4;
        }
        else
        {
            m_transpositionTable.Resize(16);
            m_numThreads = 1;
        }
    }

    void AIPlayer::SetThreads(int threads) {
        if (threads >= 1 && threads <= 64) {
            m_numThreads = threads;
        }
    }

	void AIPlayer::SetEvalCacheSizeMB(int sizeMB)
    {
        Chess::g_evalCache.Resize(sizeMB);
    }

    void AIPlayer::ClearEvalCache()
    {
        Chess::g_evalCache.Clear();
    }

    void AIPlayer::AbortSearch()
    {
        // Signal all search threads to stop as soon as possible.
        // The search loops check this flag periodically and will exit.
        m_abortSearch.store(true, std::memory_order_release);
    }

    bool AIPlayer::LoadNnue(const std::string& filename)
    {
        return m_evaluator.LoadNnue(filename);
    }

    // Assign score to move for move ordering optimization
    int AIPlayer::ScoreMove(const Move& move, const Board& board, Move ttMove, int ply)
    {
        // Highest priority: Principal Variation move from transposition table
        if (move == ttMove)
        {
            return 10000000;
        }

        // High priority: Captures using MVV-LVA and SEE
        if (move.IsCapture())
        {
            Piece victim = move.GetCaptured();
            Piece aggressor = board.GetPieceAt(move.GetFrom());

            int victimValue = PIECE_VALUES[static_cast<int>(victim.GetType())];
            int aggressorValue = PIECE_VALUES[static_cast<int>(aggressor.GetType())];

            int score = 1000000 + victimValue * 10 - aggressorValue;

            // SEE-based adjustment
            int seeValue = SEE(board, move);
            if (seeValue < 0)
            {
                score -= 100000;
            }
            else
            {
                score += seeValue;
            }

            return score;
        }

        // Good priority: Promotions
        if (move.IsPromotion())
        {
            return 900000;
        }

        // Killer move heuristic (non-capture moves that caused beta cutoffs)
        if (move == m_killerMoves[ply][0]) return 800000;
        if (move == m_killerMoves[ply][1]) return 700000;

        // Counter move heuristic - moves that refute opponent's previous move
        const Piece movingPiece = board.GetPieceAt(move.GetFrom());
        const int sideIndex = static_cast<int>(movingPiece.GetColor());

        if (ply > 0 && board.GetHistoryPly() > 0)
        {
            int prevFrom = -1, prevTo = -1;
            if (board.GetHistoryPly() > 0)
            {
                const auto& lastRecord = board.GetLastMoveRecord();
                prevFrom = lastRecord.move.GetFrom();
                prevTo = lastRecord.move.GetTo();
            }

            if (prevFrom >= 0 && prevTo >= 0)
            {
                int oppSide = 1 - sideIndex;
                Move counterMove = m_counterMoves[oppSide][prevFrom][prevTo];
                if (move == counterMove)
                    return 600000;
            }
        }

        // History heuristic: use shared table for the side that is making the move
        int historyScore = m_history[sideIndex][move.GetFrom()][move.GetTo()].load(std::memory_order_relaxed);

        // Tactical bonus for moves to central squares
        // Central control is crucial in chess - pieces on central squares
        // control more of the board and create tactical opportunities
        int toSquare = move.GetTo();
        int centerBonus = 0;
        
        if (toSquare == 27 || toSquare == 28 || toSquare == 35 || toSquare == 36)
        {
            centerBonus = 400;
        }
        else if ((toSquare >= 18 && toSquare <= 21) || (toSquare >= 42 && toSquare <= 45))
        {
            centerBonus = 150;
        }
        
        return historyScore + centerBonus;
    }
	// Sort moves by score for better alpha-beta pruning efficiency
    void AIPlayer::OrderMoves(MoveList& moves, const Board& board, Move ttMove, int ply)
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
        // Check abort flag first for immediate response to UCI "stop"
        if (m_abortSearch.load(std::memory_order_acquire))
            return true;

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

		// Reset NNUE accumulator state for new search
		m_evaluator.PrepareSearch();

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
			// Calculate actual ply count from move number and side to move
			// Formula: ply = (fullMoveNumber - 1) * 2 + (side == Black ? 1 : 0)
			// Example: Move 1 White = ply 0, Move 1 Black = ply 1, Move 2 White = ply 2
			int ply = (board.GetFullMoveNumber() - 1) * 2;
			if (board.GetSideToMove() == PlayerColor::Black)
				ply++;

			auto bookMove = ProbeBook(board, ply);
			if (bookMove.has_value())
				return bookMove.value();
		}

		const int MAX_DEPTH = 30;

		// Level 1: Weak play with preference for active moves
		// Uses 1-ply evaluation with bonuses for captures, development, and center control
		// Large margin (600) maintains weakness while avoiding purely passive play
		if (m_difficulty == 1)
		{
			const int margin = 600;

			int bestMoveScore = -INFINITY_SCORE;
			
			// First pass: find best score with activity bonuses
			for (int i = 0; i < legalMoves.size(); ++i)
			{
				const Move& move = legalMoves[i];
				searchBoard.MakeMoveUnchecked(move);

				int score = -Evaluate(searchBoard);
				
				searchBoard.UndoMove();

				// Bonuses for "interesting" moves to avoid pure passivity
				if (move.IsCapture())
				{
					score += 120;
				}

				Piece movedPiece = board.GetPieceAt(move.GetFrom());
				int fromRank = move.GetFrom() / 8;
				int toFile = move.GetTo() % 8;

				// Encourage piece development from back rank
				if (movedPiece.GetType() == PieceType::Knight || 
					movedPiece.GetType() == PieceType::Bishop)
				{
					int backRank = (movedPiece.GetColor() == PlayerColor::White) ? 0 : 7;
					if (fromRank == backRank)
					{
						score += 90;
					}
				}

				// Encourage central pawn advances
				if (movedPiece.GetType() == PieceType::Pawn)
				{
					if (toFile == 3 || toFile == 4)
					{
						score += 60;
					}
				}

				// Small bonus for castling
				if (move.IsCastling())
				{
					score += 80;
				}

				if (score > bestMoveScore)
				{
					bestMoveScore = score;
				}
			}

			// Second pass: collect moves within margin
			std::array<Move, 256> candidates;
			int candidateCount = 0;

			for (int i = 0; i < legalMoves.size(); ++i)
			{
				const Move& move = legalMoves[i];
				searchBoard.MakeMoveUnchecked(move);

				int score = -Evaluate(searchBoard);
				
				searchBoard.UndoMove();

				// Apply same bonuses as first pass
				if (move.IsCapture())
				{
					score += 120;
				}

				Piece movedPiece = board.GetPieceAt(move.GetFrom());
				int fromRank = move.GetFrom() / 8;
				int toFile = move.GetTo() % 8;

				if (movedPiece.GetType() == PieceType::Knight || 
					movedPiece.GetType() == PieceType::Bishop)
				{
					int backRank = (movedPiece.GetColor() == PlayerColor::White) ? 0 : 7;
					if (fromRank == backRank)
					{
						score += 90;
					}
				}

				if (movedPiece.GetType() == PieceType::Pawn)
				{
					if (toFile == 3 || toFile == 4)
					{
						score += 60;
					}
				}

				if (move.IsCastling())
				{
					score += 80;
				}

				if (score >= bestMoveScore - margin)
				{
					candidates[candidateCount++] = move;
				}
			}

			if (candidateCount <= 0)
			{
				return legalMoves[0];
			}

			std::random_device rd;
			std::mt19937 gen(rd());
			std::uniform_int_distribution<> dis(0, candidateCount - 1);
			return candidates[dis(gen)];
		}
		// Level 2: Amateur-friendly play (2-ply minimax)
		// Looks ahead to opponent's best response to avoid obvious blunders
		// Smaller margin (250) keeps some variety without terrible moves
		if (m_difficulty == 2)
		{
			const int margin = 250;

			int bestMoveScore = -INFINITY_SCORE;

			// First pass: compute 2-ply scores for all moves
			// For each candidate move, opponent gets to respond optimally
			for (int i = 0; i < legalMoves.size(); ++i)
			{
				const Move& move = legalMoves[i];
				searchBoard.MakeMoveUnchecked(move);

				// Generate opponent's legal responses
				auto opponentReplies = searchBoard.GenerateLegalMoves();
				int worstScoreForUs = -INFINITY_SCORE;

				if (opponentReplies.empty())
				{
					// No legal moves for opponent - checkmate or stalemate
					// Evaluate from opponent's perspective (they're on move)
					worstScoreForUs = Evaluate(searchBoard);
				}
				else
				{
					// Find opponent's best reply (worst outcome for us)
					for (int r = 0; r < opponentReplies.size(); ++r)
					{
						searchBoard.MakeMoveUnchecked(opponentReplies[r]);

						// After opponent's reply, evaluate from our perspective (negated)
						int replyScore = -Evaluate(searchBoard);

						searchBoard.UndoMove();

						if (replyScore > worstScoreForUs)
						{
							worstScoreForUs = replyScore;
						}
					}
				}

				// Our 2-ply score accounts for opponent's best response
				int score = -worstScoreForUs;

				searchBoard.UndoMove();

				if (score > bestMoveScore)
				{
					bestMoveScore = score;
				}
			}

			// Second pass: collect candidate moves within margin
			std::array<Move, 256> candidates;
			int candidateCount = 0;

			for (int i = 0; i < legalMoves.size(); ++i)
			{
				const Move& move = legalMoves[i];
				searchBoard.MakeMoveUnchecked(move);

				auto opponentReplies = searchBoard.GenerateLegalMoves();
				int worstScoreForUs = -INFINITY_SCORE;

				if (opponentReplies.empty())
				{
					worstScoreForUs = Evaluate(searchBoard);
				}
				else
				{
					for (int r = 0; r < opponentReplies.size(); ++r)
					{
						searchBoard.MakeMoveUnchecked(opponentReplies[r]);
						int replyScore = -Evaluate(searchBoard);
						searchBoard.UndoMove();

						if (replyScore > worstScoreForUs)
						{
							worstScoreForUs = replyScore;
						}
					}
				}

				int score = -worstScoreForUs;

				searchBoard.UndoMove();

				// Keep moves close to best score
				if (score >= bestMoveScore - margin)
				{
					candidates[candidateCount++] = move;
				}
			}

			if (candidateCount <= 0)
			{
				return legalMoves[0];
			}

			std::random_device rd;
			std::mt19937 gen(rd());
			std::uniform_int_distribution<> dis(0, candidateCount - 1);
			return candidates[dis(gen)];
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
					m_history[side][from][to].store(0, std::memory_order_relaxed);
					m_counterMoves[side][from][to] = Move();
				}
			}
		}

		Move bestMoveSoFar = legalMoves[0];
		int bestScore = -INFINITY_SCORE;

		// Iterative deepening with root-parallel search and aspiration windows
		for (int depth = 1; depth <= MAX_DEPTH; ++depth)
		{
			if (ShouldStop()) break;

			OrderMoves(legalMoves, searchBoard, bestMoveSoFar, 0);

			int alpha = -INFINITY_SCORE;
			int beta = INFINITY_SCORE;

			// Aspiration windows for depth >= 4
			if (depth >= 4 && bestScore > -MATE_SCORE + 1000 && bestScore < MATE_SCORE - 1000)
			{
				const int ASPIRATION_WINDOW = 100;
				alpha = bestScore - ASPIRATION_WINDOW;
				beta = bestScore + ASPIRATION_WINDOW;
			}

			Move iterBestMove = legalMoves[0];
			int iterBestScore = -INFINITY_SCORE;

			// Root-parallel search with PV searched first (proper root PVS)
			if (m_numThreads > 1 && legalMoves.size() > 1 && depth >= 4)
			{
				// Search PV move first in main thread with full window
				searchBoard.MakeMoveUnchecked(legalMoves[0]);
				ThreadLocalData pvTld;
				int pvScore = -WorkerAlphaBeta(searchBoard, depth - 1, -beta, -alpha, 1, pvTld);
				searchBoard.UndoMove();

				iterBestMove = legalMoves[0];
				iterBestScore = pvScore;

				if (pvScore > alpha)
				{
					alpha = pvScore;
				}

				// If PV already fails high or time is up, skip launching workers
				if (pvScore < beta && !ShouldStop())
				{
					// Launch workers for remaining moves starting from index 1
					int actualThreads = std::min(m_numThreads, static_cast<int>(legalMoves.size()) - 1);
					std::vector<std::future<std::pair<Move, int>>> futures;

					const Board rootBoard = searchBoard;
					const size_t totalMoves = legalMoves.size();
					auto nextMoveIndex = std::make_shared<std::atomic<size_t>>(1);
					auto sharedAlpha = std::make_shared<std::atomic<int>>(alpha);

					for (int t = 0; t < actualThreads; ++t)
					{
						futures.push_back(std::async(std::launch::async,
							[=, &legalMoves]() mutable -> std::pair<Move, int> {
								ThreadLocalData tld;
								Board localBoard = rootBoard;
								Move localBest;
								int localScore = -INFINITY_SCORE;

								while (true)
								{
									size_t i = nextMoveIndex->fetch_add(1);
									if (i >= totalMoves) break;
									if (m_abortSearch.load(std::memory_order_acquire)) break;

									localBoard.MakeMoveUnchecked(legalMoves[i]);

									int currentAlpha = sharedAlpha->load(std::memory_order_acquire);

									// All worker moves use PVS narrow window
									int score = -WorkerAlphaBeta(localBoard, depth - 1,
										-currentAlpha - 1, -currentAlpha, 1, tld);

									if (score > currentAlpha && score < beta &&
										!m_abortSearch.load(std::memory_order_acquire))
									{
										score = -WorkerAlphaBeta(localBoard, depth - 1,
											-beta, -currentAlpha, 1, tld);
									}

									localBoard.UndoMove();

									if (score > localScore)
									{
										localScore = score;
										localBest = legalMoves[i];

										int prevAlpha = sharedAlpha->load();
										while (score > prevAlpha &&
											!sharedAlpha->compare_exchange_weak(prevAlpha, score))
										{
										}
									}
								}
								return std::make_pair(localBest, localScore);
							}
						));
					}

					for (auto& f : futures)
					{
						auto [move, score] = f.get();
						if (m_abortSearch.load(std::memory_order_acquire)) continue;
						if (score > iterBestScore)
						{
							iterBestScore = score;
							iterBestMove = move;
						}
					}
				}
			}
			else
			{
				// Single-threaded root search with proper PVS
				for (size_t i = 0; i < legalMoves.size(); ++i)
				{
					if (ShouldStop()) break;

					searchBoard.MakeMoveUnchecked(legalMoves[i]);

					int score;
					if (i == 0)
					{
						score = -AlphaBeta(searchBoard, depth - 1, -beta, -alpha, 1);
					}
					else
					{
						score = -AlphaBeta(searchBoard, depth - 1, -alpha - 1, -alpha, 1);
						if (score > alpha && score < beta && !ShouldStop())
						{
							score = -AlphaBeta(searchBoard, depth - 1, -beta, -alpha, 1);
						}
					}

					searchBoard.UndoMove();

					if (score > iterBestScore)
					{
						iterBestScore = score;
						iterBestMove = legalMoves[i];
					}

					if (score > alpha)
					{
						alpha = score;
					}
				}
			}

			if (ShouldStop()) break;

			// Handle aspiration window failures
			if (iterBestScore <= alpha - 50 || iterBestScore >= beta)
			{
				alpha = -INFINITY_SCORE;
				beta = INFINITY_SCORE;

				searchBoard.MakeMoveUnchecked(iterBestMove);
				iterBestScore = -AlphaBeta(searchBoard, depth - 1, -beta, -alpha, 1);
				searchBoard.UndoMove();
			}

			bestMoveSoFar = iterBestMove;
			bestScore = iterBestScore;
		}

		// Wait for all threads to complete before returning
		m_abortSearch.store(true, std::memory_order_release);

		return bestMoveSoFar;
	}
	// Filter pseudo-legal moves to legal moves by verifying king safety
    MoveList AIPlayer::FilterLegalMoves(Board& board, const MoveList& pseudoMoves,
                                         PlayerColor sideToMove, PlayerColor opponentColor)
    {
        MoveList legalMoves;
        for (const Move& move : pseudoMoves)
        {
            board.MakeMoveUnchecked(move);
            int kingSquare = board.GetKingSquare(sideToMove);
            
            if (kingSquare != -1 && !MoveGenerator::IsSquareAttacked(
                board.GetPieces(), kingSquare, opponentColor))
            {
                legalMoves.push_back(move);
            }
            
            board.UndoMove();
        }
        return legalMoves;
    }

    // Alpha-beta negamax search with transposition table
    int AIPlayer::AlphaBeta(Board& board, int depth, int alpha, int beta, int ply)
    {
        // Check time limit
        if (ShouldStop()) return 0;

        // Draw detection by repetition - check before TT probe
        // If position occurred 2+ times before, opponent can force draw
        if (ply > 0 && board.CountRepetitions() >= 2)
        {
            return 0; // Draw score
        }

        // Mate distance pruning - don't search for mates longer than already found
        // If we found mate in 5 moves, no point searching for mate in 8 moves
        // This ensures the engine always prefers the shortest mate sequence
        int mateAlpha = -MATE_SCORE + ply;
        int mateBeta = MATE_SCORE - ply - 1;
        if (alpha < mateAlpha) alpha = mateAlpha;
        if (beta > mateBeta) beta = mateBeta;
        if (alpha >= beta) return alpha;

		// Futility pruning - skip nodes unlikely to raise alpha
        // If position is far below alpha (by futilityMargin), return alpha immediately
        // Only apply when not in check (dangerous positions need full analysis)
        if (m_difficulty > 6 && depth <= 4 && !board.IsInCheck(board.GetCurrentPlayer()))
        {
            int staticEval = m_evaluator.Evaluate(board);
            int futilityMargin = 80 + 100 * depth;

            if (staticEval + futilityMargin <= alpha)
            {
                return alpha;
            }
        }

        // Reverse futility pruning - return beta if position is too good
        // If position evaluation exceeds beta by large margin, opponent likely
        // has better alternatives earlier in tree. Return early cutoff.
        if (m_difficulty > 6 && depth <= 3 && !board.IsInCheck(board.GetCurrentPlayer()))
        {
            int staticEval = m_evaluator.Evaluate(board);
            int rfpMargin = 120 * depth;

            if (staticEval - rfpMargin >= beta)
            {
                return beta;
            }
        }

        // Probe transposition table for cached result
        uint64_t zobristKey = board.GetZobristKey();
        Move ttMove;
        int ttScore;
        if (m_transpositionTable.Probe(zobristKey, depth, alpha, beta, ttScore, ttMove, ply))
        {
            return ttScore;
        }

        // Terminal depth reached - switch to quiescence search
        if (depth == 0)
        {
            return QuiescenceSearch(board, alpha, beta, ply, 0);
        }

        // Generate pseudo-legal moves for current position
        const PlayerColor sideToMove = board.GetCurrentPlayer();
        const PlayerColor opponentColor = (sideToMove == PlayerColor::White)
            ? PlayerColor::Black : PlayerColor::White;

        const auto& castlingRights = board.GetCastlingRights();
        const auto& pieceList = board.GetPieceList(sideToMove);

        MoveList pseudoMoves = MoveGenerator::GeneratePseudoLegalMoves(
            board.GetPieces(),
            sideToMove,
            board.GetEnPassantSquare(),
            &castlingRights,
            &pieceList
        );

        MoveList moves = FilterLegalMoves(board, pseudoMoves, sideToMove, opponentColor);

        if (moves.empty())
        {
            // No legal moves - checkmate or stalemate
            if (board.IsInCheck(sideToMove))
            {
                return -MATE_SCORE + ply; // Checkmate (prefer shorter mates)
            }
            return 0; // Stalemate
        }

        // Null move pruning - disabled in endgames to avoid zugzwang errors
        // In positions with low material (phase < 64), zugzwang is common
        // and null-move can produce false beta cutoffs
        int phase = ComputePhase(board);
        if (m_difficulty > 6 && depth >= 3 && phase > 64 && !board.IsInCheck(sideToMove))
        {
            int R = 3 + depth / 4;
            if (R > depth - 1) R = depth - 1;

            board.MakeNullMoveUnchecked();
            int score = -AlphaBeta(board, depth - 1 - R, -beta, -beta + 1, ply + 1);
            board.UndoNullMove();

            if (score >= beta)
            {
                return beta;
            }
        }

        // Internal Iterative Deepening - search to find good move ordering
        // When we have no hash move at high depths, do a reduced depth search
        // to populate the TT with a best move for better move ordering
        if (m_difficulty > 6 && depth >= 6 && ttMove.GetFrom() == ttMove.GetTo())
        {
            int iidDepth = depth - 2;
            AlphaBeta(board, iidDepth, alpha, beta, ply);
            m_transpositionTable.Probe(zobristKey, 0, alpha, beta, ttScore, ttMove, ply);
        }

        const int sideIndex = static_cast<int>(sideToMove);

        OrderMoves(moves, board, ttMove, ply);

        Move bestMove;
        int bestScore = -INFINITY_SCORE;
        uint8_t flag = TT_ALPHA;

        bool sideInCheck = board.IsInCheck(sideToMove);

        // Check extension - search deeper when in check to find escape sequences
        if (sideInCheck && ply < MAX_PLY - 1)
        {
            depth++;
        }

        int moveIndex = 0;

        // Search all legal moves
        for (const auto& move : moves)
        {
            // Check if move is quiet (not tactical)
            bool isQuiet = !move.IsCapture() &&
                           !move.IsPromotion() &&
                           !move.IsEnPassant() &&
                           !move.IsCastling();

            // Late Move Pruning - skip late quiet moves at moderate depths
            // After searching many moves, remaining quiet moves are unlikely to improve score
            // Only apply when not at root and depth is moderate
            if (ply > 0 &&
                depth >= 3 && depth <= 7 &&
                moveIndex >= (4 + depth * depth / 2) &&
                !sideInCheck &&
                isQuiet)
            {
                moveIndex++;
                continue;
            }

            m_evaluator.OnMakeMove();
            board.MakeMoveUnchecked(move);

            int score;

            // Check if move gives check to opponent
            const PlayerColor opponentColorAfterMove = (sideToMove == PlayerColor::White)
                ? PlayerColor::Black : PlayerColor::White;
            bool givesCheck = board.IsInCheck(opponentColorAfterMove);

            // Late Move Reduction - reduce search depth for likely poor moves
            // Moves ordered later are less likely to be good, so search them at reduced depth
            // Don't reduce moves that give check or other forcing moves
            bool applyLMR = depth >= 3 &&
                            moveIndex >= 4 &&
                            !sideInCheck &&
                            !givesCheck &&
                            isQuiet;

            if (applyLMR)
            {
                int lmrReduction = 1 + moveIndex / 8 + depth / 8;
                if (lmrReduction >= depth) lmrReduction = depth - 1;

                score = -AlphaBeta(board, depth - 1 - lmrReduction, -alpha - 1, -alpha, ply + 1);

                if (score > alpha)
                {
                    score = -AlphaBeta(board, depth - 1, -beta, -alpha, ply + 1);
                }
            }
            else
            {
                score = -AlphaBeta(board, depth - 1, -beta, -alpha, ply + 1);
            }

            board.UndoMove();
            m_evaluator.OnUndoMove();

            if (score > bestScore)
            {
                bestScore = score;
                bestMove = move;
            }

            // Beta cutoff - this move is too good, opponent won't allow it
            if (score >= beta)
            {
                // Update history and killer moves for quiet moves that cause cutoffs
                if (isQuiet)
                {
                    // Use depth squared to give more weight to deeper cutoffs (more reliable)
					m_history[sideIndex][move.GetFrom()][move.GetTo()].fetch_add(depth * depth, std::memory_order_relaxed);

                    // Store killer move
                    if (ply < MAX_PLY)
                    {
                        m_killerMoves[ply][1] = m_killerMoves[ply][0];
                        m_killerMoves[ply][0] = move;
                    }

                    // Store counter move - this move refuted opponent's last move
                    if (board.GetHistoryPly() > 0)
                    {
                        const auto& lastRec = board.GetLastMoveRecord();
                        int prevSide = 1 - sideIndex;
                        m_counterMoves[prevSide][lastRec.move.GetFrom()][lastRec.move.GetTo()] = move;
                    }
                }
                m_transpositionTable.Store(zobristKey, depth, beta, TT_BETA, bestMove, ply);
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

        m_transpositionTable.Store(zobristKey, depth, bestScore, flag, bestMove, ply);
        return bestScore;
    }

    // Quiescence search - search only tactical moves to avoid horizon effect
    int AIPlayer::QuiescenceSearch(Board& board, int alpha, int beta, int ply, int qDepth)
    {
        // Check time and depth limits
        if (ShouldStop() || qDepth >= 8)
        {
            return m_evaluator.Evaluate(board);
        }

        const PlayerColor sideToMove = board.GetCurrentPlayer();

        // If in check, we must search all evasions (stand-pat is illegal in check)
        if (board.IsInCheck(sideToMove))
        {
            const PlayerColor opponentColor = (sideToMove == PlayerColor::White)
                ? PlayerColor::Black : PlayerColor::White;

            const auto& castlingRights = board.GetCastlingRights();
            const auto& pieceList = board.GetPieceList(sideToMove);

            MoveList pseudoEvasions = MoveGenerator::GeneratePseudoLegalMoves(
                board.GetPieces(),
                sideToMove,
                board.GetEnPassantSquare(),
                &castlingRights,
                &pieceList
            );

            MoveList evasions = FilterLegalMoves(board, pseudoEvasions, sideToMove, opponentColor);

            if (evasions.empty())
            {
                return -MATE_SCORE + ply; // Checkmated
            }

            OrderMoves(evasions, board, Move(), ply);

            int best = -INFINITY_SCORE;

            for (const auto& move : evasions)
            {
                m_evaluator.OnMakeMove();
                board.MakeMoveUnchecked(move);

                int score = -QuiescenceSearch(board, -beta, -alpha, ply + 1, qDepth + 1);

                board.UndoMove();
                m_evaluator.OnUndoMove();

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
        int standPat = m_evaluator.Evaluate(board);

        if (standPat >= beta)
        {
            return beta;
        }

        if (standPat > alpha)
        {
            alpha = standPat;
        }

        // Delta pruning: if position is too bad, even best possible capture won't help
        // Use queen value (900) + margin (200) for safety (accounts for promotions)
        const int QUEEN_VALUE = 900;
        const int DELTA_MARGIN = 200;
        if (standPat + QUEEN_VALUE + DELTA_MARGIN < alpha)
        {
            return alpha; // Position hopeless, skip tactical search
        }

        MoveList pseudoTacticalMoves = MoveGenerator::GenerateTacticalMoves(
            board.GetPieces(),
            board.GetSideToMove(),
            board.GetEnPassantSquare(),
            &board.GetPieceList(board.GetSideToMove())
        );

        PlayerColor movedColor = board.GetSideToMove();
        PlayerColor opponentColor = (movedColor == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White;
        Board temp = board;

        MoveList tacticalMoves = FilterLegalMoves(temp, pseudoTacticalMoves, movedColor, opponentColor);

        OrderMoves(tacticalMoves, board, Move(), ply);

        for (const auto& move : tacticalMoves)
        {
            // SEE pruning: skip losing captures unless promotion
            if (move.IsCapture() && !move.IsPromotion())
            {
                if (SEE(board, move) < 0)
                {
                    continue;
                }
            }

            m_evaluator.OnMakeMove();
            board.MakeMoveUnchecked(move);

            int score = -QuiescenceSearch(board, -beta, -alpha, ply + 1, qDepth + 1);

            board.UndoMove();
            m_evaluator.OnUndoMove();

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

        // Draw detection by repetition
        if (ply > 0 && board.CountRepetitions() >= 2)
        {
            return 0;
        }

        // Mate distance pruning - don't search for mates longer than already found
        // If we found mate in 5 moves, no point searching for mate in 8 moves
        // This ensures the engine always prefers the shortest mate sequence
        int mateAlpha = -MATE_SCORE + ply;
        int mateBeta = MATE_SCORE - ply - 1;
        if (alpha < mateAlpha) alpha = mateAlpha;
        if (beta > mateBeta) beta = mateBeta;
        if (alpha >= beta) return alpha;

        uint64_t zobristKey = board.GetZobristKey();
        Move ttMove;
        int ttScore;
        if (m_transpositionTable.Probe(zobristKey, depth, alpha, beta, ttScore, ttMove, ply))
        {
            return ttScore;
        }

        if (depth == 0)
        {
            return WorkerQuiescence(board, alpha, beta, ply, 0, tld);
        }

        const PlayerColor sideToMove = board.GetCurrentPlayer();
        const PlayerColor opponentColor = (sideToMove == PlayerColor::White)
            ? PlayerColor::Black : PlayerColor::White;

        const auto& castlingRights = board.GetCastlingRights();
        const auto& pieceList = board.GetPieceList(sideToMove);

        MoveList pseudoMoves = MoveGenerator::GeneratePseudoLegalMoves(
            board.GetPieces(),
            sideToMove,
            board.GetEnPassantSquare(),
            &castlingRights,
            &pieceList
        );

        MoveList moves = FilterLegalMoves(board, pseudoMoves, sideToMove, opponentColor);

        if (moves.empty())
        {
            if (board.IsInCheck(sideToMove))
            {
                return -MATE_SCORE + ply;
            }
            return 0;
        }

        // Null move pruning - disabled in endgames to avoid zugzwang errors
        int phase = ComputePhase(board);
        if (m_difficulty > 6 && depth >= 3 && phase > 64 && !board.IsInCheck(sideToMove))
        {
            int R = 3 + depth / 4;
            if (R > depth - 1) R = depth - 1;

            board.MakeNullMoveUnchecked();
            int score = -WorkerAlphaBeta(board, depth - 1 - R, -beta, -beta + 1, ply + 1, tld);
            board.UndoNullMove();

            if (score >= beta)
            {
                return beta;
            }
        }

        // Internal Iterative Deepening - search to find good move ordering
        if (m_difficulty > 6 && depth >= 6 && ttMove.GetFrom() == ttMove.GetTo())
        {
            int iidDepth = depth - 2;
            WorkerAlphaBeta(board, iidDepth, alpha, beta, ply, tld);
            m_transpositionTable.Probe(zobristKey, 0, alpha, beta, ttScore, ttMove, ply);
        }

        const int sideIndex = static_cast<int>(sideToMove);

        OrderMovesWorker(moves, board, ttMove, ply, tld);

        Move bestMove;
        int bestScore = -INFINITY_SCORE;
        uint8_t flag = TT_ALPHA;

        bool sideInCheck = board.IsInCheck(sideToMove);

        if (sideInCheck && ply < MAX_PLY - 1)
        {
            depth++;
        }

        int moveIndex = 0;

        for (const auto& move : moves)
        {
            if ((moveIndex & 15) == 0 && ShouldStop()) break;

            bool isQuiet = !move.IsCapture() && !move.IsPromotion() &&
                           !move.IsEnPassant() && !move.IsCastling();

            // Late Move Pruning - skip late quiet moves at moderate depths
            // Only apply when not at root and depth is high enough
            if (ply > 0 &&
                depth >= 3 && depth <= 7 &&
                moveIndex >= (4 + depth * depth / 2) &&
                !sideInCheck &&
                isQuiet)
            {
                moveIndex++;
                continue;
            }

            board.MakeMoveUnchecked(move);

            int score;

            // Check if move gives check to opponent
            const PlayerColor opponentColorAfterMove = (sideToMove == PlayerColor::White)
                ? PlayerColor::Black : PlayerColor::White;
            bool givesCheck = board.IsInCheck(opponentColorAfterMove);

            bool applyLMR = depth >= 3 && moveIndex >= 4 && !sideInCheck && !givesCheck && isQuiet;

            if (applyLMR)
            {
                int lmrReduction = 1 + moveIndex / 8 + depth / 8;
                if (lmrReduction >= depth) lmrReduction = depth - 1;

                score = -WorkerAlphaBeta(board, depth - 1 - lmrReduction, -alpha - 1, -alpha, ply + 1, tld);
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
                // Update heuristics on beta cutoff
                if (isQuiet)
                {
                    // Use depth squared to give more weight to deeper cutoffs (more reliable)
					m_history[sideIndex][move.GetFrom()][move.GetTo()].fetch_add(depth * depth, std::memory_order_relaxed);

                    if (ply < MAX_PLY)
                    {
                        tld.killerMoves[ply][1] = tld.killerMoves[ply][0];
                        tld.killerMoves[ply][0] = move;
                    }

                    if (board.GetHistoryPly() > 0)
                    {
                        const auto& lastRec = board.GetLastMoveRecord();
                        int prevSide = 1 - sideIndex;
                        m_counterMoves[prevSide][lastRec.move.GetFrom()][lastRec.move.GetTo()] = move;
                    }
                }
                m_transpositionTable.Store(zobristKey, depth, beta, TT_BETA, bestMove, ply);
                return beta;
            }

            if (score > alpha)
            {
                alpha = score;
                flag = TT_EXACT;
            }

            moveIndex++;
        }

        m_transpositionTable.Store(zobristKey, depth, bestScore, flag, bestMove, ply);
        return bestScore;
    }

    // Worker thread quiescence search - handles only tactical moves
    int AIPlayer::WorkerQuiescence(Board& board, int alpha, int beta, int ply, int qDepth,
                                    ThreadLocalData& tld)
    {
        // Worker threads use classical evaluation for thread safety
        if (ShouldStop() || qDepth >= 8)
        {
            return Chess::Evaluate(board);
        }

        const PlayerColor sideToMove = board.GetCurrentPlayer();

        if (board.IsInCheck(sideToMove))
        {
            const PlayerColor opponentColor = (sideToMove == PlayerColor::White)
                ? PlayerColor::Black : PlayerColor::White;

            const auto& castlingRights = board.GetCastlingRights();
            const auto& pieceList = board.GetPieceList(sideToMove);

            MoveList pseudoEvasions = MoveGenerator::GeneratePseudoLegalMoves(
                board.GetPieces(),
                sideToMove,
                board.GetEnPassantSquare(),
                &castlingRights,
                &pieceList
            );

            MoveList evasions = FilterLegalMoves(board, pseudoEvasions, sideToMove, opponentColor);

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

        // Worker threads use classical evaluation for thread safety
        int standPat = Chess::Evaluate(board);
        if (standPat >= beta) return beta;
        if (standPat > alpha) alpha = standPat;

        // Delta pruning: if position is too bad, even best possible capture won't help
        // Use queen value (900) + margin (200) for safety (accounts for promotions)
        const int QUEEN_VALUE = 900;
        const int DELTA_MARGIN = 200;
        if (standPat + QUEEN_VALUE + DELTA_MARGIN < alpha)
        {
            return alpha; // Position hopeless, skip tactical search
        }

        // Generate tactical moves and filter for legality
        MoveList tacticalMoves = MoveGenerator::GenerateTacticalMoves(
            board.GetPieces(),
            sideToMove,
            board.GetEnPassantSquare(),
            &board.GetPieceList(sideToMove)
        );

        const PlayerColor opponentColor = (sideToMove == PlayerColor::White)
            ? PlayerColor::Black : PlayerColor::White;

        MoveList legalTacticalMoves = FilterLegalMoves(board, tacticalMoves, sideToMove, opponentColor);

        OrderMovesWorker(legalTacticalMoves, board, Move(), ply, tld);

        for (const auto& move : legalTacticalMoves)
        {
            // SEE pruning: skip losing captures unless promotion
            if (move.IsCapture() && !move.IsPromotion())
            {
                if (SEE(board, move) < 0)
                {
                    continue;
                }
            }

            board.MakeMoveUnchecked(move);
            int score = -WorkerQuiescence(board, -beta, -alpha, ply + 1, qDepth + 1, tld);
            board.UndoMove();

            if (score >= beta) return beta;
            if (score > alpha) alpha = score;
        }

        return alpha;
    }

    // Move ordering for worker threads using thread-local data
    void AIPlayer::OrderMovesWorker(MoveList& moves, const Board& board, Move ttMove,
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

            int score = 1000000 + victimValue * 10 - aggressorValue;

            // SEE-based adjustment
            int seeValue = SEE(board, move);
            if (seeValue < 0)
            {
                score -= 100000;
            }
            else
            {
                score += seeValue;
            }

            return score;
        }

        if (move.IsPromotion()) return 900000;

        // Use thread-local killer moves
        if (ply < MAX_PLY)
        {
            if (move == tld.killerMoves[ply][0]) return 800000;
            if (move == tld.killerMoves[ply][1]) return 700000;
        }

        // Counter move heuristic - check if this move refutes opponent's last move
        const Piece movingPiece = board.GetPieceAt(move.GetFrom());
        const int sideIndex = static_cast<int>(movingPiece.GetColor());

        if (ply > 0 && board.GetHistoryPly() > 0)
        {
            const auto& lastRecord = board.GetLastMoveRecord();
            int prevFrom = lastRecord.move.GetFrom();
            int prevTo = lastRecord.move.GetTo();
            int oppSide = 1 - sideIndex;
            Move counterMove = m_counterMoves[oppSide][prevFrom][prevTo];
            if (move == counterMove)
                return 600000;
        }

        // Read from shared history table
        int historyScore = m_history[sideIndex][move.GetFrom()][move.GetTo()].load(std::memory_order_relaxed);

        // Tactical bonus for central squares
        // Central control is crucial in chess - pieces on central squares
        // control more of the board and create tactical opportunities
        int toSquare = move.GetTo();
        int centerBonus = 0;

        if (toSquare == 27 || toSquare == 28 || toSquare == 35 || toSquare == 36)
        {
            centerBonus = 400;
        }
        else if ((toSquare >= 18 && toSquare <= 21) || (toSquare >= 42 && toSquare <= 45))
        {
            centerBonus = 150;
        }

        return historyScore + centerBonus;
    }

    // Simple move ordering without heuristics (used for basic sorting)
    void AIPlayer::OrderMovesSimple(MoveList& moves, const Board& board, Move ttMove)
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
            // High priority: Captures using MVV-LVA and SEE
            else if (move.IsCapture())
            {
                Piece victim = move.GetCaptured();
                Piece aggressor = board.GetPieceAt(move.GetFrom());
                int victimValue = PIECE_VALUES[static_cast<int>(victim.GetType())];
                int aggressorValue = PIECE_VALUES[static_cast<int>(aggressor.GetType())];
                score = 1000000 + victimValue * 10 - aggressorValue;

                // SEE-based adjustment
			int seeValue = SEE(board, move);
			if (seeValue < 0)
			{
			score -= 100000;
			}
			else
			{
			score += seeValue;
			}
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
			// Static Exchange Evaluation - evaluate capture sequence on target square
    int AIPlayer::SEE(const Board& board, const Move& move) const
    {
        if (!move.IsCapture() && !move.IsPromotion())
            return 0;

        const auto& pieces = board.GetPieces();
        int targetSquare = move.GetTo();
        int fromSquare = move.GetFrom();

        PlayerColor attacker = pieces[fromSquare].GetColor();
        PlayerColor defender = (attacker == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White;

        std::array<int, 32> gain;
        int depth = 0;

        // Initial capture value
        if (move.IsEnPassant())
        {
            int capturedPawnSquare = targetSquare + (attacker == PlayerColor::White ? -8 : 8);
            gain[depth] = PIECE_VALUES[static_cast<int>(PieceType::Pawn)];
        }
        else if (move.IsCapture())
        {
            gain[depth] = PIECE_VALUES[static_cast<int>(move.GetCaptured().GetType())];
        }
        else
        {
            gain[depth] = 0;
        }

        // Add promotion bonus
        if (move.IsPromotion())
        {
            gain[depth] += PIECE_VALUES[static_cast<int>(move.GetPromotion())]
                         - PIECE_VALUES[static_cast<int>(PieceType::Pawn)];
        }

        // Create a working copy of the board for exchange sequence
        std::array<Piece, SQUARE_COUNT> workingBoard = pieces;

        // Apply initial move
        Piece movingPiece = workingBoard[fromSquare];
        workingBoard[fromSquare] = EMPTY_PIECE;

        if (move.IsPromotion())
        {
            movingPiece = Piece(move.GetPromotion(), attacker);
        }

        if (move.IsEnPassant())
        {
            int capturedPawnSquare = targetSquare + (attacker == PlayerColor::White ? -8 : 8);
            workingBoard[capturedPawnSquare] = EMPTY_PIECE;
        }

        workingBoard[targetSquare] = movingPiece;

        int lastCapturedValue = PIECE_VALUES[static_cast<int>(movingPiece.GetType())];
        PlayerColor sideToMove = defender;

        // Simulate exchange sequence
        while (true)
        {
            depth++;

            // Find smallest attacker for current side
            auto attackers = GetSmallestAttacker(workingBoard, targetSquare, sideToMove);
            if (attackers.empty())
                break;

            int attackerSquare = attackers[0];
            Piece attackingPiece = workingBoard[attackerSquare];

            gain[depth] = lastCapturedValue - gain[depth - 1];
            lastCapturedValue = PIECE_VALUES[static_cast<int>(attackingPiece.GetType())];

            // Apply the capture
            workingBoard[attackerSquare] = EMPTY_PIECE;
            workingBoard[targetSquare] = attackingPiece;

            sideToMove = (sideToMove == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White;

            // Prune losing captures
            if (std::max(-gain[depth - 1], gain[depth]) < 0)
                break;

            if (depth >= 31)
                break;
        }

        // Negamax the gains
        while (--depth > 0)
        {
            gain[depth - 1] = -std::max(-gain[depth - 1], gain[depth]);
        }

        return gain[0];
    }

    // Find smallest attacker of given square by specified color
    std::vector<int> AIPlayer::GetSmallestAttacker(const std::array<Piece, SQUARE_COUNT>& pieces,
                                                    int square, PlayerColor attackerColor) const
    {
        std::vector<int> attackers;

        int file = square % 8;
        int rank = square / 8;

        // Check for pawn attackers (always return first found pawn)
        if (attackerColor == PlayerColor::White)
        {
            if (rank > 0)
            {
                if (file > 0)
                {
                    int sq = square - 9;
                    Piece p = pieces[sq];
                    if (p.IsType(PieceType::Pawn) && p.GetColor() == PlayerColor::White)
                    {
                        attackers.push_back(sq);
                        return attackers;
                    }
                }
                if (file < 7)
                {
                    int sq = square - 7;
                    Piece p = pieces[sq];
                    if (p.IsType(PieceType::Pawn) && p.GetColor() == PlayerColor::White)
                    {
                        attackers.push_back(sq);
                        return attackers;
                    }
                }
            }
        }
        else
        {
            if (rank < 7)
            {
                if (file > 0)
                {
                    int sq = square + 7;
                    Piece p = pieces[sq];
                    if (p.IsType(PieceType::Pawn) && p.GetColor() == PlayerColor::Black)
                    {
                        attackers.push_back(sq);
                        return attackers;
                    }
                }
                if (file < 7)
                {
                    int sq = square + 9;
                    Piece p = pieces[sq];
                    if (p.IsType(PieceType::Pawn) && p.GetColor() == PlayerColor::Black)
                    {
                        attackers.push_back(sq);
                        return attackers;
                    }
                }
            }
        }

        // Check for knight attackers
        static constexpr std::array<std::pair<int, int>, 8> KNIGHT_OFFSETS = {{
            {2, 1}, {1, 2}, {-1, 2}, {-2, 1},
            {-2, -1}, {-1, -2}, {1, -2}, {2, -1}
        }};

        for (const auto& [df, dr] : KNIGHT_OFFSETS)
        {
            int newFile = file + df;
            int newRank = rank + dr;
            if (newFile >= 0 && newFile < 8 && newRank >= 0 && newRank < 8)
            {
                int sq = newRank * 8 + newFile;
                Piece p = pieces[sq];
                if (p.IsType(PieceType::Knight) && p.GetColor() == attackerColor)
                {
                    attackers.push_back(sq);
                    return attackers;
                }
            }
        }

        // Check for bishop/queen on diagonals
        static constexpr std::array<std::pair<int, int>, 4> DIAGONALS = {{
            {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
        }};

        for (const auto& [df, dr] : DIAGONALS)
        {
            int f = file + df;
            int r = rank + dr;
            while (f >= 0 && f < 8 && r >= 0 && r < 8)
            {
                int sq = r * 8 + f;
                Piece p = pieces[sq];
                if (p)
                {
                    if (p.GetColor() == attackerColor &&
                        (p.IsType(PieceType::Bishop) || p.IsType(PieceType::Queen)))
                    {
                        attackers.push_back(sq);
                        return attackers;
                    }
                    break;
                }
                f += df;
                r += dr;
            }
        }

        // Check for rook/queen on orthogonals
        static constexpr std::array<std::pair<int, int>, 4> ORTHOGONALS = {{
            {1, 0}, {0, 1}, {-1, 0}, {0, -1}
        }};

        for (const auto& [df, dr] : ORTHOGONALS)
        {
            int f = file + df;
            int r = rank + dr;
            while (f >= 0 && f < 8 && r >= 0 && r < 8)
            {
                int sq = r * 8 + f;
                Piece p = pieces[sq];
                if (p)
                {
                    if (p.GetColor() == attackerColor &&
                        (p.IsType(PieceType::Rook) || p.IsType(PieceType::Queen)))
                    {
                        attackers.push_back(sq);
                        return attackers;
                    }
                    break;
                }
                f += df;
                r += dr;
            }
        }

        // Check for king
        static constexpr std::array<std::pair<int, int>, 8> KING_OFFSETS = {{
            {1, 0}, {1, 1}, {0, 1}, {-1, 1},
            {-1, 0}, {-1, -1}, {0, -1}, {1, -1}
        }};

        for (const auto& [df, dr] : KING_OFFSETS)
        {
            int newFile = file + df;
            int newRank = rank + dr;
            if (newFile >= 0 && newFile < 8 && newRank >= 0 && newRank < 8)
            {
                int sq = newRank * 8 + newFile;
                Piece p = pieces[sq];
                if (p.IsType(PieceType::King) && p.GetColor() == attackerColor)
                {
                    attackers.push_back(sq);
                    return attackers;
                }
            }
        }

        return attackers;
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