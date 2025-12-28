// WinApp.h
#pragma once

#include "ChessGame.h"
#include "VectorRenderer.h"
#include <windows.h>
#include <memory>
#include <string>
#include <future>

namespace Chess
{
    // ---------- Window Messages ----------
    constexpr UINT WM_AI_MOVE_COMPLETE = WM_USER + 1;
    constexpr UINT WM_GAME_STATE_CHANGED = WM_USER + 2;

    // ---------- Main Application Window ----------
    class MainWindow
    {
    public:
        MainWindow();
        ~MainWindow();

        bool Create(HINSTANCE hInstance, int nCmdShow);
        void RunMessageLoop();

        HWND GetHandle() const { return m_hwnd; }
        ChessGame& GetGame() { return m_game; }
        ChessPieceRenderer& GetRenderer() { return m_renderer; }

        // ---------- Public Methods ----------
        void NewGame();
        void UndoMove();
        void RedoMove();
        void SetGameMode(GameMode mode);
        void SetAIDifficulty(DifficultyLevel difficulty);
        void SaveGame();
        void LoadGame();
        void ClearSelection();

        // ---------- Event Handlers ----------
        void OnPaint();
        void OnSize(int width, int height);
        void OnLButtonDown(int x, int y);
        void OnRButtonDown(int x, int y);
        void OnKeyDown(WPARAM keyCode);
        void OnTimer();

    private:
        // ---------- Window Management ----------
        HWND m_hwnd = nullptr;
        HINSTANCE m_hInstance = nullptr;
        std::wstring m_windowTitle = L"Modern Chess - C++20";
        
        static constexpr int WINDOW_WIDTH = 1000;
        static constexpr int WINDOW_HEIGHT = 700;
        static constexpr wchar_t WINDOW_CLASS_NAME[] = L"ModernChessWindowClass";

        // ---------- Game Components ----------
        ChessGame m_game;
        ChessPieceRenderer m_renderer;
        
		// ---------- UI State ----------
		struct UISettings
		{
			bool showLegalMoves = true;
			bool showCoordinates = true;
			bool flipBoard = false;
			bool autoPromoteToQueen = true;
			bool animateMoves = true;
			int animationSpeed = 300;
		};

		UISettings m_uiSettings;

		// Current game mode and difficulty for menu checkmarks
		GameMode m_currentGameMode = GameMode::HumanVsComputer;
		DifficultyLevel m_currentDifficulty = 3;
        
        // ---------- Selection & Animation ----------
        int m_selectedSquare = -1;
        std::vector<int> m_highlightedSquares;
        
        struct Animation
        {
            int fromSquare = -1;
            int toSquare = -1;
            Piece piece = EMPTY_PIECE;
            DWORD startTime = 0;
            int duration = 0;
            bool inProgress = false;
        };
        
        Animation m_currentAnimation;
        
        // ---------- Timing ----------
        UINT_PTR m_timerId = 0;
        constexpr static UINT TIMER_INTERVAL = 16;
        
        // ---------- AI Processing ----------
        bool m_aiThinking = false;
        std::future<Move> m_aiFuture;
        
        // ---------- Menu & Dialogs ----------
        HMENU m_hMenu = nullptr;
        
        // ---------- Private Methods ----------
        bool RegisterWindowClass();
        bool CreateMainWindow(int nCmdShow);
        void CreateMenu();
        void CreateStatusBar();
        void UpdateStatusBar();
        
        // ---------- Message Handlers ----------
        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
        
        // ---------- Drawing ----------
        void DrawBoard(HDC hdc, const RECT& clientRect);
        void DrawSidebar(HDC hdc, const RECT& clientRect);
        void DrawGameInfo(HDC hdc, const RECT& rect);
        void DrawMoveHistory(HDC hdc, const RECT& rect);
        void DrawControls(HDC hdc, const RECT& rect);
        
        // ---------- Input Processing ----------
        int ScreenToBoardSquare(int x, int y) const;
        RECT GetBoardRect(const RECT& clientRect) const;
        
        // ---------- Game Logic ----------
        void ProcessSquareClick(int square);
        void ProcessPromotion(int from, int to);
        void CompleteAIMove();
        void CheckGameEnd();
        
        // ---------- File Operations ----------
        std::wstring OpenFileDialog(bool save);
        void SaveGameToFile(const std::wstring& filename);
        void LoadGameFromFile(const std::wstring& filename);
        
        // ---------- Animation ----------
        void StartAnimation(int fromSquare, int toSquare, Piece piece);
        void UpdateAnimation();
        void DrawAnimatedPiece(HDC hdc, const RECT& boardRect);
    };

    // ---------- Application Class ----------
    class ChessApplication
    {
    public:
        static ChessApplication& GetInstance();
        
        int Run(HINSTANCE hInstance, int nCmdShow);
        void Exit();
        
        MainWindow& GetMainWindow() { return *m_mainWindow; }
        HINSTANCE GetInstanceHandle() const { return m_hInstance; }
        
    private:
        ChessApplication();
        ~ChessApplication();
        
        HINSTANCE m_hInstance = nullptr;
        std::unique_ptr<MainWindow> m_mainWindow;
        
        ChessApplication(const ChessApplication&) = delete;
        ChessApplication& operator=(const ChessApplication&) = delete;
    };

    // ---------- Utility Functions ----------
    std::wstring StringToWString(const std::string& str);
    std::string WStringToString(const std::wstring& wstr);
    
    COLORREF GetColorForPlayer(PlayerColor color);
    std::wstring GetPlayerName(PlayerColor color);
    
    // ---------- Dialog Helpers ----------
	class PromotionDialog
	{
	public:
		static PieceType Show(HWND parent, PlayerColor color);
		
	private:
		static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	};

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
            
            // Appearance settings
            std::wstring language = L"English";
            COLORREF lightSquareColor = RGB(240, 240, 245);
            COLORREF darkSquareColor = RGB(70, 80, 100);
            bool showPieceShadows = true;
            
            // Player settings
            std::wstring whitePlayerName = L"Player 1";
            std::wstring blackPlayerName = L"Player 2";
        };
        
        static bool Show(HWND parent, Settings& settings);
        static void LoadFromINI(Settings& settings);
        static void SaveToINI(const Settings& settings);
        
    private:
        static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
        static void InitializeDialog(HWND hwnd, Settings* settings);
        static void OnTabChanged(HWND hwnd, int tabIndex);
        static bool ApplySettings(HWND hwnd, Settings* settings);
    };
}