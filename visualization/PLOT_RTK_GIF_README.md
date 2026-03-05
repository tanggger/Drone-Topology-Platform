# RTK轨迹GIF动画生成器使用说明

## 📋 简介

`plot_rtk_gif.py` 是一个专门用于生成无人机RTK轨迹3D动画GIF的工具。它能够：
- 从 `mobility_trace_*.txt` 格式的轨迹文件中读取数据
- 自动推断通信拓扑（基于节点间距离）
- 生成精美的3D动画GIF，展示无人机飞行轨迹和通信链路

---

## 🚀 快速开始

### 基础用法

```bash
python3 visualization/plot_rtk_gif.py \
    data_rtk/mobility_trace_cross.txt \
    -o output.gif
```

### 推荐配置（使用拓扑文件）⭐

```bash
python3 visualization/plot_rtk_gif.py \
    data_rtk/mobility_trace_cross.txt \
    -o visualization/output/rtk_animation.gif \
    --topology-file benchmark/cross_Hard/topology-changes.txt \
    --time-start 0 --time-end 200 \
    --time-step 2 --tail-length 30 --fps 10 \
    --node-size 25 --link-width 0.8 \
    --no-labels
```

### 基于距离推断（无拓扑文件）

```bash
python3 visualization/plot_rtk_gif.py \
    data_rtk/mobility_trace_cross.txt \
    -o visualization/output/rtk_animation.gif \
    --time-start 800 --time-end 1100 \
    --time-step 2 --comm-range 80 \
    --tail-length 30 --fps 10 \
    --node-size 25 --link-width 0.8 \
    --no-labels
```

---

## 📊 参数详解

### 必需参数

| 参数 | 说明 | 示例 |
|------|------|------|
| `trace_file` | RTK轨迹文件路径 | `data_rtk/mobility_trace_cross.txt` |

### 输出参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `-o, --output` | str | `rtk_animation.gif` | 输出GIF文件路径 |

### 拓扑文件参数 ⭐

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `-t, --topology-file` | str | `None` | 拓扑文件路径（topology-changes.txt），如果提供则从文件读取真实通信链路 |

**拓扑文件说明：**
- 当指定 `--topology-file` 时，通信链路从文件读取（推荐）
- 当不指定时，基于 `--comm-range` 距离阈值自动推断链路
- 拓扑文件位置：`benchmark/*/topology-changes.txt`

### 时间控制参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--time-start` | float | `None` | 动画起始时间（秒），`None`表示使用数据起始时间 |
| `--time-end` | float | `None` | 动画结束时间（秒），`None`表示使用数据结束时间 |
| `--time-step` | float | `1.0` | 时间采样步长（秒）。越小越平滑但文件越大 |

**时间参数说明：**
- 如果数据时间范围是 0-2900秒，你可以选择任意时间段
- `time-step` 建议值：
  - `1.0-2.0`：标准质量，文件适中
  - `0.5`：高质量，文件较大
  - `3.0-5.0`：快速预览，文件小

### 通信拓扑参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--comm-range` | float | `50.0` | 通信距离阈值（米）。当两个节点距离 ≤ 此值时，显示通信链路 |

**通信距离建议：**
- 根据实际通信设备能力设置
- 典型值：`30-100` 米
- 值越大，显示的链路越多

### 视觉效果参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--node-size` | int | `30` | 节点（无人机）大小。值越小节点越小 |
| `--link-width` | float | `1.5` | 通信链路线宽。值越小线越细 |
| `--tail-length` | float | `20.0` | 轨迹尾迹长度（秒）。显示最近N秒的飞行轨迹 |
| `--fps` | int | `10` | 动画帧率（帧/秒）。值越大动画越流畅但文件越大 |

**视觉效果建议：**
- **节点大小**：
  - `20-30`：小节点，适合密集场景
  - `40-60`：中等节点
  - `80-120`：大节点，适合稀疏场景
- **连线宽度**：
  - `0.8-1.5`：细线，清晰不遮挡
  - `2.0-3.0`：中等粗细
  - `4.0-6.0`：粗线，突出显示
- **尾迹长度**：
  - `10-20`：短尾迹，适合快速移动
  - `30-50`：长尾迹，显示更多历史轨迹

### 渲染质量参数

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `--dpi` | int | `120` | 输出分辨率（DPI）。值越大质量越高但文件越大 |
| `--figsize` | str | `12,10` | 图形尺寸（宽,高，英寸） |

