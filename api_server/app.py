import os
import subprocess
import json
import uuid
import pandas as pd
from flask import Flask, request, jsonify
from flask_cors import CORS
import threading
import sys

app = Flask(__name__)
CORS(app) # 允许跨域请求，方便前端独立在另外的端口或服务器上运行访问

# NS-3 的工程根目录 (硬编码到本机器绝对路径或从相对路径计算)
NS3_DIR = "/home/tzx/ns-3.43"

def run_simulation_task(task_id, config):
    try:
        print(f"[{task_id}] 开始处理仿真任务...")
        
        # 解析前端配置
        num_drones = config.get("num_drones", 15)
        formation = config.get("formation", "v_formation")
        start_pos = config.get("start", "0,0,30")
        target_pos = config.get("target", "0,600,30")
        difficulty = config.get("difficulty", "Easy")
        strategy = config.get("strategy", "dynamic")
        
        # 1. 动态生成建筑物地图文件
        buildings = config.get("buildings", [])
        map_file = os.path.join(NS3_DIR, f"data_map/custom_city_{task_id}.txt")
        os.makedirs(os.path.dirname(map_file), exist_ok=True)
        with open(map_file, 'w') as f:
            f.write("# xMin xMax yMin yMax zMin zMax\n")
            for b in buildings:
                f.write(f"{b['xMin']} {b['xMax']} {b['yMin']} {b['yMax']} {b['zMin']} {b['zMax']}\n")
                
        # 2. 调用高级轨迹规划器生成轨迹
        trace_file = os.path.join(NS3_DIR, f"data_rtk/mobility_trace_custom_{task_id}.txt")
        planner_cmd = [
            sys.executable, "rtk/advanced_path_planner.py",
            "--num_drones", str(num_drones),
            "--formation", formation,
            "--start", start_pos,
            "--target", target_pos,
            "--map", map_file,
            "--output", trace_file
        ]
        
        print(f"[{task_id}] 生成航线: {' '.join(planner_cmd)}")
        subprocess.run(planner_cmd, cwd=NS3_DIR, check=True)
        
        # 3. 运行 NS-3 底层核心
        out_dir = f"output/run_{task_id}"
        os.makedirs(os.path.join(NS3_DIR, out_dir), exist_ok=True)
        
        ns3_cmd = [
            "./ns3", "run", 
            f"uav_resource_allocation --formation=custom --difficulty={difficulty} --strategy={strategy} --outputDir={out_dir}"
        ]
        # 修改 ns-3 调用以使用刚刚生成的特定轨迹文件 (注：由于我们在 C++ 中硬编码了 custom 读取 custom.txt，为了支持多任务并发，我们临时覆盖它。不过这里先保证基础功能运行，使用我们生成的轨迹文件覆盖标准路径下的临时文件)
        shutil_cmd = ["cp", trace_file, os.path.join(NS3_DIR, "data_rtk/mobility_trace_custom.txt")]
        subprocess.run(shutil_cmd, cwd=NS3_DIR, check=True)
        
        print(f"[{task_id}] 启动NS-3: {' '.join(ns3_cmd)}")
        subprocess.run(ns3_cmd, cwd=NS3_DIR, check=True, stdout=subprocess.DEVNULL) # 可以把 stdout 输出重定向以避免控制台污染
        
        # 4. 执行分析脚本
        analyze_cmd = [
            sys.executable, "analyze_resource_allocation.py", out_dir, "--all"
        ]
        print(f"[{task_id}] 启动数据分析: {' '.join(analyze_cmd)}")
        subprocess.run(analyze_cmd, cwd=NS3_DIR, check=True)
        
        print(f"[{task_id}] 仿真流水线全部执行完毕！")
        
        # 记录任务状态为成功
        with open(os.path.join(NS3_DIR, out_dir, "status.json"), "w") as f:
            json.dump({"status": "SUCCESS"}, f)
            
    except Exception as e:
        print(f"[{task_id}] 仿真任务失败: {e}")
        # 如果失败，写入失败状态
        out_dir = f"output/run_{task_id}"
        os.makedirs(os.path.join(NS3_DIR, out_dir), exist_ok=True)
        with open(os.path.join(NS3_DIR, out_dir, "status.json"), "w") as f:
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
        pos_path = os.path.join(out_dir, "rtk-node-positions.csv")
        if os.path.exists(pos_path):
            df = pd.read_csv(pos_path)
            print(f"[{task_id}] 成功读取位置点: {len(df)}")
            results_data["positions"] = df.to_dict(orient="records")
            
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

        return jsonify({
            "status": "SUCCESS",
            "data": results_data
        })
    except Exception as e:
        return jsonify({
            "status": "ERROR",
            "message": f"Failed to read result CSVs: {str(e)}"
        })

@app.route('/api/health', methods=['GET'])
def health_check():
    return jsonify({"status": "OK", "message": "Wing-Net Omni Backend Server is running."})

if __name__ == '__main__':
    # 开发环境下运行于 5000 端口，全网段可访问（0.0.0.0）
    app.run(host='0.0.0.0', port=5000, debug=False)
