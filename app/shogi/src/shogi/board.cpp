#include "board.h"
#include <algorithm>
#include <cctype>
#include <iterator>

namespace shogi {
namespace detail {

namespace {

int fwd(Turn by) { return by == Sente ? -1 : 1; }  // 先手はrow減少方向が前

struct Off { int r, c; };
const Off kGoldShape[6] = {{-1,-1},{-1,0},{-1,1},{0,-1},{0,1},{1,0}};
const Off kSilverShape[5] = {{-1,-1},{-1,0},{-1,1},{1,-1},{1,1}};
const Off kDiag[4] = {{-1,-1},{-1,1},{1,-1},{1,1}};
const Off kOrth[4] = {{-1,0},{1,0},{0,-1},{0,1}};

bool inZone(Turn mover, int row) {
    return mover == Sente ? row <= 2 : row >= 6;
}

bool promotableOn(const std::array<int, SQUARES>& sq, const Move& m) {
    int p = sq[m.from];
    if (!pc::canPromote(p)) return false;
    Turn o = pc::owner(p);
    return inZone(o, rowOf(m.from)) || inZone(o, rowOf(m.to));
}

bool mustPromoteOn(const std::array<int, SQUARES>& sq, const Move& m) {
    int p = sq[m.from];
    Turn o = pc::owner(p);
    switch (pc::kind(p)) {
    case Pawn: case Lance:
        return rowOf(m.to) == (o == Sente ? 0 : 8);
    case Knight:
        return o == Sente ? rowOf(m.to) <= 1 : rowOf(m.to) >= 7;
    default:
        return false;
    }
}

} // namespace

// SFENの持駒表記: 盤上と同じく大文字=先手、小文字=後手。枚数は駒文字の前(2P形式、YaneuraOu準拠)。
Turn handTurnFromChar(char ch) { return std::isupper(static_cast<unsigned char>(ch)) ? Sente : Gote; }
char handChar(Turn t, Kind k) {
    static const char letters[7] = { 'p', 'l', 'n', 's', 'g', 'b', 'r' };
    char c = letters[k];
    return t == Sente ? static_cast<char>(std::toupper(c)) : c;
}

// 相手(by)の盤上の駒が target を攻撃しているか
bool isAttacked(const std::array<int, SQUARES>& sq, int target, Turn by) {
    const int r = rowOf(target), c = colOf(target);
    const int f = fwd(by);

    auto pieceAt = [&](int rr, int cc) {
        return onBoard(rr, cc) ? sq[idx(rr, cc)] : NONE;
    };

    { int p = pieceAt(r - f, c);  // 歩
      if (p != NONE && pc::owner(p) == by && pc::kind(p) == Pawn && !pc::promoted(p)) return true; }
    for (int dc : {-1, 1}) {      // 桂馬
        int p = pieceAt(r - 2 * f, c + dc);
        if (p != NONE && pc::owner(p) == by && pc::kind(p) == Knight && !pc::promoted(p)) return true;
    }
    {                              // 香
        int rr = r - f;
        while (onBoard(rr, c) && sq[idx(rr, c)] == NONE) rr -= f;
        if (onBoard(rr, c)) {
            int p = sq[idx(rr, c)];
            if (p != NONE && pc::owner(p) == by && pc::kind(p) == Lance && !pc::promoted(p)) return true;
        }
    }
    for (const auto& o : kGoldShape) {   // 金系
        int p = pieceAt(r + f * o.r, c - o.c);
        if (p != NONE && pc::owner(p) == by && pc::movesAsGold(p)) return true;
    }
    for (const auto& o : kSilverShape) { // 銀(不成)
        int p = pieceAt(r + f * o.r, c - o.c);
        if (p != NONE && pc::owner(p) == by && pc::kind(p) == Silver && !pc::promoted(p)) return true;
    }
    for (int dr = -1; dr <= 1; ++dr)     // 玉・竜・馬(1マス_any: 竜=飛+玉, 馬=角+玉)
        for (int dc = -1; dc <= 1; ++dc)
            if (dr || dc) {
                int p = pieceAt(r + dr, c + dc);
                if (p != NONE && pc::owner(p) == by &&
                    (pc::kind(p) == King ||
                     (pc::promoted(p) && (pc::kind(p) == Bishop || pc::kind(p) == Rook))))
                    return true;
            }
    for (const auto& o : kDiag) {        // 角・馬(馬も斜めは角として任意距離)
        int rr = r + o.r, cc = c + o.c;
        while (onBoard(rr, cc) && sq[idx(rr, cc)] == NONE) { rr += o.r; cc += o.c; }
        if (onBoard(rr, cc)) {
            int p = sq[idx(rr, cc)];
            if (p != NONE && pc::owner(p) == by && pc::kind(p) == Bishop) return true;
        }
    }
    for (const auto& o : kOrth) {        // 飛・竜(竜も縦横は飛として任意距離)
        int rr = r + o.r, cc = c + o.c;
        while (onBoard(rr, cc) && sq[idx(rr, cc)] == NONE) { rr += o.r; cc += o.c; }
        if (onBoard(rr, cc)) {
            int p = sq[idx(rr, cc)];
            if (p != NONE && pc::owner(p) == by && pc::kind(p) == Rook) return true;
        }
    }
    return false;
}

void genPseudoFrom(const std::array<int, SQUARES>& sq, int from, std::vector<Move>& out) {
    const int p = sq[from];
    const Turn o = pc::owner(p);
    const int r = rowOf(from), c = colOf(from);
    const int f = fwd(o);

    auto push = [&](int rr, int cc) -> bool {
        if (!onBoard(rr, cc)) return false;
        int t = sq[idx(rr, cc)];
        if (t != NONE && pc::owner(t) == o) return false;
        Move m; m.from = from; m.to = idx(rr, cc);
        const bool prom = promotableOn(sq, m);
        const bool forced = mustPromoteOn(sq, m);
        if (prom && !forced) out.push_back(m);
        if (prom) { Move mp = m; mp.promote = true; out.push_back(mp); }
        if (!prom) out.push_back(m);
        return t == NONE;
    };

    Kind k = pc::kind(p);
    const bool prom = pc::promoted(p);
    if (prom && pc::movesAsGold(p)) k = Gold;

    switch (k) {
    case Pawn:
        push(r + f, c);
        break;
    case Lance:
        for (int rr = r + f; onBoard(rr, c); rr += f)
            if (!push(rr, c)) break;
        break;
    case Knight:
        push(r + 2 * f, c - 1);
        push(r + 2 * f, c + 1);
        break;
    case Silver:
        for (const auto& o_ : kSilverShape) push(r - f * o_.r, c + o_.c);
        break;
    case Gold:
        for (const auto& o_ : kGoldShape) push(r - f * o_.r, c + o_.c);
        break;
    case King:
        for (int dr = -1; dr <= 1; ++dr)
            for (int dc = -1; dc <= 1; ++dc)
                if (dr || dc) push(r + dr, c + dc);
        break;
    case Bishop:
        for (const auto& o_ : kDiag)
            for (int rr = r + o_.r, cc = c + o_.c; onBoard(rr, cc); rr += o_.r, cc += o_.c)
                if (!push(rr, cc)) break;
        break;
    case Rook:
        for (const auto& o_ : kOrth)
            for (int rr = r + o_.r, cc = c + o_.c; onBoard(rr, cc); rr += o_.r, cc += o_.c)
                if (!push(rr, cc)) break;
        break;
    default: break;
    }
    if (prom && pc::kind(p) == Rook)          // 竜: 斜め1マス
        for (const auto& o_ : kDiag) push(r + o_.r, c + o_.c);
    if (prom && pc::kind(p) == Bishop)        // 馬: 縦横1マス
        for (const auto& o_ : kOrth) push(r + o_.r, c + o_.c);
}

void genPseudoDrops(const std::array<int, SQUARES>& sq, const Hands& hand,
                    Turn o, std::vector<Move>& out) {
    for (int k = 0; k < 7; ++k) {
        if (hand[o][k] <= 0) continue;
        Kind kind = static_cast<Kind>(k);
        for (int s = 0; s < SQUARES; ++s) {
            if (sq[s] != NONE) continue;
            const int r = rowOf(s), c = colOf(s);
            if (kind == Pawn || kind == Lance) {
                if (r == (o == Sente ? 0 : 8)) continue;      // 最段に打てない
            } else if (kind == Knight) {
                if (o == Sente ? r <= 1 : r >= 7) continue;   // 最終2段に打てない
            }
            if (kind == Pawn) { // 二歩
                bool two = false;
                for (int rr = 0; rr < 9; ++rr) {
                    int t = sq[idx(rr, c)];
                    if (t != NONE && pc::owner(t) == o && pc::kind(t) == Pawn && !pc::promoted(t)) { two = true; break; }
                }
                if (two) continue;
            }
            Move m; m.from = NONE; m.to = s; m.drop = kind;
            out.push_back(m);
        }
    }
}

void applyMove(std::array<int, SQUARES>& sq, Hands& hand,
               const Move& m, Turn mover) {
    if (m.from == NONE) {
        hand[mover][m.drop]--;
        sq[m.to] = pc::make(mover, m.drop, false);
    } else {
        int p = sq[m.from];
        int t = sq[m.to];
        if (t != NONE) {
            int cap = pc::unpromote(t);
            hand[mover][pc::kind(cap)]++;
        }
        // 自分の駒は成った状態を維持したまま移動させる(unpromoteしない)
        int np = m.promote ? pc::promote(p) : p;
        sq[m.from] = NONE;
        sq[m.to] = np;
    }
}

bool kingSafeAfter(const std::array<int, SQUARES>& sqIn, const Hands& handIn,
                   const Move& m, Turn mover) {
    auto sq = sqIn;
    auto hand = handIn;
    applyMove(sq, hand, m, mover);
    for (int s = 0; s < SQUARES; ++s)
        if (sq[s] != NONE && pc::kind(sq[s]) == King && pc::owner(sq[s]) == mover)
            return !isAttacked(sq, s, pc::other(mover));
    return true;  // 玉がない局面(将棋として成立しないが通す)
}

bool hasAnyKingSafeReply(const std::array<int, SQUARES>& sq, const Hands& hand, Turn t) {
    std::vector<Move> pseudo;
    for (int s = 0; s < SQUARES; ++s) {
        int p = sq[s];
        if (p != NONE && pc::owner(p) == t) genPseudoFrom(sq, s, pseudo);
    }
    genPseudoDrops(sq, hand, t, pseudo);
    for (const auto& m : pseudo)
        if (kingSafeAfter(sq, hand, m, t)) return true;
    return false;
}

} // namespace detail

// ================= Board =================

Board::Board() { setStandard(); }

void Board::setStandard() {
    square_.fill(NONE);
    for (auto& h : hand_) h.fill(0);
    turn_ = Sente;
    moveNumber_ = 1;
    undoStack_.clear();
    posKeys_.clear();
    checkFlags_.clear();

    const Kind back[9] = { Lance, Knight, Silver, Gold, King, Gold, Silver, Knight, Lance };
    for (int c = 0; c < 9; ++c) {
        square_[idx(8, c)] = pc::make(Sente, back[c]);
        square_[idx(6, c)] = pc::make(Sente, Pawn);
        square_[idx(0, c)] = pc::make(Gote, back[c]);
        square_[idx(2, c)] = pc::make(Gote, Pawn);
    }
    square_[idx(7, 7)] = pc::make(Sente, Rook);    // ２八
    square_[idx(7, 1)] = pc::make(Sente, Bishop);  // ８八
    square_[idx(1, 1)] = pc::make(Gote, Rook);     // ８二
    square_[idx(1, 7)] = pc::make(Gote, Bishop);   // ２二
    posKeys_.push_back(posKey());
    checkFlags_.push_back(0);
}

int Board::handTotal(Turn t) const {
    int n = 0;
    for (int k = 0; k < 7; ++k) n += hand_[t][k];
    return n;
}

int Board::kingSquare(Turn t) const {
    for (int s = 0; s < SQUARES; ++s)
        if (square_[s] != NONE && pc::kind(square_[s]) == King && pc::owner(square_[s]) == t)
            return s;
    return NONE;
}

bool Board::inCheck(Turn t) const {
    int k = kingSquare(t);
    return k != NONE && detail::isAttacked(square_, k, pc::other(t));
}

bool Board::isAttacked(int sq, Turn by) const {
    return detail::isAttacked(square_, sq, by);
}

std::vector<Move> Board::legalMovesFor(Turn t) {
    std::vector<Move> pseudo;
    for (int s = 0; s < SQUARES; ++s) {
        int p = square_[s];
        if (p != NONE && pc::owner(p) == t) detail::genPseudoFrom(square_, s, pseudo);
    }
    detail::genPseudoDrops(square_, hand_, t, pseudo);

    std::vector<Move> legal;
    legal.reserve(pseudo.size());
    for (const auto& m : pseudo) {
        if (!detail::kingSafeAfter(square_, hand_, m, t)) continue;
        // 打ち歩詰め: 歩を打って王手がかかり、かつ相手が詰むなら禁止
        if (m.isDrop() && m.drop == Pawn) {
            auto sq = square_;
            auto hand = hand_;
            detail::applyMove(sq, hand, m, t);
            Turn opp = pc::other(t);
            int oppKing = NONE;
            for (int s = 0; s < SQUARES; ++s)
                if (sq[s] != NONE && pc::kind(sq[s]) == King && pc::owner(sq[s]) == opp) oppKing = s;
            if (oppKing != NONE && detail::isAttacked(sq, oppKing, t)) {
                if (!detail::hasAnyKingSafeReply(sq, hand, opp)) continue;
            }
        }
        legal.push_back(m);
    }
    return legal;
}

std::vector<Move> Board::legalMoves() { return legalMovesFor(turn_); }

std::vector<Move> Board::legalMovesFrom(int from) {
    Turn t = square_[from] == NONE ? turn_ : pc::owner(square_[from]);
    std::vector<Move> all = legalMovesFor(t);
    std::vector<Move> out;
    std::copy_if(all.begin(), all.end(), std::back_inserter(out),
                 [&](const Move& m) { return m.from == from; });
    return out;
}

std::vector<Kind> Board::legalDropsAt(int to) {
    std::vector<Move> all = legalMoves();
    std::vector<Kind> out;
    for (const auto& m : all)
        if (m.isDrop() && m.to == to) out.push_back(m.drop);
    return out;
}

bool Board::isLegal(const Move& m) {
    for (const auto& x : legalMoves()) {
        if (x.to != m.to) continue;
        if (m.isDrop()) { if (x.isDrop() && x.drop == m.drop) return true; }
        else if (!x.isDrop() && x.from == m.from && x.promote == m.promote) return true;
    }
    return false;
}

bool Board::hasAnyLegalMove(Turn t) {
    return detail::hasAnyKingSafeReply(square_, hand_, t);
    // 注: 打ち歩詰め禁止を跨いでいる合法手のみ、は極めて稀なケースのため無視
}

bool Board::mated(Turn t) { return inCheck(t) && !hasAnyLegalMove(t); }

void Board::make(const Move& m) {
    undoStack_.push_back(Snapshot{square_, hand_, turn_, moveNumber_});
    detail::applyMove(square_, hand_, m, turn_);
    turn_ = pc::other(turn_);
    moveNumber_++;
    posKeys_.push_back(posKey());
    checkFlags_.push_back(inCheck(turn_) ? 1 : 0);   // 打った手が王手になったか
}

void Board::unmake() {
    if (undoStack_.empty()) return;
    const Snapshot& s = undoStack_.back();
    square_ = s.sq;
    hand_ = s.hand;
    turn_ = s.turn;
    moveNumber_ = s.moveNumber;
    undoStack_.pop_back();
    if (!posKeys_.empty()) posKeys_.pop_back();
    if (checkFlags_.size() == posKeys_.size() + 1) checkFlags_.pop_back();
}

void Board::clearHistory() {
    undoStack_.clear();
    posKeys_.clear();
    posKeys_.push_back(posKey());
    checkFlags_.clear();
    checkFlags_.push_back(0);
}

int Board::repetitionCount() const {
    if (posKeys_.empty()) return 0;
    return static_cast<int>(std::count(posKeys_.begin(), posKeys_.end(), posKeys_.back()));
}

int Board::perpetualCheckCauser() const {
    // 直近4回の同一局面をつなぐ全着手を調べる。4回目が王手を解除した
    // 局面でも、相手側のすべての着手が王手ならその側の反則負け。
    const int n = static_cast<int>(posKeys_.size());
    if (n < 2 || checkFlags_.size() != static_cast<size_t>(n)) return -1;
    const int last = n - 1;
    const std::string& key = posKeys_[last];
    int first = -1;
    int occurrences = 0;
    for (int i = last; i >= 0; --i) {
        if (posKeys_[i] == key && ++occurrences == 4) { first = i; break; }
    }
    if (first < 0) return -1;
    bool allChecks[2] = { true, true };
    for (int i = first + 1; i <= last; ++i) {
        const int mover = int(turn_) ^ ((last - i) & 1) ^ 1;
        if (!checkFlags_[i]) allChecks[mover] = false;
    }
    for (Turn t : { Sente, Gote })
        if (allChecks[int(t)]) return int(t);
    return -1;
}

int Board::points(Turn t) const {
    // 简化入玉判定の点数: 王以外の駒(盤上+持駒)を各1点
    int p = 0;
    for (int s = 0; s < SQUARES; ++s) {
        int q = square_[s];
        if (q != NONE && pc::owner(q) == t && pc::kind(q) != King) ++p;
    }
    return p + handTotal(t);
}

bool Board::bothKingsInEnemyZone() const {
    int ks = kingSquare(Sente), kg = kingSquare(Gote);
    if (ks == NONE || kg == NONE) return false;
    int rs = ks / 9, rg = kg / 9;   // r=0 が一段目(後手陣)
    return rs <= 2 && rg >= 6;
}

std::string Board::sfen() const {
    static const char kLetter[8] = { 'p', 'l', 'n', 's', 'g', 'b', 'r', 'k' };
    std::string s;
    for (int r = 0; r < 9; ++r) {
        int empty = 0;
        for (int c = 0; c < 9; ++c) {
            int p = square_[idx(r, c)];
            if (p == NONE) { ++empty; continue; }
            if (empty) { s += char('0' + empty); empty = 0; }
            if (pc::promoted(p)) s += '+';
            char base = kLetter[pc::kind(p)];
            s += pc::owner(p) == Sente ? static_cast<char>(std::toupper(base)) : base;
        }
        if (empty) s += char('0' + empty);
        if (r < 8) s += '/';
    }
    s += turn_ == Sente ? " b " : " w ";
    std::string hands;
    bool any = false;
    for (Turn t : { Sente, Gote }) {
        for (Kind k : { Pawn, Lance, Knight, Silver, Gold, Bishop, Rook }) {
            int n = hand_[t][k];
            if (n <= 0) continue;
            if (n > 1) hands += std::to_string(n);
            hands += detail::handChar(t, k);
            any = true;
        }
    }
    s += any ? hands : "-";
    s += " " + std::to_string(moveNumber_);
    return s;
}

int Board::sfenKind(char ch) {
    switch (std::tolower(ch)) {
    case 'r': return Rook; case 'b': return Bishop; case 'g': return Gold;
    case 's': return Silver; case 'n': return Knight; case 'l': return Lance;
    case 'p': return Pawn; case 'o': case 'k': return King;
    default: return NoKind;
    }
}

bool Board::loadSfen(const std::string& sfen) {
    std::vector<std::string> tok;
    std::string cur;
    for (char ch : sfen) {
        if (ch == ' ' || ch == '\t') { if (!cur.empty()) { tok.push_back(cur); cur.clear(); } }
        else cur += ch;
    }
    if (!cur.empty()) tok.push_back(cur);
    if (tok.size() < 4) return false;
    if (tok[1] != "w" && tok[1] != "b") return false;

    std::array<int, SQUARES> sq;
    sq.fill(NONE);
    int r = 0, c = 0;
    bool prom = false;
    for (char ch : tok[0]) {
        if (ch == '+') { prom = true; continue; }
        if (ch == '/') { if (++r > 8) return false; c = 0; prom = false; continue; }
        if (std::isdigit(static_cast<unsigned char>(ch))) { c += ch - '0'; prom = false; continue; }
        if (c > 8) return false;
        int k = sfenKind(ch);
        if (k == NoKind) return false;
        Turn o = std::isupper(static_cast<unsigned char>(ch)) ? Sente : Gote;
        sq[idx(r, c++)] = pc::make(o, static_cast<Kind>(k), prom);
        prom = false;
    }
    if (r != 8 || c != 9) return false;

    Hands hands;
    for (auto& h : hands) h.fill(0);
    const std::string& handTok = tok[2];
    if (handTok != "-" && !handTok.empty()) {
        size_t i = 0;
        while (i < handTok.size()) {
            int n = 0;
            while (i < handTok.size() && std::isdigit(static_cast<unsigned char>(handTok[i])))
                n = n * 10 + (handTok[i++] - '0');
            if (i >= handTok.size()) return false;
            int k = sfenKind(handTok[i]);
            if (k == NoKind || k == King) return false;
            Turn o = detail::handTurnFromChar(handTok[i]);
            ++i;
            if (n == 0) n = 1;
            hands[o][static_cast<Kind>(k)] += n;
        }
    }

    square_ = sq;
    hand_ = hands;
    turn_ = tok[1] == "b" ? Sente : Gote;
    try { moveNumber_ = std::max(1, std::stoi(tok[3])); } catch (...) { return false; }
    undoStack_.clear();
    posKeys_.clear();
    posKeys_.push_back(posKey());
    checkFlags_.clear();
    checkFlags_.push_back(0);
    return true;
}

std::string Board::posKey() const {
    std::string k;
    for (int s = 0; s < SQUARES; ++s)
        k += square_[s] == NONE ? '.' : char('A' + (square_[s] & 0x1F));
    for (int t = 0; t < 2; ++t)
        for (int i = 0; i < 7; ++i) k += char('A' + std::min(25, hand_[t][i]));
    k += turn_ == Sente ? 'b' : 'w';
    return k;
}

std::string Board::usiCoord(int s) {
    int file = 9 - colOf(s);
    return std::string(1, char('0' + file)) + char('a' + rowOf(s));
}

bool Board::parseUsiCoord(const std::string& str, int& s) {
    if (str.size() != 2) return false;
    if (str[0] < '1' || str[0] > '9' || str[1] < 'a' || str[1] > 'i') return false;
    int file = str[0] - '0';
    s = idx(str[1] - 'a', 9 - file);
    return true;
}

std::string Board::usiMove(const Move& m) const {
    if (m.isDrop()) {
        char letter = '?';
        switch (m.drop) {
        case Pawn: letter = 'P'; break; case Lance: letter = 'L'; break;
        case Knight: letter = 'N'; break; case Silver: letter = 'S'; break;
        case Gold: letter = 'G'; break; case Bishop: letter = 'B'; break;
        case Rook: letter = 'R'; break; default: break;
        }
        return std::string(1, letter) + "*" + usiCoord(m.to);
    }
    return usiCoord(m.from) + usiCoord(m.to) + (m.promote ? "+" : "");
}

bool Board::parseUsiMove(const std::string& raw, Move& m) {
    std::string s;
    for (char ch : raw)
        if (!std::isspace(static_cast<unsigned char>(ch))) s += ch;
    m = Move();
    if (s.empty()) return false;
    // 打ち: 「P*7g」形式(USI標準)。旧形式「+7g」も許容(駒種は呼び出し側で盤面から解決)
    if (s.size() == 4 && s[1] == '*') {
        Kind k = NoKind;
        switch (std::toupper(static_cast<unsigned char>(s[0]))) {
        case 'P': k = Pawn; break; case 'L': k = Lance; break; case 'N': k = Knight; break;
        case 'S': k = Silver; break; case 'G': k = Gold; break; case 'B': k = Bishop; break;
        case 'R': k = Rook; break; default: return false;
        }
        int to;
        if (!parseUsiCoord(s.substr(2), to)) return false;
        m.to = to;
        m.drop = k;
        return true;
    }
    if (s[0] == '+') {
        int to;
        if (!parseUsiCoord(s.substr(1), to)) return false;
        m.to = to;
        m.drop = NoKind;
        return true;
    }
    if (s.size() < 4) return false;
    int from, to;
    if (!parseUsiCoord(s.substr(0, 2), from)) return false;
    if (!parseUsiCoord(s.substr(2, 2), to)) return false;
    m.from = from; m.to = to;
    m.promote = s.size() > 4 && s[4] == '+';
    return true;
}

} // namespace shogi
