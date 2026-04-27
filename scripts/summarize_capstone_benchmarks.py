#!/usr/bin/env python3
"""Capstone benchmark summarizer.

Reads CSV/FIO/JSON artifacts produced by run_capstone_inside.sh and emits a
concise mentor-ready Markdown report at <results-dir>/summary.md.

Standard library only.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional


def _try_load_json(path: Path) -> Optional[Dict[str, Any]]:
    if not path.exists():
        return None
    try:
        with path.open() as fp:
            return json.load(fp)
    except Exception as exc:
        print(f"[summary] failed to parse {path}: {exc}", file=sys.stderr)
        return None


def _read_csv(path: Path) -> List[Dict[str, str]]:
    if not path.exists():
        return []
    try:
        with path.open() as fp:
            return list(csv.DictReader(fp))
    except Exception as exc:
        print(f"[summary] failed to read CSV {path}: {exc}", file=sys.stderr)
        return []


def _format_kib(num: float) -> str:
    if num is None:
        return "n/a"
    if num >= 1 << 30:
        return f"{num / (1 << 30):.2f} GiB"
    if num >= 1 << 20:
        return f"{num / (1 << 20):.2f} MiB"
    if num >= 1 << 10:
        return f"{num / (1 << 10):.2f} KiB"
    return f"{num:.0f} B"


def _kv_stress_workload_table(rows: List[Dict[str, str]]) -> List[str]:
    if not rows:
        return ["_No KV stress results available._"]
    timed_rows = [r for r in rows if not r.get("workload", "").startswith("edge:")]
    if not timed_rows:
        return ["_No timed KV stress workloads recorded._"]

    headers = [
        "Label",
        "Workload",
        "Devs",
        "Threads",
        "Dur (s)",
        "Ops",
        "Fail",
        "Success",
        "Throughput",
        "MB/s",
        "Avg us",
        "p50 us",
        "p95 us",
        "p99 us",
        "p99.9 us",
        "Max us",
        "Key",
        "Value",
    ]
    lines = [
        "| " + " | ".join(headers) + " |",
        "|" + "|".join(["---"] * len(headers)) + "|",
    ]
    for r in timed_rows:
        try:
            ops = int(r["ops_completed"])
            fail = int(r["ops_failed"])
            success = float(r["success_rate"]) * 100.0
            tput = float(r["throughput_ops_sec"])
            mbps = float(r["throughput_mb_sec"])
            avg = float(r["avg_latency_us"])
            p50 = float(r["p50_us"])
            p95 = float(r["p95_us"])
            p99 = float(r["p99_us"])
            p999 = float(r["p999_us"])
            mx = float(r["max_latency_us"])
            duration = float(r["duration_sec"])
            threads = r["threads"]
        except Exception:
            continue
        devs = r.get("devices", "")
        dev_count = len(devs.split(",")) if devs else 0
        key_range = f"{r.get('key_min', '')}-{r.get('key_max', '')}"
        val_range = f"{r.get('value_min', '')}-{r.get('value_max', '')}"
        lines.append("| " + " | ".join([
            r.get("label", ""),
            r.get("workload", ""),
            str(dev_count),
            str(threads),
            f"{duration:.2f}",
            f"{ops:,}",
            f"{fail:,}",
            f"{success:.2f}%",
            f"{tput:,.0f}",
            f"{mbps:.2f}",
            f"{avg:.1f}",
            f"{p50:.1f}",
            f"{p95:.1f}",
            f"{p99:.1f}",
            f"{p999:.1f}",
            f"{mx:.1f}",
            key_range,
            val_range,
        ]) + " |")
    return lines


def _edge_table(rows: List[Dict[str, str]]) -> List[str]:
    edge_rows = [r for r in rows if r.get("workload", "").startswith("edge:")]
    if not edge_rows:
        return ["_No edge case results recorded._"]
    headers = ["Label", "Case", "Result", "Latency us", "Notes"]
    lines = [
        "| " + " | ".join(headers) + " |",
        "|" + "|".join(["---"] * len(headers)) + "|",
    ]
    for r in edge_rows:
        case = r.get("workload", "edge:?").split(":", 1)[1]
        ok = int(r.get("ops_completed") or 0)
        result = "PASS" if ok else "FAIL"
        try:
            lat = float(r.get("avg_latency_us") or 0)
        except Exception:
            lat = 0.0
        notes = (r.get("notes") or "").strip()
        lines.append("| " + " | ".join([
            r.get("label", ""),
            case,
            result,
            f"{lat:.2f}",
            notes,
        ]) + " |")
    return lines


def _capacity_table(rows: List[Dict[str, str]]) -> List[str]:
    if not rows:
        return ["_No capacity results recorded._"]
    headers = [
        "Label",
        "Devs",
        "Key (B)",
        "Value (B)",
        "Cap",
        "Limit (s)",
        "Stored",
        "First fail",
        "Last error",
        "Elapsed (s)",
        "Ops/s",
        "Total bytes",
        "Notes",
    ]
    lines = [
        "| " + " | ".join(headers) + " |",
        "|" + "|".join(["---"] * len(headers)) + "|",
    ]
    for r in rows:
        try:
            stored = int(r["keys_stored"])
            first_fail = int(r["first_failure_at"])
            elapsed = float(r["elapsed_sec"])
            ops = float(r["ops_per_sec"])
            total = int(r["total_bytes"])
        except Exception:
            continue
        devs = r.get("devices", "")
        dev_count = len(devs.split(",")) if devs else 0
        lines.append("| " + " | ".join([
            r.get("label", ""),
            str(dev_count),
            r.get("key_size", ""),
            r.get("value_size", ""),
            r.get("max_keys", ""),
            r.get("max_seconds", ""),
            f"{stored:,}",
            "—" if first_fail == 0 else str(first_fail),
            r.get("last_error", ""),
            f"{elapsed:.2f}",
            f"{ops:,.0f}",
            _format_kib(total),
            (r.get("notes") or "").strip(),
        ]) + " |")
    return lines


def _flatten_fio_jobs(fio_json: Dict[str, Any]) -> List[Dict[str, Any]]:
    out = []
    for job in fio_json.get("jobs", []):
        out.append(job)
    return out


def _fio_section(fio_dir: Path) -> List[str]:
    jsons = sorted(fio_dir.glob("*.json"))
    if not jsons:
        return ["_No FIO JSON results captured._"]
    lines = []
    for path in jsons:
        data = _try_load_json(path)
        if not data:
            continue
        lines.append(f"### `{path.name}`")
        global_opts = data.get("global options", {}) or {}
        if global_opts:
            keys = ["ioengine", "rw", "bs", "iodepth", "runtime",
                    "size", "directory", "rwmixread"]
            kv = ", ".join(
                f"{k}={global_opts[k]}" for k in keys if k in global_opts
            )
            if kv:
                lines.append(f"- Global: {kv}")
        headers = [
            "Job",
            "rw",
            "Read IOPS",
            "Read MB/s",
            "Read p99 us",
            "Write IOPS",
            "Write MB/s",
            "Write p99 us",
        ]
        lines.append("| " + " | ".join(headers) + " |")
        lines.append("|" + "|".join(["---"] * len(headers)) + "|")
        for job in _flatten_fio_jobs(data):
            name = job.get("jobname", "?")
            rw = job.get("job options", {}).get("rw") or global_opts.get("rw") or "?"
            read = job.get("read", {}) or {}
            write = job.get("write", {}) or {}
            r_iops = read.get("iops", 0.0) or 0.0
            r_bw = (read.get("bw_bytes") or read.get("bw", 0) * 1024) / (1024 * 1024)
            r_p99 = (read.get("clat_ns", {}) or {}).get("percentile", {}).get("99.000000")
            w_iops = write.get("iops", 0.0) or 0.0
            w_bw = (write.get("bw_bytes") or write.get("bw", 0) * 1024) / (1024 * 1024)
            w_p99 = (write.get("clat_ns", {}) or {}).get("percentile", {}).get("99.000000")
            r_p99_us = (r_p99 / 1000.0) if r_p99 else 0.0
            w_p99_us = (w_p99 / 1000.0) if w_p99 else 0.0
            lines.append("| " + " | ".join([
                str(name),
                str(rw),
                f"{r_iops:,.0f}",
                f"{r_bw:.2f}",
                f"{r_p99_us:.1f}",
                f"{w_iops:,.0f}",
                f"{w_bw:.2f}",
                f"{w_p99_us:.1f}",
            ]) + " |")
        lines.append("")
    return lines or ["_FIO output present but unparseable._"]


def _highlights(stress: List[Dict[str, str]]) -> List[str]:
    timed = [r for r in stress if not r.get("workload", "").startswith("edge:")]
    if not timed:
        return ["_No KV stress runs to summarize._"]

    def _f(r, k, default=0.0):
        try:
            return float(r.get(k) or default)
        except Exception:
            return default

    by_tput = max(timed, key=lambda r: _f(r, "throughput_ops_sec"))
    by_lat = min(
        timed,
        key=lambda r: _f(r, "p99_us", default=float("inf")),
    )
    failures = [
        r for r in timed
        if int(r.get("ops_failed") or 0) > 0
    ]
    success_rate = sum(_f(r, "success_rate") for r in timed) / max(len(timed), 1)

    lines = [
        f"- Workloads measured: **{len(timed)}** "
        f"(plus {len(stress) - len(timed)} edge cases)",
        f"- Highest throughput: **{by_tput.get('label')}** "
        f"({_f(by_tput, 'throughput_ops_sec'):,.0f} ops/s, "
        f"{_f(by_tput, 'throughput_mb_sec'):.2f} MB/s, "
        f"p99 {_f(by_tput, 'p99_us'):.1f} us)",
        f"- Lowest p99 latency: **{by_lat.get('label')}** "
        f"({_f(by_lat, 'p99_us'):.1f} us, "
        f"{_f(by_lat, 'throughput_ops_sec'):,.0f} ops/s)",
        f"- Mean success rate across timed workloads: **{success_rate * 100:.2f}%**",
    ]
    if failures:
        worst = max(failures, key=lambda r: int(r.get("ops_failed") or 0))
        lines.append(
            f"- Workload with most failures: **{worst.get('label')}** "
            f"({int(worst.get('ops_failed') or 0)} failed ops, "
            f"success={_f(worst, 'success_rate') * 100:.2f}%)"
        )
    return lines


def _capacity_highlights(rows: List[Dict[str, str]]) -> List[str]:
    if not rows:
        return []

    def _f(r, k, default=0.0):
        try:
            return float(r.get(k) or default)
        except Exception:
            return default

    totals = {}
    for r in rows:
        try:
            stored = int(r["keys_stored"])
        except Exception:
            continue
        label = r.get("label", "?")
        totals.setdefault(label, []).append((r, stored))

    lines = []
    for label, items in totals.items():
        max_keys = max(items, key=lambda t: t[1])
        max_row, max_stored = max_keys
        lines.append(
            f"- **{label}**: max keys stored = {max_stored:,} at "
            f"key={max_row.get('key_size')}B, value={max_row.get('value_size')}B "
            f"({_f(max_row, 'ops_per_sec'):,.0f} ops/s, "
            f"err={max_row.get('last_error')})"
        )
    return lines


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results-dir", required=True, type=Path)
    parser.add_argument("--mode", default="quick")
    args = parser.parse_args()

    results_dir = args.results_dir
    if not results_dir.exists():
        print(f"results dir {results_dir} does not exist", file=sys.stderr)
        return 1

    metadata = _try_load_json(results_dir / "metadata.json") or {}
    stress_rows = _read_csv(results_dir / "kv_stress.csv")
    capacity_rows = _read_csv(results_dir / "kv_capacity.csv")

    out_lines: List[str] = []
    out_lines.append(f"# NVMe KV Engine Capstone Benchmarks ({args.mode} mode)")
    captured = metadata.get("captured_at_utc") or "?"
    out_lines.append(f"_Captured at {captured} UTC_")
    out_lines.append("")

    out_lines.append("## Environment")
    uname = metadata.get("uname") or {}
    out_lines.append(
        f"- System: {uname.get('system', '?')} {uname.get('release', '?')}"
        f" ({uname.get('machine', '?')})"
    )
    out_lines.append(f"- CPU count: {metadata.get('cpu_count', '?')}")
    out_lines.append(f"- FIO: {metadata.get('fio_version') or 'not detected'}")
    devs = [d for d in (metadata.get("kvssd_devices") or []) if d]
    if devs:
        out_lines.append("- Emulated devices: " + ", ".join(f"`{d}`" for d in devs))
    if metadata.get("emulator_config"):
        out_lines.append(f"- Emulator config: `{metadata['emulator_config']}`")
    out_lines.append("")

    out_lines.append("## Highlights")
    out_lines.extend(_highlights(stress_rows))
    out_lines.extend(_capacity_highlights(capacity_rows))
    out_lines.append("")

    out_lines.append("## Single- and Multi-Device KV Stress")
    out_lines.append("Latency reported in microseconds; throughput in ops/sec and MB/s.")
    out_lines.append("")
    out_lines.extend(_kv_stress_workload_table(stress_rows))
    out_lines.append("")

    out_lines.append("## Edge Cases")
    out_lines.extend(_edge_table(stress_rows))
    out_lines.append("")

    out_lines.append("## Max Key Capacity Sweep")
    out_lines.append(
        "Each (key_size, value_size) combination starts with a fresh engine "
        "and inserts unique keys until the configured cap, time limit, or "
        "sustained failures stop the run."
    )
    out_lines.append("")
    out_lines.extend(_capacity_table(capacity_rows))
    out_lines.append("")

    out_lines.append("## FIO Multi-Device Baseline")
    out_lines.append(
        "FIO drives the same emulated four-target setup at the file/block "
        "level to validate the multi-device storage backend."
    )
    out_lines.append("")
    out_lines.extend(_fio_section(results_dir / "fio"))
    out_lines.append("")

    out_lines.append("## Artifacts")
    out_lines.append(f"- Stress CSV: `{results_dir / 'kv_stress.csv'}`")
    out_lines.append(f"- Capacity CSV: `{results_dir / 'kv_capacity.csv'}`")
    out_lines.append(f"- Raw logs: `{results_dir / 'raw'}`")
    out_lines.append(f"- FIO JSON: `{results_dir / 'fio'}`")
    out_lines.append("")

    summary_path = results_dir / "summary.md"
    summary_path.write_text("\n".join(out_lines) + "\n")
    print(f"[summary] wrote {summary_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
