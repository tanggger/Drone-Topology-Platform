#!/usr/bin/env python3
"""
Generate 5 illustrative plots for:
  - observation signal poverty (weak semantic node statistics)
  - distribution drift across time/scenario/window scales

All data are synthetic (fabricated) but shaped to match the narrative:
models can latch onto scale/noise patterns rather than stable interaction rules.

Outputs (PNG) into the same folder as this script:
  middle_1_weak_semantic_stats.png
  middle_2_correlation_load_dominated.png
  middle_3_pca_clusters_by_scale.png
  right_1_distribution_drift.png
  right_2_scale_inconsistency_same_node.png
"""

from __future__ import annotations

import os
from dataclasses import dataclass
import numpy as np
import matplotlib.pyplot as plt


SEED = 7


def _setup_style() -> None:
    plt.rcParams.update(
        {
            "figure.dpi": 140,
            "savefig.dpi": 220,
            "font.size": 11,
            "axes.titlesize": 12,
            "axes.labelsize": 11,
            "axes.grid": True,
            "grid.alpha": 0.25,
            "axes.spines.top": False,
            "axes.spines.right": False,
        }
    )


def _out_dir() -> str:
    return os.path.dirname(os.path.abspath(__file__))


def _save(fig: plt.Figure, name: str) -> str:
    path = os.path.join(_out_dir(), name)
    fig.tight_layout()
    fig.savefig(path, bbox_inches="tight")
    plt.close(fig)
    return path


@dataclass(frozen=True)
class FeatureSpec:
    names: list[str]


FEATURES = FeatureSpec(
    names=[
        "tx_packets",
        "rx_packets",
        "burst_count",
        "rssi_mean",
        "rssi_std",
        "psd_mean",
        "psd_std",
        "queue_mean",
        "retx_rate",
    ]
)


def synth_feature_vector(rng: np.random.Generator) -> tuple[np.ndarray, np.ndarray]:
    """
    Two windows from the SAME node:
      - underlying interaction rule is assumed stable (latent)
      - observed weak stats are noisy and slightly drifting
    Return (w1, w2) in standardized-ish units.
    """
    # baseline "weak semantics" stats: mostly load + channel noise
    base = np.array([520, 505, 34, -68.0, 3.2, -92.0, 4.8, 0.42, 0.06], dtype=float)

    # window 1: mild noise
    w1 = base + rng.normal([0, 0, 0, 0, 0, 0, 0, 0, 0], [45, 48, 6, 1.7, 0.7, 1.2, 0.9, 0.08, 0.02])

    # window 2: drift (e.g., different scenario / RTK drift / interference)
    drift = np.array([+40, -30, +10, -2.0, +0.8, -3.0, +1.2, +0.10, +0.03])
    w2 = base + drift + rng.normal(
        [0, 0, 0, 0, 0, 0, 0, 0, 0], [55, 60, 9, 2.5, 1.0, 2.0, 1.4, 0.11, 0.03]
    )

    return w1, w2


def plot_middle_1_weak_semantic_stats(rng: np.random.Generator) -> str:
    w1, w2 = synth_feature_vector(rng)

    # normalize for visualization only (z-score using hand-picked scales)
    scales = np.array([400, 400, 40, 20, 6, 30, 8, 1.0, 0.2], dtype=float)
    v1 = w1 / scales
    v2 = w2 / scales

    x = np.arange(len(FEATURES.names))
    fig, ax = plt.subplots(figsize=(10.6, 3.8))
    ax.bar(x - 0.18, v1, width=0.36, label="Same node • window A", color="#4C78A8")
    ax.bar(x + 0.18, v2, width=0.36, label="Same node • window B (drift)", color="#F58518")
    ax.set_xticks(x)
    ax.set_xticklabels(FEATURES.names, rotation=20, ha="right")
    ax.set_ylabel("Normalized statistic (unitless)")
    ax.set_title("Middle-1: Node features are weak semantic aggregates (counts / RSSI / PSD stats)")
    ax.axhline(0, color="black", linewidth=1, alpha=0.6)
    ax.text(
        0.02,
        0.95,
        "Same interaction rule (latent) • Observations are just noisy statistics",
        transform=ax.transAxes,
        va="top",
        fontsize=10,
        bbox=dict(boxstyle="round,pad=0.25", facecolor="white", alpha=0.85, edgecolor="#999999"),
    )
    ax.legend(frameon=True, fontsize=10)
    return _save(fig, "middle_1_weak_semantic_stats.png")


