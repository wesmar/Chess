// WinUtility.h
#pragma once

#include "../Engine/ChessConstants.h"
#include <string>
#include <windows.h>

namespace Chess
{
    // Utility functions for Windows application
    
    // String conversion utilities
    std::wstring StringToWString(const std::string& str);
    std::string WStringToString(const std::wstring& wstr);
    
    // Color utilities
    COLORREF GetColorForPlayer(PlayerColor color);
    
    // Text utilities
    std::wstring GetPlayerName(PlayerColor color);
}