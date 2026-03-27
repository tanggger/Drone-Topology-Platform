# 非合作拓扑推理与关键节点识别算法增强方案

## 1. 文档目的

这份文档单独用于回答两个问题：

1. 现有系统下一步最值得引入的算法原理是什么
2. 这些算法应当如何拆成可执行的开发任务

本文档不替代以下文档：

- [NON_COOPERATIVE_OBSERVATION_LAYER_PLAN.md](/home/tzx/ns-3.43/project_docs/NON_COOPERATIVE_OBSERVATION_LAYER_PLAN.md)
- [NON_COOPERATIVE_ATTACK_CLOSURE_PLAN.md](/home/tzx/ns-3.43/project_docs/NON_COOPERATIVE_ATTACK_CLOSURE_PLAN.md)

本文档专门关注：

- 非合作拓扑推理算法增强
- 关键节点识别算法增强
- 与现有后端闭环的结合方式

主要参考来源：

- [基于图表示的通信网拓扑推理与关键节点识别_李鹏雪.md](/home/tzx/ns-3.43/project_docs/基于图表示的通信网拓扑推理与关键节点识别_李鹏雪.md)
- [面向非合作无人机通信网络的通联拓扑推理技术.md](/home/tzx/ns-3.43/project_docs/面向非合作无人机通信网络的通联拓扑推理技术.md)

## 2. 当前系统的算法基线

当前系统已经具备完整的非合作主链：

- 事件级非合作观测
- 窗口化观测摘要
- 链路证据构造
- 概率边推理
- 图表示
- 关键节点候选
- 基于关键节点的攻击推荐与执行
- 攻击前后效果评估

当前实际推理 baseline 主要是：

1. 多观察者窗口证据融合
2. 基于重叠状态序列、活跃度、观察一致性的概率边估计
3. 基于图中心性的轻量关键节点融合评分

代码主落点：

- [non_cooperative_inference.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc)
- [non_cooperative_attack.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_attack.cc)

当前优点：

- 工程链路已完整
- 可以直接验证算法增强是否带来更好推理和更好打击目标选择

当前不足：

- 拓扑推理仍然偏“相关性/共现性”
- 对因果方向、时延响应、事件激发机制建模不足
- 关键节点识别虽已多特征融合，但还缺时间稳定性、边推理可信度和方向性信息

## 3. 建议引入的核心算法原理

## 3.1 动态窗口与分段稳定假设

原理：

- 无人机通信网络是动态变化的
- 但在短时间窗口内，拓扑通常可以视作“近似稳定”
- 因此推理不应把整段仿真压成一个静态图，而应按滑动窗口做局部推理

为什么适合当前系统：

- 当前系统本来就是窗口化观测
- 现有链路证据、推理边和攻击推荐也都按窗口输出
- 只需要把“窗口独立”进一步增强成“窗口连续”

对当前系统的直接启发：

- 每个窗口独立推理一批候选边
- 再通过上一窗口结果给当前窗口加时间连续性先验
- 避免边概率大幅无意义抖动

## 3.2 因果型边推理

第二篇论文最值得直接借鉴的核心，不是“换一个更复杂的分类器”，而是把边推理由纯共现提升为“带因果方向的事件关系推理”。

建议先引入三类轻量因果分数。

### 3.2.1 Granger-like 滞后预测得分

原理：

- 如果节点 A 的过去活动能显著提升对节点 B 当前活动的预测能力
- 那么 A 到 B 之间更可能存在有效信息关系

工程化简化实现：

- 不做完整统计检验版格兰杰
- 先在窗口内构造二值或计数序列
- 用固定滞后步长计算：
  - `P(B_t | A_{t-1})`
  - `P(B_t | not A_{t-1})`
- 二者差值或提升率作为 `laggedPredictiveScore`

适合当前系统的原因：

- 当前观测窗口已经有 `stateSequence`
- 现有实现中已有状态序列重叠度计算
- 可以在现有窗口证据层之上增加一列因果分数，而不用重写主链

### 3.2.2 Transfer-Entropy-like 非对称响应得分

原理：

- 相关性只能说明“一起发生”
- 不能说明“谁影响谁”
- 转移熵强调的是一个序列对另一个序列未来状态的不确定性减少

工程化简化实现：

