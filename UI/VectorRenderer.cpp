// VectorRenderer.cpp
#define NOMINMAX
#include "VectorRenderer.h"
#include <algorithm>

namespace Chess
{
    // Constructor - initialize fonts for piece rendering and coordinates
    ChessPieceRenderer::ChessPieceRenderer()
    {
        m_pieceFont = CreatePieceFont(48);
        m_coordinateFont = CreateCoordinateFont(14);
    }

    // Create font for displaying chess piece Unicode characters
    HFONT ChessPieceRenderer::CreatePieceFont(int size)
    {
        if (m_pieceFont) DeleteObject(m_pieceFont);
        
        LOGFONT lf = {};
        lf.lfHeight = -size;
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe UI Symbol");
        
        return CreateFontIndirect(&lf);
    }

    // Create font for displaying board coordinates (a-h, 1-8)
    HFONT ChessPieceRenderer::CreateCoordinateFont(int size)
    {
        if (m_coordinateFont) DeleteObject(m_coordinateFont);
        
        LOGFONT lf = {};
        lf.lfHeight = -size;
        lf.lfWeight = FW_NORMAL;
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfQuality = CLEARTYPE_QUALITY;
        wcscpy_s(lf.lfFaceName, L"Segoe UI");
        
        return CreateFontIndirect(&lf);
    }

    // Map piece type and color to appropriate Unicode chess character
    wchar_t ChessPieceRenderer::GetPieceChar(Piece piece) const
    {
        if (piece.IsEmpty()) return L' ';
        
        if (piece.GetColor() == PlayerColor::White)
        {
            switch (piece.GetType())
            {
                case PieceType::King: return WHITE_KING;
                case PieceType::Queen: return WHITE_QUEEN;
                case PieceType::Rook: return WHITE_ROOK;
                case PieceType::Bishop: return WHITE_BISHOP;
                case PieceType::Knight: return WHITE_KNIGHT;
                case PieceType::Pawn: return WHITE_PAWN;
                default: return L' ';
            }
        }
        else
        {
            switch (piece.GetType())
            {
                case PieceType::King: return BLACK_KING;
                case PieceType::Queen: return BLACK_QUEEN;
                case PieceType::Rook: return BLACK_ROOK;
                case PieceType::Bishop: return BLACK_BISHOP;
                case PieceType::Knight: return BLACK_KNIGHT;
                case PieceType::Pawn: return BLACK_PAWN;
                default: return L' ';
            }
        }
    }

    // Render single piece with optional selection/highlight
    void ChessPieceRenderer::RenderPiece(HDC hdc, const RECT& rect, Piece piece,
                                        bool isSelected, bool isHighlighted)
    {
        if (!piece) return;
        
        // Draw background highlight if square is selected or highlighted
        if (isSelected)
        {
            DrawHighlight(hdc, rect, m_config.selectionColor, 4);
        }
        else if (isHighlighted)
        {
            DrawHighlight(hdc, rect, m_config.highlightColor, 3);
        }
        
        DrawPiece(hdc, rect, piece);
    }

