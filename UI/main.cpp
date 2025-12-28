// main.cpp
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