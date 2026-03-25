# 非合作打击闭环开发定稿

## 1. 文档目的

这份文档单独用于定义非合作模式下“推断结果之后”的后端闭环，不混入观测层文档，也不混入总体两周计划。

当前文档要解决的问题是：

- 如何基于非合作推断结果形成打击候选
- 如何确认并执行对目标节点的打击
- 如何在打击前后评估敌方集群通信受损与恢复能力
- 如何把这套结果稳定输出为后端可验证、后续可接前端的契约

本文档的目标不是讨论前端故事线，而是先把后端可编码部分完全定稿。

## 2. 当前项目基础与本部分位置

结合当前项目已有完成情况，非合作主线已经具备或正在具备以下基础：

- 场景环境层已完成独立建模与输出约定
- `cooperative / non_cooperative` 模式边界已明确
- 非合作观测层已定稿
- 事件层、窗口层、边证据层、推理结果层的数据契约已基本收敛
- `observedNodeId`、`observerId`、缺失机制、噪声机制、时间窗口语义已经明确

因此，当前最适合补的主线就是：

- 非合作观测
- 非合作推断
- 打击目标推荐
- 用户主动执行打击
- 打击后网络受损与恢复评估

也就是说，本部分的价值在于把系统从“观测/推断展示”推进到“非合作对抗闭环”。

## 3. 本部分的总体目标

当前版本要实现的不是任意形式的打击，而是下面这条明确链路：

1. 系统基于非合作观测和推断结果，持续给出当前推荐打击目标
2. 用户决定是否采用当前推荐结果，或手工指定打击目标
3. 一旦用户主动下达打击命令，系统对目标节点实施 `node_strike`
4. 被打节点永久失效
5. 其余敌方网络允许继续恢复、重连、重路由
6. 系统输出打击前、打击后即时、恢复过程、观察窗口结束时的全套评估结果

当前版本关心的不是“能不能打掉一个节点”，而是：

- 打击哪个敌方节点
- 能否最大化降低敌方集群整体通信效率
- 打击后敌方网络是否还能恢复
- 哪类目标最值得打

## 4. 非合作打击闭环边界

### 4.1 当前版本只做一种打击动作

当前版本打击类型唯一固定为：

- `node_strike`

当前版本不做：

- `link_suppression`
- `jamming`
- `area_denial`

这样做的原因：

- `node_strike` 最容易形成完整闭环
- 与关键节点识别天然对齐
- 执行效果与评估口径最直接
- 不额外引入链路压制和干扰物理层逻辑

### 4.2 当前版本只做单轮打击

当前版本统一采用：

- 单轮打击

当前版本不做：

- 多轮打击
- 连续打击计划
- 攻击战役级调度

### 4.3 当前版本不默认自动开火

当前版本中，系统只负责：

- 实时推荐目标

真正执行打击时：

- 必须由用户主动发起

也就是说：

- 系统每个时间窗口都可以输出一个当前推荐目标
- 系统不会自行在某个时间点自动执行打击
- 执行动作必须由用户显式触发

这个口径更符合当前项目的实验与答辩表达：

- 系统是“非合作打击决策支持”
- 而不是完全黑箱自动攻击

## 5. 推荐与执行分层定稿

### 5.1 推荐层

当前版本要求：

- 每个默认时间窗口都更新一次推荐目标
- 推荐目标基于当前时刻附近的最新观测与最新推断结果滚动更新
- 任意时段都可以形成一个“当前建议打击目标”

当前版本不强制要求：

- 候选目标必须先“稳定若干窗口后才允许推荐”

推荐模块只需要保证：

- 任一时段都有当前候选
- 推荐结果带分数或排序信息

### 5.2 默认 baseline 与高级备选

当前版本虽然不把推荐算法最终冻结为唯一研究结论，但为了确保后端可以直接编码，必须先固定一个默认 baseline。

当前版本默认 baseline 定为：

- 基于多指标融合的轻量关键节点评分器

组成指标为：

- 加权度中心性
- 加权介数中心性
- 加权接近中心性
- 加权 PageRank
- 加权 K-shell

当前版本要求：

- 在推理得到的图结构或有权邻接关系上计算上述指标
- 对各指标进行归一化后形成综合打分
- 以综合分数最高的目标作为当前推荐候选

这套 baseline 的理论依据主要来自：

- [基于图表示的通信网拓扑推理与关键节点识别_李鹏雪.md](/home/tzx/ns-3.43/project_docs/基于图表示的通信网拓扑推理与关键节点识别_李鹏雪.md)

当前版本同时保留一个高级备选：

- 图节点收缩 + CNN/GCN 关键节点识别方法

