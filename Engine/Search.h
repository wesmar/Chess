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
#include <chrono>
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <functional>

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

    // ========== AI PLAYER ==========
    // Chess engine with alpha-beta search and iterative deepening
    // Uses transposition table, move ordering, and parallel search
    class AIPlayer
    {
    public:
        // Initialize AI with specified difficulty level
        // Higher difficulty = deeper search + more aggressive pruning
        AIPlayer(DifficultyLevel difficulty);

        // Calculate best move for current position
        // Uses iterative deepening with time management
        // @param board: Current position to analyze
        // @param maxTimeMs: Maximum thinking time in milliseconds
        // @param maxDepth: Maximum search depth (0 = no limit)
        // @return: Best move found within time limit
        Move CalculateBestMove(const Board& board, int maxTimeMs = 5000, int maxDepth = 0);

        // Update AI difficulty and transposition table size
        void SetDifficulty(DifficultyLevel difficulty);
        DifficultyLevel GetDifficulty() const { return m_difficulty; }

        // Configure number of search threads (root-parallel search)
        void SetThreads(int threads);

        // Abort current search immediately
        // Used when user stops analysis or game ends
        void AbortSearch();

        // Load NNUE network for neural evaluation
        // @param filename: Path to .nnue file
        // @return: true if loading succeeded
        bool LoadNnue(const std::string& filename);

        // Check if NNUE is available
        bool IsNnueAvailable() const { return m_evaluator.IsNnueAvailable(); }

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
        Move m_counterMoves[2][64][64];        // Countermove heuristic [side][from][to]

        int m_numThreads = 1;               // Parallel search threads

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

        // Live UCI info emitter (set by UCIEngine).
        InfoCallback m_infoCallback;

        // Current TT size in MB (for re-resize on SetDifficulty without losing UCI setting).
        int m_hashSizeMB = 16;

        // NNUE evaluator (hybrid mode with classical fallback)
        Neural::HybridEvaluator m_evaluator;

        // Thread-local heuristics to prevent data races
        struct ThreadLocalData {
            Move killerMoves[MAX_PLY][2];

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
        std::vector<int> GetSmallestAttacker(const std::array<Piece, SQUARE_COUNT>& pieces,
                                             int square, PlayerColor attackerColor) const;
    };
}
