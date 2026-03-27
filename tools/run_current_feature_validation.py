#!/usr/bin/env python3
"""
一键运行当前已完成的 UAV 仿真能力验证矩阵，并生成汇总报告与图表。

覆盖范围：
1. 合作模式：4 个场景 × 3 种通信模式
2. Leader 失效切换专项：centralized / hybrid
3. 非合作模式：4 个场景的观测 -> 推断 -> 关键节点识别链路

输出内容：
- 每个场景独立输出目录
- 汇总 CSV / JSON
- PNG 图表
- HTML 报告
"""

from __future__ import annotations

import argparse
import json
import math
import os
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

MPL_CONFIG_DIR = Path("/tmp/matplotlib-codex-cache")
MPL_CONFIG_DIR.mkdir(parents=True, exist_ok=True)
os.environ.setdefault("MPLCONFIGDIR", str(MPL_CONFIG_DIR))

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_OUTPUT_BASE = ROOT / "output"
NS3_BINARY = ROOT / "ns3"
PROGRAM_NAME = "uav_resource_allocation"

SCENES = ["urban", "forest", "lake", "open-field"]
COOP_MODES = ["centralized", "distributed", "hybrid"]
SCENE_MAP_FILES = {
    "urban": "data_map/test_scene_urban.geojson",
    "forest": "data_map/test_scene_forest.geojson",
    "lake": "data_map/test_scene_lake.geojson",
    "open-field": "data_map/test_scene_open_field.geojson",
}

FONT_CANDIDATES = [
    "Noto Sans CJK JP",
    "SimHei",
    "Microsoft YaHei",
    "WenQuanYi Micro Hei",
    "DejaVu Sans",
]
plt.rcParams["font.sans-serif"] = FONT_CANDIDATES
plt.rcParams["axes.unicode_minus"] = False


@dataclass
class RunSpec:
    name: str
    operation_mode: str
    scene_type: str
    difficulty: str
    formation: str
    duration: float
    num_uavs: int
    num_channels: int
    strategy: str
    map_file: Optional[str] = None
    communication_mode: Optional[str] = None
    leader_node_id: int = 0
    backup_leader_list: str = "1,2,3"
    distributed_hop_limit: int = 1
    cooperative_failure_type: str = "node_failure"
    failure_target_id: int = -1
    failure_start_time: Optional[float] = None
    failure_duration: Optional[float] = None
    recovery_policy: str = "global_recovery"
    recovery_objective: str = "connectivity"
    recovery_cooldown: float = 1.0
    allow_channel_reallocation: bool = True
    allow_power_adjustment: bool = True
    allow_rate_adjustment: bool = True
    allow_relay_reselection: bool = True
    allow_slot_reallocation: bool = True
    allow_route_rebuild: bool = True
    expect_leader_failover: bool = False
    enable_non_cooperative_attack: bool = False
    attack_type: str = "node_strike"
    manual_strike_target: int = -1
    attack_execute_time: Optional[float] = None
    attack_evaluation_duration: float = 12.0
    attack_neighborhood_hop: int = 1
    urban_altitude_penalty_db_low: Optional[float] = None
    urban_altitude_gain_db_high: Optional[float] = None
    urban_street_canyon_factor: Optional[float] = None
    lake_volatility_jitter_db: Optional[float] = None
    lake_deep_fade_probability: Optional[float] = None
    lake_deep_fade_max_db: Optional[float] = None
    lake_reflection_delay_jitter_ms: Optional[float] = None
    carrier_frequency_ghz: Optional[float] = None
    channel_bandwidth_mhz: Optional[float] = None
    polarization_mode: Optional[str] = None
    reroute_pressure_factor: Optional[float] = None
    control_message_urgency_factor: Optional[float] = None
    relay_instability_factor: Optional[float] = None
    formation_reconfig_penalty: Optional[float] = None


def bool_arg(value: bool) -> str:
    return "true" if value else "false"


def slugify(value: str) -> str:
    return (
        value.replace(" ", "_")
        .replace("/", "_")
        .replace("(", "")
        .replace(")", "")
        .replace(",", "_")
    )


def ensure_json(path: Path) -> Dict:
    if not path.exists():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def ensure_csv(path: Path) -> pd.DataFrame:
    if not path.exists():
        return pd.DataFrame()
    try:
        return pd.read_csv(path)
    except pd.errors.EmptyDataError:
        return pd.DataFrame()


def coerce_numeric(df: pd.DataFrame, columns: Sequence[str]) -> pd.DataFrame:
    if df.empty:
        return df
    out = df.copy()
    for column in columns:
        if column in out.columns:
            out[column] = pd.to_numeric(out[column], errors="coerce")
    return out


def summarize_metric_window(
    df: pd.DataFrame,
    time_col: str,
    value_col: str,
    start: float,
    end: float,
) -> float:
    if df.empty or time_col not in df.columns or value_col not in df.columns:
        return math.nan
    series = df.loc[(df[time_col] >= start) & (df[time_col] < end), value_col].dropna()
    if series.empty:
        return math.nan
    return float(series.mean())


def max_metric_window(
    df: pd.DataFrame,
    time_col: str,
    value_col: str,
    start: float,
    end: float,
) -> float:
    if df.empty or time_col not in df.columns or value_col not in df.columns:
        return math.nan
    series = df.loc[(df[time_col] >= start) & (df[time_col] < end), value_col].dropna()
    if series.empty:
        return math.nan
    return float(series.max())


def phase_segments(df: pd.DataFrame) -> List[Tuple[float, float, str]]:
    if df.empty or "time" not in df.columns or "phase" not in df.columns:
        return []

    segments: List[Tuple[float, float, str]] = []
    start = float(df.iloc[0]["time"])
    current_phase = str(df.iloc[0]["phase"])

    for idx in range(1, len(df)):
        row = df.iloc[idx]
        phase = str(row["phase"])
        time = float(row["time"])
        if phase != current_phase:
            segments.append((start, time, current_phase))
            start = time
            current_phase = phase

    end_time = float(df.iloc[-1]["time"])
    if len(df) >= 2:
        step = float(df.iloc[-1]["time"]) - float(df.iloc[-2]["time"])
        end_time += max(0.1, step)
    segments.append((start, end_time, current_phase))
    return segments


def annotate_cooperative_events(
    axes: Sequence[plt.Axes],
    failure_start: Optional[float],
    failure_end: Optional[float],
    failure_events: List[Dict],
    recovery_actions: List[Dict],
    metric_df: pd.DataFrame,
) -> None:
    phase_colors = {
        "normal": "#dfe6e9",
        "failure": "#fdecea",
        "transition": "#fff4d6",
        "recovery": "#e8f7ef",
        "stabilization": "#e8f1fd",
        "stable": "#e8f1fd",
    }

    segments = phase_segments(metric_df)
    for ax in axes:
        for start, end, phase in segments:
            color = phase_colors.get(phase, "#f3f4f6")
            ax.axvspan(start, end, color=color, alpha=0.10, linewidth=0)

        if failure_start is not None and failure_end is not None and failure_end > failure_start:
            ax.axvspan(failure_start, failure_end, color="#ff6b6b", alpha=0.08, linewidth=0)

        for event in failure_events:
            event_time = event.get("time")
            if event_time is None:
                continue
            ax.axvline(event_time, color="#c0392b", linestyle="--", alpha=0.70, linewidth=1.3)

        for action in recovery_actions:
            action_time = action.get("time")
            if action_time is None:
                continue
            ax.axvline(action_time, color="#16a085", linestyle=":", alpha=0.28, linewidth=1.1)


def plot_state_series(
    ax: plt.Axes,
    time_values: pd.Series,
    state_values: pd.Series,
    *,
    color: str,
    label: str,
    linestyle: str = "-",
    linewidth: float = 1.5,
    marker_size: float = 18.0,
) -> None:
    cleaned = pd.DataFrame({"time": time_values, "value": state_values}).dropna()
    if cleaned.empty:
        return

    ax.step(
        cleaned["time"],
        cleaned["value"],
        where="post",
        color=color,
        linestyle=linestyle,
        linewidth=linewidth,
        label=label,
    )

    changed_mask = cleaned["value"].ne(cleaned["value"].shift(1))
    changed_points = cleaned.loc[changed_mask]
    ax.scatter(
        changed_points["time"],
        changed_points["value"],
        color=color,
        s=marker_size,
        zorder=4,
        alpha=0.9,
    )


def annotate_attack_phases(ax: plt.Axes, global_df: pd.DataFrame) -> None:
    if global_df.empty or "phase" not in global_df.columns or "time" not in global_df.columns:
        return

    phase_colors = {
        "pre_attack": "#dfe6e9",
        "immediate_post_attack": "#fdecea",
        "recovery": "#e8f7ef",
        "final": "#e8f1fd",
    }

    for start, end, phase in phase_segments(global_df):
        ax.axvspan(start, end, color=phase_colors.get(phase, "#f3f4f6"), alpha=0.10, linewidth=0)


def build_run_specs(
    duration: float,
    difficulty: str,
    formation: str,
    num_uavs: int,
    num_channels: int,
    strategy: str,
    preset: str,
) -> List[RunSpec]:
    failure_start = round(duration * 0.4, 2)
    failure_duration = round(duration * 0.2, 2)
    runs: List[RunSpec] = []

    scenes = ["urban", "open-field"] if preset == "quick" else SCENES
    coop_modes = ["centralized", "distributed"] if preset == "quick" else COOP_MODES

    for scene in scenes:
        for mode in coop_modes:
            runs.append(
                RunSpec(
                    name=f"coop_{mode}_{scene}",
                    operation_mode="cooperative",
                    communication_mode=mode,
                    scene_type=scene,
                    difficulty=difficulty,
                    formation=formation,
                    duration=duration,
                    num_uavs=num_uavs,
                    num_channels=num_channels,
                    strategy=strategy,
                    map_file=SCENE_MAP_FILES.get(scene),
                    failure_start_time=failure_start,
                    failure_duration=failure_duration,
                )
            )

    leader_failover_modes = ["centralized", "hybrid"]
    for mode in leader_failover_modes:
        runs.append(
            RunSpec(
                name=f"coop_{mode}_leader_failover_urban",
                operation_mode="cooperative",
                communication_mode=mode,
                scene_type="urban",
                difficulty=difficulty,
                formation=formation,
                duration=duration,
                num_uavs=num_uavs,
                num_channels=num_channels,
                strategy=strategy,
                map_file=SCENE_MAP_FILES.get("urban"),
                failure_target_id=0,
                failure_start_time=failure_start,
                failure_duration=failure_duration,
                expect_leader_failover=True,
            )
        )

    for scene in scenes:
        runs.append(
            RunSpec(
                name=f"noncooperative_{scene}",
                operation_mode="non_cooperative",
                scene_type=scene,
                difficulty=difficulty,
                formation=formation,
                duration=duration,
                num_uavs=num_uavs,
                num_channels=num_channels,
                strategy=strategy,
                map_file=SCENE_MAP_FILES.get(scene),
            )
        )

    attack_eval_duration = max(4.0, round(min(8.0, duration * 0.20), 2))
    attack_execute_time = round(max(duration * 0.55, duration - attack_eval_duration - 1.0), 2)
    attack_scenes = ["urban", "open-field"] if preset == "quick" else scenes
    for scene in attack_scenes:
        runs.append(
            RunSpec(
                name=f"noncooperative_attack_{scene}",
                operation_mode="non_cooperative",
                scene_type=scene,
                difficulty=difficulty,
                formation=formation,
                duration=duration,
                num_uavs=num_uavs,
                num_channels=num_channels,
                strategy=strategy,
                map_file=SCENE_MAP_FILES.get(scene),
                enable_non_cooperative_attack=True,
                manual_strike_target=-1,
                attack_execute_time=attack_execute_time,
                attack_evaluation_duration=attack_eval_duration,
                attack_neighborhood_hop=1,
            )
        )

    if preset != "quick":
        runs.extend(
            [
                RunSpec(
                    name="realism_urban_altitude_profile",
                    operation_mode="cooperative",
                    communication_mode="centralized",
                    scene_type="urban",
                    difficulty=difficulty,
                    formation=formation,
                    duration=duration,
                    num_uavs=num_uavs,
                    num_channels=num_channels,
                    strategy=strategy,
                    map_file=SCENE_MAP_FILES.get("urban"),
                    failure_start_time=failure_start,
                    failure_duration=failure_duration,
                    urban_altitude_penalty_db_low=8.0,
                    urban_altitude_gain_db_high=7.0,
                    urban_street_canyon_factor=1.1,
                ),
                RunSpec(
                    name="realism_forest_radio_profile",
                    operation_mode="cooperative",
                    communication_mode="centralized",
                    scene_type="forest",
                    difficulty=difficulty,
                    formation=formation,
                    duration=duration,
                    num_uavs=num_uavs,
                    num_channels=num_channels,
                    strategy=strategy,
                    map_file=SCENE_MAP_FILES.get("forest"),
                    failure_start_time=failure_start,
                    failure_duration=failure_duration,
                    carrier_frequency_ghz=2.4,
                    channel_bandwidth_mhz=10.0,
                    polarization_mode="horizontal",
                ),
                RunSpec(
                    name="realism_lake_volatility_profile",
                    operation_mode="cooperative",
                    communication_mode="centralized",
                    scene_type="lake",
                    difficulty=difficulty,
                    formation=formation,
                    duration=duration,
                    num_uavs=num_uavs,
                    num_channels=num_channels,
                    strategy=strategy,
                    map_file=SCENE_MAP_FILES.get("lake"),
                    failure_start_time=failure_start,
                    failure_duration=failure_duration,
                    lake_volatility_jitter_db=5.0,
                    lake_deep_fade_probability=0.2,
                    lake_deep_fade_max_db=9.0,
                    lake_reflection_delay_jitter_ms=20.0,
                ),
                RunSpec(
                    name="realism_open_field_pressure_profile",
                    operation_mode="cooperative",
                    communication_mode="centralized",
                    scene_type="open-field",
                    difficulty=difficulty,
                    formation=formation,
                    duration=duration,
                    num_uavs=num_uavs,
                    num_channels=num_channels,
                    strategy=strategy,
                    map_file=SCENE_MAP_FILES.get("open-field"),
                    failure_start_time=failure_start,
                    failure_duration=failure_duration,
                    reroute_pressure_factor=1.8,
                    control_message_urgency_factor=1.4,
                    relay_instability_factor=1.3,
                    formation_reconfig_penalty=1.2,
                ),
            ]
        )

    return runs


