// MoveGenerator.cpp
// Move generation engine for chess - handles pseudo-legal and legal move generation
// 
// This module is responsible for generating all possible moves in a given position.
// It provides two main generation modes:
// 1. Pseudo-legal moves - all moves that follow piece movement rules (may leave king in check)
// 2. Tactical moves - only captures and promotions (used in quiescence search)
//
// The generator uses piece lists for efficient iteration when available, falling back
// to full board scan when necessary. Move generation is performance-critical as it's
// called millions of times during search.

#include "MoveGenerator.h"
#include "Board.h"
#include <algorithm>

namespace Chess
{
    // Generate all pseudo-legal moves for the current position
    // Pseudo-legal means moves follow piece rules but may leave king in check
    // These moves must be validated before being played in actual game
    //
    // Uses piece list optimization when available for faster iteration
    // Falls back to full board scan if piece list is not provided
    MoveList MoveGenerator::GeneratePseudoLegalMoves(
        const std::array<Piece, SQUARE_COUNT>& board,
        PlayerColor sideToMove,
        int enPassantSquare,
        const std::array<bool, 4>* castlingRights,
        const PieceList* pieceList)
    {
        MoveList moves;

        // Fast path: iterate only over pieces of current side using piece list
        // This is much faster than scanning all 64 squares
        if (pieceList)
        {
            for (int idx = 0; idx < pieceList->count; ++idx)
            {
                int i = pieceList->squares[idx];
                Piece piece = board[i];

                // Generate moves specific to each piece type
                // Each piece has different movement patterns and rules
                switch (piece.GetType())
                {
                case PieceType::Pawn:
                {
                    GeneratePawnMoves(moves, board, i, sideToMove, enPassantSquare);
                    break;
                }
                case PieceType::Knight:
                {
                    GenerateKnightMoves(moves, board, i, sideToMove);
                    break;
                }
                case PieceType::Bishop:
                {
                    GenerateBishopMoves(moves, board, i, sideToMove);
                    break;
                }
                case PieceType::Rook:
                {
                    GenerateRookMoves(moves, board, i, sideToMove);
                    break;
                }
                case PieceType::Queen:
                {
                    GenerateQueenMoves(moves, board, i, sideToMove);
                    break;
                }
                case PieceType::King:
                {
                    GenerateKingMoves(moves, board, i, sideToMove, castlingRights);
                    break;
                }
                default:
                    break;
                }
            }
        }
        else
        {
            // Slow path: scan entire board when piece list not available
            // Required during initialization or when piece list is not maintained
            for (int i = 0; i < SQUARE_COUNT; ++i)
            {
                Piece piece = board[i];
                if (piece.IsEmpty() || !piece.IsColor(sideToMove))
                    continue;

                switch (piece.GetType())
                {
                case PieceType::Pawn:
                {
                    GeneratePawnMoves(moves, board, i, sideToMove, enPassantSquare);
                    break;
                }
                case PieceType::Knight:
                {
                    GenerateKnightMoves(moves, board, i, sideToMove);
                    break;
                }
                case PieceType::Bishop:
                {
                    GenerateBishopMoves(moves, board, i, sideToMove);
                    break;
                }
                case PieceType::Rook:
                {
                    GenerateRookMoves(moves, board, i, sideToMove);
                    break;
                }
                case PieceType::Queen:
                {
                    GenerateQueenMoves(moves, board, i, sideToMove);
                    break;
                }
                case PieceType::King:
                {
                    GenerateKingMoves(moves, board, i, sideToMove, castlingRights);
                    break;
                }
                default:
                    break;
                }
            }
        }

        return moves;
    }

