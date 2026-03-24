# Wing-Net Omni 前端接口对接指南

本文档面向前端开发人员，覆盖当前后端已经完成的能力、可直接接入的接口、推荐的数据读取方式，以及合作/非合作两类工作区的数据映射关系。

当前结论只有一句：

- 可以先启动前端搭建与联调
- 可以先跑合作模式和非合作观测/推断效果
- 非合作“基于推断结果再去做打击/压制/节点破坏”可以后置，不影响当前前端主工作区搭建

## 1. 当前后端能力边界

当前后端已经具备：

- 多场景环境层
  - `urban`
  - `forest`
  - `lake`
  - `open-field`
- 合作模式
  - `centralized`
  - `distributed`
  - `hybrid`
- 合作模式完整输出
  - 模式摘要
  - 故障事件时间线
  - 恢复动作时间线
  - 恢复指标时间序列
  - 仪表盘快照
- 非合作模式当前完成到
  - 观测事件
  - 窗口化观测
  - 边证据
  - 推断边
  - 图节点
  - 关键节点候选

当前后端还没有作为正式前端能力交付的内容：

- 非合作模式下基于推断结果的正式“打击/压制/节点破坏”工作流接口
- 非合作打击后的专属效果评估 API 结果面板

因此前端当前应分两条线推进：

1. 合作模式工作区直接按完整工作流接入
2. 非合作模式工作区先做到“观测 -> 推断 -> 关键节点识别”展示

## 2. 接口总览

基准地址：

```text
http://<backend-host>:5000
```

后端已开启 CORS。

当前推荐前端使用的接口：

- `GET /api/health`
- `POST /api/simulate`
- `GET /api/results/<task_id>/frontend`
- `GET /api/results/<task_id>/manifest`
- `GET /api/maps`
- `GET /api/map_data/<map_name>`
- `POST /api/upload_osm`

兼容旧接口：

- `GET /api/results/<task_id>`

说明：

- 新前端优先使用 `/api/results/<task_id>/frontend`
- `/api/results/<task_id>` 保留，用于兼容老前端和调试

## 3. 健康检查

### 3.1 请求

```http
GET /api/health
```

### 3.2 响应

```json
{
  "status": "OK",
  "message": "Wing-Net Omni Backend Server is running."
}
```

## 4. 发起仿真任务

### 4.1 请求

```http
POST /api/simulate
Content-Type: application/json
```

### 4.2 前端推荐请求体

```json
{
  "operationMode": "cooperative",
  "communicationMode": "hybrid",
  "sceneType": "urban",
  "difficulty": "Moderate",
  "formation": "v_formation",
  "strategy": "dynamic",
  "num_drones": 15,
  "formation_spacing": 12.0,
  "start": "0,0,30",
  "target": "0,600,30",
  "map_name": "map_test",

  "leaderNodeId": 0,
  "backupLeaderList": "2,3,4",
  "distributedHopLimit": 1,

  "cooperativeFailureType": "node_failure",
  "failureTargetId": 7,
  "failureStartTime": 80.0,
  "failureDuration": 40.0,

  "recoveryPolicy": "global_recovery",
  "recoveryObjective": "connectivity",
  "recoveryCooldown": 1.0,

  "allowChannelReallocation": true,
  "allowPowerAdjustment": true,
  "allowRateAdjustment": true,
  "allowRelayReselection": true,
  "allowSlotReallocation": true,
  "allowRouteRebuild": true,

  "buildings": [],

  "pathLossExp": 2.8,
  "rxSens": -90.0,
  "txPower": 23.0,
  "nakagamiM": 0.0,
  "macRetries": 7,
  "noiseFigure": 7.0,
  "rtkNoise": 0.01,
  "rtkDriftMag": 0.0,
  "rtkDriftInt": 0.0,
  "rtkDriftDur": 0.0,
  "trafficLoad": 0.2,
  "numInterfere": 0,
  "interfereRate": 0.5,
  "interfereDuty": 0.1
}
```

