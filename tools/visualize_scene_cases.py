#!/usr/bin/env python3
"""
Generate side-by-side visualizations for the validated environment smoke cases.
"""

from __future__ import annotations

import json
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


ROOT = Path(__file__).resolve().parents[1]
OUTPUT_DIR = ROOT / "output" / "scene_case_visualizations"

CASE_DIRS = {
    "urban": ROOT / "output" / "smoke_env_doc_align_urban_los",
    "forest": ROOT / "output" / "smoke_env_doc_align_forest_nomap_flags",
    "lake": ROOT / "output" / "smoke_env_doc_align_lake_nomap_flags",
    "open-field": ROOT / "output" / "smoke_env_doc_align_open_field",
}

CASE_COLORS = {
    "urban": "#C44E52",
    "forest": "#55A868",
    "lake": "#4C78A8",
    "open-field": "#F58518",
}


def load_case(case_name: str, case_dir: Path) -> dict:
    with open(case_dir / "environment_summary.json", "r", encoding="utf-8") as f:
        summary = json.load(f)

    qos = pd.read_csv(case_dir / "qos_performance.csv")
    topo = pd.read_csv(case_dir / "topology_evolution.csv")
    positions = pd.read_csv(case_dir / "rtk-node-positions.csv")

    pdr_cols = [c for c in qos.columns if c.endswith("_pdr")]
    delay_cols = [c for c in qos.columns if c.endswith("_delay")]
    throughput_cols = [c for c in qos.columns if c.endswith("_throughput")]

    qos = qos.copy()
    qos["avg_pdr"] = qos[pdr_cols].mean(axis=1) * 100.0
    qos["avg_delay_ms"] = qos[delay_cols].mean(axis=1) * 1000.0
    qos["avg_throughput_mbps"] = qos[throughput_cols].mean(axis=1) / 1e6

    latest_t = positions["time"].max()
    latest_positions = positions[positions["time"] == latest_t].copy()

    return {
        "name": case_name,
        "dir": case_dir,
        "summary": summary,
        "qos": qos,
        "topo": topo,
        "positions": latest_positions,
        "metrics": {
            "avg_pdr": float(qos["avg_pdr"].mean()),
            "avg_delay_ms": float(qos["avg_delay_ms"].mean()),
            "avg_throughput_mbps": float(qos["avg_throughput_mbps"].mean()),
            "avg_connectivity_pct": float(topo["connectivity"].mean() * 100.0),
        },
    }


def plot_metrics_overview(cases: dict[str, dict]) -> Path:
    fig, axes = plt.subplots(2, 2, figsize=(14, 9))
    axes = axes.flatten()

    metric_specs = [
        ("avg_pdr", "Average PDR (%)"),
        ("avg_delay_ms", "Average Delay (ms)"),
        ("avg_throughput_mbps", "Average Throughput (Mbps)"),
        ("avg_connectivity_pct", "Average Connectivity (%)"),
    ]
    labels = list(cases.keys())
    colors = [CASE_COLORS[name] for name in labels]

    for ax, (metric_key, title) in zip(axes, metric_specs):
        values = [cases[name]["metrics"][metric_key] for name in labels]
        bars = ax.bar(labels, values, color=colors, alpha=0.9)
        ax.set_title(title)
        ax.grid(axis="y", alpha=0.25)
        for bar, value in zip(bars, values):
            ax.text(bar.get_x() + bar.get_width() / 2,
                    bar.get_height(),
                    f"{value:.2f}",
                    ha="center",
                    va="bottom",
                    fontsize=9)

    fig.suptitle("Environment Smoke Cases: Metrics Overview", fontsize=15)
    fig.tight_layout()
    out = OUTPUT_DIR / "scene_case_metrics_overview.png"
    fig.savefig(out, dpi=220, bbox_inches="tight")
    plt.close(fig)
    return out