该高级备选的定位是：

- 后续增强版
- 对照实验版
- 不作为当前默认推荐实现

### 5.3 目标选择策略的研究空间

在冻结 baseline 之后，当前版本仍保留研究空间：

- 默认 baseline 只是当前可执行口径
- 不等于项目最终创新算法
- 后续可以在同一输出契约上替换为更强的推荐模型

因此当前版本同时要求：

- 后端必须提供推荐接口
- 后端必须输出候选目标及其分数/排序
- 文档层冻结 baseline
- 研究层仍允许后续替换推荐算法

### 5.4 用户主动执行

当前版本支持：

- 智能推荐
- 用户主动执行

这里的“主动执行”不是系统自动确认，而是：

- 系统先给出当前推荐目标
- 用户决定是否采用该推荐
- 或用户自行指定打击目标

当前版本也允许手工指定目标，主要用于：

- 调试
- 对照实验
- 验证执行与评估链路

但主流程仍然是：

- 智能推荐
- 用户决策
- 执行打击

### 5.5 当前版本的后端执行入口

由于前端暂不接入，当前版本把执行入口冻结为命令行参数：

- `--manualStrikeTarget=<observedNodeId>`
- `--attackExecuteTime=<seconds>`

语义如下：

- 如果不提供这两个执行参数：
  - 系统只输出推荐结果
  - 不执行打击
- 如果提供执行参数：
  - 系统在 `attackExecuteTime` 时刻
  - 对 `manualStrikeTarget` 指向的当前稳定观测目标执行 `node_strike`

## 6. 目标对象与 ID 语义

### 6.1 输入侧目标 ID

当前版本目标选择输入采用：

- 推断节点 ID

也就是：

- `recommendedObservedNodeId`
- `confirmedObservedNodeId`

### 6.2 执行侧目标绑定语义

当前版本不单独设计一个“显式的 observedNodeId -> realNodeId 身份映射模块”。

当前版本采用的语义是：

- `observedNodeId` 代表当前稳定跟踪到的目标实体标识
- 用户对该 `observedNodeId` 发起打击命令
- 后端对该 track 当前绑定的目标实体执行打击

这样做的目的：

- 不把执行前的身份识别包装成显式真值映射
- 保持非合作语义干净
- 同时让仿真内部仍能对具体目标实体施加真实效果

因此当前版本只要求：

- 用户输入的是 `observedNodeId`
- 系统执行的是该稳定 track 所对应的目标实体
- 真值只在事后评估时使用

### 6.3 允许误选

当前版本明确允许：

- 基于推断结果发生误选

也就是说：

- 系统可以推荐错误目标
- 后端不偷偷替系统纠正到“真实正确目标”
- 误打或误选本身就是非合作系统真实性的一部分

因此后端必须输出：

- 是否命中真实目标
- 目标绑定是否有效
- 误选类型

## 7. `node_strike` 的执行语义

当前版本中，`node_strike` 的执行语义固定为：

- 一旦用户主动执行
- 目标真实节点立即永久失效

永久失效的含义是：

- 节点不再发射
- 节点不再接收转发
- 节点不再参与网络拓扑
- 节点自身不恢复

当前版本不做：

- 临时压制
- 定时恢复
- 节点重生

## 8. 恢复与评估口径

### 8.1 敌方网络是否允许恢复

当前版本采用：

- 被打节点永久失效
- 其余敌方网络继续运行当前已有的拓扑更新与业务流逻辑

这意味着：

- 评估不能只看打击后瞬间
- 还要看后续恢复过程和最终状态

同时进一步冻结为：

- 不额外新增一套“敌方智能恢复控制器”
- 恢复行为来自当前系统已存在的动态拓扑与业务流机制
- 也就是说，只做“现有机制下的自然恢复与重连”

### 8.2 评估关注的时间阶段

当前版本评估同时覆盖以下四个阶段：

1. `pre_attack`
- 攻击前 `4s` 基线阶段

2. `immediate_post_attack`
- 攻击后 `2s` 即时冲击阶段

3. `recovery`
- 从即时阶段结束后，到 `attackEvaluationDuration` 结束前的恢复阶段

4. `final`
- 评估窗口最后一个采样点

### 8.3 评估范围

当前版本效果评估范围同时覆盖：

- 全网
- 目标邻域

全网用于回答：

- 这次打击对敌方集群整体通信效率影响有多大

目标邻域用于回答：

- 这次打击是否确实对目标局部通信枢纽或关键区域造成了集中破坏

当前版本对“目标邻域”的实现口径进一步冻结为：

