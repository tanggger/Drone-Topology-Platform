import os
import subprocess
import json
import uuid
import pandas as pd
import hashlib
import shutil
from flask import Flask, request, jsonify
from flask_cors import CORS
import threading
import math
import sys
import shlex

app = Flask(__name__)
CORS(app) # 允许跨域请求，方便前端独立在另外的端口或服务器上运行访问

# NS-3 的工程根目录 (从当前文件位置动态计算，增强可移植性)
NS3_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

COOPERATIVE_JSON_FILES = {
    "mode_summary": "cooperative_mode_summary.json",
    "failure_timeline": "cooperative_failure_timeline.json",
    "recovery_timeline": "cooperative_recovery_timeline.json",
    "metrics_timeseries": "cooperative_metrics_timeseries.json",
    "dashboard_snapshot": "cooperative_dashboard_snapshot.json",
}

COOPERATIVE_CSV_FILES = {
    "failure_events": "cooperative_failure_events.csv",
    "recovery_actions": "cooperative_recovery_actions.csv",
    "recovery_metrics": "cooperative_recovery_metrics.csv",
    "decision_trace": "cooperative_decision_trace.csv",
}

NON_COOPERATIVE_CSV_FILES = {
    "observed_signal_events": "observed_signal_events.csv",
    "observed_comm_windows": "observed_comm_windows.csv",
    "observed_link_evidence": "observed_link_evidence.csv",
    "inferred_topology_edges": "inferred_topology_edges.csv",
    "inferred_graph_nodes": "inferred_graph_nodes.csv",
    "key_node_candidates": "key_node_candidates.csv",
}

NON_COOPERATIVE_ATTACK_JSON_FILES = {
    "plan": "noncooperative_attack_plan.json",
    "summary": "noncooperative_pre_post_comparison.json",
}

NON_COOPERATIVE_ATTACK_CSV_FILES = {
    "recommendations": "noncooperative_attack_recommendations.csv",
    "events": "noncooperative_attack_events.csv",
    "target_binding": "noncooperative_target_binding.csv",
    "effect_metrics": "noncooperative_attack_effect_metrics.csv",
}

SHARED_DATASET_FILES = {
    "positions": "rtk-node-positions.csv",
    "transmissions": "rtk-node-transmissions.csv",
    "topology_links": "rtk-topology-changes.txt",
    "topology_evolution": "topology_evolution.csv",
    "topology_detailed": "topology_detailed.csv",
    "qos": "qos_performance.csv",
    "flow_summary": "rtk-flow-stats.csv",
    "resource_detailed": "resource_allocation_detailed.csv",
    "environment_summary": "environment_summary.json",
}

def sanitize(obj):
    """递归清洗 NaN/Inf → None，确保 JSON 合法"""
    if isinstance(obj, float):
        if math.isnan(obj) or math.isinf(obj):
            return None
        return obj
    if isinstance(obj, dict):
        return {k: sanitize(v) for k, v in obj.items()}
    if isinstance(obj, list):
        return [sanitize(v) for v in obj]
    return obj

def parse_bool(value, default=False):
    if value is None:
        return default
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)):
        return value != 0
    if isinstance(value, str):
        return value.strip().lower() in {"1", "true", "yes", "on"}
    return default

def safe_read_json(path, default=None):
    if not os.path.exists(path):
        return default
    with open(path, "r", encoding="utf-8") as f:
        return sanitize(json.load(f))

def safe_read_csv(path):
    if not os.path.exists(path):
        return None
    return sanitize(pd.read_csv(path).to_dict(orient="records"))

def safe_read_text_lines(path):
    if not os.path.exists(path):
        return None
    with open(path, "r", encoding="utf-8") as f:
        return [line.strip() for line in f.readlines()]

def build_task_request_metadata(task_id, raw_config, derived_config):
    return {
        "task_id": task_id,
        "raw_request": sanitize(raw_config),
        "derived_config": sanitize(derived_config),
    }

def write_task_request_metadata(out_dir_abs, task_id, raw_config, derived_config):
    os.makedirs(out_dir_abs, exist_ok=True)
    metadata = build_task_request_metadata(task_id, raw_config, derived_config)
    with open(os.path.join(out_dir_abs, "api_request.json"), "w", encoding="utf-8") as f:
        json.dump(metadata, f, indent=2, ensure_ascii=False)

