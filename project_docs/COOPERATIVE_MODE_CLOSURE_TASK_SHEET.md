# 合作场景主线收口开发任务单

## 1. 文档目的

本任务单用于一次性完成 `operationMode=cooperative` 主线的后端收口，使合作场景具备：

- 明确的模式差异
- 明确的故障输入
- 明确的恢复策略
- 明确的评估指标
- 可直接供前端消费的输出数据

## 2. 完成定义

满足以下条件即视为完成：

1. 后端正式支持三种合作架构模式：
   - `centralized`
   - `distributed`
   - `hybrid`
2. 三种模式存在真实行为差异，不只是枚举名不同。
3. 后端正式支持合作场景故障注入、恢复触发、恢复动作记录与恢复效果评估。
4. 后端正式输出合作场景专属 `JSON + CSV` 数据。
5. 四个场景全部完成三种模式对比：
   - `urban`
   - `forest`
   - `lake`
   - `open-field`
6. 前端可直接展示：
   - 模式摘要
   - 事件级时间线
   - 指标时间序列
   - Leader / 故障 / 恢复 / 稳定状态切换

## 3. 已冻结需求

### 3.1 模式与默认值

- `operationMode`：
  - `cooperative`
- `communicationMode`：
  - `centralized`
  - `distributed`
  - `hybrid`
- 默认 `communicationMode`：
  - `centralized`
- 默认 Leader：
  - 用户可显式指定 `leaderNodeId`
  - 未指定时默认 `0` 号节点

### 3.2 拓扑可见性

- `centralized`
  - Leader 拥有全局拓扑
  - Follower 无全局拓扑，仅保留最小必要本地信息
- `distributed`
  - 无任何节点拥有全局拓扑
  - 每个节点仅拥有 `k-hop` 局部视图
  - 默认 `k=1`
  - 允许扩展到 `2-hop`
- `hybrid`
  - Leader 拥有全局拓扑
  - Follower 拥有 `1-hop` 局部视图和 Leader 下发命令

### 3.3 hybrid 控制边界

- Leader 负责全局、低频、不可被下层覆盖的事项：
  - 任务分配
  - 编队结构
  - 全局路径规划
  - 频谱 / 时隙 / 信道等资源策略
  - 故障恢复策略触发
- Follower 负责局部、高频、受限自治事项：
  - 局部避障
  - 编队保持微调
  - 轨迹平滑
  - 功率调整
  - 速率调整
  - 邻居选择 / 中继
  - 局部链路恢复
- Follower 不允许：
  - 修改任务目标
  - 修改编队形状
  - 重新分配任务

### 3.4 故障模型

- 默认故障类型：
  - `node_failure`
- 默认故障语义：
  - 完全掉线
  - 停止通信
  - 不再参与协作控制
  - 从有效协作节点集合中移除
- 必须支持显式指定被破坏 UAV：
  - `--failureTargetId=<uav_id>`
- 允许显式指定 Leader 作为故障目标
- 默认故障开始时间：
  - 仿真总时长的 `40%`
- 默认故障持续时间：
  - 仿真总时长的 `20%`
- 必须保留人工覆盖接口：
  - `--failureStartTime=<seconds>`
  - `--failureDuration=<seconds>`

#### 3.4.1 其他故障类型的冻结语义

- `environment_degradation`
  - 语义：
    - 目标节点仍在线
    - 不移出协作节点集合
    - 仅其关联链路的传播环境在故障窗口内恶化
  - 默认作用范围：
    - `failureTargetId` 的全部一跳关联链路
  - 默认生效量：
    - 关联链路附加传播损耗 `+8 dB`
    - 有效连通范围乘子 `0.80`
    - 估计链路质量乘子 `0.85`
  - 默认实现口径：
    - 不改 `txPower`
    - 不改 `rxSensitivity`
    - 不改节点是否存活
    - 仅在故障窗口内对目标相关链路施加环境退化

