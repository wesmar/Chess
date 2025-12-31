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

#include "WinApp.h"
#include <Windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    PWSTR pCmdLine, int nCmdShow)
{
    (void)hPrevInstance;
    (void)pCmdLine;
    // Initialize COM for file dialogs (if needed)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    
    // Run the application
    Chess::ChessApplication& app = Chess::ChessApplication::GetInstance();
    int result = app.Run(hInstance, nCmdShow);
    
    // Cleanup
    CoUninitialize();
    
    return result;
}