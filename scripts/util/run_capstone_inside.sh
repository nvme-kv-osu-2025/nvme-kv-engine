#!/usr/bin/env bash
#
# run_capstone_inside.sh
#
# Container-side workflow invoked by scripts/run_capstone_benchmarks.sh.
# Builds the project, wipes prior generated results, runs the capstone
# benchmark matrix, and produces a mentor-ready summary under
# /user/benchmarks/results/latest/.

set -uo pipefail

MODE="${1:-quick}"

PROJECT_DIR="/user"
RESULTS_ROOT="$PROJECT_DIR/benchmarks/results"
RESULTS_DIR="$RESULTS_ROOT/latest"
RAW_DIR="$RESULTS_DIR/raw"
FIO_DIR="$RESULTS_DIR/fio"
BENCH_BUILD_DIR="$PROJECT_DIR/build/benchmarks"
KV_STRESS_BIN="$BENCH_BUILD_DIR/bench_kv_stress"
KV_CAPACITY_BIN="$BENCH_BUILD_DIR/bench_kv_capacity"

DEVICES_4="/dev/kvemul0,/dev/kvemul1,/dev/kvemul2,/dev/kvemul3"
DEVICE_0="/dev/kvemul0"

# Workload knobs differ between quick and full presets.
case "$MODE" in
  quick)
    SINGLE_DURATION=10
    SINGLE_THREADS=4
    MULTI_DURATION=10
    MULTI_THREADS=8
    KEYSPACE=20000
    PREPOPULATE_VALUE=4096
    CAP_KEY_SIZES="16,64,255"
    CAP_VALUE_SIZES="512,4096,65536"
    CAP_MAX_KEYS=20000
    CAP_MAX_SECONDS=20
    FIO_MODE="quick"
    LARGE_VALUE_DURATION=8
    LARGE_VALUE_KEYSPACE=2000
    ;;
  full)
    SINGLE_DURATION=30
    SINGLE_THREADS=8
    MULTI_DURATION=30
    MULTI_THREADS=16
    KEYSPACE=100000
    PREPOPULATE_VALUE=4096
    CAP_KEY_SIZES="16,64,128,255"
    CAP_VALUE_SIZES="512,4096,65536,1048576,2097152"
    CAP_MAX_KEYS=200000
    CAP_MAX_SECONDS=120
    FIO_MODE="full"
    LARGE_VALUE_DURATION=20
    LARGE_VALUE_KEYSPACE=4000
    ;;
  *)
    echo "Unknown mode: $MODE" >&2
    exit 2
    ;;
esac

echo "==> [1/7] Build project"
bash "$PROJECT_DIR/scripts/util/build.sh"

