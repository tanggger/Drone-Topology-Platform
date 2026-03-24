# 非合作模式完整实现定稿

## 1. 文档目的

这份文档单独用于收敛非合作模式的模式层、观测层、输出层、推理输入层与前端视图，不再混入总体两周计划。

本文档回答的问题是：

- `cooperative / non_cooperative` 模式如何正式进入后端
- 非合作模式下到底能观测到什么
- 观测数据如何加噪声、加缺失、分时间窗口
- 后端输出文件叫什么，字段有哪些
- 观测层、窗口层、边证据层如何分层输出
- 前端如何同时承载观测证据与推理结果

当前版本目标不是只完成观测层闭环，而是直接为以下完整链路服务：

- 观测事件输出
- 窗口化观测输出
- 边证据构建
- 拓扑推理
- 图表示
- 关键节点识别
- 破坏/打击评估

## 2. 理论依据与使用方式

当前实现版本的非合作观测层建模，主要借鉴：

- [面向非合作无人机通信网络的通联拓扑推理技术.pdf](/home/tzx/ns-3.43/project_docs/面向非合作无人机通信网络的通联拓扑推理技术.pdf)

该文对当前工程最有价值的部分不是最终算法，而是它对非合作问题的系统拆分：

- 感知系统
- 数据预处理子系统
- 推理子系统

以及对“外部非侵入式可观测特征”的界定，例如：

- 发射开始时间
- 发射结束时间
- 持续时长
- 平均功率
- 频点
- 状态时间序列

另一篇：

- [基于图表示的通信网拓扑推理与关键节点识别_李鹏雪.pdf](/home/tzx/ns-3.43/project_docs/基于图表示的通信网拓扑推理与关键节点识别_李鹏雪.pdf)

在当前实现版本中，该文不只作为后续参考，也作为图表示、关键节点识别、边权构建阶段的方向依据。

## 3. 模式层定稿

系统必须先显式区分两种任务模式：

- `cooperative`
- `non_cooperative`

模式入口统一采用命令行参数：

- `--operationMode=cooperative`
- `--operationMode=non_cooperative`

### 3.1 cooperative 模式边界

- 允许输出真实拓扑
- 允许输出真实链路质量
- 不强制输出非合作观测视角数据
- 不进入敌方链路推断主流程

### 3.2 non_cooperative 模式边界

- 前端默认不显示真实拓扑
- 后续推理模块默认不直接读取真实拓扑
- 必须输出观测视角数据
- 真实拓扑仅允许作为后台评估/调试真值保留

这条边界非常重要：

- 非合作模式不能名义上是“观测和推断”
- 实际上却仍在直接使用真实链路

## 4. 观测方模型定稿

当前版本中，非合作模式的观测方统一定义为：

- 我方 UAV 本身

即：

- `observerId` 直接对应我方 UAV 节点 ID
- 默认所有我方 UAV 都具备观测能力
- 默认采用全向观测
- 不额外引入方向性天线或视场模型

这一设定的目的：

- 直接复用当前仿真中已存在的我方节点位置与运动信息
- 让 `range / occlusion / random_drop` 可以稳定落代码
- 避免第一版再额外引入独立侦察节点体系

当前版本还明确约束：

- 不允许自观测
- `observerId` 只属于我方观测器集合
- `observedNodeId` 只属于目标侧被观测对象集合
- 两者必须处于不同命名空间

### 4.1 观测能力参数定稿

为了让 `range / occlusion / random_drop` 与噪声模型可以直接进入代码，当前版本把观测能力参数定为以下工程初值。

观测半径：

- `urban = 180 m`
- `forest = 140 m`
- `lake = 260 m`
- `open-field = 220 m`

随机漏检率：

- `urban = 0.15`
- `forest = 0.18`
- `lake = 0.08`
- `open-field = 0.10`

位置噪声标准差：

- `urban = 20 m`
- `forest = 25 m`
- `lake = 12 m`
- `open-field = 15 m`

功率噪声标准差：

- `urban = 4.0 dB`
- `forest = 4.5 dB`
- `lake = 2.0 dB`
- `open-field = 3.0 dB`

