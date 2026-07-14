#!/bin/bash
# bro-vs-Chromium parity, run twice: bro built against the baseline htmlayout, and
# bro built against this working tree. Byte-for-byte identical output is the bar.
#
# This change rewrote 541 style reads across every formatting context. The failure
# it risks — a rewrite that binds a property to the wrong node — produces layout
# that is subtly wrong, not layout that crashes, and 1840 unit tests are not built
# to catch that. 300 real pages diffed against Chromium are.
#
# Neither half touches the working tree: the baseline is a detached git worktree,
# bro is pointed at it with -DHTMLAYOUT_DIR, and broparity is pointed at the
# resulting binary with $BRO_HEADLESS. An earlier version of the A/B script did
# this by stashing, and a concurrent build raced it into reverting a file.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BASE_REF=${BASE_REF:-HEAD}
WT=$ROOT/.ab/parity-wt
OUT=$ROOT/.ab

mkdir -p "$OUT"
cd "$ROOT" || exit 1

echo "### baseline worktree ($BASE_REF)" >&2
git worktree remove --force "$WT" >/dev/null 2>&1
git worktree add --detach --quiet "$WT" "$BASE_REF" || exit 1

echo "### building bro against BASELINE htmlayout" >&2
# From scratch: the baseline dir is keyed to a worktree path that no longer exists
# after the last run, and CMake will not take a new toolchain on an existing cache.
rm -rf ${BRO_DIR:-/d/projects/bro}/build-parity-base
cmake -S ${BRO_DIR:-/d/projects/bro} -B ${BRO_DIR:-/d/projects/bro}/build-parity-base \
      -DCMAKE_BUILD_TYPE=Release -DHTMLAYOUT_DIR="$WT" \
      -DCMAKE_TOOLCHAIN_FILE=${VCPKG_ROOT:-D:/vcpkg}/scripts/buildsystems/vcpkg.cmake \
      -DVCPKG_TARGET_TRIPLET=x64-windows 2>&1 | tail -20 | grep -i "CMake Error" && { echo "configure failed"; exit 1; }
cmake --build ${BRO_DIR:-/d/projects/bro}/build-parity-base --config Release --target bro-headless 2>&1 \
  | grep -Ei "error C|error LNK" && { echo "BASELINE bro build failed"; exit 1; }

echo "### building bro against WORKING TREE htmlayout" >&2
cmake --build ${BRO_DIR:-/d/projects/bro}/build --config Release --target bro-headless 2>&1 \
  | grep -Ei "error C|error LNK" && { echo "WORKING bro build failed"; exit 1; }

cd ${BROPARITY_DIR:-/d/projects/broparity} || exit 1
echo "### parity: baseline" >&2
BRO_HEADLESS=${BRO_DIR:-/d/projects/bro}/build-parity-base/Release/bro-headless.exe node run.mjs > "$OUT/parity_before.txt" 2>&1
echo "### parity: working tree" >&2
BRO_HEADLESS=${BRO_DIR:-/d/projects/bro}/build/Release/bro-headless.exe        node run.mjs > "$OUT/parity_after.txt" 2>&1

cd "$ROOT" || exit 1
git worktree remove --force "$WT" >/dev/null 2>&1
echo "### done: $(wc -l < "$OUT/parity_before.txt") / $(wc -l < "$OUT/parity_after.txt") lines" >&2
