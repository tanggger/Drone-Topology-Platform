# 前端开发对接清单

本文档基于当前后端实际实现整理，目标是让前端按页面和组件直接开始接入，而不是再从接口文档里自行拆任务。

配套文档：

- [FRONTEND_API_INTEGRATION_GUIDE.md](/home/tzx/ns-3.43/FRONTEND_API_INTEGRATION_GUIDE.md)
- [TWO_WEEK_EXECUTION_PLAN.md](/home/tzx/ns-3.43/project_docs/TWO_WEEK_EXECUTION_PLAN.md)

## 1. 总体结论

前端现在可以直接按两条工作区接入：

1. 合作模式工作区
2. 非合作模式工作区

统一依赖的后端接口只有这几个：

- `GET /api/health`
- `POST /api/simulate`
- `GET /api/results/<task_id>/frontend`
- `GET /api/results/<task_id>/manifest`
- `GET /api/maps`
- `GET /api/map_data/<map_name>`
- `POST /api/upload_osm`

前端主读接口统一使用：

- `GET /api/results/<task_id>/frontend`

## 2. 第一优先级页面

## 2.1 模式入口页

目标：

- 先选场景
- 再选任务模式
- 再进入合作或非合作工作区

前端需要接的接口：

- `GET /api/maps`
- `GET /api/map_data/<map_name>`

前端需要显示的信息：

- 地图列表
- 场景类型：
  - `urban`
  - `forest`
  - `lake`
  - `open-field`
- 任务模式：
  - `cooperative`
  - `non_cooperative`

建议组件：

- 场景卡片列表
- 地图预览卡片
- 模式选择入口按钮

## 2.2 仿真任务发起面板

目标：

- 统一调用 `POST /api/simulate`
- 覆盖合作、非合作、场景真实性增强参数

前端需要接的接口：

- `POST /api/simulate`

建议最先支持的参数组：

- 通用参数
  - `operationMode`
  - `sceneType`
  - `difficulty`
  - `formation`
  - `strategy`
  - `num_drones`
  - `formation_spacing`
  - `start`
  - `target`
  - `map_name`
  - `mapFile`

- 合作模式参数
  - `communicationMode`
  - `leaderNodeId`
  - `backupLeaderList`
  - `distributedHopLimit`
  - `cooperativeFailureType`
  - `failureTargetId`
  - `failureStartTime`
  - `failureDuration`
  - `recoveryPolicy`
  - `recoveryObjective`
  - `recoveryCooldown`
  - `allowChannelReallocation`
  - `allowPowerAdjustment`
  - `allowRateAdjustment`
  - `allowRelayReselection`
  - `allowSlotReallocation`
  - `allowRouteRebuild`

- 非合作打击参数
  - `enableNonCooperativeAttack`
  - `attackType`
  - `manualStrikeTarget`
  - `attackExecuteTime`
  - `attackEvaluationDuration`
  - `attackNeighborhoodHop`

- 场景真实性增强参数
  - `urbanAltitudePenaltyDbLow`
  - `urbanAltitudeGainDbHigh`
  - `urbanStreetCanyonFactor`
  - `lakeVolatilityJitterDb`
  - `lakeDeepFadeProbability`
  - `lakeDeepFadeMaxDb`
  - `lakeReflectionDelayJitterMs`
  - `carrierFrequencyGHz`
  - `channelBandwidthMHz`
  - `polarizationMode`
  - `reroutePressureFactor`
  - `controlMessageUrgencyFactor`
  - `relayInstabilityFactor`
  - `formationReconfigPenalty`

建议组件：

- 基础任务表单
- 合作模式高级设置
- 非合作打击设置
- 场景真实性增强设置

## 3. 第二优先级页面

## 3.1 统一任务状态页

目标：

- 提交任务后轮询结果
- 根据 `manifest` 决定进合作还是非合作工作区

前端需要接的接口：

- `GET /api/results/<task_id>/manifest`
- `GET /api/results/<task_id>/frontend`

建议逻辑：

1. 先轮询 `/frontend`
2. 若成功，再读 `data.manifest`
3. 根据：
   - `operationMode`
   - `hasNonCooperativeAttack`
   - `cooperativeDatasets`
   - `nonCooperativeDatasets`
   - `nonCooperativeAttackDatasets`
   跳转工作区

建议组件：

- 任务状态卡
- 输出目录摘要
- 数据集可用性列表

## 4. 合作模式工作区清单

## 4.1 顶部状态条

数据来源：

- `data.meta`
- `data.shared.environment_summary`
- `data.cooperative.dashboard_snapshot`

必须展示：

- `sceneType`
- `difficulty`
- `communicationMode`
- `phase`
- `leaderNodeId`
- `isLeaderAlive`
- `failureTargetId`
- `recoveryStatus`

## 4.2 左侧模式与环境信息卡

数据来源：

- `data.cooperative.mode_summary`
- `data.shared.environment_summary`

必须展示：

- `communicationMode`
- `leaderNodeId`
- `backupLeaderList`
- `distributedHopLimit`
- `failureType`
- `failureTargetId`
- `failureStartTime`
- `failureDuration`
- `recoveryPolicy`
- `recoveryObjective`
- `recoveryCooldown`
- `effectiveModelSummary`
- `carrierFrequencyGHz`
- `channelBandwidthMHz`
- `polarizationMode`

## 4.3 合作时序图

数据来源：

- `data.cooperative.metrics_timeseries.samples`

第一批必须画的曲线：

- `connectivity`
- `pdr`
- `throughputMbps`
- `delayMs`
- `p99DelayMs`

