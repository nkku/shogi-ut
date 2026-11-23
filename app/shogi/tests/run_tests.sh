#!/bin/bash
# Usage: bash tests/run_tests.sh [ENGINE_PATH ENGINE_ID ...]
# Example: bash tests/run_tests.sh "$PWD/engines/yaneuraou_x86_64" "" "$PWD/engines/suisho5_x86_64" suisho5
set -euo pipefail
if (( $# % 2 )); then
    echo 'Usage: bash tests/run_tests.sh [ENGINE_PATH ENGINE_ID ...]' >&2
    exit 2
fi
task_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
test_build_dir=${SHOGI_TEST_BUILD_DIR:-$(mktemp -d /tmp/shogi-tests.XXXXXX)}
mkdir -p "$test_build_dir"
read -r -a qt_cflags <<< "$(pkg-config --cflags Qt5Core)"
read -r -a qt_libs <<< "$(pkg-config --libs Qt5Core)"
compiler=${CXX:-g++}
moc_command=${MOC:-moc}
"$moc_command" "$task_root/src/game.h" -o "$test_build_dir/moc_game.cpp"
"$moc_command" "$task_root/src/engine/usiengine.h" -o "$test_build_dir/moc_usiengine.cpp"
objects=()
for source in "$task_root/src/game.cpp" "$task_root/src/engine/usiengine.cpp" \
              "$task_root/src/shogi/board.cpp" "$task_root/src/kifu/kifu.cpp" \
              "$test_build_dir/moc_game.cpp" "$test_build_dir/moc_usiengine.cpp"; do
    object="$test_build_dir/$(basename -- "$source" .cpp).o"
    "$compiler" -std=c++17 -O1 -g -fPIC "${qt_cflags[@]}" \
        -I"$task_root/src" -I"$task_root/src/engine" -c "$source" -o "$object"
    objects+=("$object")
done
tests=(board promote rules kifu clock usi)
if (( $# )); then tests+=(game eval); fi
for test in "${tests[@]}"; do
    "$compiler" -std=c++17 -O1 -g -fPIC "${qt_cflags[@]}" \
        "$task_root/tests/test_${test}.cpp" "${objects[@]}" "${qt_libs[@]}" \
        -o "$test_build_dir/test_${test}"
done
for test in board promote rules kifu clock usi; do
    echo "Running test_${test}"
    timeout 30 "$test_build_dir/test_${test}"
done
while (( $# )); do
    for test in game eval; do
        echo "Running test_${test}: $1 ${2}"
        timeout 160 "$test_build_dir/test_${test}" "$1" "$2"
    done
    shift 2
done
echo 'ALL TESTS PASSED'
