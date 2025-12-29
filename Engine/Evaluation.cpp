// Evaluation.cpp
// Advanced evaluation function with king safety, mobility, and pawn structure analysis
#include "Evaluation.h"
#include "MoveGenerator.h"
#include <array>
#include <algorithm>

namespace Chess
{
    // ========== PIECE-SQUARE TABLES ==========
    
    // Pawn positioning values - encourage center control and advancement
    constexpr int PAWN_PST[64] = {
         0,  0,  0,  0,  0,  0,  0,  0,
        50, 50, 50, 50, 50, 50, 50, 50,
        10, 10, 20, 30, 30, 20, 10, 10,
         5,  5, 10, 25, 25, 10,  5,  5,
         0,  0,  0, 20, 20,  0,  0,  0,
         5, -5,-10,  0,  0,-10, -5,  5,
         5, 10, 10,-20,-20, 10, 10,  5,
         0,  0,  0,  0,  0,  0,  0,  0
    };

    // Knight positioning - penalty for edges, bonus for central squares
    constexpr int KNIGHT_PST[64] = {
       -50,-40,-30,-30,-30,-30,-40,-50,
       -40,-20,  0,  0,  0,  0,-20,-40,
       -30,  0, 10, 15, 15, 10,  0,-30,
       -30,  5, 15, 20, 20, 15,  5,-30,
       -30,  0, 15, 20, 20, 15,  0,-30,
       -30,  5, 10, 15, 15, 10,  5,-30,
       -40,-20,  0,  5,  5,  0,-20,-40,
       -50,-40,-30,-30,-30,-30,-40,-50
    };

    // Bishop positioning - prefer long diagonals and center control
    constexpr int BISHOP_PST[64] = {
       -20,-10,-10,-10,-10,-10,-10,-20,
       -10,  0,  0,  0,  0,  0,  0,-10,
       -10,  0,  5, 10, 10,  5,  0,-10,
       -10,  5,  5, 10, 10,  5,  5,-10,
       -10,  0, 10, 10, 10, 10,  0,-10,
       -10, 10, 10, 10, 10, 10, 10,-10,
       -10,  5,  0,  0,  0,  0,  5,-10,
       -20,-10,-10,-10,-10,-10,-10,-20
    };

    // Rook positioning - prefer 7th rank and open files
    constexpr int ROOK_PST[64] = {
         0,  0,  0,  0,  0,  0,  0,  0,
         5, 10, 10, 10, 10, 10, 10,  5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
        -5,  0,  0,  0,  0,  0,  0, -5,
         0,  0,  0,  5,  5,  0,  0,  0
    };

    // Queen positioning - avoid early development, control center
    constexpr int QUEEN_PST[64] = {
       -20,-10,-10, -5, -5,-10,-10,-20,
       -10,  0,  0,  0,  0,  0,  0,-10,
       -10,  0,  5,  5,  5,  5,  0,-10,
        -5,  0,  5,  5,  5,  5,  0, -5,
         0,  0,  5,  5,  5,  5,  0, -5,
       -10,  5,  5,  5,  5,  5,  0,-10,
       -10,  0,  5,  0,  0,  0,  0,-10,
       -20,-10,-10, -5, -5,-10,-10,-20
    };

    // King middlegame positioning - encourage castling and safety
    constexpr int KING_PST[64] = {
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -30,-40,-40,-50,-50,-40,-40,-30,
       -20,-30,-30,-40,-40,-30,-30,-20,
       -10,-20,-20,-20,-20,-20,-20,-10,
        20, 20,  0,  0,  0,  0, 20, 20,
        20, 30, 10,  0,  0, 10, 30, 20
    };

    // King endgame positioning - active king in center is critical
    constexpr int KING_ENDGAME_PST[64] = {
       -50,-40,-30,-20,-20,-30,-40,-50,
       -30,-20,-10,  0,  0,-10,-20,-30,
       -30,-10, 20, 30, 30, 20,-10,-30,
       -30,-10, 30, 40, 40, 30,-10,-30,
       -30,-10, 30, 40, 40, 30,-10,-30,
       -30,-10, 20, 30, 30, 20,-10,-30,
       -30,-30,  0,  0,  0,  0,-30,-30,
       -50,-30,-30,-30,-30,-30,-30,-50
    };

    // ========== BASIC EVALUATION HELPERS ==========

    // Get base material value for piece type
    int GetPieceValue(PieceType type)
    {
        switch (type)
        {
        case PieceType::Pawn: return PAWN_VALUE;
        case PieceType::Knight: return KNIGHT_VALUE;
        case PieceType::Bishop: return BISHOP_VALUE;
        case PieceType::Rook: return ROOK_VALUE;
        case PieceType::Queen: return QUEEN_VALUE;
        case PieceType::King: return KING_VALUE;
        default: return 0;
        }
    }

