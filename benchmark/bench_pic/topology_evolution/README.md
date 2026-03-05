# 拓扑演化漫画可视化

本目录包含 12 个数据集的拓扑演化漫画（Topology Evolution Comic Strip）可视化图表。

## 📊 包含的图表

### 1. 单数据集演化漫画（12张）

每个数据集生成一张包含10帧的"连环画"，展示网络拓扑随时间的演变。

| 编队类型 | Easy | Moderate | Hard |
|---------|------|----------|------|
| **V-Formation** | topology_evolution_v_formation_Easy.png | topology_evolution_v_formation_Moderate.png | topology_evolution_v_formation_Hard.png |
| **Cross** | topology_evolution_cross_Easy.png | topology_evolution_cross_Moderate.png | topology_evolution_cross_Hard.png |
| **Line** | topology_evolution_line_Easy.png | topology_evolution_line_Moderate.png | topology_evolution_line_Hard.png |
| **Triangle** | topology_evolution_triangle_Easy.png | topology_evolution_triangle_Moderate.png | topology_evolution_triangle_Hard.png |

### 2. 对比条带图（1张）

**文件名**: `topology_evolution_comparison.png`
- 12×3 网格布局（12个数据集 × 3个关键时刻）
- 展示每个场景的：开始、中间、结束时刻
- 快速对比不同场景的拓扑演化特征

---

## 🎯 图表说明

### 视觉元素

#### 节点（Nodes）
- 🔵🔴🟢🟠🟣 **彩色圆圈**: 代表15个无人机节点
- **数字标签**: 节点ID（0-14）
- **位置**: 根据实际飞行轨迹动态变化

#### 链路（Links）
- **黑色连线**: 表示两个节点之间存在通信链路
- **线条粗细**:
  - Easy: 粗线（2.0px，高可靠）
  - Moderate: 中等（1.5px，中等可靠）
  - Hard: 细线（1.0px，低可靠）
- **透明度**: 反映链路质量（越透明质量越差）

#### 时间标签
- **t = Xs**: 每帧顶部显示当前仿真时间
- **10帧采样**: 均匀分布在整个仿真时间段

#### 统计信息（左上角）
- **Links**: 当前活跃链路数量
- **Components**: 连通分量数（1=全连通，>1=网络分割）

---

## 🔍 如何解读图表

### 1. 观察网络连通性

#### 全连通网络（理想状态）
- Components = 1
- 所有节点通过直接或间接链路相连
- 形成一个整体的通信网络
- **含义**: 任意两个节点都可以通信

#### 网络分割（问题状态）
- Components > 1
- 网络分裂成多个孤岛
- 某些节点组无法相互通信
- **含义**: 需要中继或等待重连

---

### 2. 观察拓扑结构类型

#### 星形拓扑（Star）
```
    1
   /|\
  2 0 3
   \|/
    4
```
- 中心节点连接所有其他节点
- **优点**: 路由简单
- **缺点**: 中心节点失效导致全网瘫痪

#### 链式拓扑（Chain）
```
0 — 1 — 2 — 3 — 4
```
- 节点串联连接
- **优点**: 适合线形编队
- **缺点**: 端到端时延大，中间节点失效导致分割

#### 网状拓扑（Mesh）
```
0 — 1
|\ /|
| X |
|/ \|
2 — 3
```
- 多条冗余路径
- **优点**: 高容错，多路径
- **缺点**: 路由复杂，开销大

---

### 3. 观察时间演化趋势

#### 稳定演化（Easy模式预期）
- 拓扑结构基本不变
- 链路数量稳定
- Components始终为1
- **像静态照片**

#### 渐变演化（Moderate模式预期）
- 拓扑缓慢变化
- 偶尔出现链路断开重连
- Components偶尔变为2
- **像慢动作电影**

#### 剧烈演化（Hard模式预期）
- 拓扑快速变化
- 链路频繁断开重连
- Components频繁变化
- **像快进电影**

---

## 📈 关键观察点

### 对比不同难度

1. **Easy（绿色背景）**
   - 预期：密集的链路，稳定的结构
   - 链路数：60-80条
   - Components：始终为1

2. **Moderate（橙色背景）**
   - 预期：中等密度，偶有变化
   - 链路数：40-60条
   - Components：1-2之间波动

3. **Hard（红色背景）**
   - 预期：稀疏链路，频繁变化
   - 链路数：20-40条
   - Components：可能出现3-4

### 对比不同编队

1. **V-Formation（V字形）**
   - 特征：头节点为中心，两翼展开
   - 预期：V形结构明显

