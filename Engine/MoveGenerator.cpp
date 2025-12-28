// MoveGenerator.cpp
#include "MoveGenerator.h"
#include <algorithm>
#include <vector>

namespace Chess
{
    // Generate all pseudo-legal moves for the current side to move
    std::vector<Move> MoveGenerator::GeneratePseudoLegalMoves(
        const std::array<Piece, SQUARE_COUNT>& board,
        PlayerColor sideToMove,
        int enPassantSquare,
        const std::array<bool, 4>* castlingRights)
    {
        std::vector<Move> moves;
        
        // Iterate through all squares on the board
        for (int i = 0; i < SQUARE_COUNT; ++i)
        {
            Piece piece = board[i];
            if (piece.IsEmpty() || !piece.IsColor(sideToMove))
                continue;
            
            // Generate moves based on piece type
            switch (piece.GetType())
            {
            case PieceType::Pawn:
            {
                auto pawnMoves = GeneratePawnMoves(board, i, sideToMove, enPassantSquare);
                moves.insert(moves.end(), pawnMoves.begin(), pawnMoves.end());
                break;
            }
            case PieceType::Knight:
            {
                auto knightMoves = GenerateKnightMoves(board, i, sideToMove);
                moves.insert(moves.end(), knightMoves.begin(), knightMoves.end());
                break;
            }
            case PieceType::Bishop:
            {
                auto bishopMoves = GenerateBishopMoves(board, i, sideToMove);
                moves.insert(moves.end(), bishopMoves.begin(), bishopMoves.end());
                break;
            }
            case PieceType::Rook:
            {
                auto rookMoves = GenerateRookMoves(board, i, sideToMove);
                moves.insert(moves.end(), rookMoves.begin(), rookMoves.end());
                break;
            }
            case PieceType::Queen:
            {
                auto queenMoves = GenerateQueenMoves(board, i, sideToMove);
                moves.insert(moves.end(), queenMoves.begin(), queenMoves.end());
                break;
            }
            case PieceType::King:
            {
                auto kingMoves = GenerateKingMoves(board, i, sideToMove, castlingRights);
                moves.insert(moves.end(), kingMoves.begin(), kingMoves.end());
                break;
            }
            default:
                break;
            }
        }
        
        return moves;
    }

