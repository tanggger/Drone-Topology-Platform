# 前端字段级对接参考

本文档按后端当前真实接口与真实输出编写，目标是给前端做字段映射、类型定义、空值处理和联调校验。

配套文档：

- [FRONTEND_API_INTEGRATION_GUIDE.md](/home/tzx/ns-3.43/project_docs/frontend/FRONTEND_API_INTEGRATION_GUIDE.md)
- [FRONTEND_HANDOFF_CHECKLIST.md](/home/tzx/ns-3.43/project_docs/frontend/FRONTEND_HANDOFF_CHECKLIST.md)

## 1. 使用约定

### 1.1 数据主入口

- 主接口：`GET /api/results/<task_id>/frontend`
- 返回顶层结构：
  - `meta`
  - `shared`
  - `cooperative`
  - `non_cooperative`
  - `manifest`

### 1.2 类型记法

- `string`
- `number`
- `boolean`
- `object`
- `array<T>`
- `null`

### 1.3 单位记法

- `s`：秒
- `ms`：毫秒
- `dB`：分贝
- `dBm`：分贝毫瓦
- `Hz` / `GHz`
- `Mbps`
- `m`
- `ratio`：通常在 `0~1`

### 1.4 可空规则

- `nullable: yes` 表示该字段可能是 `null`
- 典型原因：
  - 指标尚未产生
  - 当前场景不适用
  - 后端 `NaN` 被 API 清洗成 `null`

### 1.5 后端字段别名兼容提醒

当前后端的规范字段名与部分前端现存消费口径存在漂移。建议前端在适配层统一映射。

- `cooperative.dashboard_snapshot.latestRecoveryAction`
  - 后端规范字段
  - 若前端历史上使用 `latestActionType`，应在前端适配层映射：
    - `latestActionType = latestRecoveryAction`

- `shared.resource_detailed.interference_dBm`
  - 资源明细原始字段
  - 若前端历史上使用 `interference`，应映射：
    - `interference = interference_dBm`

- `non_cooperative.observation_inference.key_node_candidates.observedNodeId`
  - 后端规范字段
  - 若前端历史上使用 `nodeId`，应映射：
    - `nodeId = observedNodeId`

- `non_cooperative.observation_inference.key_node_candidates.keyNodeScore`
  - 后端规范字段
  - 若前端历史上使用 `score`，应映射：
    - `score = keyNodeScore`

文档以下内容全部以**后端规范字段名**为准。

## 2. 顶层对象

## 2.1 `meta`

来源：

- `/api/results/<task_id>/frontend.meta`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `taskId` | `string` | no | - | 当前任务唯一标识 |
| `operationMode` | `string` | yes | - | `cooperative` 或 `non_cooperative` |
| `sceneType` | `string` | yes | - | `urban / forest / lake / open-field` |
| `difficulty` | `string` | yes | - | `Easy / Moderate / Hard / Custom` |
| `communicationMode` | `string` | yes | - | 仅合作模式有意义 |
| `formation` | `string` | yes | - | 编队类型 |

最小 JSON 示例：

```json
{
  "taskId": "abc123",
  "operationMode": "cooperative",
  "sceneType": "urban",
  "difficulty": "Moderate",
  "communicationMode": "hybrid",
  "formation": "v_formation"
}
```

## 2.2 `manifest`

来源：

- `/api/results/<task_id>/manifest`
- `/api/results/<task_id>/frontend.manifest`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `taskId` | `string` | no | - | 当前任务 ID |
| `outputDir` | `string` | no | - | 相对输出目录 |
| `operationMode` | `string` | no | - | 当前任务模式 |
| `sceneType` | `string` | yes | - | 场景类型 |
| `difficulty` | `string` | yes | - | 难度 |
| `communicationMode` | `string` | yes | - | 合作通信模式 |
| `formation` | `string` | yes | - | 编队类型 |
| `sharedDatasets` | `array<string>` | no | - | 实际可用共享数据集名列表 |
| `cooperativeDatasets` | `array<string>` | no | - | 实际可用合作数据集名列表 |
| `nonCooperativeDatasets` | `array<string>` | no | - | 实际可用非合作基础数据集名列表 |
| `nonCooperativeAttackDatasets` | `array<string>` | no | - | 实际可用非合作打击数据集名列表 |
| `hasNonCooperativeAttack` | `boolean` | no | - | 是否存在非合作打击数据块 |
| `attackType` | `string` | yes | - | 当前非合作打击类型 |
| `attackExecuted` | `boolean` | no | - | 是否真正执行到了实体目标 |
| `availableFiles` | `array<string>` | no | - | 输出目录内实际文件名列表 |

最小 JSON 示例：

```json
{
  "taskId": "abc123",
  "outputDir": "output/run_abc123",
  "operationMode": "non_cooperative",
  "sceneType": "forest",
  "difficulty": "Moderate",
  "communicationMode": null,
  "formation": "v_formation",
  "sharedDatasets": ["environment_summary", "positions", "qos"],
  "cooperativeDatasets": [],
  "nonCooperativeDatasets": ["observed_signal_events", "inferred_topology_edges"],
  "nonCooperativeAttackDatasets": ["plan", "summary", "recommendations"],
  "hasNonCooperativeAttack": true,
  "attackType": "node_strike",
  "attackExecuted": true,
  "availableFiles": ["environment_summary.json", "noncooperative_attack_plan.json"]
}
```

## 3. shared

## 3.1 `shared.environment_summary`

来源：

