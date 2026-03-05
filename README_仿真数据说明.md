# 无人机集群网络仿真数据说明文档

## 一、仿真概述

本仿真基于 NS-3 网络模拟器，针对四种典型的无人机集群飞行形态（十字形、直线形、三角形、V字形）进行了 Ad-Hoc 网络通信性能的分析和评估。仿真采用 RTK（Real-Time Kinematic）高精度定位轨迹数据驱动节点移动，并使用 IEEE 802.11ac 标准进行无线通信。

### 1.1 仿真参数
- **节点数量**: 15个无人机节点
- **仿真时长**: 约 100-3000 秒（根据不同飞行形态而定）
- **通信标准**: IEEE 802.11ac (VhtMcs0)
- **网络拓扑**: Ad-Hoc 自组织网络
- **传输协议**: TCP (NewReno)
- **移动模型**: WaypointMobilityModel（基于RTK轨迹）

### 1.2 四种飞行形态
1. **Cross（十字形）**: 无人机以十字形队形飞行
2. **Line（直线形）**: 无人机以直线队形飞行
3. **Triangle（三角形）**: 无人机以三角形队形飞行
4. **V_formation（V字形）**: 无人机以V字编队飞行

---

## 二、仿真算法流程

### 2.1 总体流程图

```
开始
  ↓
1. 参数配置与初始化
  ├─ 解析命令行参数
  ├─ 设置输出目录
  └─ 加载RTK轨迹数据文件
  ↓
2. 节点创建与移动模型设置
  ├─ 创建节点容器（NodeContainer）
  ├─ 加载并解析轨迹文件
  ├─ 为每个节点设置WaypointMobilityModel
  └─ 按时间顺序添加轨迹航点
  ↓
3. 无线网络配置
  ├─ WiFi 802.11ac 标准配置
  ├─ 信道传播模型设置
  │   ├─ ThreeLogDistancePropagationLossModel（三段对数距离衰减）
  │   └─ NakagamiPropagationLossModel（Nakagami衰落模型）
  ├─ 物理层参数配置
  │   ├─ 发射功率: 33 dBm
  │   └─ 接收灵敏度: -93 dBm
  └─ MAC层配置（Ad-Hoc模式）
  ↓
4. 网络协议栈安装
  ├─ 安装TCP/IP协议栈
  ├─ 配置TCP参数（NewReno, 重传策略等）
  └─ 分配IPv4地址（10.0.0.0/24）
  ↓
5. 通信应用调度
  ├─ 智能通信调度算法
  ├─ 基于距离的通信概率模型
  │   ├─ 近距离（< 25m）: 概率 = 1.0
  │   ├─ 中距离（25-100m）: 概率线性衰减
  │   └─ 远距离（> 100m）: 不通信
  └─ 动态创建OnOff应用（每0.2秒评估一次）
  ↓
6. 数据采集与监控
  ├─ IPv4层传输事件回调（Tx/Rx监控）
  ├─ 节点位置记录（每秒一次）
  ├─ 拓扑变化记录（每5秒统计）
  └─ FlowMonitor流量监控
  ↓
7. 仿真执行
  └─ 运行仿真至结束时间
  ↓
8. 结果输出与统计
  ├─ 生成传输事件CSV文件
  ├─ 生成节点位置CSV文件
  ├─ 生成拓扑变化TXT文件
  ├─ 生成流统计CSV文件
  └─ 生成FlowMonitor XML文件
  ↓
结束
```

### 2.2 关键算法详解

#### 2.2.1 RTK轨迹加载算法
```cpp
LoadTrajectoryData(filename):
  1. 打开CSV格式轨迹文件
  2. 跳过文件头
  3. 逐行解析轨迹点：
     - time: 时间戳（秒）
     - nodeId: 节点ID
     - x, y, z: 三维坐标（米）
  4. 按节点ID分组存储轨迹点
  5. 对每个节点轨迹按时间升序排序
  6. 移除时间非严格递增的重复点
  7. 计算仿真时长和节点数量
```

#### 2.2.2 移动模型设置算法
```cpp
SetupRTKMobility(nodes):
  对于每个节点 i:
    1. 创建 WaypointMobilityModel
    2. 获取该节点的轨迹点列表
    3. 如果轨迹为空，设置默认初始位置
    4. 否则：
       - 设置初始位置为第一个轨迹点
       - 从第二个轨迹点开始添加航点
       - 每个航点包含时间和三维坐标
    5. 将移动模型绑定到节点
```

#### 2.2.3 智能通信调度算法
```cpp
ScheduleIntelligentCommunication(nodes, interfaces):
  对于时间 t 从 1.0 到 SimulationEndTime，步长 0.2秒:
    调度事件在时间 t:
      1. 以 40% 概率触发通信尝试
      2. 随机选择发送节点和接收节点
      3. 计算两节点间的欧氏距离 d
      4. 根据距离计算通信概率:
         - 如果 d < 25m: commProb = 1.0
         - 如果 25m ≤ d < 100m: commProb = max(0.1, 1.0 - (d-25)/75)
         - 如果 d ≥ 100m: 不通信
      5. 以 commProb 概率创建 TCP OnOff 应用:
         - 数据包大小: 512 字节
         - 数据速率: 1 Mbps
         - 持续时间: 0.01 秒（发送），0.05 秒（应用生命周期）
```

