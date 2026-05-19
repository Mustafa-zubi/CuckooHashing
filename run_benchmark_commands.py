#!/usr/bin/env python3

import argparse
import csv
import math
import re
import shlex
import subprocess
import sys
from pathlib import Path


# -----------------------------------------------------------------------------
# All outputs for this project live inside docs/cuckoo_robin_graphs/: the
# averaged benchmark CSV, the cache-miss sweep CSV, and every plot image.
# -----------------------------------------------------------------------------

CUCKOO_ROBIN_GRAPHS_DIR = Path("docs/cuckoo_robin_graphs")
DIEHARD_INPUT_DEFAULT = "DiehardCDROM-master/CD-ROM/bits.01"


SUMMARY_PATTERNS = {
    "algorithm": re.compile(r"^Algorithm:\s+(.*)$"),
    "input_file": re.compile(r"^Input file:\s+(.*)$"),
    "initial_keys_loaded": re.compile(r"^Initial keys loaded:\s+(\d+)$"),
    "benchmark_cycles": re.compile(r"^Benchmark cycles:\s+(\d+)$"),
    "configured_table_size_per_table": re.compile(r"^Configured table size per table:\s+(\d+)$"),
    "configured_max_loop_count": re.compile(r"^Configured max loop count:\s+(\d+)$"),
    "configured_target_total_slots": re.compile(r"^Configured target total slots:\s+(\d+)$"),
    "random_values_consumed": re.compile(r"^Random values consumed:\s+(\d+)$"),
    "duplicate_candidate_values_skipped": re.compile(r"^Duplicate candidate values skipped:\s+(\d+)$"),
    "successful_lookups_executed": re.compile(r"^Successful lookups executed:\s+(\d+)$"),
    "unsuccessful_lookups_executed": re.compile(r"^Unsuccessful lookups executed:\s+(\d+)$"),
    "deletions_executed": re.compile(r"^Deletions executed:\s+(\d+)$"),
    "insertions_executed": re.compile(r"^Insertions executed:\s+(\d+)$"),
    "remaining_active_keys": re.compile(r"^Remaining active keys:\s+(\d+)$"),
    "elapsed_time_seconds": re.compile(r"^Elapsed time \(seconds\):\s+([0-9]*\.?[0-9]+)$"),
    "operations_per_second": re.compile(r"^Operations per second:\s+([0-9]*\.?[0-9]+)$"),
    "active_keys": re.compile(r"^\s*Active keys:\s+(\d+)$"),
    "occupied_slots_t1": re.compile(r"^\s*Occupied slots in T1:\s+(\d+)$"),
    "occupied_slots_t2": re.compile(r"^\s*Occupied slots in T2:\s+(\d+)$"),
    "load_factor": re.compile(r"^\s*Load factor:\s+([0-9]*\.?[0-9]+)$"),
    "insert_attempts": re.compile(r"^\s*Insert attempts:\s+(\d+)$"),
    "successful_insertions": re.compile(r"^\s*Successful insertions:\s+(\d+)$"),
    "duplicate_insertions_ignored": re.compile(r"^\s*Duplicate insertions ignored:\s+(\d+)$"),
    "failed_insertions": re.compile(r"^\s*Failed insertions:\s+(\d+)$"),
    "rehash_attempts": re.compile(r"^\s*Rehash attempts:\s+(\d+)$"),
    "insertions_since_last_rehash": re.compile(r"^\s*Insertions since last rehash:\s+(\d+)$"),
    "key_displacements": re.compile(r"^\s*Key displacements:\s+(\d+)$"),
    "robinhood_swaps": re.compile(r"^\s*Robin Hood swaps during insert:\s+(\d+)$"),
    "maximum_probe_distance": re.compile(r"^\s*Maximum probe distance:\s+(\d+)$"),
    "robinhood_probes": re.compile(r"^\s*Total probes:\s+(\d+)$"),
}

TIMING_LINE = (
    r"^{label}:\s+(\d+)\s+ops in\s+([0-9]*\.?[0-9]+)\s+s"
    r"(?:\s+\|\s+avg\s+([0-9]*\.?[0-9]+)\s+ns/op)?"
    r"(?:\s+\|\s+avg\s+([0-9]*\.?[0-9]+)\s+cycles/op)?"
    r"(?:\s+\|\s+filtered ops\s+(\d+)\s+\|\s+dropped\s+(\d+))?\s*$"
)

