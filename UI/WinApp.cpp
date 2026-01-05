// WinApp.cpp
// Main Windows application window and event handling
// Implements game window with board rendering, sidebar UI, move input,
// animation system, AI integration, menu handling, and file operations

#include "WinApp.h"
#include "Dialogs/PromotionDialog.h"
#include "Dialogs/GameSettingsDialog.h"
#include "WinUtility.h"
#include "../Resources/Resource.h"
#include <windows.h>
#include <windowsx.h>
#include <commdlg.h>
#include <commctrl.h>
#include <sstream>
#include <iomanip>
#include <algorithm>

#pragma comment(lib, "comctl32.lib")

// Undefine Windows macros that conflict with std::min/max
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

namespace Chess
{
    // ---------- MainWindow Implementation ----------
	MainWindow::MainWindow()
		: m_game()
		, m_renderer()
	{
		// Load settings from INI file at startup
		GameSettingsDialog::Settings settings;
		GameSettingsDialog::LoadFromINI(settings);
		
		// Apply loaded UI preferences
		m_uiSettings.showLegalMoves = settings.showLegalMoves;
		m_uiSettings.showCoordinates = settings.showCoordinates;
		m_uiSettings.flipBoard = settings.flipBoard;
		m_uiSettings.autoPromoteToQueen = settings.autoPromoteQueen;
		m_uiSettings.animateMoves = settings.animateMoves;
		m_uiSettings.animationSpeed = settings.animationSpeed;
		
		// Store current game mode and difficulty for menu management
		m_currentGameMode = settings.gameMode;
		m_currentDifficulty = settings.aiDifficulty;
		
		// Configure renderer with loaded visual settings
		auto config = m_renderer.GetConfig();
		config.showCoordinates = settings.showCoordinates;
		config.useShadow = settings.showPieceShadows;
		config.flipBoard = settings.flipBoard;
		config.lightSquareColor = settings.lightSquareColor;
		config.darkSquareColor = settings.darkSquareColor;
		m_renderer.SetConfig(config);
	}

    MainWindow::~MainWindow()
    {
        // Clean up timer if active
        if (m_timerId != 0)
        {
            KillTimer(m_hwnd, m_timerId);
        }
        
        // Release menu resources
        if (m_hMenu != nullptr)
        {
            DestroyMenu(m_hMenu);
        }
    }

	bool MainWindow::Create(HINSTANCE hInstance, int nCmdShow)
	{
		m_hInstance = hInstance;
		
		// Register window class for our chess application
		if (!RegisterWindowClass())
		{
			return false;
		}
		
		// Create the main window
		if (!CreateMainWindow(nCmdShow))
		{
			return false;
		}
		
		// Set up menu and status bar UI elements
		CreateMenu();
		CreateStatusBar();
		
		// Start update timer for animations and AI processing
		m_timerId = SetTimer(m_hwnd, 1, TIMER_INTERVAL, nullptr);

		// Configure AI thread count from settings
		GameSettingsDialog::Settings settings;
		GameSettingsDialog::LoadFromINI(settings);
		m_game.GetCurrentAIPlayer()->SetThreads(settings.numThreads);

		// Initialize game with correct mode after window is ready
		m_game.SetGameMode(m_currentGameMode);
		m_game.NewGame(m_currentGameMode);
		
		return true;
	}

    void MainWindow::RunMessageLoop()
    {
        // Standard Windows message pump
        MSG msg = {};
        while (GetMessage(&msg, nullptr, 0, 0))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    bool MainWindow::RegisterWindowClass()
    {
        // Define window class properties
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.style = CS_HREDRAW | CS_VREDRAW; // Redraw on resize
        wc.lpfnWndProc = MainWindow::WindowProc;
        wc.cbClsExtra = 0;
        wc.cbWndExtra = 0;
        wc.hInstance = m_hInstance;
        wc.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(101));
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = CreateSolidBrush(RGB(30, 32, 38)); // Dark background
        wc.lpszMenuName = nullptr;
        wc.lpszClassName = WINDOW_CLASS_NAME;
        wc.hIconSm = LoadIcon(m_hInstance, MAKEINTRESOURCE(101));
        
        return RegisterClassEx(&wc) != 0;
    }

    bool MainWindow::CreateMainWindow(int nCmdShow)
    {
        // Calculate window size including borders and menu
        RECT rect = { 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT };
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, TRUE);
        
        // Create the main application window
        m_hwnd = CreateWindowEx(
            0,
            WINDOW_CLASS_NAME,
            m_windowTitle.c_str(),
            WS_OVERLAPPEDWINDOW,
            CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left,
            rect.bottom - rect.top,
            nullptr,
            nullptr,
            m_hInstance,
            this // Pass window instance for message handling
        );
        
        if (m_hwnd == nullptr)
        {
            return false;
        }
        
        // Display and update the window
        ShowWindow(m_hwnd, nCmdShow);
        UpdateWindow(m_hwnd);
        
