// PromotionDialog.h
#pragma once

#include "../../Engine/ChessConstants.h"
#include <windows.h>

namespace Chess
{
    // Promotion dialog - allows player to choose piece for pawn promotion
    class PromotionDialog
    {
    public:
        // Show modal promotion dialog and return selected piece type
        static PieceType Show(HWND parent, PlayerColor color);
        
    private:
        static INT_PTR CALLBACK DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    };
}