    // Generate only tactical moves (captures and promotions)
    // Used in quiescence search to resolve tactical complications
    // This avoids the horizon effect by searching forcing moves deeply
    //
    // Note: Castling is excluded as it's not considered tactical
    MoveList MoveGenerator::GenerateTacticalMoves(
        const std::array<Piece, SQUARE_COUNT>& board,
        PlayerColor sideToMove,
        int enPassantSquare,
        const PieceList* pieceList)
    {
        MoveList allMoves;
        MoveList moves;

        // Optimize with piece list when available
        if (pieceList)
        {
            for (int idx = 0; idx < pieceList->count; ++idx)
            {
                int i = pieceList->squares[idx];
                Piece piece = board[i];

                int startCount = allMoves.count;

                // Generate all moves for this piece
                switch (piece.GetType())
                {
                case PieceType::Pawn:
                    GeneratePawnMoves(allMoves, board, i, sideToMove, enPassantSquare);
                    break;
                case PieceType::Knight:
                    GenerateKnightMoves(allMoves, board, i, sideToMove);
                    break;
                case PieceType::Bishop:
                    GenerateBishopMoves(allMoves, board, i, sideToMove);
                    break;
                case PieceType::Rook:
                    GenerateRookMoves(allMoves, board, i, sideToMove);
                    break;
                case PieceType::Queen:
                    GenerateQueenMoves(allMoves, board, i, sideToMove);
                    break;
                case PieceType::King:
                    // Pass nullptr for castling rights since castling is not tactical
                    GenerateKingMoves(allMoves, board, i, sideToMove, nullptr);
                    break;
                default:
                    break;
                }

                // Filter to only tactical moves (captures and promotions)
                for (int j = startCount; j < allMoves.count; ++j)
                {
                    const auto& move = allMoves[j];
                    if (move.IsCapture() || move.IsPromotion())
                    {
                        moves.push_back(move);
                    }
                }
                allMoves.count = startCount;
            }
        }
        else
        {
            // Full board scan fallback
            for (int i = 0; i < SQUARE_COUNT; ++i)
            {
                Piece piece = board[i];
                if (piece.IsEmpty() || !piece.IsColor(sideToMove))
                    continue;

                int startCount = allMoves.count;

                switch (piece.GetType())
                {
                case PieceType::Pawn:
                    GeneratePawnMoves(allMoves, board, i, sideToMove, enPassantSquare);
                    break;
                case PieceType::Knight:
                    GenerateKnightMoves(allMoves, board, i, sideToMove);
                    break;
                case PieceType::Bishop:
                    GenerateBishopMoves(allMoves, board, i, sideToMove);
                    break;
                case PieceType::Rook:
                    GenerateRookMoves(allMoves, board, i, sideToMove);
                    break;
                case PieceType::Queen:
                    GenerateQueenMoves(allMoves, board, i, sideToMove);
                    break;
                case PieceType::King:
                    GenerateKingMoves(allMoves, board, i, sideToMove, nullptr);
                    break;
                default:
                    break;
                }

                for (int j = startCount; j < allMoves.count; ++j)
                {
                    const auto& move = allMoves[j];
                    if (move.IsCapture() || move.IsPromotion())
                    {
                        moves.push_back(move);
                    }
                }
                allMoves.count = startCount;
            }
        }

        return moves;
    }
	