#### 2.2.4 传输事件跟踪算法
```cpp
Ipv4Tracer(context, packet, ipv4, interface):
  1. 提取IP和TCP头部信息
  2. 过滤非TCP数据包
  3. 判断事件类型（Tx/Rx, Data/Ack）:
     - 有效载荷 > 0: Data
     - 有效载荷 = 0 且有ACK标志: Ack
  4. 记录到传输事件文件:
     - 时间戳, 节点ID, 事件类型
  5. 维护拓扑链路信息:
     - 提取对端节点ID
     - 记录到当前5秒时间窗口的链路集合
```

#### 2.2.5 拓扑变化统计算法
```cpp
TopologyOutput(index):
  1. 计算时间窗口: [index*5, (index+1)*5] 秒
  2. 输出该时间窗口内的所有活动链路
  3. 格式: "Node_a-Node_b" （a < b）
  4. 清空该时间窗口的链路集合
```

---

## 三、输出文件说明

每种飞行形态的仿真结果存储在对应的子目录中，包含以下五个文件：

### 3.1 rtk-node-positions.csv

**功能**: 记录所有节点在仿真过程中的位置轨迹

**格式**: CSV格式，逗号分隔

**字段说明**:
| 字段名 | 数据类型 | 单位 | 说明 |
|--------|----------|------|------|
| time_s | 浮点数 | 秒 | 仿真时间戳，从1秒开始，每秒记录一次 |
| nodeId | 整数 | - | 节点唯一标识符，范围 0-14 |
| x | 浮点数 | 米 | 节点X轴坐标（东向） |
| y | 浮点数 | 米 | 节点Y轴坐标（北向） |
| z | 浮点数 | 米 | 节点Z轴坐标（高度） |

**示例数据**:
```
time_s,nodeId,x,y,z
1,0,0,0,0
1,1,1.791,0.133,0.2
1,2,2.004,0.022,0.01
2,0,0,0,0
2,1,1.791,0.133,0.2
```

**用途**:
- 可视化无人机轨迹
- 分析集群队形保持情况
- 计算节点间距离变化
- 验证移动模型正确性

---

### 3.2 rtk-node-transmissions.csv

**功能**: 记录所有节点的数据包传输事件

**格式**: CSV格式，逗号分隔

**字段说明**:
| 字段名 | 数据类型 | 单位 | 说明 |
|--------|----------|------|------|
| time_s | 浮点数 | 秒 | 传输事件发生时间，精确到毫秒（3位小数） |
| nodeId | 整数 | - | 执行传输操作的节点ID |
| eventType | 字符串 | - | 事件类型，包含四种：<br>- "Tx Data": 发送数据包<br>- "Rx Data": 接收数据包<br>- "Tx Ack": 发送确认包<br>- "Rx Ack": 接收确认包 |

**示例数据**:
```
time_s,nodeId,eventType
2.010,3,Tx Ack
2.019,2,Rx Ack
2.019,2,Tx Ack
2.023,2,Tx Data
2.023,3,Rx Data
```

**用途**:
- 分析网络通信活跃度
- 统计节点发送/接收数据量
- 识别通信瓶颈节点
- 评估TCP确认机制性能
- 可视化通信时序图

---

### 3.3 rtk-flow-stats.csv

**功能**: 统计每个通信流的详细性能指标

**格式**: CSV格式，逗号分隔

**字段说明**:
| 字段名 | 数据类型 | 单位 | 说明 |
|--------|----------|------|------|
| FlowId | 整数 | - | 流的唯一标识符 |
| SrcAddr | IP地址 | - | 源节点IP地址（10.0.0.x格式） |
| DestAddr | IP地址 | - | 目标节点IP地址（10.0.0.x格式） |
| TxPackets | 整数 | 个 | 该流发送的数据包总数 |
| RxPackets | 整数 | 个 | 该流接收的数据包总数 |
| LostPackets | 整数 | 个 | 丢失的数据包数量（TxPackets - RxPackets） |
| PacketLossRate(%) | 浮点数 | % | 丢包率百分比 |
| Throughput(bps) | 浮点数 | bps | 平均吞吐量（比特每秒） |
| DelaySum(s) | 浮点数 | 秒 | 所有数据包的总延迟 |

**示例数据**:
```
FlowId,SrcAddr,DestAddr,TxPackets,RxPackets,LostPackets,PacketLossRate(%),Throughput(bps),DelaySum(s)
1,10.0.0.3,10.0.0.4,11,11,0,0,671996,0.0113663
2,10.0.0.4,10.0.0.3,6,6,0,0,63290.1,0.00921802
```

**用途**:
- 评估端到端通信质量
- 计算平均延迟（DelaySum / RxPackets）
- 分析丢包率分布
- 比较不同节点对之间的通信性能
- 识别通信质量较差的链路