    // Generate all legal pawn moves including captures, pushes, and en passant
    std::vector<Move> MoveGenerator::GeneratePawnMoves(
        const std::array<Piece, SQUARE_COUNT>& board,
        int square,
        PlayerColor color,
        int enPassantSquare)
    {
        std::vector<Move> moves;
        
        // Determine pawn movement direction and special ranks
        int direction = (color == PlayerColor::White) ? 8 : -8;
        int startRank = (color == PlayerColor::White) ? 1 : 6;
        int promotionRank = (color == PlayerColor::White) ? 7 : 0;
        
        auto [file, rank] = IndexToCoordinate(square);
        
        // Single square forward push
        int oneForward = square + direction;
        if (oneForward >= 0 && oneForward < SQUARE_COUNT && board[oneForward].IsEmpty())
        {
            auto [toFile, toRank] = IndexToCoordinate(oneForward);
            
            // Check for promotion
            if (toRank == promotionRank)
            {
                // Add all promotion options
                moves.emplace_back(square, oneForward, MoveType::Promotion, PieceType::Queen);
                moves.emplace_back(square, oneForward, MoveType::Promotion, PieceType::Rook);
                moves.emplace_back(square, oneForward, MoveType::Promotion, PieceType::Bishop);
                moves.emplace_back(square, oneForward, MoveType::Promotion, PieceType::Knight);
            }
            else
            {
                moves.emplace_back(square, oneForward, MoveType::Normal);
            }
            
            // Double square forward push from starting position
            if (rank == startRank)
            {
                int twoForward = square + 2 * direction;
                if (twoForward >= 0 && twoForward < SQUARE_COUNT && board[twoForward].IsEmpty())
                {
                    moves.emplace_back(square, twoForward, MoveType::Normal);
                }
            }
        }
        
        // Left diagonal capture
        if (file > 0)
        {
            int captureLeft = square + direction - 1;
            if (captureLeft >= 0 && captureLeft < SQUARE_COUNT)
            {
                Piece target = board[captureLeft];
                if (!target.IsEmpty() && target.IsOppositeColor(board[square]))
                {
                    auto [toFile, toRank] = IndexToCoordinate(captureLeft);
                    
                    // Check for promotion on capture
                    if (toRank == promotionRank)
                    {
                        moves.emplace_back(square, captureLeft, MoveType::Capture, PieceType::Queen, target);
                        moves.emplace_back(square, captureLeft, MoveType::Capture, PieceType::Rook, target);
                        moves.emplace_back(square, captureLeft, MoveType::Capture, PieceType::Bishop, target);
                        moves.emplace_back(square, captureLeft, MoveType::Capture, PieceType::Knight, target);
                    }
                    else
                    {
                        moves.emplace_back(square, captureLeft, MoveType::Capture, PieceType::None, target);
                    }
                }
            }
        }
        
        // Right diagonal capture
        if (file < 7)
        {
            int captureRight = square + direction + 1;
            if (captureRight >= 0 && captureRight < SQUARE_COUNT)
            {
                Piece target = board[captureRight];
                if (!target.IsEmpty() && target.IsOppositeColor(board[square]))
                {
                    auto [toFile, toRank] = IndexToCoordinate(captureRight);
                    
                    // Check for promotion on capture
                    if (toRank == promotionRank)
                    {
                        moves.emplace_back(square, captureRight, MoveType::Capture, PieceType::Queen, target);
                        moves.emplace_back(square, captureRight, MoveType::Capture, PieceType::Rook, target);
                        moves.emplace_back(square, captureRight, MoveType::Capture, PieceType::Bishop, target);
                        moves.emplace_back(square, captureRight, MoveType::Capture, PieceType::Knight, target);
                    }
                    else
                    {
                        moves.emplace_back(square, captureRight, MoveType::Capture, PieceType::None, target);
                    }
                }
            }
        }
        
        // En passant capture
        if (enPassantSquare >= 0)
        {
            // Verify pawn is on correct rank for en passant
            int epRank = (color == PlayerColor::White) ? 4 : 3;
            if (rank == epRank)
            {
                // Left en passant
                if (file > 0 && enPassantSquare == square + direction - 1)
                {
                    moves.emplace_back(square, enPassantSquare, MoveType::EnPassant, PieceType::None, 
                                       Piece(PieceType::Pawn, (color == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White));
                }
                // Right en passant
                else if (file < 7 && enPassantSquare == square + direction + 1)
                {
                    moves.emplace_back(square, enPassantSquare, MoveType::EnPassant, PieceType::None, 
                                       Piece(PieceType::Pawn, (color == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White));
                }
            }
        }
        
        return moves;
    }

    // Generate all knight moves using L-shaped jumps
    std::vector<Move> MoveGenerator::GenerateKnightMoves(
        const std::array<Piece, SQUARE_COUNT>& board,
        int square,
        PlayerColor color)
    {
        (void)color; // Unused parameter
        std::vector<Move> moves;

        auto [file, rank] = IndexToCoordinate(square);
        
        // Try all 8 possible knight moves
        for (const auto& offset : KNIGHT_OFFSETS)
        {
            int newFile = file + offset.first;
            int newRank = rank + offset.second;
            
            if (IsValidCoordinate(newFile, newRank))
            {
                int targetSquare = CoordinateToIndex(newFile, newRank);
                Piece target = board[targetSquare];
                
                // Can move to empty square or capture opposite color
                if (target.IsEmpty() || target.IsOppositeColor(board[square]))
                {
                    MoveType type = target.IsEmpty() ? MoveType::Normal : MoveType::Capture;
                    moves.emplace_back(square, targetSquare, type, PieceType::None, target);
                }
            }
        }
        
        return moves;
    }

    // Generate all bishop moves along diagonals
    std::vector<Move> MoveGenerator::GenerateBishopMoves(
        const std::array<Piece, SQUARE_COUNT>& board,
        int square,
        PlayerColor color)
    {
        (void)color; // Unused parameter
        std::vector<Move> moves;

        auto [file, rank] = IndexToCoordinate(square);
        
        // Check all 4 diagonal directions
        for (const auto& direction : BISHOP_DIRECTIONS)
        {
            int currentFile = file;
            int currentRank = rank;
            
            // Slide until hitting edge or piece
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
                    moves.emplace_back(square, targetSquare, MoveType::Normal);
                }
                else if (target.IsOppositeColor(board[square]))
                {
                    moves.emplace_back(square, targetSquare, MoveType::Capture, PieceType::None, target);
                    break; // Can't move past captured piece
                }
                else
                {
                    break; // Blocked by friendly piece
                }
            }
        }
        
        return moves;
    }