### 4.3 请求字段说明

#### 4.3.1 通用字段

- `operationMode`
  - `cooperative`
  - `non_cooperative`
- `sceneType`
  - `urban`
  - `forest`
  - `lake`
  - `open-field`
- `difficulty`
  - `Easy`
  - `Moderate`
  - `Hard`
  - `Custom`
- `formation`
  - `v_formation`
  - `line`
  - `cross`
  - `triangle`
- `strategy`
  - `dynamic`
  - `static`
- `num_drones`
- `formation_spacing`
- `start`
- `target`
- `map_name`
  - 对应后端已导入地图名称
- `map_file` 或 `mapFile`
  - 可选，允许显式传相对路径或绝对路径
- `buildings`
  - 当前没有选地图时可直接传前端编辑器生成的建筑盒体

#### 4.3.2 合作模式字段

- `communicationMode`
  - `centralized`
  - `distributed`
  - `hybrid`
- `leaderNodeId`
- `backupLeaderList`
  - 逗号分隔字符串，例如 `"2,3,4"`
- `distributedHopLimit`
  - `1` 或 `2`
- `cooperativeFailureType`
  - `node_failure`
  - `environment_degradation`
  - `external_interference`
  - `link_degradation`
- `failureTargetId`
- `failureStartTime`
- `failureDuration`
- `recoveryPolicy`
  - `global_recovery`
  - `local_recovery`
- `recoveryObjective`
  - `connectivity`
  - `delay`
  - `throughput`
  - `pdr`
- `recoveryCooldown`
- 动作开关：
  - `allowChannelReallocation`
  - `allowPowerAdjustment`
  - `allowRateAdjustment`
  - `allowRelayReselection`
  - `allowSlotReallocation`
  - `allowRouteRebuild`

#### 4.3.3 Custom 难度字段

只有当 `difficulty = Custom` 时，以下字段才会生效：

- `pathLossExp`
- `rxSens`
- `txPower`
- `nakagamiM`
- `macRetries`
- `noiseFigure`
- `rtkNoise`
- `rtkDriftMag`
- `rtkDriftInt`
- `rtkDriftDur`
- `trafficLoad`
- `numInterfere`
- `interfereRate`
- `interfereDuty`

### 4.4 响应

```json
{
  "message": "Simulation triggered successfully",
  "task_id": "a1b2c3d4",
  "status": "RUNNING"
}
```

## 5. 轮询策略

前端推荐轮询：

- 第一次提交后等待 `2s`
- 之后每 `3s ~ 5s` 轮询一次

轮询优先接口：

```text
GET /api/results/<task_id>/frontend
```

状态返回：

- `RUNNING`
- `FAILED`
- `SUCCESS`

## 6. 结果接口

## 6.1 前端主接口

### 6.1.1 请求

```http
GET /api/results/<task_id>/frontend
```

### 6.1.2 响应结构

```json
{
  "status": "SUCCESS",
  "data": {
    "meta": {
      "taskId": "a1b2c3d4",
      "operationMode": "cooperative",
      "sceneType": "urban",
      "difficulty": "Moderate",
      "communicationMode": "hybrid",
      "formation": "v_formation"
    },
    "shared": {
      "environment_summary": {},
      "request_metadata": {},
      "positions": [],
      "transmissions": [],
      "topology_links": [],
      "topology_evolution": [],
      "topology_detailed": [],
      "resource_detailed": [],
      "qos": [],
      "flow_summary": []
    },
    "cooperative": {},
    "non_cooperative": {},
    "manifest": {}
  }
}
```

说明：

- `shared` 是所有模式共享的数据层
- `cooperative` 只有在合作模式下才有值
- `non_cooperative` 只有在非合作模式下才有值
- `manifest` 用于判断当前有哪些数据集已经存在

## 6.2 Manifest 接口

### 6.2.1 请求

```http
GET /api/results/<task_id>/manifest
```

### 6.2.2 用途

用于前端在进入工作区前先判断：