def _synth_dataset_for_correlation(
    rng: np.random.Generator, n: int = 900
) -> tuple[np.ndarray, list[str], np.ndarray, np.ndarray]:
    """
    Build synthetic samples with:
      - latent 'interaction' (stable rule) weakly reflected in weak features
      - strong 'scale' + 'scenario' effects dominating distributions
    Returns X, feature_names, interaction_label, window_scale
    """
    # latent variables
    interaction = rng.integers(0, 3, size=n)  # 3 interaction regimes (e.g., cooperative/neutral/contested)
    scenario = rng.integers(0, 3, size=n)     # 0/1/2 (easy/moderate/hard) -> drift
    scale = rng.choice(np.array([1.0, 5.0, 20.0]), size=n, p=[0.45, 0.35, 0.20])  # window seconds

    # base load varies mostly with scenario + scale (spurious)
    load = (1.0 + 0.55 * scenario) * (scale ** 0.65) * rng.lognormal(mean=0.1, sigma=0.22, size=n)

    # weak semantic features driven by load + channel noise
    tx_packets = 220 * load + rng.normal(0, 60, size=n)
    rx_packets = tx_packets * (0.995 - 0.01 * scenario) + rng.normal(0, 70, size=n)
    burst_count = 6.0 * (scale ** 0.7) * (1.0 + 0.1 * scenario) + rng.normal(0, 3.5, size=n)

    # RSSI/PSD drift by scenario; also slightly depends on scale (estimation bias)
    rssi_mean = -63.0 - 4.5 * scenario - 0.7 * np.log(scale) + rng.normal(0, 2.2, size=n)
    rssi_std = 2.2 + 0.9 * scenario + 0.3 * np.log(scale) + rng.normal(0, 0.5, size=n)
    psd_mean = -88.0 - 5.5 * scenario - 0.3 * np.log(scale) + rng.normal(0, 2.0, size=n)
    psd_std = 3.4 + 1.1 * scenario + 0.25 * np.log(scale) + rng.normal(0, 0.6, size=n)

    # queue and retransmission mostly follow load; interaction only very weakly affects retx
    queue_mean = np.clip(0.05 + 0.18 * (load / (1.0 + load)) + 0.02 * scenario + rng.normal(0, 0.03, size=n), 0, 1)
    retx_rate = np.clip(0.02 + 0.015 * scenario + 0.006 * interaction + 0.01 * rng.random(n) + rng.normal(0, 0.01, size=n), 0, 0.25)

    X = np.vstack(
        [
            tx_packets,
            rx_packets,
            burst_count,
            rssi_mean,
            rssi_std,
            psd_mean,
            psd_std,
            queue_mean,
            retx_rate,
        ]
    ).T

    return X, FEATURES.names, interaction, scale


def plot_middle_2_correlation_load_dominated(rng: np.random.Generator) -> str:
    X, names, _, _ = _synth_dataset_for_correlation(rng, n=1200)

    # standardize columns to compute correlation (avoid scale dominance in corr)
    Xz = (X - X.mean(axis=0)) / (X.std(axis=0) + 1e-9)
    corr = (Xz.T @ Xz) / (Xz.shape[0] - 1)

    fig, ax = plt.subplots(figsize=(8.8, 7.2))
    im = ax.imshow(corr, vmin=-1, vmax=1, cmap="coolwarm")
    ax.set_xticks(np.arange(len(names)))
    ax.set_yticks(np.arange(len(names)))
    ax.set_xticklabels(names, rotation=45, ha="right")
    ax.set_yticklabels(names)
    ax.set_title("Middle-2: Weak stats are strongly correlated (load-dominated), semantic content is thin")

    # annotate values (lightweight)
    for i in range(len(names)):
        for j in range(len(names)):
            v = corr[i, j]
            if i != j and abs(v) > 0.55:
                ax.text(j, i, f"{v:.2f}", ha="center", va="center", fontsize=8, color="black")

    cbar = fig.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
    cbar.set_label("Pearson correlation")
    ax.text(
        0.02,
        0.02,
        "High correlation ⇒ model can shortcut via scale/load patterns",
        transform=ax.transAxes,
        fontsize=10,
        bbox=dict(boxstyle="round,pad=0.25", facecolor="white", alpha=0.85, edgecolor="#999999"),
    )
    return _save(fig, "middle_2_correlation_load_dominated.png")


