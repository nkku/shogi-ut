#pragma once
#include <QObject>
#include <QTimer>
#include <QStringList>
#include <QVariantList>
#include <QMap>
#include "shogi/board.h"
#include "engine/usiengine.h"

// 対局ロジック: タップ選択→指し手確定、成り確認、持駒打ち、時計、千日手、エンジン対戦
class Game : public QObject {
    Q_OBJECT
    Q_PROPERTY(int turn READ turn NOTIFY boardChanged)
    Q_PROPERTY(bool inCheck READ inCheck NOTIFY boardChanged)
    Q_PROPERTY(bool thinking READ thinking NOTIFY thinkingChanged)
    Q_PROPERTY(int engineColor READ engineColor NOTIFY modeChanged)  // -1=二面打ち 0=エンジン先手 1=エンジン後手
    Q_PROPERTY(bool flipped READ flipped NOTIFY flippedChanged)
    Q_PROPERTY(bool over READ over NOTIFY ended)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString resultText READ resultText NOTIFY ended)
    Q_PROPERTY(bool promoteAsked READ promoteAsked NOTIFY promoteChanged)
    Q_PROPERTY(QString kifuMessage READ kifuMessage NOTIFY kifuMessageChanged)
    Q_PROPERTY(int selectedSquare READ selectedSquare NOTIFY selectionChanged)
    Q_PROPERTY(int selectedHand READ selectedHand NOTIFY selectionChanged)
    Q_PROPERTY(QObject* engine READ engine CONSTANT)
    Q_PROPERTY(QString engineName READ engineName NOTIFY engineNameChanged)
    Q_PROPERTY(int clockTick READ clockTick NOTIFY boardChanged)
    Q_PROPERTY(int ply READ ply NOTIFY boardChanged)   // 着手数 (保存ボタンのenabled用)
    Q_PROPERTY(bool studyMode READ studyMode NOTIFY studyChanged)
    Q_PROPERTY(QString hint1Text READ hint1Text NOTIFY hintsChanged)
    Q_PROPERTY(QString hint2Text READ hint2Text NOTIFY hintsChanged)
    Q_PROPERTY(int hintFrom READ hintFrom NOTIFY hintsChanged)   // 検討手の強調盤面 (NONE=なし)
    Q_PROPERTY(int hintTo READ hintTo NOTIFY hintsChanged)
    Q_PROPERTY(QVariantList evalCurve READ evalCurve NOTIFY evalCurveChanged)
    Q_PROPERTY(bool hasEvalGraph READ hasEvalGraph NOTIFY evalCurveChanged)
    Q_PROPERTY(QString evalMessage READ evalMessage NOTIFY evalMessageChanged)