- 不做完整信息论估计器
- 先实现一个轻量非对称响应分数：
  - A 活动后，B 在短延迟内转为活跃的概率
  - 与基线活跃概率比较
- 形成 `directedResponseScore(A->B)`

适合当前系统的原因：

- 对无人机集群中的控制报文、转发响应、链路回执类关系更敏感
- 能帮助区分“共同受第三方影响”和“真实链路响应”

### 3.2.3 Hawkes-like 事件激发强度得分

原理：

- 多维霍克斯过程强调：一个事件会提高短时间内另一个事件发生的条件强度
- 对事件型通信网络特别合适

工程化简化实现：

- 不引入完整霍克斯参数学习
- 在窗口内统计：
  - A 的一次事件后，B 在若干小时间桶内的响应次数
  - 近时延响应权重大于远时延响应
- 得到 `excitationScore(A->B)`

适合当前系统的原因：

- 当前非合作观测就是事件驱动
- 很适合增强对“触发链路”的识别

## 3.3 两阶段伪边抑制

原理：

- 第一阶段先做高召回候选边生成
- 第二阶段再用更严格条件压掉伪边

为什么重要：

- 无人机集群中存在大量共同机动、共同广播、共同场景扰动
- 纯相关方法很容易误把“共同出现”当成边

对当前系统的直接形式：

第一阶段：

- 保留当前 `window_evidence_fusion_v1` 候选边生成

第二阶段：

- 加入因果得分门限
- 加入方向性差异门限
- 加入窗口连续性门限
- 加入观察者一致性约束

最终输出：

- 候选边
- 过滤后边
- 被过滤原因

## 3.4 多特征关键节点融合评分

第一篇论文真正值得借的不是立即把 CNN/GNN 整套搬进工程，而是它强调的两个原则：

1. 关键节点识别不应依赖单一指标
2. 图结构特征应尽量保留边信息和权重信息

当前系统已经做了一个工程 baseline：

- weighted degree
- weighted betweenness
- weighted closeness
- weighted PageRank
- weighted k-shell

下一步应继续增强为：

- 结构特征
  - weighted degree
  - weighted betweenness
  - weighted closeness
  - weighted PageRank
  - weighted k-shell
- 证据特征
  - 平均边概率
  - 平均边置信度
  - 平均因果强度
- 时间特征
  - 窗口稳定度
  - 连续窗口中高分出现频率

建议形成统一评分：

`keyNodeScore = structureScore + evidenceScore + temporalStabilityScore`

## 3.5 节点收缩思想的工程化借鉴

第一篇里的“节点收缩 + CNN/GCN”不建议当前版本直接上模型训练，但它有一个值得借的工程思想：

- 关键节点不只由本节点属性决定
- 还与其所在局部子图的连通结构有关

工程化简化借法：

- 对候选关键节点，额外构造 1-hop 或 2-hop 局部子图摘要
- 提取：
  - 邻居数
  - 邻居间连边密度
  - 是否桥接不同簇
  - 节点去除后的局部连通下降幅度
- 将这些作为 `localSubgraphFeatures`

这一步不用上学习模型，也能显著增强关键节点评分解释性。

## 4. 不建议当前阶段直接照搬的内容

以下内容理论上有价值，但不建议当前第一轮就直接引入：

1. 全量“时序图像化 + CNN 边分类”
2. 全量“CNN/GCN 关键节点识别”
3. 完整霍克斯过程参数拟合
4. 完整格兰杰统计检验流程

原因：

- 当前系统已经有完整工程闭环
- 第一轮更应选择“可解释、可调试、可验证”的增强方式
- 先做轻量因果分数和多特征融合，性价比更高

## 5. 与当前代码的对应落点

推荐按下面方式落到现有代码。

### 5.1 拓扑推理增强

主文件：

- [non_cooperative_inference.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc)
- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

适合新增的字段：

- `laggedPredictiveScore`
- `directedResponseScore`
- `excitationScore`
- `temporalContinuityScore`
- `falseLinkSuppressionReason`

### 5.2 关键节点识别增强

主文件：

- [non_cooperative_attack.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_attack.cc)
- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

适合新增的字段：

- `evidenceSupportScore`
- `causalSupportScore`
- `temporalStabilityScore`
- `localBridgeScore`
- `postRemovalDamageScore`

