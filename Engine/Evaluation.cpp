// Evaluation.cpp
// Position evaluation system for chess engine
//
// This module implements a sophisticated evaluation function that assesses chess positions
// from the current side-to-move perspective. The evaluation combines multiple factors:
//
// 1. MATERIAL - base piece values (pawn=100, knight=320, bishop=330, etc.)
// 2. PIECE-SQUARE TABLES (PST) - positional bonuses for piece placement
// 3. KING SAFETY - pawn shield, castling status, open files near king
// 4. MOBILITY - number of legal moves available for pieces
// 5. PAWN STRUCTURE - doubled pawns, isolated pawns, pawn chains
// 6. PASSED PAWNS - pawns with no opposing pawns blocking their path
// 7. TACTICAL POSITIONING - central control, piece coordination
// 8. GAME PHASE - interpolation between middlegame and endgame evaluation
//
// The evaluation uses tapered eval technique: blending middlegame and endgame scores
// based on remaining material (phase). This ensures smooth transitions as pieces are traded.

#include "Evaluation.h"
#include "MoveGenerator.h"
#include <array>
#include <algorithm>

namespace Chess
{
    // Piece-Square Tables (PST) - positional bonuses for piece placement
    // These tables encode chess principles: control center, develop pieces, king safety
    // Values are from White's perspective (rank 0 = White's back rank, rank 7 = Black's back rank)
    // Black's values are automatically mirrored when evaluating

    // Pawn PST - encourages central control and advancement
    // Pawns gain value as they advance towards promotion
    // Central pawns (d/e files) are more valuable than flank pawns
    constexpr int PAWN_PST[64] = {
         0,  0,  0,  0,  0,  0,  0,  0,   // Rank 1 (back rank) - pawns shouldn't be here
        50, 50, 50, 50, 50, 50, 50, 50,   // Rank 2 (starting rank) - good value
        10, 10, 20, 30, 30, 20, 10, 10,   // Rank 3 - central pawns get bonus
         5,  5, 10, 25, 25, 10,  5,  5,   // Rank 4 - advanced central pawns very strong
         0,  0,  0, 20, 20,  0,  0,  0,   // Rank 5 - central control bonus
         5, -5,-10,  0,  0,-10, -5,  5,   // Rank 6 - discourage weak pawns
         5, 10, 10,-20,-20, 10, 10,  5,   // Rank 7 - about to promote (huge value)
         0,  0,  0,  0,  0,  0,  0,  0    // Rank 8 - promotion square
    };

    // Knight PST - encourages centralization and development
    // Knights are most effective in the center where they control many squares
    // Edge and corner knights are penalized (fewer squares controlled)
    constexpr int KNIGHT_PST[64] = {
       -50,-40,-30,-30,-30,-30,-40,-50,   // Back rank - undeveloped knights
       -40,-20,  0,  0,  0,  0,-20,-40,   // Rank 2 - slightly better
       -30,  0, 10, 15, 15, 10,  0,-30,   // Rank 3 - good development
       -30,  5, 15, 20, 20, 15,  5,-30,   // Rank 4 - strong central knights
       -30,  0, 15, 20, 20, 15,  0,-30,   // Rank 5 - also very strong
       -30,  5, 10, 15, 15, 10,  5,-30,   // Rank 6 - advanced knights
       -40,-20,  0,  5,  5,  0,-20,-40,   // Rank 7
       -50,-40,-30,-30,-30,-30,-40,-50    // Rank 8 - "knight on the rim is dim"
    };