- 当前任务属于哪种模式
- 当前场景是什么
- 当前有哪些结果文件已经可用
- 该走合作工作区还是非合作工作区

### 6.2.3 响应结构

```json
{
  "status": "SUCCESS",
  "data": {
    "taskId": "a1b2c3d4",
    "outputDir": "output/run_a1b2c3d4",
    "operationMode": "cooperative",
    "sceneType": "urban",
    "difficulty": "Moderate",
    "communicationMode": "hybrid",
    "formation": "v_formation",
    "sharedDatasets": ["positions", "qos", "environment_summary"],
    "cooperativeDatasets": ["mode_summary", "dashboard_snapshot"],
    "nonCooperativeDatasets": [],
    "availableFiles": []
  }
}
```

## 6.3 兼容结果接口

### 6.3.1 请求

```http
GET /api/results/<task_id>
```

### 6.3.2 说明

该接口保留旧结构，并在 `data` 里增加了：

- `environment_summary`
- `request_metadata`
- `frontend_manifest`
- `cooperative`
- `non_cooperative`
- `frontend`

旧前端仍可继续读取：

- `positions`
- `qos`
- `topology_evolution`
- `transmissions`
- `topology_links`
- `flow_summary`
- `resource_detailed`
- `topology_detailed`

## 7. shared 数据层

这些数据无论合作还是非合作都可以直接接。

### 7.1 `shared.environment_summary`

文件来源：

- `environment_summary.json`

主要字段：

- `operationMode`
- `sceneType`
- `difficulty`
- `baseModel`
- `environmentSource`
- `geometryInputMode`
- `effectiveModelSummary`
- `hasBuildings`
- `hasVegetation`
- `hasWaterSurface`
- `reflectionAware`
- `interferenceFactor`
- `connectivityRangeFactor`
- `pathLossExponent`
- 合作模式下还会包含：
  - `communicationMode`
  - `leaderNodeId`
  - `cooperativeFailureType`
  - `failureTargetId`
  - `recoveryPolicy`
  - `recoveryObjective`

前端用途：

- 顶部状态栏
- 左侧环境信息卡
- 工作区入口页模式判断

### 7.2 `shared.positions`

文件来源：

- `rtk-node-positions.csv`
- 后端已自动合并 `resource_allocation_detailed.csv` 中的部分资源信息

常用字段：

- `time`
- `nodeId`
- `x`
- `y`
- `z`
- `node_type`
- `speed`
- 可能存在合并字段：
  - `power`
  - `channel`
  - `data_rate`
  - `neighbors`
  - `interference`
  - `sinr`

前端用途：

- 3D 沙盘位置回放
- 单机详情
- 节点状态着色

### 7.3 `shared.transmissions`

文件来源：

- `rtk-node-transmissions.csv`

前端用途：

- 激光/通信动画
- 时间轴上的通信事件提示

### 7.4 `shared.topology_links`

文件来源：

- `rtk-topology-changes.txt`

前端用途：

- 3D 连线回放
- 拓扑变化动画

### 7.5 `shared.topology_evolution`

文件来源：

- `topology_evolution.csv`

前端用途：

- 连通率曲线
- 链路数演化曲线

### 7.6 `shared.topology_detailed`

文件来源：

- `topology_detailed.csv`

前端用途：

- 平均度
- 网络密度
- 右侧实时网络统计面板

### 7.7 `shared.resource_detailed`

文件来源：

- `resource_allocation_detailed.csv`

前端用途：

- 节点资源详情
- 受干扰程度
- 当前信道/功率/速率显示

### 7.8 `shared.qos`

文件来源：

- `qos_performance.csv`

前端用途：

- PDR 曲线
- 吞吐曲线
- 时延曲线

### 7.9 `shared.flow_summary`

文件来源：

- `rtk-flow-stats.csv`

前端用途：

- 最终结算面板
- 流级统计表

## 8. 合作模式工作区接入

### 8.1 数据入口

前端判断：

```ts
data.meta.operationMode === "cooperative"
```

