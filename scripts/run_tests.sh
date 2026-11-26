#!/bin/bash
# Docker 内の clickable イメージでユニットテスト (test_kifu/test_game/test_replay) と
# 翻訳カタログ (po/en.po) のチェックをまとめて実行する。
#   使い方: scripts/run_tests.sh
set -eu
BASE="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE=clickable/amd64-ut24.04-1.x-amd64

docker run --rm -v "$BASE":/src "$IMAGE" bash -c '
set -e
cd /src/app/shogi
MOC=/usr/lib/qt5/bin/moc
QT=$(pkg-config --cflags Qt5Core)
QTL=$(pkg-config --libs Qt5Core)
INC="-Isrc -Isrc/shogi -Isrc/kifu -Isrc/engine"
mkdir -p /tmp/b

echo "== msgfmt po/en.po =="
mkdir -p /tmp/loc/en/LC_MESSAGES
msgfmt po/en.po -o /tmp/loc/en/LC_MESSAGES/shogi.ow.mo
msgfmt -c po/en.po -o /dev/null
cat > /tmp/probe.c <<"EOF"
#include <stdio.h>
#include <locale.h>
#include <libintl.h>
int main(void){ setlocale(LC_ALL,""); bindtextdomain("shogi.ow","/tmp/loc");
  textdomain("shogi.ow");
  printf("Q1=%s\nQ2=%s\nQ3=%s\n", dgettext("shogi.ow","対局"),
         dgettext("shogi.ow","先手が時間切れ — 後手の勝ち"),
         dgettext("shogi.ow","未収録")); return 0; }
EOF
gcc -o /tmp/probe /tmp/probe.c
echo "-- LANG=en_US.UTF-8:"; LANG=en_US.UTF-8 /tmp/probe
echo "-- LANG=ja_JP.UTF-8:"; LANG=ja_JP.UTF-8 /tmp/probe

echo "== moc =="
$MOC $INC src/game.h -o /tmp/b/moc_game.cpp
$MOC src/engine/usiengine.h -o /tmp/b/moc_usiengine.cpp
SRC="src/game.cpp src/kifu/kifu.cpp src/shogi/board.cpp src/engine/usiengine.cpp /tmp/b/moc_game.cpp /tmp/b/moc_usiengine.cpp"

echo "== compile tests =="
for t in kifu game replay; do
  g++ -std=c++17 -fPIC -O1 $QT $INC tests/test_$t.cpp $SRC $QTL -o /tmp/b/test_$t
done

echo "== run test_kifu =="
/tmp/b/test_kifu
echo "== run test_game =="
timeout 240 /tmp/b/test_game ./engines/tsuyou11_x86_64 tsuyou11
echo "== run test_replay =="
timeout 420 /tmp/b/test_replay ./engines/tsuyou11_x86_64 tsuyou11
echo ALL-OK'