def resolve_map_file(task_id, config):
    map_name = config.get("map_name")
    explicit_map_file = config.get("map_file") or config.get("mapFile")
    buildings = config.get("buildings", [])

    if map_name:
        candidate = os.path.join(NS3_DIR, f"data_map/city_{map_name}.txt")
        if not os.path.exists(candidate):
            raise FileNotFoundError(f"Real map file not found: {candidate}")
        return candidate

    if explicit_map_file:
        candidate = explicit_map_file
        if not os.path.isabs(candidate):
            candidate = os.path.join(NS3_DIR, candidate)
        if not os.path.exists(candidate):
            raise FileNotFoundError(f"Configured map file not found: {candidate}")
        return candidate

    map_file = os.path.join(NS3_DIR, f"data_map/custom_city_{task_id}.txt")
    os.makedirs(os.path.dirname(map_file), exist_ok=True)
    with open(map_file, "w", encoding="utf-8") as f:
        f.write("# xMin xMax yMin yMax zMin zMax\n")
        for b in buildings:
            f.write(f"{b['xMin']} {b['xMax']} {b['yMin']} {b['yMax']} {b['zMin']} {b['zMax']}\n")
    return map_file

def build_ns3_arg_string(base_args):
    return " ".join(shlex.quote(str(token)) for token in base_args if token not in (None, ""))

def load_shared_results(out_dir, task_id):
    results_data = {}

    pos_path = os.path.join(out_dir, SHARED_DATASET_FILES["positions"])
    res_detailed_path = os.path.join(out_dir, SHARED_DATASET_FILES["resource_detailed"])

    if os.path.exists(pos_path):
        df_pos = pd.read_csv(pos_path)

        if os.path.exists(res_detailed_path):
            try:
                df_res = pd.read_csv(res_detailed_path)
                if "time" in df_res.columns and "time_s" in df_pos.columns:
                    df_res.rename(columns={"time": "time_s"}, inplace=True)
                    merge_on_time = "time_s"
                elif "time" in df_pos.columns:
                    merge_on_time = "time"
                else:
                    merge_on_time = None

                if "node_id" in df_res.columns:
                    df_res.rename(columns={"node_id": "nodeId"}, inplace=True)

                if merge_on_time and "nodeId" in df_pos.columns and "nodeId" in df_res.columns:
                    df_pos["time_merge_key"] = df_pos[merge_on_time].round(3)
                    df_res["time_merge_key"] = df_res[merge_on_time].round(3)

                    if "interference_dBm" in df_res.columns and "interference" not in df_res.columns:
                        df_res.rename(columns={"interference_dBm": "interference"}, inplace=True)

                    merge_cols = ["time_merge_key", "nodeId", "tx_power", "channel", "data_rate", "neighbors"]
                    if "interference" in df_res.columns:
                        merge_cols.append("interference")
                    if "worst_sinr_dB" in df_res.columns:
                        merge_cols.append("worst_sinr_dB")

                    merged_df = pd.merge(
                        df_pos,
                        df_res[merge_cols],
                        on=["time_merge_key", "nodeId"],
                        how="left",
                    )

                    if "tx_power" in merged_df.columns:
                        merged_df.rename(columns={"tx_power": "power"}, inplace=True)
                    if "worst_sinr_dB" in merged_df.columns:
                        merged_df.rename(columns={"worst_sinr_dB": "sinr"}, inplace=True)

                    if "time_merge_key" in merged_df.columns:
                        merged_df.drop(columns=["time_merge_key"], inplace=True)
                    if "time_merge_key" in df_pos.columns:
                        df_pos.drop(columns=["time_merge_key"], inplace=True)
                    df_pos = merged_df
            except Exception as merge_err:
                print(f"[{task_id}] 资源数据合并失败，忽略: {merge_err}")

        results_data["positions"] = sanitize(df_pos.to_dict(orient="records"))

    topology_evolution = safe_read_csv(os.path.join(out_dir, SHARED_DATASET_FILES["topology_evolution"]))
    if topology_evolution is not None:
        results_data["topology_evolution"] = topology_evolution

    qos = safe_read_csv(os.path.join(out_dir, SHARED_DATASET_FILES["qos"]))
    if qos is not None:
        results_data["qos"] = qos

    transmissions = safe_read_csv(os.path.join(out_dir, SHARED_DATASET_FILES["transmissions"]))
    if transmissions is not None:
        results_data["transmissions"] = transmissions

    topology_links = safe_read_text_lines(os.path.join(out_dir, SHARED_DATASET_FILES["topology_links"]))
    if topology_links is not None:
        results_data["topology_links"] = topology_links

    flow_summary = safe_read_csv(os.path.join(out_dir, SHARED_DATASET_FILES["flow_summary"]))
    if flow_summary is not None:
        results_data["flow_summary"] = flow_summary

    resource_detailed = safe_read_csv(os.path.join(out_dir, SHARED_DATASET_FILES["resource_detailed"]))
    if resource_detailed is not None:
        results_data["resource_detailed"] = resource_detailed

    topology_detailed = safe_read_csv(os.path.join(out_dir, SHARED_DATASET_FILES["topology_detailed"]))
    if topology_detailed is not None:
        results_data["topology_detailed"] = topology_detailed

    environment_summary = safe_read_json(os.path.join(out_dir, SHARED_DATASET_FILES["environment_summary"]))
    if environment_summary is not None:
        results_data["environment_summary"] = environment_summary

    request_metadata = safe_read_json(os.path.join(out_dir, "api_request.json"))
    if request_metadata is not None:
        results_data["request_metadata"] = request_metadata

    return results_data