    // Get piece-square table bonus for piece position (flipped for white)
    int GetPSTValue(PieceType type, int square, PlayerColor color)
    {
        int sq = square;
        
        // Flip board vertically for white pieces
        if (color == PlayerColor::White)
        {
            int file = square % 8;
            int rank = square / 8;
            sq = (7 - rank) * 8 + file;
        }

        switch (type)
        {
        case PieceType::Pawn: return PAWN_PST[sq];
        case PieceType::Knight: return KNIGHT_PST[sq];
        case PieceType::Bishop: return BISHOP_PST[sq];
        case PieceType::Rook: return ROOK_PST[sq];
        case PieceType::Queen: return QUEEN_PST[sq];
        case PieceType::King: return KING_PST[sq];
        default: return 0;
        }
    }

    // ========== ADVANCED EVALUATION HELPERS ==========

    // Check if a square is under attack by specified color
    bool IsSquareAttacked(const Board& board, int square, PlayerColor byColor)
    {
        return MoveGenerator::IsSquareAttacked(board.GetPieces(), square, byColor);
    }

    // Check if a piece on a square is defended by friendly pieces
    bool IsDefended(const Board& board, int square, PlayerColor color)
    {
        return IsSquareAttacked(board, square, color);
    }

    // Evaluate king safety based on pawn shield and position
    int EvaluateKingSafety(const Board& board, PlayerColor color)
    {
        int kingSquare = board.GetKingSquare(color);
        if (kingSquare == -1) return 0;

        int safety = 0;
        int file = kingSquare % 8;
        int rank = kingSquare / 8;

        // Bonus for castled king (king on flank positions)
        if (color == PlayerColor::White && rank == 0)
        {
            if (file == 6 || file == 2) // Kingside or queenside castling position
            {
                safety += 30;
            }
        }
        else if (color == PlayerColor::Black && rank == 7)
        {
            if (file == 6 || file == 2)
            {
                safety += 30;
            }
        }

        // Pawn shield evaluation - pawns in front of king provide protection
        int direction = (color == PlayerColor::White) ? 1 : -1;
        int shieldRank = rank + direction;

        if (shieldRank >= 0 && shieldRank < 8)
        {
            // Check three files in front of king
            for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f)
            {
                int shieldSq = shieldRank * 8 + f;
                Piece piece = board.GetPieceAt(shieldSq);
                
                if (piece.IsType(PieceType::Pawn) && piece.IsColor(color))
                {
                    safety += 20; // Bonus for each protecting pawn
                }
            }
        }

