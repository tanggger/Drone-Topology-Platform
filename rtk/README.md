# RTK到ns-3仿真数据流水线

这个工具集用于将RTK GPS轨迹数据转换为ns-3网络仿真器可用的移动轨迹文件，并运行通信仿真生成拓扑概率图。

## 文件说明

### 数据生成和预处理
- `generate_rtk_data.py`: RTK数据生成器，模拟无人机集群运动
- `preprocess.py`: RTK数据预处理器，坐标转换和时间标准化
- `generate_ns3_traces.py`: ns-3轨迹文件生成器
- `run_pipeline.py`: 完整流水线主控脚本

### 仿真和分析
- `../scratch/rtk_simulation.cc`: 基于RTK轨迹的ns-3仿真程序
- `analyze_communication.py`: 通信数据分析和拓扑图生成
- `test_full_pipeline.py`: 完整流程测试脚本

### 文档
- `README.md`: 本说明文件

## 核心对齐算法

### 1. 时间对齐算法

**问题**: RTK数据的时间戳与ns-3仿真时间需要对齐

**解决方案**:
```python
# 时间标准化: t_sim = (t_rtk - t0) / time_scale
t0 = rtk_data['time_sec'].min()  # 起始时间作为原点
sim_time = (rtk_data['time_sec'] - t0) / time_scale
```

**关键点**:
- 使用第一个RTK数据点的时间作为仿真时间原点
- 支持时间缩放(time_scale > 1加速，< 1减速)
- 确保所有时间相关的事件(通信、位置记录)使用相同的时间轴

### 2. 空间对齐算法

**问题**: RTK使用GPS坐标(WGS84)，ns-3使用米制直角坐标

**解决方案**:
```python
# GPS到ENU坐标转换
lat0, lon0, alt0 = first_gps_point  # ENU原点
lat_to_meter = 111320.0
lon_to_meter = 111320.0 * cos(radians(lat0))

x = (lon - lon0) * lon_to_meter
y = (lat - lat0) * lat_to_meter  
z = alt - alt0
```

**关键点**:
- 选择第一个GPS点作为ENU坐标系原点
- 适用于小范围区域(< 10km)的简化转换
- 保持高度相对差值

### 3. 轨迹对齐算法

**问题**: RTK数据时间间隔不均匀，ns-3需要平滑轨迹

**解决方案**:
```python
# 轨迹插值和平滑
new_times = np.arange(t_min, t_max, target_dt)
x_interp = np.interp(new_times, original_times, x_coords)
# 滑动平均平滑
x_smooth = x_interp.rolling(window=5, center=True).mean()
```

**关键点**:
- 线性插值到固定时间间隔(如0.1s)
- 滑动平均去除GPS噪声
- 保持轨迹连续性

### 4. 通信对齐算法

**问题**: 如何根据RTK轨迹数据安排合理的通信事件

**解决方案** (在rtk_simulation.cc中实现):
```cpp
// 基于距离的智能通信调度
double distance = senderMob->GetDistanceFrom(receiverMob);
double commProb = 1.0;
if (distance > 50.0) {
    commProb = max(0.1, 1.0 - (distance - 50.0) / 150.0);
}
if (rand->GetValue() < commProb && distance < 200.0) {
    // 创建通信事件
}
```

**关键点**:
- 通信概率与节点间距离成反比
- 距离 < 50m: 概率接近100%
- 距离 50-200m: 概率线性递减
- 距离 > 200m: 不通信

### 5. 数据格式对齐

**问题**: 确保输出数据格式与原仿真程序一致

**解决方案**:
- 传输事件: `time_s,nodeId,eventType`
- 节点位置: `time_s,nodeId,x,y,z`  
- 拓扑变化: `start-end s: Node0-Node1, ...`
- FlowMonitor: XML和CSV格式统计

**关键点**:
- 完全兼容原程序的输出格式
- 相同的文件命名约定(添加rtk-前缀区分)
- 保持数据精度和时间戳格式