public:
    explicit Game(QObject* parent = nullptr);
    ~Game() override;

    // --- 表示用 ---
    Q_INVOKABLE QString pieceText(int sq) const;      // 盤上の漢字(""=空)
    Q_INVOKABLE bool pieceIsSente(int sq) const;
    Q_INVOKABLE QString handText(int turn) const;     // 例 "飛 歩2"
    Q_INVOKABLE int handCount(int turn, int kind) const;
    Q_INVOKABLE bool handActive(int turn, int kind) const;  // いまタップできる持駒か
    Q_INVOKABLE bool squareActive(int sq) const;      // 選択可能な自分の駒か
    Q_INVOKABLE bool squareSelected(int sq) const;
    Q_INVOKABLE bool squareTarget(int sq) const;      // 選択中の移動先/打ち先候補か
    Q_INVOKABLE int kingInCheckSquare() const;  // 王手時の玉位置(強調用)、そうでなければ-1
    Q_INVOKABLE int clockMs(int turn) const;
    Q_INVOKABLE QString clockText(int turn) const;   // "10:00" / "byo 30"
    Q_INVOKABLE int clockTick() const { return clockTick_; }
    Q_INVOKABLE int ply() const { return usiMoves_.size(); }
    Q_INVOKABLE QString debugSfen() const { return QString::fromStdString(board_.sfen()); }
    Q_INVOKABLE bool inByomi(int turn) const { return inByomi_[turn]; }

    // --- 学習モード / 評価値 ---
    Q_INVOKABLE void showHint(int index);   // 0=最善手 1=次善手: 盤面強調の表示/非表示を切替
    QVariantList evalCurve() const;         // [{ply,cp,mate,val,text}] 先手視点
    bool hasEvalGraph() const { return curve_.size() >= 2; }
    QString evalMessage() const { return evalMessage_; }
    QString hint1Text() const { return hintText_[0]; }
    QString hint2Text() const { return hintText_[1]; }
    int hintFrom() const { return hintFrom_; }
    int hintTo() const { return hintTo_; }
    bool studyMode() const { return studyMode_; }

    // 開発/テスト用: SFENで局面を注入(選択状態のみクリア)。成り・詰み等の再現確認に使う
    Q_INVOKABLE bool debugSetSfen(const QString& sfen);

    // --- 入力 ---
    Q_INVOKABLE void tapSquare(int sq);
    Q_INVOKABLE void tapHand(int turn, int kind);
    Q_INVOKABLE void answerPromote(bool yes);
    Q_INVOKABLE void cancelSelection();
    Q_INVOKABLE void resign();
    Q_INVOKABLE void undo();

    // --- 対局開始 ---
    // study=true: 学習モード。手番側(人間/二面打ち)にエンジン検討を走らせ、
    // 最善手・次善手のヒントと評価値グラフのデータを作る (エンジン起動が必要)
    Q_INVOKABLE void newGameHuman(int mainSec, int byomiSec, bool study = false,
                                  const QString& enginePath = QString(),
                                  const QString& engineId = QString(),
                                  int threads = 2, int hashMb = 128);
    // engineId: ""/"yaneuraou"=同梱やねうら王(Material), "suisho5"=同梱水匠5(NNUE+評価ファイル),
    //           "tsuyou11"=同梱つようら王11mini(SFNN+評価ファイル)
    Q_INVOKABLE void newGameEngine(bool humanSente, int mainSec, int byomiSec,
                                   const QString& enginePath, int threads, int hashMb,
                                   const QString& engineId = QString(),
                                   bool study = false);

    // --- 棋譜の保存・読込 (書き出しは専用領域→content-hubへ転送 / 読込は渡されたパス) ---
    Q_INVOKABLE QString saveKif();    // 保存成功でファイルパス、失敗で""(理由 は kifuMessage)
    Q_INVOKABLE QString saveSfen();
    Q_INVOKABLE bool importKifu(const QString& path, int mainSec, int byomiSec);
    QString kifuMessage() const { return kifuMessage_; }

    int turn() const { return int(board_.turn()); }
    bool inCheck() const { return board_.inCheck(board_.turn()); }
    bool thinking() const { return engine_.thinking(); }
    int engineColor() const { return engineColor_; }
    // 下側=プレイヤー側: CPU戦は人間後手のとき反転、二面打ちは手番側を下に
    bool flipped() const
    {
        if (engineColor_ >= 0) return engineColor_ == int(shogi::Sente);
        return board_.turn() == shogi::Gote;
    }
    bool over() const { return over_; }
    QString statusText() const { return statusText_; }
    QString resultText() const { return resultText_; }
    bool promoteAsked() const { return promoteAsked_; }
    int selectedSquare() const { return int(selSq_); }
    int selectedHand() const { return selHand_; }
    QObject* engine() { return &engine_; }
    QString engineName() const { return engine_.engineName(); }

signals:
    void boardChanged();
    void statusChanged();
    void kifuMessageChanged();
    void thinkingChanged();
    void ended();
    void promoteChanged();
    void selectionChanged();
    void modeChanged();
    void flippedChanged();
    void engineNameChanged();
    void lastMove(int from, int to);   // 直近の指し手(表示強調)
    void studyChanged();
    void hintsChanged();
    void evalCurveChanged();
    void evalMessageChanged();

