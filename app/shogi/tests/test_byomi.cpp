// 秒読み中のエンジン着手再現テスト: 持時間をすぐ使い切らせて btime=0+byoyomi 状態で
// エンジンが毎回指すか (パス=指し手なしでスタックしないか) を検証する
#include "../src/game.h"
#include <QCoreApplication>
#include <QTimer>
#include <QDebug>

using namespace shogi;

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { qCritical("usage: test_byomi <usi-engine> [engineId]"); return 2; }
    const QString engineId = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

    Game game;
    // 人間: 指しまくって持時間を使い切る / 秒読み2秒でエンジンにも厳しい秒読みを強制
    // (MinimumThinkingTime既定2000msだと秒読み超過になるので300msへの変更も効く)
    game.newGameEngine(/*humanSente*/ true, /*mainSec*/ 6, /*byomiSec*/ 2,
                       argv[1], /*threads*/ 1, /*hashMb*/ 64, engineId, /*study*/ false);

    QStringList humanMoves = { "7g7f", "2g2f", "8g8f", "6g6f", "5g5f",
                               "7h2b", "3c2b", "6i7g", "7a6b", "2f2e",
                               "3a2b", "1g1f", "9a9b", "1f1e", "9c9d" };
    int cursor = 0, lastSeenEngineMove = 0, stallMs = 0;
    int engineMoves = 0;
    bool failed = false;

    auto parseSq = [](const QString& usi) {
        int file = usi[0].toLatin1() - '0';
        int row = usi[1].toLatin1() - 'a';
        return row * 9 + (9 - file);
    };

    QObject::connect(&game, &Game::boardChanged, [&] {
        int em = 0;
        for (int i = 0; i < game.ply(); ++i) { (void)i; }
        // エンジン着手数は ply と手番から素朴に数える (人間先手なので奇数ply=エンジン着手)
        for (int i = 0; i < game.ply(); ++i) em += (i % 2);
        if (em > lastSeenEngineMove) { lastSeenEngineMove = em; stallMs = 0; ++engineMoves; }
    });

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        if (game.over()) { timer.stop(); app.quit(); return; }
        if (game.turn() == 1) {           // エンジン番
            stallMs += 200;
            if (stallMs > 25000) {
                qWarning() << "ENGINE STALLED at ply" << game.ply()
                           << "thinking:" << game.thinking()
                           << "sfen:" << game.debugSfen();
                failed = true;
                game.resign();
            }
            return;
        }
        if (game.promoteAsked()) { game.answerPromote(false); return; }
        if (cursor >= humanMoves.size()) { game.resign(); return; }
        const QString mv = humanMoves[cursor++];
        const int before = game.ply();
        game.tapSquare(parseSq(mv.left(2)));
        game.tapSquare(parseSq(mv.mid(2)));
        if (game.promoteAsked()) game.answerPromote(false);
        if (game.ply() == before) {
            qWarning() << "human move rejected:" << mv;
            game.resign();
        }
    });
    timer.start(200);

    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        qCritical("WATCHDOG TIMEOUT engineMoves=%d", engineMoves);
        failed = true;
        game.resign();
        timer.stop();
        app.quit();
    });
    watchdog.start(120000);

    app.exec();
    qInfo() << "engine moves:" << engineMoves << "result:" << game.resultText();
    if (failed || engineMoves < 5) {
        qWarning() << "FAIL";
        return 1;
    }
    qInfo() << "BYOMI OK";
    return 0;
}
