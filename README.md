# ♔ Modern Chess Engine

![Chess Engine Screenshot](Images/chess01.jpg)

A lightweight, educational chess engine written in modern C++20 with pure WinAPI interface. Perfect for learning how chess engines work without diving through hundreds of thousands of lines of code.

[![Windows](https://img.shields.io/badge/Windows-10%2B-blue.svg)](https://www.microsoft.com/windows)
[![C++](https://img.shields.io/badge/C%2B%2B-20-orange.svg)](https://isocpp.org/)
[![License](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Build](https://img.shields.io/badge/build-passing-brightgreen.svg)](https://github.com/wesmar/Chess/releases)

## Why This Project?

Most chess engines are either too simple (lacking modern techniques) or too complex (thousands of files, external dependencies). This project aims to be **educational yet powerful** - implementing professional chess engine techniques in clean, readable code.

This is a case study in building a high-performance chess system using **Data-Oriented Design (DOD)** principles, maximizing computational throughput while drastically reducing binary size and eliminating all external library dependencies.

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
- **Principal Variation Search (PVS)** - optimizes alpha-beta by assuming first move is best
- **Iterative Deepening** - finds best move within time limit
- **Aspiration Windows** - narrows search window based on previous iteration score for faster cutoffs
- **Quiescence Search** - eliminates horizon effect by searching tactical sequences

#### Pruning Techniques
- **Null Move Pruning (NMP)** - skips branches where opponent can't improve even with two consecutive moves; disabled in endgames to avoid zugzwang errors
- **Late Move Reduction (LMR)** - reduces depth for quiet moves late in move list, with re-search on alpha improvement
- **Late Move Pruning (LMP)** - completely skips late quiet moves at shallow depths
- **Futility Pruning** - skips nodes unlikely to raise alpha based on static evaluation margin
- **Razoring** - reduces depth when position evaluation far exceeds beta
- **Delta Pruning** - in quiescence search, skips captures that can't possibly raise alpha
- **Mate Distance Pruning** - stops searching for longer mates when shorter one already found

#### Search Extensions
- **Check Extensions** - extends search depth when side to move is in check

#### Position Caching
- **Zobrist Hashing** - lightning-fast incremental position comparison using XOR-sum
- **Transposition Table** - avoids re-analyzing positions with striped locking for thread safety
- **Threefold Repetition Detection** - recognizes draw by repetition during search

#### Move Ordering Heuristics
- **MVV-LVA (Most Valuable Victim - Least Valuable Attacker)** - prioritizes high-value captures
- **SEE (Static Exchange Evaluation)** - evaluates capture sequences to order winning captures first
- **Killer Move Heuristic** - remembers quiet moves that caused beta cutoffs at each ply
- **History Heuristic** - side-specific scoring for quiet moves that historically performed well
- **Center Control Bonus** - tactical bonus for moves targeting central squares

#### Parallel Search
- **Root-Parallel Search** - multi-threaded search with dynamic load balancing and shared alpha
- **Thread-Local Heuristics** - separate killer move tables per thread to prevent data races

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
- **Incremental Score Maintenance** - material + PST score updated on each move
- **Bitboard Occupancy Tracking** - hybrid architecture for fast sliding piece queries

#### Hybrid Board Representation
- **64-byte Mailbox (Cache-Aligned)** - entire board state in single L1 cache line
- **Bitboards for Occupancy** - fast sliding piece move generation without expensive lookups
- **~800KB savings** compared to Magic Bitboards approach

### 📊 Hand-Crafted Evaluation (HCE)

A tuned evaluation function with tapered scoring:

#### Material & Positional
- **Piece-Square Tables (PST)** - separate middlegame and endgame tables
- **Tapered Evaluation** - smooth interpolation based on game phase (256 = opening, 0 = endgame)
- **Bishop Pair Bonus** - +40 centipawns for having both bishops

#### King Safety (Middlegame)
- **Castling Position Bonus** - rewards castled king placement
- **Pawn Shield Analysis** - evaluates pawns protecting the king
- **Open File Penalty** - penalizes missing pawns near king

#### Piece Activity
- **Mobility Evaluation** - counts accessible squares for sliding pieces using bitboard occupancy
- **Center Control Bonus** - rewards knights and bishops on central squares
- **Exposed Queen Penalty** - penalizes queen on attacked square

#### Pawn Structure
- **Isolated Pawn Penalty** - pawns with no friendly pawns on adjacent files
- **Doubled Pawn Penalty** - multiple pawns on same file
- **Backward Pawn Penalty** - pawns that cannot safely advance

#### Passed Pawns
- **Advancement Bonus** - exponentially increasing value as pawn advances
- **King Distance Factor** - bonus when friendly king is close, penalty when enemy king is close
- **Rook Behind Passed Pawn** - bonus for rook supporting from behind

#### Tempo
- **Side to Move Bonus** - small advantage for having the move (+10 centipawns)

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
│   ├── Evaluation.cpp/h      # Position evaluation with PST tables
│   ├── Move.cpp/h            # Move representation (32-bit packed)
│   ├── MoveGenerator.cpp/h   # Legal move generation
│   ├── OpeningBook.cpp/h     # Hardcoded opening book
│   ├── Piece.h               # Piece representation (8-bit packed)
│   ├── TranspositionTable.cpp/h # Hash table for position caching
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
│   ├── ChessGame.cpp/h       # Game state management and AI
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
├── build_all.bat             # Build script for all configurations
├── Chess.vcxproj             # Main GUI application project
├── ChessEngineUCI.vcxproj    # UCI console engine project
└── Chess.slnx                # Visual Studio solution
```

## Learning the Code

If you're new to chess programming, here's a suggested reading order:

1. **Start with `Board.cpp`** - This is where all the chess magic happens (piece movement, rule validation)
2. **Then `Evaluation.cpp`** - Learn how the computer "thinks" about positions
3. **Move to `MoveGenerator.cpp`** - See how legal moves are generated
4. **Finally `ChessGame.cpp`** - Understand the AI's decision-making process (alpha-beta search with parallel search)

### Key Concepts Explained

**Mailbox vs Bitboards**: While bitboards are faster for some operations, the mailbox representation (64-element array) provides better cache locality for iterative search algorithms, which dominate chess engine runtime.

**Piece-Square Tables (PST)**: Arrays that assign values to pieces based on their position. For example, knights are worth more in the center than on the edge.

**Alpha-Beta Pruning**: If you find a move that's already better than the best option your opponent has, you don't need to check other moves in that branch.

**Null Move Pruning**: If giving the opponent a free move still doesn't help them, the position is so good we can skip detailed analysis of this branch.

**Late Move Reduction**: Moves that appear worse (ordered late) are searched to a shallower depth initially. If they turn out to be good, we re-search them at full depth.

**Transposition Table**: Chess positions can be reached through different move orders. The table remembers positions we've already evaluated, with striped locking for thread-safe parallel access.

**Root-Parallel Search**: Multiple threads search different root moves simultaneously with dynamic work distribution, avoiding the complexity of shared search trees.

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

## 🚀 Roadmap

### Completed ✅
- [x] Null Move Pruning (NMP) implementation
- [x] Late Move Reduction (LMR) with re-search
- [x] Late Move Pruning (LMP)
- [x] Futility Pruning
- [x] Razoring
- [x] Aspiration Windows
- [x] Delta Pruning in Quiescence
- [x] Mate Distance Pruning
- [x] Check Extensions
- [x] SEE (Static Exchange Evaluation)
- [x] Cache-aligned memory structures
- [x] Stack-allocated MoveList
- [x] Root-parallel search with dynamic load balancing
- [x] NNUE infrastructure (HybridEvaluator, NeuralEvaluator, etc.)

### In Progress 🔄
- [ ] NNUE weight training and integration
- [ ] Enhanced King Safety evaluation (tropism, attack units)
- [ ] Singular Extensions

### Future 📋
- [ ] Syzygy tablebase support
- [ ] MultiPV analysis mode
- [ ] Time management improvements

## Challenge

**Can you beat difficulty level 3?** If you can consistently win at level 3, you're playing at a decent amateur level. 🏆

## FAQ

**Q: Why WinAPI and not cross-platform?**  
A: This is an educational project focused on simplicity and extreme optimization. WinAPI provides everything needed without extra abstraction layers. A cross-platform version would require SDL/SFML, adding complexity and size.

**Q: Can it beat me?**  
A: It has 10 difficulty levels ranging from beginner-friendly (random moves) to challenging (10-ply search with full heuristics). Try different levels to find your match!

**Q: Can I modify it for my own learning?**  
A: Absolutely! That's the point. The code is well-commented and structured for learning. Fork it, break it, improve it!

**Q: Does it work on Windows 7/8?**  
A: The code uses Windows 10+ APIs, but could be adapted for older systems with minor changes to the WinAPI calls.

**Q: Why not use a UCI protocol?**  
A: The project includes both! There's a UCI console engine (`ChessEngineUCI.exe`) that works with Arena and other GUIs, plus the main standalone WinAPI application. The standalone version is the focus since it's more educational and shows the complete implementation, but advanced users can compile the UCI version to use the engine with their favorite chess GUI.

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

This project was created during Christmas 2025/2026 as an educational chess engine demonstrating Data-Oriented Design principles in modern C++. Special thanks to the chess programming community for their excellent resources:
- [Chess Programming Wiki](https://www.chessprogramming.org/)
- [Bruce Moreland's Programming Topics](https://web.archive.org/web/20071026090003/http://www.brucemo.com/compchess/programming/index.htm)

## Author

**Marek Wesołowski** (WESMAR)  
📧 marek@wesolowski.eu.org  
🌐 [kvc.pl](https://kvc.pl)

---

*Made with ♟️ in C++20 | AVE!*
