#!/usr/bin/env python3
"""
非合作推荐算法有效性评估脚本。

目标：
1. 对比推荐目标、结构基线目标、随机目标、oracle 目标的打击效果
2. 量化当前推荐算法是否已经足够有效
3. 给出是否需要继续增强算法的结论
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import shutil
import sys
from dataclasses import asdict
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Tuple

MPL_CONFIG_DIR = Path("/tmp/matplotlib-codex-cache")
MPL_CONFIG_DIR.mkdir(parents=True, exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", str(MPL_CONFIG_DIR))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

from run_current_feature_validation import (
    DEFAULT_OUTPUT_BASE,
    FONT_CANDIDATES,
    ROOT,
    SCENE_MAP_FILES,
    RunSpec,
    build_ns3_command,
    build_ns3,
    coerce_numeric,
    ensure_csv,
    ensure_json,
    pick_recommended_strike_target,
    run_checked,
    slugify,
    summarize_noncooperative_attack_run,
)


plt.rcParams["font.sans-serif"] = FONT_CANDIDATES
plt.rcParams["axes.unicode_minus"] = False


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="非合作推荐算法有效性评估")
    parser.add_argument("--scenes", nargs="*", default=["urban", "forest", "lake", "open-field"])
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--difficulty", default="Moderate")
    parser.add_argument("--formation", default="v_formation")
    parser.add_argument("--num-uavs", type=int, default=15)
    parser.add_argument("--num-channels", type=int, default=4)
    parser.add_argument("--strategy", default="dynamic")
    parser.add_argument("--attack-evaluation-duration", type=float, default=8.0)
    parser.add_argument("--oracle-max-candidates", type=int, default=8)
    parser.add_argument("--seed", type=int, default=20260325)
    parser.add_argument("--skip-build", action="store_true")
    return parser.parse_args()


def attack_execute_time_for(duration: float, attack_eval_duration: float) -> float:
    return round(max(duration * 0.55, duration - attack_eval_duration - 1.0), 2)


def damage_score(result: Dict) -> float:
    def pos_loss(key: str) -> float:
        value = result.get(key)
        if value is None or (isinstance(value, float) and math.isnan(value)):
            return 0.0
        return max(0.0, -float(value))

    def pos_gain(key: str, scale: float = 1.0) -> float:
        value = result.get(key)
        if value is None or (isinstance(value, float) and math.isnan(value)):
            return 0.0
        return max(0.0, float(value) / scale)

    return (
        2.0 * pos_loss("global_connectivity_delta")
        + 1.0 * pos_loss("global_pdr_delta")
        + 1.0 * pos_loss("local_pdr_delta")
        + 0.5 * pos_loss("global_throughput_delta_mbps")
        + pos_gain("global_delay_delta_ms", 100.0)
        + pos_gain("local_delay_delta_ms", 100.0)
    )


def create_base_spec(
    scene: str,
    args: argparse.Namespace,
) -> RunSpec:
    return RunSpec(
        name=f"recommendation_eval_{scene}",
        operation_mode="non_cooperative",
        scene_type=scene,
        difficulty=args.difficulty,
        formation=args.formation,
        duration=args.duration,
        num_uavs=args.num_uavs,
        num_channels=args.num_channels,
        strategy=args.strategy,
        map_file=SCENE_MAP_FILES.get(scene),
        enable_non_cooperative_attack=True,
        attack_type="node_strike",
        manual_strike_target=-1,
        attack_execute_time=attack_execute_time_for(args.duration, args.attack_evaluation_duration),
        attack_evaluation_duration=args.attack_evaluation_duration,
        attack_neighborhood_hop=1,
    )


def run_preflight_probe(spec: RunSpec, probe_dir: Path) -> pd.DataFrame:
    probe_spec = RunSpec(**asdict(spec))
    probe_spec.manual_strike_target = -1
    if probe_dir.exists():
        shutil.rmtree(probe_dir)
    probe_dir.mkdir(parents=True, exist_ok=True)

    (probe_dir / "run_spec.json").write_text(
        json.dumps(asdict(probe_spec), ensure_ascii=False, indent=2), encoding="utf-8"
    )
    command = build_ns3_command(probe_spec, probe_dir)
    run_checked([str(ROOT / "ns3"), "run", command, "--no-build"], ROOT, probe_dir / "simulation.log")
    recommendations = coerce_numeric(
        ensure_csv(probe_dir / "noncooperative_attack_recommendations.csv"),
        [
            "windowStart",
            "windowEnd",
            "recommendedObservedNodeId",
            "recommendedScore",
            "recommendationRank",
            "structureScore",
            "evidenceSupportScore",
            "causalSupportScore",
            "directionalInfluenceScore",
            "temporalStabilityScore",
            "localBridgeScore",
            "postRemovalDamageScore",
            "twoHopReachabilityScore",
            "interClusterBridgeScore",
            "localCutRiskScore",
            "neighborRedundancyPenalty",
        ],
    )
    return recommendations


def candidate_pool_from_recommendations(
    recommendations: pd.DataFrame,
    attack_execute_time: float,
    max_candidates: int,
) -> pd.DataFrame:
    if recommendations.empty:
        return pd.DataFrame()

    ranked = recommendations.copy()
    if "windowEnd" in ranked.columns:
        ranked = ranked.loc[ranked["windowEnd"] <= attack_execute_time + 1e-6].copy()
    if ranked.empty:
        ranked = recommendations.copy()

    grouped = (
        ranked.groupby("recommendedObservedNodeId")
        .agg(
            recommendation_score=("recommendedScore", "max"),
            structure_score=("structureScore", "max"),
            evidence_support_score=("evidenceSupportScore", "max"),
            causal_support_score=("causalSupportScore", "max"),
            directional_influence_score=("directionalInfluenceScore", "max"),
            temporal_stability_score=("temporalStabilityScore", "max"),
            local_bridge_score=("localBridgeScore", "max"),
            post_removal_damage_score=("postRemovalDamageScore", "max"),
            two_hop_reachability_score=("twoHopReachabilityScore", "max"),
            inter_cluster_bridge_score=("interClusterBridgeScore", "max"),
            local_cut_risk_score=("localCutRiskScore", "max"),
            neighbor_redundancy_penalty=("neighborRedundancyPenalty", "max"),
        )
        .reset_index()
        .sort_values(["recommendation_score", "structure_score"], ascending=False)
    )
    if max_candidates > 0:
        grouped = grouped.head(max_candidates)
    return grouped


def select_structure_target(candidate_pool: pd.DataFrame) -> Optional[int]:
    if candidate_pool.empty:
        return None
    ordered = candidate_pool.sort_values(
        ["structure_score", "recommendation_score", "recommendedObservedNodeId"],
        ascending=[False, False, True],
    )
    return int(ordered.iloc[0]["recommendedObservedNodeId"])


def select_random_target(candidate_pool: pd.DataFrame, seed: int) -> Optional[int]:
    if candidate_pool.empty:
        return None
    ids = [int(v) for v in candidate_pool["recommendedObservedNodeId"].dropna().tolist()]
    if not ids:
        return None
    rng = random.Random(seed)
    return rng.choice(ids)


def build_strategy_spec(base_spec: RunSpec, name: str, target: int) -> RunSpec:
    spec = RunSpec(**asdict(base_spec))
    spec.name = name
    spec.manual_strike_target = target
    return spec


def run_attack_evaluation(spec: RunSpec, run_dir: Path) -> Dict:
    if run_dir.exists():
        shutil.rmtree(run_dir)
    run_dir.mkdir(parents=True, exist_ok=True)

    (run_dir / "run_spec.json").write_text(
        json.dumps(asdict(spec), ensure_ascii=False, indent=2), encoding="utf-8"
    )
    command = build_ns3_command(spec, run_dir)
    run_checked([str(ROOT / "ns3"), "run", command, "--no-build"], ROOT, run_dir / "simulation.log")
    summary = summarize_noncooperative_attack_run(spec, run_dir, None)
    summary["damage_score"] = damage_score(summary)
    return summary


def effectiveness_label(rec_score: float, structure_score: float, random_score: float, oracle_score: float) -> str:
    if oracle_score <= 0:
        return "insufficient_evidence"
    oracle_ratio = rec_score / oracle_score
    if rec_score >= max(structure_score, random_score) and oracle_ratio >= 0.90:
        return "strong"
    if rec_score >= max(structure_score, random_score) * 0.95 and oracle_ratio >= 0.75:
        return "acceptable"
    return "needs_strengthening"


def write_scene_json(output_dir: Path, payload: Dict) -> None:
    output_dir.write_text(json.dumps(payload, ensure_ascii=False, indent=2), encoding="utf-8")


def plot_scene_damage_bars(scene_df: pd.DataFrame, scene: str, plots_dir: Path) -> Optional[str]:
    if scene_df.empty:
        return None
    order = ["recommended", "structure_baseline", "random_baseline", "oracle_best"]
    ordered = scene_df.copy()
    ordered["strategy"] = pd.Categorical(ordered["strategy"], order, ordered=True)
    ordered = ordered.sort_values("strategy")

    plt.figure(figsize=(8, 5))
    plt.bar(ordered["strategy"], ordered["damage_score"], color=["#2ecc71", "#3498db", "#f39c12", "#111827"])
    plt.ylabel("Damage Score")
    plt.title(f"{scene} 推荐有效性对比")
    plt.grid(True, axis="y", alpha=0.25)
    plt.tight_layout()
    name = f"{slugify(scene)}_recommendation_effectiveness.png"
    out = plots_dir / name
    plt.savefig(out, dpi=180, bbox_inches="tight")
    plt.close()
    return name


def plot_overall_summary(summary_df: pd.DataFrame, plots_dir: Path) -> Optional[str]:
    if summary_df.empty:
        return None

    fig, axes = plt.subplots(2, 1, figsize=(12, 10), sharex=True)
    x = range(len(summary_df))
    labels = summary_df["scene_type"].tolist()

    axes[0].bar(x, summary_df["recommended_damage_score"], color="#2ecc71", label="Recommended")
    axes[0].bar(x, summary_df["structure_damage_score"], color="#3498db", alpha=0.65, label="Structure")
    axes[0].bar(x, summary_df["random_damage_score"], color="#f39c12", alpha=0.55, label="Random")
    axes[0].bar(x, summary_df["oracle_damage_score"], color="#111827", alpha=0.35, label="Oracle")
    axes[0].set_ylabel("Damage Score")
    axes[0].set_title("推荐目标与基线目标损伤对比")
    axes[0].grid(True, axis="y", alpha=0.25)
    axes[0].legend(loc="upper right")

    axes[1].bar(x, summary_df["oracle_ratio"], color="#16a085")
    axes[1].axhline(0.9, color="#111827", linestyle="--", linewidth=1.0)
    axes[1].axhline(0.75, color="#7f8c8d", linestyle=":", linewidth=1.0)
    axes[1].set_ylabel("Recommended / Oracle")
    axes[1].set_title("推荐目标接近 Oracle 的程度")
    axes[1].grid(True, axis="y", alpha=0.25)
    axes[1].set_xticks(list(x))
    axes[1].set_xticklabels(labels)

    plt.tight_layout()
    out = plots_dir / "recommendation_effectiveness_summary.png"
    plt.savefig(out, dpi=180, bbox_inches="tight")
    plt.close(fig)
    return out.name


def evaluate_scene(scene: str, args: argparse.Namespace, output_root: Path) -> Tuple[Dict, List[Dict]]:
    base_spec = create_base_spec(scene, args)
    scene_root = output_root / "runs" / slugify(scene)
    scene_root.mkdir(parents=True, exist_ok=True)

    recommendations = run_preflight_probe(base_spec, scene_root / "_preflight")
    attack_time = base_spec.attack_execute_time or 0.0
    candidate_pool = candidate_pool_from_recommendations(
        recommendations, attack_time, args.oracle_max_candidates
    )
    if candidate_pool.empty:
        raise RuntimeError(f"{scene} 预探测未生成有效候选目标")

    recommended_target = pick_recommended_strike_target(recommendations, attack_time)
    structure_target = select_structure_target(candidate_pool)
    random_target = select_random_target(candidate_pool, args.seed + hash(scene) % 10000)

    evaluated_by_target: Dict[int, Dict] = {}

    def ensure_target_run(target: Optional[int], label: str) -> Optional[Dict]:
        if target is None or target < 0:
            return None
        if target not in evaluated_by_target:
            spec = build_strategy_spec(base_spec, f"{scene}_{label}_{target}", target)
            result = run_attack_evaluation(spec, scene_root / slugify(f"{label}_{target}"))
            result["strategy"] = label
            result["target"] = target
            evaluated_by_target[target] = result
        return evaluated_by_target[target]

    recommended_result = ensure_target_run(recommended_target, "recommended")
    structure_result = ensure_target_run(structure_target, "structure_baseline")
    random_result = ensure_target_run(random_target, "random_baseline")

    oracle_candidates: List[Dict] = []
    for target in candidate_pool["recommendedObservedNodeId"].tolist():
        result = ensure_target_run(int(target), "oracle_candidate")
        if result:
            oracle_candidates.append(result)
    oracle_candidates = sorted(oracle_candidates, key=lambda item: item["damage_score"], reverse=True)
    oracle_best = oracle_candidates[0] if oracle_candidates else None

    scene_summary = {
        "scene_type": scene,
        "recommended_target": int(recommended_target) if recommended_target is not None else -1,
        "structure_target": int(structure_target) if structure_target is not None else -1,
        "random_target": int(random_target) if random_target is not None else -1,
        "oracle_target": int(oracle_best["target"]) if oracle_best else -1,
        "recommended_damage_score": recommended_result["damage_score"] if recommended_result else math.nan,
        "structure_damage_score": structure_result["damage_score"] if structure_result else math.nan,
        "random_damage_score": random_result["damage_score"] if random_result else math.nan,
        "oracle_damage_score": oracle_best["damage_score"] if oracle_best else math.nan,
    }
    if oracle_best and recommended_result:
        scene_summary["oracle_ratio"] = (
            recommended_result["damage_score"] / oracle_best["damage_score"]
            if oracle_best["damage_score"] > 0
            else math.nan
        )
    else:
        scene_summary["oracle_ratio"] = math.nan

    scene_summary["effectiveness_label"] = effectiveness_label(
        float(scene_summary["recommended_damage_score"]) if not math.isnan(scene_summary["recommended_damage_score"]) else 0.0,
        float(scene_summary["structure_damage_score"]) if not math.isnan(scene_summary["structure_damage_score"]) else 0.0,
        float(scene_summary["random_damage_score"]) if not math.isnan(scene_summary["random_damage_score"]) else 0.0,
        float(scene_summary["oracle_damage_score"]) if not math.isnan(scene_summary["oracle_damage_score"]) else 0.0,
    )

    write_scene_json(
        scene_root / "scene_recommendation_effectiveness.json",
        {
            "sceneSummary": scene_summary,
            "candidatePool": candidate_pool.to_dict("records"),
            "oracleCandidates": oracle_candidates,
        },
    )

    strategy_rows: List[Dict] = []
    if recommended_result:
        strategy_rows.append({**recommended_result, "scene_type": scene, "strategy": "recommended"})
    if structure_result:
        strategy_rows.append({**structure_result, "scene_type": scene, "strategy": "structure_baseline"})
    if random_result:
        strategy_rows.append({**random_result, "scene_type": scene, "strategy": "random_baseline"})
    if oracle_best:
        strategy_rows.append({**oracle_best, "scene_type": scene, "strategy": "oracle_best"})

    return scene_summary, strategy_rows


def write_markdown(output_root: Path, summary_df: pd.DataFrame) -> None:
    lines = [
        "# 推荐算法有效性评估",
        "",
        f"- 生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
        f"- 输出目录: `{output_root.relative_to(ROOT)}`",
        "",
        "## 场景摘要",
        "",
        summary_df.to_csv(index=False),
        "",
        "## 判断口径",
        "",
        "- `strong`: 推荐目标不弱于结构/随机基线，且接近 oracle（>= 0.90）",
        "- `acceptable`: 推荐目标接近结构/随机基线，且接近 oracle（>= 0.75）",
        "- `needs_strengthening`: 推荐目标明显落后于 oracle 或基线",
    ]
    (output_root / "README.md").write_text("\n".join(lines), encoding="utf-8")


def main() -> int:
    args = parse_args()
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_root = DEFAULT_OUTPUT_BASE / f"recommendation_effectiveness_{timestamp}"
    runs_root = output_root / "runs"
    plots_dir = output_root / "plots"
    runs_root.mkdir(parents=True, exist_ok=True)
    plots_dir.mkdir(parents=True, exist_ok=True)

    build_ns3(skip_build=args.skip_build, output_root=output_root)

    scene_summaries: List[Dict] = []
    strategy_rows: List[Dict] = []
    generated_plots: List[str] = []

    for scene in args.scenes:
        print(f"[scene] {scene}")
        scene_summary, scene_rows = evaluate_scene(scene, args, output_root)
        scene_summaries.append(scene_summary)
        strategy_rows.extend(scene_rows)
        scene_df = pd.DataFrame([row for row in scene_rows if row["scene_type"] == scene])
        plot_name = plot_scene_damage_bars(scene_df, scene, plots_dir)
        if plot_name:
            generated_plots.append(plot_name)

    summary_df = pd.DataFrame(scene_summaries)
    strategy_df = pd.DataFrame(strategy_rows)

    summary_df.to_csv(output_root / "scene_recommendation_effectiveness.csv", index=False)
    strategy_df.to_csv(output_root / "all_strategy_runs.csv", index=False)

    overall_plot = plot_overall_summary(summary_df, plots_dir)
    if overall_plot:
        generated_plots.append(overall_plot)

    write_markdown(output_root, summary_df)
    (output_root / "generated_plots.json").write_text(
        json.dumps(generated_plots, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )

    print("")
    print("========================================")
    print("推荐算法有效性评估完成")
    print("========================================")
    print(f"输出目录: {output_root}")
    print(f"场景数: {len(summary_df)}")
    print(f"摘要 CSV: {output_root / 'scene_recommendation_effectiveness.csv'}")
    print(f"策略 CSV: {output_root / 'all_strategy_runs.csv'}")
    print(f"图表目录: {plots_dir}")
    print("========================================")
    return 0


if __name__ == "__main__":
    sys.exit(main())
