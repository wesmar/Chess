// PromotionDialog.cpp
#include "PromotionDialog.h"
#include "../../Resources/Resource.h"

namespace Chess
{
    PieceType PromotionDialog::Show(HWND parent, PlayerColor color)
    {
        INT_PTR result = DialogBoxParam(
            GetModuleHandle(nullptr),
            MAKEINTRESOURCE(IDD_PROMOTION),
            parent,
            DialogProc,
            static_cast<LPARAM>(color)
        );
        
        if (result == -1 || result == IDCANCEL)
        {
            return PieceType::Queen;
        }
        
        switch (result)
        {
        case 100: return PieceType::Queen;
        case 101: return PieceType::Rook;
        case 102: return PieceType::Bishop;
        case 103: return PieceType::Knight;
        default:  return PieceType::Queen;
        }
    }

    INT_PTR CALLBACK PromotionDialog::DialogProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        static PlayerColor pieceColor = PlayerColor::White;
        
        switch (msg)
        {
        case WM_INITDIALOG:
        {
            pieceColor = static_cast<PlayerColor>(lParam);
            
            // Center dialog over parent window
            RECT rcParent, rcDialog;
            GetWindowRect(GetParent(hwnd), &rcParent);
            GetWindowRect(hwnd, &rcDialog);
            
            int x = rcParent.left + (rcParent.right - rcParent.left - (rcDialog.right - rcDialog.left)) / 2;
            int y = rcParent.top + (rcParent.bottom - rcParent.top - (rcDialog.bottom - rcDialog.top)) / 2;
            
            SetWindowPos(hwnd, HWND_TOP, x, y, 0, 0, SWP_NOSIZE);
            
            // Set button text to Unicode chess piece symbols
            SetDlgItemText(hwnd, IDC_PROMOTE_QUEEN, L"♕");
            SetDlgItemText(hwnd, IDC_PROMOTE_ROOK, L"♖");
            SetDlgItemText(hwnd, IDC_PROMOTE_BISHOP, L"♗");
            SetDlgItemText(hwnd, IDC_PROMOTE_KNIGHT, L"♘");
            
            // Use black piece symbols if promoting black pawn
            if (pieceColor == PlayerColor::Black)
            {
                SetDlgItemText(hwnd, IDC_PROMOTE_QUEEN, L"♛");
                SetDlgItemText(hwnd, IDC_PROMOTE_ROOK, L"♜");
                SetDlgItemText(hwnd, IDC_PROMOTE_BISHOP, L"♝");
                SetDlgItemText(hwnd, IDC_PROMOTE_KNIGHT, L"♞");
            }
            
            // Set large font for chess piece symbols
            HFONT hFont = CreateFont(48, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH, L"Segoe UI Symbol");
            
            SendDlgItemMessage(hwnd, IDC_PROMOTE_QUEEN, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hwnd, IDC_PROMOTE_ROOK, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hwnd, IDC_PROMOTE_BISHOP, WM_SETFONT, (WPARAM)hFont, TRUE);
            SendDlgItemMessage(hwnd, IDC_PROMOTE_KNIGHT, WM_SETFONT, (WPARAM)hFont, TRUE);
            
            return TRUE;
        }
        
        case WM_COMMAND:
        {
            int id = LOWORD(wParam);
            
            switch (id)
            {
            case IDC_PROMOTE_QUEEN:
                EndDialog(hwnd, 100);
                return TRUE;
                
            case IDC_PROMOTE_ROOK:
                EndDialog(hwnd, 101);
                return TRUE;
                
            case IDC_PROMOTE_BISHOP:
                EndDialog(hwnd, 102);
                return TRUE;
                
            case IDC_PROMOTE_KNIGHT:
                EndDialog(hwnd, 103);
                return TRUE;
                
            case IDCANCEL:
                EndDialog(hwnd, IDCANCEL);
                return TRUE;
            }
            break;
        }
        
        case WM_CLOSE:
            EndDialog(hwnd, IDCANCEL);
            return TRUE;
        }
        
        return FALSE;
    }
}