说明：

- 这些值是当前版本的工程初始化值
- 它们用于支撑非合作观测层的可编码实现
- 并不声称是论文直接给出的唯一真值

## 5. 非合作完整链路目标

在 `non_cooperative` 模式下，系统不再默认“已知目标网络真实链路”，而是输出一份侦察侧可见的观测数据。

这条链路必须满足：

- 部分可见
- 带噪声
- 带缺失
- 按统一时间轴组织
- 既能支持前端展示，也能支持推理与关键节点识别

当前实现版本不仅要让系统稳定输出：

- 我方实际观测到了什么
- 这些观测有多少噪声
- 哪些窗口是缺失的
- 哪些观测更可信

还要进一步保证：

- 观测数据能直接进入后续链路推理
- 推理结果能进一步进入图表示与关键节点识别
- 前端可以同时展示观测证据和推理结果，只是默认先强调观测证据

## 6. 可观测信号定稿

结合论文可观测信息和当前工程可实现性，当前版本主观测信号分为两层：

### 6.1 P0：主观测信号

- `observedNodeId`
- `txStartTime`
- `txEndTime`
- `txDuration`
- `avgRxPowerDbm`
- `channelId`
- `centerFrequencyHz`
- `signalDetected`
- `windowStart`
- `windowEnd`
- `stateSequence`
- `coarsePosX`
- `coarsePosY`
- `confidence`

说明：

- `observedNodeId` 表示跨窗口稳定维护的观测 track id
- `observedNodeId` 不等于真实身份 ID
- `observedNodeId` 也不是窗口内一次性的临时辐射源编号
- `stateSequence` 表示在固定观测窗口内的发射活动序列，用于后续因果、转移熵、霍克斯过程等方法输入
- `coarsePosX / coarsePosY` 是带误差的粗位置，不是后端真实位置真值
- `confidence` 是观测可信度摘要值

### 6.2 P1：增强观测与预处理信号

为了避免后续推理和关键节点识别阶段返工，当前版本把下面这组字段一并纳入正式内部结构，允许作为调试输出或增强输出：

- `signalSortingGroup`
- `nodeSignalAssociation`
- `disambiguationStatus`
- `activityPatternScore`

说明：

- `signalSortingGroup`：信号分选分组结果
- `nodeSignalAssociation`：观测信号与节点的关联结果
- `disambiguationStatus`：节点消歧结果
- `activityPatternScore`：节点发射行为规律特征摘要

这些字段不一定全部进入前端主面板，但应在当前实现版本的数据结构中保留。

## 7. `stateSequence` 的定义

`stateSequence` 统一定义为：

- 在一个固定窗口内，把节点“是否处于发射活动状态”编码为一串离散状态

示例：

- `1110001100`

表示：

- 窗口内前 3 个子时隙检测到发射
- 中间 3 个子时隙未检测到发射
- 后 2 个子时隙再次检测到发射

当前版本进一步约定：

- 默认观测窗口为 `0.5s`
- 每个窗口内部切分为 `10` 个子时隙
- 每个子时隙长度为 `0.05s`

保留 `stateSequence` 的原因：

- 便于后续直接进入时序推理算法
- 比单一窗口统计值保留更多结构信息
- 同时便于前端做时间窗级活动可视化

## 8. 输出分层定稿

如果要一口气实现，就不应只保留一个窗口文件，而应把输出拆成 4 层：

1. 事件层
- `observed_signal_events.csv`
- 用于保存更细粒度的原始/事件级观测

2. 窗口层
- `observed_comm_windows.csv`
- 用于前端主展示、窗口统计、时序特征输入

3. 边证据层
- `observed_link_evidence.csv`
- 用于保存推理前的观测边证据

另外单独定义推理结果层：

4. 推理结果层
- `inferred_topology_edges.csv`
- 用于保存推理后得到的概率边结果

四层关系如下：

- 事件层生成窗口层
- 窗口层生成边证据层
- 边证据层作为推理模块输入之一
- 推理模块输出 `inferred_topology_edges.csv`
- 前端主面板优先读取窗口层、边证据层和推理结果层

