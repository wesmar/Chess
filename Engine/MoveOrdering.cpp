// MoveOrdering.cpp
// Move scoring and ordering heuristics (TT move, MVV-LVA + SEE, killers,
// countermoves, history) plus Static Exchange Evaluation.
// Good ordering maximizes beta cutoffs - the single most important factor
// for alpha-beta efficiency.

#define NOMINMAX
#include "Search.h"
#include "Evaluation.h"
#include "MoveGenerator.h"
#include <algorithm>
#include <climits>

namespace Chess
{
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

            int seeValue = SEE(board, move);
            if (seeValue < 0)
            {
                // Losing captures go BELOW killers and quiet moves so we don't
                // waste a full search window on them before trying killer heuristics
                return -200000 + victimValue * 10 - aggressorValue;
            }

            // Winning/equal captures: MVV-LVA base + SEE gain
            return 1000000 + victimValue * 10 - aggressorValue + seeValue;
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
	// Sort moves by score for better alpha-beta pruning efficiency.
    // Stack-allocated scoring buffer — no heap alloc in hot path.
    void AIPlayer::OrderMoves(MoveList& moves, const Board& board, Move ttMove, int ply)
    {
        struct Scored { int score; Move move; };
        std::array<Scored, 256> scored;

        const int n = moves.size();
        for (int i = 0; i < n; ++i)
        {
            scored[i] = { ScoreMove(moves[i], board, ttMove, ply), moves[i] };
        }

        std::sort(scored.begin(), scored.begin() + n,
            [](const Scored& a, const Scored& b) { return a.score > b.score; });

        for (int i = 0; i < n; ++i)
        {
            moves[i] = scored[i].move;
        }
    }