## 6. 开发任务单

这里按“先增强推理，再增强关键节点，再接攻击推荐”的顺序拆任务。

## 6.1 任务 A：在当前边推理中新增因果分数

目标：

- 在现有 `window_evidence_fusion_v1` 之上，新增因果型边分数

当前状态：

- 已实现第一版轻量因果增强
- 当前输出字段已新增：
  - `laggedPredictiveScore`
  - `directedResponseScore`
  - `excitationScore`
- 当前推理方法标识已升级为 `window_evidence_causality_fusion_v2`

修改文件：

- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
- [non_cooperative_inference.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc)
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

需要做的事：

1. 给 `ObservedLinkEvidence` 和 `InferredTopologyEdge` 加字段：
   - `laggedPredictiveScore`
   - `directedResponseScore`
   - `excitationScore`

2. 基于窗口 `stateSequence` 或事件计数构造轻量因果得分

3. 调整边概率融合公式：
   - 旧：证据强度 + 观察一致性 + 置信度
   - 新：旧分数 + 因果分数

4. 输出 CSV 和 JSON

验收标准：

- `inferred_topology_edges.csv` 新增因果分数字段
- 因果分数不是全空，也不是全常数

## 6.2 任务 B：加入窗口连续性先验

目标：

- 让拓扑推理结果在相邻窗口之间更稳定

当前状态：

- 已实现第一版窗口连续性先验
- 当前输出字段已新增：
  - `temporalContinuityScore`
- 当前逻辑已具备：
  - 上一窗口同边连续奖励
  - 新窗口边概率/置信度平滑
  - 消失边的短暂缓退框架

修改文件：

- [non_cooperative_inference.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc)
- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)

需要做的事：

1. 保存上一窗口推理边集
2. 对当前窗口已存在的边给予连续性奖励
3. 对新出现但证据不足的边给予抑制
4. 对消失边设置缓退机制，而不是瞬时清零

验收标准：

- 连续窗口边数量不再无意义抖动
- 同一场景多次跑，关键节点结果更稳定

## 6.3 任务 C：加入两阶段伪边抑制

目标：

- 降低伪边数量，提高关键节点识别输入质量

当前状态：

- 已实现第一版两阶段伪边抑制
- 当前输出字段已新增：
  - `edgeStage`
  - `falseLinkSuppressionReason`
- 当前输出已可区分：
  - `candidate`
  - `final`
  - `filtered_out`

修改文件：

- [non_cooperative_inference.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc)
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

需要做的事：

1. 保留候选边阶段
2. 新增过滤阶段
3. 记录被过滤原因，例如：
   - `low_causality`
   - `low_continuity`
   - `weak_observer_agreement`
   - `low_edge_confidence`

验收标准：

- 输出中可以区分候选边和最终边
- 过滤原因可追溯

## 6.4 任务 D：增强关键节点融合评分

目标：

- 把当前“纯结构多特征融合”升级为“结构 + 证据 + 时间稳定性融合”

当前状态：

- 已实现第一版关键节点融合增强
- 当前推荐输出已新增：
  - `structureScore`
  - `evidenceSupportScore`
  - `causalSupportScore`
  - `temporalStabilityScore`
- 当前推荐方法标识已升级为 `multi_metric_graph_fusion_v2`

修改文件：

- [non_cooperative_attack.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_attack.cc)
- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

需要做的事：

1. 保留现有五个结构指标
2. 新增：
   - `evidenceSupportScore`
   - `causalSupportScore`
   - `temporalStabilityScore`
3. 调整 `recommendationReason` 和打分说明
4. 输出每个候选节点的分项得分

验收标准：

- `key_node_candidates.csv` 或推荐文件中能看到分项得分
- 推荐理由不再只是结构指标

## 6.5 任务 E：加入局部子图桥接特征

目标：

- 借鉴“节点收缩/局部子图”思想，增强关键节点解释性

当前状态：

- 已实现第一版局部子图桥接特征
- 当前推荐输出已新增：
  - `localBridgeScore`
  - `postRemovalDamageScore`
- 当前实现方式：
  - 基于候选节点 1-hop 局部子图密度计算桥接分
  - 基于移除此节点后的图连通下降计算损伤分

修改文件：

