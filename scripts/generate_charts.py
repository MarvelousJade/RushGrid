#!/usr/bin/env python3
"""Generate dependency-free SVG charts from a RushGrid benchmark CSV."""

from __future__ import annotations

import argparse
import csv
import math
import statistics
from collections import defaultdict
from pathlib import Path


WIDTH = 960
HEIGHT = 540
MARGIN_LEFT = 92
MARGIN_RIGHT = 36
MARGIN_TOP = 64
MARGIN_BOTTOM = 78
PLOT_WIDTH = WIDTH - MARGIN_LEFT - MARGIN_RIGHT
PLOT_HEIGHT = HEIGHT - MARGIN_TOP - MARGIN_BOTTOM


def load_rows(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def mean_groups(
    rows: list[dict[str, str]],
    key_fields: tuple[str, ...],
) -> dict[tuple[str, ...], float]:
    grouped: dict[tuple[str, ...], list[float]] = defaultdict(list)
    for row in rows:
        grouped[tuple(row[field] for field in key_fields)].append(
            float(row["average_ms"])
        )
    return {key: statistics.fmean(values) for key, values in grouped.items()}


def svg_start(title: str, subtitle: str) -> list[str]:
    return [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" '
        f'height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">',
        "<style>"
        "text{font-family:Segoe UI,Arial,sans-serif;fill:#dce7f4}"
        ".title{font-size:26px;font-weight:700}"
        ".sub{font-size:14px;fill:#92a8be}"
        ".axis{font-size:13px;fill:#aebfd0}"
        ".grid{stroke:#32465b;stroke-width:1}"
        ".line{fill:none;stroke-width:4}"
        "</style>",
        f'<rect width="{WIDTH}" height="{HEIGHT}" fill="#101820"/>',
        f'<text x="{MARGIN_LEFT}" y="34" class="title">{title}</text>',
        f'<text x="{MARGIN_LEFT}" y="55" class="sub">{subtitle}</text>',
    ]


def write_entity_chart(rows: list[dict[str, str]], output: Path) -> None:
    selected = [
        row
        for row in rows
        if (
            row["implementation"] == "optimized"
            and row["workers"] == "5"
            and row["measured_ticks"] == "300"
        )
        or (
            row["implementation"] == "baseline"
            and row["workers"] == "0"
            and row["measured_ticks"] == "300"
        )
    ]
    means = mean_groups(selected, ("implementation", "agents"))
    counts = [1000, 5000, 10000, 25000, 50000]
    minimum_log = -1.0
    maximum_log = 2.0

    def x_position(index: int) -> float:
        return MARGIN_LEFT + index * PLOT_WIDTH / (len(counts) - 1)

    def y_position(milliseconds: float) -> float:
        value = (math.log10(milliseconds) - minimum_log) / (
            maximum_log - minimum_log
        )
        return MARGIN_TOP + (1.0 - value) * PLOT_HEIGHT

    svg = svg_start(
        "Simulation cost by agent count",
        "Release mean of three runs · logarithmic milliseconds",
    )
    for value in [0.1, 1.0, 10.0, 100.0]:
        y = y_position(value)
        svg.append(
            f'<line x1="{MARGIN_LEFT}" y1="{y:.2f}" '
            f'x2="{WIDTH - MARGIN_RIGHT}" y2="{y:.2f}" class="grid"/>'
        )
        svg.append(
            f'<text x="{MARGIN_LEFT - 12}" y="{y + 5:.2f}" '
            f'text-anchor="end" class="axis">{value:g} ms</text>'
        )
    for index, count in enumerate(counts):
        x = x_position(index)
        svg.append(
            f'<text x="{x:.2f}" y="{HEIGHT - 42}" text-anchor="middle" '
            f'class="axis">{count // 1000}k</text>'
        )

    series = [
        ("optimized", "#46c2ff", "Optimized"),
        ("baseline", "#ffb347", "Baseline"),
    ]
    for implementation, color, label in series:
        points: list[tuple[float, float]] = []
        for index, count in enumerate(counts):
            value = means.get((implementation, str(count)))
            if value is not None:
                points.append((x_position(index), y_position(value)))
        svg.append(
            f'<polyline points="{" ".join(f"{x:.2f},{y:.2f}" for x, y in points)}" '
            f'class="line" stroke="{color}"/>'
        )
        for x, y in points:
            svg.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="6" fill="{color}"/>')
        legend_x = WIDTH - 250
        legend_y = 88 if implementation == "optimized" else 114
        svg.append(
            f'<line x1="{legend_x}" y1="{legend_y}" x2="{legend_x + 34}" '
            f'y2="{legend_y}" stroke="{color}" stroke-width="4"/>'
        )
        svg.append(
            f'<text x="{legend_x + 44}" y="{legend_y + 5}" class="axis">{label}</text>'
        )
    svg.append(
        f'<text x="{MARGIN_LEFT + PLOT_WIDTH / 2:.2f}" y="{HEIGHT - 14}" '
        'text-anchor="middle" class="axis">Active agents</text>'
    )
    svg.append("</svg>")
    output.write_text("\n".join(svg) + "\n", encoding="utf-8")


def write_worker_chart(rows: list[dict[str, str]], output: Path) -> None:
    selected = [
        row
        for row in rows
        if row["implementation"] == "optimized"
        and row["agents"] == "50000"
        and row["workers"] in {"0", "1", "3", "5", "11"}
    ]
    means = mean_groups(selected, ("workers",))
    workers = [0, 1, 3, 5, 11]
    maximum = 55.0
    bar_slot = PLOT_WIDTH / len(workers)
    bar_width = bar_slot * 0.55
    svg = svg_start(
        "50,000-agent worker scaling",
        "Lower is better · caller helps while waiting",
    )
    for value in range(0, 51, 10):
        y = MARGIN_TOP + (1.0 - value / maximum) * PLOT_HEIGHT
        svg.append(
            f'<line x1="{MARGIN_LEFT}" y1="{y:.2f}" '
            f'x2="{WIDTH - MARGIN_RIGHT}" y2="{y:.2f}" class="grid"/>'
        )
        svg.append(
            f'<text x="{MARGIN_LEFT - 12}" y="{y + 5:.2f}" '
            f'text-anchor="end" class="axis">{value} ms</text>'
        )
    for index, worker_count in enumerate(workers):
        value = means[(str(worker_count),)]
        height = value / maximum * PLOT_HEIGHT
        x = MARGIN_LEFT + index * bar_slot + (bar_slot - bar_width) / 2
        y = MARGIN_TOP + PLOT_HEIGHT - height
        color = "#46c2ff" if worker_count else "#ffb347"
        svg.append(
            f'<rect x="{x:.2f}" y="{y:.2f}" width="{bar_width:.2f}" '
            f'height="{height:.2f}" rx="4" fill="{color}"/>'
        )
        svg.append(
            f'<text x="{x + bar_width / 2:.2f}" y="{y - 10:.2f}" '
            f'text-anchor="middle" class="axis">{value:.2f}</text>'
        )
        svg.append(
            f'<text x="{x + bar_width / 2:.2f}" y="{HEIGHT - 42}" '
            f'text-anchor="middle" class="axis">{worker_count}</text>'
        )
    svg.append(
        f'<text x="{MARGIN_LEFT + PLOT_WIDTH / 2:.2f}" y="{HEIGHT - 14}" '
        'text-anchor="middle" class="axis">Persistent workers</text>'
    )
    svg.append("</svg>")
    output.write_text("\n".join(svg) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "csv_path",
        nargs="?",
        type=Path,
        default=Path("artifacts/csv/sample.csv"),
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("artifacts/charts"),
    )
    arguments = parser.parse_args()
    rows = load_rows(arguments.csv_path)
    arguments.output_dir.mkdir(parents=True, exist_ok=True)
    write_entity_chart(rows, arguments.output_dir / "entity_scaling.svg")
    write_worker_chart(rows, arguments.output_dir / "worker_scaling.svg")


if __name__ == "__main__":
    main()
