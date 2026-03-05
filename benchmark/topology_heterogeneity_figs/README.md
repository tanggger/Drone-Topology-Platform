# 场景强异质 & 非稳态拓扑模式切换

## 这两张图要表达什么

这两张图用于说明导致模型泛化困难的根因：**场景强异质、非稳态，拓扑模式会变换**。

- **强异质**：不同编队/信道/负载下，拓扑结构与统计规律完全不同（不同 regime）
- **非稳态**：即使在同一任务过程中，也会发生 formation change、干扰突增、策略切换等，导致拓扑指标跳变
- **后果**：单一模型容易过拟合某一种模式，跨场景/跨时间窗时性能崩溃

> 说明：所有数据为合成数据（synthetic），用于叙事表达。

---

## 图 1：`fig1_cross_scenario_heterogeneity.png`

### 图的内容

一张散点图，展示三种场景下的拓扑指标分布。

### 坐标含义

| 坐标 | 含义 | 说明 |
|------|------|------|
| **横轴** | Average Node Degree (connectivity) | 平均节点度，反映网络连通性强弱 |
| **纵轴** | Link Stability (Jaccard similarity) | 链路稳定性，相邻时间窗口链路集合的 Jaccard 相似度（0~1，越高越稳定） |
| **颜色** | 不同场景 | 蓝=场景A，橙=场景B，红=场景C |
| **虚线椭圆** | 簇的大致范围 | 帮助视觉识别各场景的分布区域 |

### 三种场景

- **Scenario A (蓝色)**：Line 编队 + LoS 信道 + Low 负载 → 高连通、高稳定
- **Scenario B (橙色)**：Cross 编队 + Urban 信道 + Mid 负载 → 中连通、中稳定  
- **Scenario C (红色)**：V-formation 编队 + Multipath 信道 + High 负载 → 低连通、低稳定

### 怎么看

- 观察三种颜色的点云是否**分离**：如果分离明显，说明不同场景的拓扑特性完全不同
- 本图中：蓝色簇在右上角（高度高稳定），红色簇在左下角（低度低稳定），橙色在中间
- 三个簇几乎不重叠 → **强异质性**

### 反映的问题

如果模型只在某一种场景（如 Scenario A）上训练，它学到的是"高连通高稳定"的模式。当测试场景变成 Scenario C（低连通低稳定）时，模型的假设被打破，性能会崩溃。

**一句话总结**：*Different scenarios occupy completely different regions → single model overfits one regime.*

---

## 图 2：`fig2_mode_switching_within_task.png`

### 图的内容

一张时序图，展示同一任务过程中拓扑连通性指标的变化，以及模式切换点。

### 坐标含义

| 坐标 | 含义 | 说明 |
|------|------|------|
| **横轴** | Time (s) | 时间，单位秒 |
| **纵轴** | Topology Connectivity (avg. degree) | 拓扑连通性，即平均节点度 |
| **背景色块** | 不同模式阶段 | 绿=Mode A，黄=Mode B，红=Mode C，蓝=Mode D |
| **垂直虚线** | 模式切换点 | 标记 regime 切换的时刻 |

### 四种模式

- **Mode A (绿色，0-50s)**：稳定阶段，高连通，低波动
- **Mode B (黄色，50-100s)**：编队切换（formation change），连通性下降
- **Mode C (红色，100-150s)**：干扰突增（interference spike），连通性最低，波动最大
- **Mode D (蓝色，150-200s)**：恢复阶段，连通性回升

### 怎么看

- 观察曲线在背景色切换点附近是否有**跳变**
- 本图中：
  - t=50s：曲线从 ~7 跳到 ~4-5（编队切换导致连通性下降）
  - t=100s：曲线进一步下降到 ~2-3，且波动加剧（干扰突增）
  - t=150s：曲线回升到 ~5-6（恢复）
- 不同模式下曲线的**均值和方差**都不同 → **非平稳**

### 反映的问题

即使在同一条任务轨迹中，拓扑也会发生 regime switch。对模型而言，这意味着**数据生成机制非平稳**：在 Mode A 学到的模式，到 Mode C 就不再适用，导致跨时间窗性能崩溃。

**一句话总结**：*Topology metrics jump at mode switches → model trained on one mode fails on others.*

---

## 复现与调参

```bash
python3 generate_clean_figs.py
```

调参位置：
- **图1**：修改 `plot_1_cross_scenario_heterogeneity()` 中各场景的 `deg_X` 和 `stab_X` 的均值/方差
- **图2**：修改 `plot_2_mode_switching_within_task()` 中 `modes` 列表的时间段、均值、波动幅度

---

## 文件清单

```
topology_heterogeneity_figs/
├── fig1_cross_scenario_heterogeneity.png   # 图1：跨场景异质性
├── fig2_mode_switching_within_task.png     # 图2：任务内模式切换
├── generate_clean_figs.py                  # 生成脚本
└── README.md                               # 本说明文件
```
