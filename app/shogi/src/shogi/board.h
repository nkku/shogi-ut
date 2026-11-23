#pragma once
// 将棋ルールコア（Qt非依存）。参考: ShogiMaru (MIT) の searchMovablePeace/isCheck 相当。
#include "types.h"
#include <array>
#include <string>
#include <vector>

namespace shogi {

using Hands = std::array<std::array<int, 7>, 2>;  // [turn][kind]

namespace detail {
Turn handTurnFromChar(char ch);
char handChar(Turn t, Kind k);
}

class Board {
public:
    Board();

    // --- 局面 ---
    void setStandard();
    bool loadSfen(const std::string& sfen);
    std::string sfen() const;
    std::string posKey() const;            // 千日手判定用(盤面+持駒+手番)

    int at(int s) const { return square_[s]; }
    bool empty(int s) const { return square_[s] == NONE; }
    int hand(Turn t, Kind k) const { return hand_[t][k]; }
    int handTotal(Turn t) const;
    Turn turn() const { return turn_; }
    int moveNumber() const { return moveNumber_; }
    int kingSquare(Turn t) const;

    // --- 利き・合法手 ---
    bool isAttacked(int sq, Turn by) const;
    bool inCheck(Turn t) const;
    std::vector<Move> legalMoves();
    std::vector<Move> legalMovesFor(Turn t);
    std::vector<Move> legalMovesFrom(int from);
    std::vector<Kind> legalDropsAt(int to);
    bool isLegal(const Move& m);
    bool hasAnyLegalMove(Turn t);
    bool mated(Turn t);

    // --- 実行 ---
    void make(const Move& m);
    void unmake();
    void clearHistory();
    int repetitionCount() const;           // 初局面を含む現局面の出現回数。4で千日手
    int perpetualCheckCauser() const;      // 連続王手の千日手なら反則側Turn、でなければ-1
    int points(Turn t) const;              // 简化入玉判定: 王以外の駒(盤上+持駒)を各1点
    bool bothKingsInEnemyZone() const;     // 両玉が相手陣3段に侵入済みか

    // --- USI文字列 ---
    static std::string usiCoord(int s);
    static bool parseUsiCoord(const std::string&, int& s);
    std::string usiMove(const Move& m) const;
    static bool parseUsiMove(const std::string&, Move&);
    static int sfenKind(char ch);  // -> Kind or NoKind

private:
    struct Snapshot {
        std::array<int, SQUARES> sq;
        Hands hand;
        Turn turn;
        int moveNumber;
    };

    std::array<int, SQUARES> square_;
    Hands hand_;
    Turn turn_ = Sente;
    int moveNumber_ = 1;
    std::vector<Snapshot> undoStack_;
    std::vector<std::string> posKeys_;
    std::vector<char> checkFlags_;  // posKeys_と並行: その局面で手番側が王手されているか(打った手が王手だったか)
};

// 純粋関数として外部テストからも使うコア
namespace detail {
bool isAttacked(const std::array<int, SQUARES>& sq, int target, Turn by);
void genPseudoFrom(const std::array<int, SQUARES>& sq, int from, std::vector<Move>& out);
void genPseudoDrops(const std::array<int, SQUARES>& sq, const Hands& hand,
                    Turn o, std::vector<Move>& out);
void applyMove(std::array<int, SQUARES>& sq, Hands& hand, const Move& m, Turn mover);
bool kingSafeAfter(const std::array<int, SQUARES>& sq, const Hands& hand,
                   const Move& m, Turn mover);
bool hasAnyKingSafeReply(const std::array<int, SQUARES>& sq, const Hands& hand, Turn t);
} // namespace detail

} // namespace shogi
