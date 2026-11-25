// 評価値グラフ再生成 + 閲覧からの分岐 (branchFromView) の統合テスト。
// KIF取り込み → 再生成完了 → canReplay=false → その手目へ → ここから指す → 着手可能。
#include "../src/game.h"
#include "../src/kifu/kifu.h"
#include "../src/shogi/board.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include <QDebug>

using namespace shogi;

static int failures = 0;
static void check(bool ok, const QString& what)
{
    if (!ok) { qWarning().noquote() << "FAIL:" << what; ++failures; }
    else qInfo().noquote() << "ok:" << what;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 2) { qCritical("usage: test_replay <usi-engine-path> [engineId]"); return 2; }
    const QString engineId = argc > 2 ? QString::fromLocal8Bit(argv[2]) : QString();

    // --- 6手のKIFを作る ---
    Board start; start.setStandard();
    Board b; b.setStandard();
    QStringList usi;
    auto play = [&](const char* u) {
        Move m;
        if (!Board::parseUsiMove(u, m) || !b.isLegal(m)) { qCritical("setup illegal: %s", u); exit(2); }
        usi << QString::fromStdString(b.usiMove(m));
        b.make(m);
    };
    play("7g7f"); play("3c3d"); play("8h2b+"); play("3a2b"); play("2g2f"); play("B*8h");

    const QString kif = kifu::exportKif(start, usi, QStringLiteral("A"), QStringLiteral("B"),
                                        QStringLiteral("中断"));
    QTemporaryDir dir;
    const QString path = dir.filePath("t.kif");
    { QFile f(path); f.open(QIODevice::WriteOnly); f.write(kif.toUtf8()); }

    Game game;
    game.newGameHuman(600, 0, true, QString::fromLocal8Bit(argv[1]), engineId);
    auto parseSq = [](const QString& usi2) {
        int file = usi2[0].toLatin1() - '0';
        int row = usi2[1].toLatin1() - 'a';
        return row * 9 + (9 - file);
    };
    check(game.importKifu(path, 600, 0), "importKifu");
    check(game.ply() == 6, QStringLiteral("ply==6 (got %1)").arg(game.ply()));
    check(game.canReplay(), "取り込み直後は canReplay=true");

    QObject::connect(&game, &Game::ended, [&] {
        if (game.over()) qWarning().noquote() << "unexpected END:" << game.resultText();
    });

    // --- エンジン起動を待って再生成開始 → 開始 (progress>0) → 完了 (progress=") ---
    int stage = 0, waited = 0;
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&] {
        waited += 300;
        auto* eng = game.engine();
        const bool ready = eng && eng->property("ready").toBool();
        if (stage == 0) {
            if (ready) { game.startReplay(); stage = 1; }   // 検討走行中でも startReplay が stop→pending で開始する
        } else if (stage == 1) {
            if (!game.replayProgress().isEmpty()) stage = 2;
        } else if (stage == 2) {
            if (game.replayProgress().isEmpty()) { stage = 3; app.quit(); }
        }
    });
    poll.start(300);

    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, [&] {
        auto* eng = game.engine();
        qCritical("TIMEOUT: stage=%d progress=%s ready=%d thinking=%d", stage,
                  qPrintable(game.replayProgress()),
                  int(eng && eng->property("ready").toBool()),
                  int(eng && eng->property("thinking").toBool()));
        ++failures;
        app.quit();
    });
    watchdog.start(180000);

    app.exec();
    qInfo() << "exec done: stage=" << stage << "progress=" << game.replayProgress()
            << "failures=" << failures;
    if (stage != 3) return 1;
    poll.stop(); watchdog.stop();

    check(!game.canReplay(), "再生成完了後は canReplay=false");
    check(game.replayProgress().isEmpty(), "再生成完了で progress 消える");
    check(game.evalCurve().size() == 6, QStringLiteral("曲線6点 (got %1)").arg(game.evalCurve().size()));

    // --- その手目へ → 最新へ復帰 → 3手目へ再移動 → 分岐 ---
    check(game.gotoPly(3), "gotoPly(3)");
    check(game.viewPly() == 3, "viewPly==3");
    // 閲覧中は入力が着地しない (盤は3手目局面なので 8h8b 相当の着手は非法だが、
    // 合法手でも ply が進むはずがない)
    game.tapSquare(parseSq("7g"));
    game.tapSquare(parseSq("7f"));
    check(game.ply() == 6, "閲覧中は着手不能");
    check(game.gotoPly(-1), "gotoPly(-1) 復帰");
    check(game.viewPly() == -1 && game.ply() == 6, "復帰後 viewPly=-1 ply=6");
    check(game.gotoPly(3), "gotoPly(3) 再移動");

    check(game.branchFromView(), "branchFromView");
    check(game.viewPly() == -1 && game.ply() == 3, "分岐後 ply==3");
    check(!game.over(), "分岐後は対局中");

    // 4手目 (3a2b 銀で角.take) を人間が指せること
    game.tapSquare(parseSq("3a"));
    game.tapSquare(parseSq("2b"));
    if (game.promoteAsked()) game.answerPromote(false);
    check(game.ply() == 4, QStringLiteral("分岐後に着手できる (ply=%1)").arg(game.ply()));
    check(game.canReplay(), "分岐で削られた先は again 再生成対象");

    // --- 二面打ちからCPU対局へ切り替え分岐 (エンジン後手) ---
    check(game.gotoPly(3), "gotoPly(3) (CPU切替用)");
    check(game.branchFromView(1), "branchFromView(1)=エンジン後手");
    check(game.engineColor() == 1, "分岐後 engineColor==1");
    {   // エンジンがその局面から思考を開始することを確認
        QEventLoop loop;
        QTimer t;
        int waited = 0;
        QObject::connect(&t, &QTimer::timeout, [&] {
            waited += 200;
            if (game.engine()->property("thinking").toBool() || waited > 8000) loop.quit();
        });
        t.start(200);
        loop.exec();
        t.stop();
        check(game.engine()->property("thinking").toBool(), "CPU対局へ切替えてエンジン思考中");
    }
    // 分岐局面でエンジンが実際に着手すること (検討→対局goの引き継ぎが壊れると永久に無手指)
    {
        QEventLoop loop;
        QTimer t;
        int waited = 0;
        QObject::connect(&t, &QTimer::timeout, [&] {
            waited += 200;
            if (game.ply() > 3 || waited > 150000) loop.quit();
        });
        t.start(200);
        loop.exec();
        t.stop();
        check(game.ply() == 4, QStringLiteral("分岐後にCPUが着手 (ply=%1)").arg(game.ply()));
        check(!game.over(), "CPU着手後も対局中");
    }

    qInfo() << "replay test done, failures:" << failures;
    return failures == 0 ? 0 : 1;
}
