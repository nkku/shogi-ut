// 時計の実時間テスト: 秒読み表示、着手ごとの復元、持時間からの移行。
#include "../src/game.h"
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QThread>
#include <QTimer>

static int failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { qWarning() << "FAIL:" << what; ++failures; }
}
static void waitMs(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}
static int sq(const char* usi)
{
    int result;
    if (!shogi::Board::parseUsiCoord(usi, result)) qFatal("invalid test square");
    return result;
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    {
        Game g;
        g.newGameHuman(0, 3);
        check(g.inByomi(0) && g.clockMs(0) == 3000, "持時間0なら開始時から秒読み");
        int updates = 0;
        QObject::connect(&g, &Game::boardChanged, [&] { ++updates; });
        waitMs(1500);
        check(!g.over() && g.clockMs(0) > 0 && g.clockMs(0) < 2500, "秒読み残り時間が減る");
        check(g.clockText(0) != QStringLiteral("byo 0:03"), "秒読み表示が3秒から更新される");
        check(updates > 0, "秒表示の更新を画面に通知");
        waitMs(1900);
        check(g.over() && g.clockMs(0) == 0, "表示の残り時間を消費して時間切れ");
        check(g.resultText().contains(QStringLiteral("時間切れ")), "時間切れの結果表示");
    }
    {
        Game g;
        g.newGameHuman(0, 3);
        waitMs(1200);
        g.tapSquare(sq("7g")); g.tapSquare(sq("7f"));
        check(g.ply() == 1 && g.clockMs(0) == 3000, "着手後に先手の秒読み枠が戻る");
        waitMs(500);
        check(g.clockMs(0) == 3000 && g.clockMs(1) < 3000, "手番側だけを減らす");
        g.tapSquare(sq("3c")); g.tapSquare(sq("3d"));
        check(g.ply() == 2 && g.clockMs(1) == 3000, "後手の着手でも秒読み枠が戻る");
        waitMs(300);
        check(g.clockMs(0) < 3000 && g.clockMs(1) == 3000, "再び先手の秒読みが進む");
    }
    {
        Game g;
        g.newGameHuman(1, 2);
        // GUIの処理でタイマーが遅れた場合も、移行時に経過分を捨てない。
        QThread::msleep(1300);
        QCoreApplication::processEvents();
        check(g.inByomi(0) && g.clockMs(0) > 0 && g.clockMs(0) < 1900,
              "持時間を超えた経過分を秒読みから引く");
    }
    qInfo() << (failures ? "CLOCK FAILURES" : "ALL CLOCK TESTS PASSED") << failures;
    return failures ? 1 : 0;
}
