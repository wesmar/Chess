// PromotionDialog.cpp
// Pawn promotion dialog implementation
// Displays piece selection buttons when pawn reaches the last rank
#include "PromotionDialog.h"
#include "../../Resources/Resource.h"

namespace Chess
{
    // Show modal promotion dialog and return selected piece type
    // Displays four buttons with Unicode chess piece symbols for selection
    PieceType PromotionDialog::Show(HWND parent, PlayerColor color)
    {
        // Show dialog and pass piece color as parameter for correct symbols
        INT_PTR result = DialogBoxParam(
            GetModuleHandle(nullptr),
            MAKEINTRESOURCE(IDD_PROMOTION),
            parent,
            DialogProc,
            static_cast<LPARAM>(color)
        );
        
        // Handle dialog failure or cancellation - default to Queen
        if (result == -1 || result == IDCANCEL)
        {
            return PieceType::Queen;
        }
        
        // Map dialog result codes to piece types
        // Result codes 100-103 correspond to Queen, Rook, Bishop, Knight
        switch (result)
        {
        case 100: return PieceType::Queen;
        case 101: return PieceType::Rook;
        case 102: return PieceType::Bishop;
        case 103: return PieceType::Knight;
        default:  return PieceType::Queen;
        }
    }

    // Dialog message handler for promotion piece selection
    // Manages dialog initialization, button clicks, and visual styling
    INT_PTR CALLBACK PromotionDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // Static variable to store piece color across message callbacks
        static PlayerColor pieceColor = PlayerColor::White;
        
        switch (msg)
        {
        case WM_INITDIALOG:
        {
            // Store piece color passed from Show() method
            pieceColor = static_cast<PlayerColor>(lParam);
            
            // Center dialog over parent window for better UX
            RECT rcParent, rcDialog;
            GetWindowRect(GetParent(hwnd), &rcParent);
            GetWindowRect(hwnd, &rcDialog);
            
            int x = rcParent.left + (rcParent.right - rcParent.left - (rcDialog.right - rcDialog.left)) / 2;
            int y = rcParent.top + (rcParent.bottom - rcParent.top - (rcDialog.bottom - rcDialog.top)) / 2;
            
            SetWindowPos(hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
            
            // Set button text to white piece Unicode symbols (default)
            // Unicode chess symbols: ♕ Queen, ♖ Rook, ♗ Bishop, ♘ Knight
            SetDlgItemText(hwnd, IDC_PROMOTE_QUEEN, L"♕");
            SetDlgItemText(hwnd, IDC_PROMOTE_ROOK, L"♖");
            SetDlgItemText(hwnd, IDC_PROMOTE_BISHOP, L"♗");
            SetDlgItemText(hwnd, IDC_PROMOTE_KNIGHT, L"♘");
            
            // Switch to black piece symbols if promoting black pawn
            // Unicode black pieces: ♛ Queen, ♜ Rook, ♝ Bishop, ♞ Knight
            if (pieceColor == PlayerColor::Black)
            {
                SetDlgItemText(hwnd, IDC_PROMOTE_QUEEN, L"♛");
                SetDlgItemText(hwnd, IDC_PROMOTE_ROOK, L"♜");
                SetDlgItemText(hwnd, IDC_PROMOTE_BISHOP, L"♝");
                SetDlgItemText(hwnd, IDC_PROMOTE_KNIGHT, L"♞");
            }
            
            // Create large font for clear piece symbol visibility
            // Uses Segoe UI Symbol for proper Unicode chess character rendering
            HFONT hFont = CreateFont(48, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH, L"Segoe UI Symbol");
            
            // Apply font to all promotion buttons
            SendDlgItemMessage(hwnd, IDC_PROMOTE_QUEEN, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hwnd, IDC_PROMOTE_ROOK, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hwnd, IDC_PROMOTE_BISHOP, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hwnd, IDC_PROMOTE_KNIGHT, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            return TRUE;
        }
        
        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            
            // Handle promotion button clicks
            // Each button returns unique result code for piece identification
            switch (id)
            {
            case IDC_PROMOTE_QUEEN:
                EndDialog(hwnd, 100);  // Queen selected
                return TRUE;
                
            case IDC_PROMOTE_ROOK:
                EndDialog(hwnd, 101);  // Rook selected
                return TRUE;
                
            case IDC_PROMOTE_BISHOP:
                EndDialog(hwnd, 102);  // Bishop selected
                return TRUE;
                
            case IDC_PROMOTE_KNIGHT:
                EndDialog(hwnd, 103);  // Knight selected
                return TRUE;
                
            case IDCANCEL:
                EndDialog(hwnd, IDCANCEL);  // Dialog cancelled (Escape key)
                return TRUE;
            }
            break;
        }
        
        case WM_CLOSE:
            // Handle window close button - treat as cancel
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        
        return FALSE;
    }
}