- `external_interference`
  - 语义：
    - 目标节点仍在线
    - 外部同频干扰增强，主要影响目标节点及其一跳邻居的接收侧 SINR
  - 默认作用范围：
    - `failureTargetId` 节点及其一跳邻居
  - 默认生效量：
    - 接收侧等效附加干扰 `+6 dB`
    - 冲突敏感度乘子 `1.25`
    - 若存在黑飞干扰节点，则目标附近额外激活 `2` 个短时干扰源
  - 默认实现口径：
    - 不使节点掉线
    - 不改变几何拓扑
    - 仅降低有效 SINR、PDR 与可用速率

- `link_degradation`
  - 语义：
    - 目标节点仍在线
    - 目标节点与其当前主邻居集合之间的链路退化，但不彻底断开
  - 默认作用范围：
    - `failureTargetId` 到当前一跳邻居中链路质量最高的前 `2` 条链路
    - 若当前度数不足 `2`，则作用于全部一跳链路
  - 默认生效量：
    - 选中链路附加传播损耗 `+10 dB`
    - 链路可用数据率上限乘子 `0.5`
    - 链路质量乘子 `0.70`
  - 默认实现口径：
    - 节点不掉线
    - 仅对选中链路生效
    - 故障窗口结束后自动恢复默认传播口径

### 3.5 恢复策略

- 默认 `recoveryPolicy`：
  - `global_recovery`
- 默认 `recoveryObjective`：
  - `connectivity`
- 故障窗口内允许多次恢复
- 默认 `recoveryCooldown`：
  - `1.0s`
- 默认恢复动作全部开启：
  - 信道重分配
  - 发射功率调整
  - 数据速率调整
  - 邻居选择 / 中继切换
  - TDMA 时隙重分配
  - 路由重构

### 3.6 distributed 约束

- 节点可以向一跳邻居发起协作调整请求
- 不能单方面直接修改邻居资源
- 邻居资源调整必须由邻居自行确认与执行

### 3.7 恢复完成判定

- 恢复完成采用联合判定
- 结构条件：
  - 使用全局 `connectivity`
  - 必须达到可接受水平
- 业务条件：
  - 优先使用故障目标邻域的 `PDR`
  - 优先使用故障目标邻域的 `throughput`
  - 优先使用故障目标邻域的 `delay`
  - 若当前不存在有效故障邻域，则回退到全局平均业务指标
- 判定规则：
  - 全局 `connectivity` 达标
  - 且业务三项中至少两项达标
- 当前实现按“相对故障前基线 + 场景自适应”计算阈值：
  - `connectivity`
    - 默认下限 `0.80`
    - 若存在有效基线：
      - 非水面场景：`max(0.80, baselineConnectivity * 0.90)`
      - 水面场景：`max(0.85, baselineConnectivity * 0.95)`
  - `PDR`
    - 优先使用故障邻域基线 `baselineLocalPdr`
    - 否则回退到全局基线 `baselinePdr`
    - 阈值：
      - 非水面场景：`max(0.18, min(targetPDR, baselinePdr * 0.90))`
      - 水面场景：`max(0.20, min(targetPDR, baselinePdr * 0.90))`
  - `throughput`
    - 优先使用故障邻域基线 `baselineLocalThroughputMbps`
    - 否则回退到全局基线 `baselineThroughputMbps`
    - 阈值：
      - 非水面场景：`max(0.20 Mbps, baselineThroughputMbps * 0.80)`
      - 水面场景：`max(0.10 Mbps, baselineThroughputMbps * 0.80)`
  - `delay`
    - 优先使用故障邻域基线 `baselineLocalDelayMs`
    - 否则回退到全局基线 `baselineDelayMs`
    - 阈值：
      - 非水面场景：`max(100 ms, baselineDelayMs * 1.25)`
      - 水面场景：`max(140 ms, baselineDelayMs * 1.30)`