- `environment_summary.json`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `operationMode` | `string` | no | - | 运行模式 |
| `sceneType` | `string` | no | - | 场景类型 |
| `difficulty` | `string` | no | - | 难度 |
| `formation` | `string` | no | - | 编队类型 |
| `baseModel` | `string` | no | - | 基础传播模型 |
| `environmentSource` | `string` | no | - | 环境数据来源 |
| `geometryInputMode` | `string` | no | - | 地图几何输入方式 |
| `effectiveModelSummary` | `string` | no | - | 实际生效模型摘要 |
| `environmentContributionSummary` | `string` | no | - | 场景贡献摘要 |
| `hasBuildings` | `boolean` | no | - | 是否存在建筑要素 |
| `hasVegetation` | `boolean` | no | - | 是否存在植被要素 |
| `hasWaterSurface` | `boolean` | no | - | 是否存在水面要素 |
| `reflectionAware` | `boolean` | no | - | 是否启用反射增强 |
| `shadowSigmaDb` | `number` | no | `dB` | 阴影衰落标准差 |
| `nlosPenaltyDb` | `number` | no | `dB` | NLOS 额外惩罚 |
| `vegetationLossDbPerM` | `number` | no | `dB/m` | 植被穿透损耗率 |
| `interferenceFactor` | `number` | no | `ratio` | 场景干扰放大系数 |
| `connectivityRangeFactor` | `number` | no | `ratio` | 场景连通距离缩放 |
| `pathLossExponent` | `number` | no | - | 路损指数 |
| `urbanAltitudePenaltyDbLow` | `number` | no | `dB` | 城市低空额外惩罚 |
| `urbanAltitudeGainDbHigh` | `number` | no | `dB` | 城市高空增益 |
| `urbanStreetCanyonFactor` | `number` | no | `ratio` | 街谷效应因子 |
| `reroutePressureFactor` | `number` | no | `ratio` | 重路由压力系数 |
| `controlMessageUrgencyFactor` | `number` | no | `ratio` | 控制消息时效压力因子 |
| `relayInstabilityFactor` | `number` | no | `ratio` | 中继不稳定因子 |
| `formationReconfigPenalty` | `number` | no | `ratio` | 编队重构惩罚 |
| `carrierFrequencyGHz` | `number` | no | `GHz` | 载频 |
| `channelBandwidthMHz` | `number` | no | `MHz` | 带宽 |
| `polarizationMode` | `string` | no | - | 极化模式 |
| `lakeVolatilityJitterDb` | `number` | no | `dB` | 水面波动抖动强度 |
| `lakeDeepFadeProbability` | `number` | no | `ratio` | 深衰落概率 |
| `lakeDeepFadeMaxDb` | `number` | no | `dB` | 最大深衰落强度 |
| `lakeReflectionDelayJitterMs` | `number` | no | `ms` | 水面反射时延抖动 |
| `rxSensitivity` | `number` | no | `dBm` | 接收灵敏度 |
| `txPower` | `number` | no | `dBm` | 发射功率 |
| `noiseFigure` | `number` | no | `dB` | 噪声系数 |
| `trafficLoadMbps` | `number` | no | `Mbps` | 业务负载 |
| `numInterferenceNodes` | `number` | no | - | 干扰节点数量 |
| `observationEnabled` | `boolean` | no | - | 是否启用非合作观测 |
| `observationWindowDurationSec` | `number` | no | `s` | 观测窗口长度 |
| `observationSubslotCount` | `number` | no | - | 子时隙数量 |
| `observationSubslotDurationSec` | `number` | no | `s` | 子时隙长度 |
| `trackCreateWindowCount` | `number` | no | - | 轨迹创建窗口要求 |
| `trackDeleteWindowCount` | `number` | no | - | 轨迹删除窗口要求 |
| `observationRangeM` | `number` | no | `m` | 观测距离 |
| `observationRandomDropRate` | `number` | no | `ratio` | 随机漏检率 |
| `observationPositionNoiseStdDevM` | `number` | no | `m` | 位置噪声标准差 |
| `observationPowerNoiseStdDevDb` | `number` | no | `dB` | 功率噪声标准差 |
| `observationObserverCount` | `number` | no | - | 观测者数量 |
| `observationTargetObjectCount` | `number` | no | - | 目标对象数量 |
| `avgBuildingHeightM` | `number` | yes | `m` | 建筑平均高度 |
| `avgStreetWidthM` | `number` | yes | `m` | 平均街宽 |
| `communicationMode` | `string` | yes | - | 合作模式下的通信架构 |
| `leaderNodeId` | `number` | yes | - | 合作 leader |
| `cooperativeFailureType` | `string` | yes | - | 合作故障类型 |
| `failureTargetId` | `number` | yes | - | 合作故障目标 |
| `recoveryPolicy` | `string` | yes | - | 合作恢复策略 |
| `recoveryObjective` | `string` | yes | - | 合作恢复目标 |

最小 JSON 示例：

```json
{
  "operationMode": "cooperative",
  "sceneType": "urban",
  "difficulty": "Moderate",
  "formation": "v_formation",
  "baseModel": "HybridBuildingsPropagationLossModel",
  "environmentSource": "geojson-scene-overlay",
  "geometryInputMode": "geojson-scene-overlay",
  "effectiveModelSummary": "HybridBuildingsPropagationLossModel + altitude-aware urban overlay + geometry-backed buildings",
  "hasBuildings": true,
  "hasVegetation": false,
  "hasWaterSurface": false,
  "reflectionAware": false,
  "connectivityRangeFactor": 0.72,
  "carrierFrequencyGHz": 5.18,
  "channelBandwidthMHz": 20,
  "polarizationMode": "vertical",
  "avgBuildingHeightM": 28.5,
  "avgStreetWidthM": 26.25,
  "communicationMode": "hybrid",
  "leaderNodeId": 0,
  "cooperativeFailureType": "node_failure",
  "failureTargetId": 1,
  "recoveryPolicy": "global_recovery",
  "recoveryObjective": "connectivity"
}
```

## 3.2 `shared.positions`

来源：

- `rtk-node-positions.csv`
- API 会尝试与 `resource_allocation_detailed.csv` 做位置级合并

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `time_s` 或 `time` | `number` | no | `s` | 采样时间 |
| `nodeId` | `number` | no | - | 节点 ID |
| `x` | `number` | no | `m` | X 坐标 |
| `y` | `number` | no | `m` | Y 坐标 |
| `z` | `number` | no | `m` | Z 坐标 |
| `node_type` | `number` | no | - | 节点类型 |
| `speed` | `number` | no | `m/s` | 速度 |
| `power` | `number` | yes | `dBm` | 合并后的发射功率 |
| `channel` | `number` | yes | - | 合并后的信道 |
| `data_rate` | `number` | yes | `Mbps` | 合并后的速率 |
| `neighbors` | `number` | yes | - | 合并后的邻居数 |
| `interference` | `number` | yes | `dBm` | 合并后的干扰，API 已从 `interference_dBm` 映射而来 |
| `sinr` | `number` | yes | `dB` | 最差 SINR |

## 3.3 `shared.transmissions`

来源：

- `rtk-node-transmissions.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `time` | `number` | no | `s` | 事件时间 |
| `nodeId` | `number` | no | - | 节点 ID |
| `eventType` | `string` | no | - | 发送事件类型 |

## 3.4 `shared.topology_links`

来源：

- `rtk-topology-changes.txt`

类型：

- `array<string>`

说明：

- 每一项是一行原始拓扑变化文本
- 前端若需要结构化展示，应自行按行解析

## 3.5 `shared.topology_evolution`

来源：

- `topology_evolution.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `time` | `number` | no | `s` | 采样时间 |
| `num_links` | `number` | no | - | 当前链路数量 |
| `connectivity` | `number` | no | `ratio` | 当前连通率 |

## 3.6 `shared.topology_detailed`

来源：

