// WinApp.h
// Main application window and Windows message handling
// Implements chess board UI with sidebar, animation, and AI integration
#pragma once

#include "ChessGame.h"
#include "VectorRenderer.h"
#include <windows.h>
#include <memory>
#include <string>
#include <future>

namespace Chess
{
    // ========== CUSTOM WINDOW MESSAGES ==========
    // Posted from background threads to main UI thread
    constexpr UINT WM_AI_MOVE_COMPLETE = WM_USER + 1;      // AI finished calculating move
    constexpr UINT WM_GAME_STATE_CHANGED = WM_USER + 2;    // Game state updated (check, mate, etc.)

    // ========== MAIN APPLICATION WINDOW ==========
    // Primary game window with board rendering, sidebar UI, and event handling
    // Manages user input, AI integration, animation system, and file operations
    class MainWindow
    {
    public:
        MainWindow();
        ~MainWindow();

        // Initialize and create window
        // @param hInstance: Application instance handle
        // @param nCmdShow: Window show command (SW_SHOW, etc.)
        // @return: true if window created successfully
        bool Create(HINSTANCE hInstance, int nCmdShow);
        
        // Run Windows message loop until application exit
        void RunMessageLoop();

        // Window and component accessors
        HWND GetHandle() const { return m_hwnd; }
        ChessGame& GetGame() { return m_game; }
        ChessPieceRenderer& GetRenderer() { return m_renderer; }

        // ========== PUBLIC ACTIONS ==========
        
        void NewGame();                                 // Start new game with current settings
        void UndoMove();                                // Undo last move
        void RedoMove();                                // Redo previously undone move
        void SetGameMode(GameMode mode);                // Change game mode (human/AI configuration)
        void SetAIDifficulty(DifficultyLevel difficulty); // Update AI strength
        void SaveGame();                                // Show save dialog and save game
        void LoadGame();                                // Show open dialog and load game
        void ClearSelection();                          // Clear selected square

        // ========== EVENT HANDLERS ==========
        // Called by window procedure for specific events
        
        void OnPaint();                                 // Render board and UI
        void OnSize(int width, int height);             // Handle window resize
        void OnLButtonDown(int x, int y);               // Handle left mouse click
        void OnRButtonDown(int x, int y);               // Handle right mouse click
        void OnKeyDown(WPARAM keyCode);                 // Handle keyboard input
        void OnTimer();                                 // Update animation and AI state

    private:
        // ========== WINDOW MANAGEMENT ==========
        HWND m_hwnd = nullptr;                          // Window handle
        HINSTANCE m_hInstance = nullptr;                // Application instance
        std::wstring m_windowTitle = L"Modern Chess - C++20";
        
        static constexpr int WINDOW_WIDTH = 1000;       // Default window width
        static constexpr int WINDOW_HEIGHT = 700;       // Default window height
        static constexpr wchar_t WINDOW_CLASS_NAME[] = L"ModernChessWindowClass";

        // ========== GAME COMPONENTS ==========
        ChessGame m_game;                               // Game logic controller
        ChessPieceRenderer m_renderer;                  // Board and piece renderer
        
        // ========== UI SETTINGS ==========
        // User interface preferences loaded from INI file
        struct UISettings
        {
            bool showLegalMoves = true;                 // Highlight valid move destinations
            bool showCoordinates = true;                // Show a-h and 1-8 labels
            bool flipBoard = false;                     // View from black's perspective
            bool autoPromoteToQueen = true;             // Auto-promote pawns to queen
            bool animateMoves = true;                   // Enable smooth move animation
            int animationSpeed = 300;                   // Animation duration in milliseconds
        };

        UISettings m_uiSettings;

        // Current game configuration for menu checkmarks
        GameMode m_currentGameMode = GameMode::HumanVsComputer;
        DifficultyLevel m_currentDifficulty = 3;
        bool m_useNeuralEval = false;
        
        // ========== SELECTION & ANIMATION ==========
        int m_selectedSquare = -1;                      // Currently selected square (-1 = none)
        std::vector<int> m_highlightedSquares;          // Squares to highlight (legal moves)
        
