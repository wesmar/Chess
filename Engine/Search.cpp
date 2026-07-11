// Search.cpp
// AIPlayer main-thread search: iterative deepening with aspiration windows,
// PVS alpha-beta with transposition table, and quiescence search.
// Worker-thread search lives in SearchWorker.cpp; move ordering and SEE
// in MoveOrdering.cpp.

#define NOMINMAX
#include "Search.h"
#include "Evaluation.h"
#include "MoveGenerator.h"
#include "OpeningBook.h"
#include <algorithm>
#include <random>
#include <chrono>
#include <sstream>
#include <thread>
#include <future>
#include <limits>
#include <cmath>

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
            m_hashSizeMB = 64;
            m_transpositionTable.Resize(m_hashSizeMB);
            m_numThreads = std::thread::hardware_concurrency();
            if (m_numThreads < 1) m_numThreads = 4;
        }
        else
        {
            m_hashSizeMB = 16;
            m_transpositionTable.Resize(m_hashSizeMB);
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

        // Continuation history - heap allocated (2.25 MB), zero-initialized
        m_contHist = std::make_unique<std::atomic<int>[]>(CONT_HIST_SIZE);
        for (int i = 0; i < CONT_HIST_SIZE; ++i)
            m_contHist[i].store(0, std::memory_order_relaxed);

        // Load NNUE neural network for enhanced evaluation if available
        if (m_evaluator.LoadNnue("nn-small.nnue"))
        {
            m_evaluator.SetMode(Neural::EvalMode::Classical);
        }
    }

    AIPlayer::~AIPlayer()
    {
        // Stop any running search, then shut the helper pool down for good.
        m_abortSearch.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(m_helperMutex);
            m_poolQuit = true;
        }
        m_helperCv.notify_all();
        for (auto& t : m_helperPool)
            if (t.joinable()) t.join();
    }

    // ========== LAZY SMP HELPER POOL ==========

    void AIPlayer::EnsureHelperPool(int helpers)
    {
        if (helpers > 63) helpers = 63;
        while (static_cast<int>(m_helperPool.size()) < helpers)
        {
            int idx = static_cast<int>(m_helperPool.size());
            m_helperPool.emplace_back(&AIPlayer::HelperLoop, this, idx);
        }
    }

    void AIPlayer::StartHelpers(const Board& root, int maxDepth)
    {
        {
            std::lock_guard<std::mutex> lock(m_helperMutex);
            m_helperRoot = root;
            m_helperMaxDepth = maxDepth;
            m_searchId++;
            m_activeHelpers = static_cast<int>(m_helperPool.size());
        }
        m_helperCv.notify_all();
    }

    void AIPlayer::WaitHelpersIdle()
    {
        std::unique_lock<std::mutex> lock(m_helperMutex);
        m_helperDoneCv.wait(lock, [this] { return m_activeHelpers == 0; });
    }

    void AIPlayer::HelperLoop(int idx)
    {
        uint64_t lastSeenId = 0;
        for (;;)
        {
            Board root;
            int maxDepth;
            {
                std::unique_lock<std::mutex> lock(m_helperMutex);
                m_helperCv.wait(lock, [&] { return m_poolQuit || m_searchId != lastSeenId; });
                if (m_poolQuit) return;
                lastSeenId = m_searchId;
                root = m_helperRoot;
                maxDepth = m_helperMaxDepth;
            }

            // Lazy SMP: full-window iterative deepening on the whole root
            // position (ply 0), coordinating with the main thread purely via
            // the shared TT. Staggered start depth and step give the helpers
            // depth diversity: half lead one ply ahead, half sweep every ply.
            ThreadLocalData tld;
            int d = 1 + (idx % 2);
            const int step = ((idx / 2) % 2) ? 2 : 1;
            while (d <= maxDepth &&
                   !m_abortSearch.load(std::memory_order_acquire))
            {
                Board localBoard = root;
                WorkerAlphaBeta(localBoard, d, -INFINITY_SCORE, INFINITY_SCORE, 0, tld);
                d += step;
            }

            {
                std::lock_guard<std::mutex> lock(m_helperMutex);
                if (--m_activeHelpers == 0)
                    m_helperDoneCv.notify_all();
            }
        }
    }

    // Update AI difficulty and resize transposition table accordingly
    void AIPlayer::SetDifficulty(DifficultyLevel difficulty)
    {
        m_difficulty = difficulty;
        if (m_difficulty < Difficulty::MIN) m_difficulty = Difficulty::MIN;
        if (m_difficulty > Difficulty::MAX) m_difficulty = Difficulty::MAX;

        // Adjust threads based on difficulty. TT size respects UCI Hash option
        // (don't shrink user-configured hash on level change).
        if (m_difficulty >= 6)
        {
            if (m_hashSizeMB < 64) { m_hashSizeMB = 64; m_transpositionTable.Resize(m_hashSizeMB); }
            m_numThreads = std::thread::hardware_concurrency();
            if (m_numThreads < 1) m_numThreads = 4;
        }
        else
        {
            m_numThreads = 1;
        }
    }

    void AIPlayer::SetHashSize(int mb)
    {
        if (mb < 1) mb = 1;
        if (mb > 4096) mb = 4096;
        m_hashSizeMB = mb;
        m_transpositionTable.Resize(static_cast<size_t>(mb));
    }

    void AIPlayer::NewGameReset()
    {
        m_transpositionTable.Clear();
        for (int i = 0; i < MAX_PLY; ++i)
        {
            m_killerMoves[i][0] = Move();
            m_killerMoves[i][1] = Move();
        }
        for (int side = 0; side < 2; ++side)
            for (int from = 0; from < 64; ++from)
                for (int to = 0; to < 64; ++to)
                {
                    m_history[side][from][to].store(0, std::memory_order_relaxed);
                    m_counterMoves[side][from][to] = Move();
                }
        if (m_contHist)
            for (int i = 0; i < CONT_HIST_SIZE; ++i)
                m_contHist[i].store(0, std::memory_order_relaxed);
        Chess::g_evalCache.Clear();
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


    // Check if search time limit has been reached
    bool AIPlayer::ShouldStop() const
    {
        // Check abort flag first for immediate response to UCI "stop"
        if (m_abortSearch.load(std::memory_order_acquire))
            return true;

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_searchStartTime
        ).count();
        if (elapsed >= m_maxSearchTimeMs)
        {
            // Latch the timeout: every node checks this flag cheaply, so the
            // whole tree unwinds immediately instead of waiting for its own
            // (gated) clock check. See m_abortSearch comment in Search.h.
            m_abortSearch.store(true, std::memory_order_release);
            return true;
        }
        return false;
    }

	// Main search function - find best move using iterative deepening with root-parallel search
	Move AIPlayer::CalculateBestMove(const Board& board, int maxTimeMs, int maxDepth)
	{
		m_abortSearch.store(false, std::memory_order_release);
		m_searchStartTime = std::chrono::steady_clock::now();
		m_maxSearchTimeMs = maxTimeMs;

		// Reset NNUE accumulator state for new search
		m_evaluator.PrepareSearch();

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
		int searchMaxDepth = (maxDepth > 0 && maxDepth < MAX_DEPTH) ? maxDepth : MAX_DEPTH;

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

		// Preserve TT across moves in same game (cleared only on NewGameReset).
		// Killers cleared - they're ply-indexed and prior search's killers at ply N
		// refer to a position N plies deep from the *previous* root, not this one.
		for (int i = 0; i < MAX_PLY; ++i)
		{
			m_killerMoves[i][0] = Move();
			m_killerMoves[i][1] = Move();
		}
		// History: single age (right-shift by 3 ~= keep 87%) to preserve cross-move signal.
		for (int side = 0; side < 2; ++side)
			for (int from = 0; from < 64; ++from)
				for (int to = 0; to < 64; ++to)
				{
					int v = m_history[side][from][to].load(std::memory_order_relaxed);
					m_history[side][from][to].store(v - (v >> 3), std::memory_order_relaxed);
				}

		// Continuation history: same gentle decay
		for (int i = 0; i < CONT_HIST_SIZE; ++i)
		{
			int v = m_contHist[i].load(std::memory_order_relaxed);
			if (v != 0)
				m_contHist[i].store(v - (v >> 3), std::memory_order_relaxed);
		}

		// New TT generation: entries from this search outrank leftovers
		// of earlier searches in the replacement policy.
		m_transpositionTable.NewGeneration();

		// Reset node counter for this search.
		m_nodesSearched.store(0, std::memory_order_relaxed);

		// Launch Lazy SMP helpers: they search the same root at staggered
		// depths and share only the TT. Their results are never read directly;
		// the payoff is a hotter TT (deeper entries, better ordering hints)
		// for the main thread's iterative deepening below.
		const bool useHelpers = m_numThreads > 1 && m_difficulty >= 6 &&
		                        legalMoves.size() > 1;
		if (useHelpers)
		{
			EnsureHelperPool(m_numThreads - 1);
			StartHelpers(searchBoard, searchMaxDepth);
		}

		Move bestMoveSoFar = legalMoves[0];
		int bestScore = -INFINITY_SCORE;

		// Iterative deepening with root-parallel search and aspiration windows
		for (int depth = 1; depth <= searchMaxDepth; ++depth)
		{
			if (ShouldStop()) break;

			OrderMoves(legalMoves, searchBoard, bestMoveSoFar, 0);

			// Aspiration windows for depth >= 4: start with a narrow window
			// around the previous score and progressively widen on failure.
			int aspAlpha = -INFINITY_SCORE;
			int aspBeta = INFINITY_SCORE;
			int aspDelta = 30;
			if (depth >= 4 && bestScore > -MATE_SCORE + 1000 && bestScore < MATE_SCORE - 1000)
			{
				aspAlpha = bestScore - aspDelta;
				aspBeta = bestScore + aspDelta;
			}

			Move iterBestMove = legalMoves[0];
			int iterBestScore = -INFINITY_SCORE;

			// Aspiration retry loop - re-search with a wider window on fail low/high
			while (true)
			{
			int alpha = aspAlpha;
			int beta = aspBeta;
			iterBestMove = legalMoves[0];
			iterBestScore = -INFINITY_SCORE;

			// Root search: single-threaded PVS over the root moves. With Lazy
			// SMP active, helper threads are simultaneously deepening the same
			// position into the shared TT, so interior nodes here hit deep TT
			// entries far more often - that is the entire parallel gain.
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

			if (ShouldStop()) break;

			// Aspiration window failure handling: widen and retry
			if (iterBestScore <= aspAlpha && aspAlpha > -INFINITY_SCORE)
			{
				// Fail low: widen the window downwards
				aspAlpha = std::max(iterBestScore - aspDelta, -INFINITY_SCORE);
				aspDelta *= 2;
				continue;
			}
			if (iterBestScore >= aspBeta && aspBeta < INFINITY_SCORE)
			{
				// Fail high: widen the window upwards
				aspBeta = std::min(iterBestScore + aspDelta, INFINITY_SCORE);
				aspDelta *= 2;
				continue;
			}
			break; // Score inside the window - iteration complete
			} // end aspiration retry loop

			if (ShouldStop()) break;

			bestMoveSoFar = iterBestMove;
			bestScore = iterBestScore;

			// Emit UCI "info" line after each completed depth.
			if (m_infoCallback)
			{
				auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - m_searchStartTime).count();
				uint64_t nodes = m_nodesSearched.load(std::memory_order_relaxed);
				uint64_t nps = (elapsedMs > 0) ? (nodes * 1000ULL / static_cast<uint64_t>(elapsedMs)) : 0;

				// Mate window is [MATE_SCORE - 250, MATE_SCORE] for real mate
				// detection. Scores beyond MATE_SCORE are fail-soft artifacts
				// (NMP/RFP returning above the bound) — report as capped cp.
				const int reportScore = bestScore;
				std::ostringstream info;
				info << "info depth " << depth;
				if (reportScore >= MATE_SCORE - 250 && reportScore <= MATE_SCORE)
					info << " score mate " << ((MATE_SCORE - reportScore + 1) / 2);
				else if (reportScore <= -MATE_SCORE + 250 && reportScore >= -MATE_SCORE)
					info << " score mate -" << ((MATE_SCORE + reportScore + 1) / 2);
				else
				{
					// Cap absurd fail-soft scores so GUIs don't draw insane bars.
					int cp = reportScore;
					if (cp >  10000) cp =  10000;
					if (cp < -10000) cp = -10000;
					info << " score cp " << cp;
				}
				info << " nodes " << nodes
				     << " nps " << nps
				     << " time " << elapsedMs
				     << " pv " << bestMoveSoFar.ToUCI();
				m_infoCallback(info.str());
			}
		}

		// Stop helpers and wait until they are all asleep before returning -
		// the next search overwrites m_searchStartTime/m_maxSearchTimeMs and
		// restages the root, so no helper may still be running.
		m_abortSearch.store(true, std::memory_order_release);
		if (useHelpers)
			WaitHelpersIdle();

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
    int AIPlayer::AlphaBeta(Board& board, int depth, int alpha, int beta, int ply, Move excludedMove, int checkExtCount)
    {
        m_nodesSearched.fetch_add(1, std::memory_order_relaxed);

        // Abort flag is a cheap atomic load - checked every node for instant
        // UCI "stop" response. The steady_clock query is ~20-30ns, so the time
        // check runs only every 1024 nodes (bounded response-time jitter).
        if (m_abortSearch.load(std::memory_order_relaxed)) return 0;
        {
            static thread_local int s_timeCheckCounter = 0;
            if ((++s_timeCheckCounter & 1023) == 0 && ShouldStop()) return 0;
        }

        // Draw detection by repetition - check before TT probe
        // CountRepetitions includes the current position, so >= 2 means the
        // position occurred before (two-fold): score as draw inside the tree.
        if (ply > 0 && board.CountRepetitions() >= 2)
        {
            return 0; // Draw score
        }

        // Fifty-move rule: 100 half-moves without a capture or pawn move is a
        // draw. Without this the search happily shuffles pieces in won
        // positions until the arbiter (or GUI) calls the draw.
        if (ply > 0 && board.GetHalfMoveClock() >= 100)
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

        // Probe transposition table for cached result.
        // Probed before any eval-based pruning: a TT hit is much cheaper
        // than a static evaluation call.
        uint64_t zobristKey = board.GetZobristKey();
        Move ttMove;
        int ttScore = -INFINITY_SCORE;
        if (m_transpositionTable.Probe(zobristKey, depth, alpha, beta, ttScore, ttMove, ply))
        {
            return ttScore;
        }

        // Terminal depth reached - switch to quiescence search
        if (depth == 0)
        {
            return QuiescenceSearch(board, alpha, beta, ply, 0);
        }

        const PlayerColor sideToMove = board.GetCurrentPlayer();
        const bool sideInCheck = board.IsInCheck(sideToMove);

        // Static evaluation - computed once per node, shared by futility,
        // reverse futility and null-move pruning conditions.
        int staticEval = 0;
        if (!sideInCheck)
            staticEval = m_evaluator.Evaluate(board);

        // "Improving" flag: is our static eval better than two plies ago?
        // Non-improving nodes get pruned/reduced more aggressively - the
        // position is trending badly, late quiet moves rarely save it.
        static thread_local int t_evalStack[MAX_PLY + 2];
        if (ply < MAX_PLY)
            t_evalStack[ply] = sideInCheck
                ? ((ply >= 2) ? t_evalStack[ply - 2] : 0)
                : staticEval;
        const bool improving = !sideInCheck && ply >= 2 &&
                               staticEval > t_evalStack[ply - 2];

        // Futility + Reverse Futility Pruning.
        // Fail-soft returns: tighter info for aspiration window.
        if (m_difficulty > 6 && depth <= 4 && !sideInCheck)
        {
            int futilityMargin = 80 + 100 * depth;
            if (staticEval + futilityMargin <= alpha)
                return staticEval + futilityMargin;
            int rfpMargin = 120 * depth;
            if (depth <= 3 && staticEval - rfpMargin >= beta)
                return staticEval - rfpMargin;
        }

        const PlayerColor opponentColor = (sideToMove == PlayerColor::White)
            ? PlayerColor::Black : PlayerColor::White;

        // Null move pruning - disabled in endgames to avoid zugzwang errors
        // In positions with low material (phase < 64), zugzwang is common
        // and null-move can produce false beta cutoffs.
        // Gated on staticEval >= beta: if the position is already below beta,
        // giving the opponent a free move has no realistic chance to fail high.
        // Runs before move generation - a null-move cutoff skips it entirely.
        int phase = ComputePhase(board);
        if (m_difficulty > 6 && depth >= 3 && phase > 64 && !sideInCheck &&
            staticEval >= beta)
        {
            int R = 3 + depth / 4;
            if (R > depth - 1) R = depth - 1;

            board.MakeNullMoveUnchecked();
            m_transpositionTable.Prefetch(board.GetZobristKey());
            int score = -AlphaBeta(board, depth - 1 - R, -beta, -beta + 1, ply + 1, Move{}, checkExtCount);
            board.UndoNullMove();

            if (score >= beta)
            {
                // Return beta (fail-hard) here: a fail-soft return could be
                // INFINITY-bounded if the child also failed soft, polluting TT.
                return beta;
            }
        }

        const auto& castlingRights = board.GetCastlingRights();
        const auto& pieceList = board.GetPieceList(sideToMove);

        // ProbCut: if a shallow search of a CAPTURE exceeds beta by a large margin,
        // the position is likely a cutoff at full depth — prune immediately.
        // Only searches tactical moves (captures/promotions) — this is what makes it
        // different from Null Move and mathematically sound.
        if (m_difficulty > 7 && depth >= 5 && !sideInCheck &&
            std::abs(beta) < MATE_SCORE - 100)
        {
            const int PC_MARGIN = 200;
            int rbeta = std::min(beta + PC_MARGIN, INFINITY_SCORE - 1);
            int pcDepth = depth - 4;

            MoveList pcMoves = MoveGenerator::GenerateTacticalMoves(
                board.GetPieces(), sideToMove,
                board.GetEnPassantSquare(), &pieceList);

            for (const auto& pcMove : pcMoves)
            {
                if (SEE(board, pcMove) < 0) continue; // Skip losing captures

                m_evaluator.OnMakeMove();
                board.MakeMoveUnchecked(pcMove);

                // Lazy legality: skip moves that leave own king attacked
                int pcKingSq = board.GetKingSquare(sideToMove);
                if (pcKingSq == -1 || MoveGenerator::IsSquareAttacked(
                        board.GetPieces(), pcKingSq, opponentColor))
                {
                    board.UndoMove();
                    m_evaluator.OnUndoMove();
                    continue;
                }

                m_transpositionTable.Prefetch(board.GetZobristKey());
                int pcScore = -AlphaBeta(board, pcDepth, -rbeta, -rbeta + 1, ply + 1, Move{}, checkExtCount);
                board.UndoMove();
                m_evaluator.OnUndoMove();

                if (pcScore >= rbeta)
                    return pcScore;
            }
        }

        // Generate pseudo-legal moves; legality is verified lazily inside the
        // move loop (make + king-attack check). This avoids a full make/undo
        // pass over every move at every node - most nodes cut off after
        // searching only a few moves.
        MoveList moves = MoveGenerator::GeneratePseudoLegalMoves(
            board.GetPieces(),
            sideToMove,
            board.GetEnPassantSquare(),
            &castlingRights,
            &pieceList
        );

        // Internal Iterative Deepening - search to find good move ordering
        // When we have no hash move at high depths, do a reduced depth search
        // to populate the TT with a best move for better move ordering
        if (m_difficulty > 6 && depth >= 6 && ttMove.GetFrom() == ttMove.GetTo())
        {
            int iidDepth = depth - 2;
            AlphaBeta(board, iidDepth, alpha, beta, ply, Move{}, checkExtCount);
            m_transpositionTable.Probe(zobristKey, 0, alpha, beta, ttScore, ttMove, ply);
        }

        // Singular Extensions: verify that the TT move is the uniquely best move.
        // Do a reduced-depth search excluding the TT move with a lowered beta.
        // If no other move reaches singularBeta, the TT move is singular — extend it.
        bool singularExtension = false;
        if (m_difficulty > 7 && depth >= 6 && ply > 0 &&
            ttMove.IsValid() && !excludedMove.IsValid() &&
            std::abs(ttScore) < MATE_SCORE - 100)
        {
            int seTTScore = -INFINITY_SCORE;
            Move seTTMove;
            if (m_transpositionTable.ProbeSE(zobristKey, seTTScore, seTTMove) &&
                std::abs(seTTScore) < MATE_SCORE - 100)
            {
                int singularBeta  = seTTScore - 3 * depth;
                int singularDepth = (depth - 1) / 2;
                int singularScore = AlphaBeta(board, singularDepth,
                                              singularBeta - 1, singularBeta, ply, ttMove, checkExtCount);
                if (singularScore < singularBeta)
                    singularExtension = true;
            }
        }

        const int sideIndex = static_cast<int>(sideToMove);

        // Staged picking: cheap scores now, SEE lazily at pick time.
        // Most nodes cut off after 1-3 moves and never pay for the rest.
        std::array<int, 256> moveScores;
        ScoreMovesCheap(moves, moveScores.data(), board, ttMove, ply,
                        m_killerMoves[ply][0], m_killerMoves[ply][1]);

        Move bestMove;
        int bestScore = -INFINITY_SCORE;
        uint8_t flag = TT_ALPHA;

        // Check extension - search deeper when in check to find escape sequences.
        // Capped by MAX_CHECK_EXTENSIONS so an unbroken check sequence (king hunts)
        // can't keep `depth` from decreasing all the way out to MAX_PLY.
        bool checkExtended = sideInCheck && ply < MAX_PLY - 1 && checkExtCount < MAX_CHECK_EXTENSIONS;
        if (checkExtended)
        {
            depth++;
        }
        int nextCheckExtCount = checkExtCount + (checkExtended ? 1 : 0);

        int moveIndex = 0;
        int searchedMoves = 0;

        // Clear killers two plies ahead: grandchild nodes of this position
        // will see fresh slots instead of stale moves from sibling subtrees
        if (ply + 2 < MAX_PLY)
        {
            m_killerMoves[ply + 2][0] = Move();
            m_killerMoves[ply + 2][1] = Move();
        }

        // Quiet moves already searched at this node - penalized in the history
        // table when a later quiet move causes a beta cutoff (history malus).
        std::array<Move, 64> triedQuiets;
        int triedQuietCount = 0;

        // Search moves in lazily-picked order (best remaining first)
        for (int mi = 0; PickNextMove(moves, moveScores.data(), mi, board); ++mi)
        {
            const Move move = moves[mi];

            // Skip excluded move (used during Singular Extensions reduced search)
            if (excludedMove.IsValid() && move == excludedMove)
                continue;

            // Check if move is quiet (not tactical)
            bool isQuiet = !move.IsCapture() &&
                           !move.IsPromotion() &&
                           !move.IsEnPassant() &&
                           !move.IsCastling();

            // Late Move Pruning - skip late quiet moves at moderate depths
            // After searching many moves, remaining quiet moves are unlikely to improve score
            // Only apply when not at root, depth is moderate, and at least one
            // legal move has been searched (mate detection stays sound).
            // Non-improving nodes prune roughly twice as early.
            if (ply > 0 &&
                depth >= 3 && depth <= 7 &&
                moveIndex >= (4 + depth * depth / 2) &&
                !sideInCheck &&
                searchedMoves > 0 &&
                isQuiet)
            {
                moveIndex++;
                continue;
            }

            // SEE pruning - skip clearly losing captures at shallow depth.
            // The material loss is too large for the remaining depth to recover.
            if (m_difficulty > 6 && ply > 0 && depth <= 5 && !sideInCheck &&
                searchedMoves > 0 &&
                move.IsCapture() && !move.IsPromotion() &&
                bestScore > -MATE_SCORE + 1000 &&
                SEE(board, move) < -120 * depth)
            {
                moveIndex++;
                continue;
            }

            // Singular Extension: extend the TT move by 1 ply when it's proven singular
            int extension = (singularExtension && ttMove.IsValid() && move == ttMove) ? 1 : 0;

            m_evaluator.OnMakeMove();
            board.MakeMoveUnchecked(move);

            // Lazy legality check: pseudo-legal move leaving own king attacked
            // is illegal - undo and skip without counting it
            {
                int kingSq = board.GetKingSquare(sideToMove);
                if (kingSq == -1 || MoveGenerator::IsSquareAttacked(
                        board.GetPieces(), kingSq, opponentColor))
                {
                    board.UndoMove();
                    m_evaluator.OnUndoMove();
                    continue;
                }
            }

            // Prefetch the child's TT bucket - by the time the recursive call
            // probes it, the cache line is already in flight (HFT-style latency hiding)
            m_transpositionTable.Prefetch(board.GetZobristKey());

            int score;

            // Check if move gives check to opponent
            bool givesCheck = board.IsInCheck(opponentColor);

            // Late Move Reduction - reduce search depth for likely poor moves
            // Moves ordered later are less likely to be good, so search them at reduced depth
            // Don't reduce moves that give check, the extended singular move, or other forcing moves
            bool applyLMR = depth >= 3 &&
                            moveIndex >= 4 &&
                            !sideInCheck &&
                            !givesCheck &&
                            extension == 0 &&
                            isQuiet;

            if (searchedMoves == 0)
            {
                // First move: full window (the PV candidate)
                score = -AlphaBeta(board, depth - 1 + extension, -beta, -alpha, ply + 1,
                                    Move{}, nextCheckExtCount);
            }
            else
            {
                // PVS: remaining moves get a null-window scout search first,
                // optionally reduced by LMR.
                int lmrReduction = 0;
                if (applyLMR)
                {
                    // Logarithmic LMR formula — aggressive for deep/late moves, conservative early
                    lmrReduction = std::max(1, (int)(std::log((double)depth) *
                                                     std::log((double)(moveIndex + 1)) / 2.25));
                    if (lmrReduction >= depth) lmrReduction = depth - 1;
                }

                score = -AlphaBeta(board, depth - 1 - lmrReduction, -alpha - 1, -alpha, ply + 1,
                                    Move{}, nextCheckExtCount);

                // LMR scout failed high: verify at full depth, still null window
                if (lmrReduction > 0 && score > alpha)
                {
                    score = -AlphaBeta(board, depth - 1 + extension, -alpha - 1, -alpha, ply + 1,
                                        Move{}, nextCheckExtCount);
                }

                // Scout beat alpha inside an open window: full-window re-search
                if (score > alpha && score < beta)
                {
                    score = -AlphaBeta(board, depth - 1 + extension, -beta, -alpha, ply + 1,
                                        Move{}, nextCheckExtCount);
                }
            }

            board.UndoMove();
            m_evaluator.OnUndoMove();

            searchedMoves++;

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

                    // History malus: quiets searched before the cutoff move were
                    // ordered too high - push them down for future ordering.
                    for (int q = 0; q < triedQuietCount; ++q)
                    {
                        m_history[sideIndex][triedQuiets[q].GetFrom()][triedQuiets[q].GetTo()]
                            .fetch_sub(depth * depth, std::memory_order_relaxed);
                    }

                    // Store killer move
                    if (ply < MAX_PLY)
                    {
                        m_killerMoves[ply][1] = m_killerMoves[ply][0];
                        m_killerMoves[ply][0] = move;
                    }

                    // Counter move + continuation history, keyed by the
                    // opponent's previous move (board is at this node's state,
                    // so the last record is exactly that move)
                    if (board.GetHistoryPly() > 0)
                    {
                        const auto& lastRec = board.GetLastMoveRecord();
                        int prevSide = 1 - sideIndex;
                        m_counterMoves[prevSide][lastRec.move.GetFrom()][lastRec.move.GetTo()] = move;

                        if (!lastRec.movedPiece.IsEmpty())
                        {
                            const int prevPc = PieceCode(lastRec.movedPiece);
                            const int prevTo = lastRec.move.GetTo();

                            Piece mp = board.GetPieceAt(move.GetFrom());
                            if (!mp.IsEmpty())
                                m_contHist[ContHistIndex(prevPc, prevTo, PieceCode(mp), move.GetTo())]
                                    .fetch_add(depth * depth, std::memory_order_relaxed);

                            // Continuation-history malus for earlier quiets
                            for (int q = 0; q < triedQuietCount; ++q)
                            {
                                Piece qp = board.GetPieceAt(triedQuiets[q].GetFrom());
                                if (!qp.IsEmpty())
                                    m_contHist[ContHistIndex(prevPc, prevTo, PieceCode(qp), triedQuiets[q].GetTo())]
                                        .fetch_sub(depth * depth, std::memory_order_relaxed);
                            }
                        }
                    }
                }
                // Store beta as the proven lower bound; fail-hard return avoids
                // INFINITY contamination in the TT.
                m_transpositionTable.Store(zobristKey, depth, beta, TT_BETA, bestMove, ply);
                return beta;
            }

            // Record searched quiet move for potential history malus later
            if (isQuiet && triedQuietCount < (int)triedQuiets.size())
            {
                triedQuiets[triedQuietCount++] = move;
            }

            // Alpha improvement - new best move found
            if (score > alpha)
            {
                alpha = score;
                flag = TT_EXACT;
            }

            moveIndex++;
        }

        // No legal move was searched: checkmate or stalemate
        if (searchedMoves == 0)
        {
            if (excludedMove.IsValid())
                return alpha; // Singular verification: only the excluded move is legal
            return sideInCheck ? (-MATE_SCORE + ply) : 0;
        }

        m_transpositionTable.Store(zobristKey, depth, bestScore, flag, bestMove, ply);
        return bestScore;
    }

    // Quiescence search - search only tactical moves to avoid horizon effect
    int AIPlayer::QuiescenceSearch(Board& board, int alpha, int beta, int ply, int qDepth)
    {
        m_nodesSearched.fetch_add(1, std::memory_order_relaxed);

        // Check abort/time (gated clock query) and depth limits
        if (m_abortSearch.load(std::memory_order_relaxed) || qDepth >= 8)
        {
            return m_evaluator.Evaluate(board);
        }
        {
            static thread_local int s_timeCheckCounter = 0;
            if ((++s_timeCheckCounter & 1023) == 0 && ShouldStop())
                return m_evaluator.Evaluate(board);
        }

        // TT probe at depth 0 - quiescence positions repeat massively across
        // the tree (same capture sequences reached in different orders), and
        // the TT move doubles as a free ordering hint.
        uint64_t zobristKey = board.GetZobristKey();
        Move ttMove;
        int ttScore = -INFINITY_SCORE;
        if (m_transpositionTable.Probe(zobristKey, 0, alpha, beta, ttScore, ttMove, ply))
        {
            return ttScore;
        }

        const PlayerColor sideToMove = board.GetCurrentPlayer();

        // If in check, we must search all evasions (stand-pat is illegal in check)
        if (board.IsInCheck(sideToMove))
        {
            const PlayerColor opponentColor = (sideToMove == PlayerColor::White)
                ? PlayerColor::Black : PlayerColor::White;

            const auto& castlingRights = board.GetCastlingRights();
            const auto& pieceList = board.GetPieceList(sideToMove);

            MoveList evasions = MoveGenerator::GeneratePseudoLegalMoves(
                board.GetPieces(),
                sideToMove,
                board.GetEnPassantSquare(),
                &castlingRights,
                &pieceList
            );

            std::array<int, 256> evasionScores;
            ScoreMovesCheap(evasions, evasionScores.data(), board, ttMove, ply,
                            m_killerMoves[ply][0], m_killerMoves[ply][1]);

            int best = -INFINITY_SCORE;
            int legalCount = 0;

            for (int mi = 0; PickNextMove(evasions, evasionScores.data(), mi, board); ++mi)
            {
                const Move move = evasions[mi];
                m_evaluator.OnMakeMove();
                board.MakeMoveUnchecked(move);

                // Lazy legality: skip evasions that leave the king attacked
                int kingSq = board.GetKingSquare(sideToMove);
                if (kingSq == -1 || MoveGenerator::IsSquareAttacked(
                        board.GetPieces(), kingSq, opponentColor))
                {
                    board.UndoMove();
                    m_evaluator.OnUndoMove();
                    continue;
                }
                legalCount++;

                int score = -QuiescenceSearch(board, -beta, -alpha, ply + 1, qDepth + 1);

                board.UndoMove();
                m_evaluator.OnUndoMove();

                if (score > best) best = score;

                if (score >= beta)
                {
                    m_transpositionTable.Store(zobristKey, 0, beta, TT_BETA, move, ply);
                    return beta; // Beta cutoff
                }
                if (score > alpha)
                {
                    alpha = score; // Alpha improvement
                }
            }

            if (legalCount == 0)
            {
                return -MATE_SCORE + ply; // Checkmated
            }

            m_transpositionTable.Store(zobristKey, 0, alpha, TT_ALPHA, Move(), ply);
            return alpha;
        }

        // Not in check: normal stand-pat logic
        int standPat = m_evaluator.Evaluate(board);

        if (standPat >= beta)
        {
            m_transpositionTable.Store(zobristKey, 0, beta, TT_BETA, Move(), ply);
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

        MoveList tacticalMoves = MoveGenerator::GenerateTacticalMoves(
            board.GetPieces(),
            board.GetSideToMove(),
            board.GetEnPassantSquare(),
            &board.GetPieceList(board.GetSideToMove())
        );

        PlayerColor movedColor = board.GetSideToMove();
        PlayerColor opponentColor = (movedColor == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White;

        std::array<int, 256> tacticalScores;
        ScoreMovesCheap(tacticalMoves, tacticalScores.data(), board, ttMove, ply,
                        Move(), Move());

        for (int mi = 0; PickNextMove(tacticalMoves, tacticalScores.data(), mi, board); ++mi)
        {
            const Move move = tacticalMoves[mi];

            // The picker demotes SEE-losing captures below zero; once the best
            // remaining move is a losing capture, everything left is prunable
            // (promotions never get demoted, so they are searched first)
            if (tacticalScores[mi] < -100000)
            {
                break;
            }

            m_evaluator.OnMakeMove();
            board.MakeMoveUnchecked(move);

            // Lazy legality: skip moves that leave own king attacked
            int kingSq = board.GetKingSquare(movedColor);
            if (kingSq == -1 || MoveGenerator::IsSquareAttacked(
                    board.GetPieces(), kingSq, opponentColor))
            {
                board.UndoMove();
                m_evaluator.OnUndoMove();
                continue;
            }

            int score = -QuiescenceSearch(board, -beta, -alpha, ply + 1, qDepth + 1);

            board.UndoMove();
            m_evaluator.OnUndoMove();

            if (score >= beta)
            {
                m_transpositionTable.Store(zobristKey, 0, beta, TT_BETA, move, ply);
                return beta;
            }
            if (score > alpha)
            {
                alpha = score;
            }
        }

        m_transpositionTable.Store(zobristKey, 0, alpha, TT_ALPHA, Move(), ply);
        return alpha;
    }
}