- `topology_detailed.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `time` | `number` | no | `s` | 采样时间 |
| `num_nodes` | `number` | no | - | 当前节点数 |
| `num_links` | `number` | no | - | 当前链路数 |
| `avg_degree` | `number` | no | - | 平均度 |
| `network_density` | `number` | no | `ratio` | 网络密度 |

## 3.7 `shared.resource_detailed`

来源：

- `resource_allocation_detailed.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `time` | `number` | no | `s` | 采样时间 |
| `node_id` | `number` | no | - | 节点 ID |
| `channel` | `number` | no | - | 当前信道 |
| `tx_power` | `number` | no | `dBm` | 发射功率 |
| `data_rate` | `number` | no | `Mbps` | 速率 |
| `neighbors` | `number` | no | - | 邻居数 |
| `interference_dBm` | `number` | no | `dBm` | 干扰强度 |
| `worst_sinr_dB` | `number` | no | `dB` | 最差 SINR |

兼容提醒：

- 规范字段名是 `interference_dBm`
- 若前端使用 `interference`，应在适配层重命名

## 3.8 `shared.qos`

来源：

- `qos_performance.csv`

固定列规则：

| 列 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `time` | `number` | no | `s` | 采样时间 |
| `uavX_pdr` | `number` | no | `ratio` | 第 `X` 个 UAV 的 PDR |
| `uavX_delay` | `number` | no | `ms` | 第 `X` 个 UAV 的平均时延 |
| `uavX_throughput` | `number` | no | `Mbps` | 第 `X` 个 UAV 的吞吐 |

说明：

- `X` 的范围是 `0 ~ numUAVs-1`
- 列是按 UAV 数量动态展开的
- 前端应按列名前缀解析，而不是硬编码固定 UAV 数量

## 3.9 `shared.flow_summary`

来源：

- `rtk-flow-stats.csv`

约定：

- 这是流级统计摘要
- 前端宜作为附加分析表，不建议作为主大盘核心数据源

## 4. cooperative

## 4.1 `cooperative.mode_summary`

来源：

- `cooperative_mode_summary.json`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `operationMode` | `string` | no | - | 固定为 `cooperative` |
| `communicationMode` | `string` | no | - | `centralized / distributed / hybrid` |
| `recoveryPolicy` | `string` | no | - | 恢复策略 |
| `recoveryObjective` | `string` | no | - | 恢复目标 |
| `sceneType` | `string` | no | - | 场景 |
| `difficulty` | `string` | no | - | 难度 |
| `formation` | `string` | no | - | 编队 |
| `leaderNodeId` | `number` | no | - | 当前 leader |
| `backupLeaderList` | `string` | no | - | 备份 leader 列表字符串 |
| `distributedHopLimit` | `number` | no | - | 分布式/混合模式局部视图 hop 数 |
| `failureType` | `string` | no | - | 故障类型 |
| `failureTargetId` | `number` | no | - | 故障目标 |
| `failureStartTime` | `number` | no | `s` | 故障开始时刻 |
| `failureDuration` | `number` | no | `s` | 故障持续时长 |
| `recoveryCooldown` | `number` | no | `s` | 恢复冷却时间 |
| `actionFlags` | `object` | no | - | 恢复动作开关集合 |

`actionFlags` 字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `allowChannelReallocation` | `boolean` | no | - | 是否允许信道重分配 |
| `allowPowerAdjustment` | `boolean` | no | - | 是否允许功率调整 |
| `allowRateAdjustment` | `boolean` | no | - | 是否允许速率调整 |
| `allowRelayReselection` | `boolean` | no | - | 是否允许中继重选 |
| `allowSlotReallocation` | `boolean` | no | - | 是否允许时隙重分配 |
| `allowRouteRebuild` | `boolean` | no | - | 是否允许路由重构 |

## 4.2 `cooperative.dashboard_snapshot`

来源：

- `cooperative_dashboard_snapshot.json`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `time` | `number` | no | `s` | 快照时间，通常等于仿真时长 |
| `phase` | `string` | no | - | `normal / failure / recovery / stable / transition` |
| `operationMode` | `string` | no | - | 运行模式 |
| `communicationMode` | `string` | no | - | 通信模式 |
| `leaderNodeId` | `number` | no | - | 当前 leader |
| `backupLeaderId` | `number` | no | - | 当前备份 leader |
| `isLeaderAlive` | `boolean` | no | - | leader 是否存活 |
| `routeChangeCount` | `number` | no | - | 路由切换累计次数 |
| `relaySwitchCount` | `number` | no | - | 中继切换累计次数 |
| `controlDeadlineMissCount` | `number` | no | - | 控制消息截止超时累计次数 |
| `routePressureScore` | `number` | no | `ratio` | 网络层压力分 |
| `failureActive` | `boolean` | no | - | 故障是否仍在激活 |
| `failureType` | `string` | no | - | 故障类型 |
| `failureTargetId` | `number` | no | - | 故障目标 |
| `connectivity` | `number` | no | `ratio` | 全局连通率 |
| `avgDegree` | `number` | no | - | 平均邻居数 |
| `pdr` | `number` | no | `ratio` | 全局 PDR |
| `throughputMbps` | `number` | no | `Mbps` | 全局吞吐 |
| `delayMs` | `number` | no | `ms` | 全局平均时延 |
| `p99DelayMs` | `number` | no | `ms` | 全局 P99 时延 |
| `responseTimeSec` | `number` | yes | `s` | 首次恢复响应时间 |
| `recoveryTimeSec` | `number` | yes | `s` | 达到恢复完成条件时间 |
| `stabilizationTimeSec` | `number` | yes | `s` | 达到稳定条件时间 |
| `latestRecoveryAction` | `string` | no | - | 最近一次恢复动作类型 |
| `recoveryStatus` | `string` | no | - | `not_triggered / active / completed / stable` |

兼容提醒：

- 后端规范字段是 `latestRecoveryAction`
- 若前端历史代码使用 `latestActionType`，请映射：
  - `latestActionType = latestRecoveryAction`

最小 JSON 示例：

```json
{
  "time": 60,
  "phase": "stable",
  "operationMode": "cooperative",
  "communicationMode": "hybrid",
  "leaderNodeId": 1,
  "backupLeaderId": 2,
  "isLeaderAlive": true,
  "routeChangeCount": 8,
  "relaySwitchCount": 3,
  "controlDeadlineMissCount": 1,
  "routePressureScore": 0.42,
  "failureActive": false,
  "failureType": "node_failure",
  "failureTargetId": 0,
  "connectivity": 1.0,
  "avgDegree": 3.2,
  "pdr": 0.74,
  "throughputMbps": 0.51,
  "delayMs": 18.4,
  "p99DelayMs": 72.0,
  "responseTimeSec": null,
  "recoveryTimeSec": 13.2,
  "stabilizationTimeSec": 17.0,
  "latestRecoveryAction": "leader_failover",
  "recoveryStatus": "stable"
}
```

