# UAV资源分配仿真 - 快速入门指南

## 1分钟快速开始

### 步骤1: 编译程序

```bash
cd /mnt/e/Simulator/ns3/workspace/ns-allinone-3.43/ns-3.43
./ns3 build
```

### 步骤2: 运行仿真

```bash
# 使用默认参数运行（15个UAV，3个信道，图着色算法）
./run_uav_simulation.sh
```

### 步骤3: 查看结果

```bash
# 结果保存在 output/uav_resource_allocation_graph_coloring_15uavs_3ch/
cd output/uav_resource_allocation_graph_coloring_15uavs_3ch/figures/
ls -l
# 查看图表: resource_allocation.png, qos_performance.png, etc.
```

---

## 项目文件说明

### 核心仿真代码
- **uav-sim-helper.h/cc**: UAV仿真辅助类，提供距离计算、链路质量评估等功能
- **uav-resource-allocator.h/cc**: 资源分配算法实现（4种策略）
- **uav_resource_allocation_advanced.cc**: 主仿真程序

### 配置和脚本
- **uav_resource_allocation_config.ini**: 仿真配置文件
- **run_uav_simulation.sh**: 运行脚本（推荐使用）
- **compare_strategies.sh**: 策略对比脚本
- **test_installation.sh**: 安装测试脚本
- **visualize_results.py**: 结果可视化脚本

### 文档
- **UAV_README.md**: 完整使用文档
- **QUICKSTART.md**: 本快速入门指南

---

## 常用命令

### 运行不同策略

```bash
# 静态分配
./run_uav_simulation.sh static 15 3

# 贪心算法
./run_uav_simulation.sh greedy 15 3

# 图着色算法（推荐）
./run_uav_simulation.sh graph_coloring 15 3

# 干扰感知算法
./run_uav_simulation.sh interference_aware 15 3
```

### 运行策略对比实验

```bash
./compare_strategies.sh
# 将自动运行4种策略并生成对比报告
```

### 修改仿真参数

```bash
# 语法: ./run_uav_simulation.sh [策略] [UAV数] [信道数] [时长]
./run_uav_simulation.sh graph_coloring 20 4 300
```

### 仅生成可视化（不重新运行仿真）

```bash
python3 visualize_results.py output/uav_resource_allocation_xxx/
```

---

## 性能指标说明

### 输出文件

1. **resource_allocation.csv**: 记录每个时刻各UAV的信道、功率、速率分配
2. **qos_performance.csv**: 记录平均PDR、时延、吞吐量
3. **topology_evolution.csv**: 记录链路数量、网络连通性
4. **summary.txt**: 性能摘要

### 可视化图表

1. **resource_allocation.png**: 资源分配演化图
   - 信道分配演化
   - 功率分配演化
   - 速率分配演化

2. **qos_performance.png**: QoS性能曲线
   - PDR演化（目标≥85%）
   - 时延演化（目标≤100ms）
   - 吞吐量演化

3. **topology_evolution.png**: 拓扑演化图
   - 链路数量变化
   - 网络连通性变化

4. **channel_utilization.png**: 信道利用率统计

---

## 4种资源分配策略对比

| 策略 | 优点 | 缺点 | 适用场景 |
|------|------|------|---------|
| **Static** | 简单，开销小 | 不适应拓扑变化 | 静态网络 |
| **Greedy** | 快速，较好性能 | 可能陷入局部最优 | 中等规模网络 |
| **Graph Coloring** | 避免冲突，性能好 | 计算开销中等 | **推荐使用** |
| **Interference Aware** | 最小化干扰 | 计算开销较大 | 高密度网络 |

---

## 典型实验场景

### 场景1: 基础验证（推荐新手）
```bash
./run_uav_simulation.sh graph_coloring 15 3 200
```
- 15个UAV，3个信道，200秒
- 验证基本功能和性能指标

### 场景2: 高密度网络
```bash
./run_uav_simulation.sh interference_aware 30 4 300
```
- 30个UAV，4个信道，300秒
- 测试高密度下的资源分配性能

### 场景3: 信道受限
```bash
./run_uav_simulation.sh graph_coloring 20 2 200
```
- 20个UAV，仅2个信道
- 测试信道竞争情况

### 场景4: 策略对比
```bash
./compare_strategies.sh
```
- 自动运行4种策略
- 生成对比报告

---

## 修改配置文件

编辑 `uav_resource_allocation_config.ini`:

```ini
[scenario]
num_nodes = 20              # 改为20个UAV

[resource_allocation]
num_channels = 4            # 改为4个信道
reallocation_interval = 10.0  # 改为10秒重分配一次

[qos_requirements]
target_pdr = 0.90           # 提高PDR要求到90%
max_delay = 0.080           # 降低时延要求到80ms

[topology]
area_size = 1000.0          # 扩大区域到1000x1000米
communication_range = 200.0  # 增大通信范围到200米
```

---

## 常见问题快速解决

### 问题1: 编译错误
```bash
# 清理后重新编译
./ns3 clean
./ns3 configure --enable-examples --enable-tests
./ns3 build
```

### 问题2: PDR很低
- 增加信道数量
- 增大通信范围
- 降低UAV移动速度
- 使用干扰感知策略

### 问题3: 时延过高
- 减少节点数量
- 增大通信范围
- 优化路由协议

### 问题4: 可视化失败
```bash
# 安装Python依赖
pip3 install pandas matplotlib numpy
```

---

## 下一步学习

1. **阅读完整文档**: `cat UAV_README.md`
2. **修改算法**: 编辑 `uav-resource-allocator.cc` 添加自己的策略
3. **调整参数**: 修改配置文件进行参数优化
4. **扩展功能**: 添加新的性能指标和分析方法

---

## 课程大作业建议

### 基础要求（60分）
- ✓ 运行仿真并生成结果
- ✓ 分析QoS性能指标
- ✓ 撰写实验报告

### 进阶要求（80分）
- ✓ 对比不同资源分配策略
- ✓ 分析不同场景下的性能
- ✓ 优化算法参数

### 高级要求（100分）
- ✓ 实现新的资源分配算法
- ✓ 提出性能优化方案
- ✓ 深入分析算法复杂度和收敛性

---

## 技术支持

遇到问题？

1. 运行安装测试: `./test_installation.sh`
2. 查看完整文档: `cat UAV_README.md`
3. 检查输出日志: `cat output/xxx/summary.txt`

---

**祝你实验成功！Good Luck！**