def load_cooperative_results(out_dir):
    data = {}
    for key, filename in COOPERATIVE_JSON_FILES.items():
        content = safe_read_json(os.path.join(out_dir, filename))
        if content is not None:
            data[key] = content
    for key, filename in COOPERATIVE_CSV_FILES.items():
        content = safe_read_csv(os.path.join(out_dir, filename))
        if content is not None:
            data[key] = content
    return data

def load_non_cooperative_results(out_dir):
    observation_inference = {}
    for key, filename in NON_COOPERATIVE_CSV_FILES.items():
        content = safe_read_csv(os.path.join(out_dir, filename))
        if content is not None:
            observation_inference[key] = content

    attack = {}
    for key, filename in NON_COOPERATIVE_ATTACK_JSON_FILES.items():
        content = safe_read_json(os.path.join(out_dir, filename))
        if content is not None:
            attack[key] = content
    for key, filename in NON_COOPERATIVE_ATTACK_CSV_FILES.items():
        content = safe_read_csv(os.path.join(out_dir, filename))
        if content is not None:
            attack[key] = content

    data = {}
    if observation_inference:
        data["observation_inference"] = observation_inference
        data.update(observation_inference)
    if attack:
        data["attack"] = attack
        data["attack_datasets"] = sorted(attack.keys())
    return data

def build_results_manifest(task_id, out_dir, shared_results, cooperative_results, non_cooperative_results):
    environment_summary = shared_results.get("environment_summary", {}) or {}
    operation_mode = environment_summary.get("operationMode") or "unknown"
    attack_results = (
        non_cooperative_results.get("attack", {})
        if isinstance(non_cooperative_results, dict)
        else {}
    )
    attack_plan = attack_results.get("plan", {}) if isinstance(attack_results, dict) else {}
    manifest = {
        "taskId": task_id,
        "outputDir": os.path.relpath(out_dir, NS3_DIR),
        "operationMode": operation_mode,
        "sceneType": environment_summary.get("sceneType"),
        "difficulty": environment_summary.get("difficulty"),
        "communicationMode": environment_summary.get("communicationMode"),
        "formation": environment_summary.get("formationName"),
        "sharedDatasets": sorted(shared_results.keys()),
        "cooperativeDatasets": sorted(cooperative_results.keys()),
        "nonCooperativeDatasets": sorted(non_cooperative_results.keys()),
        "nonCooperativeAttackDatasets": sorted(attack_results.keys()) if attack_results else [],
        "hasNonCooperativeAttack": bool(attack_results),
        "attackType": attack_plan.get("attackType"),
        "attackExecuted": attack_plan.get("executedEntityNodeId", -1) is not None
        and attack_plan.get("executedEntityNodeId", -1) >= 0,
        "availableFiles": sorted(os.listdir(out_dir)) if os.path.isdir(out_dir) else [],
    }
    return sanitize(manifest)

