// ランダム対局から「王手がかかった局面」をSFEN+判定付きで吐き出す(fuzz_check.py でエンジンと照合)
#include "../src/shogi/board.h"
#include <cstdio>
#include <random>
#include <string>

using namespace shogi;

int main(int argc, char** argv)
{
    unsigned seed = argc > 1 ? std::stoul(argv[1]) : 42;
    int games = argc > 2 ? std::stoul(argv[2]) : 300;
    std::mt19937 rng(seed);

    for (int g = 0; g < games; ++g) {
        Board b;
        b.setStandard();
        for (int ply = 0; ply < 140; ++ply) {
            auto moves = b.legalMoves();
            if (moves.empty()) break;
            b.make(moves[rng() % moves.size()]);
            Turn nx = b.turn();
            if (b.inCheck(nx))
                printf("%s %c\n", b.sfen().c_str(), b.mated(nx) ? 'M' : 'N');
            if (b.mated(nx) || b.repetitionCount() >= 4) break;
        }
    }
    return 0;
}
