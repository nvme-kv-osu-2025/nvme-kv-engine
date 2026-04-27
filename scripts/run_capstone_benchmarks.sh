#!/usr/bin/env bash
#
# run_capstone_benchmarks.sh
#
# One-command capstone benchmark suite. Boots the kvssd Docker container,
# rebuilds the project, removes any previously generated benchmark output,
# runs the KV stress, multi-device, capacity, edge, and FIO benchmarks,
# and prints the path of the generated mentor-ready summary.
#
# Usage:
#   ./scripts/run_capstone_benchmarks.sh [quick|full]
#
#   quick (default) – ~3-5 minutes, bounded workload matrix
#   full            – longer durations, fuller capacity sweep, FIO full suite
#
# Results land at:
#   benchmarks/results/latest/summary.md
#   benchmarks/results/latest/kv_stress.csv
#   benchmarks/results/latest/kv_capacity.csv
#   benchmarks/results/latest/fio/*.json
#   benchmarks/results/latest/raw/*

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$(realpath "${BASH_SOURCE[0]}")")" && pwd)"
. "$SCRIPT_DIR"/util/build_docker.sh

MODE="${1:-quick}"
case "$MODE" in
  quick|full) ;;
  *)
    echo "Usage: $0 [quick|full]" >&2
    exit 2
    ;;
esac

CONTAINER_NAME="kvssd-container"

echo "==> Running capstone benchmarks (mode=$MODE) inside $CONTAINER_NAME"
docker exec "$CONTAINER_NAME" bash -lc "/user/scripts/util/run_capstone_inside.sh $MODE"

echo
echo "==> Done. See benchmarks/results/latest/summary.md"
