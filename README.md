# ♔ Modern Chess Engine

![Chess Engine Screenshot](Images/chess.gif)

A competitive chess engine written in modern C++20 with pure WinAPI interface. Built on a deliberately contrarian architecture — a cache-line-aligned mailbox board instead of magic bitboards — and optimized with HFT-style engineering: lock-free data structures, cache-conscious memory layout, and latency hiding via prefetch. Small enough to read, strong enough to compete.

[![Windows](https://img.shields.io/badge/Windows-10%2B-blue.svg)](https://www.microsoft.com/windows)
[![C++](https://img.shields.io/badge/C%2B%2B-20-orange.svg)](https://isocpp.org/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)](https://github.com/wesmar/Chess/releases)

## Why This Project?

Most chess engines are either too simple (lacking modern techniques) or too complex (thousands of files, external dependencies). This project implements the full modern search arsenal — PVS, singular extensions, ProbCut, aspiration windows, lock-free transposition table — in clean, readable code, while refusing to copy the standard bitboard blueprint.

This is a case study in building a high-performance chess system using **Data-Oriented Design (DOD)** principles, maximizing computational throughput while drastically reducing binary size and eliminating all external library dependencies. Every strength-affecting change is validated the scientific way: engine-vs-engine matches under cutechess-cli with fixed openings and time controls.

## Key Features

### 🎯 Extreme Optimization
- **No CRT version**: ~ 200 KB
- **With CRT version**: ~ 500 KB
- **Zero external dependencies** - no Qt, SDL, or .NET
- **Portable**: Even runs in Windows Recovery Environment (WinRE)!

### 🧠 Advanced Search Algorithms

The engine employs a comprehensive suite of search techniques:

#### Core Search Framework
- **Minimax with Alpha-Beta Pruning** - efficient move tree search
- **Principal Variation Search (PVS) at every node** - first move searched with a full window, all others with a null-window scout; re-search only on fail-high. Applied at the root and throughout the interior of the tree
- **Iterative Deepening** - finds best move within time limit
- **Aspiration Windows with progressive widening** - each iteration starts with a ±30cp window around the previous score; on fail-low/fail-high the window is doubled and the iteration re-searched, converging quickly without wasting a full-window pass
- **Quiescence Search** - eliminates horizon effect by searching tactical sequences
- **Lazy Legality Checking** - interior nodes search pseudo-legal moves and verify king safety only when a move is actually made; a beta cutoff after 1-2 moves never pays for validating the other 30+

#### Pruning Techniques
- **Null Move Pruning (NMP)** - skips branches where opponent can't improve even with two consecutive moves; gated on `staticEval >= beta` (no free move can help a losing position) and disabled in endgames to avoid zugzwang errors; runs before move generation so a cutoff skips it entirely
- **ProbCut** - prunes branches where shallow tactical search at higher beta already exceeds the expected bound; uses captures-only search with SEE filter
- **Late Move Reduction (LMR)** - reduces depth for quiet moves late in move list using logarithmic formula `log(depth) × log(moveIndex) / 2.25`, with re-search on alpha improvement
- **Late Move Pruning (LMP)** - completely skips late quiet moves at shallow depths
- **Futility Pruning** - skips quiet moves at shallow depths when static eval + margin ≤ alpha
- **Reverse Futility Pruning (RFP)** - returns early when static eval − margin ≥ beta (static null move pruning)
- **Delta Pruning** - in quiescence search, skips captures that can't possibly raise alpha
- **SEE Pruning in Main Search** - captures losing more than `120 × depth` centipawns by Static Exchange Evaluation are skipped at shallow depths
- **Mate Distance Pruning** - stops searching for longer mates when shorter one already found

#### Search Extensions
- **Check Extensions** - extends search depth when side to move is in check
- **Singular Extensions (SE)** - extends the TT move when a reduced search with lowered beta confirms it is the only good move in the position; uses `excludedMove` parameter to prevent recursive SE

#### Position Caching
- **Zobrist Hashing** - lightning-fast incremental position comparison using XOR-sum
- **Lock-Free Transposition Table (Hyatt XOR-key)** - 16-byte entries with `(keyXorData, data)` atomic pair; readers reconstruct the key by XOR'ing both fields and discard inconsistent reads. Eliminates all mutex contention in the hot path (no striped locking, no `lock` prefixes on probes). Separate `ProbeSE()` method retrieves reference scores for Singular Extensions regardless of depth/bound filtering. Mate-distance scores clamped to a fail-soft-safe envelope so INFINITY-class returns never poison future probes.
- **Cache-Line TT Buckets (4-way)** - entries grouped into 64-byte `alignas(64)` buckets of four; any probe or store touches exactly one cache line, and 4-way associativity sharply reduces replacement collisions versus a direct-mapped table. Replacement priority: same key → empty slot → shallowest entry
- **TT Prefetch (HFT-style latency hiding)** - `_mm_prefetch` issued on the child's Zobrist key immediately after making a move, so the DRAM fetch overlaps with repetition checks and mate-distance pruning; by the time the child node probes the table, the line is already in L1
- **TT Reuse Across Moves** - hash table preserved between consecutive moves in the same game; only `ucinewgame` triggers a full reset. Dramatically reduces re-search of identical positions reached through different move orders.
- **Internal Iterative Deepening (IID)** - searches shallowly to find a good TT move when none is available, improving move ordering at cost-effective depth
- **Threefold Repetition Detection** - recognizes draw by repetition during search

#### Move Ordering Heuristics
- **TT Move** - transposition table best move searched first
- **SEE (Static Exchange Evaluation)** - evaluates capture sequences; winning captures (SEE ≥ 0) ordered before killers, losing captures (SEE < 0) ordered last
- **MVV-LVA (Most Valuable Victim - Least Valuable Attacker)** - breaks ties among captures of equal SEE
- **Killer Move Heuristic** - remembers quiet moves that caused beta cutoffs at each ply (two slots per ply)
- **Countermove Heuristic** - remembers the quiet move that refuted the previous move; provides a third ordering hint after killers
- **History Heuristic with Malus** - side-specific scoring for quiet moves that historically caused cutoffs (`+depth²` bonus); quiet moves searched *before* the cutoff move receive a matching `-depth²` penalty, so misordered moves sink fast. Gentle decay (right-shift by 3 = retain ~87%) applied once per search preserves cross-move signal
- **Stack-Allocated Move Scoring** - `OrderMoves` uses a 256-element stack buffer instead of `std::vector<std::pair<int, Move>>`, removing the last heap allocation from the search hot path
- **Center Control Bonus** - tactical bonus for moves targeting central squares

#### Parallel Search (Lazy SMP)
- **Lazy SMP with a persistent thread pool** - helper threads search the *same root position* at staggered depths and coordinate purely through the shared lock-free transposition table; no root splitting, no shared alpha, no synchronization in the search itself. Measured: **+147 Elo at 4 threads** (LOS 100%), NPS ×4.1, +1 ply at equal time
- **Zero spawn cost** - helpers live inside the engine and sleep on a condition variable between moves; a search wakes them, timeout or completion puts them back to sleep
- **Thread-Local Heuristics** - separate killer move tables per thread; history/continuation tables are shared atomics (relaxed ordering)

#### Opening Book
- **Hardcoded Opening Lines** - Zobrist-indexed book with random move selection for variety

### ⚡ High-Performance Memory Architecture

#### Stack-Allocated Move Generation
- **MoveList Structure** - fixed 256-move array on stack, eliminating millions of heap allocations per search
- **Zero malloc() in hot path** - entire move generation and search uses stack memory only

#### Efficient Piece Tracking
- **PieceList per Color** - O(1) piece iteration without scanning 64 squares
- **Swap-with-last Removal** - O(1) piece list updates during make/unmake

#### Incremental Updates
- **Incremental Zobrist Key** - XOR updates instead of full recomputation
- **Incremental MG/EG Score Maintenance** - both middlegame and endgame scores (material + PST) updated on each move, eliminating per-evaluation recomputation
- **Bitboard Occupancy Tracking** - hybrid architecture for fast sliding piece queries
- **Pawn Bitboards per Color** - `m_pawnMasks[2]` updated incrementally, enabling O(1) open-file and pawn shield queries in evaluation

#### Hybrid Board Representation
- **64-byte Mailbox (Cache-Aligned)** - entire board state in single L1 cache line
- **Bitboards for Occupancy** - fast sliding piece move generation without expensive lookups
- **~800KB savings** compared to Magic Bitboards approach

### 📊 Hand-Crafted Evaluation (HCE)

A tuned evaluation function with tapered scoring:

#### Material & Positional
- **Piece-Square Tables (PST)** - separate middlegame and endgame tables for every piece type (pawn, knight, bishop, rook, queen; king already had separate MG/EG tables)
- **Tapered Evaluation** - smooth interpolation based on game phase: `score = (mgScore × phase + egScore × (256 − phase)) / 256`; phase computed from remaining material (Q=4, R=2, B/N=1, max=24 → maps to 0–256)
- **Incremental EG Score** - `m_egScore` maintained alongside `m_incrementalScore` (MG), restored via `MoveRecord` on undo — zero extra work per evaluation call
- **Bishop Pair Bonus** - +40 centipawns for having both bishops

#### King Safety (Middlegame)
- **Castling Position Bonus** - rewards castled king placement
- **Pawn Shield Analysis** - evaluates pawns protecting the king using pawn bitboards
- **Open File Penalty** - penalizes missing pawns near king (O(1) via pawn bitboard file mask)

#### Piece Activity
- **Mobility Evaluation** - counts accessible squares for sliding pieces using bitboard occupancy
- **Center Control Bonus** - rewards knights and bishops on central squares
- **Exposed Queen Penalty** - penalizes queen on attacked square in middlegame
- **Piece Tropism** - bonus for pieces close to the enemy king

#### Pawn Structure
- **Isolated Pawn Penalty** - pawns with no friendly pawns on adjacent files
- **Doubled Pawn Penalty** - multiple pawns on same file
- **Backward Pawn Penalty** - pawns that cannot safely advance

#### Passed Pawns
- **Advancement Bonus** - exponentially increasing value as pawn advances
- **King Distance Factor** - bonus when friendly king is close, penalty when enemy king is close
- **Rook Behind Passed Pawn** - bonus for rook supporting from behind

#### Tempo
- **Side to Move Bonus** - small advantage for having the move (+8 centipawns)

### 🤖 NNUE Infrastructure (Prepared)

The engine includes complete NNUE (Efficiently Updatable Neural Network) infrastructure:

- **HybridEvaluator** - seamless switching between Classical/NNUE/Auto modes
- **NeuralEvaluator** - inference engine with incremental accumulator updates
- **FeatureExtractor** - HalfKP-style feature extraction (king-piece relationships)
- **WeightLoader** - binary weight file parser (.nnue format)
- **DenseLayer** - optimized matrix operations with SIMD support
- **Transformer Architecture** - attention-based feature processing

Currently runs in Classical mode. NNUE activation requires trained weight file (`nn-small.nnue`).

### 🏗️ Architecture & Design Choices

#### Memory Architecture: Cache-Locality First
I deliberately chose a **64-byte "Mailbox" array representation** over pure bitboards, aligned to L1 cache lines. This decision minimizes cache misses during iterative game tree traversal - the most critical performance bottleneck in chess engines.

#### Data-Oriented Design
- **1-byte piece structures** (uint8_t) with bitmask encoding
- Eliminates polymorphism overhead (no v-tables)
- Deterministic memory layout for predictable performance
- SIMD-friendly data structures

### 🛠 Optimization Philosophy (Data-Oriented Design)
This project proves that modern C++20 can be as low-level as C while maintaining safety:
- **Cache Locality**: The `Board` class is aligned to 64 bytes (L1 Cache Line size). Fetching a board state fetches the entire board into the CPU cache in a single cycle
- **Data Packing**: `Piece` class is a 1-byte wrapper around `uint8_t` with no v-table overhead
- **Stack Allocation**: Heavy preference for stack memory over heap allocation to reduce fragmentation and allocation costs
- **Small Footprint**: The entire engine logic compiles to <200 KB, making it suitable for embedded environments or recovery tools (WinRE)

### 🎨 User Interface
- **Unicode Rendering** - clean chess pieces (♔♕♖♗♘♙) using native WinAPI vector rendering
- **Full Chess Rules** - castling, en passant, pawn promotion
- **Move History** - algebraic notation with full game record
- **Undo/Redo** - complete move history with board states
- **PGN Support** - save and load games in standard format
- **Multiple Game Modes** - Human vs Human, Human vs AI, AI vs AI
- **10 Difficulty Levels** - from beginner (random moves) to expert (deep search)
- **Configurable Threading** - 1-64 threads for parallel search

## Quick Start

### Download
Grab the latest release from [Releases](https://github.com/wesmar/Chess/releases/download/latest/Chess.7z):
Password: github.com
- `Chess_x64.exe` - 64-bit version (~500 KB)
- `Chess_x64_minSize.exe` - 64-bit minimal (~200 KB)
- `Chess_x86.exe` - 32-bit version (~450 KB)
- `Chess_x86_minSize.exe` - 32-bit minimal (~160 KB)

Just download and run - no installation needed!

### Building from Source

**Requirements:**
- Windows 10 or later
- Visual Studio 2022 or newer with C++20 support (v143+ toolset)
- Tested with Visual Studio 2026 (v145 toolset)

**Build Steps:**
```batch
# Clone the repository
git clone https://github.com/wesmar/Chess.git
cd Chess

# Build all configurations using build_all.bat
# Uses /BREPRO flag for reproducible builds
build_all.bat

# Binaries will be in the 'bin' folder
```

**Manual Build:**
```batch
# Open Developer Command Prompt for VS 2022+
# Navigate to project directory

# Build 64-bit release
msbuild Chess.vcxproj /p:Configuration=Release /p:Platform=x64

# Build 64-bit minimal size
msbuild Chess.vcxproj /p:Configuration=Release_MinSize /p:Platform=x64
```

## Project Structure
```
Chess/
├── Engine/                    # Chess engine core
│   ├── Board.cpp/h           # Board representation and move execution
│   ├── ChessConstants.h      # Core constants and enums
│   ├── Evaluation.cpp/h      # Position evaluation with tapered PST tables
│   ├── Move.cpp/h            # Move representation (32-bit packed)
│   ├── MoveGenerator.cpp/h   # Pseudo-legal move generation
│   ├── MoveOrdering.cpp      # Move scoring/ordering heuristics + SEE
│   ├── OpeningBook.cpp/h     # Hardcoded opening book
│   ├── Piece.h               # Piece representation (8-bit packed)
│   ├── Search.cpp/h          # AIPlayer: root search + main-thread alpha-beta
│   ├── SearchWorker.cpp      # Helper-thread search (Lazy SMP scheme)
│   ├── TranspositionTable.cpp/h # Lock-free hash table (4-way cache-line buckets)
│   └── Zobrist.cpp/h         # Zobrist hashing for positions
├── Engine/Neural/             # NNUE evaluation system
│   ├── HybridEvaluator.h     # Classical/NNUE mode switching
│   ├── NeuralEvaluator.h     # NNUE inference engine
│   ├── FeatureExtractor.h    # HalfKP feature extraction
│   ├── FeatureAccumulator.h  # Incremental accumulator updates
│   ├── DenseLayer.h          # Matrix operations
│   ├── WeightLoader.h        # .nnue file parser
│   └── Transformer.h         # Attention architecture
├── UI/                        # User interface
│   ├── Dialogs/
│   │   ├── GameSettingsDialog.cpp/h  # Tabbed settings dialog
│   │   └── PromotionDialog.cpp/h     # Pawn promotion selector
│   ├── ChessGame.cpp/h       # Game controller (state, history, PGN)
│   ├── main.cpp              # Application entry point
│   ├── VectorRenderer.cpp/h  # Board rendering with Unicode pieces
│   ├── WinApp.cpp/h          # Main window and event handling
│   └── WinUtility.cpp/h      # String conversion utilities
├── UCI/                       # UCI engine implementation
│   ├── UCIMain.cpp           # Console entry point
│   └── UCIEngine.h           # UCI protocol handler
├── Resources/                 # Icons and resource files
│   ├── Icons/app.ico
│   ├── Chess.rc              # Resource definitions
│   └── Resource.h            # Resource ID constants
├── testing/                   # Engine-vs-engine test harness
│   ├── run_match.ps1         # SPRT match driver (cutechess-cli)
│   └── openings.pgn          # Fixed opening set for fair pairs
├── build_all.bat             # Build script for all configurations
├── build_uci.bat             # Quick build: UCI engine only, x64 Release
├── Chess.vcxproj             # Main GUI application project
├── ChessEngineUCI.vcxproj    # UCI console engine project
└── Chess.slnx                # Visual Studio solution
```

## Learning the Code

If you're new to chess programming, here's a suggested reading order:

1. **Start with `Board.cpp`** - This is where all the chess magic happens (piece movement, rule validation)
2. **Then `Evaluation.cpp`** - Learn how the computer "thinks" about positions
3. **Move to `MoveGenerator.cpp`** - See how moves are generated
4. **Then `Engine/Search.cpp`** - The heart of the engine: iterative deepening, aspiration windows, PVS alpha-beta, quiescence
5. **Finally `Engine/MoveOrdering.cpp`** - Why move ordering decides everything: SEE, killers, history with malus

### Key Concepts Explained

**Mailbox vs Bitboards**: While bitboards are faster for some operations, the mailbox representation (64-element array) provides better cache locality for iterative search algorithms, which dominate chess engine runtime.

**Piece-Square Tables (PST)**: Arrays that assign values to pieces based on their position. Separate MG and EG tables allow the engine to value the same square differently depending on game phase — for example, an advanced pawn near promotion is worth far more in the endgame.

**Alpha-Beta Pruning**: If you find a move that's already better than the best option your opponent has, you don't need to check other moves in that branch.

**Null Move Pruning**: If giving the opponent a free move still doesn't help them, the position is so good we can skip detailed analysis of this branch.

**ProbCut**: If a shallow tactical search (captures only) already exceeds a higher beta threshold, the full-depth search is extremely unlikely to fall below beta — so prune immediately.

**Singular Extensions**: If the TT move is significantly better than all other moves (verified by a reduced-depth search with lowered beta), extend its search depth by one. Ensures the engine doesn't underestimate forced lines.

**Late Move Reduction**: Moves that appear worse (ordered late) are searched to a shallower depth initially. If they turn out to be good, we re-search them at full depth.

**Transposition Table**: Chess positions can be reached through different move orders. The table remembers positions we've already evaluated. Ours is fully lock-free (Hyatt XOR-key validation) and organized into 4-way buckets that each fit exactly one 64-byte cache line, with an explicit prefetch issued the moment a move is made.

**Lazy SMP**: All threads search the same position; the shared transposition table does the coordination. A helper finishing depth *d* leaves deep TT entries and move-ordering hints that the main thread's iterative deepening immediately exploits. Counterintuitive but battle-proven: the redundancy *is* the algorithm.

**Tapered Evaluation**: The same position is worth different amounts in the opening vs. endgame. The engine maintains two incremental scores (MG and EG) and interpolates between them based on remaining material.

## Settings

The game creates and saves your preferences in `settings.ini` (created automatically on first run):
```ini
[Display]
FlipBoard=0
ShowCoordinates=1
ShowLegalMoves=1
AnimateMoves=1
AnimationSpeed=300

[Game]
GameMode=1          # 0=HvH, 1=HvC, 2=CvC
AIDifficulty=3      # 1-10
Threads=4           # 1-64 (auto-detected from CPU)
AutoPromoteQueen=1

[Colors]
LightSquare=240,240,245
DarkSquare=70,80,100
```

## Keyboard Shortcuts

- **Ctrl+N** - New game
- **Ctrl+Z** - Undo move
- **Ctrl+Y** - Redo move
- **F** - Flip board
- **L** - Toggle legal moves display
- **C** - Toggle coordinates
- **Esc** - Clear selection

## UCI Engine (Arena / Cutechess / Fritz)

`ChessEngineUCI_x64.exe` is a standard UCI console engine. Drop it into any UCI-compatible GUI (Arena, Cutechess, Fritz, ChessBase) and it auto-detects.

### Supported UCI options

| Option | Type | Range | Description |
|---|---|---|---|
| `Threads` | spin | 1..64 | Parallel root-search workers. Set to physical core count for best results. |
| `Hash` | spin | 1..4096 (MB) | Transposition table size. 128-256 MB recommended; 512+ on memory-rich systems. |
| `Level` | spin | 1..10 | AI strength. 1-2 randomized, 3-5 casual, 6-7 NMP, 8-10 full pruning + ProbCut + SE + LMR. Use **10** for tournament play. |
| `Move Overhead` | spin | 0..5000 (ms) | Safety margin subtracted from per-move time budget to absorb GUI/network latency. 30-50 local, 100-200 online. |
| `Ponder` | check | on/off | Currently accepted but no-op (engine does not yet think on opponent's clock). |
| `UCI_AnalyseMode` | check | on/off | Accepted, currently no-op. |

The engine emits live `info depth/score/nodes/nps/time/pv` after every completed iterative-deepening iteration, so GUIs display the thinking process in real time.

### Quick smoke test

```
ChessEngineUCI_x64.exe
uci
setoption name Hash value 256
setoption name Threads value 8
setoption name Level value 10
isready
position startpos
go movetime 3000
```

Full Arena setup walkthrough is in [`arena.txt`](arena.txt).

A self-play driver (`selfplay.ps1`) ships with the engine — it spawns the UCI binary, feeds moves via stdin, and prints the resulting game in UCI notation:

```powershell
.\selfplay.ps1 -MaxMoves 30 -MoveTimeMs 1500
```

## Technical Details

### Why So Small?

1. **No External Libraries** - Pure WinAPI, no MFC/ATL/Qt
2. **Static Linking** - Everything in one executable
3. **Smart Optimization** - Compiler flags tuned for size
4. **Minimal CRT** - Optional CRT-free build
5. **Reproducible Builds** - Uses /BREPRO flag for deterministic output
6. **Data-Oriented Design** - Compact data structures optimized for cache locality

### Performance Characteristics

- **Memory Usage**: 16-64 MB for transposition table (configurable)
- **Startup Time**: Near-instant (<100ms)
- **Binary Size**: 150-500 KB depending on configuration

### Measured Strength Progress

Every batch of search changes is validated with cutechess-cli matches (fixed opening set, tc=8+0.08, 1 thread, level 10, classical eval). The harness ships with the repo:

```powershell
# SPRT match: stops automatically once +5 Elo is proven (or disproven)
.\testing\run_match.ps1 -New bin\ChessEngineUCI_x64.exe -Base testing\reference\ChessEngineUCI_x64.exe
```

| Batch | Changes | Result |
|---|---|---|
| 1 | PVS at all nodes, TT-probe-before-pruning, NMP eval gate, aspiration widening, history malus, SEE pruning | **~+200 Elo** (76% score over 194 games) |
| 2 | Lazy legality, 4-way cache-line TT buckets, TT prefetch, NMP before movegen, AVX2 codegen | **~+120 Elo** vs batch 1; NPS +38%, +1-2 ply at equal time |
| 3+4 | Staged move picker with lazy SEE (NPS +20%), TT in quiescence, 50-move rule in search, timeout-latch abort, endgame knowledge (mop-up, drawish scaling), O(1) occupancy restore in undo | **+35 Elo** vs batch 2 (LOS 98.7%, 410 games) |
| 5 | Continuation history, TT generation aging, killers ply+2 clearing | **+8 Elo**; search tree −15% at equal depth |
| 6 | Lazy SMP: persistent helper pool, whole-root search at staggered depths, TT-only coordination | **+147 Elo at 4 threads** (69.4% over 116 games, LOS 100%) |

Absolute calibration (single thread, fast TC): **~2160 Elo** on the Stockfish UCI_Elo scale — 200-game match vs SF 17 @ 2200 plus a descending ladder (holds SF 2100 at 80%). With 4 threads and the Lazy SMP gain the engine plays at roughly **~2300 on the same scale**.

Batch 3 taught the most valuable lesson of the project: three consecutive test matches showed a "regression"
that turned out to be time forfeits, not chess. A gated clock check let the search overrun its budget by
hundreds of milliseconds after timeout. Moral: **check the match logs for `loses on time` before touching
your evaluation.**

### Architecture Decisions

**Why 64-byte Mailbox?**
- Aligns perfectly with L1 cache line size (64 bytes on most CPUs)
- Single cache miss loads entire board state
- Simple indexing without bitboard manipulation overhead
- Better for iterative traversal patterns in alpha-beta search

**Why 1-byte Pieces?**
- No polymorphism = no v-table overhead
- Cache-friendly: entire board fits in L1 cache
- Bit masking for piece properties (type, color, moved flag)
- Predictable memory layout

**Why Hand-Crafted Evaluation?**
- Neural networks require large weights (megabytes)
- HCE provides good strength with minimal size
- Easier to understand and modify for learning
- Fast evaluation critical for search performance

**Why Stack-Allocated MoveList?**
- Eliminates millions of malloc/free calls per second during search
- Fixed 256-move array covers all legal chess positions (max ~218 moves)
- Predictable memory access patterns for CPU prefetcher

**Why Incremental MG/EG Scores?**
- Tapered eval requires both scores on every node
- Recomputing from scratch each call would be O(pieces) per node
- Incremental updates keep it O(1) — just save/restore one integer per move

## 🚀 Roadmap

### Completed ✅
- [x] Null Move Pruning (NMP)
- [x] ProbCut (tactical captures-only, SEE filtered)
- [x] Late Move Reduction (LMR) — logarithmic formula with re-search
- [x] Late Move Pruning (LMP)
- [x] Futility Pruning
- [x] Reverse Futility Pruning (RFP / Static Null Move)
- [x] Delta Pruning in Quiescence
- [x] Mate Distance Pruning
- [x] Check Extensions
- [x] Singular Extensions with `excludedMove` parameter
- [x] Internal Iterative Deepening (IID)
- [x] Aspiration Windows
- [x] SEE (Static Exchange Evaluation) — move ordering and QS pruning
- [x] Killer Move Heuristic (two slots per ply)
- [x] Countermove Heuristic
- [x] History Heuristic with gentle per-search decay (right-shift by 3)
- [x] Tapered Evaluation — separate MG/EG PST tables for all piece types
- [x] Incremental MG + EG scores (zero-cost per evaluation call)
- [x] Pawn bitboards for O(1) open-file and pawn shield queries
- [x] Cache-aligned memory structures
- [x] Stack-allocated MoveList
- [x] Stack-allocated move scoring buffer in `OrderMoves` (no heap alloc per node)
- [x] Lazy SMP: persistent thread pool, whole-root helpers, TT-only coordination (+147 Elo @ 4 threads)
- [x] Lock-free transposition table (Hyatt XOR-key validation, 16-byte entries)
- [x] TT preservation across moves in the same game (cleared only on `ucinewgame`)
- [x] Fail-soft returns for futility / reverse futility pruning
- [x] Live UCI `info depth/score/nodes/nps/time/pv` emitted after every completed iteration
- [x] Adaptive time management with `Move Overhead` UCI option
- [x] NNUE infrastructure (HybridEvaluator, NeuralEvaluator, etc.)
- [x] PVS null-window scout at every interior node
- [x] Aspiration windows with progressive widening re-search
- [x] History malus (penalize quiets ordered above the cutoff move)
- [x] SEE pruning of losing captures in the main search
- [x] NMP gated on static eval and hoisted before move generation
- [x] Lazy legality checking (no up-front legal filtering at interior nodes)
- [x] TT bucketing — 4 entries per 64-byte cache line, `alignas(64)`
- [x] TT prefetch after make-move (HFT-style latency hiding)
- [x] AVX2 + intrinsics codegen for x64 release builds
- [x] Search split by responsibility: `Search.cpp` / `SearchWorker.cpp` / `MoveOrdering.cpp`

- [x] Staged move picker: cheap scoring + lazy SEE at pick time (NPS +20%)
- [x] TT probing and stores in quiescence search
- [x] Fifty-move rule detection inside the search tree
- [x] Timeout latch: gated clock check + per-node abort flag (bounded overshoot)
- [x] Endgame knowledge: mop-up for bare-king conversion, dead-draw and OCB scaling
- [x] O(1) occupancy snapshot-restore in UndoMove (no per-undo board rescan)
- [x] SPRT-driven testing pipeline (`testing/run_match.ps1`, `-Threads` for SMP tests)
- [x] Continuation history (reply-pair move ordering, 2.25 MB flat table)

### Future 📋
- [ ] Per-thread history tables (eliminate false sharing on shared atomics)
- [ ] NNUE weight training and integration (small fast net, own trainer)
- [ ] Syzygy tablebase support
- [ ] MultiPV analysis mode
- [ ] Texel Tuning for PST and eval weights
- [ ] Real pondering (think on opponent's clock)

## Challenge

**Can you beat difficulty level 3?** If you can consistently win at level 3, you're playing at a decent amateur level. 🏆

## FAQ

**Q: Why WinAPI and not cross-platform?**  
A: This project is focused on simplicity and extreme optimization. WinAPI provides everything needed without extra abstraction layers. A cross-platform version would require SDL/SFML, adding complexity and size.

**Q: Why mailbox instead of bitboards? Isn't that slower?**  
A: It's a deliberate architectural bet. Raw move generation is somewhat slower than magic bitboards, but the entire board state lives in one L1 cache line, make/unmake is trivially cheap, and the engine wins the time back where it matters more: move ordering, pruning quality, and a cache-conscious transposition table. Strength is validated by measurement, not by copying the standard blueprint.

**Q: Can it beat me?**  
A: It has 10 difficulty levels ranging from beginner-friendly (random moves) to challenging (10-ply search with full heuristics). Try different levels to find your match!

**Q: Can I modify it for my own learning?**  
A: Absolutely! That's the point. The code is well-commented and structured for learning. Fork it, break it, improve it!

**Q: Does it work on Windows 7/8?**  
A: The code uses Windows 10+ APIs, but could be adapted for older systems with minor changes to the WinAPI calls.

**Q: Why not use a UCI protocol?**  
A: The project includes both. There's a UCI console engine (`ChessEngineUCI.exe`) that works with Arena, Cutechess, Fritz, and any UCI-compatible GUI, plus the main standalone WinAPI application. The UCI version supports `Threads`, `Hash`, `Level`, and `Move Overhead` options, emits live `info` lines, and preserves the transposition table between moves in the same game. See `arena.txt` for a step-by-step Arena setup guide.

**Q: What about NNUE?**  
A: The complete NNUE infrastructure is implemented and ready. The engine currently runs in Classical evaluation mode. To enable NNUE, place a trained `nn-small.nnue` weight file in the application directory.

## Contributing

Found a bug? Have an improvement idea? Feel free to:
1. Open an issue describing the problem/suggestion
2. Fork the repository
3. Create a pull request

Please keep changes focused and well-documented!

## License

This project is licensed under the MIT License - see the [LICENSE](https://github.com/wesmar/Chess/blob/main/LICENSE.md) file for details.

## Acknowledgments

This project was born during Christmas 2025/2026 and has since grown from an educational engine into a measured, match-tested competitor demonstrating Data-Oriented Design principles in modern C++. Special thanks to the chess programming community for their excellent resources:
- [Chess Programming Wiki](https://www.chessprogramming.org/)
- [Bruce Moreland's Programming Topics](https://web.archive.org/web/20071026090003/http://www.brucemo.com/compchess/programming/index.htm)

## Author

**Marek Wesołowski** (WESMAR)  
📧 marek@wesolowski.eu.org  
🌐 [kvc.pl](https://kvc.pl)

---

*Made with ♟️ in C++20 | AVE!*
