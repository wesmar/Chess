// UCIMain.cpp
// Entry point for UCI chess engine executable (ChessEngineUCI.exe)
//
// This is a console application that implements the UCI protocol.
// It can be loaded into any UCI-compatible GUI like Arena, Cutechess, or Fritz.
//
// How to use with Arena:
// 1. Open Arena
// 2. Go to Engines -> Install New Engine
// 3. Select ChessEngineUCI.exe
// 4. Arena will auto-detect it as a UCI engine
// 5. Start a new game or analysis mode
//
// The engine reads commands from stdin and writes responses to stdout.
// All communication is text-based, one command per line.

#include "../Engine/Zobrist.h"
#include "UCIEngine.h"

int main()
{
    // Initialize Zobrist hashing tables before any board operations
    // This must happen exactly once per process
    Chess::Zobrist::Initialize();

    // Create and run the UCI engine
    // This enters the main loop waiting for GUI commands
    Chess::UCIEngine engine;
    engine.Run();

    return 0;
}
