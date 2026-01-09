// GameSettingsDialog.h
// Game settings dialog interface with tabbed configuration panels
// Provides comprehensive settings management for board display, game mode, AI, and players
#pragma once

#include "../ChessGame.h"
#include <windows.h>
#include <string>

namespace Chess
{
    // Game settings dialog controller class
    // Displays modal tabbed dialog with four configuration sections:
    // - Board: flip, coordinates, legal moves, animation
    // - Game: mode selection, AI difficulty
    // - Appearance: language, visual effects
    // - Players: name customization
    class GameSettingsDialog
    {
    public:
        // Settings data structure holding all configurable options
        // Loaded from/saved to settings.ini file for persistence
        struct Settings
        {
            // ---------- Board Display Settings ----------
            bool flipBoard = false;           // View board from black's perspective
            bool showCoordinates = true;      // Display file/rank labels (a-h, 1-8)
            bool showLegalMoves = true;       // Highlight valid move destinations
            bool animateMoves = true;         // Enable piece movement animation
            int animationSpeed = 300;         // Animation duration in milliseconds
            
            // ---------- Game Settings ----------
            GameMode gameMode = GameMode::HumanVsComputer;  // Current game mode
            DifficultyLevel aiDifficulty = 3;               // AI strength (1-10)
            bool autoPromoteQueen = true;                   // Skip promotion dialog
            int numThreads = 4;                             // AI search thread count
            bool useNeuralEval = false;                     // Use neural network evaluation
            int maxUndoDepth = 3;                           // Maximum undo moves (1-3)

            // ---------- Appearance Settings ----------
            std::wstring language = L"English";                      // UI language
            COLORREF lightSquareColor = RGB(240, 240, 245);          // Light square RGB
            COLORREF darkSquareColor = RGB(70, 80, 100);             // Dark square RGB
            bool showPieceShadows = true;                            // Enable piece shadows
            
            // ---------- Player Settings ----------
            std::wstring whitePlayerName = L"Player 1";   // White player display name
            std::wstring blackPlayerName = L"Player 2";   // Black player display name
        };
        
        // Show modal settings dialog
        // Parameters:
        //   parent   - Handle to parent window
        //   settings - Settings structure (modified in-place if OK clicked)
        // Returns: true if user clicked OK, false if cancelled
        static bool Show(HWND parent, Settings& settings);
        
        // Load settings from INI configuration file
        // Reads from settings.ini in executable directory
        static void LoadFromINI(Settings& settings);
        
        // Save settings to INI configuration file
        // Writes to settings.ini in executable directory
        static void SaveToINI(const Settings& settings);
        
    private:
        // Windows dialog procedure callback for message handling
        static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        
        // Initialize dialog controls with current settings values
        static void InitializeDialog(HWND hwnd, Settings* settings);
        
        // Handle tab selection change - show/hide appropriate controls
        static void OnTabChanged(HWND hwnd, int tabIndex);
        
        // Read control values and update settings structure
        // Returns: true if all values successfully read
        static bool ApplySettings(HWND hwnd, Settings* settings);
    };
}
