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
- **High Performance**: ~2.5M nodes/second per core

### 🧠 Advanced Search Algorithms
The engine employs sophisticated search reduction techniques to maximize depth:
- **Minimax with Alpha-Beta Pruning** - efficient move tree search
- **Principal Variation Search (PVS)** - optimizes alpha-beta pruning by assuming the first move is best
- **Iterative Deepening** - finds best move within time limit
- **Null Move Pruning (NMP)** - aggressively prunes branches where the opponent cannot improve their position even if allowed two moves in a row (huge ELO gain)
- **Late Move Reduction (LMR)** - reduces search depth for quiet moves late in the move list, allowing deeper search in critical lines
- **Zobrist Hashing** - lightning-fast position comparison using incremental XOR-sum
- **Transposition Table** - avoids re-analyzing same positions with striped locking
- **Quiescence Search** - eliminates horizon effect
- **Move Ordering (MVV-LVA)** - prioritizes captures for faster pruning
- **Killer Move Heuristic** - remembers good non-capture moves
- **History Heuristic** - side-specific move scoring
- **Root-Parallel Search** - multi-threaded search with dynamic load balancing
- **Opening Book** - hardcoded main line positions

### ⚡ High-Performance Move Generation
- **Hybrid Representation**: 64-byte Mailbox (Cache-Aligned) for board state + Bitboards for occupancy queries
- **Bitwise Sliding Generators**: Uses advanced bit-twiddling (e.g., Hyperbola Quintessence concepts) to generate rook/bishop moves without large lookup tables (~800KB saved compared to Magic Bitboards)
- **Branchless Logic**: Heavy use of bitwise operations to minimize branch misprediction penalties

### 📊 Hand-Crafted Evaluation (HCE)
A tuned evaluation function focusing on:
- **Material & Position**: Piece-Square Tables (PST) interpolated between opening and endgame phases
- **Pawn Structure**: Penalties for isolated, doubled, and backward pawns
- **King Safety**: Analysis of pawn shield integrity and open files near the king
- **Mobility**: Bonuses for piece activity and center control
- **Passed Pawns**: Special evaluation with king distance and rook placement considerations
- **Tapered Evaluation**: Smooth blending of middlegame/endgame scores based on game phase

### 🗝️ Architecture & Design Choices

#### Memory Architecture: Cache-Locality First
I deliberately chose a **64-byte "Mailbox" array representation** over bitboards, aligned to L1 cache lines. This decision minimizes cache misses during iterative game tree traversal - the most critical performance bottleneck in chess engines.

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
- **Small Footprint**: The entire engine logic compiles to <150 KB, making it suitable for embedded environments or recovery tools (WinRE)

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

- **Search Speed**: ~2.5 million nodes/second per core (CPU-dependent)
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

## 🚀 Roadmap

- [x] Null Move Pruning (NMP) implementation
- [x] Late Move Reduction (LMR) implementation
- [x] Cache-aligned memory structures
- [x] Root-parallel search with dynamic load balancing
- [ ] LMR fine-tuning with depth-based reduction
- [ ] Enhanced King Safety evaluation
- [ ] Aspiration windows for search optimization
- [ ] Futility pruning and razoring improvements

## Challenge

**Can you beat difficulty level 3?** If you can consistently win at level 3, you're playing at a decent amateur level. Levels 7+ are quite challenging! 🏆

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
A: UCI adds complexity for engine-GUI communication. This is a standalone application focused on being self-contained and educational.

## Contributing

Found a bug? Have an improvement idea? Feel free to:
1. Open an issue describing the problem/suggestion
2. Fork the repository
3. Create a pull request

Please keep changes focused and well-documented!

## License

MIT License - see [LICENSE](LICENSE) file for details.

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