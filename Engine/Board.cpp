// Board.cpp
// Chess board representation and game state management
//
// This module implements the core chess board logic including:
// - Board state representation (64-square array + metadata)
// - Move execution (make/undo with full state restoration)
// - FEN parsing and generation (Forsyth-Edwards Notation)
// - Legal move generation and validation
// - Game state detection (checkmate, stalemate, draw)
// - Incremental evaluation maintenance (material + PST)
// - Zobrist hashing for transposition detection
//
// Performance optimizations:
// - Piece lists for fast iteration over pieces
// - Incremental Zobrist hash updates (XOR operations)
// - Incremental score updates (add/subtract deltas)
// - Move history stack for undo without copying board
//
// The Board class maintains full game state needed for UCI protocol compliance
// and efficient search tree traversal in the chess engine.

#include "Board.h"
#include "Zobrist.h"
#include "Evaluation.h"
#include <algorithm>
#include <sstream>
#include <cctype>
#include <stdexcept>
#include <cassert>

namespace Chess
{
    // Default constructor - initialize board to starting position
    // Also ensures Zobrist hash keys are initialized globally
    Board::Board()
    {
        Zobrist::Initialize();
        ResetToStartingPosition();
    }

    // Constructor from FEN string
    // If FEN parsing fails, falls back to starting position
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

    // Load position from FEN string
    // FEN (Forsyth-Edwards Notation) is standard format for chess positions
    // Example: "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"
    //
    // After loading, recomputes derived state (Zobrist key, incremental score)
    bool Board::LoadFEN(const std::string& fen)
    {
        bool result = FENParser::Parse(fen, *this);
        if (result)
        {
            RecomputeZobristKey();
            RecomputeIncrementalScore();
        }
        return result;
    }

    // Generate FEN string from current position
    // Useful for position export, debugging, and UCI protocol
    std::string Board::GetFEN() const
    {
        return FENParser::Generate(*this);
    }

    // Recompute Zobrist hash from scratch
    // Called after FEN loading or when hash may be corrupted
    //
    // Zobrist hashing: XOR together random numbers for each piece/square
    // This creates a unique 64-bit fingerprint of the position
    // Hash is incrementally updated during moves (much faster than recomputing)
    void Board::RecomputeZobristKey()
    {
        m_zobristKey = 0;

        // XOR in piece positions
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

        // XOR in side to move
        if (m_sideToMove == PlayerColor::Black)
        {
            m_zobristKey ^= Zobrist::sideToMoveKey;
        }

        // XOR in castling rights
        for (int i = 0; i < 4; ++i)
        {
            if (m_castlingRights[i])
            {
                m_zobristKey ^= Zobrist::castlingKeys[i];
            }
        }

        // XOR in en passant file (if applicable)
        if (m_enPassantSquare >= 0)
        {
            int epFile = m_enPassantSquare % 8;
            m_zobristKey ^= Zobrist::enPassantKeys[epFile];
        }
    }

    // Update incremental score when piece is added/removed
    // Incremental score = material + PST values
    //
    // This is much faster than recomputing full evaluation every time
    // Only material and PST are maintained incrementally; other eval terms
    // (mobility, king safety, etc.) are computed on-demand during search
    void Board::UpdateIncrementalScore(int square, Piece piece, bool add)
    {
        if (piece.IsEmpty()) return;

        int value = GetPieceValue(piece.GetType());
        int pstValue = GetPSTValue(piece.GetType(), square, piece.GetColor());

        int score = value + pstValue;

        // Negate score for Black pieces (evaluation is from White's perspective)
        if (piece.GetColor() == PlayerColor::Black)
        {
            score = -score;
        }

        // Add or subtract score delta
        if (add)
        {
            m_incrementalScore += score;
        }
        else
        {
            m_incrementalScore -= score;
        }
    }

    // Recompute incremental score from scratch
    // Called after FEN loading or when score may be corrupted
    void Board::RecomputeIncrementalScore()
    {
        m_incrementalScore = 0;
        for (int sq = 0; sq < SQUARE_COUNT; ++sq)
        {
            Piece piece = m_board[sq];
            if (!piece.IsEmpty())
            {
                UpdateIncrementalScore(sq, piece, true);
            }
        }
    }

