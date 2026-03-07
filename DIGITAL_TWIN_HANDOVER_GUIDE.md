# Wing-Net Omni 数字孪生平台：前后端交互与数据流转指南

本文档详细说明了“基于 NS-3 的低空无人机数字孪生平台”的前后端交互逻辑，以及如何从零开始运行整个项目，从而产出前端 3D 大屏所需的所有高保真展示物料。

后端的本质是一个**高精度的物理与网络仿真引擎**，前端则作为**指挥中心和渲染大屏**。

---

## 一、 前端需要输入什么？(配置下发)

前端需要将用户界面上的交互转化为具体的文件或命令行参数，传递给后端：

### 1. 物理环境约束 (设置建筑物)
用户在前端 3D 地图上放置大厦障碍物，前端将这些坐标转化为文本格式。
*   **目标文件**: `data_map/custom_city.txt`
*   **文件格式**: `xMin xMax yMin yMax zMin zMax`
*   **示例**:
    ```text
    # xMin  xMax  yMin  yMax  zMin  zMax
    50.0  90.0  200.0 240.0 0.0  300.0
    ```

### 2. 飞行任务规划 (起终点、编队与集群规模)
前端指定起飞点、目标点、编队类型以及**无人机集群的规模数量**（只需修改 Python 脚本的参数，随后 C++ 核心就会自动探明飞机数量并适配生成网卡硬件），并调用后端的 Python 高级路径规划器。
*   **接口参数**: `Swarm Size` (如拖动进度条选择 15, 30 或 50 架)
*   **执行脚本**: `rtk/advanced_path_planner.py`
*   **执行命令**:
    ```bash
    python3 rtk/advanced_path_planner.py --num_drones 30 --formation v_formation --start 0,0,30 --target 0,600,30 --map ../data_map/custom_city.txt --output ../data_rtk/mobility_trace_custom.txt
    ```
*   **产出物**: `data_rtk/mobility_trace_custom.txt` (融合了集群本能、阵型保持的动态避障轨迹，其内部节点的数量也将直接决定底层仿真的网络规模)。

### 3. 通信与干扰环境设定
前端用户可选的难度强度，影响背后的漂移误差、环境衰落与额外干扰节点的活跃程度。
*   **接口参数**: `Difficulty: Easy / Moderate / Hard`

### 4. 资源调度算法选择
决定无人机在复杂信号环境中“自救”的策略。
*   **接口参数**: `Strategy: static / dynamic(图着色+自适应)`

---

## 二、 后端核心引擎执行 (算力爆发)

拿到前端的“地图文件”和“动态轨迹文件”后，后端将启动 NS-3 C++ 核心仿真引擎。
前端还将附带两个核心控制变量：**电磁环境难度 (Difficulty)** 和 **抗干扰算法策略 (Strategy)**。

*   **执行组件**: `scratch/uav_resource_allocation.cc`
*   **执行命令**:
    ```bash
    ./ns3 run 'uav_resource_allocation \
      --formation=custom \
      --difficulty=Hard \
      --strategy=dynamic \
      --outputDir=output/demo_run'
    ```
    *注：`--formation=custom` 会引导 NS-3 读取刚刚由 Python 引擎秒算生成的 `data_rtk/mobility_trace_custom.txt`。*

---

## 三、 后端产出给前端的物料清单 (渲染与图表)

仿真引擎运行结束后（通常在 `output/demo_run/` 目录下），会生成以下高价值的时序数据，供前端按时间轴进行播放和图表渲染：

### 1. 3D 场景高保真回放层 (视觉图层)

