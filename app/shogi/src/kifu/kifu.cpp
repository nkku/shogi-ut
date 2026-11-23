#include "kifu.h"
#include "../shogi/types.h"
#include <QChar>
#include <QRegularExpression>
#include <QTextStream>

using namespace shogi;

namespace {

const char16_t* kFullFile = u"１２３４５６７８９";   // file 1..9
const char16_t* kRankKanji = u"一二三四五六七八九";  // rank 1..9

char kindLetter(Kind k)
{
    switch (k) {
    case Pawn: return 'P'; case Lance: return 'L'; case Knight: return 'N';
    case Silver: return 'S'; case Gold: return 'G'; case Bishop: return 'B';
    case Rook: return 'R'; case King: return 'K'; default: return '?';
    }
}
bool letterKind(char ch, Kind& k)
{
    switch (ch) {
    case 'P': k = Pawn; return true; case 'L': k = Lance; return true;
    case 'N': k = Knight; return true; case 'S': k = Silver; return true;
    case 'G': k = Gold; return true; case 'B': k = Bishop; return true;
    case 'R': k = Rook; return true; case 'K': k = King; return true;
    default: return false;
    }
}

// 盤上の駒 -> KIF駒名 (成駒は正式表記。新しい成りは呼び出し側が"成"を足す)
QString kifPieceName(int code)
{
    Kind k = pc::kind(code);
    bool prom = pc::promoted(code);
    switch (k) {
    case Pawn:    return prom ? QStringLiteral("と") : QStringLiteral("歩");
    case Lance:   return prom ? QStringLiteral("成香") : QStringLiteral("香");
    case Knight:  return prom ? QStringLiteral("成桂") : QStringLiteral("桂");
    case Silver:  return prom ? QStringLiteral("成銀") : QStringLiteral("銀");
    case Gold:    return QStringLiteral("金");
    case Bishop:  return prom ? QStringLiteral("馬") : QStringLiteral("角");
    case Rook:    return prom ? QStringLiteral("竜") : QStringLiteral("飛");
    case King:    return pc::owner(code) == Sente ? QStringLiteral("王") : QStringLiteral("玉");
    default: return QString();
    }
}

// "7g" -> KIF位置 "７七"
QString kifPos(const QString& usi)
{
    int file = usi[0].toLatin1() - '0';
    int rank = usi[1].toLatin1() - 'a' + 1;
    if (file < 1 || file > 9 || rank < 1 || rank > 9) return QStringLiteral("?");
    return QString(QChar(kFullFile[file - 1])) + QChar(kRankKanji[rank - 1]);
}

QString normalize(const QString& text)
{
    QString s = text;
    s.replace(QChar(0xFF08), QLatin1Char('(')).replace(QChar(0xFF09), QLatin1Char(')'));
    for (QChar& c : s) {
        ushort u = c.unicode();
        if (u >= 0xFF11 && u <= 0xFF19) c = QChar(QLatin1Char('1' + (u - 0xFF11))); // 全角数字
        else if (u == 0xFF10) c = QLatin1Char('0');
        else if (u == 0x00D7 || u == 0x2606) c = QLatin1Char('*');                  // ×/☆ -> 打代替
        else if (u == 0xFF0D) c = QLatin1Char('-');                                 // 全角ハイフン
    }
    s.replace(QChar(0x3000), QLatin1Char(' ')); // 全角スペース
    return s;
}

// 漢数字 -> rank (1..9)。全角/半角数字も可
int rankOf(QChar ch)
{
    static const char16_t tbl[] = u"一二三四五六七八九";
    for (int i = 0; i < 9; i++) if (ch == QChar(tbl[i])) return i + 1;
    if (ch.isDigit()) return ch.digitValue();
    return -1;
}

// 単一/二文字の駒名 -> USIレター (成り済みの駒名は '+' なしで済むのでベースのみ)
// 戻り値: 消費長 (0=不一致)。promotedName=trueなら「既に成っている駒」
int pieceOf(const QString& rest, char& letter, int& consumedLen)
{
    struct Two { const char16_t* s; char l; };
    static const Two two[] = {
        { u"成銀", 'S' }, { u"成桂", 'N' }, { u"成香", 'L' }, { u"成歩", 'P' },
        { u"成飛", 'R' }, { u"成角", 'B' },
    };
    for (const auto& t : two) {
        if (rest.startsWith(QString::fromUtf16(t.s, 2))) {
            letter = t.l; consumedLen = 2; return 1; // 成済みとして扱う
        }
    }
    if (rest.isEmpty()) return 0;
    struct One { char16_t c; char l; };
    static const One one[] = {
        { u'歩', 'P' }, { u'香', 'L' }, { u'桂', 'N' }, { u'銀', 'S' }, { u'金', 'G' },
        { u'角', 'B' }, { u'飛', 'R' }, { u'王', 'K' }, { u'玉', 'K' },
        { u'と', 'P' }, { u'圭', 'N' }, { u'杏', 'L' }, { u'全', 'S' },
        { u'馬', 'B' }, { u'竜', 'R' }, { u'龍', 'R' }, { u'金', 'G' },
    };
    for (const auto& t : one) {
        if (rest[0] == QChar(t.c)) { letter = t.l; consumedLen = 1; return 1; }
    }
    return 0;
}

} // namespace