    // Generate all rook moves along ranks and files
    std::vector<Move> MoveGenerator::GenerateRookMoves(
        const std::array<Piece, SQUARE_COUNT>& board,
        int square,
        PlayerColor color)
    {
        (void)color; // Unused parameter
        std::vector<Move> moves;

        auto [file, rank] = IndexToCoordinate(square);
        
        // Check all 4 straight directions
        for (const auto& direction : ROOK_DIRECTIONS)
        {
            int currentFile = file;
            int currentRank = rank;
            
            // Slide until hitting edge or piece
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
                    moves.emplace_back(square, targetSquare, MoveType::Normal);
                }
                else if (target.IsOppositeColor(board[square]))
                {
                    moves.emplace_back(square, targetSquare, MoveType::Capture, PieceType::None, target);
                    break; // Can't move past captured piece
                }
                else
                {
                    break; // Blocked by friendly piece
                }
            }
        }
        
        return moves;
    }

    // Generate all queen moves (combination of rook and bishop)
    std::vector<Move> MoveGenerator::GenerateQueenMoves(
        const std::array<Piece, SQUARE_COUNT>& board,
        int square,
        PlayerColor color)
    {
        std::vector<Move> moves;
        
        // Queen moves like bishop
        auto bishopMoves = GenerateBishopMoves(board, square, color);
        moves.insert(moves.end(), bishopMoves.begin(), bishopMoves.end());
        
        // Queen moves like rook
        auto rookMoves = GenerateRookMoves(board, square, color);
        moves.insert(moves.end(), rookMoves.begin(), rookMoves.end());
        
        return moves;
    }
	// Generate all king moves including castling
	std::vector<Move> MoveGenerator::GenerateKingMoves(
		const std::array<Piece, SQUARE_COUNT>& board,
		int square,
		PlayerColor color,
		const std::array<bool, 4>* castlingRights)
	{
		std::vector<Move> moves;

		// Generate standard one-square king moves
		auto [file, rank] = IndexToCoordinate(square);

		// Try all 8 adjacent squares
		for (const auto& offset : KING_OFFSETS)
		{
			int newFile = file + offset.first;
			int newRank = rank + offset.second;

			if (IsValidCoordinate(newFile, newRank))
			{
				int targetSquare = CoordinateToIndex(newFile, newRank);
				Piece target = board[targetSquare];

				// Can move to empty square or capture opposite color
				if (target.IsEmpty() || target.IsOppositeColor(board[square]))
				{
					MoveType type = target.IsEmpty() ? MoveType::Normal : MoveType::Capture;
					moves.emplace_back(square, targetSquare, type, PieceType::None, target);
				}
			}
		}

		// Early return if castling rights not provided
		if (!castlingRights)
			return moves;

		// Determine opponent color for attack checking
		PlayerColor opponentColor = (color == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White;
		
		// Get castling right indices for this color
		const int kingSideIndex  = (color == PlayerColor::White) ? 0 : 2;
		const int queenSideIndex = (color == PlayerColor::White) ? 1 : 3;
		
		int row = (color == PlayerColor::White) ? 0 : 7;
		int kingsideRookSquare = CoordinateToIndex(7, row);
		int queensideRookSquare = CoordinateToIndex(0, row);

		// Check kingside castling eligibility
		if ((*castlingRights)[kingSideIndex])
		{
			Piece rook = board[kingsideRookSquare];
			
			// Verify rook exists and is correct color
			if (rook.IsType(PieceType::Rook) && rook.IsColor(color))
			{
				// Verify path is clear between king and rook (f and g files)
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
					// King must not be in check
					if (!IsSquareAttacked(board, square, opponentColor))
					{
						// Squares king passes through must not be attacked
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
							moves.emplace_back(square, square + 2, MoveType::Castling);
						}
					}
				}
			}
		}

		// Check queenside castling eligibility
		if ((*castlingRights)[queenSideIndex])
		{
			Piece rook = board[queensideRookSquare];
			
			// Verify rook exists and is correct color
			if (rook.IsType(PieceType::Rook) && rook.IsColor(color))
			{
				// Verify path is clear between king and rook (b, c, d files)
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
					// King must not be in check
					if (!IsSquareAttacked(board, square, opponentColor))
					{
						// Squares king passes through must not be attacked (c and d files)
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
							moves.emplace_back(square, square - 2, MoveType::Castling);
						}
					}
				}
			}
		}

		return moves;
	}

    // Check if a square is under attack by the specified color
    bool MoveGenerator::IsSquareAttacked(
        const std::array<Piece, SQUARE_COUNT>& board,
        int square,
        PlayerColor attackerColor)
    {
        auto [file, rank] = IndexToCoordinate(square);

        // Check for white pawn attacks (diagonal upward from target square)
        if (attackerColor == PlayerColor::White)
        {
            // White pawns attack from below-left diagonal
            if (file > 0)
            {
                int pawnSquare = square - 9;
                if (pawnSquare >= 0 && board[pawnSquare].IsType(PieceType::Pawn) &&
                    board[pawnSquare].IsColor(PlayerColor::White))
                {
                    return true;
                }
            }

            // White pawns attack from below-right diagonal
            if (file < 7)
            {
                int pawnSquare = square - 7;
                if (pawnSquare >= 0 && board[pawnSquare].IsType(PieceType::Pawn) &&
                    board[pawnSquare].IsColor(PlayerColor::White))
                {
                    return true;
                }
            }
        }
        else // Check for black pawn attacks (diagonal downward from target square)
        {
            // Black pawns attack from above-left diagonal
            if (file > 0)
            {
                int pawnSquare = square + 7;
                if (pawnSquare < 64 && board[pawnSquare].IsType(PieceType::Pawn) &&
                    board[pawnSquare].IsColor(PlayerColor::Black))
                {
                    return true;
                }
            }

            // Black pawns attack from above-right diagonal
            if (file < 7)
            {
                int pawnSquare = square + 9;
                if (pawnSquare < 64 && board[pawnSquare].IsType(PieceType::Pawn) &&
                    board[pawnSquare].IsColor(PlayerColor::Black))
                {
                    return true;
                }
            }
        }
        
        // Check for knight attacks (all 8 L-shaped positions)
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
        
        // Check for bishop and queen attacks along diagonals
        for (const auto& direction : BISHOP_DIRECTIONS)
        {
            int currentFile = file;
            int currentRank = rank;
            
            // Slide along diagonal until hitting piece or edge
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
                    // Found attacking bishop or queen
                    if (target.IsColor(attackerColor))
                    {
                        if (target.IsType(PieceType::Bishop) || target.IsType(PieceType::Queen))
                        {
                            return true;
                        }
                    }
                    break; // Path blocked by any piece
                }
            }
        }
        
        // Check for rook and queen attacks along ranks and files
        for (const auto& direction : ROOK_DIRECTIONS)
        {
            int currentFile = file;
            int currentRank = rank;
            
            // Slide along rank/file until hitting piece or edge
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
                    // Found attacking rook or queen
                    if (target.IsColor(attackerColor))
                    {
                        if (target.IsType(PieceType::Rook) || target.IsType(PieceType::Queen))
                        {
                            return true;
                        }
                    }
                    break; // Path blocked by any piece
                }
            }
        }
        
        // Check for king attacks (all 8 adjacent squares)
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
        
        return false; // No attacks found
    }
}