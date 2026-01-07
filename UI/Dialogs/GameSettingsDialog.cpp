// GameSettingsDialog.cpp
// Game settings dialog implementation with tabbed interface
// Allows configuration of board display, game mode, AI difficulty, appearance, and player names
#include "GameSettingsDialog.h"
#include "../../Resources/Resource.h"
#include <commctrl.h>
#include <fstream>
#include <sstream>

namespace Chess
{
    // Helper function to set font for dialog controls
    // Ensures consistent typography across all UI elements
    static void SetControlFont(HWND hwndParent, int controlID, HFONT hFont)
    {
        HWND hControl = GetDlgItem(hwndParent, controlID);
        if (hControl)
        {
            SendMessage(hControl, WM_SETFONT, (WPARAM)hFont, TRUE);
        }
    }

    // Show modal settings dialog and return true if user clicked OK
    // settings parameter is modified in-place with new values
    bool GameSettingsDialog::Show(HWND parent, Settings& settings)
    {
        INT_PTR result = DialogBoxParam(
            GetModuleHandle(nullptr),
            MAKEINTRESOURCE(IDD_SETTINGS),
            parent,
            DialogProc,
            reinterpret_cast<LPARAM>(&settings)
        );
        
        return result == IDOK;
    }
    
    // Main dialog message handler
    // Routes Windows messages to appropriate handler functions
    INT_PTR CALLBACK GameSettingsDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        static Settings* pSettings = nullptr;
        
        switch (msg)
        {
        case WM_INITDIALOG:
        {
            // Store settings pointer for later access and initialize dialog controls
            pSettings = reinterpret_cast<Settings*>(lParam);
            SetWindowLongPtr(hwnd, GWLP_USERDATA, lParam);
            
            InitializeDialog(hwnd, pSettings);
            return TRUE;
        }
        
        case WM_NOTIFY:
        {
            // Handle tab control selection changes
            LPNMHDR nmhdr = reinterpret_cast<LPNMHDR>(lParam);
            if (nmhdr->idFrom == IDC_TAB_CONTROL && nmhdr->code == TCN_SELCHANGE)
            {
                HWND hTab = GetDlgItem(hwnd, IDC_TAB_CONTROL);
                int tabIndex = TabCtrl_GetCurSel(hTab);
                OnTabChanged(hwnd, tabIndex);
            }
            return TRUE;
        }
        
        case WM_HSCROLL:
        {
            // Handle slider control changes (animation speed and AI difficulty)
            HWND hSlider = reinterpret_cast<HWND>(lParam);
            if (hSlider == GetDlgItem(hwnd, IDC_ANIM_SPEED_SLIDER))
            {
                // Update animation speed label as slider moves
                int speed = static_cast<int>(SendMessage(hSlider, TBM_GETPOS, 0, 0));
                std::wstring label = L"Speed: " + std::to_wstring(speed) + L" ms";
                SetDlgItemText(hwnd, IDC_ANIM_SPEED_LABEL, label.c_str());
            }
            else if (hSlider == GetDlgItem(hwnd, IDC_DIFFICULTY_SLIDER))
            {
                // Update difficulty level label as slider moves
                int diff = static_cast<int>(SendMessage(hSlider, TBM_GETPOS, 0, 0));
                std::wstring label = L"Level: " + std::to_wstring(diff);
                SetDlgItemText(hwnd, IDC_DIFFICULTY_LABEL, label.c_str());
            }
            return TRUE;
        }
        
        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            
            if (id == IDC_SETTINGS_OK || id == IDOK)
            {
                // User clicked OK - save settings and close dialog
                pSettings = reinterpret_cast<Settings*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                if (pSettings && ApplySettings(hwnd, pSettings))
                {
                    SaveToINI(*pSettings);
                    EndDialog(hwnd, IDOK);
                }
                return TRUE;
            }
            else if (id == IDC_SETTINGS_CANCEL || id == IDCANCEL)
            {
                // User clicked Cancel - discard changes and close dialog
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            else if (id == IDC_SETTINGS_APPLY)
            {
                // User clicked Apply - save settings but keep dialog open
                pSettings = reinterpret_cast<Settings*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
                if (pSettings)
                {
                    ApplySettings(hwnd, pSettings);
                    SaveToINI(*pSettings);
                }
                return TRUE;
            }
            break;
        }
        }
        
