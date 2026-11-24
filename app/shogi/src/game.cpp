#include "game.h"
#include "kifu/kifu.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QDir>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>

using namespace shogi;

namespace {
const char* kStandardSfen = "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1";

QString turnName(int t) { return t == int(Sente) ? QStringLiteral("先手") : QStringLiteral("後手"); }
QString winText(int loserTurn) { return turnName(1 - loserTurn) + QStringLiteral("の勝ち"); }

QString pieceKanji(int code)
{
    if (code == NONE) return QString();
    Kind k = pc::kind(code);
    bool prom = pc::promoted(code);
    switch (k) {
    case Pawn:    return prom ? QStringLiteral("と") : QStringLiteral("歩");
    case Lance:   return prom ? QStringLiteral("杏") : QStringLiteral("香");
    case Knight:  return prom ? QStringLiteral("圭") : QStringLiteral("桂");
    case Silver:  return prom ? QStringLiteral("全") : QStringLiteral("銀");
    case Gold:    return QStringLiteral("金");
    case Bishop:  return prom ? QStringLiteral("馬") : QStringLiteral("角");
    case Rook:    return prom ? QStringLiteral("龍") : QStringLiteral("飛");
    case King:    return pc::owner(code) == Sente ? QStringLiteral("王") : QStringLiteral("玉");
    default: return QString();
    }
}

QString handKanji(Kind k)
{
    switch (k) {
    case Rook: return QStringLiteral("飛"); case Bishop: return QStringLiteral("角");
    case Gold: return QStringLiteral("金"); case Silver: return QStringLiteral("銀");
    case Knight: return QStringLiteral("桂"); case Lance: return QStringLiteral("香");
    case Pawn: return QStringLiteral("歩"); default: return QString();
    }
}
} // namespace

Game::Game(QObject* parent) : QObject(parent)
{
    connect(&engine_, &UsiEngine::readyChanged, this, &Game::onEngineReadyChanged);
    connect(&engine_, &UsiEngine::bestMove, this, &Game::onBestMove);
    connect(&engine_, &UsiEngine::resigned, this, &Game::onEngineResigned);
    connect(&engine_, &UsiEngine::won, this, [this] {
        endGame(QStringLiteral("相手が入玉勝ちを宣言"));
    });
    connect(&engine_, &UsiEngine::thinkingChanged, this, [this] {
        emit thinkingChanged();
        onSearchFinished();
        updateStatus();
    });
    connect(&engine_, &UsiEngine::evalInfo, this, &Game::onEvalInfo);
    connect(&engine_, &UsiEngine::engineNameChanged, this, &Game::engineNameChanged);
    connect(&tick_, &QTimer::timeout, this, &Game::onTick);

    startSfen_ = QString::fromLatin1(kStandardSfen);
    setupGame(600, 0);
    over_ = true; // 開始前は対局中扱いしない
}

// --- 対局開始 -------------------------------------------------------------

void Game::setupGame(int mainSec, int byomiSec)
{
    board_.setStandard();
    startSfen_ = QString::fromLatin1(kStandardSfen);
    usiMoves_.clear();
    selSq_ = NONE; selHand_ = -1;
    targets_.clear(); promotePair_.clear(); promoteAsked_ = false;
    lastFrom_ = lastTo_ = NONE;
    over_ = false; resultText_.clear();
    byomiMs_ = byomiSec * 1000;
    inByomi_[0] = inByomi_[1] = mainSec <= 0 && byomiMs_ > 0;
    clock_[0] = clock_[1] = inByomi_[0] ? byomiMs_ : qint64(mainSec) * 1000;
    lastTick_ = QDateTime::currentMSecsSinceEpoch();
    tick_.start(200);
    // 前局の検討探索が残っていたら止めてから評価状態を初期化する
    if (engine_.thinking()) {
        searchKind_ = SearchNone;
        engine_.stop();
    }
    resetEvalState();
    emit promoteChanged();
    emit selectionChanged();
}

void Game::newGameHuman(int mainSec, int byomiSec, bool study,
                        const QString& enginePath, const QString& engineId,
                        int threads, int hashMb)
{
    engineColor_ = -1;
    engineLabel_.clear();
    studyMode_ = study;
    setupGame(mainSec, byomiSec);
    if (study) {
        if (configureEngine(enginePath, engineId, threads, hashMb).isEmpty()) {
            studyMode_ = false;
            evalMessage_ = QStringLiteral("エンジンが見つからず学習モード off");
            emit evalMessageChanged();
        } else {
            // MultiPVは探索種別ごとにapplyMultiPvで切替 (対局=1/検討=2)。
            // here で2に焼き付けると対局探索までMultiPV=2になりCPUが弱くなる。
            engine_.newGame();
        }
    }
    emit modeChanged();
    emit flippedChanged();
    emit ended();
    emit boardChanged();
    emit studyChanged();
    updateStatus();
}