必须明确：

- `observed_link_evidence.csv` 不是推理结果文件
- `inferred_topology_edges.csv` 才是推理后的边结果文件
- 两者严格分层，不能混用

## 9. 稳定观测目标与稠密输出语义

### 9.1 `observedNodeId` 的稳定性定义

当前版本统一采用：

- `observedNodeId` = 跨窗口稳定维护的观测 track id

因此：

- 边证据构建、图表示、关键节点识别都以该稳定 track id 为基础
- 与 track 相关的不确定性不通过直接翻转 `observedNodeId` 表达
- 不确定性通过以下字段表达：
  - `signalSortingGroup`
  - `nodeSignalAssociation`
  - `disambiguationStatus`
  - `positionConfidence`
  - `signalConfidence`
  - `overallConfidence`

### 9.2 稳定 track 的建立与淘汰

为了稳定定义稠密输出边界，当前版本采用以下规则：

- 新目标连续 `2` 个窗口成功观测后，建立稳定 track
- 已建立 track 连续 `4` 个窗口缺失后，删除该 track

在默认 `0.5s` 窗口下，这等价于：

- 约 `1.0s` 的创建确认时长
- 约 `2.0s` 的删除确认时长
### 9.3 稠密输出语义

窗口层采用稠密输出，但这里的“稠密”限定为：

- 对当前已建立的稳定 `observedNodeId` 集合进行稠密输出
- 不对所有潜在目标做空占位输出

更具体地说：

- 对每个 `observerId × observedNodeId × window`
- 都输出一条窗口记录

然后通过字段区分：

- 当前窗口无活动
- 当前窗口存在活动且成功观测
- 当前窗口存在活动但发生缺失

这样前端和推理模块才能区分：

- 真正无活动
- 观测失败
- 低置信度活动
## 10. 事件层输出：`observed_signal_events.csv`

该文件用于保存更细粒度的观测事件，不替代窗口层。

当前版本统一定义事件粒度为：

- 一次连续可分辨发射活动记为一个事件

不强求协议帧级精细拆分。

事件层到窗口层的 `stateSequence` 映射规则统一定义为：

- 子时隙内任一事件与该子时隙存在时间重叠，则该子时隙记为 `1`
- 否则记为 `0`

也就是采用“任一重叠即激活”的规则。

推荐列如下：

- `eventTime`
- `observedNodeId`
- `txStartTime`
- `txEndTime`
- `txDuration`
- `avgRxPowerDbm`
- `channelId`
- `centerFrequencyHz`
- `signalDetected`
- `coarsePosX`
- `coarsePosY`
- `positionConfidence`
- `signalConfidence`
- `overallConfidence`
- `isMissing`
- `missingReason`
- `noiseLevel`
- `observerId`
- `sceneType`
- `operationMode`

## 11. 窗口层输出：`observed_comm_windows.csv`

该文件仍然是当前版本前端与推理输入最关键的主文件。

推荐列如下：

- `windowStart`
- `windowEnd`
- `observedNodeId`
- `txStartTime`
- `txEndTime`
- `txDuration`
- `avgRxPowerDbm`
- `channelId`
- `centerFrequencyHz`
- `signalDetected`
- `stateSequence`
- `activeRatio`
- `txCount`
- `coarsePosX`
- `coarsePosY`
- `positionConfidence`
- `signalConfidence`
- `overallConfidence`
- `isMissing`
- `missingReason`
- `noiseLevel`
- `observerId`
- `sceneType`
- `operationMode`

### 11.1 字段说明

`activeRatio`：

- 在当前窗口内，节点处于发射活动状态的比例

`txCount`：

- 当前窗口内检测到的发射事件次数

时间字段统一采用以下语义：

- `txStartTime`：当前窗口内首个发射事件开始时间
- `txEndTime`：当前窗口内最后一个发射事件结束时间
- `txDuration`：当前窗口内全部发射事件持续时间累计值
- `txCount`：当前窗口内发射事件总次数

`positionConfidence`：

- 位置估计可信度

`signalConfidence`：

- 信号检测可信度

`overallConfidence`：

- 综合可信度

