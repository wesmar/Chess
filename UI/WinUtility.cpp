// WinUtility.cpp
// Windows utility function implementations
// Provides UTF-8/UTF-16 string conversion and color/text helpers for WinAPI

#include "WinUtility.h"

namespace Chess
{
    std::wstring StringToWString(const std::string& str)
    {
        if (str.empty()) return L"";
        
        int size_needed = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
        std::wstring wstr(size_needed, 0);
        MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &wstr[0], size_needed);
        return wstr;
    }

    std::string WStringToString(const std::wstring& wstr)
    {
        if (wstr.empty()) return "";
        
        int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), 
                                             nullptr, 0, nullptr, nullptr);
        std::string str(size_needed, 0);
        WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), 
                           &str[0], size_needed, nullptr, nullptr);
        return str;
    }

    COLORREF GetColorForPlayer(PlayerColor color)
    {
        return (color == PlayerColor::White) ? RGB(255, 255, 255) : RGB(0, 0, 0);
    }

    std::wstring GetPlayerName(PlayerColor color)
    {
        return (color == PlayerColor::White) ? L"White" : L"Black";
    }
}