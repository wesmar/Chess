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
#include "../testing/EngineRegression.h"
#include "../testing/DataGen.h"
#include "../Engine/Neural/Network.h"
#include "../testing/NnueCheck.h"

int main(int argc, char** argv)
{
    // Initialize Zobrist hashing tables before any board operations
    // This must happen exactly once per process
    Chess::Zobrist::Initialize();
    if (argc > 1 && std::string(argv[1]) == "--selftest")
        return Chess::EngineRegression::Run();
    if (argc > 2 && std::string(argv[1]) == "--eval")
    {
        Chess::Board board(argv[2]);
        std::cout << Chess::Evaluate(board) << '\n';
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--kpk-dump")
    {
        const auto& table = Chess::KPK::GetTable();
        for (int p = 0; p < 24; ++p)
            for (int turn = 0; turn < 2; ++turn)
                for (int wk = 0; wk < 64; ++wk)
                    for (int bk = 0; bk < 64; ++bk)
                    {
                        const auto d = table.Probe(wk, bk, (p / 4 + 1) * 8 + p % 4, turn);
                        std::cout << (d == Chess::KPK::INVALID ? 'x' : (d == 0 ? '0' : '1'));
                    }
        std::cout << '\n';
        return 0;
    }
    if (argc > 4 && std::string(argv[1]) == "--histstats")
        return Chess::EngineRegression::HistoryStats(argv[2], std::stoi(argv[3]), std::stoi(argv[4]));
    // Quantised network evaluation of one position, for cross-checking the
    // C++ inference against the trainer's float forward pass.
    //   --nnue-eval <net.bin> <fen>
    if (argc > 3 && std::string(argv[1]) == "--nnue-eval")
    {
        Chess::Neural::Network net;
        if (!net.Load(argv[2]))
        {
            std::cerr << net.GetError() << "\n";
            return 1;
        }
        Chess::Board board(argv[3]);
        std::cout << net.Evaluate(board) << '\n';
        return 0;
    }

    // Incremental accumulator against full recomputation, over a real move
    // tree. Any difference is a bug in the incremental update.
    //   --nnue-check <net.bin> <depth>
    if (argc > 3 && std::string(argv[1]) == "--nnue-check")
        return Chess::NnueCheck::Run(argv[2], std::stoi(argv[3]));

    //   --nnue-bench <net.bin> <depth>   full vs incremental, same tree
    if (argc > 3 && std::string(argv[1]) == "--nnue-bench")
        return Chess::NnueCheck::Bench(argv[2], std::stoi(argv[3]));

    // Evaluate the first N positions of a packed file straight from their
    // stored feature indices. The verifier compares these against the
    // trainer's float forward pass over the very same indices, so any
    // difference is in the quantised arithmetic and nowhere else.
    //   --nnue-eval-packed <net.bin> <packed.bin> <count>
    if (argc > 4 && std::string(argv[1]) == "--nnue-eval-packed")
    {
        Chess::Neural::Network net;
        if (!net.Load(argv[2])) { std::cerr << net.GetError() << "\n"; return 1; }
        std::ifstream packed(argv[3], std::ios::binary);
        if (!packed) { std::cerr << "cannot open packed file\n"; return 1; }
        const long long wanted = std::stoll(argv[4]);
        Chess::PackedPosition rec{};
        for (long long i = 0; i < wanted; ++i)
        {
            if (!packed.read(reinterpret_cast<char*>(&rec), sizeof(rec))) break;
            std::cout << net.EvaluateFeatures(rec.stm, rec.countStm,
                                              rec.opp, rec.countOpp) << "\n";
        }
        return 0;
    }

    // Text training data -> packed network inputs.
    //   --nnue-pack <in.txt> <out.bin>
    if (argc > 3 && std::string(argv[1]) == "--nnue-pack")
        return Chess::DataGen::Pack(argv[2], argv[3]);

    // Self-play generator for NNUE training data. Diagnostic path only.
    //   --gendata <out.txt> <games> <depth> [seed]
    if (argc > 4 && std::string(argv[1]) == "--gendata")
    {
        Chess::DataGen::Config cfg;
        cfg.output = argv[2];
        cfg.games = std::stoi(argv[3]);
        cfg.depth = std::stoi(argv[4]);
        if (argc > 5) cfg.seed = static_cast<uint32_t>(std::stoul(argv[5]));
        return Chess::DataGen::Run(cfg);
    }
    if (argc > 3 && std::string(argv[1]) == "--direct")
    {
        Chess::Board board(argv[2]);
        Chess::AIPlayer ai(10);
        ai.SetThreads(1);
        ai.SetHashSize(64);
        ai.SetOwnBook(false);
        ai.SetInfoCallback([](const std::string& info) { std::cout << info << '\n'; });
        const auto move = ai.CalculateBestMove(board, 60000, std::stoi(argv[3]));
        std::cout << "bestmove " << move.ToUCI() << '\n';
        return 0;
    }

    // Create and run the UCI engine
    // This enters the main loop waiting for GUI commands
    Chess::UCIEngine engine;
    engine.Run();

    return 0;
}