`isMissing`：

- 当前窗口是否属于缺失观测

`missingReason`：

- 当前缺失原因，建议取值：
  - `range`
  - `occlusion`
  - `random_drop`

`noiseLevel`：

- 当前窗口整体噪声等级或噪声摘要

`observerId`：

- 观测方或侦察侧传感器编号

### 11.2 无活动与缺失时的空值口径

为了避免前端解析与后续聚合歧义，窗口层统一采用以下空值规则。

情况 A：当前窗口无活动

- `signalDetected = 0`
- `isMissing = 0`
- `missingReason = ""`
- `txCount = 0`
- `txDuration = 0`
- `txStartTime = NaN`
- `txEndTime = NaN`

情况 B：当前窗口存在活动，但观测缺失

- `signalDetected = 0`
- `isMissing = 1`
- `missingReason = range | occlusion | random_drop`
- `txCount = NaN`
- `txDuration = NaN`
- `txStartTime = NaN`
- `txEndTime = NaN`

情况 C：当前窗口存在活动，且成功观测

- `signalDetected = 1`
- `isMissing = 0`
- `missingReason = ""`
- 时间与计数字段按已定窗口语义填写

当前版本统一采用：

- `NaN` 作为空值口径

而不是使用 `-1` 这类哨兵值。

## 12. 边证据层输出：`observed_link_evidence.csv`

该文件保存的是推理前的观测边证据，而不是最终推理结果。

当前版本约定：

- 事件层和窗口层保留 `observerId`
- 边证据层先融合多个 observer 的证据，再输出聚合边证据
- 因此边证据层不再保留单个 observer 的原始细节行

推荐列如下：

- `windowStart`
- `windowEnd`
- `srcObservedNodeId`
- `dstObservedNodeId`
- `evidenceStrength`
- `commCount`
- `commDurationTotal`
- `avgRxPowerDbm`
- `channelId`
- `centerFrequencyHz`
- `observerCount`
- `observerAgreementScore`
- `edgeObservationConfidence`
- `isMissing`
- `missingReason`
- `noiseLevel`
- `sceneType`
- `operationMode`

说明：

- `evidenceStrength`：边证据强度
- `commCount`：窗口内推测通信次数
- `commDurationTotal`：窗口内累计通信时长
- `observerCount`：为该边提供观测证据的 observer 数量
- `observerAgreementScore`：多个 observer 之间的证据一致性得分
- `edgeObservationConfidence`：聚合后边观测证据可信度

## 13. 推理结果层输出：`inferred_topology_edges.csv`

该文件保存推理模块输出的概率边结果。

当前版本统一约定：

- 该文件按窗口输出
- 不只输出最终累计稳定拓扑
- 如后续需要，再额外补稳定汇总文件

推荐列如下：

- `windowStart`
- `windowEnd`
- `srcObservedNodeId`
- `dstObservedNodeId`
- `edgeProbability`
- `edgeConfidence`
- `inferenceMethod`
- `sceneType`
- `operationMode`

## 14. 噪声建模定稿

非合作模式下，P0 主观测项全部都需要体现不确定性。

但不采用“所有字段都统一做数值加噪”，而采用按字段类型区分的方式：

- 连续量：
  - 数值扰动
  - 适用于位置、时间、功率
- 离散量：
  - 误判或误分类
  - 适用于 `signalDetected`、`channelId`
- 序列量：
  - 序列翻转、局部错误、局部错位
  - 适用于 `stateSequence`
- 同时输出：
  - `positionConfidence`
  - `signalConfidence`
  - `overallConfidence`

这样做的目的：

- 所有主观测项都不是“完美真值”
- 同时实现方式仍然可控

特别说明：

- `observedNodeId` 不作为可任意翻转的离散噪声字段
- 与目标身份稳定性相关的不确定性，统一通过：
  - `signalSortingGroup`
  - `nodeSignalAssociation`
  - `disambiguationStatus`
  - 以及各类 `confidence`
  表达
## 15. 缺失机制定稿

非合作观测不仅要有噪声，还必须存在缺失。

当前版本统一采用三层缺失机制：

