// 千日手系の终局判定テスト: 連続王手の千日手(反則) / 通常千日手 / 简化入玉判定・10点法
#include "../src/game.h"
#include "../src/shogi/board.h"
#include <QCoreApplication>
#include <QDebug>

using namespace shogi;

static int failures = 0;
static void check(bool ok, const QString& what)
{
    if (!ok) { qWarning().noquote() << "FAIL:" << what; ++failures; }
}

static auto sqOf = [](const QString& u) {
    return (u[1].toLatin1() - 'a') * 9 + (9 - (u[0].toLatin1() - '0'));
};

static void play(Board& b, const char* usi)
{
    Move m;
    if (!Board::parseUsiMove(usi, m) || !b.isLegal(m)) {
        qCritical("illegal setup move: %s", usi);
        exit(2);
    }
    b.make(m);
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // --- 連続王手の千日手: 飛車が2i↔1iで王手を繰り返し、後手玉が1a↔2a ---
    {
        Board b;
        check(b.loadSfen("7k1/9/9/9/9/9/9/9/4K3R b - 1"), "perpetual SFEN");
        const char* mv[] = { "1i2i", "2a1a", "2i1i", "1a2a", "1i2i", "2a1a", "2i1i",
                             "1a2a", "1i2i", "2a1a", "2i1i", "1a2a", "1i2i" };
        for (int i = 0; i < 11; ++i) play(b, mv[i]);
        check(b.repetitionCount() == 3, "11手目は同一局面3回");
        check(b.perpetualCheckCauser() == -1, "4回未満なら連続王手の反則なし");
        play(b, mv[11]);   // 12手目: 初局面を含め4回目、王手を解除した状態
        check(b.repetitionCount() == 4, "12手目で同一局面4回");
        check(!b.inCheck(b.turn()), "王手解除後の局面で成立");
        check(b.perpetualCheckCauser() == int(Sente), "王手側(先手)が作り手=反則");
    }

    // --- 通常千日手: 飛車の王手の合間に金待機(無王手)が混在 -> 引き分け ---
    //  周期(8手): R1i2i(王手) K2a1a / R2i1i(王手) K1a2a / G9h8h(待機) K2a2b / G8h9h K2b2a
    //  前半4手と8手目で初局面に戻るため、12手目が4回目。
    //  窓に金待機(無王手)が含まれるので引き分け。
    {
        Board b;
        check(b.loadSfen("7k1/9/9/9/9/9/9/G8/4K3R b - 1"), "draw SFEN");
        const char* cyc[] = { "1i2i", "2a1a", "2i1i", "1a2a", "9h8h", "2a2b", "8h9h", "2b2a" };
        for (int i = 0; i < 12; ++i) play(b, cyc[i % 8]);
        check(b.repetitionCount() == 4, "同一局面4回");
        check(b.perpetualCheckCauser() == -1, "王手継続でなければ引き分け");
    }

    // --- 初局面を含む通常千日手: 3周期(12手)で成立 ---
    {
        Board b;
        check(b.loadSfen("4k4/9/9/9/9/9/9/4G4/4K4 b - 1"), "cycle SFEN");
        Move g1; g1.from = idx(7, 4); g1.to = idx(6, 4);
        Move k1; k1.from = idx(0, 4); k1.to = idx(1, 4);
        Move g2; g2.from = idx(6, 4); g2.to = idx(7, 4);
        Move k2; k2.from = idx(1, 4); k2.to = idx(0, 4);
        for (int i = 0; i < 3; ++i) { b.make(g1); b.make(k1); b.make(g2); b.make(k2); }
        check(b.repetitionCount() == 4, "12手目=初局面4回目");
        check(b.perpetualCheckCauser() == -1, "王手なしなので引き分け");
    }

    // --- 開始時から王手されている局面でも連続王手側を判定 ---
    {
        Board b;
        check(b.loadSfen("7k1/9/9/9/9/9/9/9/4K2R1 w - 1"), "checked start SFEN");
        const char* cyc[] = { "2a1a", "2i1i", "1a2a", "1i2i" };
        for (int i = 0; i < 12; ++i) play(b, cyc[i % 4]);
        check(b.inCheck(b.turn()) && b.repetitionCount() == 4, "王手状態で4回目");
        check(b.perpetualCheckCauser() == int(Sente), "王手状態の4回目も先手の反則");
        b.unmake();
        check(b.perpetualCheckCauser() == -1, "待ったで千日手未成立に戻る");
        play(b, cyc[3]);
        check(b.perpetualCheckCauser() == int(Sente), "指し直して連続王手を再判定");
        b.clearHistory();
        check(b.repetitionCount() == 1 && b.perpetualCheckCauser() == -1, "履歴消去後は未成立");
    }

    // --- 後手が王手を続けた場合 ---
    {
        Board b;
        check(b.loadSfen("r3k4/9/9/9/9/9/9/9/1K7 w - 1"), "gote perpetual SFEN");
        const char* cyc[] = { "9a8a", "8i9i", "8a9a", "9i8i" };
        for (int i = 0; i < 12; ++i) play(b, cyc[i % 4]);
        check(b.repetitionCount() == 4 && b.perpetualCheckCauser() == int(Gote), "後手の連続王手反則");
    }

    // --- Game統合: 平手の開始局面が4回現れた12手目で終局 ---
    {
        Game g;
        g.newGameHuman(600, 30);
        const char* cyc[] = { "5i5h", "5a5b", "5h5i", "5b5a" };
        for (int i = 0; i < 12; ++i) {
            check(!g.over(), "4回目より前は継続");
            const QString u(cyc[i % 4]);
            g.tapSquare(sqOf(u.left(2)));
            g.tapSquare(sqOf(u.mid(2)));
        }
        check(g.over() && g.ply() == 12 && g.resultText() == QStringLiteral("千日手"),
              "開始局面を含めた4回目で通常千日手");
    }

    // --- Game統合: 連続王手 -> 反則負け表示 ---
    {
        Game g;
        g.newGameHuman(600, 30);
        check(g.debugSetSfen("7k1/9/9/9/9/9/9/9/4K3R b - 1"), "game perpetual sfen");
        const char* mv[] = { "1i2i", "2a1a", "2i1i", "1a2a", "1i2i", "2a1a", "2i1i",
                             "1a2a", "1i2i", "2a1a", "2i1i", "1a2a", "1i2i" };
        for (const char* u : mv) {
            if (g.over()) break;
            QString s(u);
            g.tapSquare(sqOf(s.left(2)));
            g.tapSquare(sqOf(s.mid(2)));
            if (g.promoteAsked()) g.answerPromote(false);
        }
        check(g.over(), "連続王手で终局");
        check(g.ply() == 12, "王手解除後の12手目で終局");
        check(g.resultText().contains(QString::fromUtf8("連続王手")),
              QStringLiteral("結果文言: %1").arg(g.resultText()));
        check(g.resultText().contains(QString::fromUtf8("後手の勝ち")), "王手側(先手)の負け");
    }

    // --- Game統合: 简化入玉判定 (先手10点/後手0点, 両玉侵入済み) ---
    {
        Board pb;
        check(pb.loadSfen("PPPP1PPPP/PP2K4/9/9/9/9/9/9/3k5 b - 1"), "nyugyoku board");
        check(pb.points(Sente) == 10 && pb.points(Gote) == 0,
              QStringLiteral("点数 %1-%2").arg(pb.points(Sente)).arg(pb.points(Gote)));
        check(pb.bothKingsInEnemyZone(), "両玉相手陣");
        Game g;
        g.newGameHuman(600, 30);
        check(g.debugSetSfen("PPPP1PPPP/PP2K4/9/9/9/9/9/9/3k5 b - 1"), "nyugyoku sfen");
        g.tapSquare(sqOf("5b"));
        g.tapSquare(sqOf("5c"));
        if (g.promoteAsked()) g.answerPromote(false);
        check(g.over(), "入玉で终局");
        check(g.resultText().contains(QString::fromUtf8("入玉勝ち")),
              QStringLiteral("結果文言: %1").arg(g.resultText()));
    }

    // --- Game統合: 持将棋 (双方<10点で両玉侵入) ---
    {
        Game g;
        g.newGameHuman(600, 30);
        check(g.debugSetSfen("9/7K1/9/9/9/9/9/3kg4/9 w - 1"), "jishogi sfen");
        g.tapSquare(sqOf("5h"));
        g.tapSquare(sqOf("5i"));
        check(g.over(), "持将棋で终局");
        check(g.resultText().contains(QString::fromUtf8("持将棋")),
              QStringLiteral("結果文言: %1").arg(g.resultText()));
    }

    qInfo() << (failures == 0 ? "ALL RULES TESTS PASSED" : "FAILURES!") << failures;
    return failures == 0 ? 0 : 1;
}
