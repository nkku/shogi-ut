// 詰み見落とし局面のデバッグ: 当アプリの合法手replyを列挙
#include "../src/shogi/board.h"
#include <cstdio>
#include <string>
using namespace shogi;

int main()
{
    const char* sfens[] = {
        "1n5n1/2k2+N1g1/P3S1r1l/1p2pgp1p/5pPp1/p1pG+b2P1/1N6P/L1+b1gSR1L/SK5SL b P5p 121",
        "ln4bnb/1rp1P3G/1P1+P1sp2/p1GpkP2p/1NPPp1Pp1/7SP/G4g1KN/L3S1+rSL/9 b L3p 141",
    };
    for (const char* s : sfens) {
        Board b;
        if (!b.loadSfen(s)) { printf("LOAD FAIL %s\n", s); continue; }
        Turn t = b.turn();
        printf("== %s turn=%c\n", s, t == Sente ? 'b' : 'w');
        printf("inCheck=%d mated=%d\n", b.inCheck(t), b.mated(t));
        auto moves = b.legalMoves();
        printf("legal replies: %zu\n", moves.size());
        for (const auto& m : moves) {
            if (m.isDrop())
                printf("  drop %c*%s\n", "PLNSGBRK"[m.drop], b.usiCoord(m.to).c_str());
            else
                printf("  %s%s->%s\n", b.usiCoord(m.from).c_str(),
                       m.promote ? "+" : "", b.usiCoord(m.to).c_str());
        }
    }
    return 0;
}
