// ルールコアの単体テスト (builderコンテナ内でビルド: g++ -std=c++17 src/shogi/board.cpp tests/test_board.cpp)
#include "../src/shogi/board.h"
#include <cassert>
#include <cstdio>
#include <set>

using namespace shogi;

static int passed = 0;
static int failed = 0;
#define CHECK(cond) do { if (cond) { ++passed; } else { std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); ++failed; } } while (0)

int main() {
    // --- 初期局面 ---
    Board b;
    auto mv = b.legalMoves();
    CHECK(mv.size() == 30);  // 歩9 香2 銀4 金6 飛6 玉3
    CHECK(b.turn() == Sente);
    CHECK(b.sfen() == "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1");

    // --- USI座標 ---
    { int s; CHECK(Board::parseUsiCoord("7g", s) && s == idx(6, 2));
      CHECK(Board::parseUsiCoord("1a", s) && s == idx(0, 8));
      CHECK(Board::parseUsiCoord("9i", s) && s == idx(8, 0));
      CHECK(Board::usiCoord(idx(6, 2)) == "7g"); }

    // --- 7六歩 ---
    { Move m; CHECK(Board::parseUsiMove("7g7f", m));
      CHECK(!m.isDrop() && !m.promote);
      CHECK(b.isLegal(m));
      b.make(m);
      CHECK(b.turn() == Gote);
      CHECK(b.sfen() == "lnsgkgsnl/1r5b1/ppppppppp/9/9/2P6/PP1PPPPPP/1B5R1/LNSGKGSNL w - 2");
      b.unmake();
      CHECK(b.turn() == Sente);
      CHECK(b.sfen() == Board().sfen()); }

    // --- 成り表記・打ち表記(P*形式)のパース/出力 ---
    { Move m; CHECK(Board::parseUsiMove("2b1a+", m) && m.promote);
      CHECK(m.from == idx(1, 7));
      CHECK(m.to == idx(0, 8));
      Move d; CHECK(Board::parseUsiMove("P*5e", d) && d.isDrop() && d.to == idx(4, 4) && d.drop == Pawn);
      Move r; CHECK(Board::parseUsiMove("R*3c", r) && r.isDrop() && r.drop == Rook && r.to == idx(2, 6));
      // 出力も P* 形式になっている
      CHECK(b.usiMove(d) == "P*5e");
      CHECK(b.usiMove(m) == "2b1a+"); }

    // --- 二歩 (先手(hand:P)が5筋に歩を持てない) ---
    { Board b2;
      CHECK(b2.loadSfen("9/9/9/9/9/9/1PPPPPPPP/9/LNSGKGSNL b P 3"));
      CHECK(b2.turn() == Sente);
      CHECK(b2.hand(Sente, Pawn) == 1);
      auto drops = b2.legalDropsAt(idx(5, 4)); // ５五: 同筋に先手歩あり
      for (Kind k : drops) CHECK(k != Pawn);
      auto drops2 = b2.legalDropsAt(idx(5, 0)); // ９筋は打ち可
      bool pawnOk = false;
      for (Kind k : drops2) if (k == Pawn) pawnOk = true;
      CHECK(pawnOk); }

    // --- 詰み (後手玉が詰んでいる、後手番) ---
    { Board b3;
      CHECK(b3.loadSfen("8k/8G/8L/9/9/9/9/9/7R1 w - 3"));
      CHECK(b3.inCheck(Gote));
      CHECK(b3.mated(Gote));
      CHECK(b3.legalMoves().empty()); }

    // --- 打ち歩詰め禁止 (先手番、歩打ち一二は詰み→禁止。金打ちなら可) ---
    { Board b4;
      CHECK(b4.loadSfen("8k/9/8S/9/9/9/9/9/7R1 b P 3"));
      bool forbidden = true;
      for (const auto& x : b4.legalMoves())
          if (x.isDrop() && x.to == idx(1, 8) && x.drop == Pawn) forbidden = false;
      CHECK(forbidden);
      Board b5;
      CHECK(b5.loadSfen("8k/9/8S/9/9/9/9/9/7R1 b G 3"));
      bool goldMate = false;
      for (const auto& x : b5.legalMoves())
          if (x.isDrop() && x.to == idx(1, 8) && x.drop == Gold) goldMate = true;
      CHECK(goldMate); }

    // --- 成り強制/任意 (桂) ---
    { Board b7;
      CHECK(b7.loadSfen("9/9/9/9/9/3N7/9/9/9 b - 3"));
      bool hasNonPromote = false, hasPromote = false;
      for (const auto& x : b7.legalMovesFrom(idx(5, 3)))
          if (x.to == idx(3, 2)) { if (x.promote) hasPromote = true; else hasNonPromote = true; }
      CHECK(hasNonPromote && !hasPromote); // 成りゾーン外 → 不成のみ
      Board b8;
      CHECK(b8.loadSfen("9/9/3N7/9/9/9/9/9/9 b - 3"));
      bool forced = false;
      for (const auto& x : b8.legalMovesFrom(idx(2, 3)))
          if (x.to == idx(0, 2) || x.to == idx(0, 4)) { CHECK(x.promote); forced = true; }
      CHECK(forced); } // 最終段到達 → 成り必須

    // --- 竜(飛+斜め1)の動き ---
    { Board b9;
      CHECK(b9.loadSfen("9/9/9/9/4+R4/9/9/9/6k2 b - 3"));
      std::set<int> dests;
      for (const auto& x : b9.legalMovesFrom(idx(4, 4))) dests.insert(x.to);
      CHECK(dests.count(idx(3, 3)) && dests.count(idx(5, 5)));  // 斜め1マス
      CHECK(dests.size() > 4); }

    // --- SFEN往復(持駒: 大文字=先手/小文字=後手, 枚数前置) ---
    { Board b11;
      CHECK(b11.loadSfen("9/9/9/3b5/4p4/9/PP1PPPPPP/9/LN1GK1SNL b r2P2p 20"));
      CHECK(b11.hand(Sente, Pawn) == 2);
      CHECK(b11.hand(Gote, Pawn) == 2);
      CHECK(b11.hand(Gote, Rook) == 1);
      CHECK(b11.hand(Sente, Rook) == 0);
      CHECK(b11.turn() == Sente);
      // 出力→再読込で保持
      std::string s = b11.sfen();
      CHECK(s.rfind("9/9/9/3b5/4p4/9/PP1PPPPPP/9/LN1GK1SNL b ", 0) == 0);
      Board b11b;
      CHECK(b11b.loadSfen(s));
      CHECK(b11b.hand(Sente, Pawn) == 2 && b11b.hand(Gote, Pawn) == 2 && b11b.hand(Gote, Rook) == 1);
      CHECK(b11b.sfen() == s); }

    // --- 千日手(同一局面4回) ---
    { Board b12;
      CHECK(b12.loadSfen("4k4/9/9/9/9/9/9/4G4/4K4 b - 1"));
      Move g1;  g1.from = idx(7, 4); g1.to = idx(6, 4);   // 金上
      Move g2;  g2.from = idx(6, 4); g2.to = idx(7, 4);   // 金戻り
      Move k1;  k1.from = idx(0, 4); k1.to = idx(1, 4);   // 後手玉
      Move k2;  k2.from = idx(1, 4); k2.to = idx(0, 4);
      CHECK(b12.repetitionCount() == 1);
      for (int i = 0; i < 3; ++i) {   // 初局面1回+3周期で4回
          CHECK(b12.isLegal(g1)); b12.make(g1);
          CHECK(b12.isLegal(k1)); b12.make(k1);
          CHECK(b12.isLegal(g2)); b12.make(g2);
          CHECK(b12.isLegal(k2)); b12.make(k2);
      }
      CHECK(b12.repetitionCount() == 4);
      b12.unmake();
      CHECK(b12.repetitionCount() == 3);
      b12.make(k2);
      CHECK(b12.repetitionCount() == 4);
      b12.clearHistory();
      CHECK(b12.repetitionCount() == 1);
      CHECK(b12.perpetualCheckCauser() == -1); }

    std::printf("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