- 达到可接受阈值即视为恢复完成
- 后续继续逼近故障前水平仅计为优化，不影响恢复完成判定

#### 3.7.1 稳定态判定补充

- `stabilization_time` 默认稳定窗口：
  - `3s`
- 前提条件：
  - 已存在 `recoveryCompletedAt`
  - 稳定窗口起点不得早于恢复完成时刻
  - 稳定窗口内不得再次出现新的恢复动作
- 在稳定窗口内同时满足：
  - 非水面场景：
    - `connectivity` 波动不超过 `0.06`
    - `PDR` 波动不超过 `0.10`
    - `delay` 波动不超过 `30 ms`
    - 若存在吞吐基线：`throughput` 必须位于基线的 `[0.75, 1.25]` 区间内
  - 水面场景：
    - `connectivity` 波动不超过 `0.08`
    - `PDR` 波动不超过 `0.15`
    - `delay` 波动不超过 `60 ms`
    - 若存在吞吐基线：`throughput` 必须位于基线的 `[0.70, 1.35]` 区间内
- 若在稳定窗口内再次触发新的恢复动作，则稳定态判定重置

### 3.8 Leader 失效处理

- `hybrid` 下 Leader 失效处理流程：
  1. 故障检测
  2. 进入过渡态，由 Follower 局部自治接管
  3. 触发备份 Leader 切换
- 过渡态中 Follower 继续执行：
  - 局部避障
  - 编队保持微调
  - 轨迹平滑
  - 功率 / 速率调整
  - 邻居重连与中继修复
- `centralized` 下 Leader 失效：
  - 也支持备份 Leader 切换
  - 切换后继续保持中心化控制口径
- 备份 Leader 选举规则：
  - 优先用户显式指定备份候选列表
  - 否则在当前有效节点中选择连通性最好的节点
  - 若并列，再按以下顺序打破平局：
    - 度数更高
    - 平均链路质量更好
    - 节点 ID 更小
- `remainingEnergy` 不纳入当前版本硬依赖条件

#### 3.8.1 Leader 切换默认时序

- 默认故障检测确认时间：
  - `0.5s`
- 默认过渡态最短持续时间：
  - `1.0s`
- 默认备份 Leader 切换触发：
  - 在故障确认后立即进入候选评估
  - 在 `1.0s` 过渡窗口结束时完成切换

### 3.9 实验口径

- 正式必跑场景：
  - `urban`
  - `forest`
  - `lake`
  - `open-field`
- 四个场景全部要求跑三种合作模式对比：
  - `centralized`
  - `distributed`
  - `hybrid`
- 默认难度：
  - `Moderate`
- 默认编队：
  - `v_formation`
- 本次只正式收口 `cooperative`
  - `non_cooperative` 不纳入本次硬验收矩阵

## 4. 代码改动范围

### 4.1 配置与上下文

文件：

- `scratch/uav_ra/context.h`
- `scratch/uav_ra/main.cc`
- `scratch/uav_ra/simulation_setup.cc`

必须完成：

1. 新增合作主线所需枚举、配置结构、运行时状态结构。
2. 新增命令行参数解析。
3. 将合作配置写入 `g_environmentConfig` 与 `g_environmentSummary`。
4. 在环境摘要中补充合作主线字段。

建议新增结构：

- `enum class CommunicationMode`
- `enum class CooperativeFailureType`
- `enum class RecoveryPolicy`
- `enum class RecoveryObjective`
- `struct CooperativeControlConfig`
- `struct CooperativeFailureEvent`
- `struct CooperativeRecoveryAction`
- `struct CooperativeRecoveryMetrics`
- `struct CooperativeRuntimeState`

### 4.2 控制逻辑

文件：

- `scratch/uav_ra/topology_control.cc`

必须完成：

