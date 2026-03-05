# UAV网络仿真平台 (UAV Network Simulation Platform)

## 📋 概述 (Overview)

本项目基于 **ns-3** 构建了一个模块化、插件化的无人机网络仿真平台。通过 `UavSimHelper` 统一编排，支持灵活的场景配置（如侦察、编队、蜂群）、多级难度调节以及详细的数据采集。

核心设计理念是将**移动性**、**信道模型**、**通信负载**和**数据采集**解耦为独立的插件，用户可以像搭积木一样组合不同的仿真要素。

---

## 💻 仿真代码与逻辑 (Simulation Code & Logic)

以 `scratch/uav_simple_example.cc` 为代表，仿真流程如下：

### 1. 初始化与配置
```cpp
UavSimHelper sim;
sim.SetName("Simple Example")
   .SetDuration(100.0)      // 仿真时长
   .SetNumNodes(20)         // 节点数量
   .SetOutputDir("output"); // 数据输出目录
```

### 2. 场景选择 (Scenario Selection)
框架预置了多种典型任务场景，自动加载默认的插件组合：
- **`sim.ReconnaissanceScenario()`**: 侦察任务（RTK轨迹 + 城市信道 + 事件驱动通信）
- **`sim.FormationScenario()`**: 编队飞行（编队移动模型 + 理想信道 + 周期性通信）
- **`sim.SwarmScenario()`**: 蜂群协作（随机游走 + 混合业务）

### 3. 运行机制 (Execution Flow)
当调用 `sim.Run()` 时，内部逻辑如下：
1.  **加载插件**: 根据配置（如 "rtk-mobility", "urban-channel"）实例化相应的 C++ 对象。
2.  **环境初始化**: 创建 ns-3 节点，安装网卡、协议栈和移动模型。
3.  **事件循环**: 
    - **移动性插件**更新节点位置（支持RTK轨迹回放、漂移、噪声）。
    - **业务插件**根据逻辑（如距离、概率）触发通信事件。
    - **信道插件**计算路径损耗、衰落和干扰。
    - **采集插件**监听全网事件并记录数据。
4.  **清理**: 仿真结束，刷新缓冲区，生成数据文件。

---

## 📊 输出文件说明 (Output Files)

仿真结果保存在 `output/` 目录下（或通过 `SetOutputDir` 指定的目录）。

### 1. node-transmissions.csv
记录每次传输层事件（TCP/UDP包的发送与接收）。

| 字段名 | 说明 |
|--------|------|
| time_s | 事件发生时间（秒） |
| nodeId | 节点ID |
| eventType | 事件类型（Tx Data/Rx Data/Tx Ack/Rx Ack） |

### 2. topology-changes.txt
记录网络拓扑的动态变化（每5秒汇总一次活跃链路）。
```text
0-5s: Node0-Node5, Node1-Node8
5-10s: Node2-Node7, Node12-Node19
```

### 3. node-positions.csv
记录节点的三维轨迹（默认1秒间隔）。

| 字段名 | 说明 |
|--------|------|
| time_s | 记录时间 |
| nodeId | 节点ID |
| x, y, z | 三维坐标（米） |

### 4. flow-stats.csv
端到端业务流的统计指标。

| 字段名 | 说明 |
|--------|------|
| FlowId | 流ID |
| Src/DestAddr | 源/目的IP |
| PacketLossRate | 丢包率 (%) |
| Throughput | 吞吐量 (bps) |
| DelaySum | 总时延 (s) |

---

## 🔧 配置参数 (Configuration)

可以通过 `sim.SetParam("plugin.param", value)` 调整参数。

### 移动性参数 (mobility.*)
| 参数 | 说明 | 默认值 |
|------|------|--------|
| `trajectory_file` | RTK轨迹文件路径 | - |
| `noise_stddev` | GPS噪声标准差 (m) | 0.01 (Easy) / 0.2 (Hard) |
| `enable_drift` | 是否启用漂移 | false / true |
| `formation` | 编队形状 | "v", "line", "triangle" |

### 信道参数 (channel.*)
| 参数 | 说明 |
|------|------|
| `tx_power` | 发射功率 (dBm), 默认 33.0 |
| `rx_sensitivity`| 接收灵敏度 (dBm), 默认 -93.0 |
| `interference_nodes` | 人为干扰节点数量 |

### 业务参数 (traffic.*)
| 参数 | 说明 |
|------|------|
| `trigger_prob` | 通信触发概率 (0.0-1.0) |
| `near_distance` | 近距离通信阈值 (m) |
| `max_distance` | 最大通信距离 (m) |

---

## 🚀 快速开始 (Quick Start)

```bash
# 1. 配置
./ns3 configure --enable-examples

# 2. 编译
./ns3 build

# 3. 运行简单示例（指定场景和难度）
./ns3 run "uav-sim-simple-example --scenario=reconnaissance --difficulty=moderate"

# 4. 运行基准测试
./ns3 run "uav-sim-benchmark-example"
```