TIMING_PATTERNS = {
    "successful_lookup": re.compile(TIMING_LINE.format(label="Successful Lookup")),
    "unsuccessful_lookup": re.compile(TIMING_LINE.format(label="Unsuccessful Lookup")),
    "delete": re.compile(TIMING_LINE.format(label="Delete")),
    "insert": re.compile(TIMING_LINE.format(label="Insert")),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Execute the Cuckoo vs Robin Hood benchmark commands, save the "
            "averaged results to CSV, generate the cache-miss sweep data, "
            "and render the paper-style plots into docs/cuckoo_robin_graphs/."
        )
    )
    parser.add_argument(
        "--commands-file",
        default="docs/commands_cuckoo_robin",
        help="Path to the file containing benchmark commands.",
    )
    parser.add_argument(
        "--output",
        default=None,
        help=(
            "Path to the CSV file to write. Defaults to "
            "`<output-dir>/cuckoo_robin_results.csv`."
        ),
    )
    parser.add_argument(
        "--output-dir",
        default=CUCKOO_ROBIN_GRAPHS_DIR,
        type=Path,
        help=(
            "Folder that holds the CSV + every plot for this run. Defaults "
            "to docs/cuckoo_robin_graphs/."
        ),
    )
    parser.add_argument(
        "--runs",
        type=int,
        default=10,
        help="Number of repetitions per command (paper uses 10).",
    )
    parser.add_argument(
        "--include-runs",
        action="store_true",
        help="Emit per-run rows in addition to the aggregate mean row.",
    )
    parser.add_argument(
        "--no-plot",
        action="store_true",
        help="Skip the final plotting step. Just write the CSV(s).",
    )
    parser.add_argument(
        "--no-fig7",
        action="store_true",
        help=(
            "Skip generating the Fig 7 cache-miss CSV (still plots one if a "
            "cache_misses_data.csv already exists in the output folder)."
        ),
    )
    parser.add_argument(
        "--fig7-per-table-size",
        type=int,
        default=32768,
        help="`perTableSize` argument forwarded to `./benchmark[_cuckoo_robin] fig7`.",
    )
    parser.add_argument(
        "--fig7-warmup",
        type=int,
        default=100000,
        help="Warmup mixed-op count for the Fig 7 sweep.",
    )
    parser.add_argument(
        "--fig7-samples",
        type=int,
        default=100000,
        help="Sample mixed-op count for the Fig 7 sweep.",
    )
    return parser.parse_args()


# -----------------------------------------------------------------------------
# Fig 7 sweep + plotter.
# -----------------------------------------------------------------------------

CSV_FILENAME = "cuckoo_robin_results.csv"
FIG7_BINARY = "./benchmark_cuckoo_robin"
FIG7_ALGORITHMS = "cuckoo,robinhood"
MAIN_TITLE = "Cuckoo vs Robin Hood Hashing"
FIG7_TITLE = "Cache Misses: Cuckoo vs Robin Hood"


def generate_fig7_csv(output_dir: Path, workdir: Path, args: argparse.Namespace) -> None:
    """Invoke ./benchmark_cuckoo_robin in fig7 mode and capture its CSV
    inside the output folder."""
    output_csv = (workdir / output_dir / "cache_misses_data.csv").resolve()
    output_csv.parent.mkdir(parents=True, exist_ok=True)
    binary = (workdir / FIG7_BINARY).resolve()
    cmd = [
        str(binary), "fig7",
        DIEHARD_INPUT_DEFAULT,
        str(args.fig7_per_table_size),
        str(args.fig7_warmup),
        str(args.fig7_samples),
        "64",
    ]
    print(f"Running Fig 7 sweep: {' '.join(cmd)}")
    with output_csv.open("w", encoding="utf-8") as fh:
        completed = subprocess.run(cmd, cwd=workdir, stdout=fh, stderr=subprocess.PIPE,
                                    text=True, check=False)
    if completed.returncode != 0:
        print(f"  Fig 7 sweep failed (exit {completed.returncode}): {completed.stderr.strip()}",
              file=sys.stderr)
    else:
        print(f"Wrote Fig 7 data CSV to {output_csv}")


