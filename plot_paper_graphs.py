#!/usr/bin/env python3

import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path
from typing import Optional


DEFAULT_CSV = Path("docs/cuckoo_robin_graphs/cuckoo_robin_results.csv")
# All plot images for a benchmark run live in one folder.
DEFAULT_OUTPUT_DIR = Path("docs/cuckoo_robin_graphs")
MAIN_FILENAME = "paper_style_graphs.png"
DIAGNOSTICS_FILENAME = "paper_style_diagnostics.png"
FIG7_DATA_FILENAME = "cache_misses_data.csv"
FIG7_PLOT_FILENAME = "cache_misses.png"
# Newer Fig 7 output has an algorithm column. The legacy Cuckoo-only file
# starts with "alpha,". Either header is accepted.
FIG7_HEADER_PREFIXES = ("algorithm,alpha,", "alpha,")

NUMERIC_COLUMNS = {
    "log2_n",
    "initial_keys_loaded",
    "benchmark_cycles",
    "configured_table_size_per_table",
    "configured_max_loop_count",
    "configured_target_total_slots",
    "configured_two_way_bucket_capacity",
    "configured_two_way_max_grow_attempts",
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
}

ALGORITHM_STYLE = {
    "cuckoo": {"marker": "o", "label": "Cuckoo"},
    "robinhood": {"marker": "d", "label": "Robin Hood"},
    "robin": {"marker": "d", "label": "Robin Hood"},
}
ALGORITHM_ORDER = {
    "cuckoo": 0,
    "robinhood": 1,
    "robin": 1,
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Plot paper-style comparison graphs for the implemented hashing algorithms "
            "from docs/benchmark_results.csv."
        )
    )
    parser.add_argument("--csv", default=DEFAULT_CSV, type=Path, help="Benchmark CSV input path.")
    parser.add_argument(
        "--output-dir",
        default=DEFAULT_OUTPUT_DIR,
        type=Path,
        help=(
            "Single folder for ALL outputs (main plot, single-metric plots, "
            "diagnostics, and Fig 7 cache-miss plot). Defaults to "
            f"`{DEFAULT_OUTPUT_DIR}`. Override individual filenames with the "
            "more specific --*-output flags below if needed."
        ),
    )
    parser.add_argument(
        "--output",
        "--html",
        dest="output",
        default=None,
        type=Path,
        help=(
            "Main graph image output path. Defaults to "
            f"`<output-dir>/{MAIN_FILENAME}`."
        ),
    )
    parser.add_argument(
        "--single-output-dir",
        default=None,
        type=Path,
        help=(
            "Directory for standalone paper-style graph images, one metric "
            "per file. Defaults to `<output-dir>` itself."
        ),
    )
    parser.add_argument(
        "--diagnostics-output",
        "--diagnostics-html",
        dest="diagnostics_output",
        default=None,
        type=Path,
        help=(
            "Diagnostics graph image output path. Defaults to "
            f"`<output-dir>/{DIAGNOSTICS_FILENAME}`."
        ),
    )
    parser.add_argument(
        "--fig7-csv",
        default=None,
        type=Path,
        help=(
            "Path to a Fig 7 CSV (output of `./benchmark fig7 ...`). "
            f"Defaults to `<output-dir>/{FIG7_DATA_FILENAME}`."
        ),
    )
    parser.add_argument(
        "--fig7-output",
        "--fig7-html",
        dest="fig7_output",
        default=None,
        type=Path,
        help=(
            "Image output path for the Fig 7 cache-miss curve. Defaults to "
            f"`<output-dir>/{FIG7_PLOT_FILENAME}`."
        ),
    )
    parser.add_argument(
        "--fig7-algorithms",
        default="all",
        help=(
            "Comma-separated list of algorithm names to include in the Fig 7 "
            "plot (e.g. 'cuckoo', 'cuckoo,robinhood'). Use 'all' to include "
            "every algorithm present in the CSV (default)."
        ),
    )
    parser.add_argument(
        "--fig7-title",
        default=None,
        help=(
            "Optional override for the Fig 7 plot title. Defaults to "
            "'<main title> Fig. 7'."
        ),
    )
    parser.add_argument(
        "--title",
        default="Hash Table Benchmark Comparison",
        help="Title shown at the top of the report.",
    )
    parser.add_argument(
        "--x-axis",
        choices=("auto", "log2_n", "load_factor"),
        default="auto",
        help="X-axis for the plots. 'auto' uses load_factor when n is fixed and load varies, otherwise log2_n.",
    )
    return parser.parse_args()