1. 将 `PerformResourceReallocation()` 重构为合作模式控制入口。
2. 显式拆出以下流程：
   - 网络状态收集
   - 故障检测 / 注入生效
   - 恢复触发判断
   - 模式化决策
   - 资源动作下发
   - 决策日志记录
3. 根据 `communicationMode` 分流：
   - `centralized`
   - `distributed`
   - `hybrid`
4. 根据 `recoveryPolicy` 分流：
   - `global_recovery`
   - `local_recovery`

建议拆分函数：

- `CollectCooperativeNetworkState()`
- `ApplyCooperativeFailureEvents()`
- `ShouldTriggerRecovery()`
- `ExecuteCentralizedRecovery()`
- `ExecuteDistributedRecovery()`
- `ExecuteHybridRecovery()`
- `ExecuteGlobalRecoveryPolicy()`
- `ExecuteLocalRecoveryPolicy()`
- `RecordCooperativeDecisionTrace()`
- `RecordCooperativeRecoveryMetrics()`

#### 4.2.1 模式与策略动作权限矩阵

| 动作 | centralized | distributed | hybrid | global_recovery | local_recovery |
| --- | --- | --- | --- | --- | --- |
| 信道重分配 | Leader 全局统一下发 | 不允许全局统一下发，仅允许节点本地候选切换 | Leader 下发全局候选池，Follower 在候选池内执行 | 允许 | 不允许跨全网统一改动 |
| 发射功率调整 | Leader 可直接统一调整 | 节点本地自主调整 | Follower 本地自主调整 | 允许 | 允许 |
| 数据速率调整 | Leader 可直接统一调整 | 节点本地自主调整 | Follower 本地自主调整 | 允许 | 允许 |
| 邻居选择 / 中继切换 | Leader 可重构全局中继关系 | 节点仅能向一跳邻居发起协作请求并由邻居确认 | Leader 给出方向，Follower 完成局部切换 | 允许 | 允许 |
| TDMA 时隙重分配 | Leader 全局重排 | 不允许全局重排，仅允许节点申请局部补偿时隙 | Leader 可低频调整，Follower 不可独立全局改时隙 | 允许 | 仅允许局部补偿，不允许全局重排 |
| 路由重构 | Leader 可全局重构 | 节点仅做本地下一跳改选 | Leader 触发，Follower 执行局部重连 | 允许 | 仅允许局部路径修复 |

冻结规则：

- `centralized + global_recovery`
  - 默认允许全部恢复动作
- `centralized + local_recovery`
  - 允许 Leader 仅在故障节点邻域内下发局部动作
  - 不做全局 TDMA 重排
- `distributed + global_recovery`
  - 视为非法组合
  - 实现时自动降级为 `distributed + local_recovery`
  - 必须记录降级原因
- `distributed + local_recovery`
  - 只允许功率、速率、邻居/中继、本地下一跳修复
  - 不允许全局信道/时隙/任务层重构
- `hybrid + global_recovery`
  - Leader 负责低频全局资源动作
  - Follower 负责高频局部链路修复
- `hybrid + local_recovery`
  - Leader 只做触发与边界约束
  - Follower 完成邻域内恢复

### 4.3 指标与输出

文件：

- `scratch/uav_ra/traffic_metrics.cc`
- `scratch/uav_ra/output_runtime.cc`

必须完成：

1. 增加合作故障事件日志输出。
2. 增加合作恢复动作日志输出。
3. 增加恢复前、恢复中、恢复后指标聚合。
4. 增加 `response_time`、`recovery_time`、`stabilization_time` 统计。
5. 增加前端主用的结构化 JSON 输出。

## 5. 新增命令行参数

必须支持：

- `--operationMode=cooperative`
- `--communicationMode=centralized|distributed|hybrid`
- `--leaderNodeId=<node_id>`
- `--backupLeaderList=<id1,id2,...>`
- `--distributedHopLimit=1|2`
- `--cooperativeFailureType=node_failure|environment_degradation|external_interference|link_degradation`
- `--failureTargetId=<uav_id>`
- `--failureStartTime=<seconds>`
- `--failureDuration=<seconds>`
- `--recoveryPolicy=global_recovery|local_recovery`
- `--recoveryObjective=connectivity|delay|throughput|pdr`
- `--recoveryCooldown=<seconds>`