def plot_cuckoo_robin_results(csv_path: Path, output_dir: Path) -> None:
    """Render every paper-style plot for the Cuckoo vs Robin Hood benchmark
    into `output_dir`. The Fig 7 cache-miss curve overlays Cuckoo and
    Robin Hood and is titled "Cache Misses: Cuckoo vs Robin Hood"."""
    print(f"\n=== plot_cuckoo_robin_results: rendering into {output_dir} ===")
    import plot_paper_graphs as plotter
    plotter.render_all(
        csv_path=csv_path,
        output_dir=output_dir,
        title=MAIN_TITLE,
        fig7_algorithms=FIG7_ALGORITHMS,
        fig7_title=FIG7_TITLE,
    )


def load_commands(commands_file: Path) -> list[str]:
    commands = []
    for line in commands_file.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        commands.append(stripped)
    return commands


def parse_single_benchmark_summary(output: str) -> dict[str, object]:
    row: dict[str, object] = {}

    for line in output.splitlines():
        for field, pattern in SUMMARY_PATTERNS.items():
            match = pattern.match(line)
            if match:
                value = match.group(1)
                if field in {"algorithm", "input_file"}:
                    row[field] = value
                elif "." in value:
                    row[field] = float(value)
                else:
                    row[field] = int(value)

        for prefix, pattern in TIMING_PATTERNS.items():
            match = pattern.match(line)
            if match:
                row[f"{prefix}_ops"] = int(match.group(1))
                row[f"{prefix}_seconds"] = float(match.group(2))
                if match.group(3) is not None:
                    row[f"{prefix}_ns_per_op"] = float(match.group(3))
                if match.group(4) is not None:
                    row[f"{prefix}_cycles_per_op"] = float(match.group(4))
                if match.group(5) is not None:
                    row[f"{prefix}_filtered_ops"] = int(match.group(5))
                if match.group(6) is not None:
                    row[f"{prefix}_dropped_ops"] = int(match.group(6))

    if "initial_keys_loaded" in row:
        row["log2_n"] = round(math.log2(int(row["initial_keys_loaded"])), 6)

    return row


def parse_benchmark_output(output: str) -> list[dict[str, object]]:
    marker = "=== Benchmark Summary ==="
    if marker not in output:
        return [parse_single_benchmark_summary(output)]

    rows = []
    for summary in output.split(marker)[1:]:
        row = parse_single_benchmark_summary(summary)
        if row:
            rows.append(row)
    return rows


def run_command(command: str, workdir: Path, run_index: int) -> list[dict[str, object]]:
    completed = subprocess.run(
        shlex.split(command),
        cwd=workdir,
        capture_output=True,
        text=True,
        check=False,
    )

    if completed.returncode == 0:
        rows = parse_benchmark_output(completed.stdout)
    else:
        rows = [{}]

    for row in rows:
        row["command"] = command
        row["return_code"] = completed.returncode
        row["stderr"] = completed.stderr.strip()
        row["run_index"] = run_index
        row["aggregate"] = "single"
    if completed.returncode != 0:
        row["stdout"] = completed.stdout.strip()

    return rows


# Fields that should be averaged across runs (numeric measurements that scale
# with workload). Counts and structural stats are summed; identifiers are kept
# from the first run.
_MEAN_FIELDS = {
    "elapsed_time_seconds",
    "operations_per_second",
    "successful_lookup_seconds",
    "successful_lookup_ns_per_op",
    "successful_lookup_cycles_per_op",
    "unsuccessful_lookup_seconds",
    "unsuccessful_lookup_ns_per_op",
    "unsuccessful_lookup_cycles_per_op",
    "delete_seconds",
    "delete_ns_per_op",
    "delete_cycles_per_op",
    "insert_seconds",
    "insert_ns_per_op",
    "insert_cycles_per_op",
    "load_factor",
}


def aggregate_rows(rows: list[dict[str, object]]) -> dict[str, object]:
    if not rows:
        return {}
    agg: dict[str, object] = dict(rows[0])
    agg["aggregate"] = "mean"
    agg["run_index"] = -1
    agg["runs_aggregated"] = len(rows)
    for field in _MEAN_FIELDS:
        values = [r[field] for r in rows if isinstance(r.get(field), (int, float))]
        if values:
            agg[field] = sum(values) / len(values)
    return agg