1. 距离缺失
- 超出观测范围则不可见

2. 遮挡缺失
- 城市建筑、森林遮挡等导致观测失败

3. 随机漏观测
- 在前两者基础上叠加随机缺失

缺失原因统一映射到：

- `range`
- `occlusion`
- `random_drop`

并通过：

- `isMissing`
- `missingReason`

写入输出文件。

### 15.1 距离缺失口径

当前版本中，观测半径按场景给默认值：

- `urban = 180 m`
- `forest = 140 m`
- `lake = 260 m`
- `open-field = 220 m`

说明：

- 这些值是依据场景传播差异给出的工程初值
- 它们不是论文直接给出的观测真值
- 后续可以结合仿真结果继续校准

### 15.2 遮挡缺失口径

`occlusion` 缺失不在非合作观测层单独维护第二套规则，而是：

- 直接复用场景环境层已有的几何/遮挡判定结果

这样可以避免：

- 环境层与观测层出现两套不一致的遮挡结论
- 重复实现几何遮挡逻辑

### 15.3 随机漏观测口径

`random_drop` 也按场景给默认值：

- `urban = 0.15`
- `forest = 0.18`
- `lake = 0.08`
- `open-field = 0.10`

说明：

- 该值表示在距离与遮挡之外的额外随机漏观测概率
- 也是当前版本的工程初值，而不是论文直接真值

### 15.4 噪声口径

位置噪声标准差：

- `urban = 20 m`
- `forest = 25 m`
- `lake = 12 m`
- `open-field = 15 m`

功率噪声标准差：

- `urban = 4.0 dB`
- `forest = 4.5 dB`
- `lake = 2.0 dB`
- `open-field = 3.0 dB`

说明：

- 位置噪声用于 `coarsePosX / coarsePosY`
- 功率噪声用于 `avgRxPowerDbm`
- 两者均为当前版本的工程初始化值

## 16. 时间窗口定稿

非合作观测窗口不再只定义一个唯一窗口，而是：

- `0.5s` 作为默认展示窗口与默认聚合窗口
- 保留事件级时间戳
- 允许后续从事件层重新聚合不同尺度窗口

当前后端现有 QoS 统计逻辑为：

- 每 `0.1s` 更新一次
- 基于 `2s` 滑动窗口统计 PDR / 时延 / 吞吐量

因此本项目采用：

- 非合作观测：默认 `0.5s` 窗口 + 事件级时间戳
- QoS 指标：沿用现有 `0.1s` 采样 + `2s` 滑动统计

二者共享统一时间轴，但不强制使用同一统计窗口。

这样做的好处：

- 非合作观测既适合前端回放，也不把后续推理锁死在单一窗口尺度
- QoS 指标仍保留平滑稳定性

## 17. 前端显示边界

在 `non_cooperative` 模式下，前端应优先展示：

- 当前模式为 `non_cooperative`
- 当前观测窗口
- 当前可观测节点活动
- 当前噪声等级
- 当前缺失情况
- 当前置信度分布

当前版本前端不应只停留在观测证据，而应同时具备：

- 观测证据面板
- 推理结果面板
- 关键节点识别面板
- 破坏/打击评估面板

但默认视觉重点仍应是：

- 先让用户看清“观测到了什么”
- 再理解“从这些观测推出了什么”

也就是说，当前版本目标就是：

- 同版实现观测证据、推理结果、关键节点识别与破坏评估
- 只是编码顺序上按依赖关系推进

## 18. 代码落点建议

对应到当前后端结构，建议落点如下：

- [main.cc](/home/tzx/ns-3.43/scratch/uav_ra/main.cc)
  - 新增 `operationMode` 参数解析
- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
  - 定义 `OperationMode`
  - 扩展 `ObservationPreset`
  - 定义观测窗口记录结构
- [scenario_environment.cc](/home/tzx/ns-3.43/scratch/uav_ra/scenario_environment.cc)
  - 提供场景遮挡与环境几何查询能力