QString Game::configureEngine(const QString& enginePath, const QString& engineId,
                              int threads, int hashMb)
{
    const QString path = enginePathOrDefault(enginePath, engineId);
    if (path.isEmpty())
        return path;
    engine_.start(path);
    lastMultiPv_ = 1;   // 新プロセスはエンジン既定値 (MultiPV=1) から始まる
    engine_.setOption(QStringLiteral("Threads"), QString::number(qMax(1, threads)));
    engine_.setOption(QStringLiteral("USI_Hash"), QString::number(qMax(16, hashMb)));
    // やねうら王系の既定 MinimumThinkingTime=2000ms は短い秒読みで時間超過を招く
    // (30秒将棋で1手2秒未満の局面でも2秒考えて指し手が遅延)。GUI側で縮小する。
    engine_.setOption(QStringLiteral("MinimumThinkingTime"), QStringLiteral("300"));
    // NNUE評価ファイル同梱エンジン: <エンジンと同じディレクトリ>/<engineId>_eval/nn.bin
    // (この版のやねうら王は EvalFile ではなく EvalDir/nn.bin を参照する)
    if (engineId == QStringLiteral("suisho5") || engineId == QStringLiteral("tsuyou11")) {
        // 水匠5/つようら王11はいずれも大きな尺度で学習された評価ファイル。
        // 既定FV_SCALE=16だと評価が狂って枝刈りが劣化し大幅に弱くなる (推奨24)。
        // ※ eval_options.txt 経由でもエンジンが自動設定するが、GUI側でも明示する。
        engine_.setOption(QStringLiteral("FV_SCALE"), QStringLiteral("24"));
        const QString evalDir =
            QFileInfo(path).absoluteDir().absolutePath()
            + QStringLiteral("/") + engineId + QStringLiteral("_eval");
        if (QFileInfo::exists(evalDir + QStringLiteral("/nn.bin")))
            engine_.setOption(QStringLiteral("EvalDir"), evalDir);
    }
    return path;
}

void Game::newGameEngine(bool humanSente, int mainSec, int byomiSec,
                         const QString& enginePath, int threads, int hashMb,
                         const QString& engineId, bool study)
{
    engineColor_ = humanSente ? int(Gote) : int(Sente);
    studyMode_ = study;
    setupGame(mainSec, byomiSec);
    const QString path = configureEngine(enginePath, engineId, threads, hashMb);
    if (path.isEmpty()) {
        studyMode_ = false;
        endGame(QStringLiteral("USIエンジンが見つかりません。設定でパスを指定してください。"));
        return;
    }
    // 表示ラベル: 水匠5は探索部がやねうら王NNUEなので判別できる名前を付ける。
    // パス手動指定(=同梱engineId以外)ならエンジンの自己申告名を使う。
    if (!enginePath.isEmpty())
        engineLabel_.clear();
    else if (engineId == QStringLiteral("suisho5"))
        engineLabel_ = QStringLiteral("水匠5");
    else if (engineId == QStringLiteral("tsuyou11"))
        engineLabel_ = QStringLiteral("つようら王11mini");
    else
        engineLabel_ = QStringLiteral("やねうら王");
    // MultiPVは探索種別ごとの切替 (startThink/maybeStartAnalysisでapplyMultiPv)
    engine_.newGame();
    emit modeChanged();
    emit flippedChanged();
    emit ended();
    emit boardChanged();
    emit studyChanged();
    updateStatus();
}

QString Game::enginePathOrDefault(const QString& path, const QString& engineId) const
{
    QStringList candidates;
    if (!path.isEmpty()) candidates << path;
    const QString env = QProcessEnvironment::systemEnvironment().value(QStringLiteral("SHOGI_USI_ENGINE"));
    if (!env.isEmpty()) candidates << env;
    // アプリ同梱エンジン (clickableのインストール先構造に依存)
    // engines/ にはアーキ別バイナリ <engineId>_<uname -m> を同梱する。
    // QSysInfoの返す名前がQtの版で aarch64/arm64 と揺れるため両方試す
    const QString baseName = engineId.isEmpty() ? QStringLiteral("yaneuraou") : engineId;
    const QString base = QCoreApplication::applicationDirPath();
    const QString qtArch = QSysInfo::currentCpuArchitecture();
    QStringList arches;
    arches << qtArch;
    if (qtArch == QStringLiteral("arm64")) arches << QStringLiteral("aarch64");
    else if (qtArch == QStringLiteral("aarch64")) arches << QStringLiteral("arm64");
    arches << QString(); // 末尾: サフィックス無し (engines/<name>)
    for (const QString& a : arches) {
        const QString suf = a.isEmpty() ? QString() : QStringLiteral("_") + a;
        candidates << base + QStringLiteral("/engines/") + baseName + suf
                   << base + QStringLiteral("/../engines/") + baseName + suf;
    }
    for (const QString& c : candidates)
        if (QFileInfo::exists(c))
            return QFileInfo(c).absoluteFilePath();
    // 実行ファイル名だけの指定(PATH解決)は試みない: 失敗時の挙動が曖昧なため
    return QString();
}

// --- 棋譜の保存・読込 ---------------------------------------------------------
// 保存はアプリ専用データ領域 (content-hub が読める場所) にステージングし、
// content-hub の「書き出し先を選択」UI経由でユーザーの選びた場所に渡す。
// (直接 Downloads へ書くと apparmor の read/write_path が自動審査を通らない)

