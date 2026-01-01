// Board.cpp
#include "Board.h"
#include "Zobrist.h"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <cassert>

namespace Chess
{
    // ---------- Board Implementation ----------
    
    // Default constructor - initializes Zobrist hashing and sets up starting position
    Board::Board()
    {
        Zobrist::Initialize();
        ResetToStartingPosition();
    }

    // Construct board from FEN string
    Board::Board(const std::string& fen)
    {
        Zobrist::Initialize();
        if (!LoadFEN(fen))
        {
            ResetToStartingPosition();
        }
    }

    // Reset board to standard chess starting position
    void Board::ResetToStartingPosition()
    {
        LoadFEN(Positions::STARTING_POSITION);
    }

    // Load board state from FEN notation
    bool Board::LoadFEN(const std::string& fen)
    {
        bool result = FENParser::Parse(fen, *this);
        if (result)
        {
            RecomputeZobristKey();
        }
        return result;
    }

    // Export current board state as FEN string
    std::string Board::GetFEN() const
    {
        return FENParser::Generate(*this);
    }

    // Recalculate Zobrist hash from scratch (used after FEN load)
    void Board::RecomputeZobristKey()
    {
        m_zobristKey = 0;

        // XOR all pieces into hash
        for (int sq = 0; sq < SQUARE_COUNT; ++sq)
        {
            Piece piece = m_board[sq];
            if (!piece.IsEmpty())
            {
                int pieceType = static_cast<int>(piece.GetType());
                int color = static_cast<int>(piece.GetColor());
                m_zobristKey ^= Zobrist::pieceKeys[pieceType][color][sq];
            }
        }

        // XOR side to move (only if black)
        if (m_sideToMove == PlayerColor::Black)
        {
            m_zobristKey ^= Zobrist::sideToMoveKey;
        }

        // XOR castling rights
        for (int i = 0; i < 4; ++i)
        {
            if (m_castlingRights[i])
            {
                m_zobristKey ^= Zobrist::castlingKeys[i];
            }
        }

        // XOR en passant file if applicable
        if (m_enPassantSquare >= 0)
        {
            int epFile = m_enPassantSquare % 8;
            m_zobristKey ^= Zobrist::enPassantKeys[epFile];
        }
    }