## 4.3 `cooperative.failure_timeline`

来源：

- `cooperative_failure_timeline.json`

结构：

- `{ "events": array<object> }`

`events[]` 字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `eventId` | `number` | no | - | 时间线事件序号 |
| `time` | `number` | no | `s` | 事件时间 |
| `failureType` | `string` | no | - | 故障类型 |
| `targetNodeId` | `number` | no | - | 目标节点 |
| `targetRole` | `string` | no | - | 目标角色 |
| `failureState` | `string` | no | - | 如 `activated / cleared` |
| `isLeaderTarget` | `boolean` | no | - | 是否打击 leader |
| `affectedNeighborCount` | `number` | no | - | 受影响邻居数 |
| `affectedLinkCount` | `number` | no | - | 受影响链路数 |
| `effectSummary` | `string` | no | - | 影响摘要 |
| `source` | `string` | no | - | 事件来源 |

最小 JSON 示例：

```json
{
  "events": [
    {
      "eventId": 0,
      "time": 24.0,
      "failureType": "node_failure",
      "targetNodeId": 1,
      "targetRole": "follower",
      "isLeaderTarget": false,
      "failureState": "activated",
      "affectedNeighborCount": 3,
      "affectedLinkCount": 4,
      "effectSummary": "target removed from cooperative set",
      "source": "failure_scheduler"
    }
  ]
}
```

## 4.4 `cooperative.recovery_timeline`

来源：

- `cooperative_recovery_timeline.json`

结构：

- `{ "actions": array<object> }`

`actions[]` 字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `actionId` | `number` | no | - | 时间线动作序号 |
| `time` | `number` | no | `s` | 事件时间 |
| `phase` | `string` | no | - | 当前阶段 |
| `communicationMode` | `string` | no | - | 通信模式 |
| `recoveryPolicy` | `string` | no | - | 配置恢复策略 |
| `effectiveRecoveryPolicy` | `string` | no | - | 实际执行策略 |
| `triggerReason` | `string` | no | - | 触发原因 |
| `executorNodeId` | `number` | yes | - | 执行节点 |
| `targetNodeIds` | `string` | yes | - | 受影响节点 ID 列表字符串 |
| `actionType` | `string` | no | - | 恢复动作类型 |
| `oldValue` | `string` | yes | - | 动作前值 |
| `newValue` | `string` | yes | - | 动作后值 |
| `scope` | `string` | yes | - | 动作作用范围 |
| `expectedEffect` | `string` | yes | - | 预期效果 |
| `resultState` | `string` | yes | - | 动作结果状态 |

最小 JSON 示例：

```json
{
  "actions": [
    {
      "actionId": 0,
      "time": 24.1,
      "phase": "recovery",
      "communicationMode": "hybrid",
      "recoveryPolicy": "global_recovery",
      "effectiveRecoveryPolicy": "global_recovery",
      "triggerReason": "connectivity below threshold",
      "executorNodeId": 0,
      "targetNodeIds": "2|3|4",
      "actionType": "power_adjustment",
      "oldValue": "18.0",
      "newValue": "21.0",
      "scope": "failure_neighborhood",
      "expectedEffect": "improve local link margin",
      "resultState": "applied"
    }
  ]
}
```

## 4.5 `cooperative.metrics_timeseries`

来源：

- `cooperative_metrics_timeseries.json`

结构：

- `{ "samples": array<object> }`

`samples[]` 字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `time` | `number` | no | `s` | 采样时间 |
| `phase` | `string` | no | - | 当前阶段 |
| `connectivity` | `number` | no | `ratio` | 全局连通率 |
| `avgDegree` | `number` | no | - | 平均邻居数 |
| `pdr` | `number` | no | `ratio` | 全局 PDR |
| `throughputMbps` | `number` | no | `Mbps` | 全局吞吐 |
| `delayMs` | `number` | no | `ms` | 全局平均时延 |
| `p99DelayMs` | `number` | no | `ms` | 全局 P99 时延 |
| `failureNeighborhoodPdr` | `number` | yes | `ratio` | 冻结故障邻域 PDR |
| `failureNeighborhoodThroughputMbps` | `number` | yes | `Mbps` | 冻结故障邻域吞吐 |
| `failureNeighborhoodDelayMs` | `number` | yes | `ms` | 冻结故障邻域时延 |
| `failureNeighborhoodNodeCount` | `number` | no | - | 冻结故障邻域大小 |
| `failureTargetId` | `number` | no | - | 故障目标 |
| `isFailureTargetFailed` | `boolean` | no | - | 目标是否失效 |
| `failureTargetPdr` | `number` | yes | `ratio` | 目标节点 PDR |
| `failureTargetThroughputMbps` | `number` | yes | `Mbps` | 目标节点吞吐 |
| `failureTargetDelayMs` | `number` | yes | `ms` | 目标节点时延 |
| `activeNodeCount` | `number` | no | - | 当前活跃节点数 |
| `leaderNodeId` | `number` | no | - | 当前 leader |
| `isLeaderAlive` | `boolean` | no | - | leader 是否存活 |
| `routeChangeCount` | `number` | no | - | 路由切换累计次数 |
| `relaySwitchCount` | `number` | no | - | 中继切换累计次数 |
| `controlDeadlineMissCount` | `number` | no | - | 控制超时累计次数 |
| `routePressureScore` | `number` | no | `ratio` | 路由压力分 |
| `responseTimeSec` | `number` | yes | `s` | 响应时间 |
| `recoveryTimeSec` | `number` | yes | `s` | 恢复时间 |
| `stabilizationTimeSec` | `number` | yes | `s` | 稳定时间 |

注意：

- `failureNeighborhood*` 是**冻结故障邻域**，不是实时邻域
- `leaderNodeId / isLeaderAlive` 是逐样本历史值，不是最终值回填

最小 JSON 示例：

```json
{
  "samples": [
    {
      "time": 16.1,
      "phase": "recovery",
      "connectivity": 0.86,
      "avgDegree": 2.7,
      "pdr": 0.62,
      "throughputMbps": 0.31,
      "delayMs": 45.2,
      "p99DelayMs": 220.5,
      "failureNeighborhoodPdr": 0.41,
      "failureNeighborhoodThroughputMbps": 0.19,
      "failureNeighborhoodDelayMs": 78.3,
      "failureNeighborhoodNodeCount": 4,
      "failureTargetId": 1,
      "isFailureTargetFailed": true,
      "failureTargetPdr": 0.17,
      "failureTargetThroughputMbps": 0.05,
      "failureTargetDelayMs": 78.0,
      "activeNodeCount": 7,
      "leaderNodeId": 0,
      "isLeaderAlive": false,
      "routeChangeCount": 3,
      "relaySwitchCount": 1,
      "controlDeadlineMissCount": 1,
      "routePressureScore": 0.58,
      "responseTimeSec": null,
      "recoveryTimeSec": null,
      "stabilizationTimeSec": null
    }
  ]
}
```