namespace kifu {

bool letterToKind(char ch, Kind& k) { return letterKind(ch, k); }

QString exportKif(const shogi::Board& startPos, const QStringList& usiMoves,
                  const QString& senteName, const QString& goteName,
                  const QString& endText)
{
    QString out;
    out += QStringLiteral("手合割：平手\n");
    out += QStringLiteral("先手：") + senteName + QLatin1Char('\n');
    out += QStringLiteral("後手：") + goteName + QLatin1Char('\n');
    out += QStringLiteral("手数----指手---------消費時間--\n");

    shogi::Board b = startPos;
    int prevTo = NONE;
    int n = 0;
    for (const QString& mv : usiMoves) {
        Move m;
        if (!Board::parseUsiMove(mv.toStdString(), m)) continue;
        ++n;
        const QString toUsi = QString::fromStdString(Board::usiCoord(m.to));
        QString body;
        QString fromStr;
        if (m.isDrop()) {
            body = kifPos(toUsi) + kifPieceName(pc::make(Gote, m.drop)) + QStringLiteral("打");
            fromStr = QStringLiteral("00");
        } else {
            const QString fromUsi = QString::fromStdString(Board::usiCoord(m.from));
            fromStr = QString(QChar(fromUsi[0].toLatin1()))
                    + QString::number(fromUsi[1].toLatin1() - 'a' + 1);
            QString name = kifPieceName(b.at(m.from));
            if (prevTo != NONE && m.to == prevTo)
                body = QStringLiteral("同") + name;
            else
                body = kifPos(toUsi) + name;
            if (m.promote) body += QStringLiteral("成");
        }
        out += QStringLiteral("%1 %2(%3)  0:00 0:00\n")
                   .arg(n, 3)
                   .arg(body, 10)
                   .arg(fromStr);
        prevTo = m.to;
        b.make(m);
    }
    if (!endText.isEmpty())
        out += QStringLiteral("%1 %2\n").arg(n + 1, 3).arg(endText);
    return out;
}

Parsed importKif(const QString& text)
{
    Parsed r;
    r.startSfen = QStringLiteral(
        "lnsgkgsnl/1r5b1/ppppppppp/9/9/9/PPPPPPPPP/1B5R1/LNSGKGSNL b - 1");
    const QString norm = normalize(text);
    const QStringList lines = norm.split(QLatin1Char('\n'));

    bool inMoves = false;
    int prevTo = -1;
    QRegularExpression moveRe(QStringLiteral("^\\s*(\\d+)\\s+(\\S.*)$"));
    int lineNo = 0;
    for (const QString& raw : lines) {
        ++lineNo;
        QString line = raw.trimmed();
        if (line.isEmpty()) continue;
        if (line.startsWith(QStringLiteral("#"))) continue;
        if (!inMoves) {
            if (line.contains(QStringLiteral("手数"))) { inMoves = true; continue; }
            if (line.startsWith(QStringLiteral("手合割"))) {
                if (!line.contains(QStringLiteral("平手"))) {
                    r.error = QStringLiteral("平手以外の駒落ちには対応していません");
                    return r;
                }
            }
            // 「手数----」ヘッダが無いKIF/エクスポートのフォールバック:
            // 数字+空白+指手らしき行が来たら指手セクションとみなす
            QRegularExpressionMatch probe = moveRe.match(line);
            if (probe.hasMatch() && probe.captured(2).size() >= 2
                && probe.captured(2).contains(QRegularExpression(QStringLiteral("[\\x{4e00}-\\x{9fff}]"))))
                inMoves = true;
            else
                continue;
        }
        QRegularExpressionMatch mt = moveRe.match(line);
        if (!mt.hasMatch()) continue;
        QString body = mt.captured(2).trimmed();
        // 全角ハイフン区切り "(77)----------" 等は括弧以降を落とす
        int dash = body.indexOf(QLatin1Char('-'));
        if (dash > 0) body = body.left(dash).trimmed();

        if (body.contains(QStringLiteral("投了")) || body.contains(QStringLiteral("中断"))
            || body.contains(QStringLiteral("千日手")) || body.contains(QStringLiteral("詰み"))
            || body.contains(QStringLiteral("反則")) || body.contains(QStringLiteral("持将棋"))
            || body.contains(QStringLiteral("引き分け")) || body.contains(QStringLiteral("入玉"))
            || body.contains(QStringLiteral("宣言"))) {
            r.endText = body;
            break;
        }
        if (body.startsWith(QStringLiteral("開始"))) continue; // "開始" 行

        bool same = false;
        QString rest = body;
        QString toUsi;
        if (rest.startsWith(QStringLiteral("同"))) {
            same = true;
            rest = rest.mid(1);
        } else {
            int file = rest.size() > 0 ? (rest[0].toLatin1() - '0') : -1;
            int rank = rest.size() > 1 ? rankOf(rest[1]) : -1;
            if (file < 1 || file > 9 || rank < 1 || rank > 9) {
                if (!r.usi.isEmpty()) break;   // 終端側のゴミ行: ここまでで読む
                r.error = QStringLiteral("%1行目を解析できません: %2").arg(lineNo).arg(body);
                return r;
            }
            toUsi = QString(QChar('0' + file)) + QChar('a' + rank - 1);
            rest = rest.mid(2);
            if (rest.startsWith(QStringLiteral("同"))) rest = rest.mid(1); // "２二同銀(71)"
        }
        if (same) {
            if (prevTo < 0) { r.error = QStringLiteral("先頭に『同』があります"); return r; }
            toUsi = QString::fromStdString(Board::usiCoord(prevTo));
        }

        char letter = '?';
        int consumed = 0;
        if (!pieceOf(rest, letter, consumed)) {
            if (!r.usi.isEmpty()) break;       // 終端側のゴミ行: ここまでで読む
            r.error = QStringLiteral("%1行目の駒が判別できません: %2").arg(lineNo).arg(body);
            return r;
        }
        rest = rest.mid(consumed);
        // 成り/不成/打
        bool promote = false, dropHint = false;
        if (rest.startsWith(QStringLiteral("不成"))) {
            rest = rest.mid(2);
        } else if (rest.startsWith(QStringLiteral("打"))) {
            dropHint = true; rest = rest.mid(1);
        } else if (rest.startsWith(QStringLiteral("成"))) {
            promote = true; rest = rest.mid(1);
        }
        // (from)
        QString fromUsi;
        int lp = rest.indexOf(QLatin1Char('('));
        if (lp >= 0) {
            int rp = rest.indexOf(QLatin1Char(')'), lp);
            if (rp > lp) {
                QString f = rest.mid(lp + 1, rp - lp - 1).trimmed();
                if (f == QStringLiteral("00") || f.isEmpty()) dropHint = true;
                else if (f.size() == 2 && f[0].isDigit() && f[1].isDigit())
                    fromUsi = QString(f[0]) + QChar('a' + (f[1].digitValue() - 1));
            }
        }
        const QString promSuffix = promote ? QStringLiteral("+") : QString();
        if (dropHint) {
            r.usi << QString(letter) + QStringLiteral("*") + toUsi;
        } else if (fromUsi.isEmpty()) {
            // 打でも(from)でもない = 着手元不明: Game側で合法手から解決する
            r.usi << QStringLiteral("?") + toUsi + promSuffix + QString(QChar(letter));
        } else {
            r.usi << fromUsi + toUsi + promSuffix;
        }
        int toSq;
        if (Board::parseUsiCoord(toUsi.toStdString(), toSq)) prevTo = toSq;
    }
    if (r.usi.isEmpty() && r.endText.isEmpty()) {
        r.error = QStringLiteral("指し手がありませんでした");
        return r;
    }
    r.ok = true;
    return r;
}

QString exportSfenLine(const QString& startSfen, const QStringList& usiMoves)
{
    QString s = startSfen;
    if (!usiMoves.isEmpty()) s += QStringLiteral(" moves ") + usiMoves.join(QLatin1Char(' '));
    return s + QLatin1Char('\n');
}

Parsed importSfenLine(const QString& text)
{
    Parsed r;
    QString s = text.trimmed();
    if (s.startsWith(QStringLiteral("sfen "), Qt::CaseInsensitive)) s = s.mid(5);
    if (s.startsWith(QStringLiteral("position "), Qt::CaseInsensitive)) s = s.mid(9);
    const QStringList toks = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (toks.size() < 4) { r.error = QStringLiteral("SFENが壊れています"); return r; }
    r.startSfen = QStringList(toks.mid(0, 4)).join(QLatin1Char(' '));
    int i = 4;
    if (i < toks.size() && toks[i].compare(QStringLiteral("moves"), Qt::CaseInsensitive) == 0) {
        r.usi = toks.mid(i + 1);
        i = toks.size();
    }
    (void)i;
    r.ok = true;
    return r;
}

} // namespace kifu
