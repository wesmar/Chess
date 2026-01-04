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

    // Compute game phase as continuous value from 0 (endgame) to 256 (opening)
    // Based on remaining material with weights: Q=4, R=2, B=1, N=1
    int ComputePhase(const Board& board)
    {
        // Phase weights for non-pawn pieces
        constexpr int QUEEN_PHASE = 4;
        constexpr int ROOK_PHASE = 2;
        constexpr int BISHOP_PHASE = 1;
        constexpr int KNIGHT_PHASE = 1;

        // Maximum phase value from starting position:
        // 2 Queens * 4 + 4 Rooks * 2 + 4 Bishops * 1 + 4 Knights * 1 = 24
        constexpr int TOTAL_PHASE = 24;

        int phase = 0;

        // Count material and accumulate phase value
        for (int sq = 0; sq < 64; ++sq)
        {
            Piece piece = board.GetPieceAt(sq);
            if (piece.IsEmpty()) continue;

            PieceType type = piece.GetType();

            // Add phase contribution for non-pawn pieces
            switch (type)
            {
            case PieceType::Queen:  phase += QUEEN_PHASE; break;
            case PieceType::Rook:   phase += ROOK_PHASE; break;
            case PieceType::Bishop: phase += BISHOP_PHASE; break;
            case PieceType::Knight: phase += KNIGHT_PHASE; break;
            default: break;
            }
        }

        // Map phase to 0..256 scale (256 = opening, 0 = endgame)
        return (phase * 256 + (TOTAL_PHASE / 2)) / TOTAL_PHASE;
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

    // ========== PASSED PAWN EVALUATION ==========

    // Bonus for passed pawns by rank (from white's perspective: rank 1-7)
    // Exponential growth - pawns closer to promotion are much more valuable
    constexpr int PASSED_PAWN_BONUS[8] = {
        0,    // rank 1 (impossible for pawn)
        10,   // rank 2 (starting position)
        15,   // rank 3
        25,   // rank 4
        45,   // rank 5
        80,   // rank 6
        140,  // rank 7 (one step from promotion)
        0     // rank 8 (promoted, not a pawn anymore)
    };

    // Check if a pawn is passed (no enemy pawns blocking or attacking its path)
    bool IsPassedPawn(const Board& board, int square, PlayerColor color)
    {
        int file = square % 8;
        int rank = square / 8;

        // Direction pawn moves (white: +1 rank, black: -1 rank)
        int direction = (color == PlayerColor::White) ? 1 : -1;
        int promotionRank = (color == PlayerColor::White) ? 7 : 0;

        // Check all squares in front of pawn on same file and adjacent files
        for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f)
        {
            // Scan from pawn's rank toward promotion rank
            int r = rank + direction;
            while (r != promotionRank + direction && r >= 0 && r < 8)
            {
                Piece piece = board.GetPieceAt(f, r);
                if (piece.IsType(PieceType::Pawn) && !piece.IsColor(color))
                {
                    // Enemy pawn blocks or can capture - not passed
                    return false;
                }
                r += direction;
            }
        }

        return true;
    }

    // Manhattan distance between two squares
    int KingDistance(int sq1, int sq2)
    {
        int file1 = sq1 % 8, rank1 = sq1 / 8;
        int file2 = sq2 % 8, rank2 = sq2 / 8;
        return std::max(std::abs(file1 - file2), std::abs(rank1 - rank2));
    }

    // Evaluate passed pawns for both sides
    // Adds to mgScore and egScore via reference parameters
    void EvaluatePassedPawns(const Board& board, int& mgScore, int& egScore)
    {
        int whiteKingSq = board.GetKingSquare(PlayerColor::White);
        int blackKingSq = board.GetKingSquare(PlayerColor::Black);

        for (int sq = 0; sq < 64; ++sq)
        {
            Piece piece = board.GetPieceAt(sq);
            if (!piece.IsType(PieceType::Pawn)) continue;

            PlayerColor color = piece.GetColor();

            if (!IsPassedPawn(board, sq, color)) continue;

            // Get rank from pawn's perspective (how far advanced)
            int rank = sq / 8;
            int advancedRank = (color == PlayerColor::White) ? rank : (7 - rank);

            int bonus = PASSED_PAWN_BONUS[advancedRank];

            // King proximity bonus (endgame only)
            // Friendly king close = good, enemy king close = bad
            int friendlyKingSq = (color == PlayerColor::White) ? whiteKingSq : blackKingSq;
            int enemyKingSq = (color == PlayerColor::White) ? blackKingSq : whiteKingSq;

            int friendlyDist = KingDistance(sq, friendlyKingSq);
            int enemyDist = KingDistance(sq, enemyKingSq);

            // Bonus if our king supports the pawn (endgame)
            int kingSupport = (6 - friendlyDist) * 5;  // max +25 when adjacent

            // Bonus if enemy king is far (endgame)
            int enemyFar = (enemyDist - 2) * 8;  // bonus grows with distance

            // Rook behind passed pawn evaluation
            // Rook behind passer increases effectiveness, rook in front blocks it
            int rookBonus = 0;
            int pawnFile = sq % 8;
            int pawnRank = sq / 8;

            // Scan the file for rooks
            for (int r = 0; r < 8; ++r)
            {
                if (r == pawnRank) continue;

                Piece filePiece = board.GetPieceAt(pawnFile, r);
                if (!filePiece.IsType(PieceType::Rook)) continue;

                bool rookBehind;
                if (color == PlayerColor::White)
                    rookBehind = (r < pawnRank);  // Rook on lower rank = behind
                else
                    rookBehind = (r > pawnRank);  // Rook on higher rank = behind

                if (filePiece.IsColor(color))
                {
                    // Friendly rook
                    rookBonus += rookBehind ? 35 : -15;  // Behind = good, in front = bad
                }
                else
                {
                    // Enemy rook
                    rookBonus += rookBehind ? -40 : 20;  // Enemy behind = bad for us
                }
            }

            // Apply scores
            if (color == PlayerColor::White)
            {
                mgScore += bonus;
                egScore += bonus + bonus / 2 + kingSupport + enemyFar + rookBonus;
            }
            else
            {
                mgScore -= bonus;
                egScore -= bonus + bonus / 2 + kingSupport + enemyFar + rookBonus;
            }
        }
    }