## 4.6 `cooperative.failure_events`

来源：

- `cooperative_failure_events.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `time` | `number` | no | `s` | 事件时间 |
| `failure_type` | `string` | no | - | 故障类型 |
| `target_node_id` | `number` | no | - | 目标 |
| `is_leader_target` | `boolean` | no | - | 是否 leader |
| `failure_state` | `string` | no | - | 激活/清除状态 |
| `communication_mode` | `string` | no | - | 通信模式 |
| `recovery_policy` | `string` | no | - | 恢复策略 |
| `scene_type` | `string` | no | - | 场景 |
| `operation_mode` | `string` | no | - | 运行模式 |

## 4.7 `cooperative.recovery_actions`

来源：

- `cooperative_recovery_actions.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `time` | `number` | no | `s` | 动作时间 |
| `communication_mode` | `string` | no | - | 通信模式 |
| `recovery_policy` | `string` | no | - | 配置恢复策略 |
| `effective_recovery_policy` | `string` | no | - | 实际执行策略 |
| `action_type` | `string` | no | - | 动作类型 |
| `executor_node_id` | `number` | yes | - | 执行者 |
| `target_node_ids` | `string` | yes | - | 目标节点 ID 列表字符串 |
| `result_state` | `string` | yes | - | 结果状态 |
| `decision_reason` | `string` | yes | - | 决策理由 |
| `scene_type` | `string` | no | - | 场景 |
| `operation_mode` | `string` | no | - | 模式 |

## 4.8 `cooperative.recovery_metrics`

来源：

- `cooperative_recovery_metrics.csv`

说明：

- 是 `metrics_timeseries.samples` 的 CSV 版
- 字段名采用 snake_case
- 与 JSON 一一对应

## 4.9 `cooperative.decision_trace`

来源：

- `cooperative_decision_trace.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `time` | `number` | no | `s` | 决策时刻 |
| `communication_mode` | `string` | no | - | 通信模式 |
| `recovery_policy` | `string` | no | - | 恢复策略 |
| `leader_node_id` | `number` | no | - | 当前 leader |
| `failure_active` | `boolean` | no | - | 故障状态 |
| `recovery_active` | `boolean` | no | - | 恢复状态 |
| `stabilization_active` | `boolean` | no | - | 稳定状态 |
| `active_node_count` | `number` | no | - | 活跃节点数 |
| `effective_recovery_policy` | `string` | no | - | 实际策略 |
| `decision_reason` | `string` | yes | - | 原因 |
| `scene_type` | `string` | no | - | 场景 |
| `operation_mode` | `string` | no | - | 模式 |

## 5. non_cooperative.observation_inference

## 5.1 `observed_signal_events`

来源：

- `observed_signal_events.csv`

核心字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `eventTime` | `number` | no | `s` | 观测事件时间 |
| `observedNodeId` | `number` | no | - | 观测轨迹 ID |
| `txStartTime` | `number` | no | `s` | 发送开始 |
| `txEndTime` | `number` | no | `s` | 发送结束 |
| `txDuration` | `number` | no | `s` | 持续时间 |
| `avgRxPowerDbm` | `number` | yes | `dBm` | 平均接收功率 |
| `channelId` | `number` | yes | - | 信道 |
| `centerFrequencyHz` | `number` | yes | `Hz` | 中心频率 |
| `signalDetected` | `boolean` | no | - | 是否检测到信号 |
| `missingReason` | `string` | yes | - | 漏检原因 |
| `noiseLevel` | `number` | yes | `dB` | 噪声水平 |

## 5.2 `observed_comm_windows`

来源：

- `observed_comm_windows.csv`

核心字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `windowStart` | `number` | no | `s` | 窗口开始 |
| `windowEnd` | `number` | no | `s` | 窗口结束 |
| `observedNodeId` | `number` | no | - | 观测轨迹 ID |
| `txStartTime` | `number` | yes | `s` | 发送开始 |
| `txEndTime` | `number` | yes | `s` | 发送结束 |
| `txDuration` | `number` | yes | `s` | 窗口内持续时长 |
| `windowHitCount` | `number` | yes | - | 命中次数 |
| `windowMissingRatio` | `number` | yes | `ratio` | 漏检比例 |
| `windowConfidence` | `number` | yes | `ratio` | 窗口置信度 |
| `avgEvidenceStrength` | `number` | yes | `ratio` | 平均证据强度 |

## 5.3 `observed_link_evidence`

来源：

- `observed_link_evidence.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `windowStart` | `number` | no | `s` | 窗口开始 |
| `windowEnd` | `number` | no | `s` | 窗口结束 |
| `srcObservedNodeId` | `number` | no | - | 源观测节点 |
| `dstObservedNodeId` | `number` | no | - | 宿观测节点 |
| `evidenceStrength` | `number` | no | `ratio` | 边证据强度 |
| `commCount` | `number` | no | - | 通联计数 |
| `commDurationTotal` | `number` | no | `s` | 累计通联时长 |
| `avgRxPowerDbm` | `number` | yes | `dBm` | 平均接收功率 |
| `channelId` | `number` | yes | - | 信道 |
| `centerFrequencyHz` | `number` | yes | `Hz` | 中心频率 |
| `observerCount` | `number` | no | - | 观察者数 |
| `observerAgreementScore` | `number` | no | `ratio` | 观察者一致性 |
| `edgeObservationConfidence` | `number` | no | `ratio` | 观测置信度 |
| `laggedPredictiveScoreForward` | `number` | no | `ratio` | 正向滞后预测分 |
| `laggedPredictiveScoreBackward` | `number` | no | `ratio` | 反向滞后预测分 |
| `directedResponseScoreForward` | `number` | no | `ratio` | 正向响应分 |
| `directedResponseScoreBackward` | `number` | no | `ratio` | 反向响应分 |
| `excitationScoreForward` | `number` | no | `ratio` | 正向激发分 |
| `excitationScoreBackward` | `number` | no | `ratio` | 反向激发分 |
| `laggedPredictiveScore` | `number` | no | `ratio` | 无向聚合滞后预测分 |
| `directedResponseScore` | `number` | no | `ratio` | 无向聚合响应分 |
| `excitationScore` | `number` | no | `ratio` | 无向聚合激发分 |
| `directionalityScore` | `number` | no | `ratio` | 方向性强度 |
| `dominantDirection` | `string` | no | - | 主方向，如 `A->B` 形式 |
| `isMissing` | `boolean` | no | - | 是否缺失观测 |
| `missingReason` | `string` | yes | - | 缺失原因 |
| `noiseLevel` | `number` | yes | `dB` | 噪声水平 |
| `sceneType` | `string` | no | - | 场景 |
| `operationMode` | `string` | no | - | 模式 |