- [traffic_metrics.cc](/home/tzx/ns-3.43/scratch/uav_ra/traffic_metrics.cc)
  - 记录事件层观测
  - 聚合默认 `0.5s` 窗口
  - 输出 `observed_signal_events.csv`
  - 输出 `observed_comm_windows.csv`
- [topology_control.cc](/home/tzx/ns-3.43/scratch/uav_ra/topology_control.cc)
  - 消费推理结果
  - 为关键节点识别与干扰评估提供图结构入口
- 新增推理模块文件（建议）
  - 如 `non_cooperative_inference.cc`
  - 承担从窗口层到边证据层的证据构建
  - 承担从窗口层/边证据层到 `inferred_topology_edges.csv` 的推理与概率边生成
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)
  - 初始化四层输出文件
  - 写入表头
## 19. 验收标准

为了匹配“一口气实现”的目标，验收分成两层：

### 16.1 基础验收

1. 后端能明确区分 `cooperative / non_cooperative`
2. `non_cooperative` 下能输出三层文件：
   - `observed_signal_events.csv`
   - `observed_comm_windows.csv`
   - `observed_link_evidence.csv`
   - `inferred_topology_edges.csv`
3. 输出数据包含噪声、缺失和可信度
4. 前端能看到“当前观测到了什么”

### 16.2 完整验收

1. 推理模块能消费窗口层或边证据层输入
2. 前端能同时展示观测证据与推理结果
3. 能输出边概率、边置信度
4. 能接入关键节点识别
5. 能接入破坏/打击评估
## 20. 当前推荐开发顺序

虽然目标是一口气实现，但代码上仍建议按依赖顺序推进：

1. 先加 `operationMode`
2. 再定义 `ObservationPreset`
3. 再定义事件层、窗口层、边证据层结构
4. 打通 `observed_signal_events.csv`
5. 打通 `observed_comm_windows.csv`
6. 打通 `observed_link_evidence.csv`
7. 打通 `inferred_topology_edges.csv`
8. 再把推理结果接到关键节点识别和破坏评估

## 21. 完整开发清单

本节不是“第一版简化方案”，而是按本文档已经定稿的最终结构，给出一份可直接执行的完整开发步骤。

整体原则：

- 目标一次性对齐到完整形态
- 编码顺序按依赖关系推进
- 每一步完成后都必须检查：
  - 能否正常编译
  - 输出文件是否生成
  - 字段语义是否符合本文档
  - 是否未越过当前步骤边界直接偷用真值

### 第 1 步：接入任务模式层

目标：

- 正式把 `cooperative / non_cooperative` 接入后端主入口
- 后端运行时能明确知道当前任务模式
- 所有后续输出层都能带上 `operationMode`

涉及文件：

- [main.cc](/home/tzx/ns-3.43/scratch/uav_ra/main.cc)
- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
- [simulation_setup.cc](/home/tzx/ns-3.43/scratch/uav_ra/simulation_setup.cc)

需要完成：

- 在 `context.h` 中定义 `OperationMode`
- 在全局配置中加入 `operationMode`
- 在 `main.cc` 中新增命令行参数：
  - `--operationMode=cooperative`
  - `--operationMode=non_cooperative`
- 在启动阶段统一校验该参数
- 在环境摘要或运行摘要中保留当前模式

完成标准：

- 后端运行时能稳定区分两种模式
- `cooperative` 不输出非合作观测层文件
- `non_cooperative` 进入观测层主流程

### 第 2 步：扩展观测配置层

目标：

- 把本文档中的非合作观测参数正式落入代码结构体
- 不再靠零散常量或局部临时变量实现噪声、缺失和窗口规则

涉及文件：

- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
- [simulation_setup.cc](/home/tzx/ns-3.43/scratch/uav_ra/simulation_setup.cc)

需要完成：

- 扩展 `ObservationPreset`
- 将下列参数纳入正式配置：
  - `windowDurationSec`
  - `subslotCount`
  - `subslotDurationSec`
  - `trackCreateWindowCount`
  - `trackDeleteWindowCount`
  - 各场景默认观测半径
  - 各场景随机漏检率
  - 各场景位置噪声标准差
  - 各场景功率噪声标准差