def load_all_results(task_id):
    out_dir = os.path.join(NS3_DIR, f"output/run_{task_id}")
    status_file = os.path.join(out_dir, "status.json")

    if not os.path.exists(status_file):
        return {"status": "RUNNING"}

    with open(status_file, "r", encoding="utf-8") as f:
        status_data = json.load(f)

    if status_data.get("status") == "FAILED":
        return sanitize(status_data)

    shared_results = load_shared_results(out_dir, task_id)
    cooperative_results = load_cooperative_results(out_dir)
    non_cooperative_results = load_non_cooperative_results(out_dir)
    manifest = build_results_manifest(
        task_id,
        out_dir,
        shared_results,
        cooperative_results,
        non_cooperative_results,
    )

    environment_summary = shared_results.get("environment_summary", {}) or {}
    non_cooperative_frontend = None
    if non_cooperative_results:
        non_cooperative_frontend = {
            "observation_inference": non_cooperative_results.get("observation_inference", {}),
            "attack": non_cooperative_results.get("attack"),
        }

    frontend_payload = {
        "meta": {
            "taskId": task_id,
            "operationMode": environment_summary.get("operationMode"),
            "sceneType": environment_summary.get("sceneType"),
            "difficulty": environment_summary.get("difficulty"),
            "communicationMode": environment_summary.get("communicationMode"),
            "formation": environment_summary.get("formationName"),
        },
        "shared": {
            key: shared_results.get(key)
            for key in [
                "environment_summary",
                "request_metadata",
                "positions",
                "transmissions",
                "topology_links",
                "topology_evolution",
                "topology_detailed",
                "resource_detailed",
                "qos",
                "flow_summary",
            ]
            if key in shared_results
        },
        "cooperative": cooperative_results or None,
        "non_cooperative": non_cooperative_frontend,
        "manifest": manifest,
    }

    legacy_data = dict(shared_results)
    legacy_data["frontend_manifest"] = manifest
    if cooperative_results:
        legacy_data["cooperative"] = cooperative_results
    if non_cooperative_results:
        legacy_data["non_cooperative"] = non_cooperative_results
    legacy_data["frontend"] = frontend_payload

    return {
        "status": "SUCCESS",
        "data": sanitize(legacy_data),
        "frontend": sanitize(frontend_payload),
        "manifest": manifest,
    }