    // Bishop PST - encourages long diagonals and development
    // Bishops work best on long open diagonals
    // Fianchetto positions (b2/g2) are valuable
    constexpr int BISHOP_PST[64] = {
       -20,-10,-10,-10,-10,-10,-10,-20,   // Back rank
       -10,  0,  0,  0,  0,  0,  0,-10,   // Rank 2
       -10,  0,  5, 10, 10,  5,  0,-10,   // Rank 3 - good diagonals
       -10,  5,  5, 10, 10,  5,  5,-10,   // Rank 4 - central bishops
       -10,  0, 10, 10, 10, 10,  0,-10,   // Rank 5 - strong placement
       -10, 10, 10, 10, 10, 10, 10,-10,   // Rank 6 - very active
       -10,  5,  0,  0,  0,  0,  5,-10,   // Rank 7 - fianchetto bonus
       -20,-10,-10,-10,-10,-10,-10,-20    // Rank 8
    };

    // Rook PST - encourages open files and 7th rank
    // Rooks belong on open files and the 7th rank (attacking pawns)
    // Rooks are relatively piece-value heavy, so PST bonuses are smaller
    constexpr int ROOK_PST[64] = {
         0,  0,  0,  0,  0,  0,  0,  0,   // Rank 1 - starting position
         5, 10, 10, 10, 10, 10, 10,  5,   // Rank 2 - bonus for active rooks
        -5,  0,  0,  0,  0,  0,  0, -5,   // Rank 3
        -5,  0,  0,  0,  0,  0,  0, -5,   // Rank 4
        -5,  0,  0,  0,  0,  0,  0, -5,   // Rank 5
        -5,  0,  0,  0,  0,  0,  0, -5,   // Rank 6
        -5,  0,  0,  0,  0,  0,  0, -5,   // Rank 7 - "rook on the 7th" is powerful
         0,  0,  0,  5,  5,  0,  0,  0    // Rank 8
    };

    // Queen PST - encourages development and avoiding early aggression
    // Queen should develop later (not too early to avoid being chased)
    // Central queen is strong but also vulnerable
    constexpr int QUEEN_PST[64] = {
       -20,-10,-10, -5, -5,-10,-10,-20,   // Back rank
       -10,  0,  0,  0,  0,  0,  0,-10,   // Rank 2 - premature development penalty
       -10,  0,  5,  5,  5,  5,  0,-10,   // Rank 3
        -5,  0,  5,  5,  5,  5,  0, -5,   // Rank 4 - centralized queen
         0,  0,  5,  5,  5,  5,  0, -5,   // Rank 5
       -10,  5,  5,  5,  5,  5,  0,-10,   // Rank 6
       -10,  0,  5,  0,  0,  0,  0,-10,   // Rank 7
       -20,-10,-10, -5, -5,-10,-10,-20    // Rank 8
    };

    // King PST (Middlegame) - encourages castling and safety
    // King should castle and stay behind pawn shield in middlegame
    // Center is dangerous for king when pieces are on board
    constexpr int KING_PST[64] = {
       -30,-40,-40,-50,-50,-40,-40,-30,   // Rank 1 - center king is dangerous
       -30,-40,-40,-50,-50,-40,-40,-30,   // Rank 2
       -30,-40,-40,-50,-50,-40,-40,-30,   // Rank 3
       -30,-40,-40,-50,-50,-40,-40,-30,   // Rank 4
       -20,-30,-30,-40,-40,-30,-30,-20,   // Rank 5
       -10,-20,-20,-20,-20,-20,-20,-10,   // Rank 6
        20, 20,  0,  0,  0,  0, 20, 20,   // Rank 7 - g1/b1 castled positions are safe
        20, 30, 10,  0,  0, 10, 30, 20    // Rank 8 - castled king is very safe
    };

    // King PST (Endgame) - encourages centralization
    // In endgame with few pieces, king becomes active piece
    // Central king is crucial for controlling squares and supporting pawns
    constexpr int KING_ENDGAME_PST[64] = {
       -50,-40,-30,-20,-20,-30,-40,-50,   // Rank 1 - still avoid back rank
       -30,-20,-10,  0,  0,-10,-20,-30,   // Rank 2
       -30,-10, 20, 30, 30, 20,-10,-30,   // Rank 3 - start moving forward
       -30,-10, 30, 40, 40, 30,-10,-30,   // Rank 4 - strong central king
       -30,-10, 30, 40, 40, 30,-10,-30,   // Rank 5
       -30,-10, 20, 30, 30, 20,-10,-30,   // Rank 6
       -30,-30,  0,  0,  0,  0,-30,-30,   // Rank 7
       -50,-30,-30,-30,-30,-30,-30,-50    // Rank 8 - edge still not ideal
    };

