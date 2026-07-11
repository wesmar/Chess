// SearchWorker.cpp
// AIPlayer worker-thread search used by the root-parallel scheme.
// Mirrors the main-thread algorithms in Search.cpp but uses thread-local
// killer tables and the shared lock-free transposition table.

#define NOMINMAX
#include "Search.h"
#include "Evaluation.h"
#include "MoveGenerator.h"
#include <algorithm>
#include <cmath>

namespace Chess
{
	// Worker thread alpha-beta search - uses thread-local heuristics to avoid data races
    int AIPlayer::WorkerAlphaBeta(Board& board, int depth, int alpha, int beta, int ply,
                                   ThreadLocalData& tld, Move excludedMove, int checkExtCount)
    {
        m_nodesSearched.fetch_add(1, std::memory_order_relaxed);

        // Abort flag checked every node (cheap atomic load); clock query
        // gated to every 1024 nodes - same scheme as the main thread.
        if (m_abortSearch.load(std::memory_order_relaxed)) return 0;
        static thread_local int nodeCounter = 0;
        if ((++nodeCounter & 1023) == 0)
        {
            if (ShouldStop()) return 0;
        }

        // Draw detection by repetition (two-fold inside the search tree)
        if (ply > 0 && board.CountRepetitions() >= 2)
        {
            return 0;
        }

        // Fifty-move rule draw (see main-thread comment)
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

        // Probe TT before any eval-based pruning (hit is cheaper than Evaluate).
        uint64_t zobristKey = board.GetZobristKey();
        Move ttMove;
        int ttScore = -INFINITY_SCORE;
        if (m_transpositionTable.Probe(zobristKey, depth, alpha, beta, ttScore, ttMove, ply))
        {
            return ttScore;
        }

        if (depth == 0)
        {
            return WorkerQuiescence(board, alpha, beta, ply, 0, tld);
        }

        const PlayerColor sideToMove = board.GetCurrentPlayer();
        const bool sideInCheck = board.IsInCheck(sideToMove);

        // Static evaluation shared by futility, RFP and NMP conditions.
        int staticEval = 0;
        if (!sideInCheck)
            staticEval = Chess::Evaluate(board);

        // Futility + Reverse Futility Pruning (same as main thread). Fail-soft returns.
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

        // Null move pruning - disabled in endgames to avoid zugzwang errors.
        // Gated on staticEval >= beta (see main-thread comment).
        // Runs before move generation - a null-move cutoff skips it entirely.
        int phase = ComputePhase(board);
        if (m_difficulty > 6 && depth >= 3 && phase > 64 && !sideInCheck &&
            staticEval >= beta)
        {
            int R = 3 + depth / 4;
            if (R > depth - 1) R = depth - 1;

            board.MakeNullMoveUnchecked();
            m_transpositionTable.Prefetch(board.GetZobristKey());
            int score = -WorkerAlphaBeta(board, depth - 1 - R, -beta, -beta + 1, ply + 1, tld, Move{}, checkExtCount);
            board.UndoNullMove();

            if (score >= beta)
            {
                return beta;  // fail-hard (see main thread comment)
            }
        }

        const auto& castlingRights = board.GetCastlingRights();
        const auto& pieceList = board.GetPieceList(sideToMove);

        // ProbCut (worker thread version)
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
                if (SEE(board, pcMove) < 0) continue;

                board.MakeMoveUnchecked(pcMove);

                // Lazy legality: skip moves that leave own king attacked
                int pcKingSq = board.GetKingSquare(sideToMove);
                if (pcKingSq == -1 || MoveGenerator::IsSquareAttacked(
                        board.GetPieces(), pcKingSq, opponentColor))
                {
                    board.UndoMove();
                    continue;
                }

                m_transpositionTable.Prefetch(board.GetZobristKey());
                int pcScore = -WorkerAlphaBeta(board, pcDepth, -rbeta, -rbeta + 1, ply + 1, tld, Move{}, checkExtCount);
                board.UndoMove();

                if (pcScore >= rbeta)
                    return pcScore;
            }
        }

        // Generate pseudo-legal moves; legality verified lazily in the move loop
        MoveList moves = MoveGenerator::GeneratePseudoLegalMoves(
            board.GetPieces(),
            sideToMove,
            board.GetEnPassantSquare(),
            &castlingRights,
            &pieceList
        );

        // Internal Iterative Deepening - search to find good move ordering
        if (m_difficulty > 6 && depth >= 6 && ttMove.GetFrom() == ttMove.GetTo())
        {
            int iidDepth = depth - 2;
            WorkerAlphaBeta(board, iidDepth, alpha, beta, ply, tld, Move{}, checkExtCount);
            m_transpositionTable.Probe(zobristKey, 0, alpha, beta, ttScore, ttMove, ply);
        }

        // Singular Extensions (worker thread version — uses Chess::Evaluate reference)
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
                int singularScore = WorkerAlphaBeta(board, singularDepth,
                                                    singularBeta - 1, singularBeta, ply, tld, ttMove, checkExtCount);
                if (singularScore < singularBeta)
                    singularExtension = true;
            }
        }

        const int sideIndex = static_cast<int>(sideToMove);

        // Staged picking with thread-local killers (see main-thread comment)
        std::array<int, 256> moveScores;
        ScoreMovesCheap(moves, moveScores.data(), board, ttMove, ply,
                        (ply < MAX_PLY) ? tld.killerMoves[ply][0] : Move(),
                        (ply < MAX_PLY) ? tld.killerMoves[ply][1] : Move());

        Move bestMove;
        int bestScore = -INFINITY_SCORE;
        uint8_t flag = TT_ALPHA;

        // Capped like the main-thread search - see MAX_CHECK_EXTENSIONS comment in ChessGame.h.
        bool checkExtended = sideInCheck && ply < MAX_PLY - 1 && checkExtCount < MAX_CHECK_EXTENSIONS;
        if (checkExtended)
        {
            depth++;
        }
        int nextCheckExtCount = checkExtCount + (checkExtended ? 1 : 0);

        int moveIndex = 0;
        int searchedMoves = 0;

        // Quiet moves already searched at this node (history malus on cutoff)
        std::array<Move, 64> triedQuiets;
        int triedQuietCount = 0;

        for (int mi = 0; PickNextMove(moves, moveScores.data(), mi, board); ++mi)
        {
            const Move move = moves[mi];

            if ((moveIndex & 15) == 0 && ShouldStop()) break;

            // Skip excluded move (used during Singular Extensions reduced search)
            if (excludedMove.IsValid() && move == excludedMove)
                continue;

            bool isQuiet = !move.IsCapture() && !move.IsPromotion() &&
                           !move.IsEnPassant() && !move.IsCastling();

            // Late Move Pruning - skip late quiet moves at moderate depths
            // Only apply when not at root, depth is high enough, and at least
            // one legal move has been searched (mate detection stays sound)
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

            // SEE pruning - skip clearly losing captures at shallow depth
            if (m_difficulty > 6 && ply > 0 && depth <= 5 && !sideInCheck &&
                searchedMoves > 0 &&
                move.IsCapture() && !move.IsPromotion() &&
                bestScore > -MATE_SCORE + 1000 &&
                SEE(board, move) < -120 * depth)
            {
                moveIndex++;
                continue;
            }

            // Singular Extension: extend the TT move by 1 ply when proven singular
            int extension = (singularExtension && ttMove.IsValid() && move == ttMove) ? 1 : 0;

            board.MakeMoveUnchecked(move);

            // Lazy legality check (see main-thread comment)
            {
                int kingSq = board.GetKingSquare(sideToMove);
                if (kingSq == -1 || MoveGenerator::IsSquareAttacked(
                        board.GetPieces(), kingSq, opponentColor))
                {
                    board.UndoMove();
                    continue;
                }
            }

            // Prefetch the child's TT bucket before recursing
            m_transpositionTable.Prefetch(board.GetZobristKey());

            int score;

            // Check if move gives check to opponent
            bool givesCheck = board.IsInCheck(opponentColor);

            bool applyLMR = depth >= 3 && moveIndex >= 4 && !sideInCheck && !givesCheck &&
                            extension == 0 && isQuiet;

            if (searchedMoves == 0)
            {
                // First move: full window (the PV candidate)
                score = -WorkerAlphaBeta(board, depth - 1 + extension, -beta, -alpha, ply + 1, tld,
                                          Move{}, nextCheckExtCount);
            }
            else
            {
                // PVS: null-window scout, optionally reduced by LMR
                int lmrReduction = 0;
                if (applyLMR)
                {
                    // Logarithmic LMR formula
                    lmrReduction = std::max(1, (int)(std::log((double)depth) *
                                                     std::log((double)(moveIndex + 1)) / 2.25));
                    if (lmrReduction >= depth) lmrReduction = depth - 1;
                }

                score = -WorkerAlphaBeta(board, depth - 1 - lmrReduction, -alpha - 1, -alpha, ply + 1, tld,
                                          Move{}, nextCheckExtCount);

                // LMR scout failed high: verify at full depth, still null window
                if (lmrReduction > 0 && score > alpha)
                {
                    score = -WorkerAlphaBeta(board, depth - 1 + extension, -alpha - 1, -alpha, ply + 1, tld,
                                              Move{}, nextCheckExtCount);
                }

                // Scout beat alpha inside an open window: full-window re-search
                if (score > alpha && score < beta)
                {
                    score = -WorkerAlphaBeta(board, depth - 1 + extension, -beta, -alpha, ply + 1, tld,
                                              Move{}, nextCheckExtCount);
                }
            }

            board.UndoMove();

            searchedMoves++;

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

                    // History malus for quiets searched before the cutoff move
                    for (int q = 0; q < triedQuietCount; ++q)
                    {
                        m_history[sideIndex][triedQuiets[q].GetFrom()][triedQuiets[q].GetTo()]
                            .fetch_sub(depth * depth, std::memory_order_relaxed);
                    }

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
                return beta;  // fail-hard (see main thread comment)
            }

            // Record searched quiet move for potential history malus later
            if (isQuiet && triedQuietCount < (int)triedQuiets.size())
            {
                triedQuiets[triedQuietCount++] = move;
            }

            if (score > alpha)
            {
                alpha = score;
                flag = TT_EXACT;
            }

            moveIndex++;
        }

        // Aborted mid-loop: result is discarded anyway, don't pollute the TT
        // and never conclude mate from a partially-searched move list
        if (ShouldStop()) return 0;

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

    // Worker thread quiescence search - handles only tactical moves
    int AIPlayer::WorkerQuiescence(Board& board, int alpha, int beta, int ply, int qDepth,
                                    ThreadLocalData& tld)
    {
        m_nodesSearched.fetch_add(1, std::memory_order_relaxed);

        // Worker threads use classical evaluation for thread safety.
        // Abort flag every node, clock query gated to every 1024 nodes.
        if (m_abortSearch.load(std::memory_order_relaxed) || qDepth >= 8)
        {
            return Chess::Evaluate(board);
        }
        {
            static thread_local int s_timeCheckCounter = 0;
            if ((++s_timeCheckCounter & 1023) == 0 && ShouldStop())
                return Chess::Evaluate(board);
        }

        // TT probe at depth 0 (see main-thread comment)
        uint64_t zobristKey = board.GetZobristKey();
        Move ttMove;
        int ttScore = -INFINITY_SCORE;
        if (m_transpositionTable.Probe(zobristKey, 0, alpha, beta, ttScore, ttMove, ply))
        {
            return ttScore;
        }

        const PlayerColor sideToMove = board.GetCurrentPlayer();

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
            ScoreMovesCheap(evasions, evasionScores.data(), board, Move(), ply,
                            (ply < MAX_PLY) ? tld.killerMoves[ply][0] : Move(),
                            (ply < MAX_PLY) ? tld.killerMoves[ply][1] : Move());

            int best = -INFINITY_SCORE;
            int legalCount = 0;

            for (int mi = 0; PickNextMove(evasions, evasionScores.data(), mi, board); ++mi)
            {
                const Move move = evasions[mi];
                board.MakeMoveUnchecked(move);

                // Lazy legality: skip evasions that leave the king attacked
                int kingSq = board.GetKingSquare(sideToMove);
                if (kingSq == -1 || MoveGenerator::IsSquareAttacked(
                        board.GetPieces(), kingSq, opponentColor))
                {
                    board.UndoMove();
                    continue;
                }
                legalCount++;

                int score = -WorkerQuiescence(board, -beta, -alpha, ply + 1, qDepth + 1, tld);
                board.UndoMove();

                if (score > best) best = score;
                if (score >= beta)
                {
                    m_transpositionTable.Store(zobristKey, 0, beta, TT_BETA, move, ply);
                    return beta;
                }
                if (score > alpha) alpha = score;
            }

            if (legalCount == 0)
            {
                return -MATE_SCORE + ply;
            }

            m_transpositionTable.Store(zobristKey, 0, alpha, TT_ALPHA, Move(), ply);
            return alpha;
        }

        // Worker threads use classical evaluation for thread safety
        int standPat = Chess::Evaluate(board);
        if (standPat >= beta)
        {
            m_transpositionTable.Store(zobristKey, 0, beta, TT_BETA, Move(), ply);
            return beta;
        }
        if (standPat > alpha) alpha = standPat;

        // Delta pruning: if position is too bad, even best possible capture won't help
        // Use queen value (900) + margin (200) for safety (accounts for promotions)
        const int QUEEN_VALUE = 900;
        const int DELTA_MARGIN = 200;
        if (standPat + QUEEN_VALUE + DELTA_MARGIN < alpha)
        {
            return alpha; // Position hopeless, skip tactical search
        }

        // Generate tactical moves; legality verified lazily in the loop
        MoveList tacticalMoves = MoveGenerator::GenerateTacticalMoves(
            board.GetPieces(),
            sideToMove,
            board.GetEnPassantSquare(),
            &board.GetPieceList(sideToMove)
        );

        const PlayerColor opponentColor = (sideToMove == PlayerColor::White)
            ? PlayerColor::Black : PlayerColor::White;

        std::array<int, 256> tacticalScores;
        ScoreMovesCheap(tacticalMoves, tacticalScores.data(), board, ttMove, ply,
                        Move(), Move());

        for (int mi = 0; PickNextMove(tacticalMoves, tacticalScores.data(), mi, board); ++mi)
        {
            const Move move = tacticalMoves[mi];

            // Losing captures are demoted by the picker; once the best
            // remaining move is one, everything left is prunable
            if (tacticalScores[mi] < -100000)
            {
                break;
            }

            board.MakeMoveUnchecked(move);

            // Lazy legality: skip moves that leave own king attacked
            int kingSq = board.GetKingSquare(sideToMove);
            if (kingSq == -1 || MoveGenerator::IsSquareAttacked(
                    board.GetPieces(), kingSq, opponentColor))
            {
                board.UndoMove();
                continue;
            }

            int score = -WorkerQuiescence(board, -beta, -alpha, ply + 1, qDepth + 1, tld);
            board.UndoMove();

            if (score >= beta)
            {
                m_transpositionTable.Store(zobristKey, 0, beta, TT_BETA, move, ply);
                return beta;
            }
            if (score > alpha) alpha = score;
        }

        m_transpositionTable.Store(zobristKey, 0, alpha, TT_ALPHA, Move(), ply);
        return alpha;
    }
}
