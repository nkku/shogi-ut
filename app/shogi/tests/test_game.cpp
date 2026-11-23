// Game + UsiEngine + 実エンジンの統合テスト(QCoreApplication、画面なし)。
// 人間側スクリプト指し手で数手進め、エンジンが合法手を返すことを確認する。
#include "../src/game.h"
#include <QCoreApplication>
#include <QTimer>
#include <QDebug>
#include <functional>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { qCritical("usage: test_game <usi-engine-path> [engineId]"); return 2; }
    const QString engineId = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

    Game game;
    int failures = 0;

    QObject::connect(&game, &Game::ended, [&] {
        qInfo() << "END:" << game.resultText();
    });

    game.newGameEngine(/*humanSente*/ true, 30, 1, argv[1], 2, 256, engineId);

    // 人間側のスクリプト: (from,to) USI座標
    QStringList humanMoves = { "7g7f", "2g2f", "8g8f", "6g6f", "7f7e", "2f2e" };
    int cursor = 0;

    auto parseSq = [](const QString& usi) {
        int file = usi[0].toLatin1() - '0';
        int row = usi[1].toLatin1() - 'a';
        return row * 9 + (9 - file);
    };

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        if (game.over()) {
            timer.stop();
            app.quit();
            return;
        }
        if (game.ply() > 24) {
            qInfo("ply limit reached OK");
            game.resign();
            return;
        }
        if (game.turn() == 0 && !game.thinking() && !game.promoteAsked()) {
            if (cursor >= humanMoves.size()) {
                game.resign();
                return;
            }
            const QString mv = humanMoves[cursor++];
            const int before = game.ply();
            game.tapSquare(parseSq(mv.left(2)));
            game.tapSquare(parseSq(mv.mid(2)));
            if (game.promoteAsked()) game.answerPromote(false);
            if (game.ply() == before) {
                qWarning() << "scripted move not applied:" << mv << "sfen:" << game.debugSfen();
                ++failures;
                game.resign();
            }
        }
    });
    timer.start(250);

    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        qCritical("TIMEOUT: ply=%d thinking=%d sfen=%s", game.ply(), int(game.thinking()),
                  qPrintable(game.debugSfen()));
        ++failures;
        app.quit();
    });
    watchdog.start(120000);

    app.exec();
    qInfo() << "moves played:" << game.ply() << "failures:" << failures;
    return failures == 0 && game.ply() >= 8 ? 0 : 1;
}