        return true;
    }

	void MainWindow::CreateMenu()
	{
		// Load menu from resource file
		HMENU hMenu = LoadMenu(m_hInstance, MAKEINTRESOURCE(IDR_MAIN_MENU));
		if (hMenu)
		{
			SetMenu(m_hwnd, hMenu);
			m_hMenu = hMenu;
			
			// Synchronize menu checkmarks with loaded settings
			
			// Game Mode checkmarks
			CheckMenuItem(m_hMenu, IDM_MODE_HUMAN_VS_HUMAN, 
				m_currentGameMode == GameMode::HumanVsHuman ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(m_hMenu, IDM_MODE_HUMAN_VS_COMPUTER, 
				m_currentGameMode == GameMode::HumanVsComputer ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(m_hMenu, IDM_MODE_COMPUTER_VS_COMPUTER, 
				m_currentGameMode == GameMode::ComputerVsComputer ? MF_CHECKED : MF_UNCHECKED);
			
			// Difficulty level checkmarks (clear all, then check current)
			for (int i = 1; i <= 10; ++i)
			{
				CheckMenuItem(m_hMenu, IDM_DIFFICULTY_LEVEL_1 + i - 1, MF_UNCHECKED);
			}
			CheckMenuItem(m_hMenu, IDM_DIFFICULTY_LEVEL_1 + m_currentDifficulty - 1, MF_CHECKED);
			
			// View options checkmarks
			CheckMenuItem(m_hMenu, IDM_VIEW_FLIPBOARD, 
				m_uiSettings.flipBoard ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(m_hMenu, IDM_VIEW_SHOWLEGAL, 
				m_uiSettings.showLegalMoves ? MF_CHECKED : MF_UNCHECKED);
			CheckMenuItem(m_hMenu, IDM_VIEW_SHOWCOORDS, 
				m_uiSettings.showCoordinates ? MF_CHECKED : MF_UNCHECKED);
		}
	}
	void MainWindow::CreateStatusBar()
    {
        // Create status bar at bottom of window
        HWND hStatusBar = CreateWindowEx(
            0,
            STATUSCLASSNAME,
            nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0,
            m_hwnd,
            nullptr,
            m_hInstance,
            nullptr
        );

        if (hStatusBar)
        {
            // Divide status bar into 3 equal parts
            int parts[3];
            RECT rect;
            GetClientRect(m_hwnd, &rect);

            int width = rect.right - rect.left;
            parts[0] = width / 3;
            parts[1] = 2 * width / 3;
            parts[2] = -1; // Last part extends to end

            SendMessage(hStatusBar, SB_SETPARTS, 3, (LPARAM)parts);
            UpdateStatusBar();
        }
    }

    void MainWindow::UpdateStatusBar()
    {
        // Find status bar control
        HWND hStatusBar = FindWindowEx(m_hwnd, nullptr, STATUSCLASSNAME, nullptr);
        if (!hStatusBar) return;

        // Get current game state text
        GameState state = m_game.GetGameState();
        std::wstring gameStateText = StringToWString(GameStateToString(state));

        // Display current player
        std::wstring playerText = L"Current: ";
        playerText += (m_game.GetCurrentPlayer() == PlayerColor::White) ? L"White" : L"Black";

        // Display move count
        auto moves = m_game.GetMoveHistory();
        std::wstring moveText = L"Moves: " + std::to_wstring(moves.size());

        // Update all three status bar sections
        SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)gameStateText.c_str());
        SendMessage(hStatusBar, SB_SETTEXT, 1, (LPARAM)playerText.c_str());
        SendMessage(hStatusBar, SB_SETTEXT, 2, (LPARAM)moveText.c_str());
    }

    LRESULT CALLBACK MainWindow::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        MainWindow* pThis = nullptr;
        
        // On window creation, store window instance pointer
        if (msg == WM_NCCREATE)
        {
            CREATESTRUCT* pCreate = (CREATESTRUCT*)lParam;
            pThis = (MainWindow*)pCreate->lpCreateParams;
            SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)pThis);
            pThis->m_hwnd = hwnd;
        }
        else
        {
            // Retrieve stored window instance pointer
            pThis = (MainWindow*)GetWindowLongPtr(hwnd, GWLP_USERDATA);
        }
        
        // Dispatch to instance message handler
        if (pThis)
        {
            return pThis->HandleMessage(msg, wParam, lParam);
        }
        
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    LRESULT MainWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_CREATE:
            return 0;
            
        case WM_PAINT:
            OnPaint();
            return 0;
            
        case WM_ERASEBKGND:
            // Skip background erase - we handle it in double-buffered OnPaint
            return 1;
            
        case WM_SIZE:
        {
            int width = LOWORD(lParam);
            int height = HIWORD(lParam);
            OnSize(width, height);
            return 0;
        }
            
        case WM_LBUTTONDOWN:
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnLButtonDown(x, y);
            return 0;
        }
            
        case WM_RBUTTONDOWN:
        {
            int x = GET_X_LPARAM(lParam);
            int y = GET_Y_LPARAM(lParam);
            OnRButtonDown(x, y);
            return 0;
        }
            
        case WM_KEYDOWN:
            OnKeyDown(wParam);
            return 0;
            
        case WM_TIMER:
            OnTimer();
            return 0;
            
        case WM_COMMAND:
        {
            int cmdId = LOWORD(wParam);
            switch (cmdId)
            {
            // File menu commands
            case IDM_FILE_NEW:
                NewGame();
                break;

            case IDM_FILE_OPEN:
                LoadGame();
                break;

            case IDM_FILE_SAVE:
                SaveGame();
                break;

            case IDM_FILE_EXIT:
                PostMessage(m_hwnd, WM_CLOSE, 0, 0);
                break;

            // Game menu commands
            case IDM_GAME_UNDO:
                UndoMove();
                break;

            case IDM_GAME_REDO:
                RedoMove();
                break;

			// Game mode selection - Human vs Human
			case IDM_MODE_HUMAN_VS_HUMAN:
				CheckMenuItem(m_hMenu, IDM_MODE_HUMAN_VS_HUMAN, MF_CHECKED);
				CheckMenuItem(m_hMenu, IDM_MODE_HUMAN_VS_COMPUTER, MF_UNCHECKED);
				CheckMenuItem(m_hMenu, IDM_MODE_COMPUTER_VS_COMPUTER, MF_UNCHECKED);
				SetGameMode(GameMode::HumanVsHuman);
				
				// Persist mode to INI file
				{
					GameSettingsDialog::Settings settings;
					GameSettingsDialog::LoadFromINI(settings);
					settings.gameMode = GameMode::HumanVsHuman;
					GameSettingsDialog::SaveToINI(settings);
				}
				
				NewGame();
				break;

			// Game mode selection - Human vs Computer
			case IDM_MODE_HUMAN_VS_COMPUTER:
				CheckMenuItem(m_hMenu, IDM_MODE_HUMAN_VS_HUMAN, MF_UNCHECKED);
				CheckMenuItem(m_hMenu, IDM_MODE_HUMAN_VS_COMPUTER, MF_CHECKED);
				CheckMenuItem(m_hMenu, IDM_MODE_COMPUTER_VS_COMPUTER, MF_UNCHECKED);
				SetGameMode(GameMode::HumanVsComputer);
				
				// Persist mode to INI file
				{
					GameSettingsDialog::Settings settings;
					GameSettingsDialog::LoadFromINI(settings);
					settings.gameMode = GameMode::HumanVsComputer;
					GameSettingsDialog::SaveToINI(settings);
				}
				
				NewGame();
				break;

			// Game mode selection - Computer vs Computer
			case IDM_MODE_COMPUTER_VS_COMPUTER:
				CheckMenuItem(m_hMenu, IDM_MODE_HUMAN_VS_HUMAN, MF_UNCHECKED);
				CheckMenuItem(m_hMenu, IDM_MODE_HUMAN_VS_COMPUTER, MF_UNCHECKED);
				CheckMenuItem(m_hMenu, IDM_MODE_COMPUTER_VS_COMPUTER, MF_CHECKED);
				SetGameMode(GameMode::ComputerVsComputer);
				
				// Persist mode to INI file
				{
					GameSettingsDialog::Settings settings;
					GameSettingsDialog::LoadFromINI(settings);
					settings.gameMode = GameMode::ComputerVsComputer;
					GameSettingsDialog::SaveToINI(settings);
				}
				
				NewGame();
				break;

			// Difficulty level selection (10 levels)
			case IDM_DIFFICULTY_LEVEL_1:
			case IDM_DIFFICULTY_LEVEL_2:
			case IDM_DIFFICULTY_LEVEL_3:
			case IDM_DIFFICULTY_LEVEL_4:
			case IDM_DIFFICULTY_LEVEL_5:
			case IDM_DIFFICULTY_LEVEL_6:
			case IDM_DIFFICULTY_LEVEL_7:
			case IDM_DIFFICULTY_LEVEL_8:
			case IDM_DIFFICULTY_LEVEL_9:
			case IDM_DIFFICULTY_LEVEL_10:
			{
				int level = cmdId - IDM_DIFFICULTY_LEVEL_1 + 1;
				
				// Update menu checkmarks for all difficulty levels
				for (int i = 1; i <= 10; ++i)
				{
					CheckMenuItem(m_hMenu, IDM_DIFFICULTY_LEVEL_1 + i - 1, MF_UNCHECKED);
				}
				
				// Check selected difficulty level
				CheckMenuItem(m_hMenu, cmdId, MF_CHECKED);
				
				SetAIDifficulty(level);
				
				// Persist difficulty to INI file
				{
					GameSettingsDialog::Settings settings;
					GameSettingsDialog::LoadFromINI(settings);
					settings.aiDifficulty = level;
					GameSettingsDialog::SaveToINI(settings);
				}
				
				break;
			}
			// Game settings dialog
			case IDM_GAME_SETTINGS:
			{
				GameSettingsDialog::Settings settings;
				
				// Load current settings into dialog
				settings.flipBoard = m_uiSettings.flipBoard;
				settings.showCoordinates = m_uiSettings.showCoordinates;
				settings.showLegalMoves = m_uiSettings.showLegalMoves;
				settings.animateMoves = m_uiSettings.animateMoves;
				settings.animationSpeed = m_uiSettings.animationSpeed;
				settings.gameMode = m_currentGameMode;
				settings.aiDifficulty = m_currentDifficulty;
				settings.autoPromoteQueen = m_uiSettings.autoPromoteToQueen;
				
				// Show settings dialog and apply changes if user clicked OK
				if (GameSettingsDialog::Show(m_hwnd, settings))
				{
					// Apply UI settings
					m_uiSettings.flipBoard = settings.flipBoard;
					m_uiSettings.showCoordinates = settings.showCoordinates;
					m_uiSettings.showLegalMoves = settings.showLegalMoves;
					m_uiSettings.animateMoves = settings.animateMoves;
					m_uiSettings.animationSpeed = settings.animationSpeed;
					m_uiSettings.autoPromoteToQueen = settings.autoPromoteQueen;
					
					// Update game mode if changed
					if (settings.gameMode != m_currentGameMode)
					{
						m_currentGameMode = settings.gameMode;
						
						// Update menu checkmarks for game mode
						CheckMenuItem(m_hMenu, IDM_MODE_HUMAN_VS_HUMAN, 
							settings.gameMode == GameMode::HumanVsHuman ? MF_CHECKED : MF_UNCHECKED);
						CheckMenuItem(m_hMenu, IDM_MODE_HUMAN_VS_COMPUTER, 
							settings.gameMode == GameMode::HumanVsComputer ? MF_CHECKED : MF_UNCHECKED);
						CheckMenuItem(m_hMenu, IDM_MODE_COMPUTER_VS_COMPUTER, 
							settings.gameMode == GameMode::ComputerVsComputer ? MF_CHECKED : MF_UNCHECKED);
					}
					
					// Update difficulty if changed
					if (settings.aiDifficulty != m_currentDifficulty)
					{
						// Clear all difficulty checkmarks
						for (int i = 1; i <= 10; ++i)
						{
							CheckMenuItem(m_hMenu, IDM_DIFFICULTY_LEVEL_1 + i - 1, MF_UNCHECKED);
						}
						CheckMenuItem(m_hMenu, IDM_DIFFICULTY_LEVEL_1 + settings.aiDifficulty - 1, MF_CHECKED);
						
						m_currentDifficulty = settings.aiDifficulty;
						SetAIDifficulty(settings.aiDifficulty);
					}
					
					// Update menu checkmarks for view options
					CheckMenuItem(m_hMenu, IDM_VIEW_FLIPBOARD, settings.flipBoard ? MF_CHECKED : MF_UNCHECKED);
					CheckMenuItem(m_hMenu, IDM_VIEW_SHOWLEGAL, settings.showLegalMoves ? MF_CHECKED : MF_UNCHECKED);
					CheckMenuItem(m_hMenu, IDM_VIEW_SHOWCOORDS, settings.showCoordinates ? MF_CHECKED : MF_UNCHECKED);
					
					// Update renderer configuration
					auto config = m_renderer.GetConfig();
					config.showCoordinates = settings.showCoordinates;
					config.useShadow = settings.showPieceShadows;
					m_renderer.SetConfig(config);
					
					// Redraw window with new settings
					InvalidateRect(m_hwnd, nullptr, TRUE);
				}
				break;
			}

			// View menu - Flip board
			case IDM_VIEW_FLIPBOARD:
				m_uiSettings.flipBoard = !m_uiSettings.flipBoard;
				CheckMenuItem(m_hMenu, cmdId, m_uiSettings.flipBoard ? MF_CHECKED : MF_UNCHECKED);
				
				// Update renderer to match UI setting
				{
					auto config = m_renderer.GetConfig();
					config.flipBoard = m_uiSettings.flipBoard;
					m_renderer.SetConfig(config);
				}
				
				// Persist setting to INI file
				{
					GameSettingsDialog::Settings settings;
					GameSettingsDialog::LoadFromINI(settings);
					settings.flipBoard = m_uiSettings.flipBoard;
					GameSettingsDialog::SaveToINI(settings);
				}
				
				InvalidateRect(m_hwnd, nullptr, TRUE);
				break;

			// View menu - Show legal moves
			case IDM_VIEW_SHOWLEGAL:
				m_uiSettings.showLegalMoves = !m_uiSettings.showLegalMoves;
				CheckMenuItem(m_hMenu, cmdId, m_uiSettings.showLegalMoves ? MF_CHECKED : MF_UNCHECKED);
				
				// Persist setting to INI file
				{
					GameSettingsDialog::Settings settings;
					GameSettingsDialog::LoadFromINI(settings);
					settings.showLegalMoves = m_uiSettings.showLegalMoves;
					GameSettingsDialog::SaveToINI(settings);
				}
				
				InvalidateRect(m_hwnd, nullptr, TRUE);
				break;

			// View menu - Show coordinates
			case IDM_VIEW_SHOWCOORDS:
			{
				m_uiSettings.showCoordinates = !m_uiSettings.showCoordinates;
				CheckMenuItem(m_hMenu, cmdId, m_uiSettings.showCoordinates ? MF_CHECKED : MF_UNCHECKED);
				
				// Update renderer configuration
				auto config = m_renderer.GetConfig();
				config.showCoordinates = m_uiSettings.showCoordinates;
				m_renderer.SetConfig(config);
				
				// Persist setting to INI file
				{
					GameSettingsDialog::Settings settings;
					GameSettingsDialog::LoadFromINI(settings);
					settings.showCoordinates = m_uiSettings.showCoordinates;
					GameSettingsDialog::SaveToINI(settings);
				}
				
				InvalidateRect(m_hwnd, nullptr, TRUE);
				break;
			}

			// Help menu - About dialog
            case IDM_HELP_ABOUT:
                MessageBox(m_hwnd,
                    L"Modern Chess\n"
                    L"C++20 WinAPI Application\n\n"
                    L"Using Unicode chess pieces ♔♕♖♗♘♙\n\n"
                    L"Author: Marek Wesołowski\n"
                    L"WESMAR https://kvc.pl\n"
                    L"marek@wesolowski.eu.org",
                    L"About Modern Chess",
                    MB_OK | MB_ICONINFORMATION);
                break;
            }
            return 0;
        }
            
        case WM_CLOSE:
            DestroyWindow(m_hwnd);
            return 0;
            
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
            
        // Custom message - AI move completed
        case WM_AI_MOVE_COMPLETE:
            CompleteAIMove();
            return 0;
            
        // Custom message - Game state changed
        case WM_GAME_STATE_CHANGED:
            CheckGameEnd();
            UpdateStatusBar();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            return 0;
            
        default:
            return DefWindowProc(m_hwnd, msg, wParam, lParam);
        }
    }

    void MainWindow::OnPaint()
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);
        
        // Get window dimensions
        RECT clientRect;
        GetClientRect(m_hwnd, &clientRect);
        
        // Create memory DC for double buffering (eliminates flicker)
        HDC hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hBitmap = CreateCompatibleBitmap(hdc, clientRect.right, clientRect.bottom);
        HBITMAP hOldBitmap = (HBITMAP)SelectObject(hdcMem, hBitmap);
        
        // Draw to memory buffer
        DrawBoard(hdcMem, clientRect);
        DrawSidebar(hdcMem, clientRect);
        
        // Draw animated piece on top if animation is in progress
        if (m_currentAnimation.inProgress)
        {
            RECT boardRect = GetBoardRect(clientRect);
            DrawAnimatedPiece(hdcMem, boardRect);
        }
        
        // Copy buffer to screen (double buffer flip)
        BitBlt(hdc, 0, 0, clientRect.right, clientRect.bottom, hdcMem, 0, 0, SRCCOPY);
        
        // Clean up GDI objects
        SelectObject(hdcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hdcMem);
        
        EndPaint(m_hwnd, &ps);
    }

    void MainWindow::OnSize(int width, int)
    {
        // Resize status bar to match window width
        HWND hStatusBar = FindWindowEx(m_hwnd, nullptr, STATUSCLASSNAME, nullptr);
        if (hStatusBar)
        {
            SendMessage(hStatusBar, WM_SIZE, 0, 0);

            // Recalculate status bar part widths
            int parts[3];
            parts[0] = width / 3;
            parts[1] = 2 * width / 3;
            parts[2] = -1;

            SendMessage(hStatusBar, SB_SETPARTS, 3, (LPARAM)parts);
        }

        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void MainWindow::OnLButtonDown(int x, int y)
    {
        // Don't accept input during animation or AI thinking
        if (m_currentAnimation.inProgress)
            return;
            
        // Convert screen coordinates to board square
        int square = ScreenToBoardSquare(x, y);
        if (square != -1)
        {
            ProcessSquareClick(square);
        }
    }

    void MainWindow::OnRButtonDown(int, int)
    {
        // Right click clears selection
        ClearSelection();
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

    void MainWindow::OnKeyDown(WPARAM keyCode)
    {
        switch (keyCode)
        {
        case VK_ESCAPE:
            ClearSelection();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            break;
            
        // Ctrl+Z - Undo
        case 'Z':
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
            {
                UndoMove();
            }
            break;
            
        // Ctrl+Y - Redo
        case 'Y':
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
            {
                RedoMove();
            }
            break;
            
        // Ctrl+N - New game
        case 'N':
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
            {
                NewGame();
            }
            break;
            
		// F key - Flip board
		case 'F':
			m_uiSettings.flipBoard = !m_uiSettings.flipBoard;
			
			// Update renderer configuration to match
			{
				auto config = m_renderer.GetConfig();
				config.flipBoard = m_uiSettings.flipBoard;
				m_renderer.SetConfig(config);
			}
			
			InvalidateRect(m_hwnd, nullptr, TRUE);
			break;
        }
    }
	void MainWindow::OnTimer()
	{
		// Update animation frame
		if (m_currentAnimation.inProgress)
		{
			UpdateAnimation();
			InvalidateRect(m_hwnd, nullptr, FALSE);
		}
		
		// Start AI thinking if it's AI's turn and not already thinking
		if (m_game.IsAITurn() && !m_aiThinking && !m_currentAnimation.inProgress)
		{
			// Don't start AI if game is already over
			GameState state = m_game.GetGameState();
			if (state == GameState::Checkmate || 
				state == GameState::Stalemate || 
				state == GameState::Draw)
			{
				return;
			}
			
			m_aiThinking = true;
			
			// Calculate AI move asynchronously to avoid blocking UI
			m_aiFuture = std::async(std::launch::async, [this]() {
				Board boardCopy = m_game.GetBoard();
				return m_game.GetCurrentAIPlayer()->CalculateBestMove(boardCopy, 5000);
			});
		}
		
		// Check if AI has finished calculating move
		if (m_aiThinking && m_aiFuture.valid())
		{
			if (m_aiFuture.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready)
			{
				PostMessage(m_hwnd, WM_AI_MOVE_COMPLETE, 0, 0);
			}
		}
	}

    void MainWindow::DrawBoard(HDC hdc, const RECT& clientRect)
    {
        RECT boardRect = GetBoardRect(clientRect);
        
        // Prepare list of squares to highlight (legal moves)
        std::vector<int> highlightedSquares;
        if (m_uiSettings.showLegalMoves && m_selectedSquare != -1)
        {
            highlightedSquares = m_game.GetValidTargetSquares(m_selectedSquare);
        }
        
        // Render board using vector renderer
        m_renderer.RenderBoard(hdc, clientRect, 
                               m_game.GetBoard().GetPieces(),
                               m_selectedSquare,
                               highlightedSquares);
    }

    void MainWindow::DrawSidebar(HDC hdc, const RECT& clientRect)
    {
        // Calculate sidebar dimensions
        int sidebarWidth = 280;
        RECT sidebarRect = {
            clientRect.right - sidebarWidth,
            clientRect.top,
            clientRect.right,
            clientRect.bottom
        };
        
        // Draw sidebar background
        HBRUSH hBrush = CreateSolidBrush(RGB(40, 42, 48));
        FillRect(hdc, &sidebarRect, hBrush);
        DeleteObject(hBrush);
        
        // Define areas for different sidebar sections
        RECT gameInfoRect = sidebarRect;
        gameInfoRect.bottom = sidebarRect.top + 180;
        gameInfoRect.left += 10;
        gameInfoRect.right -= 10;
        
        RECT moveHistoryRect = sidebarRect;
        moveHistoryRect.top = gameInfoRect.bottom + 20;
        moveHistoryRect.bottom = sidebarRect.bottom - 80;
        moveHistoryRect.left += 10;
        moveHistoryRect.right -= 10;
        
        // Draw sidebar content sections
        DrawGameInfo(hdc, gameInfoRect);
        DrawMoveHistory(hdc, moveHistoryRect);
    }

    void MainWindow::DrawGameInfo(HDC hdc, const RECT& rect)
    {
        // Create title font
        HFONT hTitleFont = CreateFont(24, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH, L"Segoe UI");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hTitleFont);
        
        SetTextColor(hdc, RGB(220, 220, 220));
        SetBkMode(hdc, TRANSPARENT);
        
        // Draw title with chess piece symbols
        RECT titleRect = rect;
        titleRect.top += 10;
        DrawText(hdc, L"♔ Modern Chess ♚", -1, &titleRect, DT_CENTER | DT_TOP);
        
        SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
        
        // Draw game state (check, checkmate, etc.)
        RECT stateRect = rect;
        stateRect.top += 50;
        
        GameState state = m_game.GetGameState();
        std::wstring stateText = StringToWString(GameStateToString(state));
        
        // Color checkmate text red for emphasis
        if (state == GameState::Checkmate)
        {
            stateText += std::wstring(L" - ") +
                ((m_game.GetCurrentPlayer() == PlayerColor::White) ?
                 L"Black Wins!" : L"White Wins!");
            SetTextColor(hdc, RGB(255, 100, 100));
        }
        else
        {
            SetTextColor(hdc, RGB(180, 180, 180));
        }
        
        DrawText(hdc, stateText.c_str(), -1, &stateRect, DT_CENTER | DT_TOP);
        
        // Draw current player indicator
        RECT playerRect = rect;
        playerRect.top += 85;
        
        std::wstring playerText = L"To move: ";
        playerText += (m_game.GetCurrentPlayer() == PlayerColor::White) ? L"White ♔" : L"Black ♚";
        
        // Show AI indicator if applicable
        if (m_game.IsAITurn())
        {
            playerText += L" (AI)";
        }
        
        SetTextColor(hdc, RGB(200, 200, 200));
        DrawText(hdc, playerText.c_str(), -1, &playerRect, DT_CENTER | DT_TOP);
        
        // Draw material evaluation
        RECT materialRect = rect;
        materialRect.top += 120;
        
        int material = m_game.GetBoard().EvaluateMaterial();
        std::wstring materialText = L"Material: ";
        if (material > 0)
            materialText += L"+";
        materialText += std::to_wstring(material);
        
        SetTextColor(hdc, RGB(150, 150, 150));
        DrawText(hdc, materialText.c_str(), -1, &materialRect, DT_CENTER | DT_TOP);
        
        SelectObject(hdc, hOldFont);
        DeleteObject(hTitleFont);
    }

    void MainWindow::DrawMoveHistory(HDC hdc, const RECT& rect)
    {
        // Draw separator line at top
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(60, 65, 75));
        HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
        
        MoveToEx(hdc, rect.left, rect.top, nullptr);
        LineTo(hdc, rect.right, rect.top);
        
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);
        
        // Draw section title
        HFONT hFont = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI");
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        
        SetTextColor(hdc, RGB(200, 200, 200));
        SetBkMode(hdc, TRANSPARENT);
        
        RECT titleRect = rect;
        titleRect.top += 10;
        DrawText(hdc, L"Move History", -1, &titleRect, DT_CENTER);
        
        // Switch to smaller font for move list
        SelectObject(hdc, GetStockObject(DEFAULT_GUI_FONT));
        SetTextColor(hdc, RGB(180, 180, 180));
        
        // Calculate how many moves fit in available space
        auto moves = m_game.GetMoveHistory();
        int maxMoves = (rect.bottom - rect.top - 50) / 22;
        int startMove = std::max(0, static_cast<int>(moves.size()) - maxMoves);
        
        // Draw move list (scrolling from bottom)
        for (size_t i = startMove; i < moves.size(); ++i)
        {
            int row = static_cast<int>(i - startMove);
            RECT moveRect = rect;
            moveRect.top += 45 + row * 22;
            moveRect.left += 10;
            
            // Format as "1. e2e4" with move number
            std::wstring moveText = std::to_wstring((i / 2) + 1) + L". ";
            moveText += StringToWString(moves[i].ToAlgebraic());
            
            DrawText(hdc, moveText.c_str(), -1, &moveRect, DT_LEFT);
        }
        
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
    }

    void MainWindow::DrawControls(HDC, const RECT&)
    {
        // Reserved for future control buttons
    }

	int MainWindow::ScreenToBoardSquare(int x, int y) const
	{
		// Get window and board dimensions
		RECT clientRect;
		GetClientRect(m_hwnd, &clientRect);
		RECT boardRect = GetBoardRect(clientRect);
		
		// Check if click is outside board area
		if (x < boardRect.left || x >= boardRect.right ||
			y < boardRect.top || y >= boardRect.bottom)
		{
			return -1;
		}
		
		// Convert pixel coordinates to file/rank
		int squareSize = (boardRect.right - boardRect.left) / BOARD_SIZE;
		int file = (x - boardRect.left) / squareSize;
		int rank = (y - boardRect.top) / squareSize;
		
		// Apply board flip transformation to match renderer
		if (m_uiSettings.flipBoard)
		{
			file = 7 - file;
			// Rank remains unchanged for flipped board
		}
		else
		{
			// Reverse rank for normal view (board drawn from top)
			rank = 7 - rank;
		}
		
		// Validate calculated square
		if (file < 0 || file >= 8 || rank < 0 || rank >= 8)
		{
			return -1;
		}
		
		return rank * 8 + file;
	}

    RECT MainWindow::GetBoardRect(const RECT& clientRect) const
    {
        // Calculate board size to fit window (leave space for sidebar)
        int boardSize = std::min(
            clientRect.right - clientRect.left - 300,
            clientRect.bottom - clientRect.top - 60
        );
        
        // Center board in available space
        int boardLeft = (clientRect.right - 300 - boardSize) / 2;
        int boardTop = (clientRect.bottom - boardSize) / 2 - 20;
        
        return {
            boardLeft,
            boardTop,
            boardLeft + boardSize,
            boardTop + boardSize
        };
    }
	void MainWindow::ProcessSquareClick(int square)
    {
        // Ignore clicks during animation or AI thinking
        if (m_currentAnimation.inProgress || m_aiThinking)
            return;
            
        // If a square is already selected, try to move to clicked square
        if (m_selectedSquare != -1 && m_selectedSquare != square)
        {
            // Check if clicked square is a valid destination
            auto validTargets = m_game.GetValidTargetSquares(m_selectedSquare);
            if (std::find(validTargets.begin(), validTargets.end(), square) != validTargets.end())
            {
                // Handle pawn promotion if applicable
                if (m_game.IsPromotionRequired(m_selectedSquare, square))
                {
                    ProcessPromotion(m_selectedSquare, square);
                }
                else
                {
                    // Execute move and start animation
                    if (m_game.MakeMove(m_selectedSquare, square))
                    {
                        Piece movedPiece = m_game.GetBoard().GetPieceAt(square);
                        StartAnimation(m_selectedSquare, square, movedPiece);
                        PostMessage(m_hwnd, WM_GAME_STATE_CHANGED, 0, 0);
                    }
                }
                m_selectedSquare = -1;
            }
            else
            {
                // Try selecting a different piece
                Piece clickedPiece = m_game.GetBoard().GetPieceAt(square);
                if (clickedPiece && clickedPiece.GetColor() == m_game.GetCurrentPlayer())
                {
                    m_selectedSquare = square;
                }
                else
                {
                    m_selectedSquare = -1;
                }
            }
        }
        else
        {
            // Select piece if it belongs to current player
            Piece clickedPiece = m_game.GetBoard().GetPieceAt(square);
            if (clickedPiece && clickedPiece.GetColor() == m_game.GetCurrentPlayer())
            {
                m_selectedSquare = square;
            }
            else
            {
                m_selectedSquare = -1;
            }
        }
        
        InvalidateRect(m_hwnd, nullptr, TRUE);
    }

	void MainWindow::ProcessPromotion(int from, int to)
	{
		PieceType promotionType = PieceType::Queen; // Default to queen
		
		// Show promotion dialog if auto-promotion is disabled
		if (!m_uiSettings.autoPromoteToQueen)
		{
			promotionType = PromotionDialog::Show(m_hwnd, m_game.GetCurrentPlayer());
		}
		
		// Execute promotion move
		if (m_game.MakeMove(from, to, promotionType))
		{
			// Animate the promoted piece if animations enabled
			if (m_uiSettings.animateMoves)
			{
				Piece promotedPiece = m_game.GetBoard().GetPieceAt(to);
				StartAnimation(from, to, promotedPiece);
			}
			
			PostMessage(m_hwnd, WM_GAME_STATE_CHANGED, 0, 0);
		}
	}

	void MainWindow::CompleteAIMove()
	{
		// Retrieve AI's calculated move from async future
		if (m_aiFuture.valid())
		{
			try
			{
				Move aiMove = m_aiFuture.get();
				
				// Validate that AI returned a legal move
				if (aiMove.GetFrom() != aiMove.GetTo())
				{
					if (m_game.MakeMove(aiMove))
					{
						// Animate AI's move if enabled
						if (m_uiSettings.animateMoves)
						{
							StartAnimation(aiMove.GetFrom(), aiMove.GetTo(), 
										 m_game.GetBoard().GetPieceAt(aiMove.GetTo()));
						}
						
						PostMessage(m_hwnd, WM_GAME_STATE_CHANGED, 0, 0);
					}
					else
					{
						// AI made illegal move - should never happen
						MessageBox(m_hwnd, L"AI tried to make illegal move!", L"Error", MB_OK | MB_ICONERROR);
					}
				}
				else
				{
					// AI has no moves available
					MessageBox(m_hwnd, L"AI has no moves available!", L"Info", MB_OK | MB_ICONINFORMATION);
				}
			}
			catch (const std::exception& e)
			{
				std::string error = "AI move failed: ";
				error += e.what();
				MessageBoxA(m_hwnd, error.c_str(), "AI Error", MB_OK | MB_ICONERROR);
			}
			catch (...)
			{
				MessageBox(m_hwnd, L"Unknown AI error occurred!", L"Error", MB_OK | MB_ICONERROR);
			}
		}
		
		m_aiThinking = false;
	}

    void MainWindow::CheckGameEnd()
    {
        GameState state = m_game.GetGameState();
        
        // Check if game has ended
        if (state == GameState::Checkmate || state == GameState::Stalemate || state == GameState::Draw)
        {
            std::wstring message;
            std::wstring title;
            
            // Prepare appropriate end-game message
            switch (state)
            {
            case GameState::Checkmate:
                title = L"Checkmate!";
                message = (m_game.GetCurrentPlayer() == PlayerColor::White) ?
					L"Black wins by checkmate!" : L"White wins by checkmate!";
                break;
                
            case GameState::Stalemate:
                title = L"Stalemate!";
                message = L"The game is a draw by stalemate.";
                break;
                
            case GameState::Draw:
                title = L"Draw!";
                message = L"The game is a draw.";
                break;
                
            default:
                return;
            }
            
            // Offer to start new game
            message += L"\n\nDo you want to start a new game?";
            
            int result = MessageBox(m_hwnd, message.c_str(), title.c_str(),
                                   MB_YESNO | MB_ICONINFORMATION);
            
            if (result == IDYES)
            {
                NewGame();
            }
        }
    }

    std::wstring MainWindow::OpenFileDialog(bool save)
    {
        wchar_t filename[MAX_PATH] = {0};
        
        // Configure file dialog
        OPENFILENAMEW ofn = {};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = m_hwnd;
        ofn.lpstrFilter = L"Chess Games (*.pgn)\0*.pgn\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile = filename;
        ofn.nMaxFile = MAX_PATH;
        ofn.Flags = OFN_EXPLORER | OFN_HIDEREADONLY;
        
        // Show save or open dialog
        if (save)
        {
            ofn.lpstrDefExt = L"pgn";
            ofn.Flags |= OFN_OVERWRITEPROMPT;
            if (GetSaveFileNameW(&ofn))
            {
                return filename;
            }
        }
        else
        {
            if (GetOpenFileNameW(&ofn))
            {
                return filename;
            }
        }
        
        return L"";
    }

    void MainWindow::SaveGameToFile(const std::wstring& filename)
    {
        // Convert wide string to narrow string for file operations
        std::string narrowFilename = WStringToString(filename);
        m_game.SavePGN(narrowFilename);
    }

    void MainWindow::LoadGameFromFile(const std::wstring& filename)
    {
        // Convert wide string and load PGN game
        std::string narrowFilename = WStringToString(filename);
        m_game.LoadPGN(narrowFilename);
        InvalidateRect(m_hwnd, nullptr, TRUE);
        UpdateStatusBar();
    }

    void MainWindow::StartAnimation(int fromSquare, int toSquare, Piece piece)
    {
        // Don't start animation if disabled in settings
        if (!m_uiSettings.animateMoves)
            return;
            
        // Initialize animation parameters
        m_currentAnimation.fromSquare = fromSquare;
        m_currentAnimation.toSquare = toSquare;
        m_currentAnimation.piece = piece;
        m_currentAnimation.startTime = GetTickCount();
        m_currentAnimation.duration = m_uiSettings.animationSpeed;
        m_currentAnimation.inProgress = true;
    }

    void MainWindow::UpdateAnimation()
    {
        if (!m_currentAnimation.inProgress)
            return;
            
        // Check if animation duration has elapsed
        DWORD elapsed = GetTickCount() - m_currentAnimation.startTime;
        if (elapsed >= static_cast<DWORD>(m_currentAnimation.duration))
        {
            m_currentAnimation.inProgress = false;
        }
    }

    void MainWindow::DrawAnimatedPiece(HDC hdc, const RECT& boardRect)
    {
        if (!m_currentAnimation.inProgress)
            return;
            
        // Calculate animation progress (0.0 to 1.0)
        DWORD elapsed = GetTickCount() - m_currentAnimation.startTime;
        float progress = std::min(1.0f, static_cast<float>(elapsed) / m_currentAnimation.duration);
        
        // Get file and rank for source and destination
        int fromFile = m_currentAnimation.fromSquare % 8;
        int fromRank = m_currentAnimation.fromSquare / 8;
        int toFile = m_currentAnimation.toSquare % 8;
        int toRank = m_currentAnimation.toSquare / 8;
        
		// Apply board flip transformation to match renderer
		if (m_uiSettings.flipBoard)
		{
			fromFile = 7 - fromFile;
			toFile = 7 - toFile;
			// Rank unchanged for flipped view
		}
		else
		{
			fromRank = 7 - fromRank;
			toRank = 7 - toRank;
		}
        
        // Calculate pixel positions
        int squareSize = (boardRect.right - boardRect.left) / BOARD_SIZE;
        
        int startX = boardRect.left + fromFile * squareSize + squareSize / 2;
        int startY = boardRect.top + fromRank * squareSize + squareSize / 2;
        int endX = boardRect.left + toFile * squareSize + squareSize / 2;
        int endY = boardRect.top + toRank * squareSize + squareSize / 2;
        
        // Interpolate position based on progress
        int currentX = startX + static_cast<int>((endX - startX) * progress);
        int currentY = startY + static_cast<int>((endY - startY) * progress);
        
        // Calculate piece rectangle centered at current position
        RECT pieceRect = {
            currentX - squareSize / 2,
            currentY - squareSize / 2,
            currentX + squareSize / 2,
            currentY + squareSize / 2
        };
        
        // Render piece at interpolated position
        m_renderer.RenderPiece(hdc, pieceRect, m_currentAnimation.piece);
    }

	void MainWindow::NewGame()
	{
		// Reset game state and UI
		m_game.NewGame(m_currentGameMode);
		m_selectedSquare = -1;
		m_currentAnimation.inProgress = false;
		m_aiThinking = false;
		
		InvalidateRect(m_hwnd, nullptr, TRUE);
		UpdateStatusBar();
	}

    void MainWindow::UndoMove()
    {
        // Undo last move and update display
        if (m_game.UndoMove())
        {
            m_selectedSquare = -1;
            InvalidateRect(m_hwnd, nullptr, TRUE);
            UpdateStatusBar();
        }
    }

    void MainWindow::RedoMove()
    {
        // Redo previously undone move
        if (m_game.RedoMove())
        {
            m_selectedSquare = -1;
            InvalidateRect(m_hwnd, nullptr, TRUE);
            UpdateStatusBar();
        }
    }

	void MainWindow::SetGameMode(GameMode mode)
	{
		// Update game mode in both window and game logic
		m_currentGameMode = mode;
		m_game.SetGameMode(mode);
	}

	void MainWindow::SetAIDifficulty(DifficultyLevel difficulty)
	{
		// Update AI difficulty for both players
		m_currentDifficulty = difficulty;
		m_game.SetAIDifficulty(PlayerColor::White, difficulty);
		m_game.SetAIDifficulty(PlayerColor::Black, difficulty);

		// Reload thread count from settings to respect user configuration
		GameSettingsDialog::Settings settings;
		GameSettingsDialog::LoadFromINI(settings);
		m_game.GetCurrentAIPlayer()->SetThreads(settings.numThreads);
	}

    void MainWindow::SaveGame()
    {
        // Show save dialog and save game to selected file
        std::wstring filename = OpenFileDialog(true);
        if (!filename.empty())
        {
            SaveGameToFile(filename);
        }
    }

    void MainWindow::LoadGame()
    {
        // Show open dialog and load game from selected file
        std::wstring filename = OpenFileDialog(false);
        if (!filename.empty())
        {
            LoadGameFromFile(filename);
        }
    }

    void MainWindow::ClearSelection()
    {
        m_selectedSquare = -1;
    }
	// ---------- ChessApplication Implementation ----------
    ChessApplication::ChessApplication()
        : m_mainWindow(std::make_unique<MainWindow>())
    {
    }

    ChessApplication::~ChessApplication()
    {
    }

    ChessApplication& ChessApplication::GetInstance()
    {
        // Singleton pattern - single application instance
        static ChessApplication instance;
        return instance;
    }

    int ChessApplication::Run(HINSTANCE hInstance, int nCmdShow)
    {
        m_hInstance = hInstance;
        
        // Initialize common controls for modern UI elements
        INITCOMMONCONTROLSEX icex;
        icex.dwSize = sizeof(INITCOMMONCONTROLSEX);
        icex.dwICC = ICC_WIN95_CLASSES;
        InitCommonControlsEx(&icex);
        
        // Initialize COM for file dialogs
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        
        // Create and show main window
        if (!m_mainWindow->Create(hInstance, nCmdShow))
        {
            MessageBox(nullptr, L"Failed to create main window", L"Error", MB_ICONERROR);
            return 1;
        }
        
        // Run message loop until application exit
        m_mainWindow->RunMessageLoop();
        
        // Clean up COM
        CoUninitialize();
        
        return 0;
    }

    void ChessApplication::Exit()
    {
        PostQuitMessage(0);
    }
}