**质量参数建议：**
- **DPI**：
  - `100`：快速预览，文件小
  - `120-150`：标准质量（推荐）
  - `200-300`：高质量，文件大
- **图形尺寸**：
  - `10,8`：小尺寸，文件小
  - `12,10`：标准尺寸（推荐）
  - `16,12`：大尺寸，适合演示

### 主题和样式参数

| 参数 | 说明 |
|------|------|
| `--light-mode` | 使用亮色主题（默认暗色主题） |
| `--no-rotate` | 禁用相机自动旋转 |
| `--no-glow` | 禁用发光效果 |
| `--no-labels` | 不显示节点ID标签 |
| `--no-equal-aspect` | 禁用等比例坐标轴 |
| `--auto-zoom` | 动态调整视野范围（跟随节点位置） |

**样式说明：**
- **暗色主题**（默认）：霓虹色系，适合演示
- **亮色主题**：柔和配色，适合论文/报告
- **自动旋转**：相机缓慢旋转，展示3D效果
- **发光效果**：节点和连线有轻微发光，增强视觉效果
- **动态缩放**：视野自动跟随节点，适合节点移动范围大的场景

---

## 📝 使用示例

### 示例1：使用拓扑文件（推荐）⭐

```bash
python3 visualization/plot_rtk_gif.py \
    data_rtk/mobility_trace_cross.txt \
    -o visualization/output/with_topo.gif \
    --topology-file benchmark/cross_Hard/topology-changes.txt \
    --time-start 0 --time-end 150 \
    --time-step 2 --fps 10 --dpi 180 \
    --node-size 25 --link-width 0.8 \
    --no-labels
```

**特点：**
- 使用真实的通信拓扑数据
- 链路来自仿真结果，而非距离推断
- 适合展示仿真结果

### 示例2：快速预览（小文件）

```bash
python3 visualization/plot_rtk_gif.py \
    data_rtk/mobility_trace_cross.txt \
    -o preview.gif \
    --time-start 800 --time-end 900 \
    --time-step 5 --fps 8 --dpi 100 \
    --node-size 25 --link-width 0.8
```

**特点：**
- 100秒动画
- 文件小，生成快
- 适合快速查看效果

### 示例3：标准质量

```bash
python3 visualization/plot_rtk_gif.py \
    data_rtk/mobility_trace_cross.txt \
    -o standard.gif \
    --time-start 800 --time-end 1100 \
    --time-step 2 --fps 10 --dpi 120 \
    --comm-range 80 --tail-length 30 \
    --node-size 30 --link-width 1.5 \
    --no-labels
```

**特点：**
- 300秒动画
- 标准质量
- 无标签，清晰

### 示例3：高质量演示

```bash
python3 visualization/plot_rtk_gif.py \
    data_rtk/mobility_trace_cross.txt \
    -o high_quality.gif \
    --time-start 500 --time-end 1200 \
    --time-step 1 --fps 12 --dpi 150 \
    --comm-range 60 --tail-length 40 \
    --node-size 35 --link-width 2.0 \
    --figsize 14,11
```

**特点：**
- 700秒动画
- 高分辨率
- 适合正式演示

### 示例4：亮色主题（论文用）

```bash
python3 visualization/plot_rtk_gif.py \
    data_rtk/mobility_trace_cross.txt \
    -o paper_style.gif \
    --time-start 800 --time-end 1100 \
    --time-step 2 --fps 10 \
    --light-mode --no-glow \
    --node-size 28 --link-width 1.3
```

**特点：**
- 亮色背景
- 无发光效果
- 适合论文/报告

### 示例5：动态缩放视野

```bash
python3 visualization/plot_rtk_gif.py \
    data_rtk/mobility_trace_cross.txt \
    -o autozoom.gif \
    --time-start 800 --time-end 1100 \
    --auto-zoom \
    --node-size 30 --link-width 1.5 \
    --no-labels
```

**特点：**
- 视野自动跟随节点
- 适合节点移动范围大的场景

---

## 🎯 参数选择指南

### 根据场景选择参数

| 场景 | 推荐参数组合 |
|------|-------------|
| **密集编队** | `--node-size 20-25 --link-width 1.0-1.2 --comm-range 30-50` |
| **稀疏编队** | `--node-size 40-50 --link-width 2.0-3.0 --comm-range 80-100` |
| **快速移动** | `--tail-length 15-20 --time-step 1.0-1.5` |
| **缓慢移动** | `--tail-length 40-50 --time-step 2.0-3.0` |
| **论文/报告** | `--light-mode --no-glow --dpi 150` |
| **演示/展示** | `--dpi 120 --fps 12 --tail-length 30` |

