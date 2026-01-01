// Evaluation.h
#pragma once

#include "Board.h"

namespace Chess
{
    // Piece values in centipawns
    constexpr int PAWN_VALUE = 100;
    constexpr int KNIGHT_VALUE = 320;
    constexpr int BISHOP_VALUE = 330;
    constexpr int ROOK_VALUE = 500;
    constexpr int QUEEN_VALUE = 900;
    constexpr int KING_VALUE = 20000;

    // Search bounds
    constexpr int MATE_SCORE = 29000;
    constexpr int INFINITY_SCORE = 31000;

    // Main evaluation function - returns score from side-to-move perspective
    int Evaluate(const Board& board);
    
    // Get base material value for piece type
    int GetPieceValue(PieceType type);
    
	// Get positional bonus for piece on given square
	int GetPSTValue(PieceType type, int square, PlayerColor color);

	// Compute game phase as continuous value from 0 (endgame) to 256 (opening)
	// Based on remaining material with weights: Q=4, R=2, B=1, N=1
	int ComputePhase(const Board& board);
}