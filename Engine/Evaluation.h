// Evaluation.h
// Position evaluation interface for chess engine
// Provides static evaluation scoring and piece value constants
#pragma once

#include "Board.h"

namespace Chess
{
    // ========== PIECE VALUES (CENTIPAWNS) ==========
    // Standard material values used throughout the engine
    // Centipawn scale: 100 = one pawn
    constexpr int PAWN_VALUE = 100;
    constexpr int KNIGHT_VALUE = 320;
    constexpr int BISHOP_VALUE = 330;  // Slightly better than knight (bishop pair bonus)
    constexpr int ROOK_VALUE = 500;
    constexpr int QUEEN_VALUE = 900;
    constexpr int KING_VALUE = 20000;  // Effectively infinite (losing king = losing game)

    // ========== SEARCH BOUNDS ==========
    // Special score values for search algorithm
    constexpr int MATE_SCORE = 29000;      // Checkmate score threshold
    constexpr int INFINITY_SCORE = 31000;  // Alpha-beta infinity bound

    // ========== EVALUATION FUNCTIONS ==========
    
    // Main evaluation function - comprehensive position assessment
    // Combines material, PST, king safety, mobility, pawn structure, passed pawns
    // Uses tapered eval to interpolate between middlegame and endgame
    // Returns score from side-to-move perspective (positive = good for side to move)
    int Evaluate(const Board& board);
    
    // Get base material value for piece type
    // Returns centipawn value without positional considerations
    int GetPieceValue(PieceType type);
    
    // Get positional bonus from piece-square table
    // Returns bonus/penalty for piece placement on specific square
    // PST values encourage good piece placement (center control, king safety, etc.)
    int GetPSTValue(PieceType type, int square, PlayerColor color);

    // Compute game phase as continuous value from 0 (endgame) to 256 (opening)
    // Based on remaining material with weights: Q=4, R=2, B=1, N=1
    // Used for tapered evaluation (interpolating between middlegame and endgame scores)
    // Returns: 256 at start (full material), 0 in bare king endgame
    int ComputePhase(const Board& board);
}