def parse_float(value: str) -> float:
    if value is None or value == "":
        return 0.0
    return float(value)


def load_rows(csv_path: Path) -> list[dict[str, object]]:
    with csv_path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        rows = []
        for raw_row in reader:
            if raw_row.get("return_code") not in ("", "0", 0):
                continue

            row: dict[str, object] = {}
            for key, value in raw_row.items():
                if key in NUMERIC_COLUMNS:
                    row[key] = parse_float(value)
                else:
                    row[key] = value or ""

            if not row.get("algorithm"):
                row["algorithm"] = "cuckoo"
            if row.get("log2_n", 0.0) == 0.0 and row.get("initial_keys_loaded", 0.0) > 0:
                row["log2_n"] = math.log2(float(row["initial_keys_loaded"]))
            rows.append(row)

    return sorted(rows, key=lambda item: (str(item["algorithm"]), float(item["log2_n"])))


def weighted_ns(row: dict[str, object], ops_key: str, seconds_key: str) -> float:
    ops = float(row.get(ops_key, 0.0))
    seconds = float(row.get(seconds_key, 0.0))
    if ops <= 0.0:
        return 0.0
    return (seconds / ops) * 1e9


def weighted_pair_ns(
    row: dict[str, object],
    left_ops_key: str,
    left_seconds_key: str,
    right_ops_key: str,
    right_seconds_key: str,
) -> float:
    left_ops = float(row.get(left_ops_key, 0.0))
    right_ops = float(row.get(right_ops_key, 0.0))
    total_ops = left_ops + right_ops
    if total_ops <= 0.0:
        return 0.0
    total_seconds = float(row.get(left_seconds_key, 0.0)) + float(row.get(right_seconds_key, 0.0))
    return (total_seconds / total_ops) * 1e9


def derived_row(row: dict[str, object]) -> dict[str, object]:
    derived = dict(row)
    derived["avg_lookup_ns_per_op"] = weighted_pair_ns(
        row,
        "successful_lookup_ops",
        "successful_lookup_seconds",
        "unsuccessful_lookup_ops",
        "unsuccessful_lookup_seconds",
    )
    derived["avg_update_ns_per_op"] = weighted_pair_ns(
        row,
        "delete_ops",
        "delete_seconds",
        "insert_ops",
        "insert_seconds",
    )
    derived["insert_ns_per_op"] = float(row.get("insert_ns_per_op", 0.0)) or weighted_ns(
        row, "insert_ops", "insert_seconds"
    )
    derived["delete_ns_per_op"] = float(row.get("delete_ns_per_op", 0.0)) or weighted_ns(
        row, "delete_ops", "delete_seconds"
    )
    derived["avg_lookup_cycles_per_op"] = (
        (
            float(row.get("successful_lookup_cycles_per_op", 0.0)) *
            float(row.get("successful_lookup_ops", 0.0)) +
            float(row.get("unsuccessful_lookup_cycles_per_op", 0.0)) *
            float(row.get("unsuccessful_lookup_ops", 0.0))
        ) /
        max(
            1.0,
            float(row.get("successful_lookup_ops", 0.0)) +
            float(row.get("unsuccessful_lookup_ops", 0.0)),
        )
    )
    derived["avg_update_cycles_per_op"] = (
        (
            float(row.get("delete_cycles_per_op", 0.0)) * float(row.get("delete_ops", 0.0)) +
            float(row.get("insert_cycles_per_op", 0.0)) * float(row.get("insert_ops", 0.0))
        ) /
        max(1.0, float(row.get("delete_ops", 0.0)) + float(row.get("insert_ops", 0.0)))
    )
    successful_insertions = float(row.get("successful_insertions", 0.0))
    derived["displacements_per_insert"] = (
        float(row.get("key_displacements", 0.0)) / successful_insertions
        if successful_insertions > 0.0
        else 0.0
    )
    derived["robinhood_swaps_per_insert"] = (
        float(row.get("robinhood_swaps", 0.0)) / successful_insertions
        if successful_insertions > 0.0
        else 0.0
    )
    return derived


