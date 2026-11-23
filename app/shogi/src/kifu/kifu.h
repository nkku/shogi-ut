#pragma once
// 棋譜フォーマット変換: KIF / SFEN-lines (Game::usiMoves_ の USI文字列リストが中心)
#include "../shogi/board.h"
#include <QString>
#include <QStringList>

namespace kifu {

// USI駒文字 <-> Kind (KIF parser/Game側の解決処理で共用)
bool letterToKind(char ch, shogi::Kind& k);

// KIF出力。startPos=初局面(並べ替えで再現)、endText=最終行の結末(投了/詰み/千日手…)
QString exportKif(const shogi::Board& startPos, const QStringList& usiMoves,
                  const QString& senteName, const QString& goteName,
                  const QString& endText);

struct Parsed {
    bool ok = false;
    QString startSfen;    // 平手初期なら標準SFEN
    QStringList usi;      // 各手 USI ("7g7f","P*7g","8h3c+")。from不明は "?7f+歩"形式
    QString endText;
    QString error;
};

Parsed importKif(const QString& text);

// SFEN + moves を1行にした形式 ("lnsg... b - 1 moves 7g7f ...")
QString exportSfenLine(const QString& startSfen, const QStringList& usiMoves);
Parsed importSfenLine(const QString& text);

} // namespace kifu
