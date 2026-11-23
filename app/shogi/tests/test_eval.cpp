// 評価値/学習モードのテスト:
//  (1) parseUsiInfoLine のユニット検証
//  (2) 実エンジン統合: 学習モードでヒント・評価曲線・差分メッセージが出るか
#include "../src/game.h"
#include "../src/engine/usiengine.h"
#include <QCoreApplication>
#include <QTimer>
#include <QDebug>

using namespace shogi;

static int failures = 0;
static void check(bool ok, const QString& what)
{
    if (!ok) { qWarning().noquote() << "FAIL:" << what; ++failures; }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { qCritical("usage: test_eval <usi-engine-path> [engineId]"); return 2; }
    const QString engineId = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

    // --- (1) info パーサ ---
    {
        UsiInfo a = parseUsiInfoLine(
            "info depth 21 seldepth 28 multipv 1 score cp 156 nodes 12345 time 100 pv 7g7f 3c3d");
        check(a.ok && a.depth == 21 && a.multipv == 1 && a.cp == 156 && a.matePly == 0
              && a.pvFirst == QLatin1String("7g7f"), "info cp multipv");
        UsiInfo b = parseUsiInfoLine("info depth 9 multipv 2 score mate -4 nodes 1 time 1 pv 5a6b");
        check(b.ok && b.multipv == 2 && b.matePly == -4 && b.pvFirst == QLatin1String("5a6b"),
              "info mate multipv2");
        UsiInfo c = parseUsiInfoLine("info string pv test");
        check(!c.ok, "info string ignored");
        UsiInfo d = parseUsiInfoLine("info depth 12 score cp -300 lowerbound pv 2g2f 8h3c");
        check(d.ok && d.cp == -300 && d.multipv == 1 && d.pvFirst == QLatin1String("2g2f"),
              "info without multipv");
    }

    // --- (2) 学習モード統合 (人間先手) ---
    Game game;
    QObject::connect(&game, &Game::ended, [&] { qInfo() << "END:" << game.resultText(); });
    game.newGameEngine(/*humanSente*/ true, 30, 1, argv[1], 1, 128, engineId, /*study*/ true);
    check(game.studyMode(), "studyMode set");

    QStringList humanMoves = { "7g7f", "2g2f", "8g8f", "6g6f" };
    int cursor = 0;
    bool sawHint = false, sawSecond = false, sawHighlight = false, sawDiff = false;

    auto parseSq = [](const QString& usi) {
        int file = usi[0].toLatin1() - '0';
        int row = usi[1].toLatin1() - 'a';
        return row * 9 + (9 - file);
    };

    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, [&] {
        if (game.over()) { timer.stop(); app.quit(); return; }
        if (!game.hint2Text().isEmpty()) sawSecond = true;
        if (!game.evalMessage().isEmpty()) sawDiff = true;
        if (game.ply() >= 9) { game.resign(); return; }
        if (game.turn() == 0 && !game.promoteAsked()) {
            // 初回は検討(1.8s)でヒントが出るのを待つ。学習検討中でも指せること自体を確認
            if (!sawHint) {
                if (game.hint1Text().isEmpty()) return;
                sawHint = true;
                game.showHint(0);
                sawHighlight = game.hintTo() != NONE;
                qInfo() << "hint1:" << game.hint1Text() << "hint2:" << game.hint2Text();
            }
            if (cursor >= humanMoves.size()) { game.resign(); return; }
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
    timer.start(200);

    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        qCritical("TIMEOUT: ply=%d thinking=%d hint1=%s", game.ply(), int(game.thinking()),
                  qPrintable(game.hint1Text()));
        ++failures;
        game.resign();
        timer.stop();
        app.quit();
    });
    watchdog.start(150000);

    app.exec();

    check(sawHint, "最善手ヒント表示");
    check(sawHighlight, "showHintで盤面強調");
    check(sawSecond, "次善手ヒント表示");
    const int n = game.evalCurve().size();
    check(n >= 2, QStringLiteral("評価曲線ポイント数=%1").arg(n));
    check(sawDiff, "評価差分メッセージ");
    qInfo() << "evalMessage last:" << game.evalMessage();
    if (n > 0)
        qInfo() << "curve[0]:" << game.evalCurve().first().toMap();
    qInfo() << (failures ? "FAILURES!" : "ALL EVAL TESTS PASSED") << failures;
    return failures ? 1 : 0;
}
