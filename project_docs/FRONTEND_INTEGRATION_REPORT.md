# 前端接入报告：场景环境层 + 合作/非合作模式

## 1. 文档目的

本报告用于指导当前前端工作台接入已经完成的后端能力，重点覆盖：

- 多场景环境层接入
- `cooperative / non_cooperative` 模式接入
- 非合作链路的 6 层输出接入
- 前端页面结构、模块映射、数据契约、展示优先级

本报告不包含前端具体编码实现，但会给出：

- 每个前端模块应该接什么数据
- 不同模式和不同场景下展示什么
- 现有接口和文件口径怎么对齐
- 当前哪些东西已经可直接接，哪些还需要前后端适配层补一层

## 2. 当前系统状态

### 2.1 后端当前已完成的能力

后端 `uav_resource_allocation` 当前已经完成：

- 场景环境层
  - `urban`
  - `forest`
  - `lake`
  - `open-field`
- 任务模式层
  - `cooperative`
  - `non_cooperative`
- 非合作观测与推理链路
  - 事件层
  - 窗口层
  - 边证据层
  - 推理结果层
  - 图节点层
  - 关键节点候选层

### 2.2 当前后端核心输出

合作模式当前稳定输出：

- `environment_summary.json`
- 原有仿真输出：
  - `rtk-node-positions.csv`
  - `qos_performance.csv`
  - `resource_allocation_detailed.csv`
  - `topology_evolution.csv`
  - `topology_detailed.csv`
  - `rtk-topology-changes.txt`
  - `rtk-flow-stats.csv`

非合作模式在此基础上新增 6 层输出：

- `observed_signal_events.csv`
- `observed_comm_windows.csv`
- `observed_link_evidence.csv`
- `inferred_topology_edges.csv`
- `inferred_graph_nodes.csv`
- `key_node_candidates.csv`

### 2.3 当前 Flask API 状态

当前 [app.py](/home/tzx/ns-3.43/api_server/app.py) 已经能返回：

- `positions`
- `topology_evolution`
- `qos`
- `transmissions`
- `topology_links`
- `flow_summary`
- `resource_detailed`
- `topology_detailed`

但当前 API 还**没有**把以下新文件纳入 `/api/results/<task_id>` 返回体：

- `environment_summary.json`
- `observed_signal_events.csv`
- `observed_comm_windows.csv`
- `observed_link_evidence.csv`
- `inferred_topology_edges.csv`
- `inferred_graph_nodes.csv`
- `key_node_candidates.csv`

这意味着：

- 后端能力已经完成
- 前端接入时需要增加一层“新结果文件读取与组装”

## 3. 前端总接入原则

### 3.1 顶层模式必须先分流

前端不能再把合作和非合作混在一个工作区里。

推荐结构：

1. 模式入口页
2. 合作场景工作区
3. 非合作场景工作区

共享能力：

- 场景选择
- 3D 沙盘
- 回放条
- 时间轴
- 单机详情抽屉
- 地图/场景编辑器

分开的能力：

- 左侧控制项
- 右侧分析指标
- 默认图层显示
- 默认日志叙事
- 结果解释逻辑

### 3.2 场景和模式是两个维度

前端状态不应只存一个“当前页面模式”，而应拆成：

- `operationMode`
  - `cooperative`
  - `non_cooperative`
- `sceneType`
  - `urban`
  - `forest`
  - `lake`
  - `open-field`

这样前端才能正确支持：

- 合作 + 城市
- 合作 + 森林
- 非合作 + 湖面
- 非合作 + 野地

### 3.3 非合作模式默认不显示真值拓扑

非合作模式下前端默认展示顺序应为：

1. 观测到了什么
2. 这些观测有多少噪声/缺失
3. 从观测推断出了什么
4. 哪些节点最关键

而不是一开始就直接把最终概率边当作唯一主视图。

## 4. 前端页面结构建议

## 4.1 模式入口页

入口页只做三件事：

- 选择 `cooperative / non_cooperative`
- 选择 `sceneType`
- 选择地图来源
  - 预置地图
  - 自定义地图
  - OSM 上传地图