def run_simulation_task(task_id, config):
    # 所有路径变量在函数开头统一定义，避免重复
    out_dir_rel = f"output/run_{task_id}"
    out_dir_abs = os.path.join(NS3_DIR, out_dir_rel)
    cache_root = os.path.join(NS3_DIR, "output", "cache")
    cache_dir = None  # 待计算 hash 后赋值
    config_hash = None

    try:
        print(f"[{task_id}] 开始处理仿真任务...")

        # 解析前端配置
        num_drones = config.get("num_drones", 15)
        formation = config.get("formation", "v_formation")
        start_pos = config.get("start", "0,0,30")
        target_pos = config.get("target", "0,600,30")
        difficulty = config.get("difficulty", "Easy")
        strategy = config.get("strategy", "dynamic")
        operation_mode = config.get("operationMode", "cooperative")
        communication_mode = config.get("communicationMode", "centralized")
        scene_type = config.get("sceneType", "")
        formation_spacing = float(config.get("formation_spacing", 12.0))
        map_name = config.get("map_name", None)
        leader_node_id = int(config.get("leaderNodeId", 0))
        backup_leader_list = config.get("backupLeaderList", "")
        distributed_hop_limit = int(config.get("distributedHopLimit", 1))
        cooperative_failure_type = config.get("cooperativeFailureType", "node_failure")
        failure_target_id = int(config.get("failureTargetId", -1))
        failure_start_time = float(config.get("failureStartTime", -1.0))
        failure_duration = float(config.get("failureDuration", -1.0))
        recovery_policy = config.get("recoveryPolicy", "global_recovery")
        recovery_objective = config.get("recoveryObjective", "connectivity")
        recovery_cooldown = float(config.get("recoveryCooldown", 1.0))
        allow_channel_reallocation = parse_bool(config.get("allowChannelReallocation"), True)
        allow_power_adjustment = parse_bool(config.get("allowPowerAdjustment"), True)
        allow_rate_adjustment = parse_bool(config.get("allowRateAdjustment"), True)
        allow_relay_reselection = parse_bool(config.get("allowRelayReselection"), True)
        allow_slot_reallocation = parse_bool(config.get("allowSlotReallocation"), True)
        allow_route_rebuild = parse_bool(config.get("allowRouteRebuild"), True)
        enable_non_cooperative_attack = parse_bool(config.get("enableNonCooperativeAttack"), False)
        attack_type = config.get("attackType", "node_strike")
        manual_strike_target = int(config.get("manualStrikeTarget", -1))
        attack_execute_time = float(config.get("attackExecuteTime", -1.0))
        attack_evaluation_duration = float(config.get("attackEvaluationDuration", 12.0))
        attack_neighborhood_hop = int(config.get("attackNeighborhoodHop", 1))

        # Custom 参数提取
        custom_params = {}
        if difficulty == "Custom":
            # 提取所有可能自定义的参数，设定默认值与 Easy 模式一致或自行定义
            custom_params = {
                "pathLossExp": float(config.get("pathLossExp", 2.0)),
                "rxSens": float(config.get("rxSens", -90.0)),
                "txPower": float(config.get("txPower", 23.0)),
                "nakagamiM": float(config.get("nakagamiM", 0.0)),
                "macRetries": int(config.get("macRetries", 7)),
                "noiseFigure": float(config.get("noiseFigure", 7.0)),
                "rtkNoise": float(config.get("rtkNoise", 0.01)),
                "rtkDriftMag": float(config.get("rtkDriftMag", 0.0)),
                "rtkDriftInt": float(config.get("rtkDriftInt", 0.0)),
                "rtkDriftDur": float(config.get("rtkDriftDur", 0.0)),
                "trafficLoad": float(config.get("trafficLoad", 0.2)),
                "numInterfere": int(config.get("numInterfere", 0)),
                "interfereRate": float(config.get("interfereRate", 0.5)),
                "interfereDuty": float(config.get("interfereDuty", 0.1)),
            }

        # ------------------------------------------------------------------
        # [Cache] 1. 计算配置哈希指纹
        # ------------------------------------------------------------------
        hash_params = {
            "num_drones": num_drones,
            "formation": formation,
            "formation_spacing": formation_spacing,
            "start": start_pos,
            "target": target_pos,
            "difficulty": difficulty,
            "strategy": strategy,
            "operationMode": operation_mode,
            "communicationMode": communication_mode,
            "sceneType": scene_type,
            "map_name": map_name,
            "map_file": config.get("map_file") or config.get("mapFile"),
            "buildings": config.get("buildings", []),
            "leaderNodeId": leader_node_id,
            "backupLeaderList": backup_leader_list,
            "distributedHopLimit": distributed_hop_limit,
            "cooperativeFailureType": cooperative_failure_type,
            "failureTargetId": failure_target_id,
            "failureStartTime": failure_start_time,
            "failureDuration": failure_duration,
            "recoveryPolicy": recovery_policy,
            "recoveryObjective": recovery_objective,
            "recoveryCooldown": recovery_cooldown,
            "allowChannelReallocation": allow_channel_reallocation,
            "allowPowerAdjustment": allow_power_adjustment,
            "allowRateAdjustment": allow_rate_adjustment,
            "allowRelayReselection": allow_relay_reselection,
            "allowSlotReallocation": allow_slot_reallocation,
            "allowRouteRebuild": allow_route_rebuild,
            "enableNonCooperativeAttack": enable_non_cooperative_attack,
            "attackType": attack_type,
            "manualStrikeTarget": manual_strike_target,
            "attackExecuteTime": attack_execute_time,
            "attackEvaluationDuration": attack_evaluation_duration,
            "attackNeighborhoodHop": attack_neighborhood_hop,
            "custom_params": custom_params  # 将 Custom 参数加入哈希计算
        }
        param_str = json.dumps(hash_params, sort_keys=True)
        config_hash = hashlib.md5(param_str.encode('utf-8')).hexdigest()
        cache_dir = os.path.join(cache_root, config_hash)

        print(f"[{task_id}] 配置哈希: {config_hash}")

        # ------------------------------------------------------------------
        # [Cache] 2. 检查缓存命中
        # ------------------------------------------------------------------
        os.makedirs(cache_root, exist_ok=True)

        cache_status_file = os.path.join(cache_dir, "status.json")
        if os.path.isdir(cache_dir) and os.path.isfile(cache_status_file):
            # 验证缓存状态确实是 SUCCESS
            try:
                with open(cache_status_file, "r") as f:
                    cached_status = json.load(f)
                if cached_status.get("status") == "SUCCESS":
                    print(f"[{task_id}] [CACHE HIT] Hash={config_hash}")
                    if os.path.exists(out_dir_abs):
                        shutil.rmtree(out_dir_abs)
                    shutil.copytree(cache_dir, out_dir_abs)
                    write_task_request_metadata(out_dir_abs, task_id, config, hash_params)
                    print(f"[{task_id}] [CACHE] 从缓存恢复完成: {cache_dir} -> {out_dir_abs}")
                    return
            except Exception as e:
                print(f"[{task_id}] [CACHE] 缓存恢复异常，将重新计算: {e}")

        print(f"[{task_id}] [CACHE MISS] Hash={config_hash}，执行完整仿真...")

        # ------------------------------------------------------------------
        # 1. 建筑物地图文件处理
        # ------------------------------------------------------------------
        map_file = resolve_map_file(task_id, config)
        if map_name:
            print(f"[{task_id}] 使用预先编译的现实世界地图: {map_name}")

        # ------------------------------------------------------------------
        # 2. 调用高级轨迹规划器生成轨迹
        # ------------------------------------------------------------------
        trace_file = os.path.join(NS3_DIR, f"data_rtk/mobility_trace_custom_{task_id}.txt")
        planner_cmd = [
            sys.executable, "rtk/advanced_path_planner.py",
            "--num_drones", str(num_drones),
            "--formation", formation,
            "--spacing", str(formation_spacing),
            f"--start={start_pos}",    # 使用=连接，防止负数坐标被误判为参数flag
            f"--target={target_pos}",  # 使用=连接，防止负数坐标被误判为参数flag
            "--map", map_file,
            "--output", trace_file
        ]

        print(f"[{task_id}] 生成航线: {' '.join(planner_cmd)}")
        subprocess.run(planner_cmd, cwd=NS3_DIR, check=True)

        # ------------------------------------------------------------------
        # 3. 运行 NS-3 底层核心
        # ------------------------------------------------------------------
        os.makedirs(out_dir_abs, exist_ok=True)
        write_task_request_metadata(out_dir_abs, task_id, config, hash_params)

        shutil_map_cmd = ["cp", map_file, os.path.join(NS3_DIR, "data_map/custom_city.txt")]
        subprocess.run(shutil_map_cmd, cwd=NS3_DIR, check=True)

        ns3_arg_tokens = [
            "uav_resource_allocation",
            "--formation=custom",
            f"--difficulty={difficulty}",
            f"--strategy={strategy}",
            f"--outputDir={out_dir_rel}",
            f"--operationMode={operation_mode}",
            f"--sceneType={scene_type}" if scene_type else None,
            f"--mapFile={os.path.relpath(map_file, NS3_DIR)}" if map_file else None,
        ]

        if operation_mode == "cooperative":
            ns3_arg_tokens.extend([
                f"--communicationMode={communication_mode}",
                f"--leaderNodeId={leader_node_id}",
                f"--backupLeaderList={backup_leader_list}" if backup_leader_list else None,
                f"--distributedHopLimit={distributed_hop_limit}",
                f"--cooperativeFailureType={cooperative_failure_type}",
                f"--failureTargetId={failure_target_id}",
                f"--failureStartTime={failure_start_time}",
                f"--failureDuration={failure_duration}",
                f"--recoveryPolicy={recovery_policy}",
                f"--recoveryObjective={recovery_objective}",
                f"--recoveryCooldown={recovery_cooldown}",
                f"--allowChannelReallocation={'true' if allow_channel_reallocation else 'false'}",
                f"--allowPowerAdjustment={'true' if allow_power_adjustment else 'false'}",
                f"--allowRateAdjustment={'true' if allow_rate_adjustment else 'false'}",
                f"--allowRelayReselection={'true' if allow_relay_reselection else 'false'}",
                f"--allowSlotReallocation={'true' if allow_slot_reallocation else 'false'}",
                f"--allowRouteRebuild={'true' if allow_route_rebuild else 'false'}",
            ])
        elif operation_mode == "non_cooperative":
            ns3_arg_tokens.extend([
                f"--enableNonCooperativeAttack={'true' if enable_non_cooperative_attack else 'false'}",
                f"--attackType={attack_type}",
                f"--manualStrikeTarget={manual_strike_target}",
                f"--attackExecuteTime={attack_execute_time}",
                f"--attackEvaluationDuration={attack_evaluation_duration}",
                f"--attackNeighborhoodHop={attack_neighborhood_hop}",
            ])

        ns3_args = build_ns3_arg_string(ns3_arg_tokens)

        if difficulty == "Custom":
            # 只有 Custom 模式才追加详细参数
            ns3_args += " " + build_ns3_arg_string([
                f"--pathLossExp={custom_params['pathLossExp']}",
                f"--rxSens={custom_params['rxSens']}",
                f"--txPower={custom_params['txPower']}",
                f"--nakagamiM={custom_params['nakagamiM']}",
                f"--macRetries={custom_params['macRetries']}",
                f"--noiseFigure={custom_params['noiseFigure']}",
                f"--rtkNoise={custom_params['rtkNoise']}",
                f"--rtkDriftMag={custom_params['rtkDriftMag']}",
                f"--rtkDriftInt={custom_params['rtkDriftInt']}",
                f"--rtkDriftDur={custom_params['rtkDriftDur']}",
                f"--trafficLoad={custom_params['trafficLoad']}",
                f"--numInterfere={custom_params['numInterfere']}",
                f"--interfereRate={custom_params['interfereRate']}",
                f"--interfereDuty={custom_params['interfereDuty']}",
            ])

        ns3_cmd = [
            "./ns3", "run",
            ns3_args
        ]
        shutil_cmd = ["cp", trace_file, os.path.join(NS3_DIR, "data_rtk/mobility_trace_custom.txt")]
        subprocess.run(shutil_cmd, cwd=NS3_DIR, check=True)

        print(f"[{task_id}] 启动NS-3: {' '.join(ns3_cmd)}")
        # 移除 stdout=subprocess.DEVNULL 以便在此终端看到 NS-3 进度条
        subprocess.run(ns3_cmd, cwd=NS3_DIR, check=True)

        # ------------------------------------------------------------------
        # 4. 执行分析脚本
        # ------------------------------------------------------------------
        analyze_cmd = [
            sys.executable, "analyze_resource_allocation.py", out_dir_rel, "--all"
        ]
        print(f"[{task_id}] 启动数据分析: {' '.join(analyze_cmd)}")
        subprocess.run(analyze_cmd, cwd=NS3_DIR, check=True)

        print(f"[{task_id}] 仿真流水线全部执行完毕!")

        # ------------------------------------------------------------------
        # 5. 写入成功状态
        # ------------------------------------------------------------------
        with open(os.path.join(out_dir_abs, "status.json"), "w") as f:
            json.dump({"status": "SUCCESS"}, f)

        # ------------------------------------------------------------------
        # [Cache] 3. 将成功的仿真结果写入缓存
        # ------------------------------------------------------------------
        print(f"[{task_id}] [CACHE WRITE] 开始缓存仿真结果... Hash={config_hash}")
        try:
            # 确保缓存根目录存在 (防御性: NS-3运行期间可能被意外删除)
            os.makedirs(cache_root, exist_ok=True)

            if os.path.exists(cache_dir):
                shutil.rmtree(cache_dir)

            shutil.copytree(out_dir_abs, cache_dir)

            # 写入缓存元信息，便于调试
            meta_path = os.path.join(cache_dir, "_cache_meta.json")
            with open(meta_path, "w") as mf:
                json.dump({
                    "config_hash": config_hash,
                    "params": hash_params,
                    "source_task_id": task_id,
                }, mf, indent=2, ensure_ascii=False)

            # 验证缓存是否真正写入
            if os.path.isfile(os.path.join(cache_dir, "status.json")):
                print(f"[{task_id}] [CACHE WRITE] 缓存写入成功: {cache_dir}")
            else:
                print(f"[{task_id}] [CACHE WRITE] 警告: copytree 完成但 status.json 不在缓存中!")
        except Exception as cache_err:
            print(f"[{task_id}] [CACHE WRITE] 缓存写入失败 (不影响本次任务): {cache_err}")
            import traceback
            traceback.print_exc()

    except Exception as e:
        print(f"[{task_id}] 仿真任务失败: {e}")
        import traceback
        traceback.print_exc()
        os.makedirs(out_dir_abs, exist_ok=True)
        with open(os.path.join(out_dir_abs, "status.json"), "w") as f:
            json.dump({"status": "FAILED", "error": str(e)}, f)

