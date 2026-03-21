# NS-3 UAV Network Simulation Workspace

这个仓库是在 `ns-3.43` 基础上扩展出来的无人机网络仿真工作区，当前保留 4 条主线：

1. `uav_resource_allocation`
   面向无人机编队通信与动态资源分配的主仿真程序。
2. `rtk_benchmark`
   面向 4 种编队、3 种难度的 RTK Benchmark 批量实验。
3. RTK 预处理与可视化
   包括轨迹生成、对齐、可视化和研究型分析脚本。
4. 前后端数字孪生演示
   Flask 后端接收前端参数，生成轨迹、运行 ns-3、返回结果。

## 目录说明

- `scratch/`
  自定义 ns-3 主程序。当前保留的核心入口是 `uav_resource_allocation.cc`、`rtk_benchmark.cc`、`rtk_simulation.cc`。
- `rtk/`
  RTK 数据预处理、轨迹生成、测试和可视化研究。
- `visualization/`
  RTK 轨迹和通信拓扑动画工具。
- `api_server/`
  前后端演示用 Flask API。
- `benchmark/`
  Benchmark 结果、参数说明、指标分析文档。
- `data_rtk/`
  轨迹输入文件。
- `data_map/`
  地图和建筑物输入文件。
- `tools/`
  辅助脚本和一次性处理工具，例如轨迹绘图、OSM 检查、对比分析脚本。
- `samples/`
  零散样例文件、测试输入和历史配置样本。
- `notes/`
  补充说明材料、原型页面和归档资料。
- `src/`, `examples/`, `doc/`, `utils/`
  ns-3 自带源码和文档。

## 核心程序

### 1. 资源分配主线

入口文件：

- `scratch/uav_resource_allocation.cc`
- `run_uav_simulation.sh`
- `analyze_resource_allocation.py`
- `visualize_results.py`

用途：

- 加载编队轨迹
- 构建无人机自组网
- 执行动态信道/功率/速率分配
- 输出 QoS、拓扑、流量和资源分配结果

常用命令：

```bash
./ns3 build uav_resource_allocation
./run_uav_simulation.sh
./run_uav_simulation.sh graph_coloring 15 3 200
./ns3 run "uav_resource_allocation --formation=v_formation --difficulty=Easy --strategy=dynamic --duration=200"
python3 analyze_resource_allocation.py output/<result_dir> --all
python3 visualize_results.py output/<result_dir>
```

### 2. RTK Benchmark 主线

入口文件：

- `scratch/rtk_benchmark.cc`
- `run_benchmark.sh`
- `analyze_benchmark.py`
- `BENCHMARK_QUICK_REF.md`

用途：

- 运行 `cross`、`line`、`triangle`、`v_formation` 四种编队
- 运行 `Easy`、`Moderate`、`Hard` 三种难度
- 输出 benchmark 数据集并汇总分析

常用命令：

```bash
./ns3 build rtk_benchmark
./ns3 run "rtk_benchmark --formation=cross --difficulty=easy"
bash run_benchmark.sh
python3 analyze_benchmark.py
```

### 3. RTK 预处理与可视化

入口文件：

- `rtk/preprocess.py`
- `rtk/generate_ns3_traces.py`
- `rtk/run_pipeline.py`
- `rtk/README.md`
- `rtk/VISUALIZATION_README.md`
- `visualization/README.md`

用途：

- RTK 数据预处理
- ns-3 轨迹文件生成
- RTK 与仿真数据对齐
- 轨迹和拓扑动画生成

示例命令：

```bash
python3 rtk/run_pipeline.py
python3 visualization/plot_rtk_gif.py data_rtk/mobility_trace_cross.txt -o visualization/output/rtk_animation.gif
```

### 4. 前后端数字孪生演示

入口文件：

- `api_server/app.py`
- `rtk/advanced_path_planner.py`
- `osm_to_simulation.py`
- `FRONTEND_API_INTEGRATION_GUIDE.md`

后端流程：

1. 前端提交编队、规模、起终点、难度、策略、建筑物信息
2. 后端生成地图文件
3. 调用 `rtk/advanced_path_planner.py` 生成轨迹
4. 调用 `uav_resource_allocation` 运行 ns-3
5. 调用 `analyze_resource_allocation.py` 生成结果
6. API 返回位置、拓扑、QoS 和资源分配数据

启动命令：

```bash
python3 api_server/app.py
```

默认监听：

```text
http://0.0.0.0:5000
```

## 辅助材料

根目录之外还保留了 3 类低耦合资料，避免继续堆在顶层：

- `tools/`
  例如 `plot_trajectory.py`、`inspect_osm.py`、`build_city_scenario.py`、`compare_algorithms.py`
- `samples/`
  例如历史 `flowmon.xml`、`flow-stats.csv`、`topology-changes.txt`、配置样例和零散测试文件
- `notes/`
  例如原型页面、中文说明文档、PDF 材料

## 输出文件

`uav_resource_allocation` 常见输出：

- `rtk-node-positions.csv`
- `rtk-node-transmissions.csv`
- `rtk-topology-changes.txt`
- `rtk-flow-stats.csv`
- `resource_allocation.csv`
- `resource_allocation_detailed.csv`
- `qos_performance.csv`
- `topology_evolution.csv`
- `topology_detailed.csv`

`rtk_benchmark` 常见输出：

- `benchmark-config.txt`
- `flow-stats.csv`
- `node-positions.csv`
- `topology-changes.txt`

## 推荐阅读顺序

如果你是第一次接手这个仓库，建议按下面顺序看：

1. 本文件
2. `BENCHMARK_QUICK_REF.md`
3. `FRONTEND_API_INTEGRATION_GUIDE.md`
4. `rtk/README.md`
5. `visualization/README.md`
6. `benchmark/README_METRICS.md`

## 说明

这个仓库仍然保留了完整的 `ns-3.43` 源码，因此体积较大。自定义功能主要集中在 `scratch/`、`rtk/`、`visualization/`、`api_server/`、`benchmark/`、`data_rtk/` 和 `data_map/`。