    // Get base material value for piece type
    // These are standard centipawn values used in most chess engines
    int GetPieceValue(PieceType type)
    {
        switch (type)
        {
        case PieceType::Pawn: return PAWN_VALUE;      // 100
        case PieceType::Knight: return KNIGHT_VALUE;  // 320
        case PieceType::Bishop: return BISHOP_VALUE;  // 330 (slightly better than knight)
        case PieceType::Rook: return ROOK_VALUE;      // 500
        case PieceType::Queen: return QUEEN_VALUE;    // 900
        case PieceType::King: return KING_VALUE;      // 20000 (effectively infinite)
        default: return 0;
        }
    }

    // Get positional bonus from piece-square table
    // PST values are added to base material value for total piece worth
    //
    // Important: PST are defined from White's perspective
    // For Black pieces, we mirror the rank (rank 0 <-> rank 7)
    int GetPSTValue(PieceType type, int square, PlayerColor color)
    {
        int sq = square;

        // Mirror the square for White pieces (PST defined from White's view)
        // This means white pawn on e4 gets same bonus as black pawn on e5
        if (color == PlayerColor::White)
        {
            int file = square % 8;
            int rank = square / 8;
            sq = (7 - rank) * 8 + file;  // Flip rank: 0->7, 1->6, etc.
        }

        // Lookup positional value in appropriate table
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

    // Check if square is attacked by given color
    // Wrapper around MoveGenerator's IsSquareAttacked for convenience
    bool IsSquareAttacked(const Board& board, int square, PlayerColor byColor)
    {
        return MoveGenerator::IsSquareAttacked(board.GetPieces(), square, byColor);
    }

    // Check if square is defended by given color
    // A square is defended if our own pieces can attack it
    // This is useful for evaluating piece safety and pawn structure
    bool IsDefended(const Board& board, int square, PlayerColor color)
    {
        return IsSquareAttacked(board, square, color);
    }
	
	// Evaluate king safety for given color
    // King safety is critical in middlegame - unsafe king leads to tactical vulnerabilities
    //
    // Factors considered:
    // - Castled position bonus (king on g1/c1 is safer than e1)
    // - Pawn shield in front of king (f2-g2-h2 for kingside castle)
    // - Open files near king (allows rook attacks)
    //
    // Returns positive score for safe king, negative for exposed king
    int EvaluateKingSafety(const Board& board, PlayerColor color)
    {
        int kingSquare = board.GetKingSquare(color);
        if (kingSquare == -1) return 0; // No king (shouldn't happen in valid position)

        int safety = 0;
        int file = kingSquare % 8;
        int rank = kingSquare / 8;

        // Castling position bonus
        // King on g-file (kingside castle) or c-file (queenside castle) is safer
        if (color == PlayerColor::White && rank == 0)
        {
            if (file == 6 || file == 2) // g1 or c1
            {
                safety += 30;
            }
        }
        else if (color == PlayerColor::Black && rank == 7)
        {
            if (file == 6 || file == 2) // g8 or c8
            {
                safety += 30;
            }
        }

        // Pawn shield evaluation
        // Pawns one rank ahead of king provide protection from attacks
        int direction = (color == PlayerColor::White) ? 1 : -1;
        int shieldRank = rank + direction;

        if (shieldRank >= 0 && shieldRank < 8)
        {
            // Check three files: directly ahead and both diagonals
            for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f)
            {
                int shieldSq = shieldRank * 8 + f;
                Piece piece = board.GetPieceAt(shieldSq);

                // Friendly pawn in shield position is valuable protection
                if (piece.IsType(PieceType::Pawn) && piece.IsColor(color))
                {
                    safety += 20;
                }
            }
        }

        // Open file evaluation
        // Files near king without friendly pawns are dangerous (rook attacks)
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

    // Evaluate piece mobility (number of squares pieces can move to)
    // Higher mobility = better position, more tactical options
    //
    // Only evaluates Bishop, Rook, and Queen mobility (most important sliding pieces)
    // Knights and pawns have more predictable mobility patterns
    //
    // This is a simplified mobility evaluation - doesn't distinguish between
    // attacking moves and quiet moves, but still provides valuable positional info
    int EvaluateMobility(const Board& board)
    {
        // Direction vectors for sliding pieces
        constexpr std::array<std::pair<int, int>, 4> BISHOP_DIRECTIONS = {{
            {1, 1}, {1, -1}, {-1, 1}, {-1, -1}
        }};

        constexpr std::array<std::pair<int, int>, 4> ROOK_DIRECTIONS = {{
            {1, 0}, {0, 1}, {-1, 0}, {0, -1}
        }};

        int whiteMobility = 0;
        int blackMobility = 0;

        const auto& pieces = board.GetPieces();

        // Evaluate mobility for both colors
        for (int colorIdx = 0; colorIdx < 2; ++colorIdx)
        {
            PlayerColor color = static_cast<PlayerColor>(colorIdx);
            const PieceList& list = board.GetPieceList(color);

            for (int i = 0; i < list.count; ++i)
            {
                int sq = list.squares[i];
                Piece piece = pieces[sq];

                if (piece.IsEmpty()) continue;

                PieceType type = piece.GetType();
                int mobility = 0;

                int file = sq % 8;
                int rank = sq / 8;

                // Bishop mobility - count squares along diagonals
                if (type == PieceType::Bishop)
                {
                    for (const auto& [df, dr] : BISHOP_DIRECTIONS)
                    {
                        int f = file + df;
                        int r = rank + dr;

                        // Slide along diagonal until blocked
                        while (f >= 0 && f < 8 && r >= 0 && r < 8)
                        {
                            int targetSq = r * 8 + f;
                            Piece target = pieces[targetSq];

                            if (target.IsEmpty())
                            {
                                mobility++; // Empty square = mobility
                                f += df;
                                r += dr;
                            }
                            else if (target.IsOppositeColor(piece))
                            {
                                mobility++; // Can capture = mobility
                                break;      // But stops sliding here
                            }
                            else
                            {
                                break; // Blocked by own piece
                            }
                        }
                    }
                }
                // Rook mobility - count squares along ranks/files
                else if (type == PieceType::Rook)
                {
                    for (const auto& [df, dr] : ROOK_DIRECTIONS)
                    {
                        int f = file + df;
                        int r = rank + dr;

                        while (f >= 0 && f < 8 && r >= 0 && r < 8)
                        {
                            int targetSq = r * 8 + f;
                            Piece target = pieces[targetSq];

                            if (target.IsEmpty())
                            {
                                mobility++;
                                f += df;
                                r += dr;
                            }
                            else if (target.IsOppositeColor(piece))
                            {
                                mobility++;
                                break;
                            }
                            else
                            {
                                break;
                            }
                        }
                    }
                }
                // Queen mobility - combines bishop and rook mobility
                else if (type == PieceType::Queen)
                {
                    // Diagonal moves (like bishop)
                    for (const auto& [df, dr] : BISHOP_DIRECTIONS)
                    {
                        int f = file + df;
                        int r = rank + dr;

                        while (f >= 0 && f < 8 && r >= 0 && r < 8)
                        {
                            int targetSq = r * 8 + f;
                            Piece target = pieces[targetSq];

                            if (target.IsEmpty())
                            {
                                mobility++;
                                f += df;
                                r += dr;
                            }
                            else if (target.IsOppositeColor(piece))
                            {
                                mobility++;
                                break;
                            }
                            else
                            {
                                break;
                            }
                        }
                    }

                    // Orthogonal moves (like rook)
                    for (const auto& [df, dr] : ROOK_DIRECTIONS)
                    {
                        int f = file + df;
                        int r = rank + dr;

                        while (f >= 0 && f < 8 && r >= 0 && r < 8)
                        {
                            int targetSq = r * 8 + f;
                            Piece target = pieces[targetSq];

                            if (target.IsEmpty())
                            {
                                mobility++;
                                f += df;
                                r += dr;
                            }
                            else if (target.IsOppositeColor(piece))
                            {
                                mobility++;
                                break;
                            }
                            else
                            {
                                break;
                            }
                        }
                    }
                }

                // Accumulate mobility for each color
                if (color == PlayerColor::White)
                    whiteMobility += mobility;
                else
                    blackMobility += mobility;
            }
        }

        // Return mobility difference (positive = White has more mobility)
        return (whiteMobility - blackMobility);
    }

    // Compute game phase (0 = endgame, 256 = opening)
    // Game phase determines how much weight to give middlegame vs endgame evaluation
    //
    // Phase calculation based on material:
    // - Each queen = 4 phase points
    // - Each rook = 2 phase points
    // - Each bishop/knight = 1 phase point
    // - Pawns and kings don't affect phase
    //
    // Total starting phase = 24 (2 queens * 4 + 4 rooks * 2 + 4 bishops * 1 + 4 knights * 1)
    // Returns: 256 in opening (all pieces), 0 in bare king endgame
    int ComputePhase(const Board& board)
    {
        constexpr int QUEEN_PHASE = 4;   // Queens are most important for phase
        constexpr int ROOK_PHASE = 2;
        constexpr int BISHOP_PHASE = 1;
        constexpr int KNIGHT_PHASE = 1;

        constexpr int TOTAL_PHASE = 24;  // Maximum phase value

        int phase = 0;

        // Count material to determine phase
        for (int sq = 0; sq < 64; ++sq)
        {
            Piece piece = board.GetPieceAt(sq);
            if (piece.IsEmpty()) continue;

            PieceType type = piece.GetType();

            switch (type)
            {
            case PieceType::Queen:  phase += QUEEN_PHASE; break;
            case PieceType::Rook:   phase += ROOK_PHASE; break;
            case PieceType::Bishop: phase += BISHOP_PHASE; break;
            case PieceType::Knight: phase += KNIGHT_PHASE; break;
            default: break;
            }
        }

        // Scale phase to 0-256 range for smooth interpolation
        // Formula: (phase * 256 + TOTAL_PHASE/2) / TOTAL_PHASE
        // The +TOTAL_PHASE/2 provides rounding instead of truncation
        return (phase * 256 + (TOTAL_PHASE / 2)) / TOTAL_PHASE;
    }

    // Evaluate pawn structure weaknesses
    // Poor pawn structure is a long-term positional weakness
    //
    // Penalties for:
    // - Doubled pawns (two pawns on same file)
    // - Isolated pawns (no friendly pawns on adjacent files)
    //
    // Returns negative score for structural weaknesses
    int EvaluatePawnStructure(const Board& board, PlayerColor color)
    {
        int score = 0;
        std::array<int, 8> pawnFiles = {0}; // Count pawns per file

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

        // Penalize doubled pawns
        // Doubled pawns are weak because they can't protect each other
        // and only one can advance freely
        for (int file = 0; file < 8; ++file)
        {
            if (pawnFiles[file] > 1)
            {
                // Penalty increases with number of pawns on same file
                score -= 25 * (pawnFiles[file] - 1);
            }
        }

        // Penalize isolated pawns
        // Isolated pawns lack support from neighboring pawns
        // They're harder to defend and can become targets
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

    // Passed pawn bonuses by rank (how far advanced)
    // Passed pawns increase in value as they approach promotion
    // A passed pawn on 7th rank is nearly unstoppable
    constexpr int PASSED_PAWN_BONUS[8] = {
        0,    // Rank 1 - on back rank (impossible for passed pawn)
        10,   // Rank 2 - just started advancing
        15,   // Rank 3
        25,   // Rank 4
        45,   // Rank 5 - significant threat
        80,   // Rank 6 - very dangerous
        140,  // Rank 7 - almost promoting (huge bonus)
        0     // Rank 8 - promotion square
    };

    // Check if pawn is passed (no enemy pawns can block its advance)
    // A passed pawn has no opposing pawns on:
    // - Its own file
    // - Adjacent files (which could capture it)
    // And these pawns must be ahead of it (closer to promotion)
    bool IsPassedPawn(const Board& board, int square, PlayerColor color)
    {
        int file = square % 8;
        int rank = square / 8;

        int direction = (color == PlayerColor::White) ? 1 : -1;
        int promotionRank = (color == PlayerColor::White) ? 7 : 0;

        // Check three files: own file and both adjacent files
        for (int f = std::max(0, file - 1); f <= std::min(7, file + 1); ++f)
        {
            int r = rank + direction;
            
            // Scan from current rank towards promotion rank
            while (r != promotionRank + direction && r >= 0 && r < 8)
            {
                Piece piece = board.GetPieceAt(f, r);
                
                // If enemy pawn found ahead, this is not a passed pawn
                if (piece.IsType(PieceType::Pawn) && !piece.IsColor(color))
                {
                    return false;
                }
                r += direction;
            }
        }

        return true; // No blocking pawns found
    }

    // Calculate king distance using Chebyshev distance (chessboard distance)
    // This is the minimum number of king moves needed to reach a square
    // Equal to max(|file_diff|, |rank_diff|)
    int KingDistance(int sq1, int sq2)
    {
        int file1 = sq1 % 8, rank1 = sq1 / 8;
        int file2 = sq2 % 8, rank2 = sq2 / 8;
        return std::max(std::abs(file1 - file2), std::abs(rank1 - rank2));
    }
	
	// Evaluate passed pawns with endgame considerations
    // Passed pawns are extremely valuable in endgames
    //
    // This function considers several endgame factors:
    // - Base bonus for advancement (from PASSED_PAWN_BONUS table)
    // - King support (friendly king near passed pawn helps push it)
    // - Enemy king distance (far enemy king can't stop pawn)
    // - Rook placement (rook behind passed pawn is ideal - "Tarrasch rule")
    //
    // These bonuses are added to both middlegame and endgame scores
    // Endgame score gets extra weight since passed pawns are more decisive in endgames
    void EvaluatePassedPawns(const Board& board, int& mgScore, int& egScore)
    {
        int whiteKingSq = board.GetKingSquare(PlayerColor::White);
        int blackKingSq = board.GetKingSquare(PlayerColor::Black);

        // Check every square for passed pawns
        for (int sq = 0; sq < 64; ++sq)
        {
            Piece piece = board.GetPieceAt(sq);
            if (!piece.IsType(PieceType::Pawn)) continue;

            PlayerColor color = piece.GetColor();

            // Only evaluate if this is actually a passed pawn
            if (!IsPassedPawn(board, sq, color)) continue;

            int rank = sq / 8;
            
            // Calculate how advanced this pawn is (from its starting position)
            int advancedRank = (color == PlayerColor::White) ? rank : (7 - rank);

            // Base bonus from advancement table
            int bonus = PASSED_PAWN_BONUS[advancedRank];

            // Get king positions for endgame evaluation
            int friendlyKingSq = (color == PlayerColor::White) ? whiteKingSq : blackKingSq;
            int enemyKingSq = (color == PlayerColor::White) ? blackKingSq : whiteKingSq;

            // King support bonus (endgame)
            // Friendly king near passed pawn helps escort it to promotion
            int friendlyDist = KingDistance(sq, friendlyKingSq);
            int enemyDist = KingDistance(sq, enemyKingSq);

            int kingSupport = (6 - friendlyDist) * 5; // Closer king = higher bonus

            // Enemy king distance bonus (endgame)
            // Distant enemy king cannot stop the pawn
            int enemyFar = (enemyDist - 2) * 8; // Further enemy king = higher bonus

            // Rook behind pawn bonus (endgame)
            // "Rooks belong behind passed pawns" - Tarrasch
            // Rook behind pawn supports its advance and controls promotion square
            int rookBonus = 0;
            int pawnFile = sq % 8;
            int pawnRank = sq / 8;

            for (int r = 0; r < 8; ++r)
            {
                if (r == pawnRank) continue; // Skip pawn's own rank

                Piece filePiece = board.GetPieceAt(pawnFile, r);
                if (!filePiece.IsType(PieceType::Rook)) continue;

                // Determine if rook is behind or in front of pawn
                bool rookBehind;
                if (color == PlayerColor::White)
                    rookBehind = (r < pawnRank);  // White pawn advances up, so behind = lower rank
                else
                    rookBehind = (r > pawnRank);  // Black pawn advances down, so behind = higher rank

                if (filePiece.IsColor(color))
                {
                    // Friendly rook: behind is good (+35), in front is bad (-15)
                    rookBonus += rookBehind ? 35 : -15;
                }
                else
                {
                    // Enemy rook: behind is bad (-40), in front is good (+20)
                    rookBonus += rookBehind ? -40 : 20;
                }
            }

            // Apply all bonuses to scores
            // Middlegame: only base advancement bonus
            // Endgame: base bonus + 50% extra + king factors + rook placement
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

    // Evaluate tactical positioning (central control)
    // Control of center squares (d4, e4, d5, e5) is fundamental chess principle
    // Extended center (c3-c6, f3-f6) also has value
    //
    // Knights and bishops benefit most from central positions
    // Center control allows:
    // - More mobility and flexibility
    // - Better piece coordination
    // - Control of key squares
    // - Support for both flanks
    int EvaluateTacticalPosition(const Board& board)
    {
        int score = 0;

        // Central control bonus table
        // Higher values for center (d4/e4/d5/e5), lower for extended center
        constexpr int CENTER_CONTROL_BONUS[64] = {
            0,  0,  0,  0,  0,  0,  0,  0,
            0, 10, 15, 15, 15, 15, 10,  0,
            0, 15, 25, 30, 30, 25, 15,  0,
            0, 15, 30, 40, 40, 30, 15,  0,  // d4/e4 = 40 points
            0, 15, 30, 40, 40, 30, 15,  0,  // d5/e5 = 40 points
            0, 15, 25, 30, 30, 25, 15,  0,
            0, 10, 15, 15, 15, 15, 10,  0,
            0,  0,  0,  0,  0,  0,  0,  0
        };

        // Evaluate piece placement for both colors
        for (int colorIdx = 0; colorIdx < 2; ++colorIdx)
        {
            PlayerColor color = static_cast<PlayerColor>(colorIdx);
            const PieceList& list = board.GetPieceList(color);

            for (int i = 0; i < list.count; ++i)
            {
                int sq = list.squares[i];
                Piece piece = board.GetPieceAt(sq);

                if (piece.IsEmpty()) continue;

                PieceType type = piece.GetType();
                
                // Only knights and bishops get central control bonus
                // Rooks and queens prefer open files/diagonals over pure center control
                // Pawns get central bonus from PST already
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

    // Main evaluation function - combines all evaluation components
    // Returns score from side-to-move perspective (positive = good for side to move)
    //
    // Evaluation components:
    // 1. Material + PST (incrementally maintained in Board class)
    // 2. King safety (middlegame only)
    // 3. Mobility (piece activity)
    // 4. Pawn structure (doubled, isolated pawns)
    // 5. Passed pawns (with endgame bonuses)
    // 6. Tactical positioning (central control)
    // 7. Tempo bonus (side to move advantage)
    //
    // Uses tapered eval: mg_score * phase + eg_score * (256-phase) / 256
    // This smoothly transitions between middlegame and endgame evaluation
    int Evaluate(const Board& board)
    {
        // Start with incrementally maintained material+PST score
        int mgScore = board.GetIncrementalScore();
        int egScore = 0;
        int whiteBishops = 0;
        int blackBishops = 0;

        // Compute game phase for score interpolation
        int phase = ComputePhase(board);

        // Lambda to process pieces for one color
        // Evaluates piece-specific factors that aren't in incremental score
        auto processPieces = [&](PlayerColor color)
        {
            const PieceList& list = board.GetPieceList(color);
            for (int i = 0; i < list.count; ++i)
            {
                int sq = list.squares[i];
                Piece piece = board.GetPieceAt(sq);

                if (piece.IsEmpty()) continue;

                PieceType type = piece.GetType();
                int value = GetPieceValue(type);
                int egPST = GetPSTValue(type, sq, color);

                // Use endgame king PST for king in endgame evaluation
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

                // Penalize exposed queen in middlegame
                // Queen on attacked square is vulnerable to tactics
                if (type == PieceType::Queen)
                {
                    PlayerColor enemyColor = (color == PlayerColor::White) ?
                        PlayerColor::Black : PlayerColor::White;
                    if (IsSquareAttacked(board, sq, enemyColor))
                    {
                        if (color == PlayerColor::White)
                            mgScore -= 150;  // Queen under attack penalty
                        else
                            mgScore += 150;
                    }
                }

                // Accumulate endgame score
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

        processPieces(PlayerColor::White);
        processPieces(PlayerColor::Black);

        // Bishop pair bonus
        // Two bishops work well together, controlling both diagonal colors
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

        // King safety (middlegame only - kings become active in endgame)
        int kingSafety = EvaluateKingSafety(board, PlayerColor::White) -
                         EvaluateKingSafety(board, PlayerColor::Black);
        mgScore += kingSafety;

        // Mobility evaluation
        int mobility = EvaluateMobility(board);
        mgScore += mobility;
        egScore += mobility / 2;  // Half weight in endgame

        // Pawn structure evaluation
        int pawnStructure = EvaluatePawnStructure(board, PlayerColor::White) -
                            EvaluatePawnStructure(board, PlayerColor::Black);
        mgScore += pawnStructure;
        egScore += pawnStructure;

        // Passed pawns evaluation (critical in endgame)
        EvaluatePassedPawns(board, mgScore, egScore);

        // Tactical positioning (central control)
        int tacticalScore = EvaluateTacticalPosition(board);
        mgScore += tacticalScore;
        egScore += tacticalScore / 2;  // Less important in endgame

        // Tempo bonus - side to move has slight advantage
        // Having the initiative is valuable (can make threats, improve position)
        int tempo = (board.GetSideToMove() == PlayerColor::White) ? 10 : -10;
        mgScore += tempo;
        egScore += tempo;

        // Tapered evaluation: interpolate between middlegame and endgame scores
        // Formula: (mgScore * phase + egScore * (256 - phase)) / 256
        // phase=256 (opening) -> 100% mgScore
        // phase=0 (endgame) -> 100% egScore
        // phase=128 (middle) -> 50% mgScore + 50% egScore
        int score = (mgScore * phase + egScore * (256 - phase)) / 256;

        // Return from side-to-move perspective
        // Positive score = good for side to move, negative = bad
        return (board.GetSideToMove() == PlayerColor::White) ? score : -score;
    }
}