入口页不做深度参数调整。

推荐入口页字段：

- `operationMode`
- `sceneType`
- `difficulty`
- `formation`
- `strategy`
- `map_name`
- `formation_spacing`
- `start / target`

## 4.2 合作场景工作区

合作场景工作区应该围绕“通信规划与恢复”来讲。

左侧：

- 场景与任务配置
- 通信模式配置
  - `centralized`
  - `distributed`
- 关键策略参数
- 全局 QoS 指标

中间：

- 3D 沙盘
- 我方无人机
- 真实拓扑
- 环境图层
- 日志

右侧：

- 拓扑连通率
- 吞吐/PDR/时延
- 恢复过程曲线
- 失效前后对比

底部：

- 回放
- 倍速
- 拖动时间轴

## 4.3 非合作场景工作区

非合作场景工作区应该围绕“观测 -> 证据 -> 推理 -> 图表示 -> 关键节点”来讲。

左侧：

- 场景与任务配置
- 非合作观测参数概览
  - 观测半径
  - 随机漏检率
  - 位置噪声
  - 功率噪声
- 当前窗口摘要

中间：

- 3D 沙盘
- 目标侧观测迹象
- 推理概率边
- 关键节点高亮
- 当前窗口日志

右侧：

- 观测证据面板
- 推理结果面板
- 图节点面板
- 关键节点候选面板

底部：

- 回放条
- 窗口切换
- 当前窗口聚焦

## 5. 不同场景的前端接入重点

## 5.1 urban

应该强调：

- 建筑遮挡
- LoS/NLoS
- 建筑密度
- 平均建筑高度
- 建筑覆盖率
- 平均街宽

前端主要读取：

- `environment_summary.json`
  - `hasBuildings`
  - `buildingFeatureCount`
  - `avgBuildingHeightM`
  - `buildingDensityPerKm2`
  - `buildingCoverageRatio`
  - `avgStreetWidthM`
  - `losDecisionMode`
  - `losBlockedPairRatio`
  - `avgBuildingCrossingsPerPair`

推荐展示位置：

- 左侧环境卡片
- 沙盘建筑图层
- 右侧环境解释面板

## 5.2 forest

应该强调：

- 植被衰减
- 森林遮挡
- 连通性衰减

前端主要读取：

- `environment_summary.json`
  - `hasVegetation`
  - `forestFeatureCount`
  - `vegetationLossDbPerM`
  - `primaryForestDensityClass`

推荐展示位置：

- 沙盘森林区域覆盖
- 环境贡献解释卡片
- 非合作模式下的缺失原因统计

## 5.3 lake

应该强调：

- 水面反射敏感性
- 开阔 LOS
- 场景对链路稳定性的影响

前端主要读取：

- `environment_summary.json`
  - `hasWaterSurface`
  - `waterFeatureCount`
  - `reflectionAware`
  - `primaryWaterType`

推荐展示位置：

- 沙盘水域图层
- 链路解释卡片
- 非合作模式推理边置信度对比

## 5.4 open-field

应该强调：

- 开阔低遮挡
- 基线传播环境
- 适合作为合作/非合作对照场景

前端主要读取：

- `environment_summary.json`
  - `openFieldFeatureCount`
  - `primaryOpenFieldSurfaceType`
  - `pathLossExponent`
  - `connectivityRangeFactor`

推荐展示位置：

- 作为默认 benchmark 场景
- 作为合作/非合作模式切换的对照页

## 6. 合作模式前端接入清单

## 6.1 数据来源

合作模式前端主数据来源：

- `environment_summary.json`
- `rtk-node-positions.csv`
- `resource_allocation_detailed.csv`
- `qos_performance.csv`
- `topology_evolution.csv`
- `topology_detailed.csv`
- `rtk-topology-changes.txt`
- `rtk-flow-stats.csv`

### 6.2 前端模块映射

**顶部状态栏**

- 当前模式：`cooperative`
- 当前场景：`sceneType`
- 当前帧 / 当前窗口
- 引擎状态
- 连通率

