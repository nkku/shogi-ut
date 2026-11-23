// 成りフローの実測(エンジン不要・画面なし)。QCoreApplication で動く。
// 角の成り(馬)・取ると不成に戻る・歩の不成選択と強制成り(と)を人間操作で検証する。
#include "../src/game.h"
#include <QCoreApplication>
#include <QDebug>

static int sq(const QString& usi) {
    int file = usi[0].toLatin1() - '0';
    int row = usi[1].toLatin1() - 'a';
    return row * 9 + (9 - file);
}

static int fails = 0;
static void check(bool cond, const QString& what)
{
    if (!cond) qWarning() << "FAIL:" << what;
    else qInfo() << "ok:" << what;
}

// from->to をタップ。成り確認が出たら promoteAnswer(-1=出ないはず 0=不成 1=成る)で応じる
static void play(Game& game, const QString& from, const QString& to, int promoteAnswer)
{
    int before = game.ply();
    game.tapSquare(sq(from));
    game.tapSquare(sq(to));
    if (game.promoteAsked()) {
        if (promoteAnswer < 0) qWarning() << "unexpected promote ask for" << from << to;
        game.answerPromote(promoteAnswer == 1);
    } else if (promoteAnswer >= 0) {
        qWarning() << "promote dialog NOT asked for" << from << to;
        ++fails;
    }
    check(game.ply() == before + 1, QStringLiteral("%1%2 applied").arg(from, to));
    if (game.ply() != before + 1) {
        ++fails;
        qWarning() << "  pos:" << game.debugSfen() << "turn=" << game.turn()
                   << "piece at" << from << "=" << game.pieceText(sq(from))
                   << "piece at" << to << "=" << game.pieceText(sq(to));
    }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    Game game;
    game.newGameHuman(600, 0);

    // --- 角の成り: 8h-3c+ (成/不成の選択 → 成る) ---
    play(game, "7g", "7f", -1);
    play(game, "1c", "1d", -1);
    play(game, "8h", "3c", 1);
    check(game.pieceText(sq("3c")) == QString::fromUtf8("馬"), "3c shows UMA(馬)");
    check(game.debugSfen().contains("+B"), "sfen has +B");

    // 馬で角を取りに行く(3c→4d→2b)。成ったまま移動すること+再成り選択が出ないことの回帰テスト
    play(game, "4a", "4b", -1);   // 馬(3c)が4b空けて5a玉に王手→金が上がって合阻断ち
    play(game, "3c", "4d", -1);
    check(game.pieceText(sq("4d")) == QString::fromUtf8("馬"), "horse stays UMA after move");
    play(game, "9c", "9d", -1);
    play(game, "4d", "2b", -1);   // 後手角を capture。ゾーン内だが成り選択肢は出てはいけない
    check(game.pieceText(sq("2b")) == QString::fromUtf8("馬"), "2b shows UMA after capture");

    // 銀で馬を取る → 持駒は不成の角に戻る
    play(game, "3a", "2b", -1);
    check(game.pieceText(sq("2b")) == QString::fromUtf8("銀"), "2b silver after capture");
    check(game.handCount(1, 5) == 1, "gote hand has 1 unpromoted BISHOP");

    // --- 歩での成敗選択 → 最終手番で強制成り ---
    play(game, "2g", "2f", -1);
    play(game, "9d", "9e", -1);
    play(game, "2f", "2e", -1);
    play(game, "9e", "9f", -1);
    play(game, "2e", "2d", -1);   // 正面取成り(ゾーン外なので選択肢なし)
    play(game, "1a", "1b", -1);   // 後手香の待ち手
    play(game, "2d", "2c", 0);    // ゾーン进入: dialog 出現 → 不成
    check(game.pieceText(sq("2c")) == QString::fromUtf8("歩"), "2c still FU(歩) after decline");
    play(game, "1b", "1c", -1);
    play(game, "2c", "2b", 0);    // 不成(銀を正面から取る)
    play(game, "9a", "9b", -1);   // 香の待ち手(9筋の歩は既に成り越している)
    play(game, "2b", "2a", -1);   // 最終手番: 強制成り(dialogなしで即「と」)
    check(game.pieceText(sq("2a")) == QString::fromUtf8("と"), "2a shows TO(と)");
    check(game.debugSfen().contains("+P"), "sfen has +P");

    qInfo() << (fails == 0 ? "ALL PASS" : "FAILURES") << fails;

    // --- 詰み終局の統合テスト: 一手詰めをタップ → ended + 「詰み」 ---
    {
        Game g;
        bool endedFired = false;
        g.newGameHuman(600, 0);   // 先に開始(newGameHuman自身がリセット用endedを出す)
        QObject::connect(&g, &Game::ended, [&]() { endedFired = true; });
        // 後手玉1a、先手金(G)1c・飛(R)1i。1c→1b で詰め。※SFENは9筋先頭なので 8k/8G 表記
        check(g.debugSetSfen("8k/9/8G/9/9/9/9/9/7KR b - 1"), "load mate-in-1 sfen");
        check(!g.over(), "not over after setup");
        g.tapSquare(sq("1c"));
        check(g.squareTarget(sq("1b")), "1b is target");
        g.tapSquare(sq("1b"));
        qInfo() << "DBG after tap1b: sfen=" << g.debugSfen() << "over=" << g.over()
                << "promoteAsked=" << g.promoteAsked() << "ply=" << g.ply();
        check(g.over(), "game over after mate");
        check(endedFired, "ended() emitted");
        check(g.resultText().contains(QString::fromUtf8("詰み")), "result says TSUMI");
        qInfo() << "result:" << g.resultText();
    }

    // --- 千日手統合テスト: 香の往復+玉の往復で同一局面4回 ---
    {
        Game g;
        bool endedFired = false;
        g.newGameHuman(600, 0);
        QObject::connect(&g, &Game::ended, [&]() { endedFired = true; });
        // 先手: 玉1i、後手: 玉5d。先手玉1i↔1h、後手玉5d↔5eで往復(同一局面4回=千日手)
        check(g.debugSetSfen("9/9/9/4k4/9/9/9/9/8K b - 1"), "load repetition sfen");
        for (int round = 0; round < 6 && !g.over(); ++round) {
            g.tapSquare(sq("1i")); g.tapSquare(sq("1h"));
            if (g.over()) break;
            g.tapSquare(sq("5d")); g.tapSquare(sq("5e"));
            if (g.over()) break;
            g.tapSquare(sq("1h")); g.tapSquare(sq("1i"));
            if (g.over()) break;
            g.tapSquare(sq("5e")); g.tapSquare(sq("5d"));
        }
        qInfo() << "DBG rep: ply=" << g.ply() << "sfen=" << g.debugSfen()
                << "promoteAsked=" << g.promoteAsked();
        check(g.over(), "sennichite ends game");
        check(endedFired, "ended() emitted for repetition");
        check(g.resultText().contains(QString::fromUtf8("千日手")), "result says SENNICHITE");
        qInfo() << "rep result:" << g.resultText();
    }
    qInfo() << (fails == 0 ? "FINAL ALL PASS" : "FINAL FAILURES") << fails;
    return fails;
}