def build_fieldnames(rows: list[dict[str, object]]) -> list[str]:
    preferred = [
        "command",
        "return_code",
        "aggregate",
        "run_index",
        "runs_aggregated",
        "algorithm",
        "log2_n",
        "input_file",
        "initial_keys_loaded",
        "benchmark_cycles",
        "configured_table_size_per_table",
        "configured_max_loop_count",
        "configured_target_total_slots",
        "random_values_consumed",
        "duplicate_candidate_values_skipped",
        "successful_lookups_executed",
        "unsuccessful_lookups_executed",
        "deletions_executed",
        "insertions_executed",
        "remaining_active_keys",
        "elapsed_time_seconds",
        "operations_per_second",
        "successful_lookup_ops",
        "successful_lookup_seconds",
        "successful_lookup_ns_per_op",
        "successful_lookup_cycles_per_op",
        "unsuccessful_lookup_ops",
        "unsuccessful_lookup_seconds",
        "unsuccessful_lookup_ns_per_op",
        "unsuccessful_lookup_cycles_per_op",
        "delete_ops",
        "delete_seconds",
        "delete_ns_per_op",
        "delete_cycles_per_op",
        "insert_ops",
        "insert_seconds",
        "insert_ns_per_op",
        "insert_cycles_per_op",
        "active_keys",
        "occupied_slots_t1",
        "occupied_slots_t2",
        "load_factor",
        "insert_attempts",
        "successful_insertions",
        "duplicate_insertions_ignored",
        "failed_insertions",
        "rehash_attempts",
        "insertions_since_last_rehash",
        "key_displacements",
        "robinhood_swaps",
        "robinhood_probes",
        "maximum_probe_distance",
        "stderr",
        "stdout",
    ]

    keys = set()
    for row in rows:
        keys.update(row.keys())

    ordered = [field for field in preferred if field in keys]
    ordered.extend(sorted(keys - set(ordered)))
    return ordered


def main() -> int:
    args = parse_args()
    workdir = Path(__file__).resolve().parent
    commands_file = (workdir / args.commands_file).resolve()

    output_dir = (workdir / args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    csv_filename = args.output if args.output else CSV_FILENAME
    output_file = (output_dir / Path(csv_filename).name).resolve() \
        if not Path(csv_filename).is_absolute() else Path(csv_filename).resolve()

    print(f"Output folder: {output_dir}")
    print(f"CSV will be written to: {output_file}")

    if not commands_file.exists():
        print(f"Commands file not found: {commands_file}", file=sys.stderr)
        return 1

    commands = load_commands(commands_file)
    if not commands:
        print(f"No runnable commands found in: {commands_file}", file=sys.stderr)
        return 1

    rows = []
    total = len(commands)
    for index, command in enumerate(commands, start=1):
        print(f"[{index}/{total}] Running: {command} x{args.runs}", flush=True)

        # Group rows by algorithm so each scheme gets its own mean across runs.
        per_algorithm: dict[str, list[dict[str, object]]] = {}
        for run_index in range(1, args.runs + 1):
            command_rows = run_command(command, workdir, run_index)
            if command_rows and command_rows[0].get("return_code") != 0:
                print(
                    f"  Run {run_index} failed (code {command_rows[0].get('return_code')})",
                    flush=True,
                )
            if args.include_runs:
                rows.extend(command_rows)
            for row in command_rows:
                algorithm = str(row.get("algorithm", ""))
                per_algorithm.setdefault(algorithm, []).append(row)

        for algorithm, group in per_algorithm.items():
            rows.append(aggregate_rows(group))

    output_file.parent.mkdir(parents=True, exist_ok=True)
    fieldnames = build_fieldnames(rows)
    with output_file.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.DictWriter(csv_file, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(rows)

    print(f"Wrote {len(rows)} rows to {output_file}")

    # --- Fig 7 cache-miss CSV (cache_misses_data.csv) -----------------------
    if not args.no_fig7:
        generate_fig7_csv(output_dir, workdir, args)
    else:
        print("Skipping Fig 7 sweep (--no-fig7).")

    # --- Plot ---------------------------------------------------------------
    if args.no_plot:
        print("Skipping plot generation (--no-plot).")
        return 0

    plot_cuckoo_robin_results(output_file, output_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