def plot_timeseries(cases: dict[str, dict]) -> Path:
    fig, axes = plt.subplots(2, 1, figsize=(14, 9), sharex=True)

    for name, case in cases.items():
        color = CASE_COLORS[name]
        axes[0].plot(case["qos"]["time"], case["qos"]["avg_pdr"], label=name, color=color, linewidth=2)
        axes[1].plot(case["topo"]["time"], case["topo"]["connectivity"] * 100.0,
                     label=name, color=color, linewidth=2)

    axes[0].set_title("Average PDR over Time")
    axes[0].set_ylabel("PDR (%)")
    axes[0].grid(alpha=0.25)
    axes[0].legend(ncol=4, fontsize=9)

    axes[1].set_title("Topology Connectivity over Time")
    axes[1].set_xlabel("Time (s)")
    axes[1].set_ylabel("Connectivity (%)")
    axes[1].grid(alpha=0.25)

    fig.tight_layout()
    out = OUTPUT_DIR / "scene_case_timeseries.png"
    fig.savefig(out, dpi=220, bbox_inches="tight")
    plt.close(fig)
    return out


def plot_position_snapshots(cases: dict[str, dict]) -> Path:
    fig, axes = plt.subplots(2, 2, figsize=(12, 10))
    axes = axes.flatten()

    for ax, (name, case) in zip(axes, cases.items()):
        positions = case["positions"]
        ax.scatter(positions["x"], positions["y"], s=120, color=CASE_COLORS[name], edgecolors="black")
        for _, row in positions.iterrows():
            ax.text(row["x"], row["y"], str(int(row["nodeId"])), fontsize=8, ha="center", va="center")

        summary = case["summary"]
        ax.set_title(
            f"{name}\nmodel={summary['baseModel']}\nsource={summary['environmentSource']}",
            fontsize=10,
        )
        ax.set_xlabel("X (m)")
        ax.set_ylabel("Y (m)")
        ax.grid(alpha=0.2)
        ax.set_aspect("equal", adjustable="box")

    fig.suptitle("Latest UAV Position Snapshots by Scene", fontsize=15)
    fig.tight_layout()
    out = OUTPUT_DIR / "scene_case_position_snapshots.png"
    fig.savefig(out, dpi=220, bbox_inches="tight")
    plt.close(fig)
    return out


def plot_case_detail(case: dict) -> Path:
    fig, axes = plt.subplots(2, 2, figsize=(14, 10))
    color = CASE_COLORS[case["name"]]
    qos = case["qos"]
    topo = case["topo"]
    positions = case["positions"]
    summary = case["summary"]

    axes[0, 0].plot(qos["time"], qos["avg_pdr"], color=color, linewidth=2)
    axes[0, 0].set_title("Average PDR")
    axes[0, 0].set_ylabel("PDR (%)")
    axes[0, 0].grid(alpha=0.25)

    axes[0, 1].plot(qos["time"], qos["avg_delay_ms"], color=color, linewidth=2)
    axes[0, 1].set_title("Average Delay")
    axes[0, 1].set_ylabel("Delay (ms)")
    axes[0, 1].grid(alpha=0.25)

    axes[1, 0].plot(topo["time"], topo["connectivity"] * 100.0, color=color, linewidth=2)
    axes[1, 0].set_title("Topology Connectivity")
    axes[1, 0].set_xlabel("Time (s)")
    axes[1, 0].set_ylabel("Connectivity (%)")
    axes[1, 0].grid(alpha=0.25)

    axes[1, 1].scatter(positions["x"], positions["y"], s=130, color=color, edgecolors="black")
    for _, row in positions.iterrows():
        axes[1, 1].text(row["x"], row["y"], str(int(row["nodeId"])), fontsize=8, ha="center", va="center")
    axes[1, 1].set_title("Latest UAV positions")
    axes[1, 1].set_xlabel("X (m)")
    axes[1, 1].set_ylabel("Y (m)")
    axes[1, 1].grid(alpha=0.2)
    axes[1, 1].set_aspect("equal", adjustable="box")

    fig.suptitle(
        f"{case['name']} | model={summary['baseModel']} | source={summary['environmentSource']}",
        fontsize=14,
    )
    fig.tight_layout()
    out = OUTPUT_DIR / f"scene_case_{case['name']}_detail.png"
    fig.savefig(out, dpi=220, bbox_inches="tight")
    plt.close(fig)
    return out


def main() -> None:
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    cases = {name: load_case(name, case_dir) for name, case_dir in CASE_DIRS.items()}

    outputs = [
        plot_metrics_overview(cases),
        plot_timeseries(cases),
        plot_position_snapshots(cases),
    ]
    for case in cases.values():
        outputs.append(plot_case_detail(case))

    print("Generated comparison figures:")
    for path in outputs:
        print(path)


if __name__ == "__main__":
    main()
