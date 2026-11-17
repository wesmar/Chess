#pragma once

#include "Board.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace Chess::KPK
{
    // Exact win/draw attractor for K+P versus K, without the fifty-move rule.
    // Normalize to a white pawn on files a-d: 2 * 24 * 64 * 64 states.
    // Positive entries are plies to a safe winning promotion (minimax);
    // zero is a draw. This is a conversion distance, NOT distance to mate.
    constexpr int STATE_COUNT = 2 * 24 * 64 * 64;
    constexpr uint16_t INVALID = 65535;

    inline int Distance(int a, int b)
    {
        return std::max(std::abs(a % 8 - b % 8), std::abs(a / 8 - b / 8));
    }
    inline bool PawnAttacks(int pawn, int square)
    {
        return square / 8 == pawn / 8 + 1 && std::abs(square % 8 - pawn % 8) == 1;
    }
    inline int Index(int wk, int bk, int pawn, int turn)
    {
        return (((pawn / 8 - 1) * 4 + pawn % 8) * 2 + turn) * 4096 + wk * 64 + bk;
    }

    inline bool SliderAttacks(int from, int to, int blocker, bool queen)
    {
        int df = to % 8 - from % 8, dr = to / 8 - from / 8;
        if (df != 0 && dr != 0 && !(queen && std::abs(df) == std::abs(dr))) return false;
        int sf = (df > 0) - (df < 0), sr = (dr > 0) - (dr < 0);
        if (!sf && !sr) return false;
        for (int sq = from + sf + 8 * sr; sq != to; sq += sf + 8 * sr)
            if (sq == blocker) return false;
        return true;
    }

    inline bool PromotionWins(int wk, int bk, int square)
    {
        if (Distance(bk, square) == 1 && Distance(wk, square) > 1) return false;
        // Test both majors: queen promotion can stalemate where rook wins.
        for (bool queen : {true, false})
        {
            if (SliderAttacks(square, bk, wk, queen)) return true;
            for (int dr = -1; dr <= 1; ++dr)
                for (int df = -1; df <= 1; ++df)
                {
                    int f = bk % 8 + df, r = bk / 8 + dr;
                    if ((!df && !dr) || !IsValidCoordinate(f, r)) continue;
                    int to = r * 8 + f;
                    if (Distance(wk, to) > 1 && to != square &&
                        !SliderAttacks(square, to, wk, queen)) return true;
                }
        }
        return false;
    }

    class Table
    {
    public:
        Table() : m_distance(STATE_COUNT, 0)
        {
            for (int p = 0; p < 24; ++p)
                for (int turn = 0; turn < 2; ++turn)
                    for (int wk = 0; wk < 64; ++wk)
                        for (int bk = 0; bk < 64; ++bk)
                        {
                            int pawn = (p / 4 + 1) * 8 + p % 4;
                            if (wk == pawn || bk == pawn || Distance(wk, bk) <= 1 ||
                                (turn == 0 && PawnAttacks(pawn, bk)))
                                m_distance[Index(wk, bk, pawn, turn)] = INVALID;
                        }

            // Synchronous layers: all winning successors at layer n have
            // conversion distance <= n. Unresolved cycles remain draws.
            std::vector<uint16_t> next = m_distance;
            for (uint16_t layer = 1; ; ++layer)
            {
                bool changed = false;
                for (int p = 0; p < 24; ++p)
                    for (int turn = 0; turn < 2; ++turn)
                        for (int wk = 0; wk < 64; ++wk)
                            for (int bk = 0; bk < 64; ++bk)
                            {
                                int pawn = (p / 4 + 1) * 8 + p % 4;
                                int index = Index(wk, bk, pawn, turn);
                                if (m_distance[index] != 0) continue;
                                if (Winning(wk, bk, pawn, turn))
                                {
                                    next[index] = layer;
                                    changed = true;
                                }
                            }
                if (!changed) break;
                m_distance.swap(next);
                next = m_distance;
            }
        }

        uint16_t Probe(int wk, int bk, int pawn, int turn) const
        {
            return m_distance[Index(wk, bk, pawn, turn)];
        }

    private:
        bool Winning(int wk, int bk, int pawn, int turn) const
        {
            bool any = false, all = true, hasMove = false;
            auto visit = [&](uint16_t result)
            {
                if (result == INVALID) return;
                hasMove = true;
                any |= result != 0;
                all &= result != 0;
            };
            int king = turn == 0 ? wk : bk;
            for (int dr = -1; dr <= 1; ++dr)
                for (int df = -1; df <= 1; ++df)
                {
                    int f = king % 8 + df, r = king / 8 + dr;
                    if ((!df && !dr) || !IsValidCoordinate(f, r)) continue;
                    int to = r * 8 + f;
                    if (turn == 0)
                    {
                        if (to != pawn && Distance(to, bk) > 1)
                            visit(Probe(to, bk, pawn, 1));
                    }
                    else if (Distance(to, wk) > 1 && !PawnAttacks(pawn, to))
                    {
                        visit(to == pawn ? 0 : Probe(wk, to, pawn, 0));
                    }
                }
            if (turn == 0 && pawn + 8 != wk && pawn + 8 != bk)
            {
                if (pawn / 8 == 6)
                    visit(PromotionWins(wk, bk, pawn + 8) ? 1 : 0);
                else
                {
                    visit(Probe(wk, bk, pawn + 8, 1));
                    if (pawn / 8 == 1 && pawn + 16 != wk && pawn + 16 != bk)
                        visit(Probe(wk, bk, pawn + 16, 1));
                }
            }
            if (turn == 0) return any;
            return hasMove ? all : PawnAttacks(pawn, bk);
        }
        std::vector<uint16_t> m_distance;
    };

    inline const Table& GetTable()
    {
        static const Table table;
        return table;
    }

    // nullopt means the position is outside this table's domain.
    inline std::optional<int> Evaluate(const Board& board)
    {
        if (board.GetPieceList(PlayerColor::White).count +
            board.GetPieceList(PlayerColor::Black).count != 3) return std::nullopt;
        int pawn = -1;
        PlayerColor strong = PlayerColor::White;
        for (int ci = 0; ci < 2; ++ci)
            for (int i = 0; i < board.GetPieceList(static_cast<PlayerColor>(ci)).count; ++i)
            {
                int sq = board.GetPieceList(static_cast<PlayerColor>(ci)).squares[i];
                if (board.GetPieceAt(sq).GetType() == PieceType::Pawn)
                {
                    pawn = sq;
                    strong = static_cast<PlayerColor>(ci);
                }
            }
        if (pawn < 8 || pawn >= 56) return std::nullopt;
        int wk = board.GetKingSquare(strong);
        int bk = board.GetKingSquare(strong == PlayerColor::White ? PlayerColor::Black : PlayerColor::White);
        if (wk < 0 || bk < 0) return std::nullopt;
        if (strong == PlayerColor::Black) { wk ^= 56; bk ^= 56; pawn ^= 56; }
        if (pawn % 8 >= 4) { wk ^= 7; bk ^= 7; pawn ^= 7; }
        int turn = board.GetSideToMove() == strong ? 0 : 1;
        uint16_t distance = GetTable().Probe(wk, bk, pawn, turn);
        if (distance == INVALID) return std::nullopt;
        if (distance == 0) return 0;
        // Stay below a queen's value so converting to KQK remains attractive.
        int score = 700 - distance;
        return turn == 0 ? score : -score;
    }
}
