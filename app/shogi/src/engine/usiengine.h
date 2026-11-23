#pragma once
#include <QObject>
#include <QProcess>
#include <QStringList>

// "info depth 12 ... multipv 1 score cp 214 ... pv 7g7f ..." の主な項目 (学習モード用)
struct UsiInfo {
    bool ok = false;
    int multipv = 1;
    int depth = 0;
    int cp = 0;          // score cp (探索側の視点)。score mate 時は未使用
    int matePly = 0;     // 0=普通評価、+n=n手で詰ます、-n=n手で詰ます側 (探索側の視点)
    QString pvFirst;     // pvの最初の指し手 ("7g7f" / "P*5e" など)
};
// score と pv を含む info 行のみ ok=true。"info string" や区切り行は false。
UsiInfo parseUsiInfoLine(const QString& line);

// USIエンジンを子プロセスとして管理する。
// 手順: usi → [id/option] → usiok → setoption… → isready → readyok
//        → usinewgame → position sfen … moves … → go btime … → bestmove
class UsiEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool thinking READ thinking NOTIFY thinkingChanged)
    Q_PROPERTY(QString engineName READ engineName NOTIFY engineNameChanged)
public:
    explicit UsiEngine(QObject* parent = nullptr);

    Q_INVOKABLE void start(const QString& program, const QStringList& args = QStringList());
    Q_INVOKABLE void quit();
    /// readyok まで待たずに設定を積む(usiok 後にまとめて送信)
    Q_INVOKABLE void setOption(const QString& name, const QString& value);
    /// 対局開始(position/go より前に必ず呼ぶ)
    Q_INVOKABLE void newGame();
    /// 1手待たせる。moves は初手からのUSI指し手列。
    Q_INVOKABLE void think(const QString& sfenStart, const QStringList& moves,
                           int btimeMs, int wtimeMs, int byomiMs);
    Q_INVOKABLE void goPonder(const QString& sfenStart, const QStringList& moves,
                              int btimeMs, int wtimeMs, int byomiMs);
    /// 学習モードの検討用: 固定時間の探索 (go movetime)。bestmove は無視され info のみ使用
    Q_INVOKABLE void analyze(const QString& sfenStart, const QStringList& moves, int movetimeMs);
    Q_INVOKABLE void stop();
    /// 指し手文字列を "7g7f" / "P*5e" / "7g7f+" に正規化(旧形式 "+7g" は変換不可なので呼び出し側で処理)
    Q_INVOKABLE void sendRaw(const QString& line);

    bool running() const;
    bool ready() const { return ready_; }
    bool thinking() const { return thinking_; }
    QString engineName() const { return engineName_; }

signals:
    void runningChanged();
    void readyChanged();
    void thinkingChanged();
    void engineNameChanged();
    void bestMove(const QString& usi);       // 実指し手のみ
    void ponderMove(const QString& usi);     // "bestmove X ponder Y" の Y
    void resigned();
    void won();
    void gameover(const QString& line);
    void info(const QString& text);          // 探索状況(デバッグ/検討用)
    void evalInfo(int multipv, int depth, int cp, int matePly, const QString& pvFirst);
    void logLine(const QString& line);

private slots:
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);

private:
    void send(const QString& line);
    void handleLine(const QString& line);
    void flushOptions();
    void flushPendingThink();

    QProcess proc_;
    QByteArray buffer_;
    bool ready_ = false;
    bool usiokSeen_ = false;
    bool thinking_ = false;
    bool newGamePending_ = false;
    bool pondering_ = false;
    QString engineName_;
    QStringList optionQueue_;   // "setoption name X value Y"
    // 準備できるまで待たせる position/go
    QString pendingPosition_;
    QString pendingGo_;
    bool pendingNewGame_ = false;
};
