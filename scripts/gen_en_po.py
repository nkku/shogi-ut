#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""po/en.po 生成: QML の i18n.tr('..') と C++ の tx(QStringLiteral("..")) から
msgid (日本語原文) を抽出し、EN 翻訳表と突き合わせて en.po を書く。
翻訳漏れ (msgid 未登録) と翻訳表余りをエラーにする。"""
import re, sys, pathlib, subprocess

root = pathlib.Path(__file__).resolve().parent.parent / "app" / "shogi"  # app/shogi
qml = sorted((root / "qml").glob("*.qml"))
cpps = [root / "main.cpp", root / "src" / "game.cpp", root / "src" / "kifu" / "kifu.cpp",
        root / "src" / "engine" / "usiengine.cpp"]

msgids = []
seen = set()
def add(s):
    if s not in seen:
        seen.add(s); msgids.append(s)

pat_qml = re.compile(r"i18n\.tr\('([^']*)'")
for f in qml:
    for m in pat_qml.finditer(f.read_text(encoding="utf-8")):
        add(m.group(1))

pat_cpp = re.compile(r'tx\(QStringLiteral\("([^"]*)"\)\)')
for f in cpps:
    for m in pat_cpp.finditer(f.read_text(encoding="utf-8")):
        add(m.group(1))

# 連結で作られる終局結果の全文 (code にそのままの形では現れない)
for turn in ("先手", "後手"):
    other = "後手" if turn == "先手" else "先手"
    add(f"{turn}の勝ち")
    for prefix in ("投了 — ", "詰み — ", "連続王手の千日手 — ", "相手が投了 — "):
        add(f"{prefix}{turn}の勝ち")
add("先手が時間切れ — 後手の勝ち")
add("後手が時間切れ — 先手の勝ち")
add("千日手")

# --- 翻訳表 ---------------------------------------------------------------
EN = {
    # 共通・UI
    "将棋": "Shogi",
    "対局": "Game",
    "棋譜": "Moves",
    "グラフ": "Graph",
    "モード": "Mode",
    "エンジン": "Engine",
    "評価": "Eval",
    "手数": "Ply",
    "手目": "Move",
    "閉じる": "Close",
    "中止": "Cancel",
    "開始": "Start",
    "待った": "Undo",
    "投了": "Resign",
    "読み込み": "Load",
    "二面打ち": "Two players",
    "CPU先手": "Engine (Sente)",
    "CPU後手": "Engine (Gote)",
    "成りますか？": "Promote?",
    "成る": "Promote",
    "不成": "Don't promote",
    "対局終了": "Game over",
    "対局を始める": "Start a game",
    "新しい対局": "New game",
    "%1手目から対局": "Start from move %1",
    "%1手目以降の棋譜と消費時間は破棄され、選んだ設定で対局を開始します":
        "The moves and clock after move %1 will be discarded, then the game starts with the chosen settings",
    "学習モード (最善手・次善手のヒント)": "Study mode (best & second-best hints)",
    "持時間(分)": "Time (min)",
    "秒読み": "Byo-yomi",
    "CPU戦のみの設定": "Engine game settings only",
    "スレッド": "Threads",
    "エンジンパス(空=同梱の将棋エンジン)": "Engine path (empty = bundled engine)",
    "やねうら王": "YaneuraOu",
    "水匠5": "Suisho5",
    "つようら王11": "TsuyouraOu 11",
    "つようら王11mini": "TsuyouraOu 11 mini",
    # 追加UI文字列
    "%1手目": "Move %1",
    "%1手目閲覧": "Viewing move %1",
    "の持ち駒": "'s pieces in hand",
    "音": "Sound",
    "消": "Mute",
    "時間": "h",
    "分": "m",
    "評価データがありません": "No evaluation data",
    "先手 ": "Sente ",
    "後手 ": "Gote ",
    "手目を閲覧中 (時計は停止)": "Reviewing a move (clock paused)",
    "最新へ": "To latest",
    "ここから指す": "Play from here",
    "その手目へ": "Go to this move",
    # 評価グラフ
    "評価値グラフ": "Evaluation graph",
    "終局評価": "Final eval",
    "最大先手": "Max sente",
    "最大後手": "Max gote",
    "左=初手 → 右=終局。上=先手有利。目盛は300点刻みの駒得 (詰みは上限で截み)。グラフをタップで手目の詳細":
        "Left = start, right = game end. Up = sente advantage. Scale = material in 300-pt steps (mate scores clamped). Tap the graph for move details",
    "再生成中 ": "Regenerating ",
    "この棋譜の評価値が未記録です (再生成 約1.2秒/局面)":
        "No evaluations recorded for this game (~1.2 s per position to regenerate)",
    "再生成": "Regenerate",
    # 棋譜入出力
    "棋譜を開く": "Open game",
    "棋譜ファイルを選ぶ (*.kif / *.sfen)…": "Choose game file (*.kif / *.sfen)…",
    "書き出し (保存先选择ダイアログへ転送)": "Export (opens the save dialog)",
    "KIF保存": "Save KIF",
    "SFEN保存": "Save SFEN",
    "書き出し先": "Save to",
    "棋譜を読み込めませんでした": "Could not load the game",
    # C++: 状態表示
    "先手": "Sente",
    "後手": "Gote",
    "先手の勝ち": "Sente wins",
    "後手の勝ち": "Gote wins",
    "投了 — 先手の勝ち": "Resignation — Sente wins",
    "投了 — 後手の勝ち": "Resignation — Gote wins",
    "詰み — 先手の勝ち": "Checkmate — Sente wins",
    "詰み — 後手の勝ち": "Checkmate — Gote wins",
    "連続王手の千日手 — 先手の勝ち": "Perpetual check — Sente wins",
    "連続王手の千日手 — 後手の勝ち": "Perpetual check — Gote wins",
    "相手が投了 — 先手の勝ち": "Opponent resigned — Sente wins",
    "相手が投了 — 後手の勝ち": "Opponent resigned — Gote wins",
    "先手が時間切れ — 後手の勝ち": "Sente ran out of time — Gote wins",
    "後手が時間切れ — 先手の勝ち": "Gote ran out of time — Sente wins",
    "千日手": "Repetition draw",
    "相手が入玉勝ちを宣言": "Opponent declared an entering-king win",
    "入玉勝ち — %1 (%2-%3点)": "Entering-king win — %1 (%2-%3 pts)",
    "持将棋 (引き分け, %1-%2点)": "Jishogi draw, %1-%2 pts",
    "不正な指し手(%1) — %2": "Illegal move (%1) — %2",
    "の番": " to move",
    " を検討中…": " is analyzing…",
    " が思考中…": " is thinking…",
    "(王手!)": " (Check!)",
    "エンジン起動中…": "Starting engine…",
    "+詰%1": "+M%1",
    "-詰%1": "-M%1",
    # C++: 学習モード・評価
    "最善手": "Best",
    "次善手": "2nd best",
    "あなたの指し手": "Your move",
    " (%1%2点)": " (%1%2 pts)",
    "得": "gain",
    "損": "loss",
    "エンジンが見つからず学習モード off": "Engine not found; study mode off",
    "エンジンが起動していません。対局画面で学習モードをオンにして開始してください":
        "Engine is not running. Turn on study mode on the game screen and press Start",
    "エンジンが指し手を返さないため反則負けとしました":
        "The engine returned no move; the game is a forfeit",
    "USIエンジンが見つかりません。設定でパスを指定してください。":
        "USI engine not found. Set the path in Settings.",
    "USIエンジンが見つかりません。設定を確認してください":
        "USI engine not found. Check Settings.",
    # C++: 棋譜メッセージ
    "初局面を復元できませんでした": "Could not restore the starting position",
    "初局面を復元できません": "Could not restore the starting position",
    "書き込めません: ": "Cannot write: ",
    "ファイルが開けません: ": "Cannot open file: ",
    "不明な駒: ": "Unknown piece: ",
    "不正な手: ": "Illegal move: ",
    "第%1手を解釈できません: %2": "Could not interpret move %1: %2",
    "第%1手の形式が不正: %2": "Bad move format at move %1: %2",
    "第%1手が不正: %2": "Illegal move %1: %2",
    "%1手 読み込みました": "Loaded %1 moves",
    "平手以外の駒落ちには対応していません": "Handicaps other than hirate (even game) are not supported",
    "%1行目を解析できません: %2": "Could not parse line %1: %2",
    "先頭に『同』があります": 'The first move line starts with "same"',
    "%1行目の駒が判別できません: %2": "Could not identify the piece on line %1: %2",
    "指し手がありませんでした": "No moves found",
    "SFENが壊れています": "Malformed SFEN",
    # C++: エンジンログ
    "プロセスエラー: %1 (%2)": "Process error: %1 (%2)",
    "起動失敗: %1": "Failed to start: %1",
}

# 変数経由の tx() / resultText() getter 経由で使われるため抽出 regex にかからない msgid
KNOWN = {"先手", "後手", "相手が入玉勝ちを宣言",
         "エンジンが指し手を返さないため反則負けとしました",
         "USIエンジンが見つかりません。設定でパスを指定してください。"}

missing = [m for m in msgids if m not in EN]
unused = [k for k in EN if k not in seen and k not in KNOWN]
if missing:
    print("翻訳漏れ:", file=sys.stderr)
    for m in missing: print("  " + repr(m), file=sys.stderr)
if unused:
    print("翻訳表余り:", file=sys.stderr)
    for u in unused: print("  " + repr(u), file=sys.stderr)
if missing or unused:
    sys.exit(1)

def esc(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')

out = ['msgid ""',
       'msgstr ""',
       '"Project-Id-Version: shogi.ow\\n"',
       '"Language: en\\n"',
       '"MIME-Version: 1.0\\n"',
       '"Content-Type: text/plain; charset=UTF-8\\n"',
       '"Content-Transfer-Encoding: 8bit\\n"', '']
for m in msgids + [k for k in sorted(KNOWN) if k not in seen]:
    out.append(f'msgid "{esc(m)}"')
    out.append(f'msgstr "{esc(EN[m])}"')
    out.append('')

(root / "po" / "en.po").write_text("\n".join(out), encoding="utf-8")
print(f"po/en.po 書き出し: {len(msgids)} 件")
try:
    subprocess.run(["msgfmt", str(root / "po" / "en.po"), "-o", "/dev/null"], check=True)
    print("msgfmt チェック OK")
except FileNotFoundError:
    print("msgfmt はホストに無いのでスキップ (scripts/run_tests.sh で検証)")
