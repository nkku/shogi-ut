// KIF/SFEN 変換と Game::importKifu の統合テスト
#include "../src/game.h"
#include "../src/kifu/kifu.h"
#include "../src/shogi/board.h"
#include <QCoreApplication>
#include <QFile>
#include <QDebug>
#include <QTemporaryDir>

using namespace shogi;

static int failures = 0;
static void check(bool ok, const QString& what)
{
    if (!ok) { qWarning().noquote() << "FAIL:" << what; ++failures; }
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // --- 出力 + 往復 (KIF) ---
    Board start; start.setStandard();
    Board b; b.setStandard();
    QStringList usi;
    auto play = [&](const char* u) {
        Move m;
        if (!Board::parseUsiMove(u, m) || !b.isLegal(m)) {
            qCritical("setup move illegal: %s", u); exit(2);
        }
        usi << QString::fromStdString(b.usiMove(m));
        b.make(m);
    };
    play("7g7f"); play("3c3d"); play("8h2b+"); play("3a2b"); play("2g2f"); play("B*8h");

    const QString kif = kifu::exportKif(start, usi, QStringLiteral("あなた"),
                                        QStringLiteral("CPU"), QStringLiteral("中断"));
    check(kif.contains(QStringLiteral("手合割：平手")), "KIFヘッダ");
    check(kif.contains(QStringLiteral("７六歩(77)")), "KIF 7g7f -> ７六歩(77)");
    check(kif.contains(QStringLiteral("２二角成(88)")), "KIF 成り 8h2b+ -> ２二角成(88)");
    check(kif.contains(QStringLiteral("同銀(31)")), "KIF 同 3a2b -> 同銀(31)");
    check(kif.contains(QStringLiteral("８八角打(00)")), "KIF 打ち B*8h -> ８八角打(00)");

    kifu::Parsed back = kifu::importKif(kif);
    check(back.ok, QStringLiteral("KIF再読込 ok (%1)").arg(back.error));
    check(back.usi == usi, QStringLiteral("KIF往復一致 got=%1").arg(back.usi.join(',')));

    // --- SFEN 1行形式 ---
    const QString startSfen = QString::fromLatin1(
        "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1");
    const QString line = kifu::exportSfenLine(startSfen, usi);
    kifu::Parsed sBack = kifu::importSfenLine("sfen " + line.trimmed());
    check(sBack.ok && sBack.usi == usi, "SFEN lines 往復");
    kifu::Parsed sRaw = kifu::importSfenLine(line);
    check(sRaw.ok && sRaw.usi.size() == usi.size(), "SFEN lines (sfen接頭辞なし)");

    // --- from不明「同」無括弧 -> "?to+駒" 表現 ---
    kifu::Parsed noFrom = kifu::importKif(QStringLiteral(
        "手合割：平手\n手数----\n  1 ７六歩(77)\n  2 ３四歩(33)\n  3 ２六歩(27)\n"
        "  4 ４四角(22)\n  5 ７七角\n"));
    check(noFrom.ok && noFrom.usi.size() == 5, QStringLiteral("無括弧KIF (%1)").arg(noFrom.error));
    check(noFrom.usi.value(4) == QStringLiteral("?7gB"), "from不明 = ?7gB");

    // --- Game::importKifu 統合 (無括弧手を合法手から解決) ---
    QFile f(QStringLiteral("/tmp/test_imp.kif"));
    f.open(QIODevice::WriteOnly | QIODevice::Truncate);
    f.write(QStringLiteral(
        "手合割：平手\n先手：テスト\n後手：テスト\n手数----指手---------消費時間--\n"
        "  1 ７六歩(77)  0:00 0:00\n"
        "  2 ３四歩(33)  0:00 0:00\n"
        "  3 ２六歩(27)  0:00 0:00\n"
        "  4 ４四角(22)  0:00 0:00\n"
        "  5 ７七角  0:00 0:00\n"
        "  6 ---\n").toUtf8());   // 6手目はわざと壊す(読めない行でそこで打ち切り)
    f.close();

    Game game;
    bool imported = game.importKifu(QStringLiteral("/tmp/test_imp.kif"), 600, 30);
    check(imported, QStringLiteral("Game::importKifu ok (%1)").arg(game.kifuMessage()));
    check(game.ply() == 5, QStringLiteral("import後 ply=%1").arg(game.ply()));
    check(game.engineColor() == -1, "import後は二面打ち");
    check(game.debugSfen().contains(QStringLiteral("b")), "import後手番=sente to move");

    // --- 往復: 自分で保存したKIFを自分で読み込める ---
    Game g2;
    {
        // 人手スクリプトで数手 (二面打ち)
        auto tap = [&](int from, int to) { g2.tapSquare(from); g2.tapSquare(to); };
        auto sq = [](const QString& u) {
            return (u[1].toLatin1() - 'a') * 9 + (9 - (u[0].toLatin1() - '0'));
        };
        g2.newGameHuman(600, 30);
        tap(sq("7g"), sq("7f"));
        tap(sq("3c"), sq("3d"));
        tap(sq("8h"), sq("2b"));
        if (g2.promoteAsked()) g2.answerPromote(true);
        tap(sq("3a"), sq("2b"));
    }
    check(g2.ply() == 4, QStringLiteral("g2 ply=%1").arg(g2.ply()));
    // saveKif は Downloads に書く (テストコンテナではHOME以下)。返り値パスから直接import
    const QString saved = g2.saveKif();
    check(!saved.isEmpty(), QStringLiteral("saveKif (%1)").arg(g2.kifuMessage()));
    Game g3;
    if (!saved.isEmpty()) {
        bool ok = g3.importKifu(saved, 600, 30);
        check(ok, QStringLiteral("往復import (%1)").arg(g3.kifuMessage()));
        check(g3.ply() == 4, QStringLiteral("往復 ply=%1").arg(g3.ply()));
        check(g3.debugSfen() == g2.debugSfen(), "往復で局面一致");
    }

    // --- 成り・不成を保持したまま Game まで往復 (成駒を取る前の局面で比較) ---
    QTemporaryDir cases;
    check(cases.isValid(), "棋譜テスト用ディレクトリ");
    auto importText = [&](Game& g, const QString& name, const QString& text) {
        const QString path = cases.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
        file.write(text.toUtf8());
        file.close();
        return g.importKifu(path, 600, 30);
    };
    for (bool promote : { false, true }) {
        const QStringList moves = { "7g7f", "3c3d", promote ? "8h2b+" : "8h2b" };
        Board expected;
        for (const auto& u : moves) { Move m; Board::parseUsiMove(u.toStdString(), m); expected.make(m); }
        for (bool asKif : { false, true }) {
            Game g;
            const QString text = asKif ? kifu::exportKif(start, moves, "S", "G", "")
                                      : kifu::exportSfenLine(startSfen, moves);
            check(importText(g, asKif ? "promote.kif" : "promote.sfen", text), "成り/不成棋譜を読込");
            check(g.debugSfen() == QString::fromStdString(expected.sfen()), "成り/不成の局面が完全一致");
        }
    }

    // --- 同じ升に複数種類を打てても、指定した持駒を選ぶ ---
    const QString dropStart = "4k4/9/9/9/9/9/9/9/4K4 b PGR 1";
    for (const QString& u : { QString("P*4e"), QString("G*4e"), QString("R*4e") }) {
        Board expected;
        check(expected.loadSfen(dropStart.toStdString()), "打ちテスト初局面");
        Move m; Board::parseUsiMove(u.toStdString(), m);
        check(expected.isLegal(m), "打ちテストは合法手");
        expected.make(m);
        Game g;
        check(importText(g, "drop.sfen", kifu::exportSfenLine(dropStart, {u})), "複数持駒から指定した駒を打つ");
        check(g.debugSfen() == QString::fromStdString(expected.sfen()), "打った駒と残り持駒が一致");
    }

    // --- 不正な駒種や成り指定を、別の合法手に置き換えて受理しない ---
    {
        Game g;
        check(!importText(g, "invalid-drop.sfen", "4k4/9/9/9/9/9/9/9/4K4 b P 1 moves G*4e"),
              "持っていない金の代わりに歩を打たない");
        check(!importText(g, "invalid-promote.sfen", kifu::exportSfenLine(startSfen, {"7g7f+"})),
              "成れない場所の成りを不成に補正しない");
        check(!importText(g, "missing-promote.sfen", "k8/4P4/9/9/9/9/9/9/4K4 b - 1 moves 5b5a"),
              "必須の成りが欠けた手を勝手に成らせない");
    }

    qInfo() << (failures == 0 ? "ALL KIFU TESTS PASSED" : "FAILURES!") << failures;
    return failures == 0 ? 0 : 1;
}