namespace {
QDir exportStagingDir()
{
    QDir d(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
    d.mkpath(QStringLiteral("exports"));
    d.cd(QStringLiteral("exports"));
    // 1週間より古いステージングファイルは掃除する
    for (const QFileInfo& fi : d.entryInfoList(QDir::Files))
        if (fi.lastModified().daysTo(QDateTime::currentDateTime()) > 7)
            QFile::remove(fi.absoluteFilePath());
    return d;
}
} // namespace

void Game::setKifuMessage(const QString& msg)
{
    kifuMessage_ = msg;
    emit kifuMessageChanged();
}

QString Game::saveKif()
{
    Board start;
    if (!start.loadSfen(startSfen_.toLatin1().constData())) {
        setKifuMessage(QStringLiteral("初局面を復元できませんでした"));
        return QString();
    }
    QString sName, gName;
    if (engineColor_ < 0) {
        sName = QStringLiteral("先手"); gName = QStringLiteral("後手");
    } else {
        const QString eng = engineLabel_.isEmpty() ? engine_.engineName() : engineLabel_;
        if (engineColor_ == int(Sente)) { sName = eng; gName = QStringLiteral("あなた"); }
        else { sName = QStringLiteral("あなた"); gName = eng; }
    }
    const QString endText = over_ ? resultText_ : QStringLiteral("中断");
    const QString text = kifu::exportKif(start, usiMoves_, sName, gName, endText);

    const QString path = exportStagingDir().absoluteFilePath(
        QStringLiteral("shogi-%1.kif")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setKifuMessage(QStringLiteral("書き込めません: ") + path);
        return QString();
    }
    QTextStream ts(&f);
    ts.setCodec("UTF-8");
    ts << text;
    f.close();
    setKifuMessage(QString());
    return path;
}

QString Game::saveSfen()
{
    const QString path = exportStagingDir().absoluteFilePath(
        QStringLiteral("shogi-%1.sfen")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss"))));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setKifuMessage(QStringLiteral("書き込めません: ") + path);
        return QString();
    }
    f.write(kifu::exportSfenLine(startSfen_, usiMoves_).toUtf8());
    f.close();
    setKifuMessage(QString());
    return path;
}

bool Game::importKifu(const QString& path, int mainSec, int byomiSec)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setKifuMessage(QStringLiteral("ファイルが開けません: ") + path);
        return false;
    }
    const QString text = QString::fromUtf8(f.readAll());
    f.close();

    kifu::Parsed p;
    if (text.contains(QStringLiteral("手合割")) || text.contains(QStringLiteral("手数")))
        p = kifu::importKif(text);
    else
        p = kifu::importSfenLine(text);
    if (!p.ok) {
        setKifuMessage(p.error);
        return false;
    }

    // 全手を再現して合法性を確認しながら正則USI列を作る
    Board b;
    if (!b.loadSfen(p.startSfen.toLatin1().constData())) {
        setKifuMessage(QStringLiteral("初局面を復元できません"));
        return false;
    }
    QStringList canon;
    int no = 0;
    for (const QString& usi : p.usi) {
        ++no;
        std::vector<Move> legal = b.legalMoves();
        Move found;
        if (usi.startsWith(QLatin1Char('?'))) {
            // "?1a+歩" = 着手元不明: 駒種+成り+目的地から合法手を特定
            const QString body = usi.mid(1);
            const QString to = body.left(2);
            const bool prom = body.size() >= 4 && body[2] == QLatin1Char('+');
            const char letter = body.at(prom ? 3 : 2).toLatin1();
            Kind k;
            if (!kifu::letterToKind(letter, k)) { setKifuMessage(QStringLiteral("不明な駒: ") + usi); return false; }
            int toSq;
            if (!Board::parseUsiCoord(to.toStdString(), toSq)) { setKifuMessage(QStringLiteral("不正な手: ") + usi); return false; }
            for (const Move& m : legal) {
                if (!m.isDrop() && m.to == toSq && m.promote == prom
                    && pc::kind(b.at(m.from)) == k) { found = m; break; }
            }
            if (found.isNull()) {
                setKifuMessage(QStringLiteral("第%1手を解釈できません: %2").arg(no).arg(usi));
                return false;
            }
        } else {
            Move m;
            if (!Board::parseUsiMove(usi.toStdString(), m)) {
                setKifuMessage(QStringLiteral("第%1手の形式が不正: %2").arg(no).arg(usi));
                return false;
            }
            for (const Move& m2 : legal) {
                if (m2.isDrop() != m.isDrop() || m2.to != m.to) continue;
                // 棋譜が指定した駒種・成りを保持し、別の合法手に置き換えない。
                if (m.isDrop() ? m2.drop == m.drop
                               : m2.from == m.from && m2.promote == m.promote) {
                    found = m2;
                    break;
                }
            }
            if (found.isNull()) {
                setKifuMessage(QStringLiteral("第%1手が不正: %2").arg(no).arg(usi));
                return false;
            }
        }
        canon << QString::fromStdString(b.usiMove(found));
        b.make(found);
    }

    // 二面打ちとして注入 (终は applyMove 側の詰み/千日手判定に委ねる)
    if (engine_.thinking()) engine_.stop();
    engineColor_ = -1;
    engineLabel_.clear();
    setupGame(mainSec, byomiSec);
    studySuspend_ = true;              // リプレイ中の検討開始を抑制
    startSfen_ = p.startSfen;
    board_.loadSfen(p.startSfen.toLatin1().constData());
    for (const QString& usi : canon) {
        Move m;
        if (!Board::parseUsiMove(usi.toStdString(), m) || !board_.isLegal(m))
            break;   // 再現不能手が出たらそこで打ち切り (上の検証を通っている通常は来ない)
        applyMove(m);
        if (over_) break;   // 詰み/千日手など终局を含む棋譜
    }
    studySuspend_ = false;
    emit ended();
    setKifuMessage(QStringLiteral("%1手 読み込みました").arg(usiMoves_.size()));
    maybeStartAnalysis();
    return true;
}

// --- 表示 ------------------------------------------------------------------