- 一旦打击计划中的 `confirmedObservedNodeId` 已确定
- 后端在正式执行 `node_strike` 前先预冻结一份 `target_neighborhood`
- 后续 `pre_attack`、`immediate_post_attack`、`recovery`、`final` 四阶段都复用这份冻结邻域
- 不在攻击发生后按实时拓扑重新生成局部评估范围

这样做的原因：

- 避免攻击前没有局部基线，导致 `pre_attack_local_* = null`
- 保证打击前后“局部评估范围”一致，可直接做前后对比
- 避免被打节点失效后局部范围漂移，削弱局部打击评估意义

### 8.4 评估阶段是否允许使用真值

当前版本明确区分：

- 决策阶段：不能使用真实拓扑
- 评估阶段：允许后端使用真实拓扑与真实节点信息做离线判分

但真值不作为前端主视图直接暴露。

## 9. 五个同等重要的核心评估指标

当前版本必须同时评估并输出以下五个同等重要的核心指标：

1. 连通率下降
2. PDR 下降
3. 吞吐下降
4. 时延上升
5. 恢复时间 / 持续受损时间

这里不再人为区分“主指标”和“次指标”，而是统一认为：

- 这五项共同刻画打击有效性
- 缺少任何一项都会使评估不完整

## 10. 输出契约定稿

为了避免后端实现后再回补输出层，当前版本直接固定 6 个输出文件。

### 10.1 `noncooperative_attack_recommendations.csv`

作用：

- 记录每个时间窗口的智能推荐结果
- 即使未执行，也要对推荐决策留痕

建议列：

- `windowStart`
- `windowEnd`
- `recommendedObservedNodeId`
- `recommendedScore`
- `recommendationRank`
- `recommendationReason`
- `inferenceMethod`
- `sceneType`
- `operationMode`

### 10.2 `noncooperative_attack_plan.json`

作用：

- 保存本轮最终确认的打击计划快照

建议字段：

- `operationMode`
- `sceneType`
- `attackType`
- `recommendedObservedNodeId`
- `confirmedObservedNodeId`
- `userTriggeredExecution`
- `attackExecuteTime`
- `targetBindingStatus`
- `strikeExecuteTime`
- `strikeMode`
- `evaluationWindowStart`
- `evaluationWindowEnd`

### 10.3 `noncooperative_attack_events.csv`

作用：

- 记录真正执行的打击事件
- 一行对应一次真实打击执行

建议列：

- `eventTime`
- `attackType`
- `recommendedObservedNodeId`
- `confirmedObservedNodeId`
- `executedObservedNodeId`
- `targetBindingStatus`
- `isTrueTargetHit`
- `targetMismatchType`
- `nodeRemoved`
- `sceneType`
- `operationMode`

### 10.4 `noncooperative_target_binding.csv`

作用：

- 单独记录观测目标 track 与执行目标实体之间的内部绑定结果
- 用于误选分析和命中评估

建议列：

- `eventTime`
- `observedNodeId`
- `bindingStatus`
- `bindingConfidence`
- `isTrueCriticalTarget`
- `mismatchType`
- `sceneType`
- `operationMode`

### 10.5 `noncooperative_attack_effect_metrics.csv`

作用：

- 按时间记录打击前后全过程指标
- 是当前版本最核心的过程评估文件

建议列：

- `time`
- `phase`
- `targetScope`
- `connectivityRatio`
- `pdr`
- `throughputMbps`
- `delayMs`
- `damageDuration`
- `recoveryProgress`
- `recommendedObservedNodeId`
- `confirmedObservedNodeId`
- `executedObservedNodeId`
- `sceneType`
- `operationMode`

字段说明：

- `phase` 建议取值：
  - `pre_attack`
  - `immediate_post_attack`
  - `recovery`
  - `final`
- `targetScope` 建议取值：
  - `global`
  - `target_neighborhood`

补充语义：

- `target_neighborhood` 表示攻击计划确认后预冻结的目标邻域
- 该邻域在整轮评估中保持稳定，不随攻击后的实时拓扑变化
- 若某轮运行无法形成有效冻结邻域，则相应局部指标允许为 `null`

### 10.6 `noncooperative_pre_post_comparison.json`

作用：

- 给出一份便于报告、调试和后续前端接入的摘要文件

建议结构：

- `attackPlan`
- `targetBindingResult`
- `preAttackMetrics`
- `immediatePostAttackMetrics`
- `finalMetrics`
- `globalImpactSummary`
- `targetNeighborhoodImpactSummary`
- `recoverySummary`
- `overallAssessment`

## 11. 代码落点建议

结合当前工程结构，建议这部分主要落在以下位置。

### 11.1 参数入口与上下文