def build_ns3_command(spec: RunSpec, output_dir: Path) -> str:
    output_arg = (
        str(output_dir.relative_to(ROOT))
        if output_dir.is_relative_to(ROOT)
        else str(output_dir)
    )
    args = [
        PROGRAM_NAME,
        f"--duration={spec.duration}",
        f"--numUAVs={spec.num_uavs}",
        f"--numChannels={spec.num_channels}",
        f"--strategy={spec.strategy}",
        f"--difficulty={spec.difficulty}",
        f"--formation={spec.formation}",
        f"--outputDir={output_arg}",
        f"--operationMode={spec.operation_mode}",
        f"--sceneType={spec.scene_type}",
    ]
    if spec.map_file:
        args.append(f"--mapFile={spec.map_file}")
    if spec.urban_altitude_penalty_db_low is not None:
        args.append(f"--urbanAltitudePenaltyDbLow={spec.urban_altitude_penalty_db_low}")
    if spec.urban_altitude_gain_db_high is not None:
        args.append(f"--urbanAltitudeGainDbHigh={spec.urban_altitude_gain_db_high}")
    if spec.urban_street_canyon_factor is not None:
        args.append(f"--urbanStreetCanyonFactor={spec.urban_street_canyon_factor}")
    if spec.lake_volatility_jitter_db is not None:
        args.append(f"--lakeVolatilityJitterDb={spec.lake_volatility_jitter_db}")
    if spec.lake_deep_fade_probability is not None:
        args.append(f"--lakeDeepFadeProbability={spec.lake_deep_fade_probability}")
    if spec.lake_deep_fade_max_db is not None:
        args.append(f"--lakeDeepFadeMaxDb={spec.lake_deep_fade_max_db}")
    if spec.lake_reflection_delay_jitter_ms is not None:
        args.append(f"--lakeReflectionDelayJitterMs={spec.lake_reflection_delay_jitter_ms}")
    if spec.carrier_frequency_ghz is not None:
        args.append(f"--carrierFrequencyGHz={spec.carrier_frequency_ghz}")
    if spec.channel_bandwidth_mhz is not None:
        args.append(f"--channelBandwidthMHz={spec.channel_bandwidth_mhz}")
    if spec.polarization_mode:
        args.append(f"--polarizationMode={spec.polarization_mode}")
    if spec.reroute_pressure_factor is not None:
        args.append(f"--reroutePressureFactor={spec.reroute_pressure_factor}")
    if spec.control_message_urgency_factor is not None:
        args.append(
            f"--controlMessageUrgencyFactor={spec.control_message_urgency_factor}"
        )
    if spec.relay_instability_factor is not None:
        args.append(f"--relayInstabilityFactor={spec.relay_instability_factor}")
    if spec.formation_reconfig_penalty is not None:
        args.append(f"--formationReconfigPenalty={spec.formation_reconfig_penalty}")

    if spec.operation_mode == "cooperative":
        args.extend(
            [
                f"--communicationMode={spec.communication_mode}",
                f"--leaderNodeId={spec.leader_node_id}",
                f"--backupLeaderList={spec.backup_leader_list}",
                f"--distributedHopLimit={spec.distributed_hop_limit}",
                f"--cooperativeFailureType={spec.cooperative_failure_type}",
                f"--failureTargetId={spec.failure_target_id}",
                f"--failureStartTime={spec.failure_start_time}",
                f"--failureDuration={spec.failure_duration}",
                f"--recoveryPolicy={spec.recovery_policy}",
                f"--recoveryObjective={spec.recovery_objective}",
                f"--recoveryCooldown={spec.recovery_cooldown}",
                f"--allowChannelReallocation={bool_arg(spec.allow_channel_reallocation)}",
                f"--allowPowerAdjustment={bool_arg(spec.allow_power_adjustment)}",
                f"--allowRateAdjustment={bool_arg(spec.allow_rate_adjustment)}",
                f"--allowRelayReselection={bool_arg(spec.allow_relay_reselection)}",
                f"--allowSlotReallocation={bool_arg(spec.allow_slot_reallocation)}",
                f"--allowRouteRebuild={bool_arg(spec.allow_route_rebuild)}",
            ]
        )
    elif spec.enable_non_cooperative_attack:
        args.extend(
            [
                "--enableNonCooperativeAttack=true",
                f"--attackType={spec.attack_type}",
                f"--manualStrikeTarget={spec.manual_strike_target}",
                f"--attackExecuteTime={spec.attack_execute_time}",
                f"--attackEvaluationDuration={spec.attack_evaluation_duration}",
                f"--attackNeighborhoodHop={spec.attack_neighborhood_hop}",
            ]
        )

    return " ".join(args)


def run_checked(cmd: Sequence[str], cwd: Path, log_path: Path) -> None:
    with log_path.open("w", encoding="utf-8") as log_file:
        process = subprocess.run(
            list(cmd),
            cwd=str(cwd),
            stdout=log_file,
            stderr=subprocess.STDOUT,
            text=True,
        )
    if process.returncode != 0:
        raise RuntimeError(f"命令执行失败: {' '.join(cmd)}，详见 {log_path}")


def pick_recommended_strike_target(
    recommendations: pd.DataFrame, attack_execute_time: Optional[float]
) -> Optional[int]:
    if recommendations.empty or "recommendedObservedNodeId" not in recommendations.columns:
        return None

    ranked = recommendations.copy()
    ranked = coerce_numeric(
        ranked,
        ["windowStart", "windowEnd", "recommendedObservedNodeId", "recommendedScore", "recommendationRank"],
    )
    if "recommendationRank" in ranked.columns:
        top_rank = ranked.loc[ranked["recommendationRank"] == 1].copy()
        if not top_rank.empty:
            ranked = top_rank

    ranked = ranked.dropna(subset=["recommendedObservedNodeId"])
    if ranked.empty:
        return None

    time_col = "windowEnd" if "windowEnd" in ranked.columns else "windowStart"
    if attack_execute_time is not None and time_col in ranked.columns:
        prior = ranked.loc[ranked[time_col] <= attack_execute_time + 1e-6].copy()
        if prior.empty:
            return None
        ranked = prior

    sort_cols = [col for col in [time_col, "recommendedScore"] if col in ranked.columns]
    if sort_cols:
        ranked = ranked.sort_values(sort_cols)

    try:
        return int(ranked.iloc[-1]["recommendedObservedNodeId"])
    except (TypeError, ValueError):
        return None


def resolve_noncooperative_attack_target(spec: RunSpec, probe_dir: Path) -> int:
    probe_spec = RunSpec(**asdict(spec))
    probe_spec.manual_strike_target = -1

    if probe_dir.exists():
        shutil.rmtree(probe_dir)
    probe_dir.mkdir(parents=True, exist_ok=True)

    (probe_dir / "run_spec.json").write_text(
        json.dumps(asdict(probe_spec), ensure_ascii=False, indent=2), encoding="utf-8"
    )

    probe_command = build_ns3_command(probe_spec, probe_dir)
    probe_log = probe_dir / "simulation.log"
    run_checked([str(NS3_BINARY), "run", probe_command, "--no-build"], ROOT, probe_log)

    recommendations = ensure_csv(probe_dir / "noncooperative_attack_recommendations.csv")
    target = pick_recommended_strike_target(recommendations, probe_spec.attack_execute_time)
    if target is None or target < 0:
        raise RuntimeError(
            "预探测未能解析出有效的非合作打击目标，详见 "
            f"{probe_dir / 'noncooperative_attack_recommendations.csv'}"
        )
    return target


def build_ns3(skip_build: bool, output_root: Path) -> None:
    if skip_build:
        return
    build_log = output_root / "build.log"
    print(f"[build] 编译 {PROGRAM_NAME}")
    run_checked(
        [str(NS3_BINARY), "build", PROGRAM_NAME],
        ROOT,
        build_log,
    )


def run_one_spec(spec: RunSpec, runs_root: Path) -> Tuple[Path, Optional[str]]:
    run_dir = runs_root / slugify(spec.name)
    if run_dir.exists():
        shutil.rmtree(run_dir)
    run_dir.mkdir(parents=True, exist_ok=True)

    effective_spec = RunSpec(**asdict(spec))
    if (
        effective_spec.enable_non_cooperative_attack
        and effective_spec.attack_execute_time is not None
        and effective_spec.manual_strike_target < 0
    ):
        try:
            resolved_target = resolve_noncooperative_attack_target(
                effective_spec, run_dir / "_preflight"
            )
            effective_spec.manual_strike_target = resolved_target
            (run_dir / "resolved_attack_target.json").write_text(
                json.dumps(
                    {
                        "attackExecuteTime": effective_spec.attack_execute_time,
                        "resolvedObservedNodeId": resolved_target,
                        "resolutionMode": "preflight_latest_recommendation",
                    },
                    ensure_ascii=False,
                    indent=2,
                ),
                encoding="utf-8",
            )
        except Exception as exc:
            return run_dir, str(exc)

    config_path = run_dir / "run_spec.json"
    config_path.write_text(
        json.dumps(asdict(effective_spec), ensure_ascii=False, indent=2), encoding="utf-8"
    )

    command = build_ns3_command(effective_spec, run_dir)
    log_path = run_dir / "simulation.log"

    try:
        print(f"[run] {spec.name}")
        run_checked(
            [str(NS3_BINARY), "run", command, "--no-build"],
            ROOT,
            log_path,
        )
        return run_dir, None
    except Exception as exc:
        return run_dir, str(exc)


def phase_sequence(metrics_samples: List[Dict]) -> str:
    phases: List[str] = []
    for sample in metrics_samples:
        phase = str(sample.get("phase", "")).strip()
        if not phase:
            continue
        if not phases or phases[-1] != phase:
            phases.append(phase)
    return " -> ".join(phases)