- [non_cooperative_attack.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_attack.cc)

需要做的事：

1. 对候选节点提取 1-hop 或 2-hop 局部子图
2. 计算：
   - 局部桥接得分
   - 去除此节点后的局部连通下降
3. 把这两个分数接入最终关键节点融合分

验收标准：

- 关键节点排序能够反映“桥接节点”而不仅是“高连接节点”

## 6.6 任务 F：把增强后的推理结果接入攻击推荐

目标：

- 让非合作攻击推荐真正受增强算法影响

当前状态：

- 已实现
- 当前 `attack_plan.json` 已新增：
  - `recommendedScore`
  - `recommendationReason`
  - `inferenceMethod`
  - `structureScore`
  - `evidenceSupportScore`
  - `causalSupportScore`
  - `temporalStabilityScore`
  - `localBridgeScore`
  - `postRemovalDamageScore`

修改文件：

- [non_cooperative_attack.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_attack.cc)
- [NON_COOPERATIVE_ATTACK_CLOSURE_PLAN.md](/home/tzx/ns-3.43/project_docs/NON_COOPERATIVE_ATTACK_CLOSURE_PLAN.md)

需要做的事：

1. 推荐时优先使用增强后的关键节点分
2. 在 `attack_plan.json` 中写出：
   - 结构分
   - 因果分
   - 时间稳定分
   - 局部桥接分
3. 让攻击推荐的解释字段更完整

验收标准：

- 攻击计划不再只体现单一结构排序

## 7. 推荐开发顺序

建议按下面顺序做，不要反过来。

1. 任务 A：因果分数
2. 任务 B：连续性先验
3. 任务 C：伪边抑制
4. 任务 D：关键节点融合增强
5. 任务 E：局部子图桥接特征
6. 任务 F：接攻击推荐

原因：

- 边推理质量是关键节点识别的上游
- 上游不稳，下游推荐一定漂

## 8. 第一版实现边界

为了保证工程可控，第一版建议只做到：

- 轻量因果分数
- 连续性先验
- 两阶段伪边抑制
- 多特征关键节点融合

第一版不强求：

- 深度学习模型训练
- 完整霍克斯拟合
- 完整格兰杰统计推断

## 9. 最终预期结果

完成后，系统应达到下面效果：

1. 非合作拓扑推理不再只是“共现边”
2. 边会具备一定方向性和触发解释
3. 关键节点推荐更稳、更可信
4. 攻击推荐对真实脆弱节点的命中率更高
5. 前端展示时，能解释“为什么推荐打这个目标”

## 10. 一句话结论

两篇论文真正值得优先借到当前系统里的，不是整套深度学习模型，而是：

- 动态窗口与连续性先验
- 因果型边推理
- 两阶段伪边抑制
- 多特征关键节点融合
- 局部子图桥接特征

这几项与当前代码最兼容，收益也最高。

## 11. 第二版算法改进措施

前面第 6 节的任务 A 到 F，解决的是“第一版增强”：

- 从纯共现边提升到轻量因果边
- 从纯结构排序提升到多特征关键节点融合
- 从单阶段边输出提升到可解释的候选边/过滤边/最终边

这一版已经足够支撑：

- 非合作拓扑推理
- 关键节点推荐
- 非合作攻击闭环
- 前端解释与结果展示

但如果目标继续提高为：

- 更贴近论文中的动态图推理思想
- 更强地抑制伪边
- 更好地区分“真正关键节点”和“只是暂时高活跃节点”
- 提高推荐结果对真实破坏效果的命中率

那么建议进入第二版算法增强。

第二版算法包的总体定义为：

`Directed Dynamic Causal Graph Inference + Robust Key Node Ranking`

也就是：

- 有方向的动态因果拓扑推理
- 条件伪边抑制
- 动态边状态跟踪
- 更强的局部子图风险特征驱动的关键节点识别

当前状态：

- 第二版核心增强已完成第一轮工程落地
- 已实现：
  - 有方向的因果边字段与主导方向输出
  - 条件伪边抑制
  - 动态边状态跟踪
  - 更强的局部子图关键节点特征
- 当前实现版本可概括为：
  - `directed_dynamic_causal_graph_v3`
  - `directed_dynamic_graph_bridge_fusion_v4`

## 11.1 第二版改进目标

