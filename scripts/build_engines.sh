#!/bin/bash
# 同梱USIエンジンの再現ビルド手順 (YaneuraOu 9.96git / commit 369937c, master 2026-11-22)
#   https://github.com/yaneurao/YaneuraOu
#
# 同梱バイナリと edition:
#   yaneuraou_{x86_64,aarch64}  YANEURAOU_ENGINE_MATERIAL (MATERIAL_LEVEL=1; aarch64は9)
#   suisho5_{x86_64,aarch64}    YANEURAOU_ENGINE_NNUE      + engines/suisho5_eval/nn.bin
#                               (nn.bin は scripts/fetch_suisho5.sh で取得)
#   tsuyou11_{x86_64,aarch64}   YANEURAOU_ENGINE_SFNN_ka2_4096_8_64_progress8
#                               + engines/tsuyou11_eval/ (MIT・FANBOX posts/12761272 配布)
#
# 実行環境: clickable/amd64-ut24.04-1.x-amd64 にクロスツールを入れた自前image。
#   docker create + apt-get install + docker commit で作る (image自体は再配布可だが手順で再現)。
set -eu
BASE="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${BASE}/../yaneuraou-src"
COMMIT=369937c
IMAGE_XDEV=clickable-ut2404-xdev

if [ ! -d "$SRC" ]; then
    git clone https://github.com/yaneurao/YaneuraOu.git "$SRC"
fi
git -C "$SRC" fetch origin
git -C "$SRC" checkout "$COMMIT"

# クロスビルド用image (g++-aarch64-linux-gnu)
docker image inspect "$IMAGE_XDEV" >/dev/null 2>&1 || {
    id=$(docker create clickable/amd64-ut24.04-1.x-amd64 bash -c \
        'apt-get update -qq && apt-get install -y -qq g++-aarch64-linux-gnu >/dev/null')
    docker start -ai -a "$id"
    docker commit "$id" "$IMAGE_XDEV" >/dev/null
    docker rm "$id" >/dev/null
}

# SFNNヘッダ生成 (これをしないと SFNN edition の make がヘッダ不足で全滅する)
docker run --rm -v "$SRC":/build -w /build/source "$IMAGE_XDEV" bash -c \
    'python3 eval/nnue/architectures/nnue_arch_gen.py SFNN_ka2_4096_8_64_progress8 eval/nnue/architectures'

build() { # $1=target_cpu $2=compiler $3=edition $4=out
    docker run --rm -v "$SRC":/build -w /build/source "$IMAGE_XDEV" bash -c \
        "rm -rf ../obj && make -j\$(nproc) CONF=release TARGET=yaneuraou \
         TARGET_CPU=$1 COMPILER=$2 PYTHON=python3 YANEURAOU_EDITION=$3 \
         && cp yaneuraou /build/$4"
}

E=app/shogi/engines
cd "$BASE"
# x86_64 (テスト用ネイティブ。clickにはarm64のみ必須だが同梱される)
build SSE42 g++ YANEURAOU_ENGINE_MATERIAL            build_yaneuraou_x86_64
build SSE42 g++ YANEURAOU_ENGINE_NNUE                build_suisho5_x86_64
build SSE42 g++ YANEURAOU_ENGINE_SFNN_ka2_4096_8_64_progress8 build_tsuyou11_x86_64
# aarch64 クロス (click同梱版。ARMV8ベースライン: dotprod命令を使わないため古いARM機でも動く)
build ARMV8 aarch64-linux-gnu-g++ YANEURAOU_ENGINE_MATERIAL            build_yaneuraou_aarch64
build ARMV8 aarch64-linux-gnu-g++ YANEURAOU_ENGINE_NNUE                build_suisho5_aarch64
build ARMV8 aarch64-linux-gnu-g++ YANEURAOU_ENGINE_SFNN_ka2_4096_8_64_progress8 build_tsuyou11_aarch64

for f in build_yaneuraou_x86_64 build_suisho5_x86_64 build_tsuyou11_x86_64 \
         build_yaneuraou_aarch64 build_suisho5_aarch64 build_tsuyou11_aarch64; do
    mv "$SRC/$f" "$E/${f#build_}"
done
echo "done. binaries in $E"

# 補記:
# - ARMV8_DOTPROD edition は clang++ (--target=aarch64-linux-gnu) + lld でないとビルドできない
#   (g++ では NEON intrinsic の互換性で失敗)。FP5実測では ARMV8 と速度差が無かったため不採用。
# - つようら王11mini評価ファイル (MIT) の元配布:
#   https://storage.yaneu.com/yaneuraou/TuyouraOu11mini-SFNN_ka2_4096_8_64_progress8-epoch0010-20261003.7z
#   nn.bin   sha256 45323c65135da65d6c2c364eea93a5af6a285c27ebfb07eab4afbe737ef74508
#   progress.bin sha256 a2d18f0564639c945b5d6469e9b437cd64a526ad4fbcef02ab1d8f547d08e5f1