- [main.cc](/home/tzx/ns-3.43/scratch/uav_ra/main.cc)
  - 新增非合作打击相关参数入口
  - 如是否启用推荐、手工指定目标、执行时间等
- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
  - 定义打击计划结构
  - 定义推荐结果结构
  - 定义目标绑定结果结构
  - 定义效果评估记录结构

### 11.2 推荐、目标绑定与打击执行

- 建议新增 `non_cooperative_attack.cc`
  - 消费 `inferred_topology_edges.csv` 或内存中的推理结果
  - 生成推荐目标
  - 处理用户主动执行入口
  - 完成观测目标 track 到执行目标实体的内部绑定
  - 触发节点永久失效

如需要头文件，可新增：

- `non_cooperative_attack.h`

### 11.3 真实网络效果施加

- [topology_control.cc](/home/tzx/ns-3.43/scratch/uav_ra/topology_control.cc)
  - 接收已确认打击目标
  - 将对应真实节点从拓扑与通信参与集合中移除
  - 触发相关链路与路由的重新计算

### 11.4 指标记录与输出

- [traffic_metrics.cc](/home/tzx/ns-3.43/scratch/uav_ra/traffic_metrics.cc)
  - 在攻击前后持续记录五项核心指标
  - 支持全网与目标邻域双视角指标
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)
  - 初始化这 6 个输出文件
  - 写入表头和摘要结果

## 12. 推荐的后端参数项

当前版本建议至少引入以下参数：

- `--enableNonCooperativeAttack=true|false`
- `--attackType=node_strike`
- `--manualStrikeTarget=<observedNodeId>` 可选
- `--attackExecuteTime=<seconds>` 可选
- `--attackEvaluationDuration=<seconds>`
- `--attackNeighborhoodHop=<int>` 或邻域定义参数

说明：

- `attackType` 当前版本固定为 `node_strike`
- `manualStrikeTarget` 与 `attackExecuteTime` 同时提供时才执行打击
- 若不提供执行参数，则系统只输出推荐，不执行打击

## 13. 当前版本的实现重点

实现时不要把精力先花在复杂策略上，而要优先打通下面这些硬闭环：

1. 推理结果能形成推荐目标
2. 推荐目标能按 baseline 形成稳定排序
3. 用户能主动指定执行目标与执行时刻
4. 真实节点能被永久移除
5. 五项核心指标能在攻击前后持续记录
6. 能同时输出全网和目标邻域评估
7. 能输出命中/误选/目标绑定结果

## 14. 当前版本的最小验收标准

当前版本至少要满足：

1. 在 `non_cooperative` 模式下，系统能持续输出推荐目标
2. 用户主动下达打击命令后，系统能对一个目标实体执行永久失效
3. 被打节点移除后，其余网络能继续恢复或重路由
4. 五项核心指标能够在全过程中被记录
5. 能同时得到全网和目标邻域两组评估结果
6. 能输出命中/误选/目标绑定结果
7. 上述 6 个输出文件能完整生成

## 15. 当前版本的完整验收标准

如果要认为这部分“真正完成”，还应满足：

1. 四个场景都能运行这条打击闭环
   - `urban`
   - `forest`
   - `lake`
   - `open-field`
2. 推荐目标能够按窗口滚动更新
3. 手工指定执行入口可用
4. 打击前、即时打击后、恢复过程、最终状态四阶段指标都可输出
5. 能稳定生成摘要对比文件

## 16. 推荐开发顺序

虽然当前版本目标是完整闭环，但代码上仍建议按依赖顺序推进：

1. 先定义打击计划、推荐结果、目标绑定结果、评估结构
2. 再实现默认 baseline 推荐器
3. 再打通推荐结果输出
4. 再实现手工指定目标与执行时刻入口
5. 再实现观测目标 track 到执行目标实体的内部绑定
6. 再实现真实节点永久移除
7. 再实现打击前后全过程指标记录
8. 最后补齐 6 个输出文件与摘要

## 17. 本文档与其他文档的关系

这份文档依赖但不替代以下文档：

- [NON_COOPERATIVE_OBSERVATION_LAYER_PLAN.md](/home/tzx/ns-3.43/project_docs/NON_COOPERATIVE_OBSERVATION_LAYER_PLAN.md)
  - 定义观测层、窗口层、边证据层与推理结果层
- [UAV_RA_SCENARIO_ENVIRONMENT_NOTES.md](/home/tzx/ns-3.43/project_docs/UAV_RA_SCENARIO_ENVIRONMENT_NOTES.md)
  - 定义场景环境层及其传播/遮挡语义

这份文档只负责：

- 推理之后如何实施打击
- 如何评估打击效果
- 如何输出这部分结果