def summarize_cooperative_run(spec: RunSpec, run_dir: Path, error: Optional[str]) -> Dict:
    env = ensure_json(run_dir / "environment_summary.json")
    mode_summary = ensure_json(run_dir / "cooperative_mode_summary.json")
    dashboard = ensure_json(run_dir / "cooperative_dashboard_snapshot.json")
    failure_timeline = ensure_json(run_dir / "cooperative_failure_timeline.json")
    recovery_timeline = ensure_json(run_dir / "cooperative_recovery_timeline.json")
    metrics_json = ensure_json(run_dir / "cooperative_metrics_timeseries.json")
    recovery_metrics = coerce_numeric(
        ensure_csv(run_dir / "cooperative_recovery_metrics.csv"),
        [
            "time",
            "connectivity",
            "avg_degree",
            "pdr",
            "throughput_mbps",
            "delay_ms",
            "p99_delay_ms",
            "response_time_sec",
            "recovery_time_sec",
            "stabilization_time_sec",
            "failure_neighborhood_pdr",
            "failure_neighborhood_throughput_mbps",
            "failure_neighborhood_delay_ms",
            "failure_neighborhood_node_count",
            "failure_target_id",
            "failure_target_pdr",
            "failure_target_throughput_mbps",
            "failure_target_delay_ms",
            "leader_node_id",
        ],
    )

    events = failure_timeline.get("events", [])
    actions = recovery_timeline.get("actions", [])
    samples = metrics_json.get("samples", [])

    failure_start = mode_summary.get("failureStartTime", spec.failure_start_time)
    failure_duration = mode_summary.get("failureDuration", spec.failure_duration)
    failure_end = (
        float(failure_start) + float(failure_duration)
        if failure_start is not None and failure_duration is not None
        else math.nan
    )

    pre_start = max(0.0, float(failure_start or 0.0) - 4.0)
    pre_end = float(failure_start or 0.0)
    during_start = float(failure_start or 0.0)
    during_end = float(failure_end) if not math.isnan(failure_end) else float(pre_end)
    post_start = during_end
    post_end = during_end + 4.0 if not math.isnan(during_end) else during_end

    pre_connectivity = summarize_metric_window(
        recovery_metrics, "time", "connectivity", pre_start, pre_end
    )
    during_connectivity = summarize_metric_window(
        recovery_metrics, "time", "connectivity", during_start, during_end
    )
    pre_pdr = summarize_metric_window(recovery_metrics, "time", "pdr", pre_start, pre_end)
    during_pdr = summarize_metric_window(recovery_metrics, "time", "pdr", during_start, during_end)
    pre_throughput = summarize_metric_window(
        recovery_metrics, "time", "throughput_mbps", pre_start, pre_end
    )
    during_throughput = summarize_metric_window(
        recovery_metrics, "time", "throughput_mbps", during_start, during_end
    )
    pre_delay = summarize_metric_window(recovery_metrics, "time", "delay_ms", pre_start, pre_end)
    during_delay = summarize_metric_window(
        recovery_metrics, "time", "delay_ms", during_start, during_end
    )
    pre_p99_delay = summarize_metric_window(
        recovery_metrics, "time", "p99_delay_ms", pre_start, pre_end
    )
    during_p99_delay = summarize_metric_window(
        recovery_metrics, "time", "p99_delay_ms", during_start, during_end
    )
    pre_local_pdr = summarize_metric_window(
        recovery_metrics, "time", "failure_neighborhood_pdr", pre_start, pre_end
    )
    during_local_pdr = summarize_metric_window(
        recovery_metrics, "time", "failure_neighborhood_pdr", during_start, during_end
    )
    pre_local_delay = summarize_metric_window(
        recovery_metrics, "time", "failure_neighborhood_delay_ms", pre_start, pre_end
    )
    during_local_delay = summarize_metric_window(
        recovery_metrics, "time", "failure_neighborhood_delay_ms", during_start, during_end
    )
    pre_target_pdr = summarize_metric_window(
        recovery_metrics, "time", "failure_target_pdr", pre_start, pre_end
    )
    during_target_pdr = summarize_metric_window(
        recovery_metrics, "time", "failure_target_pdr", during_start, during_end
    )
    pre_target_delay = summarize_metric_window(
        recovery_metrics, "time", "failure_target_delay_ms", pre_start, pre_end
    )
    during_target_delay = summarize_metric_window(
        recovery_metrics, "time", "failure_target_delay_ms", during_start, during_end
    )
    post_connectivity = summarize_metric_window(
        recovery_metrics, "time", "connectivity", post_start, post_end
    )
    max_delay_during = max_metric_window(
        recovery_metrics, "time", "delay_ms", during_start, during_end
    )
    max_p99_during = max_metric_window(
        recovery_metrics, "time", "p99_delay_ms", during_start, during_end
    )

    original_leader = mode_summary.get("leaderNodeId", spec.leader_node_id)
    final_leader = dashboard.get("leaderNodeId")
    leader_switched = (
        spec.expect_leader_failover and final_leader is not None and final_leader != original_leader
    )

    reached_stable = bool(
        dashboard.get("recoveryStatus") == "stable"
        or any(sample.get("phase") == "stable" for sample in samples)
    )

    files_ok = all(
        (run_dir / name).exists()
        for name in [
            "environment_summary.json",
            "cooperative_mode_summary.json",
            "cooperative_failure_timeline.json",
            "cooperative_recovery_timeline.json",
            "cooperative_metrics_timeseries.json",
            "cooperative_dashboard_snapshot.json",
            "cooperative_failure_events.csv",
            "cooperative_recovery_actions.csv",
            "cooperative_recovery_metrics.csv",
            "cooperative_decision_trace.csv",
        ]
    )

    validation_notes: List[str] = []
    validation_state = "PASS"

    if error:
        validation_state = "FAIL"
        validation_notes.append(error)
    if not files_ok:
        validation_state = "FAIL"
        validation_notes.append("合作模式核心输出文件缺失")
    if len(events) == 0:
        validation_state = "FAIL"
        validation_notes.append("没有记录到故障事件")
    if len(actions) == 0:
        validation_state = "FAIL"
        validation_notes.append("没有记录到恢复动作")
    if len(samples) == 0 and recovery_metrics.empty:
        validation_state = "FAIL"
        validation_notes.append("没有记录到恢复指标时间序列")
    if spec.expect_leader_failover and not leader_switched:
        validation_state = "FAIL"
        validation_notes.append("Leader 失效切换未发生")
    if validation_state == "PASS" and not reached_stable:
        validation_state = "WARN"
        validation_notes.append("已触发恢复，但未达到 stable 阶段")

    return {
        "run_name": spec.name,
        "is_realism_profile": spec.name.startswith("realism_"),
        "category": "cooperative",
        "operation_mode": spec.operation_mode,
        "communication_mode": spec.communication_mode,
        "scene_type": spec.scene_type,
        "difficulty": spec.difficulty,
        "formation": spec.formation,
        "duration": spec.duration,
        "map_file": spec.map_file or "",
        "failure_target_id": mode_summary.get("failureTargetId", spec.failure_target_id),
        "failure_event_count": len(events),
        "recovery_action_count": len(actions),
        "metrics_sample_count": len(samples),
        "validation_state": validation_state,
        "validation_notes": " | ".join(validation_notes),
        "files_ok": files_ok,
        "leader_failover_expected": spec.expect_leader_failover,
        "leader_original": original_leader,
        "leader_final": final_leader,
        "leader_switched": leader_switched,
        "phase_sequence": phase_sequence(samples),
        "final_phase": dashboard.get("phase"),
        "recovery_status": dashboard.get("recoveryStatus"),
        "connectivity_end": dashboard.get("connectivity"),
        "avg_degree_end": dashboard.get("avgDegree"),
        "pdr_end": dashboard.get("pdr"),
        "throughput_end_mbps": dashboard.get("throughputMbps"),
        "delay_end_ms": dashboard.get("delayMs"),
        "p99_delay_end_ms": dashboard.get("p99DelayMs"),
        "response_time_sec": dashboard.get("responseTimeSec"),
        "recovery_time_sec": dashboard.get("recoveryTimeSec"),
        "stabilization_time_sec": dashboard.get("stabilizationTimeSec"),
        "failure_window_start": failure_start,
        "failure_window_end": failure_end,
        "pre_failure_connectivity": pre_connectivity,
        "during_failure_connectivity": during_connectivity,
        "post_failure_connectivity": post_connectivity,
        "connectivity_drop_during": during_connectivity - pre_connectivity,
        "pre_failure_pdr": pre_pdr,
        "during_failure_pdr": during_pdr,
        "pdr_delta_during": during_pdr - pre_pdr,
        "pre_failure_throughput_mbps": pre_throughput,
        "during_failure_throughput_mbps": during_throughput,
        "throughput_delta_during_mbps": during_throughput - pre_throughput,
        "pre_failure_delay_ms": pre_delay,
        "during_failure_delay_ms": during_delay,
        "delay_delta_during_ms": during_delay - pre_delay,
        "pre_failure_p99_delay_ms": pre_p99_delay,
        "during_failure_p99_delay_ms": during_p99_delay,
        "p99_delay_delta_during_ms": during_p99_delay - pre_p99_delay,
        "pre_failure_local_pdr": pre_local_pdr,
        "during_failure_local_pdr": during_local_pdr,
        "local_pdr_delta_during": during_local_pdr - pre_local_pdr,
        "pre_failure_local_delay_ms": pre_local_delay,
        "during_failure_local_delay_ms": during_local_delay,
        "local_delay_delta_during_ms": during_local_delay - pre_local_delay,
        "pre_failure_target_pdr": pre_target_pdr,
        "during_failure_target_pdr": during_target_pdr,
        "target_pdr_delta_during": during_target_pdr - pre_target_pdr,
        "pre_failure_target_delay_ms": pre_target_delay,
        "during_failure_target_delay_ms": during_target_delay,
        "target_delay_delta_during_ms": during_target_delay - pre_target_delay,
        "max_delay_during_ms": max_delay_during,
        "max_p99_delay_during_ms": max_p99_during,
        "environment_summary_path": str((run_dir / "environment_summary.json").relative_to(ROOT)),
        "output_dir": str(run_dir.relative_to(ROOT)),
        "error": error or "",
        "environment_source": env.get("environmentSource"),
        "effective_model_summary": env.get("effectiveModelSummary"),
        "carrier_frequency_ghz": env.get("carrierFrequencyGHz"),
        "channel_bandwidth_mhz": env.get("channelBandwidthMHz"),
        "polarization_mode": env.get("polarizationMode"),
        "urban_altitude_penalty_db_low": env.get("urbanAltitudePenaltyDbLow"),
        "urban_altitude_gain_db_high": env.get("urbanAltitudeGainDbHigh"),
        "urban_street_canyon_factor": env.get("urbanStreetCanyonFactor"),
        "lake_volatility_jitter_db": env.get("lakeVolatilityJitterDb"),
        "lake_deep_fade_probability": env.get("lakeDeepFadeProbability"),
        "lake_deep_fade_max_db": env.get("lakeDeepFadeMaxDb"),
        "lake_reflection_delay_jitter_ms": env.get("lakeReflectionDelayJitterMs"),
        "reroute_pressure_factor": env.get("reroutePressureFactor"),
        "control_message_urgency_factor": env.get("controlMessageUrgencyFactor"),
        "relay_instability_factor": env.get("relayInstabilityFactor"),
        "formation_reconfig_penalty": env.get("formationReconfigPenalty"),
    }