**左侧控制面板**

- 任务参数
- 场景参数
- 地图来源
- 通信模式切换

**左侧环境卡片**

- 读取 `environment_summary.json`
- 只显示环境摘要，不显示观测噪声字段

**中间沙盘**

- `positions`
- `topology_links` 或 `topology_detailed`
- 地图/建筑/场景图层

**右侧指标区**

- `qos`
- `topology_evolution`
- `flow_summary`

**单机详情**

- 从 `resource_detailed`
- 从 `positions`
- 从 `qos`
- 汇总得到当前节点状态

## 7. 非合作模式前端接入清单

## 7.1 数据来源

非合作模式前端需要读取：

- 合作模式基础层中与场景/位置相关的必要数据
- 新增 6 层非合作文件：
  - `observed_signal_events.csv`
  - `observed_comm_windows.csv`
  - `observed_link_evidence.csv`
  - `inferred_topology_edges.csv`
  - `inferred_graph_nodes.csv`
  - `key_node_candidates.csv`

### 7.2 前端模块映射

**顶部状态栏**

- 当前模式：`non_cooperative`
- 当前场景：`sceneType`
- 当前窗口：`windowStart ~ windowEnd`
- 当前推理边数量
- 当前关键节点数量

**左侧观测摘要卡片**

- 来自 `environment_summary.json`
  - `observationEnabled`
  - `observationWindowDurationSec`
  - `observationSubslotCount`
  - `observationRangeM`
  - `observationRandomDropRate`
  - `observationPositionNoiseStdDevM`
  - `observationPowerNoiseStdDevDb`

**中间沙盘**

应该分 3 层开关：

- 观测节点活动层
  - 来自 `observed_comm_windows.csv`
- 推理边层
  - 来自 `inferred_topology_edges.csv`
- 关键节点层
  - 来自 `key_node_candidates.csv`

推荐默认：

- 打开推理边
- 打开关键节点高亮
- 观测窗口层作为次级透明叠加

**右侧观测证据面板**

主要读取：

- `observed_signal_events.csv`
- `observed_comm_windows.csv`

推荐展示：

- 当前窗口观测到的节点数
- 当前窗口缺失情况
- `stateSequence`
- `overallConfidence`
- `missingReason`

**右侧边证据面板**

主要读取：

- `observed_link_evidence.csv`

推荐展示：

- `srcObservedNodeId`
- `dstObservedNodeId`
- `evidenceStrength`
- `observerCount`
- `observerAgreementScore`
- `edgeObservationConfidence`

**右侧推理结果面板**

主要读取：

- `inferred_topology_edges.csv`

推荐展示：

- `edgeProbability`
- `edgeConfidence`
- `inferenceMethod`
- 按概率排序的 Top 边

**右侧关键节点面板**

主要读取：

- `inferred_graph_nodes.csv`
- `key_node_candidates.csv`

推荐展示：

- `observedNodeId`
- `rank`
- `weightedDegreeScore`
- `avgIncidentProbability`
- `avgIncidentConfidence`
- `keyNodeScore`

## 8. 当前前端接入的两种技术路径

## 8.1 路径 A：继续走现有 `/api/results/<task_id>`

优点：

- 前端改动最少
- 仍然沿用当前轮询任务机制

缺点：

- 当前 [app.py](/home/tzx/ns-3.43/api_server/app.py) 还没有把新 6 层文件读进去
- 需要后端再补一轮 API 组装

适用：

- 你希望保持当前 `simulate -> poll results` 方式完全不变

前端接入要求：

- 等后端把 6 层数据注入 `data`
- 前端服务层直接消费 JSON

## 8.2 路径 B：前端直接读取结果文件清单

优点：

- 新增 6 层文件接得最快
- 字段语义最直观

缺点：

- 需要额外做文件映射或静态资源暴露
- 和当前 Flask 结果接口不是同一条数据口

适用：

- 你希望先快速把非合作面板接上
- 后面再统一回 `api/results`

当前建议：

- 如果你要尽快联调，先用路径 B
- 如果你要正式收口，再统一收进路径 A