QString Game::pieceText(int sq) const { return pieceKanji(board_.at(sq)); }
bool Game::pieceIsSente(int sq) const
{
    int p = board_.at(sq);
    return p != NONE && pc::owner(p) == Sente;
}

QString Game::handText(int t) const
{
    QString s;
    for (Kind k : { Rook, Bishop, Gold, Silver, Knight, Lance, Pawn }) {
        int n = board_.hand(Turn(t), k);
        if (n <= 0) continue;
        if (!s.isEmpty()) s += QLatin1Char(' ');
        s += handKanji(k);
        if (n > 1) s += QString::number(n);
    }
    return s;
}

int Game::handCount(int t, int kind) const { return board_.hand(Turn(t), Kind(kind)); }

bool Game::handActive(int t, int kind) const
{
    return interactive() && int(board_.turn()) == t && board_.hand(Turn(t), Kind(kind)) > 0;
}

bool Game::squareActive(int sq) const
{
    if (!interactive()) return false;
    int p = board_.at(sq);
    return p != NONE && pc::owner(p) == board_.turn();
}

bool Game::squareSelected(int sq) const { return selSq_ == sq; }

bool Game::squareTarget(int sq) const
{
    for (const Move& m : targets_)
        if (m.to == sq) return true;
    return false;
}

int Game::kingInCheckSquare() const
{
    if (!board_.inCheck(board_.turn())) return -1;
    return board_.kingSquare(board_.turn());
}

int Game::clockMs(int t) const
{
    return int(qMax(qint64(0), clock_[t]));
}