    // Render complete chess board with pieces and optional highlights
    void ChessPieceRenderer::RenderBoard(HDC hdc, const RECT& clientRect,
                                        const std::array<Piece, SQUARE_COUNT>& board,
                                        int selectedSquare,
                                        const std::vector<int>& highlightSquares)
    {
        // Calculate board size to fit in client area (leave space for sidebar)
        int boardSize = std::min(
            clientRect.right - clientRect.left - 300,
            clientRect.bottom - clientRect.top - 60
        );
        
        RECT boardRect = {
            clientRect.left + (clientRect.right - clientRect.left - 300 - boardSize) / 2,
            clientRect.top + (clientRect.bottom - clientRect.top - boardSize) / 2,
            clientRect.left + (clientRect.right - clientRect.left - 300 + boardSize) / 2,
            clientRect.top + (clientRect.bottom - clientRect.top + boardSize) / 2
        };

        // Draw background
        HBRUSH bgBrush = CreateSolidBrush(m_config.backgroundColor);
        FillRect(hdc, &clientRect, bgBrush);
        DeleteObject(bgBrush);
        
        // Draw board border
        HBRUSH borderBrush = CreateSolidBrush(m_config.borderColor);
        RECT borderRect = {
            boardRect.left - 2,
            boardRect.top - 2,
            boardRect.right + 2,
            boardRect.bottom + 2
        };
        FrameRect(hdc, &borderRect, borderBrush);
        DeleteObject(borderBrush);
        
        // Draw all squares and pieces
        for (int rank = 0; rank < BOARD_SIZE; ++rank)
        {
            for (int file = 0; file < BOARD_SIZE; ++file)
            {
                int squareIndex = rank * BOARD_SIZE + file;
                RECT squareRect = GetSquareRect(boardRect, file, rank);
                
                // Determine square color (alternating pattern)
                bool isDark = (file + rank) % 2 == 1;
                DrawChessSquare(hdc, squareRect, isDark);
                
                // Draw highlight if square is selected
                if (squareIndex == selectedSquare)
                {
                    DrawHighlight(hdc, squareRect, m_config.selectionColor, 4);
                }
                // Draw highlight if square is in highlight list
                else if (std::find(highlightSquares.begin(), highlightSquares.end(),
                                  squareIndex) != highlightSquares.end())
                {
                    DrawHighlight(hdc, squareRect, m_config.highlightColor, 3);
                }
                
                // Draw piece if present
                Piece piece = board[squareIndex];
                if (piece)
                {
                    DrawPiece(hdc, squareRect, piece);
                }
            }
        }
        
        // Draw file and rank labels
        if (m_config.showCoordinates)
        {
            RenderCoordinates(hdc, boardRect);
        }
    }

