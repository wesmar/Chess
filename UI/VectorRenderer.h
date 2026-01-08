// VectorRenderer.h
// Chess board and piece rendering using Unicode symbols
// Provides configurable vector-based rendering with GDI fonts
#pragma once

#include "../Engine/ChessConstants.h"
#include "../Engine/Piece.h"
#include <windows.h>
#include <vector>
#include <array>
#include <memory>

namespace Chess
{
    // ========== CHESS PIECE RENDERER ==========
    // Renders chess board and pieces using Unicode chess symbols (♔♕♖♗♘♙)
    // Supports configurable colors, highlighting, shadows, and board orientation
    class ChessPieceRenderer
    {
    public:
        ChessPieceRenderer();
        
        // ========== RENDERING METHODS ==========
        
        // Render single piece with optional selection/highlight border
        // @param hdc: Device context for drawing
        // @param rect: Square bounds for piece placement
        // @param piece: Piece to render (or empty)
        // @param isSelected: Draw orange selection border
        // @param isHighlighted: Draw yellow highlight border (legal move destination)
        void RenderPiece(HDC hdc, const RECT& rect, Piece piece, 
                        bool isSelected = false, bool isHighlighted = false);
        
        // Render complete 8x8 chess board with all pieces
        // Automatically handles board flipping based on configuration
        // @param hdc: Device context for drawing
        // @param clientRect: Window client area (board is centered)
        // @param board: 64-element piece array (a1=0, h8=63)
        // @param selectedSquare: Square index to highlight with selection border (-1 = none)
        // @param highlightSquares: Square indices to highlight as legal move destinations
        void RenderBoard(HDC hdc, const RECT& clientRect, 
                        const std::array<Piece, SQUARE_COUNT>& board,
                        int selectedSquare = -1,
                        const std::vector<int>& highlightSquares = {});

        // Render coordinate labels (a-h, 1-8) around board
        // Respects board flip setting
        void RenderCoordinates(HDC hdc, const RECT& boardRect);
        
        // ========== CONFIGURATION ==========
        // Visual settings for board and piece rendering
        struct RenderConfig
        {
            // Color scheme
            COLORREF lightSquareColor = RGB(240, 240, 245);     // Light squares (white)
            COLORREF darkSquareColor = RGB(70, 80, 100);        // Dark squares (black)
            COLORREF highlightColor = RGB(50, 205, 50);         // Legal move destination highlight
            COLORREF selectionColor = RGB(255, 200, 100);       // Selected piece highlight
            COLORREF lightPieceColor = RGB(255, 235, 180);      // White pieces (cream)
            COLORREF darkPieceColor = RGB(20, 20, 20);          // Black pieces (near-black)
            COLORREF coordinateColor = RGB(150, 150, 150);      // File/rank labels (gray)
            COLORREF backgroundColor = RGB(30, 32, 38);         // Window background (dark)
            COLORREF borderColor = RGB(60, 65, 75);             // Board border (medium gray)
            
            // Display settings
            double pieceScale = 0.75;                           // Piece size as fraction of square size
            bool showCoordinates = true;                        // Show a-h and 1-8 labels
            bool flipBoard = false;                             // View from black's perspective
            bool useShadow = true;                              // Enable drop shadows on pieces
            bool useBlackPieceOutline = true;                   // Draw outline for visibility
            int shadowOffset = 2;                               // Shadow offset in pixels
        };

        // Get current rendering configuration
        const RenderConfig& GetConfig() const { return m_config; }
        
        // Update rendering configuration and invalidate cached fonts
        void SetConfig(const RenderConfig& config) { m_config = config; }

    private:
        RenderConfig m_config;
        HFONT m_pieceFont = nullptr;        // Unicode symbol font for pieces
        HFONT m_coordinateFont = nullptr;   // Font for coordinate labels

        // ========== UNICODE CHESS SYMBOLS ==========
        // Unicode code points for chess piece glyphs
        static constexpr wchar_t WHITE_KING = 0x2654;    // ♔
        static constexpr wchar_t WHITE_QUEEN = 0x2655;   // ♕
        static constexpr wchar_t WHITE_ROOK = 0x2656;    // ♖
        static constexpr wchar_t WHITE_BISHOP = 0x2657;  // ♗
        static constexpr wchar_t WHITE_KNIGHT = 0x2658;  // ♘
        static constexpr wchar_t WHITE_PAWN = 0x2659;    // ♙
        
        static constexpr wchar_t BLACK_KING = 0x265A;    // ♚
        static constexpr wchar_t BLACK_QUEEN = 0x265B;   // ♛
        static constexpr wchar_t BLACK_ROOK = 0x265C;    // ♜
        static constexpr wchar_t BLACK_BISHOP = 0x265D;  // ♝
        static constexpr wchar_t BLACK_KNIGHT = 0x265E;  // ♞
        static constexpr wchar_t BLACK_PAWN = 0x265F;    // ♟

        // ========== HELPER METHODS ==========
        
        // Map piece to appropriate Unicode character
        wchar_t GetPieceChar(Piece piece) const;
        
        // Draw single square with light or dark color
        void DrawChessSquare(HDC hdc, const RECT& rect, bool isDark);
        
        // Draw piece symbol with optional outline for visibility
        void DrawPiece(HDC hdc, const RECT& rect, Piece piece);
        
        // Draw colored border around square (selection/highlight)
        void DrawHighlight(HDC hdc, const RECT& rect, COLORREF color, int thickness = 3);
        
        // Draw drop shadow for piece (currently unused)
        void DrawPieceShadow(HDC hdc, const RECT& rect, wchar_t pieceChar);
        
        // Calculate screen rectangle for board square (respects flip setting)
        // @param boardRect: Board bounds in screen coordinates
        // @param file: File index 0-7 (a-h)
        // @param rank: Rank index 0-7 (1-8)
        RECT GetSquareRect(const RECT& boardRect, int file, int rank) const;
        
        // Create GDI font for piece rendering at specified size
        HFONT CreatePieceFont(int size);
        
        // Create GDI font for coordinate labels at specified size
        HFONT CreateCoordinateFont(int size);
    };
}