建议保留动作开关：

- `--allowChannelReallocation=true|false`
- `--allowPowerAdjustment=true|false`
- `--allowRateAdjustment=true|false`
- `--allowRelayReselection=true|false`
- `--allowSlotReallocation=true|false`
- `--allowRouteRebuild=true|false`

## 6. 输出契约

### 6.1 前端主用 JSON

必须输出：

- `cooperative_mode_summary.json`
- `cooperative_failure_timeline.json`
- `cooperative_recovery_timeline.json`
- `cooperative_metrics_timeseries.json`
- `cooperative_dashboard_snapshot.json`

#### 6.1.1 JSON 最小字段表

`cooperative_mode_summary.json`

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
  - `allowChannelReallocation`
  - `allowPowerAdjustment`
  - `allowRateAdjustment`
  - `allowRelayReselection`
  - `allowSlotReallocation`
  - `allowRouteRebuild`

`cooperative_failure_timeline.json`

- `events`
  - `eventId`
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

`cooperative_recovery_timeline.json`

- `actions`
  - `actionId`
  - `time`
  - `phase`
  - `communicationMode`
  - `recoveryPolicy`
  - `triggerReason`
  - `executorNodeId`
  - `targetNodeIds`
  - `actionType`
  - `oldValue`
  - `newValue`
  - `scope`
  - `expectedEffect`
  - `resultState`

`cooperative_metrics_timeseries.json`

- `samples`
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
- 语义补充：
  - `activeNodeCount`、`leaderNodeId`、`isLeaderAlive` 为逐样本历史值
  - 不允许用最终运行时状态回填历史样本
  - `failureNeighborhood*` 表示故障目标邻域业务指标
  - `failureTarget*` 表示故障目标节点业务指标
  - 前端展示故障冲击时，应优先使用 `failureNeighborhood*` 或 `failureTarget*`
  - `responseTimeSec`、`recoveryTimeSec`、`stabilizationTimeSec` 允许为 `null`
  - `null` 表示该时间点尚未形成对应结果，不得等价显示为 `0`

`cooperative_dashboard_snapshot.json`

- `time`
- `phase`
- `operationMode`
- `communicationMode`
- `leaderNodeId`
- `backupLeaderId`
- `isLeaderAlive`
- `failureActive`
- `failureType`
- `failureTargetId`
- `connectivity`
- `avgDegree`
- `pdr`
- `throughputMbps`
- `delayMs`
- `p99DelayMs`
- `responseTimeSec`
- `recoveryTimeSec`
- `stabilizationTimeSec`
- `latestRecoveryAction`
- `recoveryStatus`
- 语义补充：
  - `latestRecoveryAction` 为当前已记录的最后一条恢复动作类型
  - `recoveryStatus` 当前实现值域：
    - `not_triggered`
    - `active`
    - `completed`
    - `stable`
  - `recoveryTimeSec = null` 表示当前运行结束时仍未满足恢复完成条件

### 6.2 明细 CSV

必须输出：

- `cooperative_failure_events.csv`
- `cooperative_recovery_actions.csv`
- `cooperative_recovery_metrics.csv`
- `cooperative_decision_trace.csv`

并保留基础输出：

- `environment_summary.json`
- `resource_allocation.csv`
- `resource_allocation_detailed.csv`
- `qos_performance.csv`
- `topology_changes.csv`
- `topology_evolution.csv`
- `topology_detailed.csv`
- `tdma_schedule.csv`

### 6.3 前端展示粒度

前端至少要能展示：

