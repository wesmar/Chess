// WinUtility.h
// Windows utility functions for chess application
// Provides string conversion, color mapping, and text formatting helpers
#pragma once

#include "../Engine/ChessConstants.h"
#include <string>
#include <windows.h>

namespace Chess
{
    // ========== STRING CONVERSION ==========
    
    // Convert UTF-8 std::string to wide string (UTF-16)
    // Used for WinAPI functions that require LPCWSTR
    std::wstring StringToWString(const std::string& str);
    
    // Convert wide string (UTF-16) to UTF-8 std::string
    // Used for converting WinAPI results to engine format
    std::string WStringToString(const std::wstring& wstr);
    
    // ========== COLOR UTILITIES ==========
    
    // Get GDI COLORREF for player color
    // White = RGB(255,255,255), Black = RGB(0,0,0)
    COLORREF GetColorForPlayer(PlayerColor color);
    
    // ========== TEXT UTILITIES ==========
    
    // Get display name for player color
    // Returns L"White" or L"Black" as wide string
    std::wstring GetPlayerName(PlayerColor color);
}