    // Generate all legal moves for current position
    std::vector<Move> Board::GenerateLegalMoves() const
    {
        Board temp = *this;

        // First generate pseudo-legal moves
        std::vector<Move> moves = MoveGenerator::GeneratePseudoLegalMoves(
            temp.m_board,
            temp.m_sideToMove,
            temp.m_enPassantSquare,
            &temp.m_castlingRights
        );

        std::vector<Move> legalMoves;
        legalMoves.reserve(moves.size());

        const PlayerColor movedColor = temp.m_sideToMove;
        const PlayerColor opponentColor = 
            (movedColor == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White;

        // Filter out moves that leave king in check
        for (const Move& move : moves)
        {
            temp.MakeMoveUnchecked(move);

            int kingSquare = temp.m_kingSquares[static_cast<int>(movedColor)];
            
            // Verify king exists and is not under attack
            if (kingSquare != -1 &&
                !MoveGenerator::IsSquareAttacked(temp.m_board, kingSquare, opponentColor))
            {
                legalMoves.push_back(move);
            }

            temp.UndoMove();
        }

        return legalMoves;
    }

    // Check if a specific move is legal in current position
    bool Board::IsMoveLegal(const Move& move) const
    {
        auto moves = GenerateLegalMoves();
        return std::any_of(
            moves.begin(),
            moves.end(),
            [&move](const Move& legalMove) {
                return legalMove.GetFrom() == move.GetFrom() &&
                       legalMove.GetTo() == move.GetTo() &&
                       legalMove.GetPromotion() == move.GetPromotion();
            }
        );
    }

    // Execute move without legality check (updates board state and Zobrist hash)
    bool Board::MakeMoveUnchecked(const Move& move)
    {
        // Save complete board state for undo
        MoveRecord record;
        record.move = move;
        record.capturedPiece = m_board[move.GetTo()];
        record.movedPiece = m_board[move.GetFrom()];
        record.previousEnPassant = m_enPassantSquare;
        record.previousCastlingRights = m_castlingRights;
        record.previousHalfMoveClock = m_halfMoveClock;
        record.previousKingSquares[0] = m_kingSquares[0];
        record.previousKingSquares[1] = m_kingSquares[1];
        record.previousZobristKey = m_zobristKey;

        const int from = move.GetFrom();
        const int to = move.GetTo();
        Piece movedPiece = m_board[from];

        // Remove piece from source square in Zobrist hash
        m_zobristKey ^= Zobrist::pieceKeys[static_cast<int>(movedPiece.GetType())][static_cast<int>(movedPiece.GetColor())][from];

        // Remove captured piece from Zobrist hash
        if (!m_board[to].IsEmpty())
        {
            Piece capturedPiece = m_board[to];
            m_zobristKey ^= Zobrist::pieceKeys[static_cast<int>(capturedPiece.GetType())][static_cast<int>(capturedPiece.GetColor())][to];
        }

        // Remove old en passant from hash
        if (m_enPassantSquare >= 0)
        {
            int oldEpFile = m_enPassantSquare % 8;
            m_zobristKey ^= Zobrist::enPassantKeys[oldEpFile];
        }

        // Remove old castling rights from hash
        for (int i = 0; i < 4; ++i)
        {
            if (m_castlingRights[i])
            {
                m_zobristKey ^= Zobrist::castlingKeys[i];
            }
        }

        // Update castling rights based on move
        UpdateCastlingRights(move.GetFrom(), move.GetTo(), m_board[move.GetFrom()]);

        // Add new castling rights to hash
        for (int i = 0; i < 4; ++i)
        {
            if (m_castlingRights[i])
            {
                m_zobristKey ^= Zobrist::castlingKeys[i];
            }
        }

        // Prepare castling rook data before moving pieces
        int castlingRookFrom = -1;
        int castlingRookTo = -1;
        Piece castlingRook = EMPTY_PIECE;

        if (move.IsCastling())
        {
            int row = (m_sideToMove == PlayerColor::White) ? 0 : 7;

            // Determine rook positions for kingside vs queenside castling
            if (move.GetTo() % 8 == 6) // Kingside
            {
                castlingRookFrom = CoordinateToIndex(7, row);
                castlingRookTo = CoordinateToIndex(5, row);
            }
            else // Queenside
            {
                castlingRookFrom = CoordinateToIndex(0, row);
                castlingRookTo = CoordinateToIndex(3, row);
            }
            
            castlingRook = m_board[castlingRookFrom];
            
            // Remove rook from starting square in Zobrist hash
            m_zobristKey ^= Zobrist::pieceKeys[static_cast<int>(castlingRook.GetType())][static_cast<int>(castlingRook.GetColor())][castlingRookFrom];
        }

        // Handle special moves (en passant, castling)
        HandleEnPassant(move);
        HandleCastling(move);

        // Execute the actual piece movement
        m_board[move.GetTo()] = m_board[move.GetFrom()];
        m_board[move.GetFrom()] = EMPTY_PIECE;
        m_board[move.GetTo()].SetMoved(true);

        // Handle pawn promotion
        if (move.IsPromotion())
        {
            m_board[move.GetTo()] = Piece(move.GetPromotion(), m_sideToMove, true);
        }

        // Add piece to destination square in Zobrist hash (after promotion)
        Piece finalPiece = m_board[to];
        m_zobristKey ^= Zobrist::pieceKeys[static_cast<int>(finalPiece.GetType())][static_cast<int>(finalPiece.GetColor())][to];

        // Update king position if king was moved
        if (m_board[move.GetTo()].GetType() == PieceType::King)
        {
            UpdateKingSquare(m_sideToMove, move.GetTo());
        }

        // Add rook to destination square in Zobrist hash (castling)
        if (move.IsCastling())
        {
            m_zobristKey ^= Zobrist::pieceKeys[static_cast<int>(castlingRook.GetType())][static_cast<int>(castlingRook.GetColor())][castlingRookTo];
        }

        // Handle en passant capture in Zobrist hash
        if (move.IsEnPassant())
        {
            int capturedPawnSquare = to + ((m_sideToMove == PlayerColor::White) ? -8 : 8);
            Piece capturedPawn = Piece(PieceType::Pawn, (m_sideToMove == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White);
            m_zobristKey ^= Zobrist::pieceKeys[static_cast<int>(capturedPawn.GetType())][static_cast<int>(capturedPawn.GetColor())][capturedPawnSquare];
        }

        // Update en passant square for next move
        m_enPassantSquare = -1;
        if (m_board[move.GetTo()].GetType() == PieceType::Pawn)
        {
            int delta = move.GetTo() - move.GetFrom();
            // Double pawn push creates en passant opportunity
            if (std::abs(delta) == 16)
            {
                m_enPassantSquare = move.GetFrom() + delta / 2;
                int newEpFile = m_enPassantSquare % 8;
                m_zobristKey ^= Zobrist::enPassantKeys[newEpFile];
            }
        }

        // Update fifty-move rule counter
        m_halfMoveClock++;
        if (move.IsCapture() || m_board[move.GetTo()].GetType() == PieceType::Pawn)
        {
            m_halfMoveClock = 0; // Reset on pawn move or capture
        }

        // Increment full move number after black moves
        if (m_sideToMove == PlayerColor::Black)
        {
            m_fullMoveNumber++;
        }

        // Flip side to move in Zobrist hash
        m_zobristKey ^= Zobrist::sideToMoveKey;

        // Switch active player
        m_sideToMove = (m_sideToMove == PlayerColor::White)
            ? PlayerColor::Black
            : PlayerColor::White;

        m_moveHistory.push_back(record);
        return true;
    }

    // Execute move with legality check
    bool Board::MakeMove(const Move& move)
    {
        if (!IsMoveLegal(move))
        {
            return false;
        }

        return MakeMoveUnchecked(move);
    }

    // Undo last move and restore previous board state
    bool Board::UndoMove()
    {
        if (m_moveHistory.empty())
        {
            return false;
        }

        MoveRecord record = m_moveHistory.back();
        m_moveHistory.pop_back();

        // Restore side to move
        m_sideToMove = (m_sideToMove == PlayerColor::White) 
            ? PlayerColor::Black 
            : PlayerColor::White;

        // Restore pieces to original positions
        m_board[record.move.GetFrom()] = record.movedPiece;
        m_board[record.move.GetTo()] = record.capturedPiece;

		// Restore special move states
        if (record.move.IsEnPassant())
        {
            // Restore captured pawn to its original square
            int captureSquare = (m_sideToMove == PlayerColor::White)
                ? record.move.GetTo() - 8
                : record.move.GetTo() + 8;
            m_board[captureSquare] = Piece(PieceType::Pawn, 
                (m_sideToMove == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White);
        }
        else if (record.move.IsCastling())
        {
            // Restore rook to original position
            int rookFrom, rookTo;
            int row = (m_sideToMove == PlayerColor::White) ? 0 : 7;
            
            if (record.move.GetTo() % 8 == 6)  // Kingside castling
            {
                rookFrom = CoordinateToIndex(5, row);  // Rook currently on f-file
                rookTo = CoordinateToIndex(7, row);    // Move back to h-file
            }
            else  // Queenside castling
            {
                rookFrom = CoordinateToIndex(3, row);  // Rook currently on d-file
                rookTo = CoordinateToIndex(0, row);    // Move back to a-file
            }
            
            m_board[rookTo] = m_board[rookFrom];
            m_board[rookFrom] = EMPTY_PIECE;
        }

        // Restore all board state variables
        m_enPassantSquare = record.previousEnPassant;
        m_castlingRights = record.previousCastlingRights;
        m_halfMoveClock = record.previousHalfMoveClock;
        m_kingSquares[0] = record.previousKingSquares[0];
        m_kingSquares[1] = record.previousKingSquares[1];
        m_zobristKey = record.previousZobristKey;

        // Decrement full move number if undoing black's move
        if (m_sideToMove == PlayerColor::Black)
        {
            m_fullMoveNumber--;
        }

        return true;
    }

    // Execute null move (pass turn to opponent without moving)
    bool Board::MakeNullMoveUnchecked()
    {
        NullMoveRecord record;
        record.previousEnPassant = m_enPassantSquare;
        record.previousZobristKey = m_zobristKey;

        // Remove old en passant from hash
        if (m_enPassantSquare >= 0)
        {
            int oldEpFile = m_enPassantSquare % 8;
            m_zobristKey ^= Zobrist::enPassantKeys[oldEpFile];
            m_enPassantSquare = -1;
        }

        // Toggle side to move in Zobrist hash
        m_zobristKey ^= Zobrist::sideToMoveKey;

        // Switch active player
        m_sideToMove = (m_sideToMove == PlayerColor::White)
            ? PlayerColor::Black
            : PlayerColor::White;

        m_nullMoveHistory.push_back(record);
        return true;
    }

    // Undo null move and restore previous board state
    bool Board::UndoNullMove()
    {
        if (m_nullMoveHistory.empty())
        {
            return false;
        }

        NullMoveRecord record = m_nullMoveHistory.back();
        m_nullMoveHistory.pop_back();

        // Restore side to move
        m_sideToMove = (m_sideToMove == PlayerColor::White)
            ? PlayerColor::Black
            : PlayerColor::White;

        // Restore en passant and Zobrist key
        m_enPassantSquare = record.previousEnPassant;
        m_zobristKey = record.previousZobristKey;

        return true;
    }

	// Count how many times current position has occurred (for threefold repetition)
	int Board::CountRepetitions() const
	{
		int count = 1;
		// Keep the current key constant to compare against history
		const uint64_t targetKey = m_zobristKey;

		// Search backwards through move history
		for (int i = static_cast<int>(m_moveHistory.size()) - 1; i >= 0; --i)
		{
			const auto& record = m_moveHistory[i];
			
			// Stop at irreversible moves (captures, pawn moves, promotions)
			if (record.move.IsCapture() || 
				record.move.IsPromotion() || 
				record.movedPiece.GetType() == PieceType::Pawn)
			{
				break;
			}

			// Check if this historical position matches the current position
			if (record.previousZobristKey == targetKey)
			{
				count++;
			}
		}
		
		return count;
	}

    // Check for insufficient material to checkmate
    bool Board::IsInsufficientMaterial() const
    {
        int whitePawns = 0, blackPawns = 0;
        int whiteKnights = 0, blackKnights = 0;
        int whiteBishops = 0, blackBishops = 0;
        int whiteRooks = 0, blackRooks = 0;
        int whiteQueens = 0, blackQueens = 0;
        int whiteBishopColor = -1, blackBishopColor = -1;

        for (int sq = 0; sq < 64; ++sq)
        {
            Piece piece = GetPieceAt(sq);
            if (piece.IsEmpty()) continue;

            PieceType type = piece.GetType();
            bool isWhite = piece.IsColor(PlayerColor::White);

            switch (type)
            {
            case PieceType::Pawn:
                if (isWhite) whitePawns++; else blackPawns++;
                break;
            case PieceType::Knight:
                if (isWhite) whiteKnights++; else blackKnights++;
                break;
            case PieceType::Bishop:
                if (isWhite) {
                    whiteBishops++;
                    whiteBishopColor = (sq + sq / 8) % 2;
                } else {
                    blackBishops++;
                    blackBishopColor = (sq + sq / 8) % 2;
                }
                break;
            case PieceType::Rook:
                if (isWhite) whiteRooks++; else blackRooks++;
                break;
            case PieceType::Queen:
                if (isWhite) whiteQueens++; else blackQueens++;
                break;
            default:
                break;
            }
        }

        // Any pawns, rooks, or queens = sufficient material
        if (whitePawns || blackPawns) return false;
        if (whiteRooks || blackRooks) return false;
        if (whiteQueens || blackQueens) return false;

        int whitePieces = whiteKnights + whiteBishops;
        int blackPieces = blackKnights + blackBishops;

        // K vs K
        if (whitePieces == 0 && blackPieces == 0) return true;

        // K+minor vs K
        if (whitePieces == 0 && blackPieces == 1) return true;
        if (whitePieces == 1 && blackPieces == 0) return true;

        // K+B vs K+B (same color bishops)
        if (whiteBishops == 1 && blackBishops == 1 &&
            whiteKnights == 0 && blackKnights == 0 &&
            whiteBishopColor == blackBishopColor)
        {
            return true;
        }

        return false;
    }

    // Determine current game state (playing, check, checkmate, stalemate, draw)
    GameState Board::GetGameState() const
    {
        auto moves = GenerateLegalMoves();
        
        // No legal moves available
        if (moves.empty())
        {
            if (IsInCheck(m_sideToMove))
            {
                return GameState::Checkmate;
            }
            else
            {
                return GameState::Stalemate;
            }
        }

        // Check for check
        if (IsInCheck(m_sideToMove))
        {
            return GameState::Check;
        }

        // Check for threefold repetition
        if (CountRepetitions() >= 3)
        {
            return GameState::Draw;
        }

        // Check for fifty-move rule
        if (m_halfMoveClock >= 100) // 100 half-moves = 50 full moves
        {
            return GameState::Draw;
        }

        // Check for insufficient material
        if (IsInsufficientMaterial())
        {
            return GameState::Draw;
        }

        return GameState::Playing;
    }

    // Check if specified color's king is under attack
    bool Board::IsInCheck(PlayerColor color) const
    {
        int kingSquare = m_kingSquares[static_cast<int>(color)];
        if (kingSquare == -1) return false;
        
        return IsSquareAttacked(kingSquare, 
            (color == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White);
    }

    // Check if current position is checkmate
    bool Board::IsCheckmate() const
    {
        return GetGameState() == GameState::Checkmate;
    }

    // Check if current position is stalemate
    bool Board::IsStalemate() const
    {
        return GetGameState() == GameState::Stalemate;
    }

    // Check if current position is a draw
    bool Board::IsDraw() const
    {
        return GetGameState() == GameState::Draw;
    }

    // Update stored king position after king move
    void Board::UpdateKingSquare(PlayerColor color, int newSquare)
    {
        m_kingSquares[static_cast<int>(color)] = newSquare;
    }

    // Update castling rights based on piece movement
    void Board::UpdateCastlingRights(int movedFrom, int movedTo, Piece movedPiece)
    {
        // King move removes all castling rights for that color
        if (movedPiece.GetType() == PieceType::King)
        {
            int colorIndex = static_cast<int>(movedPiece.GetColor()) * 2;
            m_castlingRights[colorIndex] = false;     // Kingside
            m_castlingRights[colorIndex + 1] = false; // Queenside
            return;
        }

        // Rook move removes castling right for that side
        if (movedPiece.GetType() == PieceType::Rook)
        {
            PlayerColor color = movedPiece.GetColor();

            if (color == PlayerColor::White)
            {
                if (movedFrom == 0)        // a1 rook
                    m_castlingRights[1] = false; // White queenside
                else if (movedFrom == 7)   // h1 rook
                    m_castlingRights[0] = false; // White kingside
            }
            else
            {
                if (movedFrom == 56)       // a8 rook
                    m_castlingRights[3] = false; // Black queenside
                else if (movedFrom == 63)  // h8 rook
                    m_castlingRights[2] = false; // Black kingside
            }
        }

        // Rook capture removes castling right for captured rook
        if (movedTo == 0)
            m_castlingRights[1] = false;  // White queenside
        else if (movedTo == 7)
            m_castlingRights[0] = false;  // White kingside
        else if (movedTo == 56)
            m_castlingRights[3] = false;  // Black queenside
        else if (movedTo == 63)
            m_castlingRights[2] = false;  // Black kingside
    }

    // Execute en passant capture (remove captured pawn)
    void Board::HandleEnPassant(const Move& move)
    {
        if (move.IsEnPassant())
        {
            // Calculate captured pawn position
            int capturedPawnSquare = move.GetTo() + ((m_sideToMove == PlayerColor::White) ? -8 : 8);
            m_board[capturedPawnSquare] = EMPTY_PIECE;
        }
    }

    // Execute castling (move rook to correct position)
    void Board::HandleCastling(const Move& move)
    {
        if (!move.IsCastling())
            return;

        int rookFrom, rookTo;
        int row = (m_sideToMove == PlayerColor::White) ? 0 : 7;

        if (move.GetTo() % 8 == 6) // Kingside castling
        {
            rookFrom = CoordinateToIndex(7, row); // h-file
            rookTo = CoordinateToIndex(5, row);   // f-file
        }
        else // Queenside castling
        {
            rookFrom = CoordinateToIndex(0, row); // a-file
            rookTo = CoordinateToIndex(3, row);   // d-file
        }

        // Move the rook
        m_board[rookTo] = m_board[rookFrom];
        m_board[rookFrom] = EMPTY_PIECE;
        m_board[rookTo].SetMoved(true);
    }

    // Check if making a move would leave own king in check (used for move legality)
    bool Board::WouldMoveCauseCheck(const Move& move) const
    {
        // Create a temporary board to test the move
        Board tempBoard = *this;
        
        // Temporarily make the move
        Piece movedPiece = tempBoard.m_board[move.GetFrom()];
        Piece capturedPiece = tempBoard.m_board[move.GetTo()];
        
        // Handle en passant capture
        if (move.IsEnPassant())
        {
            int capturedPawnSquare = move.GetTo() + ((m_sideToMove == PlayerColor::White) ? -8 : 8);
            tempBoard.m_board[capturedPawnSquare] = EMPTY_PIECE;
        }
        
        // Move the piece
        tempBoard.m_board[move.GetTo()] = movedPiece;
        tempBoard.m_board[move.GetFrom()] = EMPTY_PIECE;
        
        // Check if the moving side's king is in check
        return tempBoard.IsInCheck(m_sideToMove);
    }

    // Check if a square is attacked by specified color
    bool Board::IsSquareAttacked(int square, PlayerColor attackerColor) const
    {
        return MoveGenerator::IsSquareAttacked(m_board, square, attackerColor);
    }

    // Generate human-readable board representation
    std::string Board::ToString() const
    {
        std::stringstream ss;
        
        // Print board from rank 8 to rank 1
        for (int rank = BOARD_SIZE - 1; rank >= 0; --rank)
        {
            ss << (rank + 1) << " ";
            for (int file = 0; file < BOARD_SIZE; ++file)
            {
                Piece piece = GetPieceAt(file, rank);
                ss << " " << piece.GetSymbol();
            }
            ss << "\n";
        }
        ss << "   a b c d e f g h\n";
        
        // Print game state information
        ss << "\nSide to move: " << (m_sideToMove == PlayerColor::White ? "White" : "Black") << "\n";
        
        if (m_enPassantSquare >= 0)
        {
            auto [file, rank] = IndexToCoordinate(m_enPassantSquare);
            ss << "En passant: " << FILE_NAMES[file] << (rank + 1) << "\n";
        }
        else
        {
            ss << "En passant: -\n";
        }
        
        ss << "Castling: " 
            << (m_castlingRights[0] ? "K" : "") 
            << (m_castlingRights[1] ? "Q" : "") 
            << (m_castlingRights[2] ? "k" : "") 
            << (m_castlingRights[3] ? "q" : "") 
            << "\n";
        ss << "Half-move: " << m_halfMoveClock << ", Full-move: " << m_fullMoveNumber << "\n";
        
        return ss.str();
    }

    // Calculate material balance (positive = white advantage)
    int Board::EvaluateMaterial() const
    {
        int score = 0;
        
        for (int i = 0; i < SQUARE_COUNT; ++i)
        {
            Piece piece = m_board[i];
            if (!piece) continue;
            
            int value = PIECE_VALUES[static_cast<int>(piece.GetType())];
            if (piece.GetColor() == PlayerColor::White)
                score += value;
            else
                score -= value;
        }
        
        return score;
    }

    // ---------- FENParser Implementation ----------
    
    // Parse FEN string and load into board
    bool FENParser::Parse(const std::string& fen, Board& board)
    {
        std::istringstream fenStream(fen);
        std::string segment;
        
        // 1. Piece placement
        if (!std::getline(fenStream, segment, ' '))
            return false;
        
        if (!ParsePiecePlacement(segment, board.m_board))
            return false;
        
        // 2. Active color
        if (!std::getline(fenStream, segment, ' '))
            return false;
        
        if (!ParseActiveColor(segment, board.m_sideToMove))
            return false;
        
        // 3. Castling rights
        if (!std::getline(fenStream, segment, ' '))
            return false;
        
        if (!ParseCastlingRights(segment, board.m_castlingRights))
            return false;
        
        // 4. En passant
        if (!std::getline(fenStream, segment, ' '))
            return false;
        
        if (!ParseEnPassant(segment, board.m_enPassantSquare))
            return false;
        
        // 5. Half-move clock
        if (!std::getline(fenStream, segment, ' '))
            return false;
        
        // 6. Full-move number
        std::string fullMoveStr;
        if (!std::getline(fenStream, fullMoveStr, ' '))
            return false;
        
        if (!ParseMoveClocks(segment, fullMoveStr, 
            board.m_halfMoveClock, board.m_fullMoveNumber))
            return false;
        
        // Find king positions on the board
        board.m_kingSquares[0] = -1;
        board.m_kingSquares[1] = -1;
        
        for (int i = 0; i < SQUARE_COUNT; ++i)
        {
            Piece piece = board.m_board[i];
            if (piece.GetType() == PieceType::King)
            {
                board.m_kingSquares[static_cast<int>(piece.GetColor())] = i;
            }
        }
        
        // Clear move history for new position
        board.m_moveHistory.clear();
        
        return true;
    }

    // Generate FEN string from current board state
    std::string FENParser::Generate(const Board& board)
    {
        std::stringstream fen;
        
        // 1. Piece placement (from rank 8 to rank 1)
        for (int rank = BOARD_SIZE - 1; rank >= 0; --rank)
        {
            int emptyCount = 0;
            
            for (int file = 0; file < BOARD_SIZE; ++file)
            {
                Piece piece = board.GetPieceAt(file, rank);
                
                if (piece.IsEmpty())
                {
                    emptyCount++;
                }
                else
                {
                    // Output empty square count if any
                    if (emptyCount > 0)
                    {
                        fen << emptyCount;
                        emptyCount = 0;
                    }
                    
                    // Output piece character
                    char symbol = piece.GetSymbol()[0];
                    if (piece.GetColor() == PlayerColor::Black)
                    {
                        symbol = static_cast<char>(std::tolower(symbol));
                    }
                    fen << symbol;
                }
            }
            
            // Output remaining empty squares
            if (emptyCount > 0)
            {
                fen << emptyCount;
            }
            
            // Rank separator
            if (rank > 0)
            {
                fen << '/';
            }
        }
        
        // 2. Active color
        fen << (board.GetSideToMove() == PlayerColor::White ? " w " : " b ");
        
        // 3. Castling rights
        bool anyCastling = false;
        if (board.m_castlingRights[0]) { fen << 'K'; anyCastling = true; }
        if (board.m_castlingRights[1]) { fen << 'Q'; anyCastling = true; }
        if (board.m_castlingRights[2]) { fen << 'k'; anyCastling = true; }
        if (board.m_castlingRights[3]) { fen << 'q'; anyCastling = true; }
        
        if (!anyCastling)
        {
            fen << '-';
        }
        
        // 4. En passant square
        fen << ' ';
        if (board.GetEnPassantSquare() >= 0)
        {
            auto [file, rank] = IndexToCoordinate(board.GetEnPassantSquare());
            fen << FILE_NAMES[file] << (rank + 1);
        }
        else
        {
            fen << '-';
        }
        
        // 5. Half-move clock
        fen << ' ' << board.GetHalfMoveClock();
        
        // 6. Full-move number
        fen << ' ' << board.GetFullMoveNumber();
        
        return fen.str();
    }

    // Parse piece placement section of FEN string
    bool FENParser::ParsePiecePlacement(const std::string& placement, 
                                        std::array<Piece, SQUARE_COUNT>& board)
    {
        int square = 56; // Start from a8 (square 56)
        board.fill(EMPTY_PIECE);
        
        for (char c : placement)
        {
            if (c == '/')
            {
                square -= 16; // Move to next rank down
            }
            else if (std::isdigit(static_cast<unsigned char>(c)))
            {
                square += static_cast<int>(c - '0'); // Skip empty squares
            }
            else
            {
                // Parse piece character
                PieceType type = PieceType::None;
                PlayerColor color = std::isupper(static_cast<unsigned char>(c)) 
                    ? PlayerColor::White 
                    : PlayerColor::Black;
                
                char lowerC = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c))
                );
                
                switch (lowerC)
                {
                    case 'p': type = PieceType::Pawn; break;
                    case 'n': type = PieceType::Knight; break;
                    case 'b': type = PieceType::Bishop; break;
                    case 'r': type = PieceType::Rook; break;
                    case 'q': type = PieceType::Queen; break;
                    case 'k': type = PieceType::King; break;
                    default: return false;
                }
                
                if (square < 0 || square >= SQUARE_COUNT)
                    return false;
                    
                board[square] = Piece(type, color);
                square++;
            }
        }
        
        // Validate that all ranks were processed
        if (square != 8)
            return false;
        
        return true;
    }

    // Parse active color from FEN string
    bool FENParser::ParseActiveColor(const std::string& colorStr, PlayerColor& color)
    {
        if (colorStr == "w")
        {
            color = PlayerColor::White;
            return true;
        }
        else if (colorStr == "b")
        {
            color = PlayerColor::Black;
            return true;
        }
        return false;
    }

    // Parse castling rights from FEN string
    bool FENParser::ParseCastlingRights(const std::string& castlingStr, 
                                        std::array<bool, 4>& rights)
    {
        rights = {false, false, false, false};
        
        if (castlingStr == "-")
            return true;
            
        for (char c : castlingStr)
        {
            switch (c)
            {
                case 'K': rights[0] = true; break; // White kingside
                case 'Q': rights[1] = true; break; // White queenside
                case 'k': rights[2] = true; break; // Black kingside
                case 'q': rights[3] = true; break; // Black queenside
                default: return false;
            }
        }
        
        return true;
    }

    // Parse en passant square from FEN string
    bool FENParser::ParseEnPassant(const std::string& epStr, int& enPassantSquare)
    {
        if (epStr == "-")
        {
            enPassantSquare = -1;
            return true;
        }
        
        if (epStr.length() != 2)
            return false;
            
        char fileChar = epStr[0];
        char rankChar = epStr[1];
        
        if (fileChar < 'a' || fileChar > 'h' || rankChar < '1' || rankChar > '8')
            return false;
            
        int file = static_cast<int>(fileChar - 'a');
        int rank = static_cast<int>(rankChar - '1');
        
        enPassantSquare = CoordinateToIndex(file, rank);
        return true;
    }

    // Parse move clocks from FEN string
    bool FENParser::ParseMoveClocks(const std::string& halfMoveStr, 
                                    const std::string& fullMoveStr,
                                    int& halfMoveClock, int& fullMoveNumber)
    {
        try
        {
            halfMoveClock = std::stoi(halfMoveStr);
            fullMoveNumber = std::stoi(fullMoveStr);
            
            if (halfMoveClock < 0 || fullMoveNumber < 1)
                return false;
                
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
}