def aggregate_rows(rows: list[dict[str, object]]) -> list[dict[str, object]]:
    groups: dict[tuple[str, float, float], list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        groups[
            (
                str(row["algorithm"]),
                float(row["log2_n"]),
                round(float(row.get("load_factor", 0.0)), 6),
            )
        ].append(derived_row(row))

    aggregated = []
    for (algorithm, log2_n, load_factor), group in sorted(
        groups.items(),
        key=lambda item: (item[0][0], item[0][1], item[0][2]),
    ):
        result = {"algorithm": algorithm, "log2_n": log2_n, "load_factor": load_factor}
        keys = set().union(*(row.keys() for row in group))
        for key in keys:
            if key in {"algorithm", "command", "input_file", "stderr", "stdout"}:
                result[key] = group[0].get(key, "")
            elif (
                key in NUMERIC_COLUMNS or
                key.endswith("_ns_per_op") or
                key.endswith("_cycles_per_op") or
                key.endswith("_per_insert")
            ):
                result[key] = sum(float(row.get(key, 0.0)) for row in group) / len(group)
        result["trial_count"] = len(group)
        aggregated.append(result)
    return aggregated


def series_by_algorithm(rows: list[dict[str, object]]) -> dict[str, list[dict[str, object]]]:
    grouped: dict[str, list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        grouped[str(row["algorithm"])].append(row)
    return dict(
        sorted(
            grouped.items(),
            key=lambda item: (ALGORITHM_ORDER.get(item[0], 99), item[0]),
        )
    )


def style_for_algorithm(algorithm: str) -> dict[str, str]:
    return ALGORITHM_STYLE.get(
        algorithm,
        {"marker": "o", "label": algorithm},
    )


def choose_x_axis(rows: list[dict[str, object]], mode: str) -> tuple[str, str]:
    if mode == "log2_n":
        return "log2_n", "log2(n)"
    if mode == "load_factor":
        return "load_factor", "load factor"

    distinct_n = {
        int(float(row.get("initial_keys_loaded", 0.0)))
        for row in rows
        if float(row.get("initial_keys_loaded", 0.0)) > 0.0
    }
    distinct_load = {
        round(float(row.get("load_factor", 0.0)), 6)
        for row in rows
        if float(row.get("load_factor", 0.0)) > 0.0
    }
    if len(distinct_n) == 1 and len(distinct_load) > 1:
        return "load_factor", "load factor"
    return "log2_n", "log2(n)"


def load_matplotlib():
    try:
        import matplotlib.pyplot as plt
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "Matplotlib is not installed. Install it with `pip install matplotlib` "
            "and rerun this script."
        ) from exc
    return plt


def apply_paper_axes(ax, title: str, x_label: str, y_label: str) -> None:
    ax.set_title(title, fontsize=13, pad=8)
    ax.set_xlabel(x_label, fontsize=12)
    ax.set_ylabel(y_label, fontsize=12)
    ax.tick_params(direction="in", top=True, right=True, width=1.0, labelsize=10)
    for spine in ax.spines.values():
        spine.set_linewidth(1.0)
        spine.set_color("0.2")


def plot_metric(ax, grouped, metric, x_metric, x_label, y_label, title, *, show_legend=False):
    for algorithm, values in grouped.items():
        style = style_for_algorithm(algorithm)
        x_values = [float(item.get(x_metric, 0.0)) for item in values]
        y_values = [float(item.get(metric, 0.0)) for item in values]
        marker = style["marker"]
        markerface = "none" if marker != "x" else "0.25"
        ax.plot(
            x_values,
            y_values,
            linestyle=":",
            linewidth=1.1,
            color="0.35",
            marker=marker,
            markersize=5,
            markerfacecolor=markerface,
            markeredgecolor="0.25",
            markeredgewidth=1.0,
            label=style["label"],
        )
    apply_paper_axes(ax, title, x_label, y_label)
    if show_legend:
        legend = ax.legend(
            loc="upper left",
            frameon=True,
            fancybox=False,
            framealpha=1.0,
            edgecolor="0.35",
            fontsize=10,
        )
        legend.get_frame().set_linewidth(0.8)


def has_cycle_metrics(rows: list[dict[str, object]]) -> bool:
    return any(float(row.get("successful_lookup_cycles_per_op", 0.0)) > 0.0 for row in rows)


def grouped_aggregates(rows: list[dict[str, object]], x_metric: str) -> dict[str, list[dict[str, object]]]:
    return {
        algorithm: sorted(values, key=lambda item: float(item.get(x_metric, 0.0)))
        for algorithm, values in series_by_algorithm(aggregate_rows(rows)).items()
    }


def main_plot_specs(rows: list[dict[str, object]]) -> list[tuple[str, str, str, str]]:
    use_cycles = has_cycle_metrics(rows)
    timing_axis = "Clock Cycles" if use_cycles else "Nanoseconds"
    return [
        (
            "average_lookup",
            "avg_lookup_cycles_per_op" if use_cycles else "avg_lookup_ns_per_op",
            f"{timing_axis} per Lookup",
            "Average Lookup",
        ),
        (
            "update",
            "avg_update_cycles_per_op" if use_cycles else "avg_update_ns_per_op",
            f"{timing_axis} per Update",
            "Update",
        ),
        (
            "successful_lookup",
            "successful_lookup_cycles_per_op" if use_cycles else "successful_lookup_ns_per_op",
            timing_axis,
            "Successful Lookup",
        ),
        (
            "unsuccessful_lookup",
            "unsuccessful_lookup_cycles_per_op" if use_cycles else "unsuccessful_lookup_ns_per_op",
            timing_axis,
            "Unsuccessful Lookup",
        ),
        (
            "insert",
            "insert_cycles_per_op" if use_cycles else "insert_ns_per_op",
            timing_axis,
            "Insert",
        ),
        (
            "delete",
            "delete_cycles_per_op" if use_cycles else "delete_ns_per_op",
            timing_axis,
            "Delete",
        ),
        (
            "throughput",
            "operations_per_second",
            "Operations per Second",
            "Throughput",
        ),
    ]


def build_figure(
    rows: list[dict[str, object]],
    csv_path: Path,
    title: str,
    x_metric: str,
    x_label: str,
):
    plt = load_matplotlib()

    grouped = grouped_aggregates(rows, x_metric)
    specs = main_plot_specs(rows)
    fig, axes = plt.subplots(4, 2, figsize=(11.0, 15.0), constrained_layout=True)
    fig.suptitle(title, fontsize=16)
    for ax, (_, metric, y_label, plot_title) in zip(axes.flat, specs):
        plot_metric(
            ax,
            grouped,
            metric,
            x_metric,
            x_label,
            y_label,
            plot_title,
            show_legend=(plot_title == "Average Lookup"),
        )
    for ax in axes.flat[len(specs):]:
        ax.set_visible(False)
    return fig


def build_single_figures(
    rows: list[dict[str, object]],
    title: str,
    x_metric: str,
    x_label: str,
) -> list[tuple[str, object]]:
    plt = load_matplotlib()
    grouped = grouped_aggregates(rows, x_metric)
    figures = []
    for filename_stem, metric, y_label, plot_title in main_plot_specs(rows):
        fig, ax = plt.subplots(figsize=(6.4, 4.8), constrained_layout=True)
        plot_metric(
            ax,
            grouped,
            metric,
            x_metric,
            x_label,
            y_label,
            plot_title,
            show_legend=True,
        )
        figures.append((filename_stem, fig))
    return figures


def build_diagnostics_figure(
    rows: list[dict[str, object]],
    csv_path: Path,
    title: str,
    x_metric: str,
    x_label: str,
):
    plt = load_matplotlib()
    grouped = {
        algorithm: sorted(values, key=lambda item: float(item.get(x_metric, 0.0)))
        for algorithm, values in series_by_algorithm(aggregate_rows(rows)).items()
    }
    fig, axes = plt.subplots(1, 2, figsize=(10.5, 4.5), constrained_layout=True)
    fig.suptitle(f"{title} Diagnostics", fontsize=16)
    plot_metric(
        axes[0],
        grouped,
        "load_factor",
        x_metric,
        x_label,
        "Measured Load Factor",
        "Load Factor",
        show_legend=True,
    )

    # Cuckoo tracks displacements per insert; Robin Hood tracks PSL swaps
    # per insert. Both are reported as "moves per insert" on the same axis.
    diagnostic_metrics = {
        "cuckoo": "displacements_per_insert",
        "robinhood": "robinhood_swaps_per_insert",
        "robin": "robinhood_swaps_per_insert",
    }
    for algorithm, values in grouped.items():
        style = style_for_algorithm(algorithm)
        metric = diagnostic_metrics.get(algorithm, "displacements_per_insert")
        axes[1].plot(
            [float(item.get(x_metric, 0.0)) for item in values],
            [float(item.get(metric, 0.0)) for item in values],
            linestyle=":",
            linewidth=1.1,
            color="0.35",
            marker=style["marker"],
            markersize=5,
            markerfacecolor="none" if style["marker"] != "x" else "0.25",
            markeredgecolor="0.25",
            markeredgewidth=1.0,
        )
    apply_paper_axes(axes[1], "Key Moves per Insert", x_label, "Count per Insert")
    return fig


def load_fig7_rows(csv_path: Path) -> list[dict[str, object]]:
    """Read a Fig 7 CSV emitted by `./benchmark fig7 ...`.

    The benchmark binary prints startup banner lines ("Discovered input file: ...")
    to stdout before the CSV header. We skip lines until we hit either the
    multi-algorithm header (`algorithm,alpha,...`) or the legacy single-
    algorithm header (`alpha,...`). Legacy rows are tagged as `algorithm =
    cuckoo`.
    """
    text = csv_path.read_text(encoding="utf-8")
    lines = text.splitlines()
    header_index = None
    for i, line in enumerate(lines):
        if any(line.startswith(prefix) for prefix in FIG7_HEADER_PREFIXES):
            header_index = i
            break
    if header_index is None:
        return []

    reader = csv.DictReader(lines[header_index:])
    rows: list[dict[str, object]] = []
    for raw in reader:
        try:
            rows.append({
                "algorithm": str(raw.get("algorithm", "cuckoo") or "cuckoo"),
                "alpha": float(raw["alpha"]),
                "n": float(raw.get("n", 0.0)),
                "total_slots": float(
                    raw.get("total_slots") or raw.get("totalSlots") or 0.0
                ),
                "avg_cell_accesses_per_insert": float(
                    raw["avg_cell_accesses_per_insert"]
                ),
                "sampled_inserts": float(raw.get("sampled_inserts", 0.0)),
            })
        except (KeyError, ValueError):
            continue
    return rows


def build_fig7_figure(
    rows: list[dict[str, object]],
    csv_path: Path,
    title: str,
    algorithm_filter: str = "all",
    custom_subtitle: Optional[str] = None,
):
    """Plot the Fig 7 cache-miss curves for the algorithms present in the CSV
    (or restricted by `algorithm_filter`, a comma-separated list or 'all').
    The paper's Fig 7 shows Cuckoo, Two-Way, Chained and Double Hashing; we
    can include any subset and substitute Linear Probing / Robin Hood when
    appropriate."""
    plt = load_matplotlib()

    # Resolve the algorithm filter into a set (or None to keep everything).
    allowed: Optional[set] = None
    if algorithm_filter.lower() != "all":
        allowed = {tok.strip().lower() for tok in algorithm_filter.split(",") if tok.strip()}

    # Group rows by algorithm, applying the filter.
    grouped: dict[str, list[dict[str, object]]] = defaultdict(list)
    for row in rows:
        algorithm = str(row["algorithm"])
        if allowed is not None and algorithm.lower() not in allowed:
            continue
        grouped[algorithm].append(row)
    grouped = dict(
        sorted(
            grouped.items(),
            key=lambda item: (ALGORITHM_ORDER.get(item[0], 99), item[0]),
        )
    )

    fig, ax = plt.subplots(figsize=(7.0, 5.0), constrained_layout=True)

    for algorithm, group in grouped.items():
        group = sorted(group, key=lambda item: item["alpha"])
        style = style_for_algorithm(algorithm)
        marker = style["marker"]
        ax.plot(
            [item["alpha"] for item in group],
            [item["avg_cell_accesses_per_insert"] for item in group],
            linestyle=":",
            linewidth=1.1,
            color="0.35",
            marker=marker,
            markersize=5,
            markerfacecolor="none" if marker != "x" else "0.25",
            markeredgecolor="0.25",
            markeredgewidth=1.0,
            label=style["label"],
        )

    plot_title = custom_subtitle if custom_subtitle is not None else f"{title} Fig. 7"
    apply_paper_axes(ax, plot_title, "Load Factor alpha", "Cache Misses")
    legend = ax.legend(
        loc="upper left",
        frameon=True,
        fancybox=False,
        framealpha=1.0,
        edgecolor="0.35",
        fontsize=10,
    )
    legend.get_frame().set_linewidth(0.8)
    return fig


def save_figure(figure, output_path: Path) -> None:
    output_path.parent.mkdir(parents=True, exist_ok=True)
    figure.savefig(output_path, dpi=300, bbox_inches="tight")


def render_all(
    csv_path: Path,
    output_dir: Path,
    *,
    title: str = "Hash Table Benchmark Comparison",
    fig7_algorithms: str = "all",
    fig7_title: Optional[str] = None,
    x_axis: str = "auto",
) -> None:
    """Render every paper-style plot for `csv_path` into `output_dir`.

    Files written:
      <output_dir>/paper_style_graphs.png         (combined 7-panel figure)
      <output_dir>/<metric>.png                   (one per metric)
      <output_dir>/paper_style_diagnostics.png    (diagnostics)
      <output_dir>/cache_misses.png               (Fig 7, if cache_misses_data.csv is present)

    This is the entry point that `run_benchmark_commands.py` calls. It does
    not honour the per-file --*-output overrides — those are handled by the
    legacy `main()` CLI wrapper for backwards compatibility.
    """
    csv_path = Path(csv_path)
    output_dir = Path(output_dir)
    if not csv_path.exists():
        raise SystemExit(f"CSV file not found: {csv_path}")

    rows = load_rows(csv_path)
    if not rows:
        raise SystemExit(f"No successful benchmark rows found in {csv_path}")

    x_metric, x_label = choose_x_axis(rows, x_axis)

    main_output = output_dir / MAIN_FILENAME
    figure = build_figure(rows, csv_path, title, x_metric, x_label)
    save_figure(figure, main_output)
    print(f"Wrote paper-style graph to {main_output}")

    for filename_stem, single_figure in build_single_figures(rows, title, x_metric, x_label):
        save_figure(single_figure, output_dir / f"{filename_stem}.png")
    print(f"Wrote standalone paper-style graphs to {output_dir}")

    diagnostics_output = output_dir / DIAGNOSTICS_FILENAME
    diagnostics = build_diagnostics_figure(rows, csv_path, title, x_metric, x_label)
    save_figure(diagnostics, diagnostics_output)
    print(f"Wrote diagnostics graph to {diagnostics_output}")

    fig7_csv = output_dir / FIG7_DATA_FILENAME
    if fig7_csv.exists():
        fig7_rows = load_fig7_rows(fig7_csv)
        if fig7_rows:
            fig7_output = output_dir / FIG7_PLOT_FILENAME
            fig7 = build_fig7_figure(
                fig7_rows,
                fig7_csv,
                title,
                algorithm_filter=fig7_algorithms,
                custom_subtitle=fig7_title,
            )
            save_figure(fig7, fig7_output)
            print(f"Wrote Fig 7 cache-miss curve to {fig7_output}")
        else:
            print(f"No Fig 7 rows parsed from {fig7_csv}; skipping Fig 7 plot.")
    else:
        print(f"Fig 7 CSV not found at {fig7_csv}; skipping Fig 7 plot.")


def main() -> int:
    args = parse_args()
    if not args.csv.exists():
        raise SystemExit(f"CSV file not found: {args.csv}")

    # Resolve all output paths from --output-dir unless the user overrode a
    # specific one. The folder gets created on first save_figure() call.
    output_dir = args.output_dir
    main_output = args.output if args.output is not None else output_dir / MAIN_FILENAME
    single_dir = args.single_output_dir if args.single_output_dir is not None else output_dir
    diagnostics_output = (
        args.diagnostics_output if args.diagnostics_output is not None
        else output_dir / DIAGNOSTICS_FILENAME
    )
    fig7_csv = args.fig7_csv if args.fig7_csv is not None else output_dir / FIG7_DATA_FILENAME
    fig7_output = (
        args.fig7_output if args.fig7_output is not None
        else output_dir / FIG7_PLOT_FILENAME
    )

    rows = load_rows(args.csv)
    if not rows:
        raise SystemExit(f"No successful benchmark rows found in {args.csv}")

    x_metric, x_label = choose_x_axis(rows, args.x_axis)

    figure = build_figure(rows, args.csv, args.title, x_metric, x_label)
    save_figure(figure, main_output)

    single_figures = build_single_figures(rows, args.title, x_metric, x_label)
    for filename_stem, single_figure in single_figures:
        save_figure(single_figure, single_dir / f"{filename_stem}.png")

    diagnostics = build_diagnostics_figure(rows, args.csv, args.title, x_metric, x_label)
    save_figure(diagnostics, diagnostics_output)

    print(f"Wrote paper-style graph to {main_output}")
    print(f"Wrote standalone paper-style graphs to {single_dir}")
    print(f"Wrote diagnostics graph to {diagnostics_output}")

    if fig7_csv.exists():
        fig7_rows = load_fig7_rows(fig7_csv)
        if fig7_rows:
            fig7 = build_fig7_figure(
                fig7_rows,
                fig7_csv,
                args.title,
                algorithm_filter=args.fig7_algorithms,
                custom_subtitle=args.fig7_title,
            )
            save_figure(fig7, fig7_output)
            print(f"Wrote Fig 7 cache-miss curve to {fig7_output}")
        else:
            print(f"No Fig 7 rows parsed from {fig7_csv}; skipping Fig 7 plot.")
    else:
        print(f"Fig 7 CSV not found at {fig7_csv}; skipping Fig 7 plot.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