## 5.4 `inferred_topology_edges`

来源：

- `inferred_topology_edges.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `windowStart` | `number` | no | `s` | 窗口开始 |
| `windowEnd` | `number` | no | `s` | 窗口结束 |
| `srcObservedNodeId` | `number` | no | - | 源观测节点 |
| `dstObservedNodeId` | `number` | no | - | 宿观测节点 |
| `edgeProbability` | `number` | no | `ratio` | 当前边概率 |
| `edgeConfidence` | `number` | no | `ratio` | 当前边置信度 |
| `laggedPredictiveScoreForward` | `number` | no | `ratio` | 正向滞后预测分 |
| `laggedPredictiveScoreBackward` | `number` | no | `ratio` | 反向滞后预测分 |
| `directedResponseScoreForward` | `number` | no | `ratio` | 正向响应分 |
| `directedResponseScoreBackward` | `number` | no | `ratio` | 反向响应分 |
| `excitationScoreForward` | `number` | no | `ratio` | 正向激发分 |
| `excitationScoreBackward` | `number` | no | `ratio` | 反向激发分 |
| `laggedPredictiveScore` | `number` | no | `ratio` | 无向滞后预测分 |
| `directedResponseScore` | `number` | no | `ratio` | 无向响应分 |
| `excitationScore` | `number` | no | `ratio` | 无向激发分 |
| `directionalityScore` | `number` | no | `ratio` | 方向性强度 |
| `dominantDirection` | `string` | no | - | 主方向 |
| `temporalContinuityScore` | `number` | no | `ratio` | 连续性分 |
| `posteriorEdgeProbability` | `number` | no | `ratio` | 平滑后的后验边概率 |
| `edgeDynamicState` | `string` | no | - | `emerging / stable / weakening / vanished` |
| `stabilityAge` | `number` | no | - | 连续稳定窗口数 |
| `weakeningAge` | `number` | no | - | 连续减弱窗口数 |
| `edgeStage` | `string` | no | - | `candidate / final / filtered_out` |
| `falseLinkSuppressionReason` | `string` | yes | - | 伪边抑制原因 |
| `suppressionMediatorObservedNodeId` | `number` | yes | - | 伪边中介节点 |
| `inferenceMethod` | `string` | no | - | 推理方法版本 |
| `sceneType` | `string` | no | - | 场景 |
| `operationMode` | `string` | no | - | 模式 |

## 5.5 `inferred_graph_nodes`

来源：

- `inferred_graph_nodes.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `windowStart` | `number` | no | `s` | 窗口开始 |
| `windowEnd` | `number` | no | `s` | 窗口结束 |
| `observedNodeId` | `number` | no | - | 观测节点 ID |
| `incidentEdgeCount` | `number` | no | - | 关联边数 |
| `weightedDegreeScore` | `number` | no | `ratio` | 加权度分 |
| `avgIncidentProbability` | `number` | no | `ratio` | 平均关联边概率 |
| `avgIncidentConfidence` | `number` | no | `ratio` | 平均关联边置信度 |
| `sceneType` | `string` | no | - | 场景 |
| `operationMode` | `string` | no | - | 模式 |

## 5.6 `key_node_candidates`

来源：

- `key_node_candidates.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `windowStart` | `number` | no | `s` | 窗口开始 |
| `windowEnd` | `number` | no | `s` | 窗口结束 |
| `observedNodeId` | `number` | no | - | 观测节点 ID |
| `rank` | `number` | no | - | 排名 |
| `weightedDegreeScore` | `number` | no | `ratio` | 加权度分 |
| `avgIncidentProbability` | `number` | no | `ratio` | 平均关联边概率 |
| `avgIncidentConfidence` | `number` | no | `ratio` | 平均关联边置信度 |
| `keyNodeScore` | `number` | no | `ratio` | 关键节点总分 |
| `keyNodeMethod` | `string` | no | - | 方法标识 |
| `sceneType` | `string` | no | - | 场景 |
| `operationMode` | `string` | no | - | 模式 |

兼容提醒：

- 若前端历史代码使用：
  - `nodeId`
  - `score`
- 应映射为：
  - `nodeId = observedNodeId`
  - `score = keyNodeScore`

## 6. non_cooperative.attack

## 6.1 `recommendations`

来源：

- `noncooperative_attack_recommendations.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `windowStart` | `number` | no | `s` | 窗口开始 |
| `windowEnd` | `number` | no | `s` | 窗口结束 |
| `recommendedObservedNodeId` | `number` | no | - | 推荐目标观测节点 |
| `recommendedScore` | `number` | no | `ratio` | 最终推荐总分 |
| `recommendationRank` | `number` | no | - | 排名 |
| `recommendationReason` | `string` | no | - | 推荐理由说明 |
| `inferenceMethod` | `string` | no | - | 推荐方法版本 |
| `structureScore` | `number` | no | `ratio` | 结构分 |
| `evidenceSupportScore` | `number` | no | `ratio` | 证据支撑分 |
| `causalSupportScore` | `number` | no | `ratio` | 因果支撑分 |
| `directionalInfluenceScore` | `number` | no | `ratio` | 方向影响分 |
| `temporalStabilityScore` | `number` | no | `ratio` | 时间稳定分 |
| `localBridgeScore` | `number` | no | `ratio` | 局部桥接分 |
| `postRemovalDamageScore` | `number` | no | `ratio` | 移除后损伤分 |
| `twoHopReachabilityScore` | `number` | no | `ratio` | 两跳可达性分 |
| `interClusterBridgeScore` | `number` | no | `ratio` | 簇间桥接分 |
| `localCutRiskScore` | `number` | no | `ratio` | 局部割点风险分 |
| `neighborRedundancyPenalty` | `number` | no | `ratio` | 邻域冗余惩罚 |
| `weightedDegreeCentrality` | `number` | no | `ratio` | 加权度中心性 |
| `weightedBetweennessCentrality` | `number` | no | `ratio` | 加权介数 |
| `weightedClosenessCentrality` | `number` | no | `ratio` | 加权接近 |
| `weightedPageRank` | `number` | no | `ratio` | 加权 PageRank |
| `weightedKShell` | `number` | no | `ratio` | 加权 k-shell |