    // Render coordinate labels (a-h, 1-8) around board
    void ChessPieceRenderer::RenderCoordinates(HDC hdc, const RECT& boardRect)
    {
        int squareSize = (boardRect.right - boardRect.left) / BOARD_SIZE;
        
        HFONT oldFont = (HFONT)SelectObject(hdc, m_coordinateFont);
        SetTextColor(hdc, m_config.coordinateColor);
        SetBkMode(hdc, TRANSPARENT);
        
        // Draw file letters (a-h) below board
        for (int file = 0; file < BOARD_SIZE; ++file)
        {
            int drawFile = m_config.flipBoard ? (7 - file) : file;
            wchar_t fileChar[2] = { static_cast<wchar_t>(FILE_NAMES[file]), 0 };
            RECT textRect = {
                boardRect.left + drawFile * squareSize + squareSize / 2 - 10,
                boardRect.bottom + 5,
                boardRect.left + drawFile * squareSize + squareSize / 2 + 10,
                boardRect.bottom + 25
            };
            
            DrawTextW(hdc, fileChar, 1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        
        // Draw rank numbers (1-8) left of board
        for (int rank = 0; rank < BOARD_SIZE; ++rank)
        {
            int drawRank = m_config.flipBoard ? rank : (7 - rank);
            wchar_t rankChar[2] = { static_cast<wchar_t>(RANK_NAMES[rank]), 0 };
            RECT textRect = {
                boardRect.left - 25,
                boardRect.top + drawRank * squareSize + squareSize / 2 - 10,
                boardRect.left - 5,
                boardRect.top + drawRank * squareSize + squareSize / 2 + 10
            };
            
            DrawTextW(hdc, rankChar, 1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        
        SelectObject(hdc, oldFont);
    }

    // Calculate screen rectangle for a board square (respects board flip setting)
    RECT ChessPieceRenderer::GetSquareRect(const RECT& boardRect, int file, int rank) const
    {
        int squareSize = (boardRect.right - boardRect.left) / BOARD_SIZE;
        
        // Flip board coordinates if configured
        int drawFile = m_config.flipBoard ? (7 - file) : file;
        int drawRank = m_config.flipBoard ? rank : (7 - rank);
        
        return {
            boardRect.left + drawFile * squareSize,
            boardRect.top + drawRank * squareSize,
            boardRect.left + (drawFile + 1) * squareSize,
            boardRect.top + (drawRank + 1) * squareSize
        };
    }

    // Draw single square with appropriate color
    void ChessPieceRenderer::DrawChessSquare(HDC hdc, const RECT& rect, bool isDark)
    {
        HBRUSH brush = CreateSolidBrush(isDark ? m_config.darkSquareColor : m_config.lightSquareColor);
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
    }

    // Draw chess piece with outline for better visibility
    void ChessPieceRenderer::DrawPiece(HDC hdc, const RECT& rect, Piece piece)
    {
        if (piece.IsEmpty()) return;
        
        wchar_t pieceChar = GetPieceChar(piece);
        if (pieceChar == L' ') return;
        
        int squareSize = rect.right - rect.left;
        int fontSize = static_cast<int>(squareSize * m_config.pieceScale);
        
        HFONT pieceFont = CreatePieceFont(fontSize);
        HFONT oldFont = (HFONT)SelectObject(hdc, pieceFont);
        
        SetBkMode(hdc, TRANSPARENT);
        
        // Determine main piece color
        COLORREF pieceColor = (piece.GetColor() == PlayerColor::White)
            ? m_config.lightPieceColor
            : m_config.darkPieceColor;
        
        // Draw outline for better visibility on both light and dark squares
        if (m_config.useBlackPieceOutline)
        {
            // Use contrasting outline color (black for white pieces, white for black pieces)
            COLORREF outlineColor = (piece.GetColor() == PlayerColor::White)
                ? RGB(0, 0, 0)        // Black outline for white pieces
                : RGB(255, 255, 255); // White outline for black pieces
            
            SetTextColor(hdc, outlineColor);
            
            // Draw outline in 8 directions to create stroke effect
            for (int dx = -1; dx <= 1; dx++)
            {
                for (int dy = -1; dy <= 1; dy++)
                {
                    if (dx == 0 && dy == 0) continue; // Skip center position
                    
                    RECT offsetRect = rect;
                    offsetRect.left += dx;
                    offsetRect.top += dy;
                    offsetRect.right += dx;
                    offsetRect.bottom += dy;
                    
                    DrawTextW(hdc, &pieceChar, 1, &offsetRect, 
                             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                }
            }
        }
        
        // Draw main piece on top of outline
        SetTextColor(hdc, pieceColor);
        DrawTextW(hdc, &pieceChar, 1, (LPRECT)&rect, 
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        
        SelectObject(hdc, oldFont);
        DeleteObject(pieceFont);
    }

    // Draw shadow effect for piece (currently unused but available)
    void ChessPieceRenderer::DrawPieceShadow(HDC hdc, const RECT& rect, wchar_t pieceChar)
    {
        RECT shadowRect = rect;
        shadowRect.left += m_config.shadowOffset;
        shadowRect.top += m_config.shadowOffset;
        shadowRect.right += m_config.shadowOffset;
        shadowRect.bottom += m_config.shadowOffset;
        
        SetTextColor(hdc, RGB(0, 0, 0));
        DrawTextW(hdc, &pieceChar, 1, &shadowRect, 
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    // Draw highlight border around square
    void ChessPieceRenderer::DrawHighlight(HDC hdc, const RECT& rect, COLORREF color, int thickness)
    {
        HPEN pen = CreatePen(PS_SOLID, thickness, color);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        
        // Draw rectangle without fill
        Rectangle(hdc, rect.left + thickness/2, rect.top + thickness/2, 
                 rect.right - thickness/2, rect.bottom - thickness/2);
        
        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
    }
}