第二批建议加：

- `failureNeighborhoodPdr`
- `failureNeighborhoodThroughputMbps`
- `failureNeighborhoodDelayMs`
- `failureTargetPdr`
- `failureTargetThroughputMbps`
- `failureTargetDelayMs`

第三批建议加：

- `routeChangeCount`
- `relaySwitchCount`
- `controlDeadlineMissCount`
- `routePressureScore`

注意事项：

- `failureNeighborhood*` 是冻结后的故障邻域
- `leaderNodeId`、`isLeaderAlive` 是离散状态，不适合普通折线
- 时间字段允许 `null`，不要补成 `0`

## 4.4 故障与恢复时间线

数据来源：

- `data.cooperative.failure_timeline.events`
- `data.cooperative.recovery_timeline.actions`

必须展示：

- 故障触发
- 故障解除
- 恢复动作发生顺序
- Leader 切换事件

建议组件：

- 时间轴
- 动作列表
- 当前步骤高亮

## 4.5 3D 沙盘 / 节点回放

数据来源：

- `data.shared.positions`
- `data.shared.transmissions`
- `data.shared.topology_links`
- `data.shared.resource_detailed`

第一批先实现：

- 节点位置回放
- 节点类型着色
- 拓扑连线
- 通信闪烁

第二批再加：

- 节点资源信息：
  - `channel`
  - `power`
  - `data_rate`
  - `neighbors`
  - `interference`
  - `sinr`

## 5. 非合作模式工作区清单

## 5.1 顶部状态条

数据来源：

- `data.meta`
- `data.shared.environment_summary`
- `data.non_cooperative.attack.plan`

必须展示：

- `sceneType`
- `difficulty`
- `attackType`
- `recommendedObservedNodeId`
- `confirmedObservedNodeId`
- `executedObservedNodeId`
- `executedEntityNodeId`
- `targetBindingStatus`

## 5.2 观测与推断基础层

数据来源：

- `data.non_cooperative.observation_inference.observed_signal_events`
- `data.non_cooperative.observation_inference.observed_comm_windows`
- `data.non_cooperative.observation_inference.observed_link_evidence`
- `data.non_cooperative.observation_inference.inferred_topology_edges`
- `data.non_cooperative.observation_inference.inferred_graph_nodes`
- `data.non_cooperative.observation_inference.key_node_candidates`

第一批必须展示：

- 观测节点列表
- 推断边列表
- 关键节点 Top-N

第二批建议展示：

- 边证据分解
- 方向性信息
- 伪边过滤原因

特别要显示的字段：

- `directionalityScore`
- `dominantDirection`
- `edgeDynamicState`
- `edgeStage`
- `falseLinkSuppressionReason`
- `suppressionMediatorObservedNodeId`

## 5.3 推荐目标面板

数据来源：

- `data.non_cooperative.attack.recommendations`
- `data.non_cooperative.attack.plan`

第一批必须展示：

- 当前 top-1 推荐目标
- 推荐分数
- 推荐理由
- 当前方法名

第二批建议展示推荐分解：

- `structureScore`
- `evidenceSupportScore`
- `causalSupportScore`
- `directionalInfluenceScore`
- `temporalStabilityScore`
- `localBridgeScore`
- `postRemovalDamageScore`
- `twoHopReachabilityScore`
- `interClusterBridgeScore`
- `localCutRiskScore`
- `neighborRedundancyPenalty`

建议组件：

- 推荐目标卡片
- Top-K 列表
- 推荐分解柱状图

## 5.4 打击执行与绑定面板

数据来源：

- `data.non_cooperative.attack.plan`
- `data.non_cooperative.attack.events`
- `data.non_cooperative.attack.target_binding`

必须展示：

- 是否执行
- 执行时间
- 绑定是否成功
- 真实执行实体
- 是否命中真实关键目标

建议组件：

- 打击执行结果卡
- 绑定状态表
- 事件时间线

## 5.5 打击效果评估图

数据来源：

- `data.non_cooperative.attack.effect_metrics`
- `data.non_cooperative.attack.summary`

第一批必须画：

- `global.connectivityRatio`
- `global.pdr`
- `global.throughputMbps`
- `global.delayMs`

第二批必须补：

- `target_neighborhood.connectivityRatio`
- `target_neighborhood.pdr`
- `target_neighborhood.throughputMbps`
- `target_neighborhood.delayMs`

说明：

- `target_neighborhood` 是预冻结的局部评估范围
- 不能把它理解成实时重算邻域
- `phase` 建议用阶段条带展示，不要普通折线

## 6. 第一批最小交付顺序

如果前端现在要最短时间联调，我建议按下面顺序做：

1. 模式入口页
2. 仿真任务发起面板
3. 统一任务状态页
4. 合作模式工作区
   - 顶部状态条
   - 模式摘要卡
   - 合作时序图
   - 故障恢复时间线
5. 非合作模式工作区
   - 推断边列表
   - 关键节点 Top-N
   - 推荐目标卡
   - 打击效果图

## 7. 第二批增强交付

等第一批联调通了以后，再补这些：

- 3D 沙盘精细联动
- 推荐分解图
- 方向性边高亮
- 伪边过滤解释
- 路由压力 / 中继切换 / 控制时效图
- 更完整的节点详情面板

## 8. 一句话交接

前端现在不需要再猜后端能力边界了。

直接按：

- 合作模式工作区
- 非合作模式工作区
- 推荐 / 打击 / 评估闭环

三条线开发即可。前端主读接口统一使用：

- `GET /api/results/<task_id>/frontend`