## 依赖安装

```bash
# 基础依赖
pip install pandas numpy matplotlib

# 如果需要更精确的坐标转换
pip install pyproj
```

## 完整流程使用指南

### 方法1: 一键运行完整流程

```bash
# 进入rtk目录
cd rtk

# 运行完整测试流程
python3 test_full_pipeline.py
```

这将自动执行:
1. 生成RTK数据 (10架无人机，30秒)
2. 预处理数据
3. 生成ns-3轨迹文件
4. 编译并运行仿真
5. 检查输出文件

### 方法2: 分步骤运行

#### 步骤1: 生成RTK数据
```bash
python3 generate_rtk_data.py --num_drones 20 --duration 100 --output rtk_data.csv
```

#### 步骤2: 预处理数据
```bash
python3 preprocess.py --input rtk_data.csv --output_dir processed
```

#### 步骤3: 生成ns-3轨迹
```bash
python3 generate_ns3_traces.py --input processed/processed_trajectories.csv --output_dir ns3_traces
```

#### 步骤4: 编译并运行仿真
```bash
# 返回ns-3根目录
cd ..

# 编译RTK仿真程序
./ns3 build rtk_simulation

# 运行仿真
./ns3 run 'rtk_simulation --trajectory=rtk/ns3_traces/mobility_trace.txt'
```

#### 步骤5: 分析结果和生成拓扑图
```bash
# 返回rtk目录
cd rtk

# 分析通信数据并生成拓扑图
python3 analyze_communication.py \
  --trans_file ../rtk-node-transmissions.csv \
  --pos_file ../rtk-node-positions.csv \
  --output_dir analysis_results
```

### 方法3: 使用流水线脚本

```bash
# 生成20架无人机，100秒的仿真数据
python3 run_pipeline.py

# 自定义参数
python3 run_pipeline.py --num_drones 30 --duration 200 --time_scale 2.0
```

## 输出文件详解

### RTK仿真程序输出 (与原record.cc格式一致)

运行`rtk_simulation`后生成的文件:

1. **rtk-node-transmissions.csv**: 传输事件记录
   ```csv
   time_s,nodeId,eventType
   1.234,0,Tx Data
   1.235,5,Rx Data
   1.240,0,Tx Ack
   ```

2. **rtk-node-positions.csv**: 节点位置记录(每秒)
   ```csv
   time_s,nodeId,x,y,z
   1.000,0,12.5,45.2,25.0
   1.000,1,15.2,48.1,26.5
   ```

3. **rtk-topology-changes.txt**: 拓扑活动链路(每5秒)
   ```
   0-5s: Node0-Node1, Node2-Node5
   5-10s: Node1-Node3
   10-15s: none
   ```

4. **rtk-flowmon.xml**: FlowMonitor详细统计(XML格式)

5. **rtk-flow-stats.csv**: 流统计摘要
   ```csv
   FlowId,SrcAddr,DestAddr,TxPackets,RxPackets,LostPackets,PacketLossRate(%),Throughput(bps),DelaySum(s)
   1,10.0.0.1,10.0.0.2,10,8,2,20.0,1024.5,0.05
   ```

### 中间数据文件

1. **rtk_data.csv**: 原始RTK数据
   ```csv
   timestamp,drone_id,latitude,longitude,altitude,time_sec
   2024-01-01T10:00:00.000000,0,39.904200,116.407400,50.0,0.0
   ```