echo "==> [2/7] Ensure fio + GNU time are installed"
NEEDED_PKGS=()
command -v fio >/dev/null 2>&1 || NEEDED_PKGS+=(fio)
[[ -x /usr/bin/time ]] || NEEDED_PKGS+=(time)
if (( ${#NEEDED_PKGS[@]} > 0 )); then
  apt-get update >/dev/null
  apt-get install -y "${NEEDED_PKGS[@]}" >/dev/null
fi
TIME_BIN=""
if [[ -x /usr/bin/time ]]; then
  TIME_BIN="/usr/bin/time -v"
fi
fio --version | head -n 1

echo "==> [3/7] Reset results directory"
rm -rf "$RESULTS_DIR"
mkdir -p "$RAW_DIR" "$FIO_DIR"
# Also wipe legacy fio output so future runs can't be confused with stale runs.
rm -rf "$PROJECT_DIR/benchmarks/fio/results"
mkdir -p "$PROJECT_DIR/benchmarks/fio/results"

KV_STRESS_CSV="$RESULTS_DIR/kv_stress.csv"
KV_CAPACITY_CSV="$RESULTS_DIR/kv_capacity.csv"

echo "==> [4/7] Setup emulated SSDs"
# shellcheck disable=SC1091
source "$PROJECT_DIR/scripts/setup_emulated_ssds.sh" 4 "$PROJECT_DIR"
export KVSSD_EMU_CONFIGFILE

write_metadata() {
  local meta="$RESULTS_DIR/metadata.json"
  python3 - "$meta" "$MODE" <<'PY'
import json, os, platform, subprocess, sys, time
out = sys.argv[1]
mode = sys.argv[2]

def cmd(*a):
    try:
        return subprocess.check_output(a, stderr=subprocess.STDOUT,
                                       text=True).strip()
    except Exception as e:
        return f"<err: {e}>"

data = {
    "mode": mode,
    "captured_at_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    "uname": platform.uname()._asdict(),
    "cpu_count": os.cpu_count(),
    "fio_version": cmd("fio", "--version").splitlines()[0]
        if cmd("fio", "--version") else None,
    "kvssd_devices": [
        os.environ.get(f"KVSSD_DEVICE_PATH_{i}") for i in range(4)
    ],
    "emulator_config": os.environ.get("KVSSD_EMU_CONFIGFILE"),
}
with open(out, "w") as fp:
    json.dump(data, fp, indent=2, default=str)
PY
}

write_metadata

run_kv_stress() {
  local label="$1"; shift
  local log="$RAW_DIR/${label}.log"
  echo "  -> kv_stress :: $label"
  set +e
  if [[ -n "$TIME_BIN" ]]; then
    $TIME_BIN "$KV_STRESS_BIN" \
      --label "$label" \
      --csv "$KV_STRESS_CSV" \
      --summary "$RAW_DIR/${label}.summary.txt" \
      "$@" >"$log" 2>&1
  else
    "$KV_STRESS_BIN" \
      --label "$label" \
      --csv "$KV_STRESS_CSV" \
      --summary "$RAW_DIR/${label}.summary.txt" \
      "$@" >"$log" 2>&1
  fi
  local rc=$?
  set -e
  if [[ $rc -ne 0 ]]; then
    echo "     kv_stress[$label] exit=$rc (see $log)"
  fi
}

run_kv_capacity() {
  local label="$1"; shift
  local log="$RAW_DIR/${label}.log"
  echo "  -> kv_capacity :: $label"
  set +e
  if [[ -n "$TIME_BIN" ]]; then
    $TIME_BIN "$KV_CAPACITY_BIN" \
      --label "$label" \
      --csv "$KV_CAPACITY_CSV" \
      --summary "$RAW_DIR/${label}.summary.txt" \
      "$@" >"$log" 2>&1
  else
    "$KV_CAPACITY_BIN" \
      --label "$label" \
      --csv "$KV_CAPACITY_CSV" \
      --summary "$RAW_DIR/${label}.summary.txt" \
      "$@" >"$log" 2>&1
  fi
  local rc=$?
  set -e
  if [[ $rc -ne 0 ]]; then
    echo "     kv_capacity[$label] exit=$rc (see $log)"
  fi
}

echo "==> [5/7] KV engine stress (single device)"
run_kv_stress single_write_4k \
  --workload write --devices "$DEVICE_0" \
  --threads "$SINGLE_THREADS" --duration-sec "$SINGLE_DURATION" --warmup-sec 1 \
  --key-min 16 --key-max 16 \
  --value-min "$PREPOPULATE_VALUE" --value-max "$PREPOPULATE_VALUE" \
  --keyspace "$KEYSPACE"

run_kv_stress single_read_4k \
  --workload read --devices "$DEVICE_0" \
  --threads "$SINGLE_THREADS" --duration-sec "$SINGLE_DURATION" --warmup-sec 1 \
  --key-min 16 --key-max 16 \
  --value-min "$PREPOPULATE_VALUE" --value-max "$PREPOPULATE_VALUE" \
  --keyspace "$KEYSPACE"

run_kv_stress single_mixed_70r_4k \
  --workload mixed --read-percent 70 --devices "$DEVICE_0" \
  --threads "$SINGLE_THREADS" --duration-sec "$SINGLE_DURATION" --warmup-sec 1 \
  --key-min 16 --key-max 16 \
  --value-min "$PREPOPULATE_VALUE" --value-max "$PREPOPULATE_VALUE" \
  --keyspace "$KEYSPACE"

run_kv_stress single_delete \
  --workload delete --devices "$DEVICE_0" \
  --threads "$SINGLE_THREADS" --duration-sec "$SINGLE_DURATION" --warmup-sec 1 \
  --key-min 16 --key-max 16 \
  --value-min 1024 --value-max 1024 \
  --keyspace "$KEYSPACE"

run_kv_stress single_exists \
  --workload exists --devices "$DEVICE_0" \
  --threads "$SINGLE_THREADS" --duration-sec "$SINGLE_DURATION" --warmup-sec 1 \
  --key-min 16 --key-max 16 \
  --value-min 1024 --value-max 1024 \
  --keyspace "$KEYSPACE"

run_kv_stress single_large_value \
  --workload mixed --read-percent 70 --devices "$DEVICE_0" \
  --threads 2 --duration-sec "$LARGE_VALUE_DURATION" --warmup-sec 0 \
  --key-min 32 --key-max 32 \
  --value-min 524288 --value-max 524288 \
  --keyspace "$LARGE_VALUE_KEYSPACE"

run_kv_stress single_small_value \
  --workload mixed --read-percent 70 --devices "$DEVICE_0" \
  --threads "$SINGLE_THREADS" --duration-sec "$SINGLE_DURATION" --warmup-sec 1 \
  --key-min 16 --key-max 16 \
  --value-min 64 --value-max 64 \
  --keyspace "$KEYSPACE"

echo "==> Edge case scenarios"
run_kv_stress edge_cases_single \
  --workload edge --devices "$DEVICE_0" \
  --threads 1 \
  --key-min 4 --key-max 255 \
  --value-min 1 --value-max 2097152 \
  --keyspace 1

echo "==> KV engine stress (multi device, 4-way sharded)"
run_kv_stress multi_write \
  --workload write --devices "$DEVICES_4" \
  --threads "$MULTI_THREADS" --duration-sec "$MULTI_DURATION" --warmup-sec 1 \
  --key-min 16 --key-max 16 \
  --value-min "$PREPOPULATE_VALUE" --value-max "$PREPOPULATE_VALUE" \
  --keyspace "$KEYSPACE"

run_kv_stress multi_mixed_70r_4k \
  --workload mixed --read-percent 70 --devices "$DEVICES_4" \
  --threads "$MULTI_THREADS" --duration-sec "$MULTI_DURATION" --warmup-sec 1 \
  --key-min 16 --key-max 16 \
  --value-min "$PREPOPULATE_VALUE" --value-max "$PREPOPULATE_VALUE" \
  --keyspace "$KEYSPACE"

run_kv_stress multi_read \
  --workload read --devices "$DEVICES_4" \
  --threads "$MULTI_THREADS" --duration-sec "$MULTI_DURATION" --warmup-sec 1 \
  --key-min 16 --key-max 16 \
  --value-min "$PREPOPULATE_VALUE" --value-max "$PREPOPULATE_VALUE" \
  --keyspace "$KEYSPACE"

echo "==> Capacity sweep"
run_kv_capacity capacity_single \
  --devices "$DEVICE_0" \
  --key-sizes "$CAP_KEY_SIZES" \
  --value-sizes "$CAP_VALUE_SIZES" \
  --max-keys "$CAP_MAX_KEYS" \
  --max-seconds "$CAP_MAX_SECONDS" \
  --stop-on-failure 1

run_kv_capacity capacity_multi \
  --devices "$DEVICES_4" \
  --key-sizes "$CAP_KEY_SIZES" \
  --value-sizes "$CAP_VALUE_SIZES" \
  --max-keys "$CAP_MAX_KEYS" \
  --max-seconds "$CAP_MAX_SECONDS" \
  --stop-on-failure 1

echo "==> [6/7] FIO multi-device baseline ($FIO_MODE)"
set +e
bash "$PROJECT_DIR/scripts/run_fio_bench.sh" "$FIO_MODE" >"$RAW_DIR/fio_${FIO_MODE}.log" 2>&1
fio_rc=$?
set -e
if [[ $fio_rc -ne 0 ]]; then
  echo "  FIO returned $fio_rc (see $RAW_DIR/fio_${FIO_MODE}.log)"
fi
# Copy fio outputs into the consolidated results bundle.
if compgen -G "$PROJECT_DIR/benchmarks/fio/results/*.json" >/dev/null; then
  cp "$PROJECT_DIR"/benchmarks/fio/results/*.json "$FIO_DIR"/ 2>/dev/null || true
fi
if compgen -G "$PROJECT_DIR/benchmarks/fio/results/*.log" >/dev/null; then
  cp "$PROJECT_DIR"/benchmarks/fio/results/*.log "$FIO_DIR"/ 2>/dev/null || true
fi

echo "==> [7/7] Generate summary"
python3 "$PROJECT_DIR/scripts/summarize_capstone_benchmarks.py" \
  --results-dir "$RESULTS_DIR" \
  --mode "$MODE"

echo "==> Capstone benchmark suite complete."
echo "    Summary: $RESULTS_DIR/summary.md"