    // Move ordering for worker threads using thread-local data
    void AIPlayer::OrderMovesWorker(MoveList& moves, const Board& board, Move ttMove,
                                     int ply, const ThreadLocalData& tld)
    {
        struct Scored { int score; Move move; };
        std::array<Scored, 256> scored;

        const int n = moves.size();
        for (int i = 0; i < n; ++i)
        {
            scored[i] = { ScoreMoveWorker(moves[i], board, ttMove, ply, tld), moves[i] };
        }

        std::sort(scored.begin(), scored.begin() + n,
            [](const Scored& a, const Scored& b) { return a.score > b.score; });

        for (int i = 0; i < n; ++i)
        {
            moves[i] = scored[i].move;
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

            int seeValue = SEE(board, move);
            if (seeValue < 0)
            {
                // Losing captures go BELOW killers so killer heuristics are tried first
                return -200000 + victimValue * 10 - aggressorValue;
            }

            // Winning/equal captures: MVV-LVA base + SEE gain
            return 1000000 + victimValue * 10 - aggressorValue + seeValue;
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
        struct Scored { int score; Move move; };
        std::array<Scored, 256> scored;

        const int n = moves.size();
        for (int i = 0; i < n; ++i)
        {
            const Move& move = moves[i];
            int score = 0;

            if (move == ttMove)
            {
                score = 10000000;
            }
            else if (move.IsCapture())
            {
                Piece victim = move.GetCaptured();
                Piece aggressor = board.GetPieceAt(move.GetFrom());
                int victimValue = PIECE_VALUES[static_cast<int>(victim.GetType())];
                int aggressorValue = PIECE_VALUES[static_cast<int>(aggressor.GetType())];
                score = 1000000 + victimValue * 10 - aggressorValue;

                int seeValue = SEE(board, move);
                if (seeValue < 0) score -= 100000;
                else              score += seeValue;
            }
            else if (move.IsPromotion())
            {
                score = 900000;
            }

            scored[i] = { score, move };
        }

        std::sort(scored.begin(), scored.begin() + n,
            [](const Scored& a, const Scored& b) { return a.score > b.score; });

        for (int i = 0; i < n; ++i)
        {
            moves[i] = scored[i].move;
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

    // ========== STAGED MOVE PICKING ==========

    // Cheap move scoring - identical priorities to ScoreMove() except SEE:
    // every capture starts in the MVV-LVA band and is verified lazily by
    // PickNextMove() only when it is actually about to be searched.
    void AIPlayer::ScoreMovesCheap(const MoveList& moves, int* scores, const Board& board,
                                   Move ttMove, int ply, Move killer0, Move killer1) const
    {
        // Previous move context for the countermove heuristic (loop-invariant)
        int prevFrom = -1, prevTo = -1;
        if (ply > 0 && board.GetHistoryPly() > 0)
        {
            const auto& lastRecord = board.GetLastMoveRecord();
            prevFrom = lastRecord.move.GetFrom();
            prevTo = lastRecord.move.GetTo();
        }

        const int n = moves.size();
        for (int i = 0; i < n; ++i)
        {
            const Move& move = moves[i];

            // Highest priority: TT move
            if (move == ttMove)
            {
                scores[i] = 10000000;
                continue;
            }

            // Captures: MVV-LVA band; SEE verification deferred to pick time
            if (move.IsCapture())
            {
                int victimValue = PIECE_VALUES[static_cast<int>(move.GetCaptured().GetType())];
                int aggressorValue = PIECE_VALUES[static_cast<int>(board.GetPieceAt(move.GetFrom()).GetType())];
                scores[i] = 1000000 + victimValue * 10 - aggressorValue;
                continue;
            }

            if (move.IsPromotion())
            {
                scores[i] = 900000;
                continue;
            }

            // Killer moves (caller passes main-thread or thread-local killers)
            if (move == killer0) { scores[i] = 800000; continue; }
            if (move == killer1) { scores[i] = 700000; continue; }

            const int sideIndex = static_cast<int>(board.GetPieceAt(move.GetFrom()).GetColor());

            // Countermove heuristic
            if (prevFrom >= 0)
            {
                int oppSide = 1 - sideIndex;
                if (move == m_counterMoves[oppSide][prevFrom][prevTo])
                {
                    scores[i] = 600000;
                    continue;
                }
            }

            // History heuristic + central-square bonus
            int historyScore = m_history[sideIndex][move.GetFrom()][move.GetTo()].load(std::memory_order_relaxed);

            int toSquare = move.GetTo();
            int centerBonus = 0;
            if (toSquare == 27 || toSquare == 28 || toSquare == 35 || toSquare == 36)
                centerBonus = 400;
            else if ((toSquare >= 18 && toSquare <= 21) || (toSquare >= 42 && toSquare <= 45))
                centerBonus = 150;

            scores[i] = historyScore + centerBonus;
        }
    }

    // Selection-pick the best remaining move into position `start`.
    // Captures in the MVV-LVA band get their SEE computed here, exactly once,
    // and only if they reach the front of the queue - a beta cutoff two moves
    // earlier means their SEE is never paid at all. Losing captures are
    // demoted below quiet moves and re-picked later (or never).
    bool AIPlayer::PickNextMove(MoveList& moves, int* scores, int start, const Board& board) const
    {
        const int n = moves.size();

        while (true)
        {
            int bestIdx = -1;
            int bestScore = INT_MIN;
            for (int i = start; i < n; ++i)
            {
                if (scores[i] > bestScore)
                {
                    bestScore = scores[i];
                    bestIdx = i;
                }
            }

            if (bestIdx < 0)
                return false;

            // Lazy SEE: first touch of a capture in the MVV-LVA band
            if (bestScore >= 1000000 && bestScore < 10000000 && moves[bestIdx].IsCapture())
            {
                if (SEE(board, moves[bestIdx]) < 0)
                {
                    // Losing capture: demote below quiets, keep MVV-LVA order
                    // within the losing band, and pick again
                    scores[bestIdx] = bestScore - 1000000 - 200000;
                    continue;
                }
            }

            if (bestIdx != start)
            {
                Move tmpMove = moves[start];
                moves[start] = moves[bestIdx];
                moves[bestIdx] = tmpMove;
                int tmpScore = scores[start];
                scores[start] = scores[bestIdx];
                scores[bestIdx] = tmpScore;
            }
            return true;
        }
    }
}