def _pca_2d(X: np.ndarray) -> np.ndarray:
    # center
    Xc = X - X.mean(axis=0, keepdims=True)
    # SVD
    U, S, Vt = np.linalg.svd(Xc, full_matrices=False)
    Z = U[:, :2] * S[:2]
    return Z


def plot_middle_3_pca_clusters_by_scale(rng: np.random.Generator) -> str:
    X, _, interaction, scale = _synth_dataset_for_correlation(rng, n=900)

    # z-score features to make PCA about patterns not magnitude
    Xz = (X - X.mean(axis=0)) / (X.std(axis=0) + 1e-9)
    Z = _pca_2d(Xz)

    fig, ax = plt.subplots(figsize=(9.4, 5.8))

    # color by window scale (the spurious clustering we want to highlight)
    scale_vals = np.array([1.0, 5.0, 20.0])
    colors = {1.0: "#4C78A8", 5.0: "#F58518", 20.0: "#54A24B"}
    labels = {1.0: "1s window", 5.0: "5s window", 20.0: "20s window"}

    for s in scale_vals:
        idx = scale == s
        ax.scatter(Z[idx, 0], Z[idx, 1], s=16, alpha=0.55, c=colors[s], label=labels[s], edgecolors="none")

    # overlay interaction label as markers (they overlap -> hard to separate)
    markers = {0: "o", 1: "s", 2: "^"}
    for k in [0, 1, 2]:
        idx = interaction == k
        ax.scatter(Z[idx, 0], Z[idx, 1], s=10, facecolors="none", edgecolors="black", linewidths=0.35, marker=markers[k], alpha=0.25)

    ax.set_title("Middle-3: Embedding clusters by window scale (spurious), not by interaction regime (thin signal)")
    ax.set_xlabel("PC1")
    ax.set_ylabel("PC2")
    ax.legend(loc="best", frameon=True, fontsize=10)
    ax.text(
        0.02,
        0.98,
        "Color = window scale (dominant)\nMarker outline = interaction label (overlapping)",
        transform=ax.transAxes,
        va="top",
        fontsize=10,
        bbox=dict(boxstyle="round,pad=0.25", facecolor="white", alpha=0.85, edgecolor="#999999"),
    )
    return _save(fig, "middle_3_pca_clusters_by_scale.png")


def plot_right_1_distribution_drift(rng: np.random.Generator) -> str:
    """
    Show distribution drift of a weak statistic across scenarios/time:
      e.g., RSSI mean shifts + variance changes.
    """
    n = 700
    scenarios = ["Easy", "Moderate", "Hard"]
    colors = ["#4C78A8", "#F58518", "#E45756"]

    # fabricate feature distributions
    rssi = {
        "Easy": rng.normal(-63.0, 1.6, size=n),
        "Moderate": rng.normal(-67.5, 2.2, size=n),
        "Hard": rng.normal(-72.0, 3.0, size=n),
    }

    fig, ax = plt.subplots(figsize=(9.2, 4.8))
    bins = np.linspace(-82, -56, 34)
    for sc, c in zip(scenarios, colors):
        ax.hist(rssi[sc], bins=bins, density=True, alpha=0.35, color=c, label=sc, edgecolor="none")
        ax.axvline(np.mean(rssi[sc]), color=c, linewidth=2)

    ax.set_title("Right-1: Distribution drift across scenarios (example: RSSI mean)")
    ax.set_xlabel("RSSI mean (dBm)  ← weaker channel / interference")
    ax.set_ylabel("Density")
    ax.legend(frameon=True)
    ax.text(
        0.02,
        0.95,
        "Same feature, different scenario ⇒ shifted mean + changed variance\nModel may learn scenario/scale signatures instead of interaction rules",
        transform=ax.transAxes,
        va="top",
        fontsize=10,
        bbox=dict(boxstyle="round,pad=0.25", facecolor="white", alpha=0.85, edgecolor="#999999"),
    )
    return _save(fig, "right_1_distribution_drift.png")