@app.route('/api/simulate', methods=['POST'])
def start_simulation():
    """
    接收前端配置，并异步触发仿真任务
    """
    data = request.json
    task_id = str(uuid.uuid4())[:8] # 短UUID
    
    # 使用新线程异步剥离仿真过程，避免阻塞前端请求
    thread = threading.Thread(target=run_simulation_task, args=(task_id, data))
    thread.start()
    
    return jsonify({
        "message": "Simulation triggered successfully",
        "task_id": task_id,
        "status": "RUNNING"
    })

@app.route('/api/results/<task_id>', methods=['GET'])
def get_results(task_id):
    """
    前端通过轮询此接口，获取任务状态和仿真产生的 CSV 数据
    """
    try:
        results = load_all_results(task_id)
        return jsonify(results)
    except Exception as e:
        return jsonify({
            "status": "ERROR",
            "message": f"Failed to read result files: {str(e)}"
        })

@app.route('/api/results/<task_id>/frontend', methods=['GET'])
def get_frontend_results(task_id):
    try:
        results = load_all_results(task_id)
        if results.get("status") != "SUCCESS":
            return jsonify(results)
        return jsonify({
            "status": "SUCCESS",
            "data": results["frontend"]
        })
    except Exception as e:
        return jsonify({
            "status": "ERROR",
            "message": f"Failed to build frontend payload: {str(e)}"
        })