	// Generate all legal pawn moves from given square
    // Pawns are the most complex piece due to special rules:
    // - Move forward one square (or two from starting rank)
    // - Capture diagonally
    // - En passant capture
    // - Promotion on reaching back rank
    //
    // Direction and rules depend on pawn color (white moves up, black moves down)
    void MoveGenerator::GeneratePawnMoves(
        MoveList& moves,
        const std::array<Piece, SQUARE_COUNT>& board,
        int square,
        PlayerColor color,
        int enPassantSquare)
    {

        // Calculate direction and special ranks based on pawn color
        int direction = (color == PlayerColor::White) ? 8 : -8;  // White moves up (+8), black down (-8)
        int startRank = (color == PlayerColor::White) ? 1 : 6;   // Starting rank for two-square moves
        int promotionRank = (color == PlayerColor::White) ? 7 : 0; // Back rank where promotion occurs

        auto [file, rank] = IndexToCoordinate(square);

        // Forward move (one square)
        int oneForward = square + direction;
        if (oneForward >= 0 && oneForward < SQUARE_COUNT && board[oneForward].IsEmpty())
        {
            auto [toFile, toRank] = IndexToCoordinate(oneForward);

            // Check if pawn reaches promotion rank
            if (toRank == promotionRank)
            {
                // Generate all four promotion options (Queen, Rook, Bishop, Knight)
                // Queen promotion is most common, but underpromotion can be useful
                moves.push_back(Move(square, oneForward, MoveType::Promotion, PieceType::Queen));
                moves.push_back(Move(square, oneForward, MoveType::Promotion, PieceType::Rook));
                moves.push_back(Move(square, oneForward, MoveType::Promotion, PieceType::Bishop));
                moves.push_back(Move(square, oneForward, MoveType::Promotion, PieceType::Knight));
            }
            else
            {
                moves.push_back(Move(square, oneForward, MoveType::Normal));
            }

            // Two-square move from starting position
            // Only legal if pawn is on starting rank and both squares ahead are empty
            if (rank == startRank)
            {
                int twoForward = square + 2 * direction;
                if (twoForward >= 0 && twoForward < SQUARE_COUNT && board[twoForward].IsEmpty())
                {
                    moves.push_back(Move(square, twoForward, MoveType::Normal));
                }
            }
        }

        // Diagonal capture left
        // Pawns capture one square diagonally forward
        if (file > 0)
        {
            int captureLeft = square + direction - 1;
            if (captureLeft >= 0 && captureLeft < SQUARE_COUNT)
            {
                Piece target = board[captureLeft];
                if (!target.IsEmpty() && target.IsOppositeColor(board[square]))
                {
                    auto [toFile, toRank] = IndexToCoordinate(captureLeft);

                    // Check for capture with promotion
                    if (toRank == promotionRank)
                    {
                        // All four promotion options for capturing moves
                        moves.push_back(Move(square, captureLeft, MoveType::Capture, PieceType::Queen, target));
                        moves.push_back(Move(square, captureLeft, MoveType::Capture, PieceType::Rook, target));
                        moves.push_back(Move(square, captureLeft, MoveType::Capture, PieceType::Bishop, target));
                        moves.push_back(Move(square, captureLeft, MoveType::Capture, PieceType::Knight, target));
                    }
                    else
                    {
                        moves.push_back(Move(square, captureLeft, MoveType::Capture, PieceType::None, target));
                    }
                }
            }
        }

        // Diagonal capture right
        if (file < 7)
        {
            int captureRight = square + direction + 1;
            if (captureRight >= 0 && captureRight < SQUARE_COUNT)
            {
                Piece target = board[captureRight];
                if (!target.IsEmpty() && target.IsOppositeColor(board[square]))
                {
                    auto [toFile, toRank] = IndexToCoordinate(captureRight);

                    if (toRank == promotionRank)
                    {
                        moves.push_back(Move(square, captureRight, MoveType::Capture, PieceType::Queen, target));
                        moves.push_back(Move(square, captureRight, MoveType::Capture, PieceType::Rook, target));
                        moves.push_back(Move(square, captureRight, MoveType::Capture, PieceType::Bishop, target));
                        moves.push_back(Move(square, captureRight, MoveType::Capture, PieceType::Knight, target));
                    }
                    else
                    {
                        moves.push_back(Move(square, captureRight, MoveType::Capture, PieceType::None, target));
                    }
                }
            }
        }

        // En passant capture - special pawn capture rule
        // If opponent pawn just moved two squares forward, it can be captured as if it moved one
        // This must happen immediately on the next move or the right is lost
        if (enPassantSquare >= 0)
        {
            int epRank = (color == PlayerColor::White) ? 4 : 3; // Rank where en passant is possible
            if (rank == epRank)
            {
                // Check if en passant square is diagonally adjacent
                if (file > 0 && enPassantSquare == square + direction - 1)
                {
                    // Create captured pawn piece for move record
                    moves.push_back(Move(square, enPassantSquare, MoveType::EnPassant, PieceType::None,
                                       Piece(PieceType::Pawn, (color == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White)));
                }
                else if (file < 7 && enPassantSquare == square + direction + 1)
                {
                    moves.push_back(Move(square, enPassantSquare, MoveType::EnPassant, PieceType::None,
                                       Piece(PieceType::Pawn, (color == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White)));
                }
            }
        }
    }

    // Generate all legal knight moves from given square
    // Knights move in an L-shape: 2 squares in one direction, 1 square perpendicular
    // Knights are the only piece that can "jump over" other pieces
    void MoveGenerator::GenerateKnightMoves(
        MoveList& moves,
        const std::array<Piece, SQUARE_COUNT>& board,
        int square,
        PlayerColor color)
    {
        (void)color; // Color not needed for knight move generation

        auto [file, rank] = IndexToCoordinate(square);

        // Try all 8 possible knight moves (L-shaped jumps)
        for (const auto& offset : KNIGHT_OFFSETS)
        {
            int newFile = file + offset.first;
            int newRank = rank + offset.second;

            if (IsValidCoordinate(newFile, newRank))
            {
                int targetSquare = CoordinateToIndex(newFile, newRank);
                Piece target = board[targetSquare];

                // Knight can move to empty squares or capture opponent pieces
                if (target.IsEmpty() || target.IsOppositeColor(board[square]))
                {
                    MoveType type = target.IsEmpty() ? MoveType::Normal : MoveType::Capture;
                    moves.push_back(Move(square, targetSquare, type, PieceType::None, target));
                }
            }
        }
    }

    // Generate all legal bishop moves from given square
    // Bishops move diagonally any number of squares until blocked
    // This is a sliding piece - moves along diagonals until hitting edge or another piece
    void MoveGenerator::GenerateBishopMoves(
        MoveList& moves,
        const std::array<Piece, SQUARE_COUNT>& board,
        int square,
        PlayerColor color)
    {
        (void)color;

        auto [file, rank] = IndexToCoordinate(square);

        // Scan along all 4 diagonal directions
        for (const auto& direction : BISHOP_DIRECTIONS)
        {
            int currentFile = file;
            int currentRank = rank;

            // Slide along diagonal until blocked or edge reached
            while (true)
            {
                currentFile += direction.first;
                currentRank += direction.second;

                if (!IsValidCoordinate(currentFile, currentRank))
                    break; // Hit board edge

                int targetSquare = CoordinateToIndex(currentFile, currentRank);
                Piece target = board[targetSquare];

                if (target.IsEmpty())
                {
                    // Empty square - add move and continue sliding
                    moves.push_back(Move(square, targetSquare, MoveType::Normal));
                }
                else if (target.IsOppositeColor(board[square]))
                {
                    // Opponent piece - can capture but must stop here
                    moves.push_back(Move(square, targetSquare, MoveType::Capture, PieceType::None, target));
                    break;
                }
                else
                {
                    // Own piece - blocked, cannot move here
                    break;
                }
            }
        }
    }

    // Generate all legal rook moves from given square
    // Rooks move horizontally or vertically any number of squares until blocked
    // Similar to bishop but along ranks/files instead of diagonals
    void MoveGenerator::GenerateRookMoves(
        MoveList& moves,
        const std::array<Piece, SQUARE_COUNT>& board,
        int square,
        PlayerColor color)
    {
        (void)color;

        auto [file, rank] = IndexToCoordinate(square);

        // Scan along all 4 orthogonal directions (up, down, left, right)
        for (const auto& direction : ROOK_DIRECTIONS)
        {
            int currentFile = file;
            int currentRank = rank;

            // Slide along rank/file until blocked or edge reached
            while (true)
            {
                currentFile += direction.first;
                currentRank += direction.second;

                if (!IsValidCoordinate(currentFile, currentRank))
                    break;

                int targetSquare = CoordinateToIndex(currentFile, currentRank);
                Piece target = board[targetSquare];

                if (target.IsEmpty())
                {
                    moves.push_back(Move(square, targetSquare, MoveType::Normal));
                }
                else if (target.IsOppositeColor(board[square]))
                {
                    moves.push_back(Move(square, targetSquare, MoveType::Capture, PieceType::None, target));
                    break;
                }
                else
                {
                    break;
                }
            }
        }
    }

    // Generate all legal queen moves from given square
    // Queens combine bishop and rook movement - can move any direction any distance
    // This is implemented by combining bishop and rook move generation
    void MoveGenerator::GenerateQueenMoves(
        MoveList& moves,
        const std::array<Piece, SQUARE_COUNT>& board,
        int square,
        PlayerColor color)
    {
        // Queen moves = Bishop moves + Rook moves
        GenerateBishopMoves(moves, board, square, color);
        GenerateRookMoves(moves, board, square, color);
    }
	
	// Generate all legal king moves from given square
	// Kings move one square in any direction (8 possible squares)
	// Special case: Castling - king moves two squares towards rook
	//
	// Castling requirements:
	// - King and rook haven't moved
	// - Squares between are empty
	// - King not in check
	// - King doesn't pass through or land on attacked square
	void MoveGenerator::GenerateKingMoves(
		MoveList& moves,
		const std::array<Piece, SQUARE_COUNT>& board,
		int square,
		PlayerColor color,
		const std::array<bool, 4>* castlingRights)
	{
		auto [file, rank] = IndexToCoordinate(square);

		// Regular king moves - one square in any direction
		for (const auto& offset : KING_OFFSETS)
		{
			int newFile = file + offset.first;
			int newRank = rank + offset.second;

			if (IsValidCoordinate(newFile, newRank))
			{
				int targetSquare = CoordinateToIndex(newFile, newRank);
				Piece target = board[targetSquare];

				if (target.IsEmpty() || target.IsOppositeColor(board[square]))
				{
					MoveType type = target.IsEmpty() ? MoveType::Normal : MoveType::Capture;
					moves.push_back(Move(square, targetSquare, type, PieceType::None, target));
				}
			}
		}

		// Early return if castling rights not provided (e.g., in tactical move generation)
		if (!castlingRights)
			return;

		PlayerColor opponentColor = (color == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White;

		// Castling rights indices: [White KS, White QS, Black KS, Black QS]
		const int kingSideIndex  = (color == PlayerColor::White) ? 0 : 2;
		const int queenSideIndex = (color == PlayerColor::White) ? 1 : 3;

		int row = (color == PlayerColor::White) ? 0 : 7;
		int kingStartSquare = CoordinateToIndex(4, row);      // e1 or e8
		int kingsideRookSquare = CoordinateToIndex(7, row);   // h1 or h8
		int queensideRookSquare = CoordinateToIndex(0, row);  // a1 or a8

		// Verify king is on its starting square before generating castling moves
		// This prevents generating castling moves if the king has moved and returned to center
		if (square != kingStartSquare)
			return;

		// Kingside castling (O-O)
		// King moves from e1/e8 to g1/g8, rook from h1/h8 to f1/f8
		if ((*castlingRights)[kingSideIndex])
		{
			Piece rook = board[kingsideRookSquare];

			// Verify rook is present and correct color
			if (rook.IsType(PieceType::Rook) && rook.IsColor(color))
			{
				// Check if squares f and g are empty (between king and rook)
				bool pathClear = true;
				for (int i = 1; i <= 2; ++i)
				{
					if (!board[square + i].IsEmpty())
					{
						pathClear = false;
						break;
					}
				}

				if (pathClear)
				{
					// King must not be in check currently
					if (!IsSquareAttacked(board, square, opponentColor))
					{
						// King must not pass through or land on attacked square
						bool pathSafe = true;
						for (int i = 1; i <= 2; ++i)
						{
							if (IsSquareAttacked(board, square + i, opponentColor))
							{
								pathSafe = false;
								break;
							}
						}

						if (pathSafe)
						{
							// Castling move: king moves two squares towards rook
							moves.push_back(Move(square, square + 2, MoveType::Castling));
						}
					}
				}
			}
		}

		// Queenside castling (O-O-O)
		// King moves from e1/e8 to c1/c8, rook from a1/a8 to d1/d8
		if ((*castlingRights)[queenSideIndex])
		{
			Piece rook = board[queensideRookSquare];

			if (rook.IsType(PieceType::Rook) && rook.IsColor(color))
			{
				// Check if squares b, c, d are empty (between king and rook)
				// Note: b square doesn't need to be safe, only c and d
				bool pathClear = true;
				for (int i = 1; i <= 3; ++i)
				{
					if (!board[square - i].IsEmpty())
					{
						pathClear = false;
						break;
					}
				}

				if (pathClear)
				{
					if (!IsSquareAttacked(board, square, opponentColor))
					{
						// Check squares c and d (king passes through d and lands on c)
						bool pathSafe = true;
						for (int i = 1; i <= 2; ++i)
						{
							if (IsSquareAttacked(board, square - i, opponentColor))
							{
								pathSafe = false;
								break;
							}
						}

						if (pathSafe)
						{
							moves.push_back(Move(square, square - 2, MoveType::Castling));
						}
					}
				}
			}
		}
	}

    // Check if a square is under attack by opponent pieces
    // Used for castling validation and check detection
    // This is a critical function called frequently during move generation
    //
    // Checks all possible attack patterns:
    // - Pawn attacks (diagonal)
    // - Knight attacks (L-shape)
    // - Bishop/Queen attacks (diagonals)
    // - Rook/Queen attacks (ranks/files)
    // - King attacks (adjacent squares)
    bool MoveGenerator::IsSquareAttacked(
        const std::array<Piece, SQUARE_COUNT>& board,
        int square,
        PlayerColor attackerColor)
    {
        auto [file, rank] = IndexToCoordinate(square);

        // Check for pawn attacks
        // Pawns attack one square diagonally forward (from attacker's perspective)
        if (attackerColor == PlayerColor::White)
        {
            // White pawns attack diagonally upward (from white's perspective)
            if (file > 0)
            {
                int pawnSquare = square - 9; // Down-left diagonal
                if (pawnSquare >= 0 && board[pawnSquare].IsType(PieceType::Pawn) &&
                    board[pawnSquare].IsColor(PlayerColor::White))
                {
                    return true;
                }
            }

            if (file < 7)
            {
                int pawnSquare = square - 7; // Down-right diagonal
                if (pawnSquare >= 0 && board[pawnSquare].IsType(PieceType::Pawn) &&
                    board[pawnSquare].IsColor(PlayerColor::White))
                {
                    return true;
                }
            }
        }
        else
        {
            // Black pawns attack diagonally downward (from white's perspective)
            if (file > 0)
            {
                int pawnSquare = square + 7; // Up-left diagonal
                if (pawnSquare < 64 && board[pawnSquare].IsType(PieceType::Pawn) &&
                    board[pawnSquare].IsColor(PlayerColor::Black))
                {
                    return true;
                }
            }

            if (file < 7)
            {
                int pawnSquare = square + 9; // Up-right diagonal
                if (pawnSquare < 64 && board[pawnSquare].IsType(PieceType::Pawn) &&
                    board[pawnSquare].IsColor(PlayerColor::Black))
                {
                    return true;
                }
            }
        }

        // Check for knight attacks
        // Knights attack in L-shape pattern (2+1 or 1+2 squares)
        for (const auto& offset : KNIGHT_OFFSETS)
        {
            int newFile = file + offset.first;
            int newRank = rank + offset.second;

            if (IsValidCoordinate(newFile, newRank))
            {
                int targetSquare = CoordinateToIndex(newFile, newRank);
                Piece target = board[targetSquare];

                if (target.IsType(PieceType::Knight) && target.IsColor(attackerColor))
                {
                    return true;
                }
            }
        }

        // Check for bishop/queen attacks along diagonals
        // Slide along diagonals until hitting a piece
        for (const auto& direction : BISHOP_DIRECTIONS)
        {
            int currentFile = file;
            int currentRank = rank;

            while (true)
            {
                currentFile += direction.first;
                currentRank += direction.second;

                if (!IsValidCoordinate(currentFile, currentRank))
                    break;

                int targetSquare = CoordinateToIndex(currentFile, currentRank);
                Piece target = board[targetSquare];

                if (!target.IsEmpty())
                {
                    // Found a piece - check if it's an attacking bishop or queen
                    if (target.IsColor(attackerColor))
                    {
                        if (target.IsType(PieceType::Bishop) || target.IsType(PieceType::Queen))
                        {
                            return true;
                        }
                    }
                    break; // Piece blocks the diagonal, stop searching this direction
                }
            }
        }

        // Check for rook/queen attacks along ranks and files
        // Slide along orthogonal directions until hitting a piece
        for (const auto& direction : ROOK_DIRECTIONS)
        {
            int currentFile = file;
            int currentRank = rank;

            while (true)
            {
                currentFile += direction.first;
                currentRank += direction.second;

                if (!IsValidCoordinate(currentFile, currentRank))
                    break;

                int targetSquare = CoordinateToIndex(currentFile, currentRank);
                Piece target = board[targetSquare];

                if (!target.IsEmpty())
                {
                    // Found a piece - check if it's an attacking rook or queen
                    if (target.IsColor(attackerColor))
                    {
                        if (target.IsType(PieceType::Rook) || target.IsType(PieceType::Queen))
                        {
                            return true;
                        }
                    }
                    break; // Piece blocks the rank/file, stop searching this direction
                }
            }
        }

        // Check for king attacks
        // King can only attack adjacent squares (one square in any direction)
        for (const auto& offset : KING_OFFSETS)
        {
            int newFile = file + offset.first;
            int newRank = rank + offset.second;

            if (IsValidCoordinate(newFile, newRank))
            {
                int targetSquare = CoordinateToIndex(newFile, newRank);
                Piece target = board[targetSquare];

                if (target.IsType(PieceType::King) && target.IsColor(attackerColor))
                {
                    return true;
                }
            }
        }

        // No attacks found
        return false;
    }
}