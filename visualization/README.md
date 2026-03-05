# 可视化工具集 (Visualization Tools)

本目录包含用于可视化无人机RTK轨迹和通信拓扑的Python脚本。

## 📁 文件说明

### 🎬 核心动画脚本（推荐）

| 脚本 | 功能 | 输入格式 | 输出格式 |
|------|------|----------|----------|
| **`plot_rtk_3d_animation.py`** ⭐⭐ | RTK轨迹动画（推荐） | `mobility_trace_*.txt` | HTML/GIF/MP4 |
| `plot_3d_animation_advanced.py` ⭐ | 高级3D动画 | `node-positions.csv` | HTML/GIF/MP4 |
| `plot_3d_animation.py` | 标准3D动画 | `node-positions.csv` | HTML/GIF/MP4 |

### 📊 静态图脚本

| 脚本 | 功能 |
|------|------|
| `plot_rtk_trajectory.py` | RTK轨迹可视化（3D/2D） |
| `plot_topology.py` | 通信拓扑可视化 |
| `plot_combined.py` | 轨迹+拓扑组合图 |

---

## 🚀 快速开始

### 安装依赖

```bash
# 基础依赖
pip install pandas numpy matplotlib

# 高级动画（推荐）
pip install plotly

# 专业级渲染（可选）
pip install pyvista
```

---

## 🎬 创建RTK轨迹3D动画（推荐）⭐⭐

专门处理 `data_rtk/mobility_trace_*.txt` 格式的轨迹文件。

### 方式1: 交互式HTML动画（推荐，可在浏览器中旋转查看）

```bash
# 基础用法 - 生成前300秒的动画
python3 visualization/plot_rtk_3d_animation.py \
    data_rtk/mobility_trace_cross.txt \
    -o visualization/output/rtk_animation.html \
    --time-end 300

# 自定义通信距离阈值（默认50m）
python3 visualization/plot_rtk_3d_animation.py \
    data_rtk/mobility_trace_cross.txt \
    -o visualization/output/rtk_animation.html \
    --comm-range 30 --time-end 200
```

### 方式2: 高质量GIF动画

```bash
python3 visualization/plot_rtk_3d_animation.py \
    data_rtk/mobility_trace_cross.txt \
    -o visualization/output/rtk_animation.gif \
    --renderer matplotlib \
    --time-end 100 --time-step 2 --fps 10
```

### 方式3: 完整参数示例

```bash
python3 visualization/plot_rtk_3d_animation.py \
    data_rtk/mobility_trace_cross.txt \
    -o visualization/output/rtk_custom.html \
    --comm-range 25 \
    --time-start 0 --time-end 500 --time-step 3 \
    --tail-length 30 \
    --node-size 16 --link-width 5 \
    --fps 12
```

### RTK动画参数说明

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--comm-range` | 通信距离阈值（米），小于此距离显示链路 | 50 |
| `--time-start` | 动画起始时间（秒） | 0 |
| `--time-end` | 动画结束时间（秒） | 文件最大时间 |
| `--time-step` | 时间采样步长（秒） | 1.0 |
| `--tail-length` | 尾迹长度（秒） | 30 |
| `--fps` | 播放帧率 | 10 |
| `--renderer` | 渲染器 (plotly/matplotlib/both) | plotly |
| `--light-mode` | 使用亮色主题 | 暗色 |
| `--no-glow` | 禁用发光效果 | 启用 |
| `--dpi` | GIF分辨率 | 150 |

### 可用的RTK轨迹文件

```
data_rtk/
├── mobility_trace_cross.txt      # Cross编队
├── mobility_trace_line.txt       # Line编队
├── mobility_trace_triangle.txt   # Triangle编队
└── mobility_trace_v_formation.txt # V形编队
```

---

## 🎬 使用node-positions.csv的动画

### 方式1: 交互式HTML动画（推荐，可在浏览器中旋转）

```bash
python3 visualization/plot_3d_animation_advanced.py \
    benchmark/cross_Easy/node-positions.csv \
    benchmark/cross_Easy/topology-changes.txt \
    -o visualization/output/uav_animation.html
```

### 方式2: 高质量GIF动画

```bash
python3 visualization/plot_3d_animation_advanced.py \
    benchmark/cross_Easy/node-positions.csv \
    benchmark/cross_Easy/topology-changes.txt \
    -o visualization/output/uav_animation.gif \
    --renderer matplotlib
```

### 方式3: 亮色主题

```bash
python3 visualization/plot_3d_animation_advanced.py \
    benchmark/cross_Easy/node-positions.csv \
    benchmark/cross_Easy/topology-changes.txt \
    -o visualization/output/uav_light.html \
    --light-mode
