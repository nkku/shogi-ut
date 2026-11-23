#pragma once
// 将棋データ型定義 (参考: ShogiMaru MIT License © 2022-2024 Shogimaru project)
#include <cstdint>

namespace shogi {

enum Turn : uint8_t { Sente = 0, Gote = 1 };

enum Kind : uint8_t {
    Pawn = 0, Lance = 1, Knight = 2, Silver = 3,
    Gold = 4, Bishop = 5, Rook = 6, King = 7, NoKind = 15,
};

constexpr int NONE = 0xFF;   // empty square
constexpr int BOARD_N = 9;
constexpr int SQUARES = 81;

constexpr int idx(int row, int col) { return row * 9 + col; }
inline int rowOf(int s) { return s / 9; }
inline int colOf(int s) { return s % 9; }
inline bool onBoard(int r, int c) { return r >= 0 && r < 9 && c >= 0 && c < 9; }

// code = owner<<4 | promoted<<3 | kind
namespace pc {
constexpr int make(Turn owner, Kind k, bool promoted = false) {
    return (owner << 4) | (promoted << 3) | k;
}
constexpr Turn owner(int c) { return static_cast<Turn>((c >> 4) & 1); }
constexpr Kind kind(int c) { return static_cast<Kind>(c & 0x7); }
constexpr bool promoted(int c) { return (c >> 3) & 1; }
constexpr bool isValid(int c) { return c != NONE && (c & ~0x1F) == 0; }
constexpr int unpromote(int c) { return c & ~0x8; }
inline int promote(int c) { return c | 0x8; }  // caller must ensure promotable
constexpr Turn other(Turn t) { return static_cast<Turn>(t ^ 1); }
inline bool canPromote(int c) {
    Kind k = kind(c);
    return !promoted(c) && k != Gold && k != King;
}
// goldshogi: と金・成銀・成桂・成香 are gold movements
constexpr bool movesAsGold(int c) {
    Kind k = kind(c);
    return k == Gold || (promoted(c) && (k == Pawn || k == Silver || k == Knight || k == Lance));
}
} // namespace pc

struct Move {
    int from = NONE;       // NONE => drop
    int to = NONE;
    bool promote = false;
    Kind drop = NoKind;
    bool isDrop() const { return from == NONE; }
    bool isNull() const { return to == NONE; }
};

} // namespace shogi