---

### 3.4 rtk-topology-changes.txt

**功能**: 记录网络拓扑的动态变化情况

**格式**: 文本格式，每行代表一个5秒时间窗口

**字段说明**:
- **时间窗口**: 格式为 "开始时间-结束时间s:"
- **活动链路**: 格式为 "Node_a-Node_b"，其中 a < b，多个链路用逗号和空格分隔
- **无链路**: 显示 "none"

**示例数据**:
```
0-5s: Node0-Node8, Node2-Node3, Node2-Node4, Node2-Node8, Node4-Node8
5-10s: Node0-Node1, Node0-Node2, Node1-Node3, Node1-Node12, Node2-Node7
10-15s: Node0-Node3, Node1-Node10, Node3-Node10, Node5-Node14, Node7-Node13
```

**用途**:
- 分析网络连通性随时间的变化
- 识别稳定通信链路
- 发现拓扑频繁变化的时间段
- 评估集群协同通信能力
- 为路由协议优化提供依据

**链路定义**: 在某个5秒窗口内，两个节点之间发生过至少一次成功的数据包传输（Tx-Rx配对），则认为该链路活动。

---

### 3.5 rtk-flowmon.xml

**功能**: FlowMonitor模块生成的详细流量监控数据

**格式**: XML格式，符合NS-3 FlowMonitor规范

**主要内容**:
- **FlowStats**: 每个流的详细统计信息
  - 时延直方图
  - 抖动直方图
  - 数据包大小分布
  - 丢包原因分析
- **Ipv4FlowClassifier**: 流的五元组分类信息
  - 源IP、目标IP
  - 源端口、目标端口
  - 协议类型
- **FlowProbes**: 各个探针点的监控数据

**用途**:
- 深度分析网络性能
- 使用NS-3 FlowMonitor工具进行可视化
- 导出到第三方分析工具（如Wireshark、Python脚本）
- 研究时延、抖动等QoS指标的分布特性

**注意**: 此文件通常较大，建议使用专门的XML解析工具或NS-3提供的分析脚本进行处理。

---

## 四、数据文件目录结构

```
output/
├── cross/                      # 十字形飞行形态
│   ├── rtk-node-positions.csv
│   ├── rtk-node-transmissions.csv
│   ├── rtk-flow-stats.csv
│   ├── rtk-topology-changes.txt
│   └── rtk-flowmon.xml
├── line/                       # 直线形飞行形态
│   ├── rtk-node-positions.csv
│   ├── rtk-node-transmissions.csv
│   ├── rtk-flow-stats.csv
│   ├── rtk-topology-changes.txt
│   └── rtk-flowmon.xml
├── triangle/                   # 三角形飞行形态
│   ├── rtk-node-positions.csv
│   ├── rtk-node-transmissions.csv
│   ├── rtk-flow-stats.csv
│   ├── rtk-topology-changes.txt
│   └── rtk-flowmon.xml
└── v_formation/                # V字形飞行形态
    ├── rtk-node-positions.csv
    ├── rtk-node-transmissions.csv
    ├── rtk-flow-stats.csv
    ├── rtk-topology-changes.txt
    └── rtk-flowmon.xml
```

---

## 五、仿真技术细节

### 5.1 通信模型参数

#### 传播损耗模型
- **ThreeLogDistancePropagationLossModel**（三段对数距离衰减模型）
  - Distance0 = 1.0 m, Exponent0 = 2.5
  - Distance1 = 100.0 m, Exponent1 = 3.0
  - Distance2 = 250.0 m, Exponent2 = 3.5
  - ReferenceLoss = 46.6777 dB

- **NakagamiPropagationLossModel**（Nakagami衰落模型）
  - Distance1 = 50.0 m, m0 = 1.5
  - Distance2 = 150.0 m, m1 = 1.0, m2 = 0.75

#### 物理层参数
- 发射功率: 33 dBm
- 接收灵敏度: -93 dBm
- 噪声系数: 6.0 dB
- 传播速度: 光速 (299,792,458 m/s)

#### TCP参数
- 连接超时: 5.0 秒
- 最大连接尝试次数: 6 次
- 数据重传次数: 6 次
- 延迟ACK超时: 0.2 秒
- 最小RTO: 0.5 秒
- 拥塞控制算法: TCP NewReno

### 5.2 应用层参数
- 数据包大小: 512 字节
- 数据速率: 1 Mbps
- OnOff周期: On = 0.01秒, Off = 0.0秒
- 应用生命周期: 0.05秒

### 5.3 距离相关的通信概率模型

该模型模拟了无人机之间基于距离的通信可靠性：

```
commProb(d) = {
  1.0,                        如果 d < 25m
  max(0.1, 1.0 - (d-25)/75),  如果 25m ≤ d < 100m
  0.0,                        如果 d ≥ 100m
}
```

其中 d 为两节点间的欧氏距离。

**设计理念**:
- 近距离（<25m）保证高可靠通信
- 中距离（25-100m）概率线性衰减，模拟信号质量下降
- 远距离（≥100m）禁止通信，避免低质量连接

---


