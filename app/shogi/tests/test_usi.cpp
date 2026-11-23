// 自身をUSIエンジンの代役として起動し、実際の標準入出力を検証する。
#include "../src/game.h"
#include <QCoreApplication>
#include <QDebug>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <functional>
#include <fstream>
#include <iostream>
#include <string>

static int fakeEngine(const char* logPath)
{
    std::ofstream log(logPath, std::ios::app);
    std::string line, position;
    while (std::getline(std::cin, line)) {
        log << line << std::endl;
        if (line == "usi") std::cout << "id name Regression engine\nusiok" << std::endl;
        else if (line == "isready") std::cout << "readyok" << std::endl;
        else if (line.rfind("position ", 0) == 0) position = line;
        else if (line.rfind("go ", 0) == 0)
            std::cout << "bestmove " << (position.find("7g7f") == std::string::npos ? "7g7f" : "2g2f") << std::endl;
        else if (line == "quit") return 0;
    }
    return 0;
}
static int failures = 0;
static void check(bool ok, const char* what)
{
    if (!ok) { qWarning() << "FAIL:" << what; ++failures; }
}
static bool waitFor(const std::function<bool()>& predicate)
{
    if (predicate()) return true;
    QEventLoop loop;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&] { if (predicate()) loop.quit(); });
    poll.start(10);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    return predicate();
}
static QString readLog(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QString();
    return QString::fromUtf8(file.readAll());
}
int main(int argc, char** argv)
{
    if (argc == 3 && std::string(argv[1]) == "--fake-engine") return fakeEngine(argv[2]);
    const QByteArray logEnv = qgetenv("SHOGI_TEST_USI_LOG");
    if (!logEnv.isEmpty()) return fakeEngine(logEnv.constData());
    QCoreApplication app(argc, argv);
    QTemporaryDir dir;
    check(dir.isValid(), "USIログ用ディレクトリ");
    const QString standard = QString::fromStdString(shogi::Board().sfen());
    {
        const QString logPath = dir.filePath("engine.log");
        UsiEngine engine;
        int bestMoves = 0;
        QObject::connect(&engine, &UsiEngine::bestMove, [&] { ++bestMoves; });
        engine.start(QCoreApplication::applicationFilePath(), { "--fake-engine", logPath });
        check(waitFor([&] { return engine.ready(); }), "USI準備完了");
        engine.newGame();
        engine.think(standard, {}, 1234, 5678, 3000);
        check(waitFor([&] { return bestMoves == 1; }), "通常探索の応答");
        engine.goPonder(standard, {}, 9012, 3456, 5000);
        check(waitFor([&] { return bestMoves == 2; }), "先読み探索の応答");
        engine.quit();
        const QString log = readLog(logPath);
        check(log.contains("go btime 1234 wtime 5678 byoyomi 3000\n"), "通常探索でbyoyomiを送信");
        check(log.contains("go ponder btime 9012 wtime 3456 byoyomi 5000\n"), "先読みでもbyoyomiを送信");
    }
    {
        const QString logPath = dir.filePath("game.log");
        qputenv("SHOGI_TEST_USI_LOG", logPath.toUtf8());
        Game g;
        g.newGameEngine(false, 0, 3, QCoreApplication::applicationFilePath(), 1, 16);
        qunsetenv("SHOGI_TEST_USI_LOG");
        check(waitFor([&] { return g.ply() == 1 || g.over(); }), "秒読みからCPUが着手");
        check(g.ply() == 1 && !g.over(), "CPUの合法手を適用");
        check(readLog(logPath).contains("go btime 0 wtime 0 byoyomi 3000\n"),
              "秒読みの枠を基本持時間に重複して含めない");
        g.resign();
    }
    qInfo() << (failures ? "USI FAILURES" : "ALL USI TESTS PASSED") << failures;
    return failures ? 1 : 0;
}