def summarize_noncooperative_run(spec: RunSpec, run_dir: Path, error: Optional[str]) -> Dict:
    env = ensure_json(run_dir / "environment_summary.json")
    observed_signal = coerce_numeric(
        ensure_csv(run_dir / "observed_signal_events.csv"),
        ["eventTime", "overallConfidence", "noiseLevel"],
    )
    observed_windows = coerce_numeric(
        ensure_csv(run_dir / "observed_comm_windows.csv"),
        ["windowStart", "windowEnd", "activeRatio", "overallConfidence", "noiseLevel"],
    )
    link_evidence = coerce_numeric(
        ensure_csv(run_dir / "observed_link_evidence.csv"),
        ["windowStart", "windowEnd", "evidenceStrength", "observerAgreementScore", "edgeObservationConfidence"],
    )
    inferred_edges = coerce_numeric(
        ensure_csv(run_dir / "inferred_topology_edges.csv"),
        ["windowStart", "windowEnd", "edgeProbability", "edgeConfidence"],
    )
    inferred_nodes = coerce_numeric(ensure_csv(run_dir / "inferred_graph_nodes.csv"), ["weightedDegreeScore"])
    key_nodes = coerce_numeric(
        ensure_csv(run_dir / "key_node_candidates.csv"),
        ["windowStart", "windowEnd", "rank", "keyNodeScore"],
    )

    files_ok = all(
        (run_dir / name).exists()
        for name in [
            "environment_summary.json",
            "observed_signal_events.csv",
            "observed_comm_windows.csv",
            "observed_link_evidence.csv",
            "inferred_topology_edges.csv",
            "inferred_graph_nodes.csv",
            "key_node_candidates.csv",
        ]
    )

    avg_edge_probability = (
        float(inferred_edges["edgeProbability"].mean())
        if not inferred_edges.empty and "edgeProbability" in inferred_edges.columns
        else math.nan
    )
    top_key_node_score = (
        float(key_nodes["keyNodeScore"].max())
        if not key_nodes.empty and "keyNodeScore" in key_nodes.columns
        else math.nan
    )
    observed_missing_ratio = (
        float((pd.to_numeric(observed_windows["isMissing"], errors="coerce") == 1).mean())
        if not observed_windows.empty and "isMissing" in observed_windows.columns
        else math.nan
    )
    avg_window_confidence = (
        float(observed_windows["overallConfidence"].mean())
        if not observed_windows.empty and "overallConfidence" in observed_windows.columns
        else math.nan
    )
    avg_evidence_strength = (
        float(link_evidence["evidenceStrength"].mean())
        if not link_evidence.empty and "evidenceStrength" in link_evidence.columns
        else math.nan
    )

    validation_notes: List[str] = []
    validation_state = "PASS"
    if error:
        validation_state = "FAIL"
        validation_notes.append(error)
    if not files_ok:
        validation_state = "FAIL"
        validation_notes.append("非合作模式核心输出文件缺失")
    if observed_signal.empty:
        validation_state = "FAIL"
        validation_notes.append("没有观测到信号事件")
    if inferred_edges.empty:
        validation_state = "FAIL"
        validation_notes.append("没有生成拓扑推断边")
    if key_nodes.empty:
        validation_state = "FAIL"
        validation_notes.append("没有生成关键节点候选")

    return {
        "run_name": spec.name,
        "is_realism_profile": spec.name.startswith("realism_"),
        "category": "non_cooperative",
        "operation_mode": spec.operation_mode,
        "communication_mode": "",
        "scene_type": spec.scene_type,
        "difficulty": spec.difficulty,
        "formation": spec.formation,
        "duration": spec.duration,
        "map_file": spec.map_file or "",
        "validation_state": validation_state,
        "validation_notes": " | ".join(validation_notes),
        "files_ok": files_ok,
        "observed_signal_count": int(len(observed_signal)),
        "observed_window_count": int(len(observed_windows)),
        "link_evidence_count": int(len(link_evidence)),
        "inferred_edge_count": int(len(inferred_edges)),
        "inferred_node_count": int(len(inferred_nodes)),
        "key_node_count": int(len(key_nodes)),
        "avg_edge_probability": avg_edge_probability,
        "top_key_node_score": top_key_node_score,
        "observed_missing_ratio": observed_missing_ratio,
        "avg_window_confidence": avg_window_confidence,
        "avg_evidence_strength": avg_evidence_strength,
        "output_dir": str(run_dir.relative_to(ROOT)),
        "error": error or "",
        "environment_source": env.get("environmentSource"),
        "effective_model_summary": env.get("effectiveModelSummary"),
        "carrier_frequency_ghz": env.get("carrierFrequencyGHz"),
        "channel_bandwidth_mhz": env.get("channelBandwidthMHz"),
        "polarization_mode": env.get("polarizationMode"),
        "urban_altitude_penalty_db_low": env.get("urbanAltitudePenaltyDbLow"),
        "urban_altitude_gain_db_high": env.get("urbanAltitudeGainDbHigh"),
        "urban_street_canyon_factor": env.get("urbanStreetCanyonFactor"),
        "lake_volatility_jitter_db": env.get("lakeVolatilityJitterDb"),
        "lake_deep_fade_probability": env.get("lakeDeepFadeProbability"),
        "lake_deep_fade_max_db": env.get("lakeDeepFadeMaxDb"),
        "lake_reflection_delay_jitter_ms": env.get("lakeReflectionDelayJitterMs"),
        "reroute_pressure_factor": env.get("reroutePressureFactor"),
        "control_message_urgency_factor": env.get("controlMessageUrgencyFactor"),
        "relay_instability_factor": env.get("relayInstabilityFactor"),
        "formation_reconfig_penalty": env.get("formationReconfigPenalty"),
    }