最小 JSON 示例：

```json
[
  {
    "windowStart": 46.5,
    "windowEnd": 47.0,
    "recommendedObservedNodeId": 200003,
    "recommendedScore": 0.74,
    "recommendationRank": 1,
    "recommendationReason": "fusion(structure|evidence|causality|temporal|bridge|damage)",
    "inferenceMethod": "directed_dynamic_graph_bridge_fusion_v4",
    "structureScore": 0.82,
    "evidenceSupportScore": 0.53,
    "causalSupportScore": 0.61,
    "directionalInfluenceScore": 0.58,
    "temporalStabilityScore": 0.67,
    "localBridgeScore": 1.0,
    "postRemovalDamageScore": 0.44,
    "twoHopReachabilityScore": 0.71,
    "interClusterBridgeScore": 0.36,
    "localCutRiskScore": 0.63,
    "neighborRedundancyPenalty": 0.12
  }
]
```

## 6.2 `plan`

来源：

- `noncooperative_attack_plan.json`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `operationMode` | `string` | no | - | 固定为 `non_cooperative` |
| `sceneType` | `string` | no | - | 场景 |
| `attackType` | `string` | no | - | 当前打击类型 |
| `recommendedObservedNodeId` | `number` | no | - | 推荐目标 |
| `confirmedObservedNodeId` | `number` | no | - | 用户确认目标 |
| `recommendedScore` | `number` | no | `ratio` | 推荐总分 |
| `recommendationReason` | `string` | no | - | 推荐说明 |
| `inferenceMethod` | `string` | no | - | 推荐方法 |
| `structureScore` | `number` | no | `ratio` | 结构分 |
| `evidenceSupportScore` | `number` | no | `ratio` | 证据分 |
| `causalSupportScore` | `number` | no | `ratio` | 因果分 |
| `directionalInfluenceScore` | `number` | no | `ratio` | 方向影响分 |
| `temporalStabilityScore` | `number` | no | `ratio` | 时间稳定分 |
| `localBridgeScore` | `number` | no | `ratio` | 局部桥接分 |
| `postRemovalDamageScore` | `number` | no | `ratio` | 移除后损伤分 |
| `twoHopReachabilityScore` | `number` | no | `ratio` | 两跳可达性分 |
| `interClusterBridgeScore` | `number` | no | `ratio` | 簇间桥接分 |
| `localCutRiskScore` | `number` | no | `ratio` | 局部割点风险分 |
| `neighborRedundancyPenalty` | `number` | no | `ratio` | 邻域冗余惩罚 |
| `userTriggeredExecution` | `boolean` | no | - | 是否由用户确认执行 |
| `attackExecuteTime` | `number` | no | `s` | 计划执行时刻 |
| `targetBindingStatus` | `string` | no | - | 绑定状态 |
| `strikeExecuteTime` | `number` | yes | `s` | 真实执行时刻 |
| `strikeMode` | `string` | no | - | 执行模式 |
| `evaluationWindowStart` | `number` | no | `s` | 评估窗口开始 |
| `evaluationWindowEnd` | `number` | no | `s` | 评估窗口结束 |
| `executedObservedNodeId` | `number` | no | - | 最终执行的观测目标 |
| `executedEntityNodeId` | `number` | no | - | 最终执行的实体节点 |
| `executedTargetObjectKey` | `number` | no | - | 最终目标对象键 |
| `targetNeighborhoodSize` | `number` | no | - | 冻结目标邻域大小 |

最小 JSON 示例：

```json
{
  "operationMode": "non_cooperative",
  "sceneType": "open-field",
  "attackType": "node_strike",
  "recommendedObservedNodeId": 200003,
  "confirmedObservedNodeId": 200003,
  "recommendedScore": 0.74,
  "recommendationReason": "fusion(...)",
  "inferenceMethod": "directed_dynamic_graph_bridge_fusion_v4",
  "structureScore": 0.82,
  "evidenceSupportScore": 0.53,
  "causalSupportScore": 0.61,
  "directionalInfluenceScore": 0.58,
  "temporalStabilityScore": 0.67,
  "localBridgeScore": 1.0,
  "postRemovalDamageScore": 0.44,
  "twoHopReachabilityScore": 0.71,
  "interClusterBridgeScore": 0.36,
  "localCutRiskScore": 0.63,
  "neighborRedundancyPenalty": 0.12,
  "userTriggeredExecution": true,
  "attackExecuteTime": 51.0,
  "targetBindingStatus": "binding_success",
  "strikeExecuteTime": 51.0,
  "strikeMode": "manual_confirmed",
  "evaluationWindowStart": 47.0,
  "evaluationWindowEnd": 59.0,
  "executedObservedNodeId": 200003,
  "executedEntityNodeId": 3,
  "executedTargetObjectKey": 200003,
  "targetNeighborhoodSize": 8
}
```

## 6.3 `events`

来源：

- `noncooperative_attack_events.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `eventTime` | `number` | no | `s` | 事件时间 |
| `attackType` | `string` | no | - | 打击类型 |
| `recommendedObservedNodeId` | `number` | no | - | 推荐目标 |
| `confirmedObservedNodeId` | `number` | no | - | 确认目标 |
| `executedObservedNodeId` | `number` | no | - | 实际执行观测目标 |
| `targetBindingStatus` | `string` | no | - | 绑定状态 |
| `isTrueTargetHit` | `boolean` | no | - | 是否命中真实关键目标 |
| `targetMismatchType` | `string` | yes | - | 误选类型 |
| `nodeRemoved` | `boolean` | no | - | 是否真的移除节点 |
| `executedEntityNodeId` | `number` | no | - | 实体节点 ID |
| `boundTargetObjectKey` | `number` | no | - | 绑定目标对象键 |

## 6.4 `target_binding`

来源：

- `noncooperative_target_binding.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `eventTime` | `number` | no | `s` | 绑定事件时间 |
| `observedNodeId` | `number` | no | - | 观测目标 |
| `bindingStatus` | `string` | no | - | 绑定状态 |
| `bindingConfidence` | `number` | no | `ratio` | 绑定置信度 |
| `isTrackStable` | `boolean` | no | - | 轨迹是否稳定 |
| `isTrackActive` | `boolean` | no | - | 轨迹是否活跃 |
| `boundTargetObjectKey` | `number` | no | - | 绑定目标对象键 |
| `executedEntityNodeId` | `number` | no | - | 实体节点 ID |
| `isTrueCriticalTarget` | `boolean` | no | - | 是否真实关键目标 |
| `mismatchType` | `string` | yes | - | 误配类型 |

## 6.5 `effect_metrics`