- 提供一个统一函数，根据：
  - `sceneType`
  - `difficulty`
  - `operationMode`
  组装非合作观测参数

完成标准：

- 非合作观测层不再依赖散落常量
- 当前场景下的观测参数可统一读取

### 第 3 步：定义完整数据结构

目标：

- 把四层输出对应的数据结构一次性补齐
- 后续代码不直接拼字符串或临时 map 来组织观测数据

涉及文件：

- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)

需要完成：

- 定义事件层结构，例如：
  - `ObservedSignalEvent`
- 定义窗口层结构，例如：
  - `ObservedCommWindow`
- 定义边证据层结构，例如：
  - `ObservedLinkEvidence`
- 定义推理结果层结构，例如：
  - `InferredTopologyEdge`
- 定义稳定 track 结构，例如：
  - `ObservedTrackState`
- 定义观测缓存、窗口缓存、track 状态容器

完成标准：

- 四层输出和 track 生命周期都有正式结构体承载
- 后续实现不再重复讨论字段命名

### 第 4 步：初始化四层输出文件

目标：

- 四层输出文件一次性接入系统
- 在 `non_cooperative` 模式下自动创建并写入表头

涉及文件：

- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

需要完成：

- 初始化：
  - `observed_signal_events.csv`
  - `observed_comm_windows.csv`
  - `observed_link_evidence.csv`
  - `inferred_topology_edges.csv`
- 表头字段严格对齐本文档
- 仅在 `non_cooperative` 模式下创建这些文件

完成标准：

- 启动非合作模式后能看到 4 个文件
- 表头字段完整且无歧义

### 第 5 步：建立目标侧与观测侧命名空间

目标：

- 保证 `observerId` 与 `observedNodeId` 真正属于不同命名空间
- 为后续 track 稳定化和前端显示打基础

涉及文件：

- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
- [traffic_metrics.cc](/home/tzx/ns-3.43/scratch/uav_ra/traffic_metrics.cc)

需要完成：

- 建立我方 observer 集合
- 建立目标侧 observed object / track id 分配规则
- 明确禁止自观测
- 给目标侧维护稳定 track id

完成标准：

- 输出中不会混淆 observer 和 target
- `observedNodeId` 可跨窗口复用

### 第 6 步：实现事件层观测生成

目标：

- 在 `non_cooperative` 模式下，把目标侧发射活动转换成侦察侧观测事件
- 真实事件先进入事件层，再进入窗口层

涉及文件：

- [traffic_metrics.cc](/home/tzx/ns-3.43/scratch/uav_ra/traffic_metrics.cc)
- [scenario_environment.cc](/home/tzx/ns-3.43/scratch/uav_ra/scenario_environment.cc)

需要完成：

- 明确事件触发来源
- 对每个 observer 判断：
  - 是否在观测半径内
  - 是否被遮挡
  - 是否发生随机漏检
- 对位置和功率加入噪声
- 输出事件层记录：
  - `observed_signal_events.csv`

完成标准：

- 事件层文件中能看到真实非合作观测事件
- 包含噪声、缺失和可信度

### 第 7 步：实现稳定 track 管理

目标：

- 把事件流稳定映射为跨窗口 track
- 按文档规则实现创建和淘汰

涉及文件：

- [traffic_metrics.cc](/home/tzx/ns-3.43/scratch/uav_ra/traffic_metrics.cc)

需要完成：

- 连续 `2` 个窗口观测成功后建立 track
- 连续 `4` 个窗口缺失后删除 track
- 维护 track 的最后观测时间、缺失计数、活动状态

完成标准：

- `observedNodeId` 在窗口层是稳定的
- 不会每个窗口重新生成一个临时 id

### 第 8 步：实现窗口层聚合

目标：

- 从事件层生成默认 `0.5s` 窗口层输出
- 严格遵守本文档中对时间字段、稠密输出和空值的定义

涉及文件：

- [traffic_metrics.cc](/home/tzx/ns-3.43/scratch/uav_ra/traffic_metrics.cc)

需要完成：

- 每 `0.5s` 聚合一次窗口
- 每个窗口内部按 `10` 个子时隙生成 `stateSequence`
- 执行映射规则：
  - 子时隙内任一事件与子时隙重叠，则该子时隙记为 `1`