// Evaluate piece activity and tactical patterns
// Bonuses for centralized pieces and active positioning
// Optimized to use PieceLists for efficient iteration
int EvaluateTacticalPosition(const Board& board)
{
    int score = 0;

    // Central squares provide strong positional advantage
    // These bonuses stack with PST values for compound effect
    constexpr int CENTER_CONTROL_BONUS[64] = {
        0,  0,  0,  0,  0,  0,  0,  0,
        0, 10, 15, 15, 15, 15, 10,  0,
        0, 15, 25, 30, 30, 25, 15,  0,
        0, 15, 30, 40, 40, 30, 15,  0,
        0, 15, 30, 40, 40, 30, 15,  0,
        0, 15, 25, 30, 30, 25, 15,  0,
        0, 10, 15, 15, 15, 15, 10,  0,
        0,  0,  0,  0,  0,  0,  0,  0
    };

    // Iterate only over existing pieces using PieceLists
    for (int colorIdx = 0; colorIdx < 2; ++colorIdx)
    {
        PlayerColor color = static_cast<PlayerColor>(colorIdx);
        const PieceList& list = board.GetPieceList(color);

        for (int i = 0; i < list.count; ++i)
        {
            int sq = list.squares[i];
            Piece piece = board.GetPieceAt(sq);

            if (piece.IsEmpty()) continue;

            // Apply center bonus for knights and bishops (most benefit from centralization)
            PieceType type = piece.GetType();
            if (type == PieceType::Knight || type == PieceType::Bishop)
            {
                int bonus = CENTER_CONTROL_BONUS[sq];
                if (color == PlayerColor::White)
                    score += bonus;
                else
                    score -= bonus;
            }
        }
    }

    return score;
}

    // ========== MAIN EVALUATION FUNCTION ==========

    // Complete position evaluation combining multiple factors
    // Returns score from side-to-move perspective (positive = advantage)
    // Uses tapered evaluation to smoothly blend middlegame and endgame scores
    int Evaluate(const Board& board)
    {
        // Start with incremental baseline (Material + PST for middlegame)
        int mgScore = board.GetIncrementalScore();
        int egScore = 0;
        int whiteBishops = 0;
        int blackBishops = 0;

        // Compute game phase (256 = opening/middlegame, 0 = endgame)
        int phase = ComputePhase(board);

        // 1. COMPUTE ENDGAME PST AND COMPLEX TERMS
        // Optimized to iterate only over existing pieces using PieceLists (~4x faster)
        auto processPieces = [&](PlayerColor color)
        {
            const PieceList& list = board.GetPieceList(color);
            for (int i = 0; i < list.count; ++i)
            {
                int sq = list.squares[i];
                Piece piece = board.GetPieceAt(sq);

                // Safety check to prevent desync errors
                if (piece.IsEmpty()) continue;

                PieceType type = piece.GetType();
                int value = GetPieceValue(type);
                int egPST = GetPSTValue(type, sq, color);

                // King uses different tables for middlegame vs endgame
                if (type == PieceType::King)
                {
                    int sq_adjusted = sq;
                    if (color == PlayerColor::White)
                    {
                        int file = sq % 8;
                        int rank = sq / 8;
                        sq_adjusted = (7 - rank) * 8 + file;
                    }
                    egPST = KING_ENDGAME_PST[sq_adjusted];
                }

                int egPieceScore = value + egPST;

                // Complex middlegame terms that rely on board context (not incremental)
                if (type == PieceType::Queen)
                {
                    PlayerColor enemyColor = (color == PlayerColor::White) ?
                        PlayerColor::Black : PlayerColor::White;
                    if (IsSquareAttacked(board, sq, enemyColor))
                    {
                        if (color == PlayerColor::White)
                            mgScore -= 150;
                        else
                            mgScore += 150;
                    }
                }

                if (color == PlayerColor::White)
                {
                    egScore += egPieceScore;
                    if (type == PieceType::Bishop) whiteBishops++;
                }
                else
                {
                    egScore -= egPieceScore;
                    if (type == PieceType::Bishop) blackBishops++;
                }
            }
        };

        // Process both White and Black piece lists
        processPieces(PlayerColor::White);
        processPieces(PlayerColor::Black);

        // 2. BISHOP PAIR BONUS
        // Having both bishops gives significant advantage in open positions
        if (whiteBishops >= 2)
        {
            mgScore += 40;
            egScore += 40;
        }
        if (blackBishops >= 2)
        {
            mgScore -= 40;
            egScore -= 40;
        }

        // 3. KING SAFETY
        // Only relevant in middlegame; fades out automatically with phase
        int kingSafety = EvaluateKingSafety(board, PlayerColor::White) -
                         EvaluateKingSafety(board, PlayerColor::Black);
        mgScore += kingSafety;  // Add to middlegame only
        // egScore += 0;  // Not added to endgame (king safety irrelevant)

        // 4. MOBILITY
        // Important in middlegame, less so in endgame
        int mobility = EvaluateMobility(board);
        mgScore += mobility;      // Full weight in middlegame
        egScore += mobility / 2;  // Half weight in endgame

        // 5. PAWN STRUCTURE
        // Penalize weak pawn formations (doubled, isolated)
        int pawnStructure = EvaluatePawnStructure(board, PlayerColor::White) -
                            EvaluatePawnStructure(board, PlayerColor::Black);
        mgScore += pawnStructure;
        egScore += pawnStructure;

        // 6. PASSED PAWNS
        // Reward advanced passed pawns with king support in endgame
        EvaluatePassedPawns(board, mgScore, egScore);

        // 7. TACTICAL POSITIONING
        // Reward active piece placement and central control
        int tacticalScore = EvaluateTacticalPosition(board);
        mgScore += tacticalScore;
        egScore += tacticalScore / 2;  // Less important in endgame

        // 8. TEMPO BONUS
        // Small bonus for side to move (having the initiative)
        int tempo = (board.GetSideToMove() == PlayerColor::White) ? 10 : -10;
        mgScore += tempo;
        egScore += tempo;

        // 9. TAPERED EVALUATION
        // Smoothly blend middlegame and endgame scores based on phase
        // phase=256 (opening) → use mgScore, phase=0 (endgame) → use egScore
        int score = (mgScore * phase + egScore * (256 - phase)) / 256;

        // Return score from side-to-move perspective
        return (board.GetSideToMove() == PlayerColor::White) ? score : -score;
    }
}