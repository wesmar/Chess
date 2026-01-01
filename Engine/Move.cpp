// Move.cpp
// UCI move format conversion utilities
//
// UCI (Universal Chess Interface) uses a simple coordinate notation for moves:
// - Standard moves: "e2e4" (from square + to square)
// - Promotions: "e7e8q" (adds lowercase piece letter: q/r/b/n)
// - Castling: "e1g1" (king's actual movement, not "O-O")
//
// This differs from our internal representation which tracks MoveType explicitly.
// The FromUCI() function bridges this gap by matching against legal moves,
// ensuring we get the correct MoveType (castling, en passant, etc.) automatically.

#include "Move.h"
#include "Board.h"
#include "ChessConstants.h"
#include <cctype>

namespace Chess
{
    // Convert internal Move to UCI string format
    // Examples: "e2e4", "e7e8q", "e1g1" (kingside castling)
    std::string Move::ToUCI() const
    {
        auto [fromFile, fromRank] = IndexToCoordinate(GetFrom());
        auto [toFile, toRank] = IndexToCoordinate(GetTo());

        std::string uci;
        uci += FILE_NAMES[fromFile];
        uci += RANK_NAMES[fromRank];
        uci += FILE_NAMES[toFile];
        uci += RANK_NAMES[toRank];

        // UCI uses lowercase for promotion piece
        if (IsPromotion())
        {
            char promoChar = 'q';
            switch (GetPromotion())
            {
                case PieceType::Queen:  promoChar = 'q'; break;
                case PieceType::Rook:   promoChar = 'r'; break;
                case PieceType::Bishop: promoChar = 'b'; break;
                case PieceType::Knight: promoChar = 'n'; break;
                default: promoChar = 'q'; break;
            }
            uci += promoChar;
        }

        return uci;
    }

    // Parse UCI move string and match against legal moves
    // This approach is more robust than trying to infer MoveType from squares,
    // because castling, en passant, and captures require context from the position.
    std::optional<Move> Move::FromUCI(const std::string& uci, const Board& board)
    {
        // UCI moves are 4-5 characters: "e2e4" or "e7e8q"
        if (uci.size() < 4)
            return std::nullopt;

        // Parse file and rank characters to integer indices
        auto fileToInt = [](char c) -> int { return c - 'a'; };
        auto rankToInt = [](char c) -> int { return c - '1'; };

        const int fromFile = fileToInt(uci[0]);
        const int fromRank = rankToInt(uci[1]);
        const int toFile = fileToInt(uci[2]);
        const int toRank = rankToInt(uci[3]);

        // Validate coordinates are within board bounds
        if (!IsValidCoordinate(fromFile, fromRank) || !IsValidCoordinate(toFile, toRank))
            return std::nullopt;

        const int from = CoordinateToIndex(fromFile, fromRank);
        const int to = CoordinateToIndex(toFile, toRank);

        // Parse optional promotion piece (5th character)
        PieceType promo = PieceType::None;
        if (uci.size() >= 5)
        {
            char promoChar = static_cast<char>(std::tolower(static_cast<unsigned char>(uci[4])));
            switch (promoChar)
            {
                case 'q': promo = PieceType::Queen;  break;
                case 'r': promo = PieceType::Rook;   break;
                case 'b': promo = PieceType::Bishop; break;
                case 'n': promo = PieceType::Knight; break;
                default:  promo = PieceType::None;   break;
            }
        }

        // Find the matching move in legal move list
        // This gives us the correct MoveType, captured piece, etc.
        auto legalMoves = board.GenerateLegalMoves();
        for (const auto& move : legalMoves)
        {
            if (move.GetFrom() == from && 
                move.GetTo() == to && 
                move.GetPromotion() == promo)
            {
                return move;
            }
        }

        // No matching legal move found
        return std::nullopt;
    }
}