        // Penalty for open files near king (allows enemy rooks/queens to attack)
        for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f)
        {
            bool hasPawn = false;
            for (int r = 0; r < 8; ++r)
            {
                Piece p = board.GetPieceAt(f, r);
                if (p.IsType(PieceType::Pawn) && p.IsColor(color))
                {
                    hasPawn = true;
                    break;
                }
            }
            if (!hasPawn)
            {
                safety -= 15; // Penalty for open file near king
            }
        }

        return safety;
    }

    // Evaluate piece mobility - more available moves means better position
    int EvaluateMobility(const Board& board)
    {
        // Mobility: count only sliding piece moves (bishop/rook/queen)
        // Knights are intentionally ignored to avoid overvaluing them in closed positions

        auto countSlidingMovesFor = [&](PlayerColor color) -> int
        {
            auto moves = MoveGenerator::GeneratePseudoLegalMoves(
                board.GetPieces(),
                color,
                board.GetEnPassantSquare(),
                nullptr
            );

            int mobility = 0;
            for (const auto& move : moves)
            {
                Piece piece = board.GetPieceAt(move.GetFrom());
                const PieceType type = piece.GetType();

                if (type == PieceType::Bishop || type == PieceType::Rook || type == PieceType::Queen)
                {
                    mobility++;
                }
            }

            return mobility;
        };

        const int whiteMobility = countSlidingMovesFor(PlayerColor::White);
        const int blackMobility = countSlidingMovesFor(PlayerColor::Black);

        return (whiteMobility - blackMobility);
    }

    // ========== GAME PHASE DETECTION ==========

    // Determine current game phase based on remaining pieces
    enum class GamePhase { Opening, Middlegame, Endgame };

    GamePhase GetGamePhase(const Board& board)
    {
        int pieceCount = 0;
        int queenCount = 0;

        // Count non-pawn, non-king pieces
        for (int sq = 0; sq < 64; ++sq)
        {
            Piece piece = board.GetPieceAt(sq);
            if (!piece.IsEmpty() && piece.GetType() != PieceType::Pawn && piece.GetType() != PieceType::King)
            {
                pieceCount++;
                if (piece.GetType() == PieceType::Queen)
                    queenCount++;
            }
        }

        // Classify game phase based on remaining material
        if (pieceCount <= 6 || queenCount == 0)
            return GamePhase::Endgame;
        else if (pieceCount <= 12)
            return GamePhase::Middlegame;
        else
            return GamePhase::Opening;
    }

    // Evaluate pawn structure quality (penalizes weaknesses)
    int EvaluatePawnStructure(const Board& board, PlayerColor color)
    {
        int score = 0;
        std::array<int, 8> pawnFiles = {0};

        // Count pawns on each file
        for (int sq = 0; sq < 64; ++sq)
        {
            Piece piece = board.GetPieceAt(sq);
            if (piece.IsType(PieceType::Pawn) && piece.IsColor(color))
            {
                int file = sq % 8;
                pawnFiles[file]++;
            }
        }

        // Penalty for doubled pawns (multiple pawns on same file)
        for (int file = 0; file < 8; ++file)
        {
            if (pawnFiles[file] > 1)
            {
                score -= 25 * (pawnFiles[file] - 1);
            }
        }

        // Penalty for isolated pawns (no friendly pawns on adjacent files)
        for (int file = 0; file < 8; ++file)
        {
            if (pawnFiles[file] > 0)
            {
                bool hasNeighbor = false;
                if (file > 0 && pawnFiles[file - 1] > 0) hasNeighbor = true;
                if (file < 7 && pawnFiles[file + 1] > 0) hasNeighbor = true;

                if (!hasNeighbor)
                {
                    score -= 20; // Isolated pawn penalty
                }
            }
        }

        return score;
    }

    // ========== MAIN EVALUATION FUNCTION ==========

    // Complete position evaluation combining multiple factors
    // Returns score from side-to-move perspective (positive = advantage)
    int Evaluate(const Board& board)
    {
        int score = 0;
        int whiteBishops = 0;
        int blackBishops = 0;
        int whiteMaterial = 0;
        int blackMaterial = 0;

        GamePhase phase = GetGamePhase(board);

        // 1. MATERIAL AND PIECE-SQUARE TABLES
        for (int sq = 0; sq < 64; ++sq)
        {
            Piece piece = board.GetPieceAt(sq);
            if (piece.IsEmpty()) continue;

            PieceType type = piece.GetType();
            PlayerColor color = piece.GetColor();

            int value = GetPieceValue(type);
            int pstValue = 0;

            // Use endgame king table when appropriate
            if (type == PieceType::King && phase == GamePhase::Endgame)
            {
                int sq_adjusted = sq;
                if (color == PlayerColor::White)
                {
                    int file = sq % 8;
                    int rank = sq / 8;
                    sq_adjusted = (7 - rank) * 8 + file;
                }
                pstValue = KING_ENDGAME_PST[sq_adjusted];
            }
            else
            {
                pstValue = GetPSTValue(type, sq, color);
            }

            int pieceScore = value + pstValue;

            // Add to respective side's score and track material/bishops
            if (color == PlayerColor::White)
            {
                whiteMaterial += value;
                score += pieceScore;
                if (type == PieceType::Bishop) whiteBishops++;
            }
            else
            {
                blackMaterial += value;
                score -= pieceScore;
                if (type == PieceType::Bishop) blackBishops++;
            }
        }

        // 2. BISHOP PAIR BONUS
        // Having both bishops gives significant advantage in open positions
        if (whiteBishops >= 2) score += 40;
        if (blackBishops >= 2) score -= 40;

        // 3. KING SAFETY (only in opening and middlegame)
        // In endgame, active king is more important than safety
        if (phase != GamePhase::Endgame)
        {
            score += EvaluateKingSafety(board, PlayerColor::White);
            score -= EvaluateKingSafety(board, PlayerColor::Black);
        }

        // 4. MOBILITY (less important in opening)
        // Pieces with more available moves are generally better positioned
        if (phase != GamePhase::Opening)
        {
            score += EvaluateMobility(board);
        }

        // 5. PAWN STRUCTURE
        // Penalize weak pawn formations (doubled, isolated)
        score += EvaluatePawnStructure(board, PlayerColor::White);
        score -= EvaluatePawnStructure(board, PlayerColor::Black);

        // 6. TEMPO BONUS
        // Small bonus for side to move (having the initiative)
        if (board.GetSideToMove() == PlayerColor::White)
            score += 10;
        else
            score -= 10;

        // Return score from side-to-move perspective
        return (board.GetSideToMove() == PlayerColor::White) ? score : -score;
    }
}