@app.route('/api/results/<task_id>/manifest', methods=['GET'])
def get_results_manifest(task_id):
    try:
        results = load_all_results(task_id)
        if results.get("status") != "SUCCESS":
            return jsonify(results)
        return jsonify({
            "status": "SUCCESS",
            "data": results["manifest"]
        })
    except Exception as e:
        return jsonify({
            "status": "ERROR",
            "message": f"Failed to build manifest: {str(e)}"
        })

@app.route('/api/map_data/<map_name>', methods=['GET'])
def get_map_data(map_name):
    """
    提供给前端用于渲染 3D 城市沙盘的建筑物渲染 JSON 文件
    """
    json_path = os.path.join(NS3_DIR, "api_server", "static", f"{map_name}_buildings.json")
    if not os.path.exists(json_path):
        return jsonify({
            "status": "ERROR",
            "message": f"Map data not found for {map_name}"
        }), 404
        
    with open(json_path, 'r', encoding='utf-8') as f:
        data = json.load(f)
        
    return jsonify({
        "status": "SUCCESS",
        "data": data
    })

@app.route('/api/upload_osm', methods=['POST'])
def upload_osm():
    """上传 OSM 文件并解析"""
    if 'file' not in request.files:
         return jsonify({"status": "ERROR", "message": "No file part"}), 400
    file = request.files['file']
    if file.filename == '':
        return jsonify({"status": "ERROR", "message": "No selected file"}), 400
    
    if file:
        map_name = request.form.get("map_name", "custom_uploaded_map")
        
        osm_upload_dir = os.path.join(NS3_DIR, "data_map", "osm")
        os.makedirs(osm_upload_dir, exist_ok=True)
        
        osm_path = os.path.join(osm_upload_dir, f"{map_name}.osm")
        file.save(osm_path)
        
        # 解析它
        import sys
        sys.path.append(NS3_DIR)
        from osm_to_simulation import convert_osm_to_simulation_map
        
        output_txt = os.path.join(NS3_DIR, f"data_map/city_{map_name}.txt")
        output_json = os.path.join(NS3_DIR, f"api_server/static/{map_name}_buildings.json")
        success, w, h = convert_osm_to_simulation_map(osm_path, output_txt, output_json)
        
        if success:
            with open(output_json, 'r') as f:
                json_data = json.load(f)
            return jsonify({
                "status": "SUCCESS", 
                "map_name": map_name,
                "data": json_data
            })
        else:
             return jsonify({
                "status": "ERROR", 
                "message": "Failed to parse OSM file: No buildings found."
            }), 400