- 模式摘要
- 当前 Leader / 备份 Leader
- 故障事件时间线
- 恢复动作时间线
- 指标时间序列
- 阶段状态切换：
  - 正常期
  - 故障期
  - 恢复期
  - 稳定期

### 6.4 输出目录命名

输出目录必须带关键实验维度：

- `operationMode`
- `communicationMode`
- `failureType`
- `recoveryPolicy`
- `sceneType`
- `difficulty`
- `formation`

建议命名格式：

```text
output/cooperative_<communicationMode>_<failureType>_<recoveryPolicy>_<sceneType>_<difficulty>_<formation>/
```

## 7. 指标要求

必须全量输出：

- `connectivity`
- `avg_degree`
- `PDR`
- `throughput`
- `delay`
- `P99 delay`
- `response_time`
- `recovery_time`
- `stabilization_time`

汇报按三组主指标组织：

- 结构类：
  - `connectivity`
- 业务类：
  - `PDR`
  - `throughput`
  - `delay`
- 恢复类：
  - `response_time`
  - `recovery_time`
  - `stabilization_time`

### 7.1 时间指标定义

- `response_time`
  - 从故障发生到首次触发恢复动作的时间
- `recovery_time`
  - 从故障发生到满足恢复完成联合判定条件的时间
- `stabilization_time`
  - 从满足恢复完成条件开始到进入稳定窗口的时间
- 默认稳定窗口：
  - `3s`

#### 7.2 与现有通信参数的对齐原则

本任务单中的默认阈值与当前仿真口径保持一致：

- 目标投递率：
  - `targetPDR = 0.85`
- 最大端到端时延：
  - `maxEndToEndDelay = 100 ms`
- 最小有效吞吐：
  - `minThroughput = 0.50 Mbps`
- 默认发射功率：
  - `txPower = 23 dBm`
- 默认接收灵敏度（Moderate）：
  - `rxSensitivity = -85 dBm`
- 默认重分配周期：
  - `reallocationInterval = 5.0s`
- 默认 TDMA 时隙：
  - `slotDuration = 10 ms`
- 默认 TDMA 守护间隔：
  - `guardTime = 1 ms`

## 8. 验收矩阵

必须完成以下正式矩阵：

1. 四个场景全部覆盖：
   - `urban`
   - `forest`
   - `lake`
   - `open-field`
2. 每个场景全部覆盖三种模式：
   - `centralized`
   - `distributed`
   - `hybrid`
3. 默认故障为：
   - `node_failure`
4. 默认难度为：
   - `Moderate`
5. 默认编队为：
   - `v_formation`

每组至少验证：

- 程序正常结束
- JSON 与 CSV 文件齐全
- Leader / 故障 / 恢复状态切换完整
- 恢复动作非空或明确记录未触发原因
- 指标时间序列可读
- 三种模式结果存在可解释差异

## 9. 风险点

重点防止以下问题：

1. 三种模式只有名称不同，没有真实行为差异。
2. `hybrid` 没有体现 Leader 与 Follower 的权限边界。
3. Leader 失效后没有过渡态和切换链路。
4. 故障事件存在，但没有对应恢复动作和恢复指标。
5. 只输出原始 CSV，没有前端可直接消费的 JSON。
6. 输出文件存在，但缺少模式、故障、Leader 等关键字段。

## 10. 本次迭代总结

### 10.1 已完成范围

本轮迭代已经完成合作场景主线的后端收口，实现内容包括：

1. 合作模式配置层完成接入：
   - `communicationMode`
   - `leaderNodeId`
   - `backupLeaderList`
   - `distributedHopLimit`
   - `cooperativeFailureType`
   - `recoveryPolicy`
   - `recoveryObjective`
   - 恢复动作开关
2. 合作模式专属输出骨架完成：
   - `cooperative_failure_events.csv`
   - `cooperative_recovery_actions.csv`
   - `cooperative_recovery_metrics.csv`
   - `cooperative_decision_trace.csv`
   - `cooperative_mode_summary.json`
   - `cooperative_failure_timeline.json`
   - `cooperative_recovery_timeline.json`
   - `cooperative_metrics_timeseries.json`
   - `cooperative_dashboard_snapshot.json`
