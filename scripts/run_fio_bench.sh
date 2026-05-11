#!/usr/bin/env bash
# run_fio_bench.sh
#
# Runs FIO benchmarks with multiple simulated SSDs (file-based).
#
# Usage:
#   bash /user/scripts/run_fio_bench.sh [quick|full]
#
#   quick  – 30s random read/write across 4 SSDs (default)
#   full   – comprehensive benchmark suite (sequential + random workloads)
#
# Prerequisites:
#   apt-get install -y fio    (already installed if you ran setup_fio_spdk.sh)

set -euo pipefail

FIO_DIR="/user/benchmarks/fio"
OUTPUT_DIR="/user/benchmarks/fio/results"
BENCH_DIR="/tmp/fio_bench"

BENCH_MODE="${1:-quick}"

# Verify fio is installed
if ! command -v fio &> /dev/null; then
    echo "ERROR: fio not found. Install with: apt-get install -y fio"
    exit 1
fi

echo "FIO version: $(fio --version)"

# Create directories
mkdir -p "$OUTPUT_DIR" "$BENCH_DIR"

TIMESTAMP=$(date +%Y%m%d_%H%M%S)

case "$BENCH_MODE" in
    quick)
        FIO_FILE="$FIO_DIR/multi_ssd_fileio.fio"
        OUTPUT_FILE="$OUTPUT_DIR/randrw_${TIMESTAMP}"
        echo "=== Running quick multi-SSD random R/W benchmark ==="
        echo "  Config: $FIO_FILE"
        echo "  Output: ${OUTPUT_FILE}.json"
        echo ""
        fio "$FIO_FILE" \
            --output-format=json+ \
            --output="${OUTPUT_FILE}.json" | tee "${OUTPUT_FILE}.log"
        ;;
    full)
        FIO_FILE="$FIO_DIR/multi_ssd_fileio_full.fio"
        OUTPUT_FILE="$OUTPUT_DIR/full_bench_${TIMESTAMP}"
        echo "=== Running full multi-SSD benchmark suite ==="
        echo "  Config: $FIO_FILE"
        echo "  Output: ${OUTPUT_FILE}.json"
        echo "  This will take several minutes..."
        echo ""
        fio "$FIO_FILE" \
            --output-format=json+ \
            --output="${OUTPUT_FILE}.json" | tee "${OUTPUT_FILE}.log"
        ;;
    *)
        echo "Usage: $0 [quick|full]"
        echo "  quick  – 30s random R/W across 4 SSDs (default)"
        echo "  full   – comprehensive benchmark suite"
        exit 1
        ;;
esac

echo ""
echo "=== Benchmark complete ==="
echo "  JSON results: ${OUTPUT_FILE}.json"
echo "  Log output:   ${OUTPUT_FILE}.log"
