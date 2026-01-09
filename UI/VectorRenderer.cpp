// VectorRenderer.cpp
// Chess board and piece rendering using Unicode symbols
// Renders 8x8 board with piece-square highlighting, coordinate labels,
// and configurable color schemes using GDI fonts and text rendering

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

        // Create a copy of the rect to modify position without affecting the highlight
        RECT pieceRect = rect;

        // Draw background highlight if square is selected or highlighted
        if (isSelected)
        {
            DrawHighlight(hdc, rect, m_config.selectionColor, 4);
            
            // "Lift" the piece slightly up to simulate it being picked up
            // This adds a nice tactile feel to the selection
            int liftOffset = (rect.bottom - rect.top) / 10; // 10% of square size
            pieceRect.top -= liftOffset;
            pieceRect.bottom -= liftOffset;
            
            // Optional: Shift slightly left too for a subtle 3D shadow effect perspective
            // pieceRect.left -= 2;
            // pieceRect.right -= 2;
        }
        else if (isHighlighted)
        {
            DrawHighlight(hdc, rect, m_config.highlightColor, 3);
        }
        
        // Pass the modified (lifted) rect to the drawing function
        DrawPiece(hdc, pieceRect, piece);
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
			clientRect.top + (clientRect.bottom - clientRect.top - boardSize) / 2 - 20,
			clientRect.left + (clientRect.right - clientRect.left - 300 + boardSize) / 2,
			clientRect.top + (clientRect.bottom - clientRect.top + boardSize) / 2 - 20
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
				
				// Draw piece if present
				Piece piece = board[squareIndex];
				if (piece)
				{
					bool isSelected = (squareIndex == selectedSquare);
					bool isHighlighted = std::find(highlightSquares.begin(), 
												   highlightSquares.end(),
												   squareIndex) != highlightSquares.end();
					
					// Draw capture hint BEFORE piece (4 corner diamonds)
					if (isHighlighted)
					{
						DrawCaptureHighlight(hdc, squareRect, m_config.highlightColor);
					}
					
					RenderPiece(hdc, squareRect, piece, isSelected, false); // Nie podświetlaj już w RenderPiece
				}
				else
				{
					// Draw move hint on empty square (large center diamond)
					if (std::find(highlightSquares.begin(), highlightSquares.end(),
								 squareIndex) != highlightSquares.end())
					{
						DrawHighlight(hdc, squareRect, m_config.highlightColor, 3);
					}
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

    // Draw piece symbol with optional outline for visibility
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
        
        // Draw drop shadow first for depth effect
        if (m_config.useShadow)
        {
            DrawPieceShadow(hdc, rect, pieceChar);
        }
        
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

    // Draw shadow effect for piece
    void ChessPieceRenderer::DrawPieceShadow(HDC hdc, const RECT& rect, wchar_t pieceChar)
    {
        RECT shadowRect = rect;
        shadowRect.left += m_config.shadowOffset;
        shadowRect.top += m_config.shadowOffset;
        shadowRect.right += m_config.shadowOffset;
        shadowRect.bottom += m_config.shadowOffset;

        // Use a dark gray instead of pure black for a softer, more elegant look
        // Pure black creates too much contrast on light squares
        SetTextColor(hdc, RGB(80, 80, 90)); 
        
        // Draw the shadow text
        DrawTextW(hdc, &pieceChar, 1, &shadowRect, 
                 DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

	// Draw highlight on a square (border for selection, diamond for move hints)
	void ChessPieceRenderer::DrawHighlight(HDC hdc, const RECT& rect, COLORREF color, int thickness)
	{
		// If thickness is high, this represents a selected piece - draw border
		if (thickness >= 4)
		{
			HBRUSH frameBrush = CreateSolidBrush(color);
			RECT frameRect = rect;
			FrameRect(hdc, &frameRect, frameBrush);
			InflateRect(&frameRect, -1, -1);
			FrameRect(hdc, &frameRect, frameBrush);
			DeleteObject(frameBrush);
			return;
		}

		int width = rect.right - rect.left;
		int height = rect.bottom - rect.top;

		// Diamond size
		int size = width / 6;

		// Outline color (darker)
		BYTE r = GetRValue(color);
		BYTE g = GetGValue(color);
		BYTE b = GetBValue(color);
		COLORREF outlineColor = RGB(r * 0.6, g * 0.6, b * 0.6);

		HBRUSH hBrush = CreateSolidBrush(color);
		HPEN hPen = CreatePen(PS_SOLID, 1, outlineColor);
		HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
		HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

		// Draw ONE LARGE diamond in center
		int centerX = rect.left + width / 2;
		int centerY = rect.top + height / 2;
		
		POINT vertices[4];
		vertices[0] = { centerX, centerY - size };
		vertices[1] = { centerX + size, centerY };
		vertices[2] = { centerX, centerY + size };
		vertices[3] = { centerX - size, centerY };
		
		Polygon(hdc, vertices, 4);

		SelectObject(hdc, hOldBrush);
		SelectObject(hdc, hOldPen);
		DeleteObject(hBrush);
		DeleteObject(hPen);
	}

	// Draw 4 corner diamonds for capture hints
	void ChessPieceRenderer::DrawCaptureHighlight(HDC hdc, const RECT& rect, COLORREF color)
	{
		int width = rect.right - rect.left;
		int height = rect.bottom - rect.top;
		int size = width / 12; // Smaller diamonds for corners

		BYTE r = GetRValue(color);
		BYTE g = GetGValue(color);
		BYTE b = GetBValue(color);
		COLORREF outlineColor = RGB(r * 0.6, g * 0.6, b * 0.6);

		HBRUSH hBrush = CreateSolidBrush(color);
		HPEN hPen = CreatePen(PS_SOLID, 1, outlineColor);
		HBRUSH hOldBrush = (HBRUSH)SelectObject(hdc, hBrush);
		HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);

		// 4 corner positions
		POINT corners[4] = {
			{ rect.left + width / 6, rect.top + height / 6 },       // Top-left
			{ rect.right - width / 6, rect.top + height / 6 },      // Top-right
			{ rect.left + width / 6, rect.bottom - height / 6 },    // Bottom-left
			{ rect.right - width / 6, rect.bottom - height / 6 }    // Bottom-right
		};

		// Draw diamond at each corner
		for (const auto& corner : corners)
		{
			POINT vertices[4];
			vertices[0] = { corner.x, corner.y - size };
			vertices[1] = { corner.x + size, corner.y };
			vertices[2] = { corner.x, corner.y + size };
			vertices[3] = { corner.x - size, corner.y };
			Polygon(hdc, vertices, 4);
		}

		SelectObject(hdc, hOldBrush);
		SelectObject(hdc, hOldPen);
		DeleteObject(hBrush);
		DeleteObject(hPen);
	}
}