    // Generate all legal moves in current position
    // Legal moves = pseudo-legal moves that don't leave king in check
    //
    // Process:
    // 1. Generate all pseudo-legal moves (follow piece movement rules)
    // 2. For each move, make it temporarily
    // 3. Check if our king is in check after the move
    // 4. If not in check, move is legal
    // 5. Undo the temporary move
    //
    // This is the definitive legal move list used for game rules enforcement
    std::vector<Move> Board::GenerateLegalMoves() const
    {
        // Create temporary board for move validation
        Board temp = *this;

        // Generate pseudo-legal moves using move generator
        std::vector<Move> moves = MoveGenerator::GeneratePseudoLegalMoves(
            temp.m_board,
            temp.m_sideToMove,
            temp.m_enPassantSquare,
            &temp.m_castlingRights,
            &temp.m_pieceLists[static_cast<int>(temp.m_sideToMove)]
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

            // Move is legal if king is not in check after making it
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
    // Used for validating user input and UCI move strings
    //
    // This generates all legal moves and searches for matching move
    // Could be optimized by generating only moves from source square
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

    // Make a move without legality checking (unchecked version)
    // Used internally during search where move legality is known
    //
    // This is the core move execution function - handles all special cases:
    // - Captures (remove captured piece)
    // - Castling (move rook)
    // - En passant (remove captured pawn)
    // - Promotion (replace pawn with promoted piece)
    //
    // All state changes are recorded in MoveRecord for perfect undo
    // Zobrist hash and incremental score are updated incrementally
    bool Board::MakeMoveUnchecked(const Move& move)
    {
        // Create move record for undo functionality
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
        record.previousIncrementalScore = m_incrementalScore;

        const int from = move.GetFrom();
        const int to = move.GetTo();
        Piece movedPiece = m_board[from];

        // Remove moved piece from source square (Zobrist)
        m_zobristKey ^= Zobrist::pieceKeys[static_cast<int>(movedPiece.GetType())][static_cast<int>(movedPiece.GetColor())][from];

        // Remove moved piece from source square (incremental score)
        UpdateIncrementalScore(from, movedPiece, false);

        // Handle capture - remove captured piece
        if (!m_board[to].IsEmpty())
        {
            Piece capturedPiece = m_board[to];
            m_zobristKey ^= Zobrist::pieceKeys[static_cast<int>(capturedPiece.GetType())][static_cast<int>(capturedPiece.GetColor())][to];
            UpdateIncrementalScore(to, capturedPiece, false);
            m_pieceLists[static_cast<int>(capturedPiece.GetColor())].Remove(to);
        }

        // Update Zobrist hash for en passant change
        if (m_enPassantSquare >= 0)
        {
            int oldEpFile = m_enPassantSquare % 8;
            m_zobristKey ^= Zobrist::enPassantKeys[oldEpFile];
        }

        // Update Zobrist hash for castling rights change
        // Must XOR out old rights before updating
        for (int i = 0; i < 4; ++i)
        {
            if (m_castlingRights[i])
            {
                m_zobristKey ^= Zobrist::castlingKeys[i];
            }
        }

        // Update castling rights based on moved piece/squares
        UpdateCastlingRights(move.GetFrom(), move.GetTo(), m_board[move.GetFrom()]);

        // XOR in new castling rights
        for (int i = 0; i < 4; ++i)
        {
            if (m_castlingRights[i])
            {
                m_zobristKey ^= Zobrist::castlingKeys[i];
            }
        }

        // Handle castling - need to move rook as well
        int castlingRookFrom = -1;
        int castlingRookTo = -1;
        Piece castlingRook = EMPTY_PIECE;

        if (move.IsCastling())
        {
            int row = (m_sideToMove == PlayerColor::White) ? 0 : 7;

            // Kingside castling (O-O)
            if (move.GetTo() % 8 == 6)
            {
                castlingRookFrom = CoordinateToIndex(7, row);
                castlingRookTo = CoordinateToIndex(5, row);
            }
            // Queenside castling (O-O-O)
            else
            {
                castlingRookFrom = CoordinateToIndex(0, row);
                castlingRookTo = CoordinateToIndex(3, row);
            }

            castlingRook = m_board[castlingRookFrom];

            // Remove rook from old position (Zobrist)
            m_zobristKey ^= Zobrist::pieceKeys[static_cast<int>(castlingRook.GetType())][static_cast<int>(castlingRook.GetColor())][castlingRookFrom];
        }

        // Execute special moves (en passant, castling)
        HandleEnPassant(move);
        HandleCastling(move);

        // Move piece to destination square
        m_board[move.GetTo()] = m_board[move.GetFrom()];
        m_board[move.GetFrom()] = EMPTY_PIECE;
        m_board[move.GetTo()].SetMoved(true);

        // Handle promotion - replace pawn with promoted piece
        if (move.IsPromotion())
        {
            m_board[move.GetTo()] = Piece(move.GetPromotion(), m_sideToMove, true);
        }

        // Add piece to destination square (Zobrist)
        Piece finalPiece = m_board[to];
        m_zobristKey ^= Zobrist::pieceKeys[static_cast<int>(finalPiece.GetType())][static_cast<int>(finalPiece.GetColor())][to];

        // Add piece to destination square (incremental score)
        UpdateIncrementalScore(to, finalPiece, true);

        // Update piece list
        m_pieceLists[static_cast<int>(m_sideToMove)].Update(from, to);

        // Update king square tracking if king moved
        if (m_board[move.GetTo()].GetType() == PieceType::King)
        {
            UpdateKingSquare(m_sideToMove, move.GetTo());
        }

        // Complete castling rook move
        if (move.IsCastling())
        {
            m_zobristKey ^= Zobrist::pieceKeys[static_cast<int>(castlingRook.GetType())][static_cast<int>(castlingRook.GetColor())][castlingRookTo];
            UpdateIncrementalScore(castlingRookFrom, castlingRook, false);
            UpdateIncrementalScore(castlingRookTo, castlingRook, true);
            m_pieceLists[static_cast<int>(m_sideToMove)].Update(castlingRookFrom, castlingRookTo);
        }

        // Complete en passant capture (remove captured pawn)
        if (move.IsEnPassant())
        {
            int capturedPawnSquare = to + ((m_sideToMove == PlayerColor::White) ? -8 : 8);
            Piece capturedPawn = Piece(PieceType::Pawn, (m_sideToMove == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White);
            m_zobristKey ^= Zobrist::pieceKeys[static_cast<int>(capturedPawn.GetType())][static_cast<int>(capturedPawn.GetColor())][capturedPawnSquare];
            UpdateIncrementalScore(capturedPawnSquare, capturedPawn, false);
            m_pieceLists[static_cast<int>(capturedPawn.GetColor())].Remove(capturedPawnSquare);
        }

        // Set new en passant square if pawn moved two squares
        m_enPassantSquare = -1;
        if (m_board[move.GetTo()].GetType() == PieceType::Pawn)
        {
            int delta = move.GetTo() - move.GetFrom();
            if (std::abs(delta) == 16) // Two-square pawn move
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
            m_halfMoveClock = 0; // Reset on capture or pawn move
        }

        // Update move counters
        if (m_sideToMove == PlayerColor::Black)
        {
            m_fullMoveNumber++;
        }

        // Update side to move (Zobrist)
        m_zobristKey ^= Zobrist::sideToMoveKey;

        // Switch sides
        m_sideToMove = (m_sideToMove == PlayerColor::White)
            ? PlayerColor::Black
            : PlayerColor::White;

        // Save move record for undo
        m_moveHistory[m_historyPly++] = record;
        return true;
    }
	
	// Make a move with legality checking
    // This is the public interface for making moves (used by UI/game logic)
    // Returns false if move is illegal
    //
    // Internally calls IsMoveLegal() then MakeMoveUnchecked()
    // The unchecked version is faster but requires caller to ensure legality
    bool Board::MakeMove(const Move& move)
    {
        if (!IsMoveLegal(move))
        {
            return false;
        }

        return MakeMoveUnchecked(move);
    }

    // Undo the last move made
    // Restores complete board state from move history record
    //
    // This is perfect undo - all state is restored exactly:
    // - Piece positions
    // - Captured pieces
    // - Castling rights
    // - En passant square
    // - King positions
    // - Zobrist hash
    // - Incremental score
    // - Move counters
    //
    // Used for:
    // - Search tree traversal (make/undo millions of moves)
    // - User undo functionality
    // - Move validation
    bool Board::UndoMove()
    {
        if (m_historyPly == 0)
        {
            return false; // No moves to undo
        }

        MoveRecord record = m_moveHistory[--m_historyPly];

        // Switch side back
        m_sideToMove = (m_sideToMove == PlayerColor::White)
            ? PlayerColor::Black
            : PlayerColor::White;

        const int from = record.move.GetFrom();
        const int to = record.move.GetTo();

        // Update piece list (reverse the move)
        m_pieceLists[static_cast<int>(m_sideToMove)].Update(to, from);

        // Restore captured piece to piece list
        if (!record.capturedPiece.IsEmpty())
        {
            m_pieceLists[static_cast<int>(record.capturedPiece.GetColor())].Add(to);
        }

        // Restore piece positions
        m_board[record.move.GetFrom()] = record.movedPiece;
        m_board[record.move.GetTo()] = record.capturedPiece;

        // Handle en passant undo - restore captured pawn
        if (record.move.IsEnPassant())
        {
            int captureSquare = (m_sideToMove == PlayerColor::White)
                ? record.move.GetTo() - 8
                : record.move.GetTo() + 8;
            Piece capturedPawn = Piece(PieceType::Pawn,
                (m_sideToMove == PlayerColor::White) ? PlayerColor::Black : PlayerColor::White);
            m_board[captureSquare] = capturedPawn;
            m_pieceLists[static_cast<int>(capturedPawn.GetColor())].Add(captureSquare);
        }
        // Handle castling undo - move rook back
        else if (record.move.IsCastling())
        {
            int rookFrom, rookTo;
            int row = (m_sideToMove == PlayerColor::White) ? 0 : 7;

            // Determine rook squares based on castling type
            if (record.move.GetTo() % 8 == 6) // Kingside
            {
                rookFrom = CoordinateToIndex(5, row);
                rookTo = CoordinateToIndex(7, row);
            }
            else // Queenside
            {
                rookFrom = CoordinateToIndex(3, row);
                rookTo = CoordinateToIndex(0, row);
            }

            // Move rook back to original square
            m_board[rookTo] = m_board[rookFrom];
            m_board[rookFrom] = EMPTY_PIECE;
            m_pieceLists[static_cast<int>(m_sideToMove)].Update(rookFrom, rookTo);
        }

        // Restore all game state from move record
        m_enPassantSquare = record.previousEnPassant;
        m_castlingRights = record.previousCastlingRights;
        m_halfMoveClock = record.previousHalfMoveClock;
        m_kingSquares[0] = record.previousKingSquares[0];
        m_kingSquares[1] = record.previousKingSquares[1];
        m_zobristKey = record.previousZobristKey;
        m_incrementalScore = record.previousIncrementalScore;

        // Update full move counter
        if (m_sideToMove == PlayerColor::Black)
        {
            m_fullMoveNumber--;
        }

        return true;
    }

    // Make a null move (pass turn without moving)
    // Used in null move pruning search technique
    //
    // Null move pruning: if position is so good that even passing the turn
    // still results in beta cutoff, we can skip searching this branch
    //
    // Null move only changes:
    // - Side to move
    // - En passant square (cleared)
    // - Zobrist hash
    //
    // Piece positions and other state unchanged
    bool Board::MakeNullMoveUnchecked()
    {
        NullMoveRecord record;
        record.previousEnPassant = m_enPassantSquare;
        record.previousZobristKey = m_zobristKey;

        // Clear en passant (passing turn forfeits en passant right)
        if (m_enPassantSquare >= 0)
        {
            int oldEpFile = m_enPassantSquare % 8;
            m_zobristKey ^= Zobrist::enPassantKeys[oldEpFile];
            m_enPassantSquare = -1;
        }

        // Toggle side to move
        m_zobristKey ^= Zobrist::sideToMoveKey;

        m_sideToMove = (m_sideToMove == PlayerColor::White)
            ? PlayerColor::Black
            : PlayerColor::White;

        // Save null move record for undo
        m_nullMoveHistory[m_nullMovePly++] = record;
        return true;
    }

    // Undo null move
    // Restores state before null move was made
    bool Board::UndoNullMove()
    {
        if (m_nullMovePly == 0)
        {
            return false; // No null moves to undo
        }

        NullMoveRecord record = m_nullMoveHistory[--m_nullMovePly];

        // Restore side to move
        m_sideToMove = (m_sideToMove == PlayerColor::White)
            ? PlayerColor::Black
            : PlayerColor::White;

        // Restore en passant and Zobrist hash
        m_enPassantSquare = record.previousEnPassant;
        m_zobristKey = record.previousZobristKey;

        return true;
    }

    // Count how many times current position has occurred in game history
    // Used for threefold repetition draw detection
    //
    // Searches backward through move history comparing Zobrist keys
    // Stops at irreversible moves (capture, pawn move, promotion) since
    // these prevent position from recurring
    //
    // Returns: number of times this position occurred (including current)
    int Board::CountRepetitions() const
    {
        int count = 1; // Current position counts as first occurrence
        const uint64_t targetKey = m_zobristKey;

        // Search backward through move history
        for (int i = m_historyPly - 1; i >= 0; --i)
        {
            const auto& record = m_moveHistory[i];

            // Stop at irreversible moves (these create a new "game tree")
            if (record.move.IsCapture() ||
                record.move.IsPromotion() ||
                record.movedPiece.GetType() == PieceType::Pawn)
            {
                break; // Can't repeat position before irreversible move
            }

            // Check if position before this move matches current position
            if (record.previousZobristKey == targetKey)
            {
                count++;
            }
        }

        return count;
    }

    // Check if position has insufficient material for checkmate
    // Used for automatic draw detection
    //
    // Insufficient material cases (cannot force checkmate):
    // - K vs K (bare kings)
    // - K+N vs K (king and knight vs king)
    // - K+B vs K (king and bishop vs king)
    // - K+B vs K+B (bishops on same color)
    //
    // Even with best play, these positions cannot result in checkmate
    bool Board::IsInsufficientMaterial() const
    {
        // Count all pieces
        int whitePawns = 0, blackPawns = 0;
        int whiteKnights = 0, blackKnights = 0;
        int whiteBishops = 0, blackBishops = 0;
        int whiteRooks = 0, blackRooks = 0;
        int whiteQueens = 0, blackQueens = 0;
        int whiteBishopColor = -1, blackBishopColor = -1; // Square color of bishop

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
                    whiteBishopColor = (sq + sq / 8) % 2; // Light or dark square
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

        // Any pawns, rooks, or queens means checkmate is possible
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
        // Bishops on same color cannot force checkmate
        if (whiteBishops == 1 && blackBishops == 1 &&
            whiteKnights == 0 && blackKnights == 0 &&
            whiteBishopColor == blackBishopColor)
        {
            return true;
        }

        return false;
    }

    // Get current game state (playing, check, checkmate, stalemate, draw)
    // This is the primary function for determining game status
    //
    // Decision tree:
    // 1. Generate legal moves
    // 2. If no legal moves:
    //    - In check → Checkmate
    //    - Not in check → Stalemate
    // 3. If legal moves exist:
    //    - In check → Check (game continues)
    //    - Three repetitions → Draw
    //    - Fifty moves without progress → Draw
    //    - Insufficient material → Draw
    //    - Otherwise → Playing
    GameState Board::GetGameState() const
    {
        auto moves = GenerateLegalMoves();

        // No legal moves - either checkmate or stalemate
        if (moves.empty())
        {
            if (IsInCheck(m_sideToMove))
            {
                return GameState::Checkmate; // In check and can't move
            }
            else
            {
                return GameState::Stalemate; // Not in check but can't move
            }
        }

        // Legal moves exist - check other conditions
        if (IsInCheck(m_sideToMove))
        {
            return GameState::Check; // In check but has legal moves (game continues)
        }

        // Check draw conditions
        if (CountRepetitions() >= 3)
        {
            return GameState::Draw; // Threefold repetition
        }

        if (m_halfMoveClock >= 100)
        {
            return GameState::Draw; // Fifty-move rule (100 half-moves = 50 full moves)
        }

        if (IsInsufficientMaterial())
        {
            return GameState::Draw; // Cannot force checkmate
        }

        return GameState::Playing; // Game continues normally
    }

    // Check if specified side is in check
    // King is in check if opponent pieces attack its square
    bool Board::IsInCheck(PlayerColor color) const
    {
        int kingSquare = m_kingSquares[static_cast<int>(color)];
        if (kingSquare == -1) return false; // No king (shouldn't happen)

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

    // Check if game is drawn
    bool Board::IsDraw() const
    {
        return GetGameState() == GameState::Draw;
    }

    // Update king square tracking
    // Called when king moves to maintain fast king position lookup
    void Board::UpdateKingSquare(PlayerColor color, int newSquare)
    {
        m_kingSquares[static_cast<int>(color)] = newSquare;
    }

    // Update castling rights based on move
    // Castling rights are lost when:
    // - King moves (loses both sides)
    // - Rook moves from starting square (loses that side)
    // - Rook is captured on starting square (loses that side)
    //
    // Castling rights array: [White KS, White QS, Black KS, Black QS]
    void Board::UpdateCastlingRights(int movedFrom, int movedTo, Piece movedPiece)
    {
        // King move - lose all castling rights for that color
        if (movedPiece.GetType() == PieceType::King)
        {
            int colorIndex = static_cast<int>(movedPiece.GetColor()) * 2;
            m_castlingRights[colorIndex] = false;     // Kingside
            m_castlingRights[colorIndex + 1] = false; // Queenside
            return;
        }

        // Rook move - lose castling right for that side
        if (movedPiece.GetType() == PieceType::Rook)
        {
            PlayerColor color = movedPiece.GetColor();

            if (color == PlayerColor::White)
            {
                if (movedFrom == 0)      // a1 rook moved
                    m_castlingRights[1] = false; // Lose queenside
                else if (movedFrom == 7) // h1 rook moved
                    m_castlingRights[0] = false; // Lose kingside
            }
            else // Black
            {
                if (movedFrom == 56)     // a8 rook moved
                    m_castlingRights[3] = false; // Lose queenside
                else if (movedFrom == 63) // h8 rook moved
                    m_castlingRights[2] = false; // Lose kingside
            }
        }

        // Rook captured - lose castling right for that side
        // Check if destination square is a rook starting position
        if (movedTo == 0)       // a1 captured
            m_castlingRights[1] = false;
        else if (movedTo == 7)  // h1 captured
            m_castlingRights[0] = false;
        else if (movedTo == 56) // a8 captured
            m_castlingRights[3] = false;
        else if (movedTo == 63) // h8 captured
            m_castlingRights[2] = false;
    }

    // Execute en passant capture
    // Remove the captured pawn (which is not on the destination square)
    void Board::HandleEnPassant(const Move& move)
    {
        if (move.IsEnPassant())
        {
            // Calculate captured pawn position (behind destination square)
            int capturedPawnSquare = move.GetTo() + ((m_sideToMove == PlayerColor::White) ? -8 : 8);
            m_board[capturedPawnSquare] = EMPTY_PIECE;
        }
    }

    // Execute castling rook move
    // King is moved by normal move execution, this handles the rook
    void Board::HandleCastling(const Move& move)
    {
        if (!move.IsCastling())
            return;

        int rookFrom, rookTo;
        int row = (m_sideToMove == PlayerColor::White) ? 0 : 7;

        // Determine rook squares based on castling type
        if (move.GetTo() % 8 == 6) // Kingside castling (king to g-file)
        {
            rookFrom = CoordinateToIndex(7, row); // Rook from h-file
            rookTo = CoordinateToIndex(5, row);   // Rook to f-file
        }
        else // Queenside castling (king to c-file)
        {
            rookFrom = CoordinateToIndex(0, row); // Rook from a-file
            rookTo = CoordinateToIndex(3, row);   // Rook to d-file
        }

        // Move the rook
        m_board[rookTo] = m_board[rookFrom];
        m_board[rookFrom] = EMPTY_PIECE;
        m_board[rookTo].SetMoved(true);
    }
	
	// Check if a move would leave king in check (used for move validation)
    // Creates temporary board, makes move, checks if king is in check
    //
    // This is a helper function - not typically used in hot paths
    // Legal move generation uses similar logic but optimized for batch processing
    bool Board::WouldMoveCauseCheck(const Move& move) const
    {
        Board tempBoard = *this;

        Piece movedPiece = tempBoard.m_board[move.GetFrom()];
        Piece capturedPiece = tempBoard.m_board[move.GetTo()];

        // Handle en passant - remove captured pawn
        if (move.IsEnPassant())
        {
            int capturedPawnSquare = move.GetTo() + ((m_sideToMove == PlayerColor::White) ? -8 : 8);
            tempBoard.m_board[capturedPawnSquare] = EMPTY_PIECE;
        }

        // Execute move on temporary board
        tempBoard.m_board[move.GetTo()] = movedPiece;
        tempBoard.m_board[move.GetFrom()] = EMPTY_PIECE;

        // Check if king is in check after move
        return tempBoard.IsInCheck(m_sideToMove);
    }

    // Check if square is attacked by given color
    // Wrapper around MoveGenerator for convenience
    bool Board::IsSquareAttacked(int square, PlayerColor attackerColor) const
    {
        return MoveGenerator::IsSquareAttacked(m_board, square, attackerColor);
    }

    // Generate human-readable string representation of board
    // Displays board from White's perspective (rank 8 at top)
    // Also shows game metadata (side to move, castling, en passant, etc.)
    //
    // Example output:
    // 8  r n b q k b n r
    // 7  p p p p p p p p
    // 6  . . . . . . . .
    // ...
    // 1  R N B Q K B N R
    //    a b c d e f g h
    //
    // Side to move: White
    // En passant: -
    // Castling: KQkq
    // Half-move: 0, Full-move: 1
    std::string Board::ToString() const
    {
        std::stringstream ss;

        // Draw board from rank 8 to rank 1 (White's perspective)
        for (int rank = BOARD_SIZE - 1; rank >= 0; --rank)
        {
            ss << (rank + 1) << " "; // Rank number
            for (int file = 0; file < BOARD_SIZE; ++file)
            {
                Piece piece = GetPieceAt(file, rank);
                ss << " " << piece.GetSymbol(); // Piece symbol (K, Q, R, etc. or '.')
            }
            ss << "\n";
        }
        
        // File letters
        ss << "   a b c d e f g h\n";

        // Game metadata
        ss << "\nSide to move: " << (m_sideToMove == PlayerColor::White ? "White" : "Black") << "\n";

        // En passant target square (if any)
        if (m_enPassantSquare >= 0)
        {
            auto [file, rank] = IndexToCoordinate(m_enPassantSquare);
            ss << "En passant: " << FILE_NAMES[file] << (rank + 1) << "\n";
        }
        else
        {
            ss << "En passant: -\n";
        }

        // Castling rights (KQkq format)
        ss << "Castling: "
            << (m_castlingRights[0] ? "K" : "") // White kingside
            << (m_castlingRights[1] ? "Q" : "") // White queenside
            << (m_castlingRights[2] ? "k" : "") // Black kingside
            << (m_castlingRights[3] ? "q" : "") // Black queenside
            << "\n";
        
        // Move counters
        ss << "Half-move: " << m_halfMoveClock << ", Full-move: " << m_fullMoveNumber << "\n";

        return ss.str();
    }

    // Calculate material balance (sum of piece values)
    // Positive = White ahead, Negative = Black ahead
    //
    // Simple material evaluation without positional factors
    // Used primarily for display/debugging, not for search evaluation
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

    // ========== FEN PARSER IMPLEMENTATION ==========
    // FEN (Forsyth-Edwards Notation) is standard format for chess positions
    //
    // FEN format: 6 space-separated fields
    // 1. Piece placement (rank 8 to rank 1, '/' separates ranks)
    // 2. Active color ('w' or 'b')
    // 3. Castling rights ('KQkq', '-' if none)
    // 4. En passant target square (e.g., 'e3', '-' if none)
    // 5. Halfmove clock (fifty-move rule counter)
    // 6. Fullmove number (starts at 1, incremented after Black's move)
    //
    // Example: "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

    // Parse FEN string into board state
    // Returns true on success, false on parse error
    //
    // On success, board is fully initialized including:
    // - Piece positions
    // - King square tracking
    // - Piece lists
    // - All metadata (side to move, castling, etc.)
    bool FENParser::Parse(const std::string& fen, Board& board)
    {
        std::istringstream fenStream(fen);
        std::string segment;

        // Field 1: Piece placement
        if (!std::getline(fenStream, segment, ' '))
            return false;

        if (!ParsePiecePlacement(segment, board.m_board))
            return false;

        // Field 2: Active color
        if (!std::getline(fenStream, segment, ' '))
            return false;

        if (!ParseActiveColor(segment, board.m_sideToMove))
            return false;

        // Field 3: Castling rights
        if (!std::getline(fenStream, segment, ' '))
            return false;

        if (!ParseCastlingRights(segment, board.m_castlingRights))
            return false;

        // Field 4: En passant target square
        if (!std::getline(fenStream, segment, ' '))
            return false;

        if (!ParseEnPassant(segment, board.m_enPassantSquare))
            return false;

        // Field 5: Halfmove clock
        if (!std::getline(fenStream, segment, ' '))
            return false;

        std::string fullMoveStr;
        // Field 6: Fullmove number
        if (!std::getline(fenStream, fullMoveStr, ' '))
            return false;

        if (!ParseMoveClocks(segment, fullMoveStr,
            board.m_halfMoveClock, board.m_fullMoveNumber))
            return false;

        // Initialize king square tracking and piece lists
        board.m_kingSquares[0] = -1;
        board.m_kingSquares[1] = -1;
        board.m_pieceLists[0].Clear();
        board.m_pieceLists[1].Clear();

        // Scan board to find kings and populate piece lists
        for (int i = 0; i < SQUARE_COUNT; ++i)
        {
            Piece piece = board.m_board[i];
            if (!piece.IsEmpty())
            {
                if (piece.GetType() == PieceType::King)
                {
                    board.m_kingSquares[static_cast<int>(piece.GetColor())] = i;
                }
                board.m_pieceLists[static_cast<int>(piece.GetColor())].Add(i);
            }
        }

        // Reset move history (new position)
        board.m_historyPly = 0;
        board.m_nullMovePly = 0;

        return true;
    }

    // Generate FEN string from current board state
    // Inverse of Parse() - creates FEN representation of position
    std::string FENParser::Generate(const Board& board)
    {
        std::stringstream fen;

        // Field 1: Piece placement (rank 8 to rank 1)
        for (int rank = BOARD_SIZE - 1; rank >= 0; --rank)
        {
            int emptyCount = 0;

            for (int file = 0; file < BOARD_SIZE; ++file)
            {
                Piece piece = board.GetPieceAt(file, rank);

                if (piece.IsEmpty())
                {
                    emptyCount++; // Count consecutive empty squares
                }
                else
                {
                    // Write empty count if any
                    if (emptyCount > 0)
                    {
                        fen << emptyCount;
                        emptyCount = 0;
                    }

                    // Write piece character (uppercase = White, lowercase = Black)
                    char symbol = piece.GetSymbol()[0];
                    if (piece.GetColor() == PlayerColor::Black)
                    {
                        symbol = static_cast<char>(std::tolower(symbol));
                    }
                    fen << symbol;
                }
            }

            // Write remaining empty count for this rank
            if (emptyCount > 0)
            {
                fen << emptyCount;
            }

            // Rank separator (except after last rank)
            if (rank > 0)
            {
                fen << '/';
            }
        }

        // Field 2: Active color
        fen << (board.GetSideToMove() == PlayerColor::White ? " w " : " b ");

        // Field 3: Castling rights
        bool anyCastling = false;
        if (board.m_castlingRights[0]) { fen << 'K'; anyCastling = true; }
        if (board.m_castlingRights[1]) { fen << 'Q'; anyCastling = true; }
        if (board.m_castlingRights[2]) { fen << 'k'; anyCastling = true; }
        if (board.m_castlingRights[3]) { fen << 'q'; anyCastling = true; }

        if (!anyCastling)
        {
            fen << '-';
        }

        // Field 4: En passant target square
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

        // Field 5: Halfmove clock
        fen << ' ' << board.GetHalfMoveClock();

        // Field 6: Fullmove number
        fen << ' ' << board.GetFullMoveNumber();

        return fen.str();
    }

    // Parse piece placement field (rank 8 to rank 1)
    // Format: pieces and digits separated by '/'
    // Digits represent consecutive empty squares
    //
    // Example: "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR"
    bool FENParser::ParsePiecePlacement(const std::string& placement,
                                        std::array<Piece, SQUARE_COUNT>& board)
    {
        int square = 56; // Start at a8 (rank 8, file a)
        board.fill(EMPTY_PIECE);

        for (char c : placement)
        {
            if (c == '/')
            {
                square -= 16; // Move to start of next rank down (skip 8 back, skip 8 forward we added)
            }
            else if (std::isdigit(static_cast<unsigned char>(c)))
            {
                // Digit = number of empty squares
                square += static_cast<int>(c - '0');
            }
            else
            {
                // Piece character
                PieceType type = PieceType::None;
                PlayerColor color = std::isupper(static_cast<unsigned char>(c))
                    ? PlayerColor::White
                    : PlayerColor::Black;

                char lowerC = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c))
                );

                // Map character to piece type
                switch (lowerC)
                {
                    case 'p': type = PieceType::Pawn; break;
                    case 'n': type = PieceType::Knight; break;
                    case 'b': type = PieceType::Bishop; break;
                    case 'r': type = PieceType::Rook; break;
                    case 'q': type = PieceType::Queen; break;
                    case 'k': type = PieceType::King; break;
                    default: return false; // Invalid character
                }

                if (square < 0 || square >= SQUARE_COUNT)
                    return false;

                board[square] = Piece(type, color);
                square++;
            }
        }

        // Should end at square 8 (off the board after rank 1)
        if (square != 8)
            return false;

        return true;
    }

    // Parse active color field ('w' or 'b')
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

    // Parse castling rights field
    // Format: 'KQkq' (any combination), or '-' for none
    // K = White kingside, Q = White queenside
    // k = Black kingside, q = Black queenside
    bool FENParser::ParseCastlingRights(const std::string& castlingStr,
                                        std::array<bool, 4>& rights)
    {
        rights = {false, false, false, false};

        if (castlingStr == "-")
            return true; // No castling rights

        for (char c : castlingStr)
        {
            switch (c)
            {
                case 'K': rights[0] = true; break; // White kingside
                case 'Q': rights[1] = true; break; // White queenside
                case 'k': rights[2] = true; break; // Black kingside
                case 'q': rights[3] = true; break; // Black queenside
                default: return false; // Invalid character
            }
        }

        return true;
    }

    // Parse en passant target square field
    // Format: algebraic notation (e.g., 'e3'), or '-' for none
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

        // Validate algebraic notation
        if (fileChar < 'a' || fileChar > 'h' || rankChar < '1' || rankChar > '8')
            return false;

        int file = static_cast<int>(fileChar - 'a');
        int rank = static_cast<int>(rankChar - '1');

        enPassantSquare = CoordinateToIndex(file, rank);
        return true;
    }

    // Parse move clock fields (halfmove clock and fullmove number)
    // Halfmove clock: moves since last capture or pawn move (fifty-move rule)
    // Fullmove number: starts at 1, incremented after Black's move
    bool FENParser::ParseMoveClocks(const std::string& halfMoveStr,
                                    const std::string& fullMoveStr,
                                    int& halfMoveClock, int& fullMoveNumber)
    {
        try
        {
            halfMoveClock = std::stoi(halfMoveStr);
            fullMoveNumber = std::stoi(fullMoveStr);

            // Validate ranges
            if (halfMoveClock < 0 || fullMoveNumber < 1)
                return false;

            return true;
        }
        catch (...)
        {
            return false; // Parsing failed
        }
    }
}