```

### 方式4: 自定义参数

```bash
python3 visualization/plot_3d_animation_advanced.py \
    benchmark/cross_Easy/node-positions.csv \
    benchmark/cross_Easy/topology-changes.txt \
    -o visualization/output/uav_custom.html \
    --fps 15 \
    --tail-length 15 \
    --node-size 18 \
    --link-width 5
```

### 参数说明

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--renderer` | 渲染器 (plotly/matplotlib/both) | plotly |
| `--fps` | 帧率 | 12 |
| `--tail-length` | 尾迹长度（秒） | 10 |
| `--node-size` | 节点大小 | 14 |
| `--link-width` | 链路线宽 | 4 |
| `--light-mode` | 亮色主题 | 暗色 |
| `--no-rotate` | 禁用相机旋转 | 启用 |
| `--no-glow` | 禁用发光效果 | 启用 |
| `--dpi` | GIF/MP4分辨率 | 200 |

### 视觉效果特性

**暗色主题（默认）：**
- 🌈 霓虹色系节点颜色
- ✨ 发光尾迹效果
- 💫 脉冲链路动画
- 🎥 自动相机旋转

**亮色主题：**
- 🎨 柔和的节点颜色
- 📍 清晰的节点标签
- 📊 适合论文/报告使用

---

## 📊 静态图可视化

### RTK轨迹可视化

```bash
# 绘制所有视图（3D + 3个2D投影）
python3 visualization/plot_rtk_trajectory.py \
    benchmark/cross_Easy/node-positions.csv \
    --output-dir visualization/output/rtk_trajectory

# 只绘制3D视图
python3 visualization/plot_rtk_trajectory.py \
    benchmark/cross_Easy/node-positions.csv \
    --view 3d --output visualization/output/rtk_3d.png
```

### 通信拓扑可视化

```bash
# 绘制静态拓扑图
python3 visualization/plot_topology.py \
    benchmark/cross_Easy/topology-changes.txt \
    --positions benchmark/cross_Easy/node-positions.csv \
    --mode static --output visualization/output/topology_static.png

# 绘制拓扑统计图
python3 visualization/plot_topology.py \
    benchmark/cross_Easy/topology-changes.txt \
    --mode stats --output visualization/output/topology_stats.png
```

### 组合可视化

```bash
# 绘制3D组合图
python3 visualization/plot_combined.py \
    benchmark/cross_Easy/node-positions.csv \
    benchmark/cross_Easy/topology-changes.txt \
    --view 3d --output visualization/output/combined_3d.png
```

---

## 📊 输入数据格式

### node-positions.csv
```csv
time_s,nodeId,x,y,z
1.0,0,0.0204039,0.00660787,0.00697727
1.0,1,1.8113,0.127791,0.206522
2.0,0,0.0204039,0.00660787,0.00697727
...
```

### topology-changes.txt
```
0-5: Node0-Node13, Node2-Node14
5-10: Node0-Node11, Node5-Node12, Node7-Node13
10-15: Node3-Node7, Node4-Node7, Node6-Node10
...
```

---

## 🎨 输出示例

### 3D动画特性
- **节点**: 彩色球体，表示无人机位置
- **轨迹**: 渐变尾迹，显示飞行历史
- **链路**: 红色连线，表示当前通信拓扑
- **信息面板**: 实时显示时间、链路数等信息

### 交互功能（HTML输出）
- 🖱️ 拖拽旋转3D视角
- 🔍 滚轮缩放
- ▶️ 播放/暂停动画
- 📊 时间滑块控制

---

## 📝 批量处理示例

```bash
# 为所有场景生成动画
for scenario in cross_Easy cross_Moderate cross_Hard line_Easy line_Moderate line_Hard; do
    python3 visualization/plot_3d_animation_advanced.py \
        benchmark/$scenario/node-positions.csv \
        benchmark/$scenario/topology-changes.txt \
        -o visualization/output/${scenario}_animation.html
done
```

---

## 📦 依赖库

| 库 | 用途 | 必需 |
|----|------|------|
| pandas | 数据处理 | ✅ |
| numpy | 数值计算 | ✅ |
| matplotlib | 基础绑图/GIF | ✅ |
| plotly | 交互式HTML | 推荐 |
| pyvista | 专业级3D渲染 | 可选 |

```bash
pip install pandas numpy matplotlib plotly
```

---

## 📚 相关文档

- RTK数据处理: `rtk/README.md`
- 基准测试: `BENCHMARK_QUICK_REF.md`
- 系统运行逻辑: `系统运行逻辑分析.md`