## 9. 推荐的前端接入顺序

### 第 1 步：先接模式层

- 页面入口支持 `cooperative / non_cooperative`
- 顶部状态栏展示当前模式

### 第 2 步：先接环境摘要层

- 所有模式都先接 `environment_summary.json`
- 先把场景环境卡片稳定起来

### 第 3 步：接合作模式主链

- `positions`
- `qos`
- `topology_evolution`
- `resource_detailed`

### 第 4 步：接非合作观测主链

- `observed_comm_windows.csv`
- `observed_link_evidence.csv`
- `inferred_topology_edges.csv`

### 第 5 步：接关键节点层

- `inferred_graph_nodes.csv`
- `key_node_candidates.csv`

### 第 6 步：最后统一回放逻辑

- 合作模式按帧/时间
- 非合作模式按窗口
- 统一到底部时间轴

## 10. 推荐的数据适配层设计

前端建议新增一个单独的适配层，不要让组件直接消费原始 CSV 结构。

推荐中间结构：

### 10.1 环境摘要

```ts
type EnvironmentSummaryView = {
  operationMode: "cooperative" | "non_cooperative"
  sceneType: "urban" | "forest" | "lake" | "open-field"
  baseModel: string
  environmentContributionSummary: string
  hasBuildings: boolean
  hasVegetation: boolean
  hasWaterSurface: boolean
  reflectionAware: boolean
  observationEnabled: boolean
  observationRangeM?: number
  observationRandomDropRate?: number
}
```

### 10.2 非合作窗口视图

```ts
type ObservedWindowView = {
  windowStart: number
  windowEnd: number
  observedNodeId: number
  observerId: number
  signalDetected: boolean
  isMissing: boolean
  missingReason: string
  stateSequence: string
  activeRatio: number
  overallConfidence: number
}
```

### 10.3 推理边视图

```ts
type InferredEdgeView = {
  windowStart: number
  windowEnd: number
  srcObservedNodeId: number
  dstObservedNodeId: number
  edgeProbability: number
  edgeConfidence: number
}
```

### 10.4 关键节点视图

```ts
type KeyNodeView = {
  windowStart: number
  windowEnd: number
  observedNodeId: number
  rank: number
  keyNodeScore: number
  weightedDegreeScore: number
}
```

## 11. 前端验收标准

### 11.1 场景层验收

- 切换 `urban / forest / lake / open-field` 后，环境卡片内容会变化
- 沙盘图层与环境摘要一致

### 11.2 合作模式验收

- 合作模式只展示合作主链数据
- 不出现非合作观测/推理面板

### 11.3 非合作模式验收

- 能看到观测窗口
- 能看到边证据
- 能看到概率边
- 能看到关键节点候选
- 默认不展示真实拓扑

### 11.4 数据一致性验收

- 当前窗口切换时：
  - 窗口层
  - 边证据层
  - 推理结果层
  - 图节点层
  - 关键节点层
  必须同步变化

## 12. 当前最值得前端优先接的内容

如果时间紧，优先级建议如下：

1. `environment_summary.json`
2. `observed_comm_windows.csv`
3. `inferred_topology_edges.csv`
4. `key_node_candidates.csv`
5. `observed_link_evidence.csv`
6. `inferred_graph_nodes.csv`

原因：

- 这 6 个文件已经足够把“场景 + 非合作 + 推理 + 关键节点”讲清楚
- 其中 `observed_comm_windows + inferred_topology_edges + key_node_candidates` 是前端最关键的三层

## 13. 结论

当前后端已经具备支撑前端接入的完整基础，特别是：

- 不同场景环境信息已经可解释
- 合作与非合作模式边界已经清楚
- 非合作链路的 6 层输出已经稳定存在
- 图表示和关键节点识别入口已经准备好

前端现在最应该做的不是再改概念，而是：

1. 把模式入口分开
2. 先接环境摘要
3. 接非合作的窗口、概率边、关键节点三层主数据
4. 最后统一回放和交互逻辑

这条路径是当前最稳、返工最少、也最符合老师展示逻辑的接入方式。