def summarize_noncooperative_attack_run(spec: RunSpec, run_dir: Path, error: Optional[str]) -> Dict:
    env = ensure_json(run_dir / "environment_summary.json")
    attack_plan = ensure_json(run_dir / "noncooperative_attack_plan.json")
    pre_post = ensure_json(run_dir / "noncooperative_pre_post_comparison.json")
    recommendations = coerce_numeric(
        ensure_csv(run_dir / "noncooperative_attack_recommendations.csv"),
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
    if recommendations.empty:
        recommendations = coerce_numeric(
            ensure_csv(run_dir / "_preflight" / "noncooperative_attack_recommendations.csv"),
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
    bindings = coerce_numeric(
        ensure_csv(run_dir / "noncooperative_target_binding.csv"),
        [
            "eventTime",
            "observedNodeId",
            "bindingConfidence",
            "boundTargetObjectKey",
            "executedEntityNodeId",
            "isTrackStable",
            "isTrackActive",
            "isTrueCriticalTarget",
        ],
    )
    events = coerce_numeric(
        ensure_csv(run_dir / "noncooperative_attack_events.csv"),
        [
            "eventTime",
            "recommendedObservedNodeId",
            "confirmedObservedNodeId",
            "executedObservedNodeId",
            "isTrueTargetHit",
            "nodeRemoved",
            "executedEntityNodeId",
            "boundTargetObjectKey",
        ],
    )
    effect_metrics = coerce_numeric(
        ensure_csv(run_dir / "noncooperative_attack_effect_metrics.csv"),
        [
            "time",
            "connectivityRatio",
            "pdr",
            "throughputMbps",
            "delayMs",
            "damageDuration",
            "recoveryProgress",
            "recommendedObservedNodeId",
            "confirmedObservedNodeId",
            "executedObservedNodeId",
        ],
    )

    files_ok = all(
        (run_dir / name).exists()
        for name in [
            "environment_summary.json",
            "noncooperative_attack_recommendations.csv",
            "noncooperative_attack_plan.json",
            "noncooperative_attack_events.csv",
            "noncooperative_target_binding.csv",
            "noncooperative_attack_effect_metrics.csv",
            "noncooperative_pre_post_comparison.json",
        ]
    )

    def phase_scope_avg(phase: str, scope: str, col: str) -> float:
        if effect_metrics.empty or "phase" not in effect_metrics.columns or "targetScope" not in effect_metrics.columns:
            return math.nan
        series = effect_metrics.loc[
            (effect_metrics["phase"] == phase) & (effect_metrics["targetScope"] == scope), col
        ].dropna()
        return float(series.mean()) if not series.empty else math.nan

    pre_global_connectivity = phase_scope_avg("pre_attack", "global", "connectivityRatio")
    post_global_connectivity = phase_scope_avg("immediate_post_attack", "global", "connectivityRatio")
    pre_global_pdr = phase_scope_avg("pre_attack", "global", "pdr")
    post_global_pdr = phase_scope_avg("immediate_post_attack", "global", "pdr")
    pre_global_throughput = phase_scope_avg("pre_attack", "global", "throughputMbps")
    post_global_throughput = phase_scope_avg("immediate_post_attack", "global", "throughputMbps")
    pre_global_delay = phase_scope_avg("pre_attack", "global", "delayMs")
    post_global_delay = phase_scope_avg("immediate_post_attack", "global", "delayMs")
    pre_local_connectivity = phase_scope_avg("pre_attack", "target_neighborhood", "connectivityRatio")
    post_local_connectivity = phase_scope_avg("immediate_post_attack", "target_neighborhood", "connectivityRatio")
    pre_local_pdr = phase_scope_avg("pre_attack", "target_neighborhood", "pdr")
    post_local_pdr = phase_scope_avg("immediate_post_attack", "target_neighborhood", "pdr")
    pre_local_delay = phase_scope_avg("pre_attack", "target_neighborhood", "delayMs")
    post_local_delay = phase_scope_avg("immediate_post_attack", "target_neighborhood", "delayMs")

    recovery_summary = pre_post.get("recoverySummary", {})
    validation_notes: List[str] = []
    validation_state = "PASS"
    if error:
        validation_state = "FAIL"
        validation_notes.append(error)
    if not files_ok:
        validation_state = "FAIL"
        validation_notes.append("非合作打击核心输出文件缺失")
    if recommendations.empty:
        validation_state = "FAIL"
        validation_notes.append("没有生成打击推荐结果")
    if bindings.empty:
        validation_state = "FAIL"
        validation_notes.append("没有生成目标绑定记录")
    if events.empty:
        validation_state = "FAIL"
        validation_notes.append("没有生成打击事件")
    if effect_metrics.empty:
        validation_state = "FAIL"
        validation_notes.append("没有生成打击效果指标")

    node_removed = bool(pd.to_numeric(events.get("nodeRemoved"), errors="coerce").fillna(0).max()) if not events.empty and "nodeRemoved" in events.columns else False
    if validation_state == "PASS" and not node_removed:
        validation_state = "WARN"
        validation_notes.append("打击事件已记录，但节点未成功移除")

    return {
        "run_name": spec.name,
        "is_realism_profile": spec.name.startswith("realism_"),
        "category": "non_cooperative_attack",
        "operation_mode": spec.operation_mode,
        "communication_mode": "",
        "scene_type": spec.scene_type,
        "difficulty": spec.difficulty,
        "formation": spec.formation,
        "duration": spec.duration,
        "map_file": spec.map_file or "",
        "validation_state": validation_state,
        "validation_notes": " | ".join(validation_notes),
        "files_ok": files_ok,
        "recommendation_count": int(len(recommendations)),
        "binding_count": int(len(bindings)),
        "attack_event_count": int(len(events)),
        "effect_metric_count": int(len(effect_metrics)),
        "recommended_observed_node_id": attack_plan.get("recommendedObservedNodeId"),
        "confirmed_observed_node_id": attack_plan.get("confirmedObservedNodeId"),
        "executed_observed_node_id": attack_plan.get("executedObservedNodeId"),
        "executed_entity_node_id": attack_plan.get("executedEntityNodeId"),
        "target_binding_status": attack_plan.get("targetBindingStatus"),
        "attack_executed": recovery_summary.get("attackExecuted"),
        "attack_execute_time": attack_plan.get("strikeExecuteTime"),
        "recommendation_score": attack_plan.get("recommendedScore"),
        "structure_score": attack_plan.get("structureScore"),
        "evidence_support_score": attack_plan.get("evidenceSupportScore"),
        "causal_support_score": attack_plan.get("causalSupportScore"),
        "directional_influence_score": attack_plan.get("directionalInfluenceScore"),
        "temporal_stability_score": attack_plan.get("temporalStabilityScore"),
        "local_bridge_score": attack_plan.get("localBridgeScore"),
        "post_removal_damage_score": attack_plan.get("postRemovalDamageScore"),
        "two_hop_reachability_score": attack_plan.get("twoHopReachabilityScore"),
        "inter_cluster_bridge_score": attack_plan.get("interClusterBridgeScore"),
        "local_cut_risk_score": attack_plan.get("localCutRiskScore"),
        "neighbor_redundancy_penalty": attack_plan.get("neighborRedundancyPenalty"),
        "recovery_completed_at": recovery_summary.get("recoveryCompletedAt"),
        "target_neighborhood_size": recovery_summary.get("targetNeighborhoodSize"),
        "pre_attack_global_connectivity": pre_global_connectivity,
        "post_attack_global_connectivity": post_global_connectivity,
        "global_connectivity_delta": post_global_connectivity - pre_global_connectivity,
        "pre_attack_global_pdr": pre_global_pdr,
        "post_attack_global_pdr": post_global_pdr,
        "global_pdr_delta": post_global_pdr - pre_global_pdr,
        "pre_attack_global_throughput_mbps": pre_global_throughput,
        "post_attack_global_throughput_mbps": post_global_throughput,
        "global_throughput_delta_mbps": post_global_throughput - pre_global_throughput,
        "pre_attack_global_delay_ms": pre_global_delay,
        "post_attack_global_delay_ms": post_global_delay,
        "global_delay_delta_ms": post_global_delay - pre_global_delay,
        "pre_attack_local_connectivity": pre_local_connectivity,
        "post_attack_local_connectivity": post_local_connectivity,
        "local_connectivity_delta": post_local_connectivity - pre_local_connectivity,
        "pre_attack_local_pdr": pre_local_pdr,
        "post_attack_local_pdr": post_local_pdr,
        "local_pdr_delta": post_local_pdr - pre_local_pdr,
        "pre_attack_local_delay_ms": pre_local_delay,
        "post_attack_local_delay_ms": post_local_delay,
        "local_delay_delta_ms": post_local_delay - pre_local_delay,
        "output_dir": str(run_dir.relative_to(ROOT)),
        "error": error or "",
        "environment_source": env.get("environmentSource"),
        "effective_model_summary": env.get("effectiveModelSummary"),
        "carrier_frequency_ghz": env.get("carrierFrequencyGHz"),
        "channel_bandwidth_mhz": env.get("channelBandwidthMHz"),
        "polarization_mode": env.get("polarizationMode"),
        "urban_altitude_penalty_db_low": env.get("urbanAltitudePenaltyDbLow"),
        "urban_altitude_gain_db_high": env.get("urbanAltitudeGainDbHigh"),
        "urban_street_canyon_factor": env.get("urbanStreetCanyonFactor"),
        "lake_volatility_jitter_db": env.get("lakeVolatilityJitterDb"),
        "lake_deep_fade_probability": env.get("lakeDeepFadeProbability"),
        "lake_deep_fade_max_db": env.get("lakeDeepFadeMaxDb"),
        "lake_reflection_delay_jitter_ms": env.get("lakeReflectionDelayJitterMs"),
        "reroute_pressure_factor": env.get("reroutePressureFactor"),
        "control_message_urgency_factor": env.get("controlMessageUrgencyFactor"),
        "relay_instability_factor": env.get("relayInstabilityFactor"),
        "formation_reconfig_penalty": env.get("formationReconfigPenalty"),
    }


def plot_cooperative_summary(coop_df: pd.DataFrame, plots_dir: Path) -> List[str]:
    if coop_df.empty:
        return []

    ordered = coop_df.copy()
    ordered["scene_type"] = pd.Categorical(ordered["scene_type"], SCENES, ordered=True)
    ordered["communication_mode"] = pd.Categorical(
        ordered["communication_mode"], COOP_MODES, ordered=True
    )
    ordered = ordered.sort_values(["scene_type", "communication_mode"])

    metrics = [
        ("connectivity_end", "最终连通率"),
        ("pdr_end", "最终 PDR"),
        ("delay_end_ms", "最终平均时延 (ms)"),
        ("recovery_time_sec", "恢复时间 (s)"),
        ("max_p99_delay_during_ms", "故障窗口 P99 最大时延 (ms)"),
    ]
    fig, axes = plt.subplots(len(metrics), 1, figsize=(15, 16), sharex=True)

    x_labels = [
        f"{scene}\n{mode}"
        for scene, mode in zip(ordered["scene_type"].astype(str), ordered["communication_mode"].astype(str))
    ]
    x = np.arange(len(x_labels))
    colors = ordered["validation_state"].map(
        {"PASS": "#2ecc71", "WARN": "#f1c40f", "FAIL": "#e74c3c"}
    )

    for ax, (col, title) in zip(axes, metrics):
        values = (
            pd.to_numeric(ordered[col], errors="coerce")
            if col in ordered.columns
            else pd.Series([math.nan] * len(ordered))
        )
        ax.bar(x, values, color=colors)
        ax.set_title(title)
        ax.grid(True, axis="y", alpha=0.25)

    axes[-1].set_xticks(x)
    axes[-1].set_xticklabels(x_labels, rotation=45, ha="right")
    plt.tight_layout()

    output = plots_dir / "cooperative_summary.png"
    plt.savefig(output, dpi=180, bbox_inches="tight")
    plt.close(fig)
    return [output.name]


def plot_cooperative_impact_summary(coop_df: pd.DataFrame, plots_dir: Path) -> List[str]:
    if coop_df.empty:
        return []

    ordered = coop_df.copy()
    ordered["scene_type"] = pd.Categorical(ordered["scene_type"], SCENES, ordered=True)
    ordered["communication_mode"] = pd.Categorical(
        ordered["communication_mode"], COOP_MODES, ordered=True
    )
    ordered = ordered.sort_values(["scene_type", "communication_mode"])

    metrics = [
        ("during_failure_local_pdr", "故障邻域 PDR"),
        ("during_failure_target_pdr", "故障目标 PDR"),
        ("during_failure_local_delay_ms", "故障邻域时延 (ms)"),
        ("during_failure_target_delay_ms", "故障目标时延 (ms)"),
        ("local_pdr_delta_during", "故障邻域 PDR Δ"),
        ("target_pdr_delta_during", "故障目标 PDR Δ"),
        ("local_delay_delta_during_ms", "故障邻域时延 Δ"),
        ("target_delay_delta_during_ms", "故障目标时延 Δ"),
    ]
    fig, axes = plt.subplots(len(metrics), 1, figsize=(15, 24), sharex=True)

    x_labels = [
        f"{scene}\n{mode}"
        for scene, mode in zip(
            ordered["scene_type"].astype(str), ordered["communication_mode"].astype(str)
        )
    ]
    x = np.arange(len(x_labels))
    colors = ordered["validation_state"].map(
        {"PASS": "#2ecc71", "WARN": "#f1c40f", "FAIL": "#e74c3c"}
    )

    for ax, (col, title) in zip(axes, metrics):
        values = (
            pd.to_numeric(ordered[col], errors="coerce")
            if col in ordered.columns
            else pd.Series([math.nan] * len(ordered))
        )
        ax.bar(x, values, color=colors)
        ax.set_title(title)
        ax.grid(True, axis="y", alpha=0.25)
        if "Δ" in title:
            ax.axhline(0.0, color="#6b7280", linewidth=0.8, linestyle="--")

    axes[-1].set_xticks(x)
    axes[-1].set_xticklabels(x_labels, rotation=45, ha="right")
    plt.tight_layout()

    output = plots_dir / "cooperative_impact_summary.png"
    plt.savefig(output, dpi=180, bbox_inches="tight")
    plt.close(fig)
    return [output.name]


def plot_cooperative_delta_heatmap(coop_df: pd.DataFrame, plots_dir: Path) -> List[str]:
    if coop_df.empty:
        return []

    ordered = coop_df.copy()
    ordered["scene_type"] = pd.Categorical(ordered["scene_type"], SCENES, ordered=True)
    ordered["communication_mode"] = pd.Categorical(
        ordered["communication_mode"], COOP_MODES, ordered=True
    )
    ordered = ordered.sort_values(["scene_type", "communication_mode"]).reset_index(drop=True)

    metric_specs = [
        ("connectivity_drop_during", "Conn Δ"),
        ("pdr_delta_during", "PDR Δ"),
        ("throughput_delta_during_mbps", "Thr Δ"),
        ("delay_delta_during_ms", "Delay Δ"),
        ("p99_delay_delta_during_ms", "P99 Δ"),
        ("local_pdr_delta_during", "Local PDR Δ"),
        ("local_delay_delta_during_ms", "Local Delay Δ"),
        ("target_pdr_delta_during", "Target PDR Δ"),
        ("target_delay_delta_during_ms", "Target Delay Δ"),
    ]
    matrix = []
    for _, row in ordered.iterrows():
        matrix.append(
            [
                pd.to_numeric(row[col], errors="coerce") if col in ordered.columns else math.nan
                for col, _ in metric_specs
            ]
        )
    data = np.array(matrix, dtype=float)

    fig, ax = plt.subplots(figsize=(10, max(6, len(ordered) * 0.45)))
    vmax = np.nanmax(np.abs(data)) if np.isfinite(data).any() else 1.0
    vmax = max(vmax, 1e-6)
    im = ax.imshow(data, aspect="auto", cmap="RdBu_r", vmin=-vmax, vmax=vmax)

    ax.set_xticks(np.arange(len(metric_specs)))
    ax.set_xticklabels([label for _, label in metric_specs])
    ax.set_yticks(np.arange(len(ordered)))
    ax.set_yticklabels(
        [f"{row.scene_type} / {row.communication_mode}" for row in ordered.itertuples()]
    )
    ax.set_title("合作模式故障窗口指标变化热力图（during - pre）")

    for i in range(data.shape[0]):
        for j in range(data.shape[1]):
            value = data[i, j]
            if np.isnan(value):
                text = "nan"
            else:
                text = f"{value:.2f}"
            ax.text(j, i, text, ha="center", va="center", fontsize=8, color="#111827")

    fig.colorbar(im, ax=ax, shrink=0.9)
    plt.tight_layout()
    output = plots_dir / "cooperative_delta_heatmap.png"
    plt.savefig(output, dpi=180, bbox_inches="tight")
    plt.close(fig)
    return [output.name]


def plot_cooperative_timelines(coop_df: pd.DataFrame, runs_root: Path, plots_dir: Path) -> List[str]:
    generated: List[str] = []
    for row in coop_df.to_dict("records"):
        run_dir = ROOT / row["output_dir"]
        metrics_json = ensure_json(run_dir / "cooperative_metrics_timeseries.json")
        failure_timeline = ensure_json(run_dir / "cooperative_failure_timeline.json")
        recovery_timeline = ensure_json(run_dir / "cooperative_recovery_timeline.json")
        mode_summary = ensure_json(run_dir / "cooperative_mode_summary.json")
        topology_df = coerce_numeric(
            ensure_csv(run_dir / "topology_detailed.csv"),
            ["time", "num_nodes", "num_links", "avg_degree", "network_density"],
        )
        resource_df = coerce_numeric(
            ensure_csv(run_dir / "resource_allocation_detailed.csv"),
            [
                "time",
                "node_id",
                "channel",
                "tx_power",
                "data_rate",
                "neighbors",
                "interference_dBm",
                "worst_sinr_dB",
            ],
        )
        samples = metrics_json.get("samples", [])
        if not samples:
            continue

        df = pd.DataFrame(samples)
        if df.empty or "time" not in df.columns:
            continue
        df = coerce_numeric(
            df,
            [
                "time",
                "connectivity",
                "avgDegree",
                "pdr",
                "throughputMbps",
                "delayMs",
                "p99DelayMs",
                "failureNeighborhoodPdr",
                "failureNeighborhoodThroughputMbps",
                "failureNeighborhoodDelayMs",
                "failureNeighborhoodNodeCount",
                "failureTargetId",
                "failureTargetPdr",
                "failureTargetThroughputMbps",
                "failureTargetDelayMs",
                "activeNodeCount",
                "leaderNodeId",
            ],
        )

        failure_start = mode_summary.get("failureStartTime")
        failure_duration = mode_summary.get("failureDuration")
        failure_end = (
            float(failure_start) + float(failure_duration)
            if failure_start is not None and failure_duration is not None
            else None
        )
        failure_events = failure_timeline.get("events", [])
        recovery_actions = recovery_timeline.get("actions", [])
        target_node_id = mode_summary.get("failureTargetId", -1)

        fig, axes = plt.subplots(7, 1, figsize=(15, 22), sharex=True)
        fig.suptitle(row["run_name"], fontsize=13, fontweight="bold")

        axes[0].plot(df["time"], df["connectivity"], color="#2980b9", linewidth=1.8, label="Connectivity")
        if not topology_df.empty and "avg_degree" in topology_df.columns:
            ax0b = axes[0].twinx()
            ax0b.plot(
                topology_df["time"],
                topology_df["avg_degree"],
                color="#34495e",
                linewidth=1.2,
                linestyle="--",
                label="Avg Degree",
            )
            ax0b.set_ylabel("Avg Degree")
        axes[0].grid(True, alpha=0.25)
        axes[0].set_ylabel("Connectivity")
        axes[0].legend(loc="upper left")

        axes[1].plot(df["time"], df["pdr"], color="#27ae60", linewidth=1.8, label="Global PDR")
        if "failureNeighborhoodPdr" in df.columns:
            axes[1].plot(
                df["time"],
                df["failureNeighborhoodPdr"],
                color="#f39c12",
                linewidth=1.5,
                linestyle="--",
                label="Neighborhood PDR",
            )
        if "failureTargetPdr" in df.columns:
            axes[1].plot(
                df["time"],
                df["failureTargetPdr"],
                color="#c0392b",
                linewidth=1.3,
                linestyle=":",
                label="Target PDR",
            )
        axes[1].set_ylabel("PDR")
        axes[1].grid(True, alpha=0.25)
        axes[1].legend(loc="upper right", ncol=3, fontsize=8)

        axes[2].plot(df["time"], df["delayMs"], color="#d35400", linewidth=1.8, label="Global Delay")
        if "failureNeighborhoodDelayMs" in df.columns:
            axes[2].plot(
                df["time"],
                df["failureNeighborhoodDelayMs"],
                color="#e67e22",
                linewidth=1.5,
                linestyle="--",
                label="Neighborhood Delay",
            )
        if "failureTargetDelayMs" in df.columns:
            axes[2].plot(
                df["time"],
                df["failureTargetDelayMs"],
                color="#922b21",
                linewidth=1.3,
                linestyle=":",
                label="Target Delay",
            )
        if "p99DelayMs" in df.columns:
            axes[2].plot(
                df["time"],
                df["p99DelayMs"],
                color="#8e44ad",
                linewidth=1.2,
                linestyle="--",
                label="P99 Delay",
            )
            axes[2].legend(loc="upper right")
        axes[2].set_ylabel("Delay (ms)")
        axes[2].set_yscale("symlog", linthresh=10.0)
        axes[2].grid(True, alpha=0.25)

        axes[3].plot(
            df["time"],
            df["throughputMbps"],
            color="#16a085",
            linewidth=1.8,
            label="Global Throughput",
        )
        if "failureNeighborhoodThroughputMbps" in df.columns:
            axes[3].plot(
                df["time"],
                df["failureNeighborhoodThroughputMbps"],
                color="#3498db",
                linewidth=1.5,
                linestyle="--",
                label="Neighborhood Throughput",
            )
        if "failureTargetThroughputMbps" in df.columns:
            axes[3].plot(
                df["time"],
                df["failureTargetThroughputMbps"],
                color="#2c3e50",
                linewidth=1.3,
                linestyle=":",
                label="Target Throughput",
            )
        axes[3].set_ylabel("Throughput (Mbps)")
        axes[3].grid(True, alpha=0.25)
        axes[3].legend(loc="upper right", ncol=3, fontsize=8)

        plot_state_series(
            axes[4],
            df["time"],
            df["leaderNodeId"],
            color="#8e44ad",
            label="Leader Node",
            linewidth=1.6,
        )
        leader_alive = df["isLeaderAlive"].map(lambda x: 1 if bool(x) else 0)
        plot_state_series(
            axes[4],
            df["time"],
            leader_alive,
            color="#c0392b",
            label="Leader Alive (0/1)",
            linestyle="--",
            linewidth=1.2,
        )
        plot_state_series(
            axes[4],
            df["time"],
            df["activeNodeCount"],
            color="#2c3e50",
            label="Active Nodes",
            linestyle=":",
            linewidth=1.1,
        )
        if "failureNeighborhoodNodeCount" in df.columns:
            plot_state_series(
                axes[4],
                df["time"],
                df["failureNeighborhoodNodeCount"],
                color="#f39c12",
                label="Frozen Neighborhood Size",
                linestyle="-.",
                linewidth=1.1,
            )
        axes[4].set_ylabel("State / Count")
        axes[4].set_title("离散状态量：Leader / 存活 / 冻结邻域", fontsize=10)
        axes[4].grid(True, alpha=0.25)
        axes[4].legend(loc="upper right", ncol=4, fontsize=8)

        target_df = pd.DataFrame()
        if not resource_df.empty and "node_id" in resource_df.columns and target_node_id is not None:
            target_df = resource_df[resource_df["node_id"] == int(target_node_id)].copy()

        if not target_df.empty:
            plot_state_series(
                axes[5],
                target_df["time"],
                target_df["neighbors"],
                color="#e67e22",
                label="Target Neighbors",
                linewidth=1.6,
            )
            ax4b = axes[5].twinx()
            ax4b.plot(
                target_df["time"],
                target_df["worst_sinr_dB"],
                color="#16a085",
                linewidth=1.2,
                linestyle="--",
                label="Target Worst SINR",
            )
            axes[5].set_ylabel("Neighbors")
            ax4b.set_ylabel("Worst SINR (dB)")
            axes[5].grid(True, alpha=0.25)
            axes[5].set_title(f"Failure Target Node {target_node_id} Local State", fontsize=10)

            plot_state_series(
                axes[6],
                target_df["time"],
                target_df["channel"],
                color="#2980b9",
                label="Channel",
                linewidth=1.5,
            )
            axes[6].plot(
                target_df["time"],
                target_df["data_rate"],
                color="#27ae60",
                linewidth=1.2,
                linestyle="--",
                label="Data Rate",
            )
            ax5b = axes[6].twinx()
            ax5b.plot(
                target_df["time"],
                target_df["tx_power"],
                color="#7f8c8d",
                linewidth=1.1,
                linestyle=":",
                label="Tx Power",
            )
            axes[6].set_ylabel("Channel / Rate")
            ax5b.set_ylabel("Tx Power (dBm)")
            axes[6].grid(True, alpha=0.25)
            axes[6].legend(loc="upper left", ncol=2, fontsize=8)
        else:
            axes[5].axis("off")
            axes[6].axis("off")

        annotate_cooperative_events(
            axes,
            float(failure_start) if failure_start is not None else None,
            failure_end,
            failure_events,
            recovery_actions,
            df,
        )

        axes[-1].set_xlabel("Time (s)")

        plt.tight_layout()
        name = f"{slugify(row['run_name'])}_timeline.png"
        output = plots_dir / name
        plt.savefig(output, dpi=160, bbox_inches="tight")
        plt.close(fig)
        generated.append(name)

    return generated


def plot_scene_realism_profiles(all_df: pd.DataFrame, plots_dir: Path) -> List[str]:
    if all_df.empty or "is_realism_profile" not in all_df.columns:
        return []

    realism_df = all_df.loc[all_df["is_realism_profile"] == True].copy()  # noqa: E712
    if realism_df.empty:
        return []

    metrics = [
        ("connectivity_end", "最终连通率"),
        ("pdr_end", "最终 PDR"),
        ("delay_end_ms", "最终平均时延 (ms)"),
    ]

    fig, axes = plt.subplots(len(metrics), 1, figsize=(13, 12), sharex=True)
    if len(metrics) == 1:
        axes = [axes]

    x = np.arange(len(realism_df))
    labels = realism_df["run_name"].tolist()
    colors = ["#2E86AB", "#F18F01", "#C73E1D", "#6C9A8B"]

    for idx, (metric, title) in enumerate(metrics):
        ax = axes[idx]
        vals = pd.to_numeric(realism_df[metric], errors="coerce").fillna(0.0)
        ax.bar(x, vals, color=colors[idx % len(colors)], alpha=0.88)
        ax.set_ylabel(title)
        ax.grid(True, alpha=0.25, linestyle="--")

    axes[-1].set_xticks(x)
    axes[-1].set_xticklabels(labels, rotation=20, ha="right")
    axes[0].set_title("场景真实性专项验证")

    output = plots_dir / "scene_realism_profiles.png"
    fig.tight_layout()
    fig.savefig(output, dpi=160)
    plt.close(fig)
    return [output.name]


def plot_noncooperative_summary(non_df: pd.DataFrame, plots_dir: Path) -> List[str]:
    if non_df.empty:
        return []

    ordered = non_df.copy()
    ordered["scene_type"] = pd.Categorical(ordered["scene_type"], SCENES, ordered=True)
    ordered = ordered.sort_values("scene_type")

    fig, axes = plt.subplots(3, 1, figsize=(12, 12), sharex=True)
    x = np.arange(len(ordered))
    labels = ordered["scene_type"].astype(str).tolist()

    width = 0.25
    observed_signal = (
        pd.to_numeric(ordered["observed_signal_count"], errors="coerce")
        if "observed_signal_count" in ordered.columns
        else pd.Series([math.nan] * len(ordered))
    )
    inferred_edge = (
        pd.to_numeric(ordered["inferred_edge_count"], errors="coerce")
        if "inferred_edge_count" in ordered.columns
        else pd.Series([math.nan] * len(ordered))
    )
    key_node = (
        pd.to_numeric(ordered["key_node_count"], errors="coerce")
        if "key_node_count" in ordered.columns
        else pd.Series([math.nan] * len(ordered))
    )
    avg_edge_prob = (
        pd.to_numeric(ordered["avg_edge_probability"], errors="coerce")
        if "avg_edge_probability" in ordered.columns
        else pd.Series([math.nan] * len(ordered))
    )
    top_key_score = (
        pd.to_numeric(ordered["top_key_node_score"], errors="coerce")
        if "top_key_node_score" in ordered.columns
        else pd.Series([math.nan] * len(ordered))
    )
    observed_missing_ratio = (
        pd.to_numeric(ordered["observed_missing_ratio"], errors="coerce")
        if "observed_missing_ratio" in ordered.columns
        else pd.Series([math.nan] * len(ordered))
    )
    avg_window_confidence = (
        pd.to_numeric(ordered["avg_window_confidence"], errors="coerce")
        if "avg_window_confidence" in ordered.columns
        else pd.Series([math.nan] * len(ordered))
    )
    avg_evidence_strength = (
        pd.to_numeric(ordered["avg_evidence_strength"], errors="coerce")
        if "avg_evidence_strength" in ordered.columns
        else pd.Series([math.nan] * len(ordered))
    )

    axes[0].bar(x - width, observed_signal, width=width, label="Observed Signals")
    axes[0].bar(x, inferred_edge, width=width, label="Inferred Edges")
    axes[0].bar(x + width, key_node, width=width, label="Key Nodes")
    axes[0].set_ylabel("Count")
    axes[0].set_title("非合作观测/推断产物数量")
    axes[0].grid(True, axis="y", alpha=0.25)
    axes[0].legend(loc="upper right")

    axes[1].bar(x - width / 2, avg_edge_prob, width=width, label="Avg Edge Prob.")
    axes[1].bar(x + width / 2, top_key_score, width=width, label="Top Key Node Score")
    axes[1].set_ylabel("Score")
    axes[1].set_title("非合作推断置信度与关键节点得分")
    axes[1].grid(True, axis="y", alpha=0.25)
    axes[1].legend(loc="upper right")
    axes[1].set_xticks(x)
    axes[1].set_xticklabels(labels)

    axes[2].bar(x - width, observed_missing_ratio, width=width, label="Missing Ratio")
    axes[2].bar(x, avg_window_confidence, width=width, label="Avg Confidence")
    axes[2].bar(x + width, avg_evidence_strength, width=width, label="Avg Evidence")
    axes[2].set_ylabel("Score")
    axes[2].set_title("非合作观测质量")
    axes[2].grid(True, axis="y", alpha=0.25)
    axes[2].legend(loc="upper right")
    axes[2].set_xticks(x)
    axes[2].set_xticklabels(labels)

    plt.tight_layout()
    output = plots_dir / "noncooperative_summary.png"
    plt.savefig(output, dpi=180, bbox_inches="tight")
    plt.close(fig)
    return [output.name]


def plot_noncooperative_timelines(non_df: pd.DataFrame, plots_dir: Path) -> List[str]:
    generated: List[str] = []
    for row in non_df.to_dict("records"):
        run_dir = ROOT / row["output_dir"]
        observed_windows = coerce_numeric(
            ensure_csv(run_dir / "observed_comm_windows.csv"),
            [
                "windowStart",
                "windowEnd",
                "activeRatio",
                "overallConfidence",
                "noiseLevel",
                "signalDetected",
                "isMissing",
            ],
        )
        link_evidence = coerce_numeric(
            ensure_csv(run_dir / "observed_link_evidence.csv"),
            ["windowStart", "evidenceStrength", "observerAgreementScore", "edgeObservationConfidence"],
        )
        inferred_edges = coerce_numeric(
            ensure_csv(run_dir / "inferred_topology_edges.csv"),
            ["windowStart", "edgeProbability", "edgeConfidence"],
        )
        key_nodes = coerce_numeric(
            ensure_csv(run_dir / "key_node_candidates.csv"),
            ["windowStart", "keyNodeScore", "rank"],
        )

        if observed_windows.empty:
            continue

        win_group = observed_windows.groupby("windowStart").agg(
            observed_count=("signalDetected", "sum"),
            missing_count=("isMissing", "sum"),
            avg_active_ratio=("activeRatio", "mean"),
            avg_confidence=("overallConfidence", "mean"),
            avg_noise=("noiseLevel", "mean"),
        ).reset_index()

        evidence_group = pd.DataFrame()
        if not link_evidence.empty:
            evidence_group = link_evidence.groupby("windowStart").agg(
                evidence_count=("evidenceStrength", "size"),
                avg_evidence_strength=("evidenceStrength", "mean"),
                avg_observer_agreement=("observerAgreementScore", "mean"),
            ).reset_index()

        edge_group = pd.DataFrame()
        if not inferred_edges.empty:
            edge_group = inferred_edges.groupby("windowStart").agg(
                inferred_edge_count=("edgeProbability", "size"),
                avg_edge_probability=("edgeProbability", "mean"),
                avg_edge_confidence=("edgeConfidence", "mean"),
            ).reset_index()

        key_group = pd.DataFrame()
        if not key_nodes.empty:
            key_group = key_nodes.groupby("windowStart").agg(
                top_key_node_score=("keyNodeScore", "max"),
                key_node_count=("observedNodeId", "size"),
            ).reset_index()

        fig, axes = plt.subplots(4, 1, figsize=(14, 14), sharex=True)
        fig.suptitle(row["run_name"], fontsize=13, fontweight="bold")

        axes[0].plot(win_group["windowStart"], win_group["observed_count"], color="#2980b9", label="Observed")
        axes[0].plot(win_group["windowStart"], win_group["missing_count"], color="#c0392b", linestyle="--", label="Missing")
        axes[0].set_ylabel("Window Count")
        axes[0].set_title("观测窗口命中/缺失")
        axes[0].grid(True, alpha=0.25)
        axes[0].legend(loc="upper right")

        axes[1].plot(win_group["windowStart"], win_group["avg_confidence"], color="#27ae60", label="Avg Confidence")
        axes[1].plot(win_group["windowStart"], win_group["avg_active_ratio"], color="#16a085", linestyle="--", label="Active Ratio")
        axes[1].plot(win_group["windowStart"], win_group["avg_noise"], color="#8e44ad", linestyle=":", label="Noise")
        axes[1].set_ylabel("Score")
        axes[1].set_title("窗口质量与噪声")
        axes[1].grid(True, alpha=0.25)
        axes[1].legend(loc="upper right")

        if not evidence_group.empty:
            axes[2].plot(
                evidence_group["windowStart"],
                evidence_group["evidence_count"],
                color="#d35400",
                label="Evidence Count",
            )
            ax2b = axes[2].twinx()
            ax2b.plot(
                evidence_group["windowStart"],
                evidence_group["avg_evidence_strength"],
                color="#f39c12",
                linestyle="--",
                label="Avg Evidence Strength",
            )
            ax2b.plot(
                evidence_group["windowStart"],
                evidence_group["avg_observer_agreement"],
                color="#7f8c8d",
                linestyle=":",
                label="Observer Agreement",
            )
            axes[2].set_ylabel("Evidence Count")
            ax2b.set_ylabel("Evidence Score")
        axes[2].set_title("边证据强度")
        axes[2].grid(True, alpha=0.25)

        if not edge_group.empty:
            axes[3].plot(
                edge_group["windowStart"],
                edge_group["inferred_edge_count"],
                color="#2c3e50",
                label="Inferred Edges",
            )
            axes[3].plot(
                edge_group["windowStart"],
                edge_group["avg_edge_probability"],
                color="#1abc9c",
                linestyle="--",
                label="Avg Edge Probability",
            )
        if not key_group.empty:
            axes[3].plot(
                key_group["windowStart"],
                key_group["top_key_node_score"],
                color="#e74c3c",
                linestyle=":",
                label="Top Key Node Score",
            )
        axes[3].set_ylabel("Inference")
        axes[3].set_title("推断边与关键节点")
        axes[3].set_xlabel("Window Start (s)")
        axes[3].grid(True, alpha=0.25)
        axes[3].legend(loc="upper right")

        plt.tight_layout()
        name = f"{slugify(row['run_name'])}_noncooperative_timeline.png"
        output = plots_dir / name
        plt.savefig(output, dpi=160, bbox_inches="tight")
        plt.close(fig)
        generated.append(name)

    return generated


def plot_noncooperative_attack_summary(attack_df: pd.DataFrame, plots_dir: Path) -> List[str]:
    if attack_df.empty:
        return []

    ordered = attack_df.copy()
    ordered["scene_type"] = pd.Categorical(ordered["scene_type"], SCENES, ordered=True)
    ordered = ordered.sort_values("scene_type")

    metrics = [
        ("global_connectivity_delta", "全网连通率 Δ"),
        ("global_pdr_delta", "全网 PDR Δ"),
        ("global_delay_delta_ms", "全网时延 Δ (ms)"),
        ("local_connectivity_delta", "目标邻域连通率 Δ"),
        ("local_pdr_delta", "目标邻域 PDR Δ"),
        ("local_delay_delta_ms", "目标邻域时延 Δ (ms)"),
    ]
    fig, axes = plt.subplots(len(metrics), 1, figsize=(12, 18), sharex=True)
    x = np.arange(len(ordered))
    labels = ordered["scene_type"].astype(str).tolist()
    colors = ordered["validation_state"].map(
        {"PASS": "#2ecc71", "WARN": "#f1c40f", "FAIL": "#e74c3c"}
    )

    for ax, (col, title) in zip(axes, metrics):
        values = pd.to_numeric(ordered[col], errors="coerce")
        ax.bar(x, values, color=colors)
        ax.set_title(title)
        ax.axhline(0.0, color="#6b7280", linewidth=0.8, linestyle="--")
        ax.grid(True, axis="y", alpha=0.25)

    axes[-1].set_xticks(x)
    axes[-1].set_xticklabels(labels)
    plt.tight_layout()
    output = plots_dir / "noncooperative_attack_summary.png"
    plt.savefig(output, dpi=180, bbox_inches="tight")
    plt.close(fig)
    return [output.name]


def plot_noncooperative_algorithm_effects(attack_df: pd.DataFrame, plots_dir: Path) -> List[str]:
    if attack_df.empty:
        return []

    ordered = attack_df.copy()
    ordered["scene_type"] = pd.Categorical(ordered["scene_type"], SCENES, ordered=True)
    ordered = ordered.sort_values("scene_type")

    metrics = [
        ("recommendation_score", "Top Recommendation Score"),
        ("structure_score", "Structure Score"),
        ("evidence_support_score", "Evidence Support"),
        ("causal_support_score", "Causal Support"),
        ("directional_influence_score", "Directional Influence"),
        ("temporal_stability_score", "Temporal Stability"),
        ("local_bridge_score", "Local Bridge"),
        ("post_removal_damage_score", "Post-removal Damage"),
        ("two_hop_reachability_score", "Two-hop Reachability"),
        ("inter_cluster_bridge_score", "Inter-cluster Bridge"),
        ("local_cut_risk_score", "Local Cut Risk"),
        ("neighbor_redundancy_penalty", "Neighbor Redundancy Penalty"),
    ]
    fig, axes = plt.subplots(len(metrics), 1, figsize=(12, 31), sharex=True)
    x = np.arange(len(ordered))
    labels = ordered["scene_type"].astype(str).tolist()
    colors = ordered["validation_state"].map(
        {"PASS": "#2ecc71", "WARN": "#f1c40f", "FAIL": "#e74c3c"}
    )

    for ax, (col, title) in zip(axes, metrics):
        values = pd.to_numeric(ordered[col], errors="coerce")
        ax.bar(x, values, color=colors)
        ax.set_title(title)
        ax.set_ylim(0, max(1.0, float(np.nanmax(values)) if not values.isna().all() else 1.0))
        ax.grid(True, axis="y", alpha=0.25)

    axes[-1].set_xticks(x)
    axes[-1].set_xticklabels(labels)
    plt.tight_layout()
    output = plots_dir / "noncooperative_algorithm_effects.png"
    plt.savefig(output, dpi=180, bbox_inches="tight")
    plt.close(fig)
    return [output.name]


def plot_noncooperative_attack_timelines(attack_df: pd.DataFrame, plots_dir: Path) -> List[str]:
    generated: List[str] = []
    for row in attack_df.to_dict("records"):
        run_dir = ROOT / row["output_dir"]
        effect_metrics = coerce_numeric(
            ensure_csv(run_dir / "noncooperative_attack_effect_metrics.csv"),
            [
                "time",
                "connectivityRatio",
                "pdr",
                "throughputMbps",
                "delayMs",
                "damageDuration",
                "recoveryProgress",
            ],
        )
        events = coerce_numeric(
            ensure_csv(run_dir / "noncooperative_attack_events.csv"),
            ["eventTime", "executedObservedNodeId", "executedEntityNodeId", "nodeRemoved"],
        )
        recommendations = coerce_numeric(
            ensure_csv(run_dir / "noncooperative_attack_recommendations.csv"),
            [
                "windowStart",
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
        if effect_metrics.empty:
            continue

        global_df = effect_metrics[effect_metrics["targetScope"] == "global"].copy()
        local_df = effect_metrics[effect_metrics["targetScope"] == "target_neighborhood"].copy()
        if global_df.empty:
            continue

        fig, axes = plt.subplots(5, 1, figsize=(14, 19), sharex=True)
        fig.suptitle(row["run_name"], fontsize=13, fontweight="bold")

        axes[0].plot(global_df["time"], global_df["connectivityRatio"], color="#2980b9", label="Global Connectivity")
        if not local_df.empty:
            axes[0].plot(local_df["time"], local_df["connectivityRatio"], color="#f39c12", linestyle="--", label="Neighborhood Connectivity")
        axes[0].set_ylabel("Connectivity")
        axes[0].grid(True, alpha=0.25)
        axes[0].legend(loc="upper right")
        annotate_attack_phases(axes[0], global_df)

        axes[1].plot(global_df["time"], global_df["pdr"], color="#27ae60", label="Global PDR")
        if not local_df.empty:
            axes[1].plot(local_df["time"], local_df["pdr"], color="#c0392b", linestyle="--", label="Neighborhood PDR")
        axes[1].set_ylabel("PDR")
        axes[1].grid(True, alpha=0.25)
        axes[1].legend(loc="upper right")
        annotate_attack_phases(axes[1], global_df)

        axes[2].plot(global_df["time"], global_df["delayMs"], color="#d35400", label="Global Delay")
        if not local_df.empty:
            axes[2].plot(local_df["time"], local_df["delayMs"], color="#8e44ad", linestyle="--", label="Neighborhood Delay")
        axes[2].set_ylabel("Delay (ms)")
        axes[2].grid(True, alpha=0.25)
        axes[2].legend(loc="upper right")
        annotate_attack_phases(axes[2], global_df)

        axes[3].plot(global_df["time"], global_df["recoveryProgress"], color="#16a085", label="Global Recovery Progress")
        if not local_df.empty:
            axes[3].plot(local_df["time"], local_df["recoveryProgress"], color="#2c3e50", linestyle="--", label="Neighborhood Recovery Progress")
        phase_y = pd.Categorical(global_df["phase"], ["pre_attack", "immediate_post_attack", "recovery", "final"], ordered=True).codes
        ax3c = axes[3].twinx()
        plot_state_series(
            ax3c,
            global_df["time"],
            pd.Series(phase_y, index=global_df.index),
            color="#8e44ad",
            label="Phase",
            linestyle="-.",
            linewidth=1.0,
            marker_size=14.0,
        )
        ax3c.set_yticks([0, 1, 2, 3])
        ax3c.set_yticklabels(["pre", "post", "recovery", "final"])
        ax3c.set_ylabel("Phase")
        if not events.empty:
            for _, event in events.iterrows():
                if pd.notna(event.get("eventTime")):
                    for ax in axes:
                        ax.axvline(float(event["eventTime"]), color="#c0392b", linestyle="--", alpha=0.6)
        axes[3].set_ylabel("Recovery Progress")
        axes[3].grid(True, alpha=0.25)
        axes[3].legend(loc="upper right")
        annotate_attack_phases(axes[3], global_df)

        if not recommendations.empty:
            ranked = recommendations.copy()
            if "recommendationRank" in ranked.columns:
                top_rank = ranked.loc[ranked["recommendationRank"] == 1].copy()
                if not top_rank.empty:
                    ranked = top_rank
            ranked = ranked.sort_values("windowStart")

            axes[4].plot(ranked["windowStart"], ranked["recommendedScore"], color="#111827", label="Top Recommendation")
            if "structureScore" in ranked.columns:
                axes[4].plot(ranked["windowStart"], ranked["structureScore"], color="#2980b9", linestyle="--", label="Structure")
            if "causalSupportScore" in ranked.columns:
                axes[4].plot(ranked["windowStart"], ranked["causalSupportScore"], color="#c0392b", linestyle=":", label="Causality")
            if "directionalInfluenceScore" in ranked.columns:
                axes[4].plot(ranked["windowStart"], ranked["directionalInfluenceScore"], color="#e67e22", linestyle="-", label="Directional")
            if "temporalStabilityScore" in ranked.columns:
                axes[4].plot(ranked["windowStart"], ranked["temporalStabilityScore"], color="#16a085", linestyle="-.", label="Temporal")
            if "localBridgeScore" in ranked.columns:
                axes[4].plot(ranked["windowStart"], ranked["localBridgeScore"], color="#8e44ad", linestyle="--", label="Bridge")
            if "postRemovalDamageScore" in ranked.columns:
                axes[4].plot(ranked["windowStart"], ranked["postRemovalDamageScore"], color="#d35400", linestyle=":", label="Post-removal Damage")
            if "twoHopReachabilityScore" in ranked.columns:
                axes[4].plot(ranked["windowStart"], ranked["twoHopReachabilityScore"], color="#2c3e50", linestyle="-", label="Two-hop")
            if "interClusterBridgeScore" in ranked.columns:
                axes[4].plot(ranked["windowStart"], ranked["interClusterBridgeScore"], color="#7f8c8d", linestyle="--", label="Inter-cluster")
            if "localCutRiskScore" in ranked.columns:
                axes[4].plot(ranked["windowStart"], ranked["localCutRiskScore"], color="#27ae60", linestyle=":", label="Cut Risk")
            if "neighborRedundancyPenalty" in ranked.columns:
                axes[4].plot(ranked["windowStart"], ranked["neighborRedundancyPenalty"], color="#95a5a6", linestyle="-.", label="Redundancy Penalty")
        axes[4].set_ylabel("Rec Score")
        axes[4].set_xlabel("Time (s)")
        axes[4].grid(True, alpha=0.25)
        axes[4].legend(loc="upper right")
        annotate_attack_phases(axes[4], global_df)

        plt.tight_layout()
        name = f"{slugify(row['run_name'])}_attack_timeline.png"
        output = plots_dir / name
        plt.savefig(output, dpi=160, bbox_inches="tight")
        plt.close(fig)
        generated.append(name)

    return generated


def write_html_report(
    output_root: Path,
    coop_df: pd.DataFrame,
    non_df: pd.DataFrame,
    attack_df: pd.DataFrame,
    generated_plots: List[str],
) -> None:
    report_path = output_root / "validation_report.html"
    plots_html = "\n".join(
        f'<div class="plot"><img src="plots/{name}" alt="{name}"><p>{name}</p></div>'
        for name in generated_plots
    )

    summary_rows = {
        "总运行数": int(len(coop_df) + len(non_df) + len(attack_df)),
        "合作运行数": int(len(coop_df)),
        "非合作运行数": int(len(non_df)),
        "非合作打击运行数": int(len(attack_df)),
        "PASS 数": int((pd.concat([coop_df, non_df, attack_df], ignore_index=True)["validation_state"] == "PASS").sum()),
        "WARN 数": int((pd.concat([coop_df, non_df, attack_df], ignore_index=True)["validation_state"] == "WARN").sum()),
        "FAIL 数": int((pd.concat([coop_df, non_df, attack_df], ignore_index=True)["validation_state"] == "FAIL").sum()),
    }

    summary_table = pd.DataFrame(
        [{"指标": key, "值": value} for key, value in summary_rows.items()]
    ).to_html(index=False, classes="table summary")
    coop_table = coop_df.to_html(index=False, classes="table detail", float_format=lambda x: f"{x:.4f}")
    non_table = non_df.to_html(index=False, classes="table detail", float_format=lambda x: f"{x:.4f}")
    attack_table = attack_df.to_html(index=False, classes="table detail", float_format=lambda x: f"{x:.4f}")

    html = f"""<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <title>当前功能仿真验证报告</title>
  <style>
    body {{
      font-family: "Noto Sans CJK JP", "Microsoft YaHei", sans-serif;
      margin: 24px;
      color: #1f2937;
      background: #f8fafc;
    }}
    h1, h2, h3 {{ color: #111827; }}
    .meta {{ margin-bottom: 20px; }}
    .table {{
      width: 100%;
      border-collapse: collapse;
      background: white;
      margin-bottom: 24px;
      font-size: 13px;
    }}
    .table th, .table td {{
      border: 1px solid #d1d5db;
      padding: 6px 8px;
      vertical-align: top;
    }}
    .table th {{
      background: #e5e7eb;
      position: sticky;
      top: 0;
    }}
    .plot {{
      background: white;
      border: 1px solid #d1d5db;
      border-radius: 8px;
      padding: 12px;
      margin-bottom: 16px;
    }}
    .plot img {{
      width: 100%;
      max-width: 1200px;
      display: block;
    }}
    .section {{
      margin-bottom: 32px;
    }}
  </style>
</head>
<body>
  <h1>当前功能仿真验证报告</h1>
  <div class="meta">
    <p>生成时间：{datetime.now().strftime("%Y-%m-%d %H:%M:%S")}</p>
    <p>输出目录：{output_root.relative_to(ROOT)}</p>
  </div>

  <div class="section">
    <h2>总体摘要</h2>
    {summary_table}
  </div>

  <div class="section">
    <h2>图表</h2>
    {plots_html}
  </div>

  <div class="section">
    <h2>合作模式明细</h2>
    {coop_table}
  </div>

  <div class="section">
    <h2>非合作模式明细</h2>
    {non_table}
  </div>

  <div class="section">
    <h2>非合作打击明细</h2>
    {attack_table}
  </div>
</body>
</html>
"""
    report_path.write_text(html, encoding="utf-8")


def write_markdown_summary(output_root: Path, coop_df: pd.DataFrame, non_df: pd.DataFrame, attack_df: pd.DataFrame) -> None:
    md = output_root / "README.md"
    lines = [
        "# 当前功能仿真验证结果",
        "",
        f"- 生成时间: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}",
        f"- 输出目录: `{output_root.relative_to(ROOT)}`",
        "",
        "## 合作模式结果",
        "",
        coop_df.to_csv(index=False),
        "",
        "## 非合作模式结果",
        "",
        non_df.to_csv(index=False),
        "",
        "## 非合作打击结果",
        "",
        attack_df.to_csv(index=False),
        "",
        "## 关键文件",
        "",
        "- `summary/all_runs_summary.csv`",
        "- `summary/cooperative_summary.csv`",
        "- `summary/noncooperative_summary.csv`",
        "- `summary/noncooperative_attack_summary.csv`",
        "- `validation_report.html`",
        "- `plots/`",
    ]
    md.write_text("\n".join(lines), encoding="utf-8")


def persist_summaries(
    output_root: Path,
    all_df: pd.DataFrame,
    coop_df: pd.DataFrame,
    non_df: pd.DataFrame,
    attack_df: pd.DataFrame,
) -> None:
    summary_dir = output_root / "summary"
    summary_dir.mkdir(parents=True, exist_ok=True)
    all_df.to_csv(summary_dir / "all_runs_summary.csv", index=False)
    coop_df.to_csv(summary_dir / "cooperative_summary.csv", index=False)
    non_df.to_csv(summary_dir / "noncooperative_summary.csv", index=False)
    attack_df.to_csv(summary_dir / "noncooperative_attack_summary.csv", index=False)
    (summary_dir / "all_runs_summary.json").write_text(
        all_df.to_json(orient="records", force_ascii=False, indent=2), encoding="utf-8"
    )


def main() -> int:
    parser = argparse.ArgumentParser(description="一键运行当前功能仿真验证矩阵")
    parser.add_argument("--preset", choices=["full", "quick"], default="full")
    parser.add_argument("--duration", type=float, default=60.0, help="单次仿真时长（秒）")
    parser.add_argument("--difficulty", default="Moderate")
    parser.add_argument("--formation", default="v_formation")
    parser.add_argument("--num-uavs", type=int, default=12)
    parser.add_argument("--num-channels", type=int, default=3)
    parser.add_argument("--strategy", default="dynamic")
    parser.add_argument("--skip-build", action="store_true")
    parser.add_argument("--output-root", default="", help="自定义输出目录")
    args = parser.parse_args()

    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output_root = (
        Path(args.output_root).resolve()
        if args.output_root
        else DEFAULT_OUTPUT_BASE / f"current_feature_validation_{timestamp}"
    )
    runs_root = output_root / "runs"
    plots_dir = output_root / "plots"
    runs_root.mkdir(parents=True, exist_ok=True)
    plots_dir.mkdir(parents=True, exist_ok=True)

    specs = build_run_specs(
        duration=args.duration,
        difficulty=args.difficulty,
        formation=args.formation,
        num_uavs=args.num_uavs,
        num_channels=args.num_channels,
        strategy=args.strategy,
        preset=args.preset,
    )

    metadata = {
        "generatedAt": datetime.now().isoformat(),
        "preset": args.preset,
        "duration": args.duration,
        "difficulty": args.difficulty,
        "formation": args.formation,
        "numUAVs": args.num_uavs,
        "numChannels": args.num_channels,
        "strategy": args.strategy,
        "runCount": len(specs),
    }
    (output_root / "run_plan.json").write_text(
        json.dumps({"meta": metadata, "runs": [asdict(spec) for spec in specs]}, ensure_ascii=False, indent=2),
        encoding="utf-8",
    )

    build_ns3(skip_build=args.skip_build, output_root=output_root)

    results: List[Dict] = []
    for spec in specs:
        run_dir, error = run_one_spec(spec, runs_root)
        if spec.operation_mode == "cooperative":
            result = summarize_cooperative_run(spec, run_dir, error)
        elif spec.enable_non_cooperative_attack:
            result = summarize_noncooperative_attack_run(spec, run_dir, error)
        else:
            result = summarize_noncooperative_run(spec, run_dir, error)
        results.append(result)

    all_df = pd.DataFrame(results)
    coop_df = all_df[all_df["category"] == "cooperative"].copy()
    non_df = all_df[all_df["category"] == "non_cooperative"].copy()
    attack_df = all_df[all_df["category"] == "non_cooperative_attack"].copy()

    generated_plots: List[str] = []
    generated_plots.extend(plot_cooperative_summary(coop_df, plots_dir))
    generated_plots.extend(plot_cooperative_impact_summary(coop_df, plots_dir))
    generated_plots.extend(plot_cooperative_delta_heatmap(coop_df, plots_dir))
    generated_plots.extend(plot_cooperative_timelines(coop_df, runs_root, plots_dir))
    generated_plots.extend(plot_scene_realism_profiles(all_df, plots_dir))
    generated_plots.extend(plot_noncooperative_summary(non_df, plots_dir))
    generated_plots.extend(plot_noncooperative_timelines(non_df, plots_dir))
    generated_plots.extend(plot_noncooperative_attack_summary(attack_df, plots_dir))
    generated_plots.extend(plot_noncooperative_algorithm_effects(attack_df, plots_dir))
    generated_plots.extend(plot_noncooperative_attack_timelines(attack_df, plots_dir))

    persist_summaries(output_root, all_df, coop_df, non_df, attack_df)
    write_html_report(output_root, coop_df, non_df, attack_df, generated_plots)
    write_markdown_summary(output_root, coop_df, non_df, attack_df)

    pass_count = int((all_df["validation_state"] == "PASS").sum())
    warn_count = int((all_df["validation_state"] == "WARN").sum())
    fail_count = int((all_df["validation_state"] == "FAIL").sum())

    print("")
    print("========================================")
    print("当前功能仿真验证完成")
    print("========================================")
    print(f"输出目录: {output_root}")
    print(f"总运行数: {len(all_df)}")
    print(f"PASS: {pass_count}  WARN: {warn_count}  FAIL: {fail_count}")
    print(f"HTML 报告: {output_root / 'validation_report.html'}")
    print(f"汇总 CSV: {output_root / 'summary' / 'all_runs_summary.csv'}")
    print(f"图表目录: {plots_dir}")
    print("========================================")

    return 0 if fail_count == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