        // Move animation state
        struct Animation
        {
            int fromSquare = -1;                        // Source square
            int toSquare = -1;                          // Destination square
            Piece piece = EMPTY_PIECE;                  // Piece being animated
            DWORD startTime = 0;                        // Animation start time (GetTickCount)
            int duration = 0;                           // Animation duration (ms)
            bool inProgress = false;                    // Animation active flag
        };
        
        Animation m_currentAnimation;
        
        // ========== TIMING ==========
        UINT_PTR m_timerId = 0;                         // Timer ID for periodic updates
        constexpr static UINT TIMER_INTERVAL = 16;     // ~60 FPS update rate
        
        // ========== AI PROCESSING ==========
        bool m_aiThinking = false;                      // AI calculation in progress
        std::future<Move> m_aiFuture;                   // Async AI move calculation
        
        // ========== MENU & DIALOGS ==========
        HMENU m_hMenu = nullptr;                        // Main menu handle
        
        // ========== WINDOW CREATION ==========
        bool RegisterWindowClass();                     // Register WNDCLASSEX
        bool CreateMainWindow(int nCmdShow);            // Create and show window
        void CreateMenu();                              // Create menu bar
        void LocalizeMenu();                            // Apply localized strings to menu
        void CreateStatusBar();                         // Create status bar at bottom
        void UpdateStatusBar();                         // Update status bar text
        
        // ========== MESSAGE HANDLING ==========
        // Windows message procedure dispatch
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
        
        // ========== RENDERING ==========
        void DrawBoard(HDC hdc, const RECT& clientRect);        // Render chess board
        void DrawSidebar(HDC hdc, const RECT& clientRect);      // Render sidebar UI
        void DrawGameInfo(HDC hdc, const RECT& rect);           // Game state and player info
        void DrawMoveHistory(HDC hdc, const RECT& rect);        // Move list display
        void DrawControls(HDC hdc, const RECT& rect);           // Button controls (reserved)
        
        // ========== INPUT PROCESSING ==========
        // Convert screen coordinates to board square index
        int ScreenToBoardSquare(int x, int y) const;
        
        // Calculate board rectangle within window (centered, leaves space for sidebar)
        RECT GetBoardRect(const RECT& clientRect) const;
        
        // ========== GAME LOGIC ==========
        void ProcessSquareClick(int square);            // Handle square selection and move execution
        void ProcessPromotion(int from, int to);        // Show promotion dialog and execute
        void CompleteAIMove();                          // Execute AI move from async result
        void CheckGameEnd();                            // Check for checkmate/stalemate/draw
        
        // ========== FILE OPERATIONS ==========
        std::wstring OpenFileDialog(bool save);         // Show open/save file dialog
        void SaveGameToFile(const std::wstring& filename);
        void LoadGameFromFile(const std::wstring& filename);
        
        // ========== ANIMATION ==========
        void StartAnimation(int fromSquare, int toSquare, Piece piece);
        void UpdateAnimation();                         // Update animation progress
        void DrawAnimatedPiece(HDC hdc, const RECT& boardRect); // Render piece at interpolated position
    };

    // ========== APPLICATION CLASS ==========
    // Singleton application controller - manages window lifecycle
    class ChessApplication
    {
    public:
        // Get singleton instance
        static ChessApplication& GetInstance();
        
        // Initialize and run application
        // @param hInstance: Application instance handle from WinMain
        // @param nCmdShow: Window show command
        // @return: Application exit code
        int Run(HINSTANCE hInstance, int nCmdShow);
        
        // Exit application (post quit message)
        void Exit();
        
        MainWindow& GetMainWindow() { return *m_mainWindow; }
        HINSTANCE GetInstanceHandle() const { return m_hInstance; }
        
    private:
        ChessApplication();
        ~ChessApplication();
        
        HINSTANCE m_hInstance = nullptr;
        std::unique_ptr<MainWindow> m_mainWindow;
        
        // Prevent copying
        ChessApplication(const ChessApplication&) = delete;
        ChessApplication& operator=(const ChessApplication&) = delete;
    };
}