2. **Cross（十字形）**
   - 特征：中心节点连接四个方向
   - 预期：星形拓扑为主

3. **Line（直线形）**
   - 特征：节点排成一条线
   - 预期：链式拓扑为主

4. **Triangle（三角形）**
   - 特征：三个顶点形成骨架
   - 预期：三角网状结构

---

## 💡 发现与洞察

### 网络稳定性评估

通过观察10帧的变化程度，可以评估：
- **帧间差异小** → 网络稳定
- **帧间差异大** → 网络动态
- **结构保持** → 编队控制良好
- **结构失真** → 编队控制困难

### 关键节点识别

观察哪些节点：
- **连接最多** → Hub节点（关键中继）
- **位置中心** → 几何中心节点
- **始终孤立** → 边缘节点

### 时间模式发现

- **周期性变化** → 可能有周期性干扰
- **突变点** → 可能有事件触发
- **渐进退化** → 系统性能下降

---

## 🎨 视觉设计

### 节点配色方案
使用15种不同颜色循环，确保相邻节点颜色区分度高：
- 节点0: 🔵 蓝色 `#3498db`
- 节点1: 🔴 红色 `#e74c3c`
- 节点2: 🟢 绿色 `#2ecc71`
- 节点3: 🟠 橙色 `#f39c12`
- 节点4: 🟣 紫色 `#9b59b6`
- ...（循环使用）

### 布局算法
- 使用实际GPS坐标
- 归一化到[0,1]范围
- 保持纵横比

### 帧率设计
- 10帧均匀采样
- 每行最多5帧
- 2行布局（便于打印）

---

## 🛠️ 技术细节

### 数据来源
1. **拓扑数据**: `topology-changes.txt`
   - 格式: `时间范围: 链路列表`
   - 示例: `0-5: Node0-Node13, Node2-Node14`

2. **位置数据**: `node-positions.csv`
   - 格式: `time_s, nodeId, x, y, z`
   - 使用x,y坐标，忽略z（高度）

### 采样策略
- **时间采样**: 线性均匀分布10个时间点
- **空间采样**: ±2秒时间窗口内的拓扑合并
- **位置插值**: 选择最接近的时间点

### 性能优化
- 批量读取CSV文件
- 预计算归一化坐标
- 使用NetworkX加速图计算

---

## 🚀 应用场景

### 学术论文
- 展示拓扑动态性
- 对比不同算法的稳定性
- 分析编队保持能力

### 技术报告
- 直观展示网络演化
- 识别问题时间点
- 评估协议性能

### PPT演示
- 使用对比条带图作为总结
- 选择典型场景的演化图详细说明
- 动画效果：逐帧播放

---

## 📝 重新生成

如需重新生成或调整参数：

```bash
cd /mnt/e/Simulator/ns3/workspace/ns-allinone-3.43/ns-3.43/benchmark/bench_pic
python3 visualize_topology_evolution.py
```

### 参数调整

**修改采样帧数**（默认10帧）:
```python
# 在 generate_all_visualizations() 中修改
self.plot_topology_comic(dataset_name, num_frames=15)  # 改为15帧
```

**修改时间窗口**（默认±2秒）:
```python
# 在 plot_topology_comic() 中修改
time_range = (time_point - 5, time_point + 5)  # 改为±5秒
```

**修改节点大小**:
```python
# 在绘制节点时修改
ax.scatter(x, y, s=300, ...)  # 改为更大的节点
```

---

## 🔬 进阶分析

### 1. 拓扑度量计算
- 平均度（Average Degree）
- 聚类系数（Clustering Coefficient）
- 最短路径长度（Shortest Path Length）
- 网络直径（Network Diameter）

### 2. 时间序列分析
- 链路变化率曲线
- Components数量变化
- 网络密度演化

### 3. 动画生成
```python
# 可以生成GIF动画
import imageio
images = []
for frame in range(10):
    images.append(imageio.imread(f'frame_{frame}.png'))
imageio.mimsave('evolution.gif', images, duration=0.5)
```

---

## 📊 统计信息

### 图表数量
- **单数据集演化图**: 12张
- **对比条带图**: 1张
- **总计**: 13张高质量PNG图片

### 图片尺寸
- **演化漫画**: 17.5×7英寸（5列×2行）
- **对比条带**: 12×30英寸（3列×12行）
- **分辨率**: 150 DPI

---

**生成时间**: 2025-10-12  
**工具版本**: Topology Evolution Visualizer v1.0  
**Python依赖**: matplotlib, numpy, pandas, networkx
