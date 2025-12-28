// VectorRenderer.h
#pragma once

#include "../Engine/ChessConstants.h"
#include "../Engine/Piece.h"
#include <windows.h>
#include <vector>
#include <array>
#include <memory>

namespace Chess
{
    // ---------- Chess Piece Renderer (Unicode Font Based) ----------
    // Renders chess board and pieces using Unicode chess symbols
    class ChessPieceRenderer
    {
    public:
        ChessPieceRenderer();
        
        // Render single piece with optional selection/highlight
        void RenderPiece(HDC hdc, const RECT& rect, Piece piece, 
                        bool isSelected = false, bool isHighlighted = false);
        
        // Render complete chess board with all pieces
        void RenderBoard(HDC hdc, const RECT& clientRect, 
                        const std::array<Piece, SQUARE_COUNT>& board,
                        int selectedSquare = -1,
                        const std::vector<int>& highlightSquares = {});

        // Render coordinate labels around board
        void RenderCoordinates(HDC hdc, const RECT& boardRect);
        
        // ---------- Configuration ----------
        struct RenderConfig
        {
            // Color scheme
            COLORREF lightSquareColor = RGB(240, 240, 245);     // Very light gray-blue
            COLORREF darkSquareColor = RGB(70, 80, 100);        // Dark blue-gray
            COLORREF highlightColor = RGB(255, 215, 0);             // Highlight Color
            COLORREF selectionColor = RGB(255, 200, 100);       // Orange selection
            COLORREF lightPieceColor = RGB(255, 235, 180);      // White pieces
            COLORREF darkPieceColor = RGB(20, 20, 20);          // Black pieces
            COLORREF coordinateColor = RGB(150, 150, 150);      // Gray coordinates
            COLORREF backgroundColor = RGB(30, 32, 38);         // Dark background
            COLORREF borderColor = RGB(60, 65, 75);             // Border color
            
            // Display settings
            double pieceScale = 0.75;                           // Piece size relative to square
            bool showCoordinates = true;                        // Show file/rank labels
            bool flipBoard = false;                             // View from black's perspective
            bool useShadow = true;                              // Enable piece shadows
            bool useBlackPieceOutline = true;                   // Outline for visibility
            int shadowOffset = 2;                               // Shadow offset in pixels
        };

        const RenderConfig& GetConfig() const { return m_config; }
        void SetConfig(const RenderConfig& config) { m_config = config; }

    private:
        RenderConfig m_config;
        HFONT m_pieceFont = nullptr;
        HFONT m_coordinateFont = nullptr;

        // ---------- Unicode Chess Pieces ----------
        static constexpr wchar_t WHITE_KING = 0x2654;
        static constexpr wchar_t WHITE_QUEEN = 0x2655;
        static constexpr wchar_t WHITE_ROOK = 0x2656;
        static constexpr wchar_t WHITE_BISHOP = 0x2657;
        static constexpr wchar_t WHITE_KNIGHT = 0x2658;
        static constexpr wchar_t WHITE_PAWN = 0x2659;
        
        static constexpr wchar_t BLACK_KING = 0x265A;
        static constexpr wchar_t BLACK_QUEEN = 0x265B;
        static constexpr wchar_t BLACK_ROOK = 0x265C;
        static constexpr wchar_t BLACK_BISHOP = 0x265D;
        static constexpr wchar_t BLACK_KNIGHT = 0x265E;
        static constexpr wchar_t BLACK_PAWN = 0x265F;

        // ---------- Helper Methods ----------
        wchar_t GetPieceChar(Piece piece) const;
        void DrawChessSquare(HDC hdc, const RECT& rect, bool isDark);
        void DrawPiece(HDC hdc, const RECT& rect, Piece piece);
        void DrawHighlight(HDC hdc, const RECT& rect, COLORREF color, int thickness = 3);
        void DrawPieceShadow(HDC hdc, const RECT& rect, wchar_t pieceChar);
        
        RECT GetSquareRect(const RECT& boardRect, int file, int rank) const;
        
        HFONT CreatePieceFont(int size);
        HFONT CreateCoordinateFont(int size);
    };
}