之后读取：

- `data.shared.*`
- `data.cooperative.*`

### 8.2 `cooperative.mode_summary`

文件来源：

- `cooperative_mode_summary.json`

主要字段：

- `operationMode`
- `communicationMode`
- `recoveryPolicy`
- `recoveryObjective`
- `sceneType`
- `difficulty`
- `formation`
- `leaderNodeId`
- `backupLeaderList`
- `distributedHopLimit`
- `failureType`
- `failureTargetId`
- `failureStartTime`
- `failureDuration`
- `recoveryCooldown`
- `actionFlags`

前端用途：

- 左侧模式配置摘要
- 顶部状态栏
- 合作模式信息总卡

### 8.3 `cooperative.dashboard_snapshot`

文件来源：

- `cooperative_dashboard_snapshot.json`

主要字段：

- `phase`
- `operationMode`
- `communicationMode`
- `leaderNodeId`
- `backupLeaderId`
- `isLeaderAlive`
- `failureActive`
- `failureType`
- `failureTargetId`
- `responseTimeSec`
- `recoveryTimeSec`
- `stabilizationTimeSec`
- `recoveryStatus`
- `latestRecoveryAction`

字段语义补充：

- `recoveryStatus` 当前值域为：
  - `not_triggered`
  - `active`
  - `completed`
  - `stable`
- `responseTimeSec`、`recoveryTimeSec`、`stabilizationTimeSec` 允许为 `null`
- 前端不得把 `null` 渲染成 `0`
  - `null` 表示当前运行尚未形成对应时间结论
  - `0` 表示真实的零时延结果，两者语义不同
- `latestRecoveryAction` 对应当前已记录的最后一条恢复动作类型

前端用途：

- 右侧恢复总览卡片
- 底部状态条
- 顶部当前阶段徽章

### 8.4 `cooperative.failure_timeline`

文件来源：

- `cooperative_failure_timeline.json`

结构：

- `events[]`

事件字段：

- `time`
- `failureType`
- `targetNodeId`
- `targetRole`
- `isLeaderTarget`
- `failureState`
- `affectedNeighborCount`
- `affectedLinkCount`
- `effectSummary`
- `source`

前端用途：

- 底部事件时间轴
- 故障事件列表
- 3D 沙盘故障提示

### 8.5 `cooperative.recovery_timeline`

文件来源：

- `cooperative_recovery_timeline.json`

结构：

- `actions[]`

动作字段：

- `time`
- `phase`
- `communicationMode`
- `recoveryPolicy`
- `effectiveRecoveryPolicy`
- `triggerReason`
- `executorNodeId`
- `targetNodeIds`
- `actionType`
- `oldValue`
- `newValue`
- `scope`
- `expectedEffect`
- `resultState`

前端用途：

- 恢复动作时间线
- 恢复步骤列表
- 单机详情里的“当前是否参与恢复”

### 8.6 `cooperative.metrics_timeseries`

文件来源：

- `cooperative_metrics_timeseries.json`

结构：

- `samples[]`

样本字段：

- `time`
- `phase`
- `connectivity`
- `avgDegree`
- `pdr`
- `throughputMbps`
- `delayMs`
- `p99DelayMs`
- `failureNeighborhoodPdr`
- `failureNeighborhoodThroughputMbps`
- `failureNeighborhoodDelayMs`
- `failureNeighborhoodNodeCount`
- `failureTargetId`
- `isFailureTargetFailed`
- `failureTargetPdr`
- `failureTargetThroughputMbps`
- `failureTargetDelayMs`
- `activeNodeCount`
- `leaderNodeId`
- `isLeaderAlive`
- `responseTimeSec`
- `recoveryTimeSec`
- `stabilizationTimeSec`

字段语义补充：

- `activeNodeCount`、`leaderNodeId`、`isLeaderAlive` 是逐样本历史值
- 不要把它们当成最终态字段再回放整条曲线
- `leaderNodeId` 在 Leader 切换场景下会真实反映：
  - 故障前 Leader
  - 故障检测后失活阶段
  - 备份 Leader 接管后的新值
