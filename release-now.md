**Download:** `Chess.7z` (${SIZE_7Z}) — password: `github.com`

A competitive chess engine + GUI in modern C++20. Cache-line-aligned mailbox
architecture (no bitboard blueprint copying), lock-free transposition table,
full modern search arsenal — in binaries small enough to run from WinRE.

## What's inside

| File | Purpose |
|---|---|
| `Chess_x64.exe` | GUI application (64-bit, recommended) |
| `Chess_x64_minSize.exe` | GUI, minimal CRT-free build (~260 KB) |
| `Chess_x86[_minSize].exe` | 32-bit GUI builds |
| `ChessEngineUCI_x64.exe` | UCI engine for Arena / Cutechess / Fritz (AVX2) |
| `ChessEngineUCI_*` | UCI engine variants (x64/x86, minSize) |
| `lang/` | UI translations (en, pl, de, fr) |
| `README.txt` | Setup guide (GUI + UCI options) |

## Engine strength

**~2160 Elo** at 1 thread, **~2300 Elo** at 4 threads (Level 10, classical
eval) — single-thread figure measured over 200 games at 8s+0.08s vs
Stockfish 17 limited to UCI_Elo 2200; the 4-thread Lazy SMP gain measured
at **+147 Elo** (69.4% over 116 games, LOS 100%).

## Highlights of this release

- **~+550 Elo** vs the original December baseline, every change validated by
  engine-vs-engine SPRT matches (methodology in the repo README)
- **NEW: Lazy SMP** — persistent thread pool, helpers search the whole root
  at staggered depths, coordinating only through the shared lock-free TT;
  NPS ×4.1 at 4 threads, +1 ply at equal time
- **NEW: Continuation history** — reply-pair move ordering (−15% tree)
- PVS at every node, aspiration windows with progressive widening
- Staged move picker with lazy SEE (+20% NPS)
- Lock-free 4-way cache-line TT buckets + prefetch after make (HFT-style)
- Lazy legality checking, O(1) occupancy restore in undo
- Endgame knowledge: mop-up, dead-draw and OCB scaling, 50-move rule in search
- Bounded time-management jitter (timeout latch — zero time forfeits in testing)

## Quick UCI setup

```
Threads = physical cores | Hash = 128-256 MB | Level = 10
Move Overhead = 30-50 ms (local) / 100-200 ms (online)
```

Full Arena walkthrough: [`arena.txt`](https://github.com/${REPO}/blob/main/arena.txt)

---
Sources: https://github.com/${REPO} | MIT License | (c) WESMAR 2026