def plot_right_2_scale_inconsistency_same_node(rng: np.random.Generator) -> str:
    """
    Same node, same underlying behaviour; compute a weak stat (burst rate)
    at different window scales -> inconsistent time series.
    """
    T = 240  # seconds
    t = np.arange(T)

    # latent "interaction intensity" stable-ish with one event (RTK drift / interference)
    latent = 0.55 + 0.08 * np.sin(2 * np.pi * t / 90)  # slow
    event = np.exp(-0.5 * ((t - 150) / 12) ** 2)  # sudden disturbance around t=150
    latent = latent - 0.18 * event

    # observed packet bursts as an inhomogeneous Poisson process (weak semantic)
    lam = np.clip(8.0 + 10.0 * latent + 6.0 * rng.normal(0, 0.08, size=T), 1.0, None)
    bursts_per_sec = rng.poisson(lam)

    def window_stat(series: np.ndarray, win: int) -> np.ndarray:
        # burst rate per second estimated on window, but biased by win-size + noise
        kernel = np.ones(win) / win
        sm = np.convolve(series, kernel, mode="same")
        # add scale-dependent bias (what a model could latch onto)
        bias = 0.6 * np.log(win) + 0.35 * rng.normal(0, 1.0, size=series.shape[0])
        return sm + bias

    y1 = window_stat(bursts_per_sec, win=1)
    y5 = window_stat(bursts_per_sec, win=5)
    y20 = window_stat(bursts_per_sec, win=20)

    fig, ax = plt.subplots(figsize=(10.0, 4.6))
    ax.plot(t, y1, color="#4C78A8", linewidth=1.0, alpha=0.75, label="1s window")
    ax.plot(t, y5, color="#F58518", linewidth=1.6, alpha=0.85, label="5s window")
    ax.plot(t, y20, color="#54A24B", linewidth=2.2, alpha=0.90, label="20s window")
    ax.axvspan(140, 165, color="#E45756", alpha=0.10, label="disturbance period")

    ax.set_title("Right-2: Same node, different window scales ⇒ inconsistent features (scale/noise patterns)")
    ax.set_xlabel("Time (s)")
    ax.set_ylabel("Estimated burst rate (a weak statistic)")
    ax.legend(frameon=True, ncol=2)
    ax.text(
        0.02,
        0.95,
        "Even for the SAME node, feature trajectories differ by window size\n⇒ scale shortcut risk; interaction rules become non-stationary to the model",
        transform=ax.transAxes,
        va="top",
        fontsize=10,
        bbox=dict(boxstyle="round,pad=0.25", facecolor="white", alpha=0.85, edgecolor="#999999"),
    )
    return _save(fig, "right_2_scale_inconsistency_same_node.png")


def main() -> None:
    _setup_style()
    rng = np.random.default_rng(SEED)

    paths = []
    paths.append(plot_middle_1_weak_semantic_stats(rng))
    paths.append(plot_middle_2_correlation_load_dominated(rng))
    paths.append(plot_middle_3_pca_clusters_by_scale(rng))
    paths.append(plot_right_1_distribution_drift(rng))
    paths.append(plot_right_2_scale_inconsistency_same_node(rng))

    print("Generated:")
    for p in paths:
        print(" -", p)


if __name__ == "__main__":
    main()