### 根据数据特点选择时间段

**如何选择最佳时间段？**

1. **查看数据统计**：
   ```bash
   # 分析各时间段的坐标跨度
   python3 -c "
   import pandas as pd
   # ... 分析代码 ...
   "
   ```

2. **推荐时间段**（基于cross编队）：
   - `800-1100秒`：所有节点活跃，移动范围大 ⭐
   - `500-800秒`：节点逐渐活跃
   - `0-200秒`：只有部分节点，移动范围小

3. **测试不同时间段**：
   ```bash
   # 快速测试多个时间段
   for start in 0 300 500 800; do
       python3 visualization/plot_rtk_gif.py \
           data_rtk/mobility_trace_cross.txt \
           -o test_${start}.gif \
           --time-start $start --time-end $((start+200)) \
           --time-step 3 --fps 8
   done
   ```

---

## ⚙️ 高级配置

### 批量生成多个编队

```bash
for formation in cross line triangle v_formation; do
    python3 visualization/plot_rtk_gif.py \
        data_rtk/mobility_trace_${formation}.txt \
        -o visualization/output/${formation}.gif \
        --time-start 800 --time-end 1100 \
        --time-step 2 --comm-range 80 \
        --node-size 30 --link-width 1.5 \
        --no-labels
done
```

### 生成不同质量版本

```bash
# 低质量预览
python3 visualization/plot_rtk_gif.py ... --dpi 80 --fps 8 -o preview.gif

# 标准质量
python3 visualization/plot_rtk_gif.py ... --dpi 120 --fps 10 -o standard.gif

# 高质量
python3 visualization/plot_rtk_gif.py ... --dpi 200 --fps 12 -o high_quality.gif
```

---

## 📊 输出文件说明

### 文件大小估算

| 参数组合 | 预计文件大小 |
|----------|-------------|
| 100秒，time-step=5，dpi=100 | ~1-2 MB |
| 300秒，time-step=2，dpi=120 | ~5-7 MB |
| 700秒，time-step=1，dpi=150 | ~15-20 MB |

### 优化文件大小

如果文件太大，可以：
1. 增加 `--time-step`（如从1.0改为2.0）
2. 降低 `--dpi`（如从150改为100）
3. 缩短时间范围
4. 降低 `--fps`（如从12改为8）

---

## 🔧 常见问题

### Q1: 节点都挤在一起看不清？

**原因**：选择的时间段节点移动范围太小

**解决**：
1. 使用 `--auto-zoom` 动态缩放
2. 选择移动范围大的时间段（如800-1100秒）
3. 减小 `--node-size`

### Q2: 连线太细/太粗？

**解决**：调整 `--link-width` 参数
- 更细：`0.8-1.2`
- 标准：`1.5-2.0`
- 更粗：`3.0-5.0`

### Q3: 动画播放太快/太慢？

**解决**：调整 `--fps` 参数
- 慢速：`6-8`
- 标准：`10`
- 快速：`12-15`

### Q4: 文件太大？

**解决**：
1. 增加 `--time-step`（减少帧数）
2. 降低 `--dpi`
3. 缩短时间范围

### Q5: 看不到通信链路？

**原因**：`--comm-range` 太小，或节点距离太远

**解决**：
1. 增大 `--comm-range`（如从50改为80）
2. 检查节点间实际距离
3. 选择节点更密集的时间段

---

## 📚 相关文件

- **输入数据格式**：`data_rtk/mobility_trace_*.txt`
  - 格式：`time,nodeId,x,y,z`
  - 逗号分隔，可包含注释行（以`#`开头）

- **其他可视化工具**：
  - `plot_rtk_3d_animation.py`：交互式HTML动画
  - `plot_rtk_trajectory.py`：静态轨迹图
  - `plot_topology.py`：拓扑可视化

---

## 💡 提示

1. **首次使用**：先用小时间段（如100秒）和低质量参数测试
2. **参数调优**：根据实际数据特点调整参数
3. **批量处理**：使用shell脚本批量生成多个场景
4. **文件管理**：大文件建议放在 `visualization/output/` 目录

---

**最后更新**：2025-01-04  
**版本**：v1.0