@app.route('/api/health', methods=['GET'])
def health_check():
    return jsonify({"status": "OK", "message": "Wing-Net Omni Backend Server is running."})

@app.route('/api/maps', methods=['GET'])
def list_maps():
    """
    列出所有可用的已导入地图
    返回格式:
    {
        "status": "SUCCESS",
        "maps": ["map_name1", "map_name2", ...]
    }
    """
    static_dir = os.path.join(NS3_DIR, "api_server", "static")
    data_map_dir = os.path.join(NS3_DIR, "data_map")
    
    available_maps = []
    
    try:
        # 扫描 static 目录下所有的 *_buildings.json 文件
        if os.path.exists(static_dir):
            for filename in os.listdir(static_dir):
                if filename.endswith("_buildings.json"):
                    # 提取 map_name (去掉后缀 _buildings.json)
                    map_name = filename[:-15] 
                    
                    # 检查对应的后端仿真地图文件是否存在 (data_map/city_{map_name}.txt)
                    txt_path = os.path.join(data_map_dir, f"city_{map_name}.txt")
                    
                    if os.path.exists(txt_path):
                        available_maps.append(map_name)
                        
        return jsonify({
            "status": "SUCCESS",
            "maps": sorted(available_maps)
        })
    except Exception as e:
        return jsonify({
            "status": "ERROR",
            "message": f"Failed to list maps: {str(e)}"
        })

if __name__ == '__main__':
    # 开发环境下运行于 5000 端口，全网段可访问（0.0.0.0）
    app.run(host='0.0.0.0', port=5000, debug=False)
