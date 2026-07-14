#!/bin/bash
# Same-session A/B: baseline vs working tree, run interleaved.
#
# The baseline is built in a detached git worktree, NOT by stashing the working
# tree. An earlier version of this script did stash, and a build I kicked off
# while it was mid-run raced it: the pop failed and left box.h reverted to HEAD
# with every other file restored. Nothing that only reads the repo should ever be
# able to do that to it.
#
# Interleaved, not sequential: the CPU heats up over a run and the governor never
# gives the second binary the same clocks as the first. Reported as min-of-N —
# interference can only add time, so the minimum is the honest estimate.
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
BASE_REF=${BASE_REF:-HEAD}
OUT=$ROOT/.ab
WT=$ROOT/.ab/baseline-wt
mkdir -p "$OUT"

cd "$ROOT" || exit 1

# A bench left running from an interrupted run holds a lock on its own .exe, and
# Windows then fails the copy rather than the process. Clear them first.
taskkill //F //IM before.exe //IM after.exe //IM htmlayout_bench.exe > /dev/null 2>&1
git worktree remove --force "$WT" > /dev/null 2>&1

echo "### building working tree" >&2
cmake --build build --config Release --target htmlayout_bench > /dev/null 2>&1 || { echo "AFTER build failed"; exit 1; }
cp build/bench/Release/htmlayout_bench.exe "$OUT/after.exe" || exit 1

echo "### building baseline ($BASE_REF) in a worktree" >&2
git worktree remove --force "$WT" 2>/dev/null
git worktree add --detach --quiet "$WT" "$BASE_REF" || exit 1
cmake -S "$WT" -B "$WT/build" -DCMAKE_BUILD_TYPE=Release > /dev/null 2>&1 || exit 1
cmake --build "$WT/build" --config Release --target htmlayout_bench > /dev/null 2>&1 || { echo "BEFORE build failed"; exit 1; }
cp "$WT/build/bench/Release/htmlayout_bench.exe" "$OUT/before.exe" || exit 1

echo "### running interleaved" >&2
: > "$OUT/raw.txt"
for i in 1 2 3 4 5; do
  for who in before after; do
    "$OUT/$who.exe" 2>&1 | awk -v W="$who" '
      /layout \(cold\)/         {print W, "cold",    $3}
      /layout \(1 leaf dirty\)/ {print W, "leaf",    $5}
      /layout \(resize/         {print W, "resize",  $NF=="ms" ? $(NF-1) : $4}
      /hitTest x1000/           {print W, "hittest", $3}' >> "$OUT/raw.txt"
  done
done
git worktree remove --force "$WT" 2>/dev/null
echo "### done" >&2