2. **processed/**: 预处理数据目录
   - `processed_trajectories.csv`: ENU坐标系轨迹数据
   - `velocities.csv`: 速度统计数据

3. **ns3_traces/**: ns-3轨迹目录
   - `uav_*.wp`: 各无人机waypoint文件
   - `mobility_trace.txt`: 统一trace文件
   - `mobility_setup.cpp`: C++代码片段
   - `simulation_config.txt`: 仿真参数配置

### 分析结果输出

运行`analyze_communication.py`后生成:

1. **analysis_results/topology_*.png**: 各时间窗口拓扑图
   - 左图: 网络拓扑图(节点位置+通信链路)
   - 右图: 通信概率矩阵热图

2. **analysis_results/communication_statistics.txt**: 统计摘要
   ```
   通信统计摘要
   ==========================================
   传输事件统计:
     总事件数: 1234
     节点数: 10
     时间范围: 0.0 - 30.0 秒
   ```

## 在ns-3中使用

### 方法1: 使用WaypointMobilityModel

```cpp
#include "ns3/waypoint-mobility-model.h"

// 创建节点
NodeContainer nodes;
nodes.Create(20);

// 设置移动模型
MobilityHelper mobility;
mobility.SetMobilityModel("ns3::WaypointMobilityModel");
mobility.Install(nodes);

// 加载轨迹数据
for (uint32_t i = 0; i < nodes.GetN(); ++i) {
    Ptr<WaypointMobilityModel> waypoint = 
        nodes.Get(i)->GetObject<WaypointMobilityModel>();
    
    std::ifstream file("rtk/ns3_traces/uav_" + std::to_string(i) + ".wp");
    std::string line;
    std::getline(file, line); // skip header
    std::getline(file, line); // skip header
    
    double time, x, y, z;
    while (file >> time >> x >> y >> z) {
        waypoint->AddWaypoint(Waypoint(Seconds(time), Vector(x, y, z)));
    }
    file.close();
}
```

### 方法2: 使用Trace文件

```cpp
// 在仿真开始前设置trace回调
Config::Connect("/NodeList/*/$ns3::MobilityModel/CourseChange", 
                MakeCallback(&CourseChangeCallback));

// 从trace文件读取并设置位置
void LoadMobilityTrace() {
    std::ifstream file("rtk/ns3_traces/mobility_trace.txt");
    // ... 解析并应用位置数据
}
```

## 参数说明

### RTK数据生成参数
- `--num_drones`: 无人机数量 (默认: 20)
- `--duration`: 仿真持续时间，秒 (默认: 100.0)
- `--dt`: 时间步长，秒 (默认: 0.1)

### 预处理参数
- `--time_scale`: 时间缩放因子，>1加速，<1减速 (默认: 1.0)
- `--dt`: 插值时间间隔 (默认: 0.1)
- `--smooth`: 平滑窗口大小 (默认: 5)

## 集群运动算法

RTK数据生成器实现了简单的Boids集群算法，包含三个基本行为:

1. **分离 (Separation)**: 避免与邻近无人机碰撞
2. **对齐 (Alignment)**: 与邻居保持相似的飞行方向
3. **聚合 (Cohesion)**: 向邻居群体中心靠拢

这样生成的轨迹具有真实的集群飞行特征。

## 坐标系转换

- **输入**: WGS84 GPS坐标 (纬度、经度、高度)
- **输出**: ENU坐标系 (东、北、上)，单位米
- **原点**: 第一个数据点的GPS位置

## 故障排除

### 常见问题

1. **ModuleNotFoundError**: 安装缺失的Python包
2. **文件不存在**: 检查文件路径和权限
3. **数据格式错误**: 确认CSV文件格式正确
4. **内存不足**: 减少无人机数量或仿真时长

### 调试技巧

- 使用 `--dt 1.0` 减少数据点数量进行测试
- 检查生成的配置文件了解数据统计信息
- 使用matplotlib可视化轨迹数据

## 扩展功能

### 添加自定义运动模式
修改 `random.py` 中的 `update_formation()` 方法。

### 支持真实RTK数据
修改 `preprocess.py` 的数据加载部分以支持您的RTK数据格式。

### 高精度坐标转换
安装 `pyproj` 并修改坐标转换函数以获得更高精度。

## 许可证

MIT License