- `responseTimeSec`、`recoveryTimeSec`、`stabilizationTimeSec` 允许按样本为 `null`
- 这些时间字段推荐只在状态卡或光标定位样本上展示，不建议对整条曲线做插值补零

恢复判定口径：

- 恢复完成不是只看全网平均业务指标
- 当前实现使用：
  - 全局 `connectivity`
  - 故障目标邻域业务指标 `PDR / throughput / delay`
- 若当前不存在有效故障邻域，则回退到全局平均业务指标
- 满足规则：
  - 全局 `connectivity` 达标
  - 且业务三项中至少两项达标

前端可视化建议：

- 原有 `pdr / throughputMbps / delayMs` 继续作为全局平均曲线
- 需要明确展示故障冲击时，优先画：
  - `failureNeighborhoodPdr`
  - `failureNeighborhoodThroughputMbps`
  - `failureNeighborhoodDelayMs`
- 需要明确展示“被打掉目标”时，优先画：
  - `failureTargetPdr`
  - `failureTargetThroughputMbps`
  - `failureTargetDelayMs`
- 这三层曲线建议分组展示：
  - 全局平均
  - 故障邻域
  - 故障目标

前端用途：

- 右侧时序图
- 连通率恢复曲线
- PDR / 吞吐 / 时延 / P99 时延图
- 响应时间 / 恢复时间 / 稳定时间展示

### 8.7 CSV 明细

前端一般不直接首选这些 CSV 明细，但需要时可从 `data.cooperative` 中读取：

- `failure_events`
- `recovery_actions`
- `recovery_metrics`
- `decision_trace`

建议用途：

- 调试抽屉
- 高级表格
- 导出按钮

## 9. 非合作模式工作区接入

### 9.1 数据入口

前端判断：

```ts
data.meta.operationMode === "non_cooperative"
```

之后读取：

- `data.shared.*`
- `data.non_cooperative.*`

### 9.2 当前非合作工作区的正式展示边界

当前前端应该正式展示：

1. 观测事件
2. 窗口化观测
3. 边证据
4. 推断边
5. 图节点
6. 关键节点候选

当前不应默认承诺已经具备的前端能力：

1. 非合作打击控制面板真实下发
2. 基于推断结果的正式压制/打击执行结果
3. 打击前后专属效果评估页

### 9.3 `non_cooperative.observed_signal_events`

文件来源：

- `observed_signal_events.csv`

前端用途：

- 观测事件面板
- 单节点观测历史

### 9.4 `non_cooperative.observed_comm_windows`

文件来源：

- `observed_comm_windows.csv`

前端用途：

- 当前窗口摘要
- 中间沙盘“观测到的活动节点”

### 9.5 `non_cooperative.observed_link_evidence`

文件来源：

- `observed_link_evidence.csv`

前端用途：

- 边证据面板
- 推断前证据层

### 9.6 `non_cooperative.inferred_topology_edges`

文件来源：

- `inferred_topology_edges.csv`

前端用途：

- 中间沙盘推断概率边
- 右侧推断结果列表

### 9.7 `non_cooperative.inferred_graph_nodes`

文件来源：

- `inferred_graph_nodes.csv`

前端用途：

- 图表示节点信息
- 节点权重信息展示

### 9.8 `non_cooperative.key_node_candidates`

文件来源：

- `key_node_candidates.csv`

前端用途：

- 关键节点高亮
- Top-N 关键节点面板

## 10. 地图接口

### 10.1 地图列表

```http
GET /api/maps
```

响应：

```json
{
  "status": "SUCCESS",
  "maps": ["map_test", "map_city_a"]
}
```

### 10.2 读取建筑渲染数据

```http
GET /api/map_data/<map_name>
```

用途：

- 场景入口页地图预览
- 中间沙盘建筑图层

### 10.3 上传 OSM

```http
POST /api/upload_osm
```