private slots:
    void onEngineReadyChanged();
    void onBestMove(const QString& usi);
    void onEngineResigned();
    void onEvalInfo(int multipv, int depth, int cp, int matePly, const QString& pvFirst);
    void onTick();

private:
    enum SearchKind { SearchNone, SearchGame, SearchAnalysis };
    struct EvalRec {
        int cp = 0;      // 先手視点に変換済み
        int mate = 0;    // 先手視点: +n=先手勝ち/n手で詰ます, -n=後手勝ち
        int depth = 0;
        bool valid = false;
    };
    void setupGame(int mainSec, int byomiSec);
    bool interactive() const;
    void clearSelection();
    void selectSquare(int sq);
    void selectHand(int kind);
    void tryTargets(int sq);
    void applyMove(const shogi::Move& m);
    void startThink();
    void maybeStartAnalysis();
    void applyMultiPv(int n);        // 対局=1/検討=2 (エンジンアイドル時のみ送信)
    void onSearchFinished();       // engine thinking=false 時に探索を確定
    void commitEval();
    void resetEvalState();
    void clearHints();
    QString usiToText(const QString& usi, const shogi::Board& beforeMove) const;  // "☗76歩"/"☗55歩打"
    QString moveTextAtPly(int ply) const;   // n手目(1始まり)を初形から再現して文言化
    QString fmtEval(const EvalRec& e) const;
    int graphVal(const EvalRec& e) const;   // グラフY値 (詰みは ±1200 でクランプ)
    void endGame(const QString& result);
    void updateStatus();
    QString enginePathOrDefault(const QString& path, const QString& engineId) const;
    QString configureEngine(const QString& enginePath, const QString& engineId,
                            int threads, int hashMb);   // 起動+共通option設定。失敗で""
    void setKifuMessage(const QString& msg);

    shogi::Board board_;
    UsiEngine engine_;
    QStringList usiMoves_;
    QString startSfen_;
    int engineColor_ = -1;
    QString engineLabel_;       // 表示用のエンジン名 (水匠5=やねうら王NNUE+水匠5評価なので判別ラベル)
    QString kifuMessage_;

    int selSq_ = shogi::NONE;
    int selHand_ = -1;
    std::vector<shogi::Move> targets_;      // 現在の選択に対する合法手候補
    std::vector<shogi::Move> promotePair_;  // 成/不成の2択待ち
    bool promoteAsked_ = false;
    int lastFrom_ = shogi::NONE, lastTo_ = shogi::NONE;

    bool over_ = false;
    QString resultText_;
    QString statusText_;

    qint64 clock_[2] = { 0, 0 };
    bool inByomi_[2] = { false, false };
    int byomiMs_ = 0;
    int clockTick_ = 0;
    QTimer tick_;
    qint64 lastTick_ = 0;

    // --- 学習モード / 評価値 ---
    bool studyMode_ = false;
    bool studySuspend_ = false;      // 棋譜リプレイ中は検討を開始しない
    SearchKind searchKind_ = SearchNone;
    int lastMultiPv_ = 1;                // エンジンに送信済みのMultiPV (configureEngineで1へreset)
    bool pendingThink_ = false;      // 検討中断のstop→対局goの延期
    int gameGoRetries_ = 0;          // bestmove null 時の対局go再発行回数 (上限で反則負け)
    int analysisDonePly_ = -1;       // このplyで検討収斂済みなら再探索しない
    int searchPly_ = 0;              // 収斂中の探索が始まった時点のply
    int searchTurn_ = 0;             // その探索の着手側(=探索視点)
    EvalRec lastEval_;               // 収斂中の探索の最新info評価(探索視点)
    QMap<int, EvalRec> curve_;       // ply → 先手視点評価
    QString hintUsi_[2];
    shogi::Move hintMove_[2];
    bool hintValid_[2] = { false, false };
    QString hintText_[2];
    int hintFrom_ = shogi::NONE, hintTo_ = shogi::NONE;
    int hintSel_ = -1;
    QString evalMessage_;
};