3. 四类合作故障完成接入：
   - `node_failure`
   - `environment_degradation`
   - `external_interference`
   - `link_degradation`
4. 合作控制入口完成重构：
   - 网络状态收集
   - 恢复触发判断
   - 决策日志记录
   - 恢复指标记录
5. 三种通信模式完成真实行为分流：
   - `centralized`
   - `distributed`
   - `hybrid`
6. 两种恢复策略完成真实分流：
   - `global_recovery`
   - `local_recovery`
   - `distributed + global_recovery` 自动降级为 `local_recovery`
7. 时间指标完成接入：
   - `response_time`
   - `recovery_time`
   - `stabilization_time`
8. Leader 失效与备份 Leader 切换完成接入：
   - 故障检测
   - 过渡态局部自治
   - 备份 Leader 切换
   - 切换完成后恢复中心化或 hybrid 口径
9. 编队口径完成补齐：
   - `formation` 已从命令行进入环境摘要和合作模式摘要
   - `v_formation` 不再被写成泛化的 `trajectory`

### 10.2 已完成验收

已完成下列 cooperative 验收：

1. 四场景 x 三模式矩阵输出验证：
   - `urban`
   - `forest`
   - `lake`
   - `open-field`
   - 每个场景覆盖：
     - `centralized`
     - `distributed`
     - `hybrid`
2. `Leader` 失效专项验证：
   - `centralized` 下 Leader 故障后成功切换备份 Leader
   - `hybrid` 下 Leader 故障后先进入局部自治，再切换备份 Leader
3. `v_formation` 专项验证：
   - cooperative 模式下成功加载 `mobility_trace_v_formation.txt`
   - 成功以 15 机编队轨迹运行
   - `environment_summary.json` 与 `cooperative_mode_summary.json` 中 `formation` 字段正确写为 `v_formation`

### 10.3 代表性验证结果

代表性 cooperative `v_formation` 用例：

- `operationMode=cooperative`
- `communicationMode=centralized`
- `sceneType=open-field`
- `difficulty=Moderate`
- `formation=v_formation`
- `cooperativeFailureType=node_failure`
- `failureTargetId=1`
- `recoveryPolicy=global_recovery`
- `recoveryObjective=connectivity`

输出目录：

- `output/cooperative_vformation_smoke_rerun`

关键结果：

- 平均分组投递率：
  - `68.4374%`
- 平均端到端时延：
  - `8.86929 ms`
- 总吞吐量：
  - `2.99329 Mbps`

### 10.4 本轮新增可视化结果

已基于 cooperative `v_formation` 用例生成结果图：

- `output/cooperative_vformation_smoke_rerun/visualizations/cooperative_vformation_metrics.png`
- `output/cooperative_vformation_smoke_rerun/visualizations/cooperative_vformation_timeline.png`
- `output/cooperative_vformation_smoke_rerun/visualizations/cooperative_vformation_snapshot.png`

对应内容分别为：

1. 恢复指标时间序列图：
   - `connectivity`
   - `PDR`
   - `throughput`
   - `delay`
2. 故障 / 决策 / 恢复动作时间线图
3. cooperative 最终快照摘要图

### 10.5 当前状态判断

截至本次迭代结束，合作场景主线已经具备：

1. 可配置的合作模式和恢复策略
2. 可注入的故障事件
3. 可记录的恢复动作与时间指标
4. 可直接供前端接入的 JSON / CSV 输出
5. Leader 失效后的过渡与切换能力
6. `v_formation` 编队口径下的正式运行能力

从后端角度看，合作场景主线已经完成主体收口，可以进入：

- 前端接入
- 扩展实验矩阵
- 更细粒度恢复动作深化