前端使用 `multipart/form-data`。

## 11. 前端推荐接线方式

### 11.1 模式入口页

进入任务详情页前，先读：

- `sceneType`
- `operationMode`
- `difficulty`
- `formation`

推荐来源：

- 启动参数表单本地状态
- 任务完成后由 `manifest` 和 `meta` 校验

### 11.2 合作工作区

推荐读取顺序：

1. `data.meta`
2. `data.shared.environment_summary`
3. `data.cooperative.mode_summary`
4. `data.cooperative.dashboard_snapshot`
5. `data.shared.positions`
6. `data.shared.topology_links`
7. `data.cooperative.metrics_timeseries`
8. `data.cooperative.failure_timeline`
9. `data.cooperative.recovery_timeline`

### 11.3 非合作工作区

推荐读取顺序：

1. `data.meta`
2. `data.shared.environment_summary`
3. `data.non_cooperative.observed_signal_events`
4. `data.non_cooperative.observed_comm_windows`
5. `data.non_cooperative.observed_link_evidence`
6. `data.non_cooperative.inferred_topology_edges`
7. `data.non_cooperative.inferred_graph_nodes`
8. `data.non_cooperative.key_node_candidates`

## 12. Axios 示例

```ts
import axios from "axios";

const BASE_URL = "http://localhost:5000";

export async function startSimulation(payload: Record<string, unknown>) {
  const res = await axios.post(`${BASE_URL}/api/simulate`, payload);
  return res.data;
}

export async function pollFrontendResults(taskId: string) {
  const res = await axios.get(`${BASE_URL}/api/results/${taskId}/frontend`);
  return res.data;
}

export async function pollManifest(taskId: string) {
  const res = await axios.get(`${BASE_URL}/api/results/${taskId}/manifest`);
  return res.data;
}
```

轮询示例：

```ts
async function waitForResult(taskId: string) {
  while (true) {
    const res = await pollFrontendResults(taskId);
    if (res.status === "SUCCESS") return res.data;
    if (res.status === "FAILED") throw new Error(res.error || "Simulation failed");
    await new Promise((r) => setTimeout(r, 3000));
  }
}
```

## 13. 前端实现建议

### 13.1 不要把合作和非合作工作区混成一个默认大页

推荐结构：

1. 入口页
2. 合作工作区
3. 非合作工作区

### 13.2 统一用 `meta.operationMode` 做页面分流

不要靠某个文件是否存在来猜模式。

### 13.3 前端状态建议拆成两层

- `runMeta`
  - 任务、模式、场景、难度、编队
- `workspaceData`
  - shared
  - cooperative
  - nonCooperative

### 13.4 大数组不要无脑全量响应式深拷贝

尤其是：

- `positions`
- `resource_detailed`
- `qos`
- `metrics_timeseries.samples`

建议按当前时间窗口切片或建立索引。

## 14. 当前最适合前端先接的部分

如果要尽快看到界面结果，优先顺序建议是：

1. 先接 `meta + manifest`
2. 再接 `shared.environment_summary`
3. 再接 `shared.positions + topology_links`
4. 合作模式先接 `dashboard_snapshot + metrics_timeseries`
5. 非合作模式先接 `inferred_topology_edges + key_node_candidates`
6. 最后补故障时间线、恢复时间线和高级明细表

前端接入注意：

- `dashboard_snapshot.recoveryTimeSec = null` 时，不要显示为 `0s`
- `metrics_timeseries.samples[*].leaderNodeId` 和 `isLeaderAlive` 应直接用于 Leader 切换时间线
- 需要展示“恢复完成但未稳定”时，应依据：
  - `recoveryStatus = completed`
  - 而不是只看 `phase = recovery`

## 15. 一句话交付边界

当前这套接口已经足够支持你先把前端合作工作区和非合作观测/推断工作区搭起来，并先验证仿真效果。

非合作“基于推断结果的正式打击/压制/节点破坏”可以放到前端效果稳定之后再做，不会卡住现在的前后端联调。
