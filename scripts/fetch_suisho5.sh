#!/bin/sh
# 水匠5 評価関数ファイル (nn.bin, 62MB) を公式GitHubリリースから取得する。
# リポジトリには容量の都合で同梱しないため、ビルド前に一度実行すること。
# 出典: https://github.com/yaneurao/YaneuraOu/releases/tag/suisho5 (Suisho5.7z)
set -eu
cd "$(dirname "$0")/.."
DEST=app/shogi/engines/suisho5_eval
URL="https://github.com/yaneurao/YaneuraOu/releases/download/suisho5/Suisho5.7z"
EXPECTED_SHA256=768068f0d534a0603a5d38bcd143de6bbca820d5f1c95a14d40863e5b7892d76

if [ -f "$DEST/nn.bin" ]; then
    echo "既存の nn.bin があります: $DEST/nn.bin"
    exit 0
fi

mkdir -p "$DEST"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
curl -fL -o "$tmp/Suisho5.7z" "$URL"
7z x -y -o"$tmp" "$tmp/Suisho5.7z" >/dev/null 2>&1 || 7za x -y -o"$tmp" "$tmp/Suisho5.7z" >/dev/null
echo "$EXPECTED_SHA256  $tmp/nn.bin" | sha256sum -c -
mv "$tmp/nn.bin" "$DEST/nn.bin"
echo "取得完了: $DEST/nn.bin"