来源：

- `noncooperative_attack_effect_metrics.csv`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `time` | `number` | no | `s` | 采样时间 |
| `phase` | `string` | no | - | `pre_attack / immediate_post_attack / recovery / final` |
| `targetScope` | `string` | no | - | `global / target_neighborhood` |
| `connectivityRatio` | `number` | no | `ratio` | 连通率 |
| `pdr` | `number` | no | `ratio` | PDR |
| `throughputMbps` | `number` | no | `Mbps` | 吞吐 |
| `delayMs` | `number` | no | `ms` | 时延 |
| `damageDuration` | `number` | no | `s` | 受损持续时间 |
| `recoveryProgress` | `number` | no | `ratio` | 恢复进度 |
| `recommendedObservedNodeId` | `number` | no | - | 推荐目标 |
| `confirmedObservedNodeId` | `number` | no | - | 确认目标 |
| `executedObservedNodeId` | `number` | no | - | 执行目标 |

注意：

- `target_neighborhood` 是**预冻结目标邻域**
- 不应解释成实时邻接范围

## 6.6 `summary`

来源：

- `noncooperative_pre_post_comparison.json`

结构：

- `phaseMetrics`
- `finalMetrics`
- `recoverySummary`

### 6.6.1 `summary.phaseMetrics`

类型：

- `object`

键：

- `pre_attack`
- `immediate_post_attack`
- `recovery`
- `final`

每个阶段对象都包含：

- `global`
- `target_neighborhood`

`global` / `target_neighborhood` 结构：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `count` | `number` | no | - | 该阶段落入统计的样本数 |
| `connectivityRatio` | `number` | yes | `ratio` | 平均连通率 |
| `pdr` | `number` | yes | `ratio` | 平均 PDR |
| `throughputMbps` | `number` | yes | `Mbps` | 平均吞吐 |
| `delayMs` | `number` | yes | `ms` | 平均时延 |

### 6.6.2 `summary.finalMetrics`

类型：

- `object`

键：

- `global`
- `target_neighborhood`

每个对象包含：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `connectivityRatio` | `number` | yes | `ratio` | 最后一个样本连通率 |
| `pdr` | `number` | yes | `ratio` | 最后一个样本 PDR |
| `throughputMbps` | `number` | yes | `Mbps` | 最后一个样本吞吐 |
| `delayMs` | `number` | yes | `ms` | 最后一个样本时延 |

### 6.6.3 `summary.recoverySummary`

类型：

- `object`

字段：

| 字段 | 类型 | nullable | 单位 | 说明 |
|---|---|---:|---|---|
| `attackExecuted` | `boolean` | no | - | 是否真正执行打击 |
| `actualAttackExecutionTime` | `number` | yes | `s` | 真实执行时刻 |
| `recoveryCompletedAt` | `number` | yes | `s` | 恢复完成时刻 |
| `executedObservedNodeId` | `number` | no | - | 执行观测目标 |
| `executedEntityNodeId` | `number` | no | - | 执行实体目标 |
| `executedTargetObjectKey` | `number` | no | - | 执行目标对象键 |
| `targetNeighborhoodSize` | `number` | no | - | 冻结目标邻域大小 |
| `eventCount` | `number` | no | - | 打击事件数 |

最小 JSON 示例：

```json
{
  "phaseMetrics": {
    "pre_attack": {
      "global": {
        "count": 8,
        "connectivityRatio": 1.0,
        "pdr": 0.72,
        "throughputMbps": 0.41,
        "delayMs": 16.5
      },
      "target_neighborhood": {
        "count": 8,
        "connectivityRatio": 1.0,
        "pdr": 0.69,
        "throughputMbps": 0.38,
        "delayMs": 15.8
      }
    },
    "immediate_post_attack": {
      "global": {
        "count": 4,
        "connectivityRatio": 0.75,
        "pdr": 0.53,
        "throughputMbps": 0.21,
        "delayMs": 73.9
      },
      "target_neighborhood": {
        "count": 4,
        "connectivityRatio": 0.71,
        "pdr": 0.49,
        "throughputMbps": 0.18,
        "delayMs": 79.2
      }
    },
    "recovery": {
      "global": {
        "count": 12,
        "connectivityRatio": 0.75,
        "pdr": 0.56,
        "throughputMbps": 0.24,
        "delayMs": 61.4
      },
      "target_neighborhood": {
        "count": 12,
        "connectivityRatio": 0.71,
        "pdr": 0.52,
        "throughputMbps": 0.20,
        "delayMs": 66.0
      }
    },
    "final": {
      "global": {
        "count": 1,
        "connectivityRatio": 0.75,
        "pdr": 0.57,
        "throughputMbps": 0.25,
        "delayMs": 59.4
      },
      "target_neighborhood": {
        "count": 1,
        "connectivityRatio": 0.71,
        "pdr": 0.53,
        "throughputMbps": 0.21,
        "delayMs": 63.8
      }
    }
  },
  "finalMetrics": {
    "global": {
      "connectivityRatio": 0.75,
      "pdr": 0.57,
      "throughputMbps": 0.25,
      "delayMs": 59.4
    },
    "target_neighborhood": {
      "connectivityRatio": 0.71,
      "pdr": 0.53,
      "throughputMbps": 0.21,
      "delayMs": 63.8
    }
  },
  "recoverySummary": {
    "attackExecuted": true,
    "actualAttackExecutionTime": 51.0,
    "recoveryCompletedAt": null,
    "executedObservedNodeId": 200003,
    "executedEntityNodeId": 3,
    "executedTargetObjectKey": 200003,
    "targetNeighborhoodSize": 8,
    "eventCount": 2
  }
}
```

## 7. 最小对接建议

如果前端先做最小可用联调，优先消费：

合作模式：

- `meta`
- `manifest`
- `shared.environment_summary`
- `cooperative.mode_summary`
- `cooperative.dashboard_snapshot`
- `cooperative.metrics_timeseries`
- `cooperative.failure_timeline`
- `cooperative.recovery_timeline`

非合作模式：

- `meta`
- `manifest`
- `shared.environment_summary`
- `non_cooperative.observation_inference.inferred_topology_edges`
- `non_cooperative.observation_inference.key_node_candidates`
- `non_cooperative.attack.recommendations`
- `non_cooperative.attack.plan`
- `non_cooperative.attack.effect_metrics`
- `non_cooperative.attack.summary`

## 8. 文档边界说明

- 本文档以当前后端真实输出为准
- 某些 CSV 明细文件可能后续追加新列，前端应采用“字段存在性判断”而不是硬编码全列
- 本文档以当前后端真实输出为准；若前端历史代码仍使用旧字段名，应通过前端适配层做一次别名映射