| 输出文件 | 数据格式 | 前端用途 |
| :--- | :--- | :--- |
| **`rtk-node-positions.csv`** | `time_s, nodeId, x, y, z` | **核心轨迹动画**。控制 3D 地图中 15 架无人机模型的位置。包含了底层模拟的 RTK 抖动漂移和动态避障产生的绕飞平滑曲线。 |
| **`rtk-topology-changes.txt`** | `[Time] Node A - Node B` | **组网连线动画**。绘制无人机之间的拓扑骨干网，当拓扑因为建筑遮挡而断裂时，前端即可断开连线引起视觉警报。 |
| **`rtk-node-transmissions.csv`** | `time, nodeId, eventType` | **通信激光特效**。配合拓扑连线，当有 Tx/Rx 事件时，在两架无人机之间发射光束动画，丢包时可以做红色的消散特效。 |
| **`resource_allocation.csv`** | `time, nodeId, channel, TxPower`| **AI 自适应光环**。在无人机脚下生成颜色光环（展示信道 Channel 切换，如蓝色=信道1），光环大小绑定 TxPower（展示智能功率放大/缩小）。 |

### 2. 大屏面板分析图表层 (数据分析层)

这些文件除了前端直接解析外，还会被 `analyze_resource_allocation.py` 读取，进一步生成自动化的分析报告（Markdown/JSON）和高科研感的可视化图表。

| 输出文件 | 数据格式 | 前端用途 |
| :--- | :--- | :--- |
| **`qos_performance.csv`** | `time, nodeId, PDR, Delay, Throughput` | **核心 QoS 仪表盘**。用于绘制前端两侧的实时折线图。展现 AI 图着色算法开启后，PDR（包到达率）如何在 Hard 干扰难度下保持 95% 以上。 |
| **`topology_evolution.csv`** | `time, num_links, connectivity` | **网络连通性演化**。反映抗干扰通信网络的动态变化抗毁程度，非常适合画成动态折线图。 |
| **`topology_detailed.csv`** | `time, num_nodes, num_links, avg_degree, network_density` | **深度拓扑密度监控**。记录随着飞机队列散开/收缩而发生的全网度数（Avg Degree）变化。 |
| **`resource_allocation_detailed.csv`**| `time, node_id, channel, tx_power, data_rate, neighbors, interference` | **精细化散点热图**。相比基础的 `resource_allocation.csv`，它多了该时刻节点感知到的**干扰值**和**邻居数**，可用于画气泡图或抗干扰对照直方图。 |
| **`rtk-flow-stats.csv`** | `FlowId, Src, Dest, Tx, Rx, LossRate` | **综合评估榜单**。全局网络视角的总结性统计，用于仿真播放完成后的“总结算面板”，展示整体吞吐总量和端到端延迟分布。 |

---

## 四、 完整的交互闭环 (One-Click Demo)

当评委或用户在前端点击 **[开始推演]** 按钮时，系统背后的自动化流水线如下：

1. **【配置期】** 评委在前端网页上点击屏幕：“在坐标 (50, 200) 建一栋 300 米高楼”，设定“30 架规模，V字起飞，目的地是 (0, 700)”，并开启“AI 图着色抗干扰算法”。点击【开始仿真】。
2. **【轨迹秒算】** 后台收到请求，第一步携带 `--num_drones 30` 参数调用 `advanced_path_planner.py`。耗时 0.5 秒，根据大楼位置瞬间算好 30 架飞机的 V 字群体避障机动曲线 `trace_custom.txt`。
3. **【核心网仿】** 后台第二步唤起 `./ns3 run ...` 进入 `uav_resource_allocation.cc` 的时间循环。在这个过程中，无人机群不仅展现集群队形绕飞大楼，且底层动态图着色引擎不断对抗 Hard 难度的瞎广播干扰，生成每一次成功避开碰撞与丢包的网络抗争记录。
4. **【数据交付】** 仿真结束，所有上述 CSV 文件就绪并推行给前端。
5. **【前端播片】** 前端开始读取时间戳（time_s=1, 2, 3...）：
   * 30 架无人机从画面下方浩浩荡荡起飞；
   * 在靠近高楼时智能分离绕行；
   * 飞机底部因为策略调整瞬间闪烁紫/蓝光（智能信道切分）；
   * 各机之间绿色激光连线交汇不断；
   * 伴随右侧大屏的 PDR 折线稳定在健康的 90% 以上而基准对撞红线俯冲暴跌。
   一场极具科研深度的数字孪生演示完美达成！
