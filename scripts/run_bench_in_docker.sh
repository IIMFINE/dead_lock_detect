#!/usr/bin/env bash
# 宿主入口：在 astribot_x86_2204_2204_develop_container 容器内跑 bench
set -euo pipefail

CONTAINER="${CONTAINER:-astribot_x86_2204_2204_develop_container}"
HOST_REPO="${HOST_REPO:-/home/pan/workspace/code/bubble/dead_lock_detect}"
GUEST_REPO="${GUEST_REPO:-/home/astribot/workspace/code/bubble/dead_lock_detect}"

TS="$(date +%Y%m%d-%H%M)"
OUT_HOST="$HOST_REPO/bench_result-$TS"
OUT_GUEST="$GUEST_REPO/bench_result-$TS"

if ! docker ps --format '{{.Names}}' | grep -qx "$CONTAINER"; then
    echo "error: container '$CONTAINER' is not running" >&2
    exit 1
fi

mkdir -p "$OUT_HOST"

echo "[host] running bench in container '$CONTAINER'"
echo "[host] out: $OUT_HOST"

docker exec \
    -e ROUNDS="${ROUNDS:-3}" \
    -e WARMUP="${WARMUP:-3}" \
    -e RUN_S="${RUN_S:-30}" \
    -e PRODUCERS="${PRODUCERS:-4}" \
    -e CONSUMERS="${CONSUMERS:-4}" \
    -e MAX_CORES="${MAX_CORES:-8}" \
    -e TASKSET_CORES="${TASKSET_CORES:-0-7}" \
    -e DEADLOCK_RING_BYTES="${DEADLOCK_RING_BYTES:-}" \
    "$CONTAINER" bash "$GUEST_REPO/tests/bench_run.sh" "$OUT_GUEST"

docker exec "$CONTAINER" python3 "$GUEST_REPO/scripts/bench_report.py" "$OUT_GUEST"

echo "[host] report: $OUT_HOST/report.md"
