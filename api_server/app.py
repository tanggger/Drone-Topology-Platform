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

app = Flask(__name__)
CORS(app) # 允许跨域请求，方便前端独立在另外的端口或服务器上运行访问

# NS-3 的工程根目录 (从当前文件位置动态计算，增强可移植性)
NS3_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

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
        buildings = config.get("buildings", [])
        map_name = config.get("map_name", None)

        # ------------------------------------------------------------------
        # [Cache] 1. 计算配置哈希指纹
        # ------------------------------------------------------------------
        hash_params = {
            "num_drones": num_drones,
            "formation": formation,
            "start": start_pos,
            "target": target_pos,
            "difficulty": difficulty,
            "strategy": strategy,
            "buildings": buildings,
            "map_name": map_name
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
                    print(f"[{task_id}] [CACHE] 从缓存恢复完成: {cache_dir} -> {out_dir_abs}")
                    return
            except Exception as e:
                print(f"[{task_id}] [CACHE] 缓存恢复异常，将重新计算: {e}")

        print(f"[{task_id}] [CACHE MISS] Hash={config_hash}，执行完整仿真...")

        # ------------------------------------------------------------------
        # 1. 建筑物地图文件处理
        # ------------------------------------------------------------------
        if map_name:
            map_file = os.path.join(NS3_DIR, f"data_map/city_{map_name}.txt")
            if not os.path.exists(map_file):
                raise FileNotFoundError(f"Real map file not found: {map_file}")
            print(f"[{task_id}] 使用预先编译的现实世界地图: {map_name}")
        else:
            map_file = os.path.join(NS3_DIR, f"data_map/custom_city_{task_id}.txt")
            os.makedirs(os.path.dirname(map_file), exist_ok=True)
            with open(map_file, 'w') as f:
                f.write("# xMin xMax yMin yMax zMin zMax\n")
                for b in buildings:
                    f.write(f"{b['xMin']} {b['xMax']} {b['yMin']} {b['yMax']} {b['zMin']} {b['zMax']}\n")

        # ------------------------------------------------------------------
        # 2. 调用高级轨迹规划器生成轨迹
        # ------------------------------------------------------------------
        trace_file = os.path.join(NS3_DIR, f"data_rtk/mobility_trace_custom_{task_id}.txt")
        planner_cmd = [
            sys.executable, "rtk/advanced_path_planner.py",
            "--num_drones", str(num_drones),
            "--formation", formation,
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

        shutil_map_cmd = ["cp", map_file, os.path.join(NS3_DIR, "data_map/custom_city.txt")]
        subprocess.run(shutil_map_cmd, cwd=NS3_DIR, check=True)

        ns3_cmd = [
            "./ns3", "run",
            f"uav_resource_allocation --formation=custom --difficulty={difficulty} --strategy={strategy} --outputDir={out_dir_rel}"
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
    out_dir = os.path.join(NS3_DIR, f"output/run_{task_id}")
    status_file = os.path.join(out_dir, "status.json")
    
    if not os.path.exists(status_file):
        return jsonify({"status": "RUNNING"})
        
    with open(status_file, "r") as f:
        status_data = json.load(f)
        
    if status_data.get("status") == "FAILED":
        return jsonify(status_data)
        
    # 如果成功，开始读取 CSV 文件准备返回给前端！
    try:
        results_data = {}
        
        # 1. 轨迹与RTK点位位置
        # 优化: 在后端将 positions 与 resource_detailed 进行合并，方便前端直接使用 Uav.power / Uav.interference
        pos_path = os.path.join(out_dir, "rtk-node-positions.csv")
        res_detailed_path = os.path.join(out_dir, "resource_allocation_detailed.csv")
        
        if os.path.exists(pos_path):
            df_pos = pd.read_csv(pos_path)
            
            # 尝试根据 resource_detailed 丰富 positions
            if os.path.exists(res_detailed_path):
                try:
                    df_res = pd.read_csv(res_detailed_path)
                    # 统一列名以进行合并
                    # resource_detailed usually has: time, node_id, tx_power, interference
                    # positions usually has: time/time_s, nodeId, ...
                    
                    # 1. 处理时间列名对齐 (time vs time_s)
                    if 'time' in df_res.columns and 'time_s' in df_pos.columns:
                        df_res.rename(columns={'time': 'time_s'}, inplace=True)
                        merge_on_time = 'time_s'
                    elif 'time' in df_pos.columns:
                        merge_on_time = 'time'
                    else:
                        merge_on_time = None # 无法确定时间列

                    # 2. 处理节点ID列名对齐 (node_id vs nodeId)
                    if 'node_id' in df_res.columns:
                        df_res.rename(columns={'node_id': 'nodeId'}, inplace=True)
                    
                    if merge_on_time and 'nodeId' in df_pos.columns and 'nodeId' in df_res.columns:
                        # [Critical Fix] 解决浮点数时间不匹配问题
                        # 将两个 DataFrame 的时间列都四舍五入到 3 位小数 (根据 ns-3 常见精度)
                        df_pos['time_merge_key'] = df_pos[merge_on_time].round(3)
                        df_res['time_merge_key'] = df_res[merge_on_time].round(3)
                        
                        print(f"[{task_id}] 数据合并调试: Pos Time Range: {df_pos['time_merge_key'].min()}~{df_pos['time_merge_key'].max()}, Sample: {df_pos['time_merge_key'].iloc[0]}")
                        print(f"[{task_id}] 数据合并调试: Res Time Range: {df_res['time_merge_key'].min()}~{df_res['time_merge_key'].max()}, Sample: {df_res['time_merge_key'].iloc[0]}")

                        # 3. 执行左连接合并 (使用 round 后的 key)
                        merged_df = pd.merge(df_pos, df_res[['time_merge_key', 'nodeId', 'tx_power', 'interference', 'channel', 'data_rate', 'neighbors']], 
                                           on=['time_merge_key', 'nodeId'], 
                                           how='left')
                        
                        # 4. 重命名列以适配前端 (tx_power -> power)
                        if 'tx_power' in merged_df.columns:
                            merged_df.rename(columns={'tx_power': 'power'}, inplace=True)
                        
                        # 清理临时 key
                        if 'time_merge_key' in merged_df.columns:
                            merged_df.drop(columns=['time_merge_key'], inplace=True)
                        df_pos.drop(columns=['time_merge_key'], inplace=True) # 清理原 df_pos 的临时 key

                        # 检查合并效果
                        null_power_count = merged_df['power'].isnull().sum() if 'power' in merged_df.columns else len(merged_df)
                        print(f"[{task_id}] 资源合并报告: 总行数 {len(merged_df)}, 成功匹配 {len(merged_df) - null_power_count} 行, 未匹配 {null_power_count} 行")
                            
                        # 替换原始 df_pos
                        df_pos = merged_df
                        # print(f"[{task_id}] 成功合并资源数据到位置信息 ({len(df_pos)} 行)")
                except Exception as merge_err:
                    print(f"[{task_id}] 资源数据合并失败，忽略: {merge_err}")
                    import traceback
                    traceback.print_exc()

            print(f"[{task_id}] 成功读取位置点: {len(df_pos)}")
            results_data["positions"] = df_pos.to_dict(orient="records")

            
        # 2. 拓扑演化 (图表用)
        topo_evol_path = os.path.join(out_dir, "topology_evolution.csv")
        if os.path.exists(topo_evol_path):
            df = pd.read_csv(topo_evol_path)
            print(f"[{task_id}] 成功读取拓扑周期: {len(df)}")
            results_data["topology_evolution"] = df.to_dict(orient="records")
            
        # 3. QoS 数据 (图表用)
        qos_path = os.path.join(out_dir, "qos_performance.csv")
        if os.path.exists(qos_path):
            results_data["qos"] = pd.read_csv(qos_path).to_dict(orient="records")
            
        # 4. 通信事件 (激光动画用)
        trans_path = os.path.join(out_dir, "rtk-node-transmissions.csv")
        if os.path.exists(trans_path):
            results_data["transmissions"] = pd.read_csv(trans_path).to_dict(orient="records")
            
        # 5. 拓扑连线变化 (3D连线逻辑用)
        topo_changes_path = os.path.join(out_dir, "rtk-topology-changes.txt")
        if os.path.exists(topo_changes_path):
            with open(topo_changes_path, 'r') as f:
                results_data["topology_links"] = [line.strip() for line in f.readlines()]
                
        # 6. 流量统计汇总 (最终面板用)
        flow_stats_path = os.path.join(out_dir, "rtk-flow-stats.csv")
        if os.path.exists(flow_stats_path):
            results_data["flow_summary"] = pd.read_csv(flow_stats_path).to_dict(orient="records")

        # 7. 详细资源分配 (进阶分析用)
        res_detailed_path = os.path.join(out_dir, "resource_allocation_detailed.csv")
        if os.path.exists(res_detailed_path):
            results_data["resource_detailed"] = pd.read_csv(res_detailed_path).to_dict(orient="records")

        # 8. 详细拓扑统计 (网络密度/平均度/链路数 实时态势面板用)
        topo_detailed_path = os.path.join(out_dir, "topology_detailed.csv")
        if os.path.exists(topo_detailed_path):
            results_data["topology_detailed"] = pd.read_csv(topo_detailed_path).to_dict(orient="records")

        return jsonify({
            "status": "SUCCESS",
            "data": sanitize(results_data)
        })
    except Exception as e:
        return jsonify({
            "status": "ERROR",
            "message": f"Failed to read result CSVs: {str(e)}"
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