        return FALSE;
    }
    
    // Initialize all dialog controls with current settings values
    // Creates tabbed interface with Board, Game, Appearance, and Players tabs
    void GameSettingsDialog::InitializeDialog(HWND hwnd, Settings* settings)
    {
        if (!settings) return;
        
        // Create modern font for dialog controls
        HFONT hFont = CreateFont(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH, L"Segoe UI");
        
        // Initialize tab control with four main categories
        HWND hTab = GetDlgItem(hwnd, IDC_TAB_CONTROL);
        
        TCITEM tie = {};
        tie.mask = TCIF_TEXT;
        
        tie.pszText = const_cast<LPWSTR>(L"Board");
        TabCtrl_InsertItem(hTab, 0, &tie);
        
        tie.pszText = const_cast<LPWSTR>(L"Game");
        TabCtrl_InsertItem(hTab, 1, &tie);
        
        tie.pszText = const_cast<LPWSTR>(L"Appearance");
        TabCtrl_InsertItem(hTab, 2, &tie);
        
        tie.pszText = const_cast<LPWSTR>(L"Players");
        TabCtrl_InsertItem(hTab, 3, &tie);
        
        // Calculate client area for tab content (excluding tab headers)
        RECT rcTab;
        GetClientRect(hTab, &rcTab);
        TabCtrl_AdjustRect(hTab, FALSE, &rcTab);
        
        // ========== BOARD TAB CONTROLS ==========
        // Display options affecting board rendering and visualization
        
        CreateWindowEx(0, L"BUTTON", L"Flip board (Black on bottom)",
            WS_CHILD | BS_AUTOCHECKBOX,
            rcTab.left + 20, rcTab.top + 20, 300, 20,
            hwnd, (HMENU)IDC_FLIP_BOARD_CHECK, GetModuleHandle(nullptr), nullptr);
        
        CreateWindowEx(0, L"BUTTON", L"Show coordinates",
            WS_CHILD | BS_AUTOCHECKBOX,
            rcTab.left + 20, rcTab.top + 50, 300, 20,
            hwnd, (HMENU)IDC_SHOW_COORDS_CHECK, GetModuleHandle(nullptr), nullptr);
        
        CreateWindowEx(0, L"BUTTON", L"Show legal moves",
            WS_CHILD | BS_AUTOCHECKBOX,
            rcTab.left + 20, rcTab.top + 80, 300, 20,
            hwnd, (HMENU)IDC_SHOW_LEGAL_CHECK, GetModuleHandle(nullptr), nullptr);
        
        CreateWindowEx(0, L"BUTTON", L"Animate moves",
            WS_CHILD | BS_AUTOCHECKBOX,
            rcTab.left + 20, rcTab.top + 110, 300, 20,
            hwnd, (HMENU)IDC_ANIMATE_CHECK, GetModuleHandle(nullptr), nullptr);
        
        CreateWindowEx(0, L"STATIC", L"Animation speed:",
            WS_CHILD,
            rcTab.left + 20, rcTab.top + 140, 120, 20,
            hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        
        // Animation speed slider (100-500ms range)
        HWND hSlider = CreateWindowEx(0, TRACKBAR_CLASS, L"",
            WS_CHILD | TBS_AUTOTICKS | TBS_HORZ,
            rcTab.left + 20, rcTab.top + 160, 250, 30,
            hwnd, (HMENU)IDC_ANIM_SPEED_SLIDER, GetModuleHandle(nullptr), nullptr);
        SendMessage(hSlider, TBM_SETRANGE, TRUE, MAKELONG(100, 500));
        SendMessage(hSlider, TBM_SETPOS, TRUE, settings->animationSpeed);
        
        std::wstring speedLabel = L"Speed: " + std::to_wstring(settings->animationSpeed) + L" ms";
        CreateWindowEx(0, L"STATIC", speedLabel.c_str(),
            WS_CHILD,
            rcTab.left + 280, rcTab.top + 165, 100, 20,
            hwnd, (HMENU)IDC_ANIM_SPEED_LABEL, GetModuleHandle(nullptr), nullptr);

        // ========== GAME TAB CONTROLS ==========
        // Game mode selection and AI configuration
        
        CreateWindowEx(0, L"BUTTON", L"Human vs Human",
            WS_CHILD | BS_AUTORADIOBUTTON | WS_GROUP,
            rcTab.left + 20, rcTab.top + 20, 200, 20,
            hwnd, (HMENU)IDC_MODE_RADIO_HVH, GetModuleHandle(nullptr), nullptr);
        
        CreateWindowEx(0, L"BUTTON", L"Human vs Computer",
            WS_CHILD | BS_AUTORADIOBUTTON,
            rcTab.left + 20, rcTab.top + 50, 200, 20,
            hwnd, (HMENU)IDC_MODE_RADIO_HVC, GetModuleHandle(nullptr), nullptr);
        
        CreateWindowEx(0, L"BUTTON", L"Computer vs Computer",
            WS_CHILD | BS_AUTORADIOBUTTON,
            rcTab.left + 20, rcTab.top + 80, 200, 20,
            hwnd, (HMENU)IDC_MODE_RADIO_CVC, GetModuleHandle(nullptr), nullptr);
        
        CreateWindowEx(0, L"STATIC", L"AI Difficulty:",
            WS_CHILD,
            rcTab.left + 20, rcTab.top + 120, 120, 20,
            hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        
        // AI difficulty slider (1-10 levels)
        HWND hDiffSlider = CreateWindowEx(0, TRACKBAR_CLASS, L"",
            WS_CHILD | TBS_AUTOTICKS | TBS_HORZ,
            rcTab.left + 20, rcTab.top + 140, 250, 30,
            hwnd, (HMENU)IDC_DIFFICULTY_SLIDER, GetModuleHandle(nullptr), nullptr);
        SendMessage(hDiffSlider, TBM_SETRANGE, TRUE, MAKELONG(1, 10));
        SendMessage(hDiffSlider, TBM_SETPOS, TRUE, settings->aiDifficulty);
        
        std::wstring diffLabel = L"Level: " + std::to_wstring(settings->aiDifficulty);
        CreateWindowEx(0, L"STATIC", diffLabel.c_str(),
            WS_CHILD,
            rcTab.left + 280, rcTab.top + 145, 100, 20,
            hwnd, (HMENU)IDC_DIFFICULTY_LABEL, GetModuleHandle(nullptr), nullptr);
        
        CreateWindowEx(0, L"BUTTON", L"Auto-promote to Queen",
            WS_CHILD | BS_AUTOCHECKBOX,
            rcTab.left + 20, rcTab.top + 190, 250, 20,
            hwnd, (HMENU)IDC_AUTO_QUEEN_CHECK, GetModuleHandle(nullptr), nullptr);
        
        // ========== APPEARANCE TAB CONTROLS ==========
        // Visual customization options
        
        CreateWindowEx(0, L"STATIC", L"Language:",
            WS_CHILD,
            rcTab.left + 20, rcTab.top + 20, 100, 20,
            hwnd, nullptr, GetModuleHandle(nullptr), nullptr);
        
        // Language selection dropdown
        HWND hCombo = CreateWindowEx(0, L"COMBOBOX", L"",
            WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
            rcTab.left + 130, rcTab.top + 18, 150, 200,
            hwnd, (HMENU)IDC_LANGUAGE_COMBO, GetModuleHandle(nullptr), nullptr);
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"English");
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Polski");
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Deutsch");
        SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM)L"Français");
        
        // Set current language selection
        int langIndex = 0;
        if (settings->language == L"Polski") langIndex = 1;
        else if (settings->language == L"Deutsch") langIndex = 2;
        else if (settings->language == L"Français") langIndex = 3;
        SendMessage(hCombo, CB_SETCURSEL, langIndex, 0);
        
        CreateWindowEx(0, L"BUTTON", L"Show piece shadows",
            WS_CHILD | BS_AUTOCHECKBOX,
            rcTab.left + 20, rcTab.top + 60, 250, 20,
            hwnd, (HMENU)IDC_PIECE_SHADOW_CHECK, GetModuleHandle(nullptr), nullptr);
        
        // ========== PLAYERS TAB CONTROLS ==========
        // Player name customization
        
        CreateWindowEx(0, L"STATIC", L"White player name:",
            WS_CHILD,
            rcTab.left + 20, rcTab.top + 20, 150, 20,
            hwnd, (HMENU)IDC_WHITE_NAME_LABEL, GetModuleHandle(nullptr), nullptr);
        
        CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"Player 1",
            WS_CHILD | ES_LEFT | ES_AUTOHSCROLL,
            rcTab.left + 20, rcTab.top + 45, 300, 25,
            hwnd, (HMENU)IDC_WHITE_NAME_EDIT, GetModuleHandle(nullptr), nullptr);
        
        CreateWindowEx(0, L"STATIC", L"Black player name:",
            WS_CHILD,
            rcTab.left + 20, rcTab.top + 90, 150, 20,
            hwnd, (HMENU)IDC_BLACK_NAME_LABEL, GetModuleHandle(nullptr), nullptr);
        
        CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", L"Player 2",
            WS_CHILD | ES_LEFT | ES_AUTOHSCROLL,
            rcTab.left + 20, rcTab.top + 115, 300, 25,
            hwnd, (HMENU)IDC_BLACK_NAME_EDIT, GetModuleHandle(nullptr), nullptr);
        
        // Set initial checkbox states from loaded settings
        CheckDlgButton(hwnd, IDC_FLIP_BOARD_CHECK, settings->flipBoard ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_SHOW_COORDS_CHECK, settings->showCoordinates ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_SHOW_LEGAL_CHECK, settings->showLegalMoves ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_ANIMATE_CHECK, settings->animateMoves ? BST_CHECKED : BST_UNCHECKED);
        
        // Set initial game mode radio button
        if (settings->gameMode == GameMode::HumanVsHuman)
            CheckDlgButton(hwnd, IDC_MODE_RADIO_HVH, BST_CHECKED);
        else if (settings->gameMode == GameMode::HumanVsComputer)
            CheckDlgButton(hwnd, IDC_MODE_RADIO_HVC, BST_CHECKED);
        else
            CheckDlgButton(hwnd, IDC_MODE_RADIO_CVC, BST_CHECKED);
        
        CheckDlgButton(hwnd, IDC_AUTO_QUEEN_CHECK, settings->autoPromoteQueen ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hwnd, IDC_PIECE_SHADOW_CHECK, settings->showPieceShadows ? BST_CHECKED : BST_UNCHECKED);
        
        // Set initial player names
        SetDlgItemText(hwnd, IDC_WHITE_NAME_EDIT, settings->whitePlayerName.c_str());
        SetDlgItemText(hwnd, IDC_BLACK_NAME_EDIT, settings->blackPlayerName.c_str());
        
        // Apply consistent font to all controls for modern appearance
        SetControlFont(hwnd, IDC_FLIP_BOARD_CHECK, hFont);
        SetControlFont(hwnd, IDC_SHOW_COORDS_CHECK, hFont);
        SetControlFont(hwnd, IDC_SHOW_LEGAL_CHECK, hFont);
        SetControlFont(hwnd, IDC_ANIMATE_CHECK, hFont);
        SetControlFont(hwnd, IDC_ANIM_SPEED_LABEL, hFont);
        SetControlFont(hwnd, IDC_MODE_RADIO_HVH, hFont);
        SetControlFont(hwnd, IDC_MODE_RADIO_HVC, hFont);
        SetControlFont(hwnd, IDC_MODE_RADIO_CVC, hFont);
        SetControlFont(hwnd, IDC_DIFFICULTY_LABEL, hFont);
        SetControlFont(hwnd, IDC_AUTO_QUEEN_CHECK, hFont);
        SetControlFont(hwnd, IDC_LANGUAGE_COMBO, hFont);
        SetControlFont(hwnd, IDC_PIECE_SHADOW_CHECK, hFont);
        SetControlFont(hwnd, IDC_WHITE_NAME_LABEL, hFont);
        SetControlFont(hwnd, IDC_BLACK_NAME_LABEL, hFont);
        SetControlFont(hwnd, IDC_WHITE_NAME_EDIT, hFont);
        SetControlFont(hwnd, IDC_BLACK_NAME_EDIT, hFont);
        
        // Show first tab by default
        OnTabChanged(hwnd, 0);
    }
    
    // Show/hide controls based on selected tab
    // Only controls for active tab are visible to reduce clutter
    void GameSettingsDialog::OnTabChanged(HWND hwnd, int tabIndex)
    {
        // Hide all tab-specific controls first
        ShowWindow(GetDlgItem(hwnd, IDC_FLIP_BOARD_CHECK), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_SHOW_COORDS_CHECK), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_SHOW_LEGAL_CHECK), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_ANIMATE_CHECK), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_ANIM_SPEED_SLIDER), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_ANIM_SPEED_LABEL), SW_HIDE);
        
        ShowWindow(GetDlgItem(hwnd, IDC_MODE_RADIO_HVH), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_MODE_RADIO_HVC), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_MODE_RADIO_CVC), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_DIFFICULTY_SLIDER), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_DIFFICULTY_LABEL), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_AUTO_QUEEN_CHECK), SW_HIDE);
        
        ShowWindow(GetDlgItem(hwnd, IDC_LANGUAGE_COMBO), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_PIECE_SHADOW_CHECK), SW_HIDE);
        
        ShowWindow(GetDlgItem(hwnd, IDC_WHITE_NAME_EDIT), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_BLACK_NAME_EDIT), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_WHITE_NAME_LABEL), SW_HIDE);
        ShowWindow(GetDlgItem(hwnd, IDC_BLACK_NAME_LABEL), SW_HIDE);
        
        // Show only controls for the selected tab
        switch (tabIndex)
        {
        case 0: // Board tab - display and animation settings
            ShowWindow(GetDlgItem(hwnd, IDC_FLIP_BOARD_CHECK), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_SHOW_COORDS_CHECK), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_SHOW_LEGAL_CHECK), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_ANIMATE_CHECK), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_ANIM_SPEED_SLIDER), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_ANIM_SPEED_LABEL), SW_SHOW);
            break;
            
        case 1: // Game tab - mode and AI difficulty
            ShowWindow(GetDlgItem(hwnd, IDC_MODE_RADIO_HVH), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_MODE_RADIO_HVC), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_MODE_RADIO_CVC), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_DIFFICULTY_SLIDER), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_DIFFICULTY_LABEL), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_AUTO_QUEEN_CHECK), SW_SHOW);
            break;
            
        case 2: // Appearance tab - language and visual effects
            ShowWindow(GetDlgItem(hwnd, IDC_LANGUAGE_COMBO), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_PIECE_SHADOW_CHECK), SW_SHOW);
            break;
            
        case 3: // Players tab - name customization
            ShowWindow(GetDlgItem(hwnd, IDC_WHITE_NAME_EDIT), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_BLACK_NAME_EDIT), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_WHITE_NAME_LABEL), SW_SHOW);
            ShowWindow(GetDlgItem(hwnd, IDC_BLACK_NAME_LABEL), SW_SHOW);
            break;
        }
    }
    
    // Read values from dialog controls and update settings structure
    // Returns true if all values were successfully read
    bool GameSettingsDialog::ApplySettings(HWND hwnd, Settings* settings)
    {
        if (!settings) return false;
        
        // Read board display settings
        settings->flipBoard = (IsDlgButtonChecked(hwnd, IDC_FLIP_BOARD_CHECK) == BST_CHECKED);
        settings->showCoordinates = (IsDlgButtonChecked(hwnd, IDC_SHOW_COORDS_CHECK) == BST_CHECKED);
        settings->showLegalMoves = (IsDlgButtonChecked(hwnd, IDC_SHOW_LEGAL_CHECK) == BST_CHECKED);
        settings->animateMoves = (IsDlgButtonChecked(hwnd, IDC_ANIMATE_CHECK) == BST_CHECKED);
        
        // Read animation speed from slider
        HWND hSlider = GetDlgItem(hwnd, IDC_ANIM_SPEED_SLIDER);
        if (hSlider)
        {
            settings->animationSpeed = static_cast<int>(SendMessage(hSlider, TBM_GETPOS, 0, 0));
        }
        
        // Read game mode from radio buttons
        if (IsDlgButtonChecked(hwnd, IDC_MODE_RADIO_HVH) == BST_CHECKED)
            settings->gameMode = GameMode::HumanVsHuman;
        else if (IsDlgButtonChecked(hwnd, IDC_MODE_RADIO_HVC) == BST_CHECKED)
            settings->gameMode = GameMode::HumanVsComputer;
        else if (IsDlgButtonChecked(hwnd, IDC_MODE_RADIO_CVC) == BST_CHECKED)
            settings->gameMode = GameMode::ComputerVsComputer;
        
        // Read AI difficulty from slider
        HWND hDiffSlider = GetDlgItem(hwnd, IDC_DIFFICULTY_SLIDER);
        if (hDiffSlider)
        {
            settings->aiDifficulty = static_cast<int>(SendMessage(hDiffSlider, TBM_GETPOS, 0, 0));
        }
        
        // Read gameplay options
        settings->autoPromoteQueen = (IsDlgButtonChecked(hwnd, IDC_AUTO_QUEEN_CHECK) == BST_CHECKED);
        settings->showPieceShadows = (IsDlgButtonChecked(hwnd, IDC_PIECE_SHADOW_CHECK) == BST_CHECKED);
        
        // Read language selection from combo box
        HWND hCombo = GetDlgItem(hwnd, IDC_LANGUAGE_COMBO);
        if (hCombo)
        {
            int sel = static_cast<int>(SendMessage(hCombo, CB_GETCURSEL, 0, 0));
            if (sel != CB_ERR)
            {
                wchar_t langBuffer[64] = {};
                SendMessage(hCombo, CB_GETLBTEXT, sel, (LPARAM)langBuffer);
                settings->language = langBuffer;
            }
        }
        
        // Read player names from edit controls
        wchar_t whiteName[256] = {};
        wchar_t blackName[256] = {};
        GetDlgItemText(hwnd, IDC_WHITE_NAME_EDIT, whiteName, 256);
        GetDlgItemText(hwnd, IDC_BLACK_NAME_EDIT, blackName, 256);
        
        if (wcslen(whiteName) > 0)
            settings->whitePlayerName = whiteName;
        if (wcslen(blackName) > 0)
            settings->blackPlayerName = blackName;
        
        return true;
    }
    
    // Load settings from INI configuration file
    // Uses standard Windows INI file API for persistence
    void GameSettingsDialog::LoadFromINI(Settings& settings)
    {
        // Get path to settings.ini in executable directory
        wchar_t exePath[MAX_PATH];
        GetModuleFileName(nullptr, exePath, MAX_PATH);
        std::wstring exeDir = exePath;
        size_t pos = exeDir.find_last_of(L"\\/");
        if (pos != std::wstring::npos)
            exeDir = exeDir.substr(0, pos + 1);
        
        std::wstring iniPath = exeDir + L"settings.ini";
        
        // Load display settings
        settings.flipBoard = GetPrivateProfileInt(L"Display", L"FlipBoard", 0, iniPath.c_str()) != 0;
        settings.showCoordinates = GetPrivateProfileInt(L"Display", L"ShowCoordinates", 1, iniPath.c_str()) != 0;
        settings.showLegalMoves = GetPrivateProfileInt(L"Display", L"ShowLegalMoves", 1, iniPath.c_str()) != 0;
        settings.animateMoves = GetPrivateProfileInt(L"Display", L"AnimateMoves", 1, iniPath.c_str()) != 0;
        settings.animationSpeed = GetPrivateProfileInt(L"Display", L"AnimationSpeed", 300, iniPath.c_str());
        
        // Load game settings
        int gameMode = GetPrivateProfileInt(L"Game", L"GameMode", 1, iniPath.c_str());
        settings.gameMode = static_cast<GameMode>(gameMode);
        settings.aiDifficulty = GetPrivateProfileInt(L"Game", L"AIDifficulty", 3, iniPath.c_str());
        
        // Load thread count with sensible defaults and bounds checking
        settings.numThreads = GetPrivateProfileInt(L"Game", L"Threads",
            std::thread::hardware_concurrency(), iniPath.c_str());
        if (settings.numThreads < 1) settings.numThreads = 1;
        if (settings.numThreads > 64) settings.numThreads = 64;
        
        settings.autoPromoteQueen = GetPrivateProfileInt(L"Game", L"AutoPromoteQueen", 1, iniPath.c_str()) != 0;
        settings.useNeuralEval = GetPrivateProfileInt(L"Game", L"UseNeuralEval", 0, iniPath.c_str()) != 0;

        // Load appearance settings
        wchar_t langBuffer[64] = {};
        GetPrivateProfileString(L"Display", L"Language", L"English", langBuffer, 64, iniPath.c_str());
        settings.language = langBuffer;
        settings.showPieceShadows = GetPrivateProfileInt(L"Display", L"ShowPieceShadows", 1, iniPath.c_str()) != 0;
        
        // Load player names from INI file
        // Uses 256-char buffers for player name strings
        wchar_t whiteBuffer[256] = {};
        wchar_t blackBuffer[256] = {};
        GetPrivateProfileString(L"Players", L"WhitePlayer", L"Player 1", whiteBuffer, 256, iniPath.c_str());
        GetPrivateProfileString(L"Players", L"BlackPlayer", L"Player 2", blackBuffer, 256, iniPath.c_str());
        settings.whitePlayerName = whiteBuffer;
        settings.blackPlayerName = blackBuffer;
        
        // Load board square colors from INI file
        // Colors stored as comma-separated RGB values (e.g., "240,240,245")
        wchar_t colorBuffer[64] = {};
        int r, g, b;
        
        // Parse light square color (default: off-white)
        GetPrivateProfileString(L"Colors", L"LightSquare", L"240,240,245", colorBuffer, 64, iniPath.c_str());
        if (swscanf_s(colorBuffer, L"%d,%d,%d", &r, &g, &b) == 3)
        {
            settings.lightSquareColor = RGB(r, g, b);
        }
        
        // Parse dark square color (default: blue-gray)
        GetPrivateProfileString(L"Colors", L"DarkSquare", L"70,80,100", colorBuffer, 64, iniPath.c_str());
        if (swscanf_s(colorBuffer, L"%d,%d,%d", &r, &g, &b) == 3)
        {
            settings.darkSquareColor = RGB(r, g, b);
        }
    }
    
    // Save all settings to INI configuration file
    // Writes current settings values to settings.ini in executable directory
    void GameSettingsDialog::SaveToINI(const Settings& settings)
    {
        // Determine INI file path relative to executable location
        wchar_t exePath[MAX_PATH];
        GetModuleFileName(nullptr, exePath, MAX_PATH);
        std::wstring exeDir = exePath;
        size_t pos = exeDir.find_last_of(L"\\/");
        if (pos != std::wstring::npos)
            exeDir = exeDir.substr(0, pos + 1);
        
        std::wstring iniPath = exeDir + L"settings.ini";
        
        // Save display/board settings
        WritePrivateProfileString(L"Display", L"FlipBoard", settings.flipBoard ? L"1" : L"0", iniPath.c_str());
        WritePrivateProfileString(L"Display", L"ShowCoordinates", settings.showCoordinates ? L"1" : L"0", iniPath.c_str());
        WritePrivateProfileString(L"Display", L"ShowLegalMoves", settings.showLegalMoves ? L"1" : L"0", iniPath.c_str());
        WritePrivateProfileString(L"Display", L"AnimateMoves", settings.animateMoves ? L"1" : L"0", iniPath.c_str());
        WritePrivateProfileString(L"Display", L"AnimationSpeed", std::to_wstring(settings.animationSpeed).c_str(), iniPath.c_str());
        
        // Save game settings (mode, difficulty, threads)
        WritePrivateProfileString(L"Game", L"GameMode", std::to_wstring(static_cast<int>(settings.gameMode)).c_str(), iniPath.c_str());
        WritePrivateProfileString(L"Game", L"AIDifficulty", std::to_wstring(settings.aiDifficulty).c_str(), iniPath.c_str());
        WritePrivateProfileString(L"Game", L"Threads",
            std::to_wstring(settings.numThreads).c_str(), iniPath.c_str());
        WritePrivateProfileString(L"Game", L"AutoPromoteQueen", settings.autoPromoteQueen ? L"1" : L"0", iniPath.c_str());
        WritePrivateProfileString(L"Game", L"UseNeuralEval",
            settings.useNeuralEval ? L"1" : L"0", iniPath.c_str());

        // Save appearance settings
        WritePrivateProfileString(L"Display", L"Language", settings.language.c_str(), iniPath.c_str());
        WritePrivateProfileString(L"Display", L"ShowPieceShadows", settings.showPieceShadows ? L"1" : L"0", iniPath.c_str());
        
        // Save player names
        WritePrivateProfileString(L"Players", L"WhitePlayer", settings.whitePlayerName.c_str(), iniPath.c_str());
        WritePrivateProfileString(L"Players", L"BlackPlayer", settings.blackPlayerName.c_str(), iniPath.c_str());
        
        // Save light square color as comma-separated RGB string
        int r = GetRValue(settings.lightSquareColor);
        int g = GetGValue(settings.lightSquareColor);
        int b = GetBValue(settings.lightSquareColor);
        std::wstring lightSquareStr = std::to_wstring(r) + L"," + std::to_wstring(g) + L"," + std::to_wstring(b);
        WritePrivateProfileString(L"Colors", L"LightSquare", lightSquareStr.c_str(), iniPath.c_str());
        
        // Save dark square color as comma-separated RGB string
        r = GetRValue(settings.darkSquareColor);
        g = GetGValue(settings.darkSquareColor);
        b = GetBValue(settings.darkSquareColor);
        std::wstring darkSquareStr = std::to_wstring(r) + L"," + std::to_wstring(g) + L"," + std::to_wstring(b);
        WritePrivateProfileString(L"Colors", L"DarkSquare", darkSquareStr.c_str(), iniPath.c_str());
    }
}
