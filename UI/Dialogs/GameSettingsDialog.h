// GameSettingsDialog.h
#pragma once

#include "../ChessGame.h"
#include <windows.h>
#include <string>

namespace Chess
{
    // Game settings dialog - allows configuration of all game options
    class GameSettingsDialog
    {
    public:
        struct Settings
        {
            // Board settings
            bool flipBoard = false;
            bool showCoordinates = true;
            bool showLegalMoves = true;
            bool animateMoves = true;
            int animationSpeed = 300;
            
            // Game settings
            GameMode gameMode = GameMode::HumanVsComputer;
            DifficultyLevel aiDifficulty = 3;
            bool autoPromoteQueen = true;
            int numThreads = 4;

            // Appearance settings
            std::wstring language = L"English";
            COLORREF lightSquareColor = RGB(240, 240, 245);
            COLORREF darkSquareColor = RGB(70, 80, 100);
            bool showPieceShadows = true;
            
            // Player settings
            std::wstring whitePlayerName = L"Player 1";
            std::wstring blackPlayerName = L"Player 2";
        };
        
        // Show modal settings dialog and return true if OK was pressed
        static bool Show(HWND parent, Settings& settings);
        
        // Load/save settings from/to INI file
        static void LoadFromINI(Settings& settings);
        static void SaveToINI(const Settings& settings);
        
    private:
        static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        static void InitializeDialog(HWND hwnd, Settings* settings);
        static void OnTabChanged(HWND hwnd, int tabIndex);
        static bool ApplySettings(HWND hwnd, Settings* settings);
    };
}