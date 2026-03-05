# 如何衡量每种优化算法的效果

## 📊 快速答案

### 核心评估指标（必须查看）

1. **分组投递率 (PDR)** - 越高越好，目标 ≥ 85%
2. **端到端时延** - 越低越好，目标 ≤ 100ms  
3. **总吞吐量** - 越高越好

### 三步评估法

```bash
# 步骤1: 运行仿真
./run_uav_simulation.sh graph_coloring 15 3 200

# 步骤2: 查看性能摘要
cat output/uav_resource_allocation_xxx/summary.txt

# 步骤3: 查看可视化图表
ls output/uav_resource_allocation_xxx/figures/
```

---

## 🔬 详细评估方法

### 方法1: 单独评估单个算法

**步骤**:
1. 运行仿真
2. 查看summary.txt获取关键指标
3. 查看figures/中的性能曲线图

**判断标准**:
- ✓ **优秀**: PDR ≥ 90%, 时延 ≤ 50ms
- ✓ **良好**: PDR ≥ 85%, 时延 ≤ 100ms  
- ⚠ **一般**: PDR ≥ 70%, 时延 ≤ 200ms
- ✗ **较差**: 不满足以上条件

### 方法2: 对比多个算法

**使用自动对比脚本**:
```bash
# 运行所有策略并生成对比报告
./compare_strategies.sh
```

**或手动对比**:
```bash
# 1. 分别运行各个算法
./run_uav_simulation.sh static 15 3 200
./run_uav_simulation.sh greedy 15 3 200
./run_uav_simulation.sh graph_coloring 15 3 200
./run_uav_simulation.sh interference_aware 15 3 200

# 2. 使用对比工具
python3 compare_algorithms.py \
    output/uav_resource_allocation_static_15uavs_3ch/ \
    output/uav_resource_allocation_greedy_15uavs_3ch/ \
    output/uav_resource_allocation_graph_coloring_15uavs_3ch/ \
    output/uav_resource_allocation_interference_aware_15uavs_3ch/
```

**输出结果**:
- `comparison_table.txt` - 指标对比表格
- `comparison_report.txt` - 详细对比报告
- `pdr_comparison.png` - PDR柱状图对比
- `delay_comparison.png` - 时延柱状图对比
- `radar_comparison.png` - 综合性能雷达图

### 方法3: 多场景评估

测试算法在不同场景下的表现：

```bash
# 场景A: 标准场景
./run_uav_simulation.sh graph_coloring 15 3 200

# 场景B: 高密度场景
./run_uav_simulation.sh graph_coloring 30 4 200

# 场景C: 信道受限场景
./run_uav_simulation.sh graph_coloring 20 2 200

# 场景D: 大区域场景（修改config文件area_size=1000）
./run_uav_simulation.sh graph_coloring 15 3 200
```

---

## 📈 评估指标详解

### 主要性能指标

| 指标 | 计算公式 | 目标值 | 说明 |
|-----|---------|--------|------|
| **PDR** | 接收包数/发送包数 × 100% | ≥ 85% | 网络可靠性 |
| **时延** | 平均(接收时间-发送时间) | ≤ 100ms | 实时性 |
| **吞吐量** | 接收字节数×8/仿真时长 | 越高越好 | 容量 |

### 资源效率指标

| 指标 | 说明 | 评价 |
|-----|------|------|
| **信道利用率** | 各信道分配的节点数分布 | 越均衡越好 |
| **功率效率** | 吞吐量/平均功率 | 越高越好 |
| **网络连通性** | 活跃链路数/最大可能链路数 | 适中为好 |

### 算法开销指标

| 指标 | 说明 |
|-----|------|
| **计算时间** | 资源分配算法执行时间 |
| **重配置率** | 信道变更频率 |
| **收敛速度** | 达到稳态的速度 |

---

## 🎯 算法效果判断指南

### 判断算法是否达标