- 对每个 `observerId × observedNodeId × window` 输出一条记录
- 严格区分：
  - 无活动
  - 有活动且观测成功
  - 有活动但发生缺失
- 空值语义按文档落地：
  - `NaN`
  - `0`
  - `1`
  - `0`

完成标准：

- `observed_comm_windows.csv` 能稳定生成
- 前端和后续推理都能区分无活动与缺失

### 第 9 步：实现边证据层构建

目标：

- 从窗口层构造推理前的边证据
- 先做“观测证据组织”，不把它和推理结果混在一起

涉及文件：

- 新增 [non_cooperative_inference.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc)
- 可配套新增头文件

需要完成：

- 从窗口层构造：
  - `evidenceStrength`
  - `commCount`
  - `commDurationTotal`
  - `avgRxPowerDbm`
  - `edgeObservationConfidence`
- 融合多个 observer 的证据
- 输出：
  - `observerCount`
  - `observerAgreementScore`
- 写出：
  - `observed_link_evidence.csv`

完成标准：

- 边证据层是推理前证据，不含最终概率边
- 多 observer 融合逻辑稳定存在

### 第 10 步：实现推理结果层

目标：

- 从窗口层 / 边证据层生成推理后的概率边
- 正式输出 `inferred_topology_edges.csv`

涉及文件：

- 新增 [non_cooperative_inference.cc](/home/tzx/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc)
- [topology_control.cc](/home/tzx/ns-3.43/scratch/uav_ra/topology_control.cc)

需要完成：

- 实现推理入口函数
- 消费：
  - `ObservedCommWindow`
  - `ObservedLinkEvidence`
- 输出：
  - `edgeProbability`
  - `edgeConfidence`
  - `inferenceMethod`
- 写出：
  - `inferred_topology_edges.csv`

完成标准：

- 推理结果层与边证据层严格分离
- 概率边输出可被前端和后续图模块消费

### 第 11 步：把推理结果接入图表示和关键节点识别入口

目标：

- 让非合作观测层不止停在“生成 CSV”
- 而是正式成为图表示与关键节点识别的输入层

涉及文件：

- [topology_control.cc](/home/tzx/ns-3.43/scratch/uav_ra/topology_control.cc)
- 新增图分析/关键节点模块文件（如需要）

需要完成：

- 把 `inferred_topology_edges.csv` 对应的数据结构接入图入口
- 为关键节点识别准备：
  - 图节点
  - 图边
  - 边概率
  - 边置信度

完成标准：

- 后续关键节点识别与破坏评估不再需要重新组织输入

### 第 12 步：把非合作层输出接到前端

目标：

- 让前端能直接使用这四层数据
- 不是只在后端产文件

涉及位置：

- 前端非合作工作区的数据加载层
- API / 文件读取适配层

需要完成：

- 前端读取：
  - `observed_comm_windows.csv`
  - `observed_link_evidence.csv`
  - `inferred_topology_edges.csv`
- 优先展示：
  - 当前窗口观测证据
  - 当前噪声等级
  - 当前缺失情况
- 同步支持：
  - 概率边
  - 推理结果
  - 关键节点高亮

完成标准：

- 非合作工作区能真正展示观测与推理两层结果

### 第 13 步：做端到端校验

目标：

- 在完整链路下做一次真实验收

需要完成：

- 分别验证：
  - cooperative 模式不输出非合作观测层文件
  - non_cooperative 模式四层文件齐全
  - 观测文件有噪声、缺失和置信度
  - 推理结果文件不为空
  - 前端能区分观测证据与推理结果
- 每步都检查：
  - 代码能编译
  - 仿真能跑
  - 文件能生成
  - 字段语义无偏差

最终通过标准：

1. 后端能区分 `cooperative / non_cooperative`
2. `non_cooperative` 下 4 层输出都能稳定生成
3. 观测层、边证据层、推理结果层语义不混淆
4. 前端能同时承载观测证据与推理结果
5. 关键节点识别和破坏评估已有稳定输入入口