QString Game::clockText(int t) const
{
    int ms = clockMs(t);
    int sec = (ms + 999) / 1000;
    QString s;
    if (sec >= 3600)   // 7時間級の持時間対応
        s = QStringLiteral("%1:%2:%3")
                .arg(sec / 3600)
                .arg((sec % 3600) / 60, 2, 10, QLatin1Char('0'))
                .arg(sec % 60, 2, 10, QLatin1Char('0'));
    else
        s = QStringLiteral("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QLatin1Char('0'));
    if (inByomi_[t]) s = QStringLiteral("byo ") + s;
    return s;
}

// --- 入力 ------------------------------------------------------------------

bool Game::interactive() const
{
    if (over_ || promoteAsked_) return false;
    // 学習検討中は人間入力を許可 (着手時にstop→対局goへ切り替える)
    if (engine_.thinking() && searchKind_ != SearchAnalysis) return false;
    return engineColor_ < 0 || int(board_.turn()) != engineColor_;
}

void Game::clearSelection()
{
    if (selSq_ != NONE || selHand_ >= 0 || !targets_.empty()) {
        selSq_ = NONE; selHand_ = -1;
        targets_.clear();
        emit selectionChanged();
    }
}

void Game::selectSquare(int sq)
{
    auto moves = board_.legalMovesFrom(sq);
    if (moves.empty()) { clearSelection(); return; }
    selSq_ = sq; selHand_ = -1;
    targets_ = std::move(moves);
    emit selectionChanged();
}

void Game::selectHand(int kind)
{
    std::vector<Move> drops;
    Turn t = board_.turn();
    for (int s = 0; s < SQUARES; ++s) {
        if (!board_.empty(s)) continue;
        auto kinds = board_.legalDropsAt(s);
        for (Kind k : kinds) {
            if (int(k) == kind) {
                Move m; m.to = s; m.drop = Kind(kind);
                drops.push_back(m);
                break;
            }
        }
    }
    if (drops.empty()) { clearSelection(); return; }
    selSq_ = NONE; selHand_ = kind;
    targets_ = std::move(drops);
    emit selectionChanged();
}

void Game::tapSquare(int sq)
{
    if (over_ || promoteAsked_) return;
    if (!interactive()) { clearSelection(); return; }

    if (selSq_ != NONE || selHand_ >= 0) {
        std::vector<Move> matches;
        for (const Move& m : targets_)
            if (m.to == sq) matches.push_back(m);
        if (matches.size() == 2) {           // 成/不成の選択
            promotePair_ = matches;
            promoteAsked_ = true;
            emit promoteChanged();
            return;
        }
        if (matches.size() == 1) { applyMove(matches.front()); return; }
        clearSelection();
    }
    int p = board_.at(sq);
    if (p != NONE && pc::owner(p) == board_.turn()) {
        if (selSq_ == sq) clearSelection();
        else selectSquare(sq);
    }
}

void Game::tapHand(int t, int kind)
{
    if (!handActive(t, kind)) { clearSelection(); return; }
    if (selHand_ == kind) { clearSelection(); return; }
    selectHand(kind);
}

void Game::answerPromote(bool yes)
{
    if (!promoteAsked_) return;
    for (const Move& m : promotePair_) {
        if (m.promote == yes) {
            promotePair_.clear();
            promoteAsked_ = false;
            emit promoteChanged();
            applyMove(m);
            return;
        }
    }
    promotePair_.clear();
    promoteAsked_ = false;
    emit promoteChanged();
    clearSelection();
}

void Game::cancelSelection() { clearSelection(); }

void Game::resign()
{
    if (over_) return;
    int loser = engineColor_ >= 0 ? 1 - engineColor_ : int(board_.turn());
    endGame(QStringLiteral("投了 — ") + winText(loser) );
}

bool Game::debugSetSfen(const QString& sfen)
{
    if (!board_.loadSfen(sfen.toStdString())) return false;
    clearSelection();
    if (engine_.thinking() && searchKind_ == SearchAnalysis) {
        searchKind_ = SearchNone;
        pendingThink_ = false;
        engine_.stop();
    }
    resetEvalState();
    over_ = false;
    usiMoves_.clear();
    lastFrom_ = lastTo_ = shogi::NONE;
    clock_[0] = clock_[1] = 0;
    emit boardChanged();
    emit flippedChanged();
    updateStatus();
    return true;
}

void Game::undo()
{
    if (over_ || usiMoves_.isEmpty()) return;
    if (engine_.thinking()) {
        if (searchKind_ == SearchAnalysis) {          // 検討中なら止めてから許す
            searchKind_ = SearchNone;
            pendingThink_ = false;
            engine_.stop();
        } else {
            return;
        }
    }
    board_.unmake();
    usiMoves_.removeLast();
    if (engineColor_ >= 0 && int(board_.turn()) == engineColor_ && !usiMoves_.isEmpty()) {
        board_.unmake();
        usiMoves_.removeLast();
    }
    lastFrom_ = lastTo_ = NONE;
    clearSelection();
    while (!curve_.isEmpty() && curve_.lastKey() > usiMoves_.size())
        curve_.remove(curve_.lastKey());
    analysisDonePly_ = -1;
    evalMessage_.clear();
    emit evalMessageChanged();
    emit evalCurveChanged();
    emit boardChanged();
    emit flippedChanged();
    emit lastMove(NONE, NONE);
    updateStatus();
    maybeStartAnalysis();
}

// --- 指し手実行 -------------------------------------------------------------

void Game::applyMove(const Move& m)
{
    Turn mover = board_.turn();
    const QString usi = QString::fromStdString(board_.usiMove(m));
    lastFrom_ = m.from; lastTo_ = m.to;
    board_.make(m);
    usiMoves_ << usi;

    if (inByomi_[int(mover)]) clock_[int(mover)] = byomiMs_;
    lastTick_ = QDateTime::currentMSecsSinceEpoch();

    clearSelection();
    clearHints();                     // 旧局面の検討手は失効
    emit boardChanged();
    emit flippedChanged();   // 二面打ちでは手番ごとに反転
    emit lastMove(m.from, m.to);

    Turn next = board_.turn();
    if (board_.mated(next)) {
        endGame(QStringLiteral("詰み — ") + winText(int(next)) );
        return;
    }
    if (board_.repetitionCount() >= 4) {
        const int causer = board_.perpetualCheckCauser();
        if (causer >= 0)
            endGame(QStringLiteral("連続王手の千日手 — ") + winText(causer));
        else
            endGame(QStringLiteral("千日手"));
        return;
    }
    // 简化入玉判定・10点法: 両玉が相手陣3段に侵入しているときのみ発火
    //  指し手側>=10点 かつ 相手<10点 → 入玉勝ち / 双方<10点 → 持将棋(引き分け)
    if (board_.bothKingsInEnemyZone()) {
        const int a = board_.points(mover);
        const int b = board_.points(pc::other(mover));
        if (a >= 10 && b < 10) {
            endGame(QStringLiteral("入玉勝ち — ") + winText(int(pc::other(mover)))
                    + QStringLiteral(" (%1-%2点)").arg(a).arg(b));
            return;
        }
        if (a < 10 && b < 10) {
            endGame(QStringLiteral("持将棋 (引き分け, %1-%2点)").arg(a).arg(b));
            return;
        }
    }
    if (engineColor_ >= 0 && int(board_.turn()) == engineColor_) {
        startThink();
    } else {
        updateStatus();
        maybeStartAnalysis();
    }
}

// MultiPV切替。やねうら王系は思考中のsetoptionを受け付けないため、
// go発行直前 (エンジンアイドル時) にしか呼ばない。変化なしなら送信しない。
void Game::applyMultiPv(int n)
{
    if (lastMultiPv_ == n) return;
    engine_.setOption(QStringLiteral("MultiPV"), QString::number(n));
    lastMultiPv_ = n;
}

void Game::startThink()
{
    if (engineColor_ < 0 || over_) return;
    if (int(board_.turn()) != engineColor_) return;
    if (!engine_.ready()) { updateStatus(); return; } // readyChangedで再試行
    if (engine_.thinking()) {
        if (searchKind_ == SearchGame) return;         // 対局探索は継続中
        // 検討中/状態不明: stopしてbestmove到着後にgoを延期発行 (stopせず放置すると
        // エンジンが永久に次のgoを受け付けない=サイレントパスになる)
        engine_.stop();
        pendingThink_ = true;
        return;
    }
    searchKind_ = SearchGame;
    searchPly_ = usiMoves_.size();
    searchTurn_ = int(board_.turn());
    lastEval_ = EvalRec();
    // 秒読みは byoyomi で別送するため、基本持時間に重ねて加算しない。
    int b = inByomi_[0] ? 0 : clockMs(0);
    int w = inByomi_[1] ? 0 : clockMs(1);
    applyMultiPv(1);   // 対局探索は常に最善手1本 (MultiPV=2は検討専用)
    engine_.think(startSfen_, usiMoves_, b, w, byomiMs_);
    updateStatus();
}

// 手番側が人間/二面打ちのとき学習モードの検討探索を開始する
void Game::maybeStartAnalysis()
{
    if (!studyMode_ || over_ || studySuspend_ || searchKind_ != SearchNone) return;
    if (!engine_.ready() || engine_.thinking()) return;
    if (engineColor_ >= 0 && int(board_.turn()) == engineColor_) return; // engin側は対局探索
    if (analysisDonePly_ == usiMoves_.size()) return;   // この局面の検討は収まった
    searchKind_ = SearchAnalysis;
    searchPly_ = usiMoves_.size();
    searchTurn_ = int(board_.turn());
    lastEval_ = EvalRec();
    clearHints();
    applyMultiPv(2);   // 検討は最善/次善の2本 (ヒント用)
    engine_.analyze(startSfen_, usiMoves_, 1800);   // FP5で手触りの良い固定検討時間
    updateStatus();
}

// --- 評価値グラフの再生成 (KIF/SFEN取り込み後は評価が未記録のため再検討する) ---

static int startTurnOfSfen(const QString& sfen)
{
    const QStringList f = sfen.split(QLatin1Char(' '));
    return (f.size() >= 3 && f.at(2) == QLatin1String("w")) ? int(Gote) : int(Sente);
}

void Game::setReplayProgress(const QString& s)
{
    if (replayProgress_ == s) return;
    replayProgress_ = s;
    emit replayProgressChanged();
}

void Game::startReplay()
{
    if (replayPly_ > 0) return;
    if (usiMoves_.size() < 2 || searchKind_ == SearchGame || !engine_.ready()) {
        pendingReplayStart_ = false;
        studySuspend_ = false;     // pending経路で立てていた旗を戻す
        return;
    }
    if (engine_.thinking()) {
        // 検討探索中なら停止完了 (onSearchFinished) から開始する
        studySuspend_ = true;
        pendingReplayStart_ = true;
        engine_.stop();
        return;
    }
    replayTarget_ = usiMoves_.size();
    studySuspend_ = true;
    stepReplay();
}

void Game::stopReplay()
{
    if (replayPly_ <= 0 && !pendingReplayStart_) return;
    pendingReplayStart_ = false;
    replayPly_ = 0;
    replayTarget_ = 0;
    studySuspend_ = false;
    setReplayProgress(QString());
    if (engine_.thinking()) engine_.stop();
}

void Game::stepReplay()
{
    int p = replayPly_ > 0 ? replayPly_ : 1;
    while (p <= replayTarget_ && curve_.contains(p)) ++p;   // 既存記録はスキップ
    if (p > replayTarget_) {                                // 完了
        replayPly_ = 0;
        replayTarget_ = 0;
        studySuspend_ = false;
        setReplayProgress(QString());
        maybeStartAnalysis();       // 対局継続中なら通常の検討に戻る
        return;
    }
    replayPly_ = p;
    setReplayProgress(QStringLiteral("%1/%2").arg(p).arg(replayTarget_));
    searchKind_ = SearchReplay;
    searchPly_ = p;
    searchTurn_ = (startTurnOfSfen(startSfen_) + p) % 2;    // 探索視点の手番側
    lastEval_ = EvalRec();
    applyMultiPv(1);
    engine_.analyze(startSfen_, usiMoves_.mid(0, p), 1200);
}

void Game::onSearchFinished()
{
    if (engine_.thinking() || searchKind_ == SearchNone) return;
    const SearchKind kind = searchKind_;
    searchKind_ = SearchNone;
    commitEval();
    if (kind == SearchGame) {
        // 対局探索が指し手なく終わった (bestmove null/(none)) = そのままではサイレントパス。
        // 将棋でパスは反則なので、回数上限つきで探索を再発行する。上限超過はエンジン失格扱い。
        if (!over_ && engineColor_ >= 0 && int(board_.turn()) == engineColor_) {
            if (searchPly_ != usiMoves_.size())
                gameGoRetries_ = 0;
            ++gameGoRetries_;
            if (gameGoRetries_ > 4) {
                endGame(QStringLiteral("エンジンが指し手を返さないため反則負けとしました"));
                return;
            }
            startThink();
        }
        return;
    }
    if (kind == SearchReplay) {
        if (replayPly_ > 0) {
            replayPly_ = searchPly_ + 1;    // 次へ (既存記録は stepReplay が飛ばす)
            stepReplay();
        } else if (pendingReplayStart_) {
            pendingReplayStart_ = false;    // stop完了 → 再生成開始
            startReplay();
        } else {
            // stopReplay による中断
            replayTarget_ = 0;
            studySuspend_ = false;
            setReplayProgress(QString());
            maybeStartAnalysis();
        }
        return;
    }
    if (kind == SearchAnalysis) {
        // 盤が進んでいない=収斂(movetime消化)まで走った検討なら同一局面を繰返さない
        if (!pendingThink_ && searchPly_ == usiMoves_.size())
            analysisDonePly_ = searchPly_;
        // 検討を止めた理由が「人間の着手→対局go待ち」ならそこでgoを発行
        if (pendingThink_) { pendingThink_ = false; startThink(); }
        else if (pendingReplayStart_) { pendingReplayStart_ = false; startReplay(); }
        else maybeStartAnalysis();                     // 連続検討 (二面打ちなど)
    }
}

void Game::onEvalInfo(int multipv, int depth, int cp, int matePly, const QString& pvFirst)
{
    if (searchKind_ == SearchNone || (over_ && searchKind_ != SearchReplay)) return;
    if (multipv == 1 && depth >= 1 && depth >= lastEval_.depth)
        lastEval_ = EvalRec{cp, matePly, depth, true};

    if (searchKind_ != SearchAnalysis || depth < 2 || multipv < 1 || multipv > 2)
        return;
    const int i = multipv - 1;
    // 探索視点→先手視点
    const int sign = searchTurn_ == int(Sente) ? 1 : -1;
    EvalRec e{sign * cp, sign * matePly, depth, true};
    Move m;
    const bool ok = !pvFirst.isEmpty() && pvFirst != QLatin1String("(none)")
                    && Board::parseUsiMove(pvFirst.toStdString(), m) && board_.isLegal(m);
    if (!ok) {
        if (hintUsi_[i].isEmpty() && hintText_[i].isEmpty()) return;
        hintUsi_[i].clear(); hintText_[i].clear(); hintValid_[i] = false;
        if (hintSel_ == i) { hintSel_ = -1; hintFrom_ = hintTo_ = NONE; }
        emit hintsChanged();
        return;
    }
    if (hintUsi_[i] != pvFirst) {
        hintUsi_[i] = pvFirst;
        hintMove_[i] = m;
        hintValid_[i] = true;
        if (hintSel_ == i) { hintSel_ = -1; hintFrom_ = hintTo_ = NONE; }  // 旧強調は無効化
    }
    const QString txt = QStringLiteral("%1 %2 (%3)")
        .arg(i == 0 ? QStringLiteral("最善手") : QStringLiteral("次善手"),
             usiToText(pvFirst, board_), fmtEval(e));
    if (txt != hintText_[i]) {
        hintText_[i] = txt;
        emit hintsChanged();
    }
}

void Game::showHint(int index)
{
    if (index < 0 || index > 1 || !hintValid_[index]) return;
    if (hintSel_ == index) {
        hintSel_ = -1;
        hintFrom_ = hintTo_ = NONE;
    } else {
        hintSel_ = index;
        hintFrom_ = hintMove_[index].from;   // 打の場合は NONE
        hintTo_ = hintMove_[index].to;
    }
    emit hintsChanged();
}

void Game::clearHints()
{
    for (int i = 0; i < 2; ++i) {
        hintUsi_[i].clear(); hintText_[i].clear(); hintValid_[i] = false;
    }
    hintSel_ = -1;
    hintFrom_ = hintTo_ = NONE;
    emit hintsChanged();
}

void Game::resetEvalState()
{
    curve_.clear();
    lastEval_ = EvalRec();
    searchPly_ = searchTurn_ = 0;
    pendingThink_ = false;
    searchKind_ = SearchNone;
    gameGoRetries_ = 0;
    analysisDonePly_ = -1;
    replayPly_ = 0;
    replayTarget_ = 0;
    pendingReplayStart_ = false;
    setReplayProgress(QString());
    clearHints();
    evalMessage_.clear();
    emit evalMessageChanged();
    emit evalCurveChanged();
}

// 収斂した探索の評価を曲線に確定し、学習モードなら前手との差分メッセージを作る
void Game::commitEval()
{
    if (!lastEval_.valid) return;
    const EvalRec raw = lastEval_;          // 探索視点の最新評価
    lastEval_ = EvalRec();
    // 中断由来でも、探索した局面(ply)の評価としては有効なので確定する
    const int sign = searchTurn_ == int(Sente) ? 1 : -1;
    EvalRec e{sign * raw.cp, sign * raw.mate, raw.depth, true};
    const int p = searchPly_;
    const bool isNew = !curve_.contains(p);
    if (isNew || e.depth > curve_.value(p).depth)
        curve_[p] = e;
    if (studyMode_ && p >= 1 && curve_.contains(p - 1)) {
        const EvalRec before = curve_.value(p - 1);
        const Turn mover = pc::other(static_cast<Turn>(searchTurn_));
        QString who;
        if (engineColor_ >= 0)
            who = int(mover) == engineColor_ ? QStringLiteral("エンジン")
                                             : QStringLiteral("あなたの指し手");
        else
            who = turnName(int(mover));
        QString msg = QStringLiteral("%1 %2: %3 → %4")
                          .arg(who, moveTextAtPly(p), fmtEval(before), fmtEval(e));
        if (!before.mate && !e.mate) {
            const int dm = mover == Sente ? e.cp - before.cp : before.cp - e.cp;
            if (dm != 0)
                msg += QStringLiteral(" (%1%2点)").arg(dm > 0 ? QStringLiteral("得")
                                                               : QStringLiteral("損")).arg(qAbs(dm));
        }
        evalMessage_ = msg;
        emit evalMessageChanged();
    }
    if (isNew)
        emit evalCurveChanged();
}

void Game::onEngineReadyChanged()
{
    if (engine_.ready() && !over_ && !engine_.thinking()) {
        if (engineColor_ >= 0 && int(board_.turn()) == engineColor_)
            startThink();
        else
            maybeStartAnalysis();   // 学習モードなら人間の番で検討を開始
    }
    updateStatus();
}

void Game::onBestMove(const QString& usi)
{
    // 学習検討の bestmove は情報収集用。確定・対局go切替は thinkingChanged 側で行う
    if (searchKind_ == SearchAnalysis) return;
    if (over_ || engineColor_ < 0 || int(board_.turn()) != engineColor_)
        return;
    // 対局探索の収斂: 着手を適用する前にこの局面の評価を曲線へ確定する
    if (searchKind_ == SearchGame) {
        commitEval();
        searchKind_ = SearchNone;
    }

    Move m;
    bool ok = Board::parseUsiMove(usi.toStdString(), m);
    if (ok && m.isDrop() && m.drop == NoKind) {
        // 旧形式「+5e」: 持駒から解決
        for (int k = 0; k < 7; ++k) {
            Move d; d.to = m.to; d.drop = Kind(k);
            if (board_.hand(board_.turn(), Kind(k)) > 0 && board_.isLegal(d)) { m = d; ok = true; break; }
            ok = false;
        }
    }
    if (!ok || !board_.isLegal(m)) {
        endGame(QStringLiteral("不正な指し手(") + usi + QStringLiteral(") — ")
                + winText(engineColor_) );
        return;
    }
    applyMove(m);
}

void Game::onEngineResigned()
{
    if (over_) return;
    endGame(QStringLiteral("相手が投了 — ") + winText(engineColor_) );
}

// --- 終局・表示更新 ----------------------------------------------------------

void Game::endGame(const QString& result)
{
    if (over_) return;
    over_ = true;
    searchKind_ = SearchNone;      // stop由来のthinkingChangedで再探索しないよう先に消す
    pendingThink_ = false;
    clearHints();
    if (engine_.thinking()) engine_.stop();
    resultText_ = result;
    tick_.stop();
    clearSelection();
    updateStatus();
    emit ended();
    emit boardChanged();
}

void Game::updateStatus()
{
    QString s;
    if (over_) {
        s = resultText_;
    } else {
        s = turnName(int(board_.turn()));
        if (engine_.thinking())
            s += searchKind_ == SearchAnalysis ? QStringLiteral(" を検討中…")
                                               : QStringLiteral(" が思考中…");
        else
            s += QStringLiteral("の番");
        if (board_.inCheck(board_.turn())) s += QStringLiteral("(王手!)");
        if (engineColor_ >= 0)
            s += QStringLiteral("  [%1]").arg(!engineLabel_.isEmpty() ? engineLabel_
                                       : engine_.ready() ? engine_.engineName()
                                                         : QStringLiteral("エンジン起動中…"));
    }
    if (s != statusText_) {
        statusText_ = s;
        emit statusChanged();
    }
}

void Game::onTick()
{
    if (over_) return;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    qint64 dt = now - lastTick_;
    lastTick_ = now;
    if (promoteAsked_) return; // 成り確認中は時計を止める

    int t = int(board_.turn());
    const bool wasInByomi = inByomi_[t];
    const int before = (clockMs(t) + 999) / 1000;
    clock_[t] -= dt;
    if (clock_[t] <= 0 && byomiMs_ > 0 && !inByomi_[t]) {
        inByomi_[t] = true;
        clock_[t] += byomiMs_;  // 持時間を使い切った後の経過分も秒読みから引く
    }
    if (clock_[t] <= 0) {
        clock_[t] = 0;
        endGame(turnName(t) + QStringLiteral("が時間切れ — ") + winText(t) );
        return;
    }
    if (inByomi_[t] != wasInByomi || (clockMs(t) + 999) / 1000 != before) {
        ++clockTick_;
        emit boardChanged();   // 秒表示の更新
    }
}

// --- 学習モード / 評価値 --------------------------------------------------------

QString Game::fmtEval(const EvalRec& e) const
{
    if (e.mate > 0) return QStringLiteral("+詰%1").arg(e.mate);
    if (e.mate < 0) return QStringLiteral("-詰%1").arg(-e.mate);
    return (e.cp > 0 ? QStringLiteral("+") : QString()) + QString::number(e.cp);
}

int Game::graphVal(const EvalRec& e) const
{
    if (e.mate) return e.mate > 0 ? 1200 : -1200;
    return qBound(-1200, e.cp, 1200);
}

QString Game::usiToText(const QString& usi, const Board& beforeMove) const
{
    Move m;
    if (!Board::parseUsiMove(usi.toStdString(), m))
        return usi;
    const int f = 9 - (m.to % 9);        // 筋
    const int r = m.to / 9 + 1;          // 段 (a=1)
    const QString dest = QString::number(f) + QString::number(r);
    const QString arrow = beforeMove.turn() == Sente ? QStringLiteral("☗")
                                                     : QStringLiteral("☖");
    if (m.isDrop())
        return arrow + dest + handKanji(m.drop) + QStringLiteral("打");
    const int q = beforeMove.at(m.from);
    QString piece;
    if (q != NONE)
        piece = m.promote ? pieceKanji(pc::make(pc::owner(q), pc::kind(q), true))
                          : pieceKanji(q);
    return arrow + dest + piece;
}

QString Game::moveTextAtPly(int ply) const
{
    if (ply < 1 || ply > usiMoves_.size()) return QString();
    Board b;
    if (!b.loadSfen(startSfen_.toLatin1().constData())) return usiMoves_[ply - 1];
    for (int i = 0; i < ply - 1; ++i) {
        Move m;
        if (!Board::parseUsiMove(usiMoves_[i].toStdString(), m)) break;
        b.make(m);
    }
    return usiToText(usiMoves_[ply - 1], b);
}

QVariantList Game::evalCurve() const
{
    QVariantList out;
    for (auto it = curve_.constBegin(); it != curve_.constEnd(); ++it) {
        QVariantMap m;
        m.insert("ply", it.key());
        m.insert("cp", it->cp);
        m.insert("mate", it->mate);
        m.insert("val", graphVal(*it));
        m.insert("text", fmtEval(*it));
        if (it.key() >= 1)
            m.insert("move", moveTextAtPly(it.key()));   // その位置を作った着手 (KIF風)
        out << m;
    }
    return out;
}

Game::~Game()
{
    engine_.quit();
}
