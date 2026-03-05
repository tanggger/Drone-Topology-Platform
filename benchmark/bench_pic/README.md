# Benchmark 可视化工具集

本目录包含所有 benchmark 数据集的可视化工具和生成的图片。

## 📁 目录结构

```
bench_pic/
├── README.md                          # 本文件
├── visualize_link_survival.py         # 链路存活条形谱绘图脚本
├── visualize_topology_evolution.py    # 拓扑演化漫画绘图脚本
├── link_survival/                     # 链路存活图片存放目录
└── topology_evolution/                # 拓扑演化图片存放目录
    ├── link_survival_v_formation_Easy.png
    ├── link_survival_v_formation_Moderate.png
    ├── link_survival_v_formation_Hard.png
    ├── link_survival_cross_Easy.png
    ├── link_survival_cross_Moderate.png
    ├── link_survival_cross_Hard.png
    ├── link_survival_line_Easy.png
    ├── link_survival_line_Moderate.png
    ├── link_survival_line_Hard.png
    ├── link_survival_triangle_Easy.png
    ├── link_survival_triangle_Moderate.png
    ├── link_survival_triangle_Hard.png
    └── link_survival_comparison_grid.png
```

## 🚀 快速使用

### 1. 生成链路存活条形谱

```bash
cd /mnt/e/Simulator/ns3/workspace/ns-allinone-3.43/ns-3.43/benchmark/bench_pic
python3 visualize_link_survival.py
```

这将：
- 解析所有12个数据集的 `topology-changes.txt` 文件
- 生成12张单数据集详细图
- 生成1张 4×3 网格对比图
- 所有图片保存在 `link_survival/` 文件夹

### 2. 生成拓扑演化漫画

```bash
cd /mnt/e/Simulator/ns3/workspace/ns-allinone-3.43/ns-3.43/benchmark/bench_pic
python3 visualize_topology_evolution.py
```

这将：
- 解析 `topology-changes.txt` 和 `node-positions.csv` 文件
- 生成12张演化漫画（每张10帧）
- 生成1张对比条带图（12×3布局）
- 所有图片保存在 `topology_evolution/` 文件夹

## 📊 可视化工具说明

### 1. 链路存活条形谱 (visualize_link_survival.py)

**功能**：
- 可视化网络拓扑动态变化
- X轴=时间，Y轴=链路对，深色条形表示链路存在
- 按链路活跃度排序，最活跃链路在顶部

**输出**：
- 单数据集图：完整展示所有105条链路
- 对比网格图：快速对比12个场景

**参数调整**：
```python
# 修改每个子图显示的链路数量（默认40条）
links_per_subplot=40

# 修改对比网格图显示的链路数量（默认前20条）
top_links = sorted(...)[:20]
```

### 2. 拓扑演化漫画 (visualize_topology_evolution.py)

**功能**：
- 像连环画一样展示网络拓扑随时间演变
- 节点位置基于实际GPS坐标
- 显示链路连接和网络连通性

**输出**：
- 演化漫画：10帧展示整个仿真过程
- 对比条带：展示开始、中间、结束三个时刻

**视觉元素**：
- 彩色节点：15个无人机，不同颜色
- 黑色连线：活跃的通信链路
- 统计信息：链路数量、连通分量数

## 🎨 配色方案

### 难度颜色
- 🟢 **Easy**: `#2ecc71` (绿色) - 理想环境
- 🟠 **Moderate**: `#f39c12` (橙色) - 城市环境
- 🔴 **Hard**: `#e74c3c` (红色) - 极端环境

### 编队颜色（备用）
- 🔵 **V-Formation**: `#3498db` (蓝色)
- 🟣 **Cross**: `#9b59b6` (紫色)
- 🟦 **Line**: `#1abc9c` (青色)
- 🟧 **Triangle**: `#e67e22` (橙黄色)

## 📝 添加新的可视化工具

当你需要添加其他类型的可视化时：

1. **创建新的子文件夹**
```bash
mkdir -p bench_pic/new_visualization_type
```

2. **编写绘图脚本**
```python
# bench_pic/visualize_xxx.py
class XXXVisualizer:
    def __init__(self, benchmark_dir):
        self.output_dir = os.path.join(benchmark_dir, 'bench_pic', 'xxx')
        os.makedirs(self.output_dir, exist_ok=True)
```

3. **更新本 README**

## 🛠️ 依赖项

所有脚本需要以下 Python 包：
```bash
pip3 install matplotlib numpy pandas
```

## 📖 图表解读

详细的图表解读说明请参考：
- 链路存活图：见各图片同目录下的说明
- 更多技术细节：查看 `../README_METRICS.md`

## ⚙️ 高级用法

### 批量生成所有可视化

创建一个主脚本：
```bash
#!/bin/bash
python3 visualize_link_survival.py
# python3 visualize_xxx.py  # 添加其他可视化
```

### 自定义输出格式

在脚本中修改：
```python
plt.savefig(output_path, dpi=300, format='pdf')  # 改为PDF格式，更高分辨率
```

---

**最后更新**: 2025-10-12
**维护者**: RTK Benchmark Team