```python
# 伪代码
if PDR >= 85% AND 时延 <= 100ms:
    print("✓ 算法满足QoS要求")
    if PDR >= 90% AND 时延 <= 50ms:
        print("✓✓ 算法性能优秀")
else:
    print("✗ 算法不满足QoS要求")
```

### 选择最佳算法

考虑三个维度：

1. **性能**: PDR、时延、吞吐量
2. **效率**: 资源利用率、计算开销
3. **鲁棒性**: 不同场景下的稳定性

**推荐决策树**:
```
计算资源是否受限？
├─ 是 → 使用 Static 或 Greedy
└─ 否 → QoS要求是否严格？
    ├─ 非常严格 → 使用 Interference Aware
    └─ 一般 → 使用 Graph Coloring（推荐）
```

---

## 📋 评估报告模板

### 实验配置
- 节点数量: 15
- 信道数量: 3
- 仿真时长: 200秒
- 移动模型: RandomWalk2d
- 通信范围: 150米

### 性能对比表

| 算法 | PDR | 时延(ms) | 吞吐量(Mbps) | QoS满足 |
|-----|-----|---------|-------------|---------|
| Static | 72.3% | 145.2 | 8.7 | ✗ |
| Greedy | 78.5% | 112.8 | 10.3 | ✗ |
| Graph Coloring | **87.2%** | **92.5** | **13.1** | ✓ |
| Interference Aware | 89.1% | 85.3 | 14.2 | ✓ |

### 结论
- **最佳PDR**: Interference Aware (89.1%)
- **最低时延**: Interference Aware (85.3ms)
- **最高吞吐量**: Interference Aware (14.2 Mbps)
- **综合推荐**: Graph Coloring (性能与开销平衡最好)

---

## 🚀 快速使用示例

### 示例1: 评估单个算法
```bash
# 运行Graph Coloring算法
./run_uav_simulation.sh graph_coloring 15 3 200

# 查看结果
cat output/uav_resource_allocation_graph_coloring_15uavs_3ch/summary.txt

# 关键指标:
# - 平均PDR: 87.2% (✓ 满足 ≥85%)
# - 平均时延: 92.5 ms (✓ 满足 ≤100ms)
# 结论: 满足QoS要求
```

### 示例2: 对比所有算法
```bash
# 一键对比所有策略
./compare_strategies.sh

# 查看对比报告
cat output/strategy_comparison_xxx/comparison_report.txt

# 查看对比图表
ls output/strategy_comparison_xxx/*/figures/
```

### 示例3: 课程大作业完整评估
```bash
# 1. 运行基础实验
./run_uav_simulation.sh graph_coloring 15 3 200

# 2. 运行对比实验
./compare_strategies.sh

# 3. 查看评估指南
cat PERFORMANCE_EVALUATION_GUIDE.md

# 4. 生成对比分析
python3 compare_algorithms.py output/*_15uavs_3ch/

# 5. 撰写实验报告（参考生成的report文件）
```

---

## 📚 相关文档

- **完整评估指南**: `PERFORMANCE_EVALUATION_GUIDE.md`
- **快速入门**: `QUICKSTART.md`
- **技术文档**: `UAV_README.md`
- **对比工具使用**: `python3 compare_algorithms.py --help`

---

## ❓ 常见问题

**Q: 如何判断算法好坏？**
A: 主要看三个指标：PDR ≥ 85%, 时延 ≤ 100ms, 吞吐量越高越好

**Q: Graph Coloring为什么是推荐算法？**
A: 因为它在性能、效率、复杂度三者间达到最佳平衡

**Q: 如何提高PDR？**
A: 1) 增加信道数量 2) 使用更好的算法 3) 调整功率控制参数

**Q: 实验要做几次？**
A: 基础：1次；标准：3-5次（不同种子）；严格：10次以上

**Q: 如何生成课程报告？**
A: 运行实验 → 使用compare_algorithms.py生成对比 → 参考生成的report文件撰写

---

**总结: 使用以上方法，你可以全面、科学地评估各种资源分配算法的效果！**

