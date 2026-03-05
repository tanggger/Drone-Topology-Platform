# 🚀 快速开始指南

## 轨迹数据位置

RTK轨迹数据位于 `data_rtk/` 目录：

```
data_rtk/
├── mobility_trace_cross.txt        (1.4M, 44028行)
├── mobility_trace_line.txt         (1.4M)
├── mobility_trace_triangle.txt     (1.4M)
└── mobility_trace_v_formation.txt  (1.6M)
```

## 编译命令

```bash
# 1. 配置
./ns3 configure --enable-examples

# 2. 编译
./ns3 build rtk_benchmark

# 如果遇到错误，清理后重新编译
./ns3 clean && ./ns3 build
```

## 运行命令

### 方式1：快速测试单个场景（推荐）

```bash
# Easy模式 - Cross编队
./run_single_benchmark.sh cross easy

# Moderate模式 - Line编队
./run_single_benchmark.sh line moderate

# Hard模式 - V编队
./run_single_benchmark.sh v_formation hard
```

### 方式2：直接运行ns-3命令

```bash
# 使用默认路径（会自动找到data_rtk目录）
./ns3 run "rtk_benchmark --formation=cross --difficulty=easy"

# 或指定完整路径
./ns3 run "rtk_benchmark --trajectory=data_rtk/mobility_trace_cross.txt --formation=cross --difficulty=easy"
```

### 方式3：批量运行所有12个场景

```bash
# 运行所有场景（4种形态 × 3种难度 = 12个数据集）
./run_benchmark.sh
```

预计耗时：20-40分钟

## 轨迹文件命名规则

脚本会自动按以下规则查找轨迹文件：

1. **优先查找**：`data_rtk/mobility_trace_{formation}.txt`
   - `data_rtk/mobility_trace_cross.txt`
   - `data_rtk/mobility_trace_line.txt`
   - `data_rtk/mobility_trace_triangle.txt`
   - `data_rtk/mobility_trace_v_formation.txt`

2. **备用文件**：`data_rtk/mobility_trace.txt`（如果特定文件不存在）

## 输出结果

所有结果保存在 `benchmark/` 目录：

```
benchmark/
├── cross_Easy/              # Cross编队-简单模式
│   ├── benchmark-config.txt
│   ├── flow-stats.csv
│   ├── node-positions.csv
│   ├── topology-changes.txt
│   └── ...
├── cross_Moderate/
├── cross_Hard/
├── line_Easy/
├── ... (共12个目录)
└── analysis_plots/          # 分析脚本生成的图表
```

## 分析结果

```bash
# 安装Python依赖（首次运行）
pip3 install pandas numpy matplotlib

# 分析所有数据集
python3 analyze_benchmark.py

# 可视化单个轨迹
python3 plot_trajectory.py benchmark/cross_Easy/node-positions.csv
```

## 完整测试流程

```bash
# 第1步：编译
./ns3 build rtk_benchmark

# 第2步：运行一个测试场景（约2-3分钟）
./run_single_benchmark.sh cross easy

# 第3步：查看输出
ls -lh benchmark/cross_Easy/
cat benchmark/cross_Easy/benchmark-config.txt

# 第4步：查看流统计（前20行）
head -20 benchmark/cross_Easy/flow-stats.csv

# 第5步（可选）：运行所有场景
./run_benchmark.sh

# 第6步（可选）：分析所有结果
python3 analyze_benchmark.py
```

## 三种难度说明

| 难度 | 特点 | 适用场景 |
|------|------|----------|
| **easy** | 理想信道、无干扰、低负载 | 算法功能验证、性能基线 |
| **moderate** | 城市环境、周期漂移、视频流 | 实际应用测试、鲁棒性评估 |
| **hard** | 复杂多径、频繁漂移、高负载+干扰 | 极限测试、性能上限探索 |

## 常见问题

### Q: 编译错误怎么办？
```bash
./ns3 clean
./ns3 configure --enable-examples
./ns3 build
```

### Q: 找不到轨迹文件？
确保文件存在：
```bash
ls -lh data_rtk/mobility_trace_*.txt
```

### Q: 修改轨迹文件路径？
编辑运行时参数：
```bash
./ns3 run "rtk_benchmark --trajectory=你的路径/文件名.txt --formation=cross --difficulty=easy"
```

### Q: 如何只测试一种形态的三种难度？
```bash
./run_single_benchmark.sh cross easy
./run_single_benchmark.sh cross moderate
./run_single_benchmark.sh cross hard
```

## 下一步

- 📖 详细文档：`BENCHMARK_README.md`
- 📋 快速参考：`BENCHMARK_QUICK_REF.md`
- 📁 文件说明：`BENCHMARK_FILES.txt`
- ✅ 完成说明：`BENCHMARK_SETUP_COMPLETE.md`

---

**祝实验顺利！** 🚁📡📊