第二版不再追求“再多几个普通统计量”，而是明确增强下面四件事：

1. 边的方向性
2. 伪边的条件解释能力
3. 动态拓扑状态跟踪能力
4. 局部子图层面的关键节点风险表达

第二版希望解决的典型问题包括：

- 当前 `A-B` 边虽然有因果分，但仍然更接近“无向强相关边”
- 某些边可能只是共同机动或共同受第三节点影响而形成的伪边
- 同一条边在相邻窗口之间仍然可能抖动
- 某些节点结构中心性不低，但并不是真正值得打击的桥接核心

## 11.2 改进项一：有方向的因果边推理

### 原理

第二篇论文中最值得继续借鉴的地方，是把通信关系理解为：

- 不是简单的“同时出现”
- 而是“一个节点的活动更可能先于并推动另一个节点的活动”

所以第二版不应再把：

- `A -> B`
- `B -> A`

压成一条对称边，而应显式保留方向性。

### 建议增强内容

在当前已有的三类轻量因果得分基础上，扩展成双向显式表示：

- `laggedPredictiveScoreForward`
- `laggedPredictiveScoreBackward`
- `directedResponseScoreForward`
- `directedResponseScoreBackward`
- `excitationScoreForward`
- `excitationScoreBackward`

其中：

- `Forward` 表示 `u -> v`
- `Backward` 表示 `v -> u`

在此基础上再新增：

- `directionalityScore`
- `dominantDirection`

建议定义：

- `directionalityScore = abs(forwardScore - backwardScore)`
- `dominantDirection = u_to_v | v_to_u | bidirectional | undetermined`

### 为什么值得做

这一步能带来三类直接收益：

1. 减少“只因共现强而被误判为强边”的情况
2. 让关键节点识别不只知道“谁中心”，还知道“谁更像信息源头”
3. 让前端和评估层能够解释“推荐打这个节点，是因为它更像上游驱动节点”

### 主要改动文件

- [non_cooperative_inference.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc)
- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

## 11.3 改进项二：条件伪边抑制

### 原理

当前第一版的伪边抑制，主要还是：

- 低置信度过滤
- 低因果过滤
- 低连续性过滤
- 观察者一致性过滤

这已经有效，但仍然偏“单边阈值抑制”。

第二版更值得补的是“条件伪边抑制”。

核心思想是：

- 如果 `u-v` 这条边的相关性，本质上可以被 `u-w` 和 `w-v` 解释
- 那么 `u-v` 就更可能是一条伪边

这类伪边常见于：

- 共同编队动作
- 共同同步控制
- 共同受某个上游节点驱动

### 建议增强内容

不做完整条件独立检验，而做工程可落地的轻量版。

对于每条候选边 `(u,v)`：

1. 找到与 `u`、`v` 都存在强关联的第三节点 `w`
2. 计算：
   - `u-w` 的直接因果强度
   - `w-v` 的直接因果强度
   - `u-v` 的直接因果强度
3. 如果：
   - `u-w` 和 `w-v` 很强
   - 而 `u-v` 自己较弱
4. 则将 `u-v` 标记为：
   - `shared_cause_suspected`
   - 或 `indirect_path_explained`

### 为什么值得做

它能显著压掉这类边：

- 两个节点同时活跃，但并没有直接信息关系
- 两个 follower 同时响应 leader，结果误推成它们之间有边

### 主要改动文件

- [non_cooperative_inference.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc)
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

## 11.4 改进项三：动态边状态跟踪

### 原理

第一版已经有了窗口连续性先验，但它本质上仍然是：

- 当前窗口算一批边
- 再用上一窗口的结果做轻量奖励和平滑

第二版建议更进一步，把边当作“有状态的动态对象”。

建议每条边维护以下状态之一：

- `emerging`
- `stable`
- `weakening`
- `vanished`

同时引入显式后验更新：

`posterior_t = alpha * currentEvidence + beta * posterior_{t-1}`

并辅以状态迁移逻辑：

- 连续高证据才进入 `stable`
- 连续低证据才进入 `weakening`
- 连续弱化后才进入 `vanished`

### 为什么值得做

这一步的价值很高：

