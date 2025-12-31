// PromotionDialog.h
// Pawn promotion dialog interface
// Provides modal dialog for selecting promotion piece when pawn reaches last rank
#pragma once

#include "../../Engine/ChessConstants.h"
#include <windows.h>

namespace Chess
{
    // Promotion dialog controller class
    // Displays four piece options (Queen, Rook, Bishop, Knight) as clickable buttons
    // with Unicode chess symbols matching the promoting pawn's color
    class PromotionDialog
    {
    public:
        // Show modal promotion dialog and return selected piece type
        // Parameters:
        //   parent - Handle to parent window for modal positioning
        //   color  - Color of promoting pawn (determines piece symbols)
        // Returns: Selected PieceType (defaults to Queen on cancel)
        static PieceType Show(HWND parent, PlayerColor color);
        
    private:
        // Windows dialog procedure callback for message handling
        // Manages dialog initialization, button styling, and user input
        static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    };
}
