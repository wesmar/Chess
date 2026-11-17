// Search.h
// AIPlayer - the chess engine's search interface.
// Iterative deepening, aspiration windows, PVS alpha-beta with a lock-free
// transposition table, quiescence search, and root-parallel worker threads.
//
// Implementation is split by responsibility:
//   Search.cpp       - lifecycle, root search, main-thread alpha-beta/quiescence
//   SearchWorker.cpp - worker-thread search (thread-local heuristics)
//   MoveOrdering.cpp - move scoring/ordering and Static Exchange Evaluation
#pragma once

#include "Board.h"
#include "TranspositionTable.h"
#include "Neural/HybridEvaluator.h"
#include "Neural/Network.h"
#include <chrono>
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <functional>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace Chess
{
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

    // History heuristic bounds. Values are kept inside +/-MAX_HISTORY by a
    // "gravity" update: a bonus moves a slot toward the limit in proportion to
    // how far it still has to go, so a slot saturates instead of growing without
    // end. Two reasons that matters in this engine:
    //
    //   - ScoreMovesCheap scores a quiet move at its raw history value while the
    //     killers sit at a fixed 800000 and the countermove at 600000. A counter
    //     with no ceiling can climb past those bands and invert the very
    //     ordering it exists to refine.
    //   - Saturation lets a newly good move catch up with a long-established one
    //     inside a single search instead of being outranked indefinitely by
    //     accumulated bonuses.
    //
    // Aging between root searches (v - v/8) already existed and stays; it limits
    // how much survives from move to move, not how far a slot climbs within one
    // search.
    // Bonus applied on a quiet-move cutoff, and its malus counterpart.
    //
    // Measured with --histstats over a 24-ply game at depth 8, the plain
    // depth*depth bonus left the butterfly table at maxAbs 1167 and meanAbs 23,
    // with 402 of 416 touched cells below 128. Meanwhile ScoreMovesCheap adds a
    // static centre bonus of up to 400 to the same score. In other words the
    // ordering of quiet moves was being decided by "does this move go to the
    // middle of the board", with the history heuristic as rounding noise, and
    // neither the MAX_HISTORY ceiling nor its gravity term ever engaged.
    //
    // Scaling the bonus lifts the table into the range the rest of the design
    // already assumed: history becomes the dominant term for moves that have
    // actually proven themselves, the centre bonus falls back to breaking ties
    // between moves with no history, and gravity starts shaping the table.
    inline constexpr int HISTORY_BONUS_SCALE = 8;

    // Weight of continuation history when ScoreMovesCheap orders quiet moves.
    //
    // Kept at 1 after a result that contradicted the obvious reading. The table
    // is sparse - 5306 of 589824 cells non-zero after a 24-ply game, 0.9% - and
    // a cell that saw one cutoff already holds up to 8*depth*depth, enough to
    // outweigh a butterfly entry built from many observations. Dropping it from
    // the ordering removed 20% of the nodes at fixed depth:
    //   weight 0 -> 1 633 063     weight 2 -> 1 823 314
    //   weight 1 -> 2 046 410     weight 4 -> 2 378 673
    //                             weight 6 -> 2 208 389
    //
    // And it still lost the match: 0.480 over 200 paired games. Cheaper cutoffs
    // on the moves the butterfly table already favours is a self-confirming
    // loop; the noisy continuation term was buying search diversity that the
    // node count cannot see. Do not treat fewer nodes as better ordering here.
    inline constexpr int CONT_HIST_WEIGHT = 1;

    inline constexpr int MAX_HISTORY = 16384;

    // A history-driven LMR term was tried here and measured negative: reducing
    // proven-good quiet moves by up to two plies less cost 18% more nodes for
    // 0.470 over 200 paired games. Together with the flat "reduce from move six"
    // experiment (also negative) that is two independent results saying this
    // search does not want to spend nodes on weaker reduction. Both directions
    // of the reduction knob have now been measured; see the Opus entry in
    // HANDOFF_OPUS.md before trying a third.

    // The table is shared with the helper threads. Losing an update to a race is
    // harmless - this is an ordering hint, not state the search depends on - so
    // the read-modify-write is deliberately not one atomic operation. The old
    // fetch_add was atomic but unbounded, which is the worse trade here.
    inline void ApplyHistoryBonus(std::atomic<int>& slot, int bonus) noexcept
    {
        if (bonus > MAX_HISTORY) bonus = MAX_HISTORY;
        else if (bonus < -MAX_HISTORY) bonus = -MAX_HISTORY;
        const int magnitude = bonus < 0 ? -bonus : bonus;
        const int value = slot.load(std::memory_order_relaxed);
        slot.store(value + bonus - value * magnitude / MAX_HISTORY,
                   std::memory_order_relaxed);
    }

    // Late Move Reduction strength: reduction = log(depth) * log(moveIndex+1) / LMR_DIVISOR.
    // Larger divisor = shallower reduction = less pruning.
    //
    // Kept at the original 2.25: raising it to 2.75 vanished into integer
    // truncation at the depths that matter (identical tree at depth 6, +2% and
    // +8% nodes at depth 9), so it was not a testable change on its own.
    inline constexpr double LMR_DIVISOR = 2.25;

    // Departing pawns on these ranks/diagonals might expose their own king.
    // A straight push on the king's FILE keeps that file blocked, so the
    // file deliberately is not included. Compile-time geometry: 512 bytes.
    inline constexpr auto PAWN_EXPOSURE_RAYS = [] {
        std::array<uint64_t, 64> rays{};
        for (int king = 0; king < 64; ++king)
            for (int sq = 0; sq < 64; ++sq)
            {
                const int df = sq % 8 - king % 8;
                const int dr = sq / 8 - king / 8;
                if (dr == 0 || df == dr || df == -dr)
                    rays[king] |= uint64_t{1} << sq;
            }
        return rays;
    }();

    inline bool StalematePossible(const Board& board, PlayerColor side) noexcept
    {
        // Callers already know side is NOT in check. One empty forward square
        // and no possible discovered check prove a legal pawn push (including
        // promotion). No make/undo or attack generation is needed in this case.
        // Otherwise return "unknown" and let HasLegalMove do the exact check.
        // Piece count is not a proof: four or more pieces can be stalemated.
        const int king = board.GetKingSquare(side);
        if (king < 0 || king >= 64) return true;
        const uint64_t occupied = board.GetAllOccupied();
        const uint64_t blocked = side == PlayerColor::White ? occupied >> 8 : occupied << 8;
        const uint64_t pushable = board.GetPawnMask(side) & ~blocked & 0x00FFFFFFFFFFFF00ULL;
        return (pushable & ~PAWN_EXPOSURE_RAYS[king]) == 0;
    }

    // Percentage of the move's time budget past which a new iterative-deepening
    // iteration is not started. See the comment at the loop in CalculateBestMove.
    // Two search parameters measured to exhaustion in this round, both left as
    // they were. Recorded so nobody spends the matches again.
    //
    // Aspiration window half-width, currently 30. Node counts over a 24-ply game
    // at depth 8: 12 -> 3 240 689, 20 -> 2 738 894, 30 -> 2 013 689,
    // 50 -> 1 675 818, 80 -> 1 752 289. Widening to 50 therefore removed 17% of
    // the work - and lost, 0.445 over 200 paired games. The window is not a cost
    // knob: its bounds feed null move, LMR and the futility margins, so a loose
    // window buys cheap nodes by making every pruning decision less informed.
    // Tightening to 20 on the inverted hypothesis then measured 0.503 after 160
    // games under SPRT. Both directions are settled; 30 is the optimum.
    //
    // Capture history, a table of [piece][destination][victim] recording which
    // captures actually cut off, was added and removed. At a weight of +/-2048 it
    // could lift a bishop capture over a rook one, overriding MVV-LVA with a
    // noisier signal: 0.425 over 167 games, again with 13% FEWER nodes. Cut to a
    // tie-breaker of +/-256, small enough to only separate captures of the same
    // victim, it measured 0.506 over 165 games. Harmful when strong enough to
    // matter, invisible when weak enough to be safe - MVV-LVA plus lazy SEE
    // already orders captures well enough here.

    inline constexpr int ITERATION_START_PERCENT = 55;

    // ========== AI PLAYER ==========
    // Chess engine with alpha-beta search and iterative deepening
    // Uses transposition table, move ordering, and parallel search
    class AIPlayer
    {
    public:
        // Initialize AI with specified difficulty level
        // Higher difficulty = deeper search + more aggressive pruning
        AIPlayer(DifficultyLevel difficulty);
        ~AIPlayer();

        // Calculate best move for current position
        // Uses iterative deepening with time management
        // @param board: Current position to analyze
        // @param maxTimeMs: Maximum thinking time in milliseconds
        // @param maxDepth: Maximum search depth (0 = no limit)
        // @return: Best move found within time limit
        Move CalculateBestMove(const Board& board, int maxTimeMs = 5000, int maxDepth = 0);

        // Update playing level without changing explicitly configured resources.
        void SetDifficulty(DifficultyLevel difficulty);
        DifficultyLevel GetDifficulty() const { return m_difficulty; }

        // Configure number of search threads (root-parallel search)
        void SetThreads(int threads);
        int GetThreads() const { return m_numThreads; }
        int GetHashSize() const { return m_hashSizeMB; }
        void SetOwnBook(bool enabled) { m_useBook = enabled; }
        // Must be called before launching a search that uses an external stop flag.
        void SetStopSignal(const std::atomic<bool>* signal) { m_stopSignal = signal; }

        // Abort current search immediately
        // Used when user stops analysis or game ends
        void AbortSearch();

        // Load NNUE network for neural evaluation
        // @param filename: Path to .nnue file
        // @return: true if loading succeeded
        bool LoadNnue(const std::string& filename);

        // Check if NNUE is available
        bool IsNnueAvailable() const { return m_network.IsReady(); }

        // Load a network; an empty path or a failure leaves classical evaluation
        // in place and reports why.
        bool LoadNetwork(const std::string& path)
        {
            const bool ok = m_network.Load(path);
            m_nnueMain.Reset();
            return ok;
        }
        const std::string& GetNetworkError() const { return m_network.GetError(); }

        // The single evaluation entry point for the search. Both the main thread
        // and the helpers go through it, so a loaded network is used by all of
        // them or by none - a mix would make the helpers disagree with the main
        // search about the same position.
        int EvaluateFor(const Board& board, Neural::NnueAccumulatorStack& stack)
        {
            if (m_network.IsReady()) return m_network.Evaluate(board, stack);
            return Chess::Evaluate(board);
        }

        // Get evaluator for direct access
        Neural::HybridEvaluator& GetEvaluator() { return m_evaluator; }
        void SetEvalCacheSizeMB(int sizeMB);
        void ClearEvalCache();

        // Set transposition table size in MB. Wired from UCI "Hash" option.
        void SetHashSize(int mb);

        // Reset TT + heuristics for new game. Called on UCI "ucinewgame".
        void NewGameReset();

        // Nodes searched in last/current CalculateBestMove. For UCI info output.
        [[nodiscard]] uint64_t GetNodesSearched() const { return m_nodesSearched.load(std::memory_order_relaxed); }

        // Set callback to emit live "info ..." strings (UCI). nullptr disables.
        using InfoCallback = std::function<void(const std::string&)>;
        void SetInfoCallback(InfoCallback cb) { m_infoCallback = std::move(cb); }

    private:
        friend struct EngineRegression;
        DifficultyLevel m_difficulty;
        std::chrono::steady_clock::time_point m_searchStartTime;
        int m_maxSearchTimeMs;

        // Transposition table for position caching
        TranspositionTable m_transpositionTable;

        static constexpr int MAX_PLY = 64;  // Maximum search depth

        // Hard cap on cumulative check extensions within a single search line.
        // Without this, an unbroken sequence of checks never lets `depth` decrease
        // (check extension adds back the ply just spent), so the recursion runs
        // all the way to MAX_PLY instead of the nominal search depth - catastrophic
        // in king-hunt endgames with long forcing check sequences.
        static constexpr int MAX_CHECK_EXTENSIONS = 16;

        // Move ordering heuristics for main thread
        Move m_killerMoves[MAX_PLY][2];     // Killer move heuristic
        std::atomic<int> m_history[2][64][64];           // History heuristic (per side)
        std::atomic<uint32_t> m_counterMoves[2][64][64]; // packed quiet moves, shared by helpers

        // Continuation history: quality of playing (piece,to) as a REPLY to
        // the opponent's previous (piece,to). Much sharper ordering signal
        // than from/to history alone because it captures move pairs.
        // Flat array [prevPiece(12)][prevTo(64)][piece(12)][to(64)] = 2.25 MB,
        // heap-allocated (too big for an in-object array).
        static constexpr int CONT_HIST_SIZE = 12 * 64 * 12 * 64;
        std::unique_ptr<std::atomic<int>[]> m_contHist;

        // 0-11 piece code for continuation history indexing (type + color)
        static int PieceCode(Piece p) noexcept
        {
            return (static_cast<int>(p.GetType()) - 1) + 6 * static_cast<int>(p.GetColor());
        }
        static int ContHistIndex(int prevPc, int prevTo, int pc, int to) noexcept
        {
            return ((prevPc * 64 + prevTo) * 12 + pc) * 64 + to;
        }

        int m_numThreads = 1;               // Parallel search threads
        bool m_useBook = true;
        const std::atomic<bool>* m_stopSignal = nullptr;

        // ========== LAZY SMP HELPER POOL ==========
        // Persistent helper threads searching the SAME root position as the
        // main thread, at staggered depths, coordinating only through the
        // shared lock-free TT. No root splitting, no shared alpha: helpers
        // populate the TT with deep entries and better move ordering, and the
        // main thread's own iterative deepening reaps them. Threads persist
        // across moves (no spawn cost per search); they sleep on a condition
        // variable between searches.
        std::vector<std::thread> m_helperPool;
        std::mutex m_helperMutex;
        std::condition_variable m_helperCv;      // wakes helpers for a new search
        std::condition_variable m_helperDoneCv;  // signals all helpers idle
        Board m_helperRoot;                      // root position of current search
        int m_helperMaxDepth = 0;
        uint64_t m_searchId = 0;                 // bumped per search; helpers wait for change
        int m_activeHelpers = 0;                 // helpers currently searching
        bool m_poolQuit = false;                 // set once in destructor

        void EnsureHelperPool(int helpers);      // grow pool to `helpers` threads
        void StartHelpers(const Board& root, int maxDepth);
        void WaitHelpersIdle();                  // after abort: block until all helpers sleep
        void HelperLoop(int idx);                // helper thread main

        // Search abort flag. Set by UCI "stop" AND by ShouldStop() itself when
        // the time budget expires: the clock query is gated (every 1024 nodes),
        // but once any node detects timeout, this flag makes every other node
        // unwind immediately via its cheap per-node check. mutable because
        // ShouldStop() is const. Without this propagation the tree keeps
        // searching at full cost between sparse clock checks - measured as
        // hundreds of ms of overshoot and outright time forfeits.
        mutable std::atomic<bool> m_abortSearch{false};

        // Node counter for UCI info. Incremented per AlphaBeta/Quiescence call.
        std::atomic<uint64_t> m_nodesSearched{0};
        static thread_local uint64_t s_pendingNodes;
        void CountNode()
        {
            if (++s_pendingNodes == 1024) FlushNodes();
        }
        void FlushNodes()
        {
            m_nodesSearched.fetch_add(s_pendingNodes, std::memory_order_relaxed);
            s_pendingNodes = 0;
        }

        // Live UCI info emitter (set by UCIEngine).
        InfoCallback m_infoCallback;

        // Current TT size in MB (for re-resize on SetDifficulty without losing UCI setting).
        int m_hashSizeMB = 16;

        // NNUE evaluator (hybrid mode with classical fallback)
        Neural::HybridEvaluator m_evaluator;

        // Network evaluation. When no network is loaded every path below falls
        // through to the classical evaluation, so the engine behaves exactly as
        // before; that is what keeps the two comparable in a match.
        Neural::Network m_network;
        Neural::NnueAccumulatorStack m_nnueMain;

        // Thread-local heuristics to prevent data races
        struct ThreadLocalData {
            Move killerMoves[MAX_PLY][2];
            // One accumulator per helper thread. The network weights are shared
            // and read-only; only this state is per-thread.
            Neural::NnueAccumulatorStack nnue;

            ThreadLocalData() {
                for (int i = 0; i < MAX_PLY; ++i) {
                    killerMoves[i][0] = Move();
                    killerMoves[i][1] = Move();
                }
            }
        };

        // ========== SEARCH ALGORITHMS ==========

        // Alpha-beta negamax with pruning optimizations
        // excludedMove: when set (IsValid()), that move is skipped in the loop (used for SE)
        // checkExtCount: cumulative check extensions applied so far in this line (caps runaway check-chains)
        int AlphaBeta(Board& board, int depth, int alpha, int beta, int ply,
                      Move excludedMove = Move{}, int checkExtCount = 0);

        // Quiescence search - tactical move resolution
        int QuiescenceSearch(Board& board, int alpha, int beta, int ply, int qDepth);

        // Worker thread search with thread-local data
        // excludedMove: when set (IsValid()), that move is skipped in the loop (used for SE)
        // checkExtCount: cumulative check extensions applied so far in this line (caps runaway check-chains)
        int WorkerAlphaBeta(Board& board, int depth, int alpha, int beta, int ply,
                            ThreadLocalData& tld, Move excludedMove = Move{}, int checkExtCount = 0);
        int WorkerQuiescence(Board& board, int alpha, int beta, int ply, int qDepth,
                             ThreadLocalData& tld);

        // Check if search time limit exceeded
        bool ShouldStop() const;
        std::optional<int> TerminalScore(Board& board, int ply);
        uint64_t SearchKey(const Board& board) const;
        static int LateMoveReduction(int depth, int moveIndex);

        // ========== MOVE ORDERING ==========
        // Better move ordering = more beta cutoffs = faster search

        void OrderMoves(MoveList& moves, const Board& board, Move ttMove, int ply);
        int ScoreMove(const Move& move, const Board& board, Move ttMove, int ply);

        void OrderMovesWorker(MoveList& moves, const Board& board, Move ttMove,
                              int ply, const ThreadLocalData& tld);
        int ScoreMoveWorker(const Move& move, const Board& board, Move ttMove, int ply,
                            const ThreadLocalData& tld);

        void OrderMovesSimple(MoveList& moves, const Board& board, Move ttMove);

        // ========== STAGED MOVE PICKING (interior nodes) ==========
        // 70-80% of nodes cut off after 1-3 moves, so sorting the whole list
        // and running SEE on every capture up front is wasted work. Instead:
        // ScoreMovesCheap assigns SEE-free scores (TT move, MVV-LVA, killers,
        // countermove, history), and PickNextMove selects lazily - SEE is
        // computed only for a capture that is actually about to be searched,
        // and losing captures are demoted below quiets on first touch.

        // Fill scores[i] for each move using cheap heuristics only (no SEE)
        void ScoreMovesCheap(const MoveList& moves, int* scores, const Board& board,
                             Move ttMove, int ply, Move killer0, Move killer1) const;

        // Selection-pick the best remaining move into position `start`
        // (swaps in both arrays). Returns false when no moves remain.
        // Lazily verifies captures with SEE, demoting losing ones.
        bool PickNextMove(MoveList& moves, int* scores, int start, const Board& board) const;

        // Filter pseudo-legal moves to legal moves by verifying king safety.
        // Used at the root; interior nodes verify legality lazily during make.
        MoveList FilterLegalMoves(Board& board, const MoveList& pseudoMoves,
                                  PlayerColor sideToMove, PlayerColor opponentColor);

        // Static Exchange Evaluation - evaluate capture sequences
        int SEE(const Board& board, const Move& move) const;
        int GetSmallestAttacker(const std::array<Piece, SQUARE_COUNT>& pieces,
                                             int square, PlayerColor attackerColor) const;
    };
}
