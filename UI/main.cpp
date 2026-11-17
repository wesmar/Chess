// main.cpp
// Application entry point for Modern Chess - WinAPI C++20 chess application
// 
// This is the Windows GUI entry point (wWinMain) which:
// 1. Initializes COM library for file dialogs (Open/Save)
// 2. Creates singleton ChessApplication instance
// 3. Runs the main message loop
// 4. Cleans up COM on exit
//
// Build targets: x86/x64, Release/Release_MinSize
// Dependencies: user32, gdi32, comctl32, comdlg32, shell32, ole32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "WinApp.h"
#include <Windows.h>
#include <shellapi.h>
#include "../testing/EngineRegression.h"
#include "../Engine/Zobrist.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    PWSTR pCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    if (std::wstring(pCmdLine) == L"--selftest")
    {
        Chess::Zobrist::Initialize();
        return Chess::EngineRegression::Run();
    }
    int argc = 0;
    auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv && argc > 3 && std::wstring(argv[1]) == L"--direct")
    {
        Chess::Zobrist::Initialize();
        std::wstring wideFen(argv[2]);
        std::string fen;
        for (wchar_t ch : wideFen) fen.push_back(static_cast<char>(ch)); // FEN is ASCII
        Chess::Board board(fen);
        Chess::AIPlayer ai(10);
        ai.SetThreads(1);
        ai.SetHashSize(64);
        ai.SetOwnBook(false);
        ai.SetInfoCallback([](const std::string& info) { std::cout << info << '\n'; });
        const auto move = ai.CalculateBestMove(board, 60000, std::stoi(argv[3]));
        std::cout << "bestmove " << move.ToUCI() << '\n';
        LocalFree(argv);
        return 0;
    }
    if (argv) LocalFree(argv);
    // Initialize COM for file dialogs (if needed)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    
    // Run the application
    Chess::ChessApplication& app = Chess::ChessApplication::GetInstance();
    int result = app.Run(hInstance, nCmdShow);
    
    // Cleanup
    CoUninitialize();
    
    return result;
}