1. 边不会因为一个短窗口波动就立刻出现/消失
2. 图结构更稳定，关键节点排序更稳
3. 攻击推荐能区分：
   - “刚冒出来的热点”
   - “长期稳定的重要节点”

### 建议新增字段

- `edgeDynamicState`
- `posteriorEdgeProbability`
- `stabilityAge`
- `weakeningAge`

### 主要改动文件

- [non_cooperative_inference.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc)
- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

## 11.5 改进项四：更强的局部子图关键节点特征

### 原理

第一篇论文里最值得继续借的，不是立刻引入 CNN/GCN，而是：

- 不只看单节点统计量
- 要保留节点所在局部图结构的信息

当前第一版已经有：

- `localBridgeScore`
- `postRemovalDamageScore`

这已经迈出第一步，但第二版还可以继续加强。

### 建议新增特征

建议补这四类局部结构特征：

- `twoHopReachabilityScore`
- `interClusterBridgeScore`
- `localCutRiskScore`
- `neighborRedundancyPenalty`

它们的含义分别是：

1. `twoHopReachabilityScore`
   - 节点能在 2-hop 范围内影响多少局部可达节点

2. `interClusterBridgeScore`
   - 节点是否连接了两个低连通或不同簇的局部区域

3. `localCutRiskScore`
   - 删除该节点后，2-hop 子图是否分裂成多个连通块

4. `neighborRedundancyPenalty`
   - 如果一个节点邻居之间本就高度冗余连通，那么它的重要性应适度降权

### 为什么值得做

这一步比单纯 degree 更能识别：

- 真正的桥接核心
- 真正的一跳/两跳瓶颈
- 真正导致局部断裂的关键节点

这类特征特别适合用于：

- 非合作攻击推荐
- “为什么打它而不是打另一个高活跃节点”的解释

### 主要改动文件

- [non_cooperative_attack.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_attack.cc)
- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

## 11.6 第二版不建议立即做的内容

虽然两篇论文都涉及更强的学习模型路线，但第二版仍然不建议直接做这些：

- 全量时序图像化 + CNN 边分类
- CNN/GCN 关键节点识别
- 完整霍克斯过程参数拟合
- 完整格兰杰统计检验和显著性流程

原因是：

- 当前系统已经有完整工程闭环
- 第二版优先目标应是“更强、但仍然可解释和可调试”
- 现在最需要的是增强边方向、动态状态和局部风险表达
- 不是立即引入训练数据和模型部署成本

所以第二版最合理的上限仍然是：

- 强因果
- 强动态
- 强过滤
- 强局部结构

## 11.7 第二版建议开发顺序

建议按这个顺序推进，不要反过来：

1. 先做“有方向的因果边”
2. 再做“条件伪边抑制”
3. 再做“动态边状态跟踪”
4. 最后做“关键节点局部结构增强”

原因：

- 方向性和条件过滤先稳住边质量
- 动态状态再稳住时间演化
- 局部结构增强最后接入推荐器，收益最大

## 11.8 第二版验收建议

如果启动第二版开发，建议按下面口径验收，而不是只看“代码有没有跑通”。

### 拓扑推理侧

- `inferred_topology_edges.csv` 新增方向性字段
- 同一条边能区分 `u->v` 和 `v->u`
- 输出中能看到：
  - `dominantDirection`
  - `directionalityScore`
  - `edgeDynamicState`
- `filtered_out` 中能新增：
  - `shared_cause_suspected`
  - `indirect_path_explained`

### 关键节点与推荐侧

- 推荐结果新增：
  - `twoHopReachabilityScore`
  - `interClusterBridgeScore`
  - `localCutRiskScore`
  - `neighborRedundancyPenalty`
- 推荐理由能解释：
  - 该节点为什么是局部瓶颈
  - 为什么比另一个高中心性节点更值得打

### 效果评估侧

- 推荐目标相对 `structure_baseline` 的真实损伤效果提升
- 推荐目标与 `oracle_best` 的差距缩小
- 连续窗口中的 top-1 推荐切换频率下降

## 11.9 一句话结论

第二版最该加强的，不是继续堆更多普通中心性指标，而是：

- 有方向的因果边
- 条件伪边抑制
- 动态边状态跟踪
- 局部子图层面的关键节点风险特征

这四项是当前系统继续增强时，最贴论文、最可解释、也最值得工程落地的方向。
