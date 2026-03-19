# Dynamic + Robust Information Bottleneck 集成方案

## 1. 文档目标

本文档给出一个面向当前仓库的专属方案：将“鲁棒信息瓶颈”思想作为增强模块，逐步集成到现有 `dynamic` 资源分配流程中，用于提升系统在以下扰动下的稳定性：

- RTK 漂移与测量噪声
- 黑飞节点干扰
- 短时吞吐量/时延抖动
- 建筑遮挡与 NLOS
- 局部观测不完备
- 拓扑快速变化

目标不是替换当前 C++ `dynamic` 规则系统，而是为其增加一层“鲁棒状态表征与决策辅助模块”。

---

## 2. 当前系统基线

现有动态资源分配主流程位于：

- [`scratch/uav_resource_allocation.cc`](/home/tzx/ns-3.43/scratch/uav_resource_allocation.cc)

关键函数：

- `PerformResourceReallocation()`
  - 入口调度函数
  - 每 0.1s 记录一次状态，每 0.5s 执行一次资源逻辑
- `DynamicChannelAllocation()`
  - 动态信道分配
- `AdaptiveRateControl()`
  - 自适应速率控制
- `DynamicPowerControl()`
  - 动态功率控制
- `MonitorQoSPerformance()`
  - QoS 统计与吞吐量/PDR/时延更新
- `UpdateTopology()`
  - 邻接关系和拓扑状态更新

当前 `dynamic` 的逻辑本质是：

1. 读取当前网络状态
2. 基于启发式规则估计链路质量和干扰
3. 调整信道、速率、功率
4. 下发到 PHY/MAC

当前优势：

- 工程闭环完整
- 能直接运行在 NS-3 仿真中
- 可解释性较强

当前弱点：

- 对 noisy state 很敏感
- 容易把短时抖动误判成真实退化
- 观测状态未区分“任务相关信息”和“噪声/扰动信息”
- 所有输入默认同等可信

---

## 3. 集成目标

将鲁棒信息瓶颈思想接入 `dynamic`，形成：

```text
Raw State -> Robust IB Encoder -> Clean Latent S / Noise Latent T
         -> Dynamic Decision Adapter -> 现有 Dynamic 调度器
```

核心效果：

- `S` 保留对资源调度真正有用的信息
- `T` 吸收噪声、扰动、假异常、观测不一致性
- `dynamic` 后续逻辑尽量基于 `S` 做判断，而不是直接吃原始 noisy state

一句话定义：

> 让 `dynamic` 的输入从“原始状态”升级为“去噪后的任务状态表示”。

---

## 4. 为什么必须接在 dynamic，而不是 static

`static` 模式不做实时调整，只有固定信道/功率/速率，缺少学习驱动空间。

`dynamic` 才具备以下条件：

- 有持续输入的状态序列
- 有需要优化的控制动作
- 有明确的闭环指标（PDR/时延/吞吐量）
- 有扰动场景（Hard、黑飞、NLOS、RTK 漂移）

因此，鲁棒信息瓶颈最适合在 `dynamic` 中承担以下角色之一：

1. 状态净化器
2. 风险评分器
3. 重分配触发器
4. 动作建议器

推荐顺序：先 1，再 2，再 3，最后 4。

---

## 5. 推荐总体架构

### 5.1 设计原则

- 不推翻现有 C++ `dynamic`
- 先旁路验证，再小规模接管
- 学习模块只增强，不直接一开始完全决策
- 所有新能力都要能回退到当前规则系统

### 5.2 架构分层

#### A. 仿真执行层

继续由 NS-3 C++ 完成：

- 拓扑更新
- QoS 计算
- PHY/MAC 下发
- 轨迹与移动仿真

#### B. 鲁棒表征层

新加一个学习模块：

- 输入：原始状态特征 `x_t`
- 输出：
  - `S_t`：clean latent
  - `T_t`：noise latent
  - `c_t`：置信度 / 稳定度 / 风险分数

#### C. 决策适配层

将 `S_t` 和现有规则逻辑结合：

- 保持现有启发式逻辑不动
- 对输入做重加权
- 对动作幅度做 gating
- 对重分配触发做抑制或增强

#### D. 评估层

输出以下日志用于对比：

- 原始 state
- clean latent 驱动后的动作
- 规则动作 vs 增强动作
- QoS 改善量

---

## 6. 状态建模：输入变量定义

建议每个时刻 `t`、每个 UAV 节点 `i` 定义一个状态向量 `x_i(t)`。

### 6.1 来自当前系统的直接特征

这些特征已经在当前工程中存在或可低成本导出：

- `neighbors_i`
  - 当前邻居数
- `avg_neighbor_degree_i`
  - 邻域平均度
- `worst_sinr_i`
  - 最差链路 SINR
- `avg_sinr_i`
  - 邻居平均 SINR
- `interference_i`
  - 干扰功率
- `channel_i`
  - 当前信道
- `tx_power_i`
  - 当前发射功率
- `rate_i`
  - 当前速率
- `pdr_i`
  - 当前 PDR
- `delay_i`
  - 当前平均时延
- `throughput_i`
  - 当前吞吐量
- `speed_i`
  - 当前速度
- `pos_i = (x, y, z)`
  - 当前位置信息
- `dist_to_center_i`
  - 与编队中心或 leader 的距离
- `topology_density`
  - 当前网络密度
- `num_links`
  - 当前链路总数
- `difficulty`
  - Easy/Moderate/Hard
- `strategy`
  - 当前模式标签

### 6.2 建议新增的时间序列特征

因为鲁棒信息瓶颈要区分“持续变化”和“瞬时噪声”，建议加入时间窗口特征。

建议窗口长度：

- 短窗：最近 5 个时刻（0.5s）
- 中窗：最近 20 个时刻（2.0s）

新增统计：

- `delta_sinr_i`
- `delta_throughput_i`
- `delta_pdr_i`
- `delta_delay_i`
- `delta_neighbors_i`
- `std_sinr_i(window)`
- `std_throughput_i(window)`
- `std_delay_i(window)`
- `topology_change_rate_i`

### 6.3 噪声代理变量

虽然你们没有“标签噪声”这个分类场景，但可以构造“噪声代理信号”，用于让模型学习 `T`。

建议代理变量：

- `rtk_drift_active`
- `rtk_noise_level`
- `nlos_flag`
- `blackfly_density_local`
- `measurement_inconsistency_score`
  - 例如：吞吐量暴跌但 SINR 未变
- `topology_instability_score`
  - 例如：邻居数在短时间高频抖动
- `stale_state_score`
  - 例如：位置和链路状态不一致

这些变量不一定都直接监督训练，但都可以作为辅助输入和分析标签。

---

## 7. 将 LaT-IB 思想映射到本项目

论文里的 clean/noise 分离，在本项目中建议这样定义：

### 7.1 `S`：任务相关 clean 表示

应尽量包含：

- 稳定的链路退化趋势
- 稳定的拓扑结构变化
- 真正需要资源重分配的信号
- 与 QoS 下降强相关的状态模式

### 7.2 `T`：噪声/扰动表示

应吸收：

- RTK 漂移
- 黑飞造成的瞬时异常
- 单次测量尖峰
- 短时吞吐量跳变
- 统计窗口边缘效应
- 观测不一致性

### 7.3 训练目标映射

原论文中的目标可转译为：

1. `S` 应足够预测“真实控制目标”
   - 如未来 1~2s 的 QoS 退化风险
   - 如是否需要重分配
   - 如推荐动作方向

2. `S` 应尽量压缩
   - 不要保留原始噪声细节

3. `S` 与 `T` 应尽量解耦
   - 减少“假异常”进入主决策

4. `T` 可以解释观测中的异常扰动
   - 用于诊断，不一定直接控制

---

## 8. 模型设计建议

## 8.1 第一阶段推荐模型

不要一开始就做很重的图神经网络 + 对抗训练。

建议第一阶段用轻量模型：

- 输入：
  - 节点级特征 + 少量邻域统计
- 编码器：
  - MLP 或 GRU + MLP
- 输出：
  - `mu_s, logvar_s`
  - `mu_t, logvar_t`
  - control confidence

### 8.2 模块拆分

```text
Input Window X_t
  -> Shared Temporal Encoder
  -> Clean Encoder      -> S
  -> Noise Encoder      -> T
  -> Predictor Head     -> y_hat
  -> Trigger Head       -> trigger score
  -> Action Delta Head  -> delta power/rate/channel priority
```

### 8.3 输出定义

建议输出不是最终离散动作，而是“辅助量”：

- `risk_score_i`
  - 当前节点是否处于真实退化状态
- `realloc_score_i`
  - 是否值得重分配
- `delta_power_i`
  - 建议功率修正量
- `delta_rate_i`
  - 建议速率修正量
- `channel_preference_i[k]`
  - 各信道优先级

这样可以降低落地风险。

---

## 9. 最关键的落地策略：不要直接替代规则，先做 Hybrid

推荐 `Hybrid Dynamic`：

```text
现有规则输出 = RuleAction
鲁棒模块输出 = RobustHint
最终输出 = Fuse(RuleAction, RobustHint, Confidence)
```

建议融合方式：

### 9.1 通道分配

现有规则算出每个信道干扰分数后：

- 用 `channel_preference_i` 修正分数
- 只在高置信度下改变排序

### 9.2 功率控制

现有 `DynamicPowerControl()` 输出 `newPower`

融合方式：

```text
newPower_final = newPower_rule + alpha * confidence * delta_power_i
```

其中：

- `alpha` 初始建议 0.2~0.4
- 置信度低时不生效

### 9.3 速率调整

现有 `AdaptiveRateControl()` 依据 SINR 查表

可以让鲁棒模块先输出：

- `effective_sinr_bias`
  - 如果判定当前测量不可靠，则降低对瞬时坏值的敏感度

### 9.4 重分配触发

这是第一阶段最值得接管的点。

现有系统会周期性重分配。  
鲁棒模块可以先只做：

- 判断这次重分配是否值得执行
- 判断哪个节点是真异常，哪个是噪声抖动

这是最容易见到收益、也是最不容易把系统搞崩的位置。

---

## 10. 推荐实现路径

## Phase 0：日志增强

目标：先把训练数据采出来。

需要增加的新日志：

- 节点级状态快照
- 邻居级统计
- topology change rate
- NLOS/建筑遮挡状态
- 黑飞局部密度
- RTK 漂移状态
- 未来窗口内 QoS 变化标签

建议新文件：

- `output/.../robust_state_snapshot.csv`
- `output/.../robust_training_labels.csv`

### Phase 0 需要修改的代码位置

- [`scratch/uav_resource_allocation.cc`](/home/tzx/ns-3.43/scratch/uav_resource_allocation.cc)
  - `PerformResourceReallocation()`
  - `MonitorQoSPerformance()`
  - `LogPositions()`
  - `UpdateTopology()`

---

## Phase 1：离线数据集构建

在 Python 侧新增目录：

```text
ml/robust_dynamic_ib/
  data/
  models/
  train.py
  dataset.py
  infer.py
  losses.py
```

### 数据样本定义

每条样本：

- 输入：节点 `i` 在 `[t-k, ..., t]` 的状态序列
- 输出标签：
  - `future_qos_drop`
  - `future_realloc_benefit`
  - `future_best_action_delta`

### 标签构造方式

不是人工标注，而是从仿真结果自动生成：

- 若未来 1s 内 PDR 下降超过阈值，则 `risk=1`
- 若执行 dynamic 后 QoS 优于 static baseline，则 `benefit=1`
- 若某种信道/功率动作带来明显收益，则记录为动作标签

---

## Phase 2：离线训练鲁棒编码器

建议训练目标：

### 主任务损失

- 未来 QoS 风险预测损失
- 是否应触发重分配分类损失
- 动作增量回归损失

### IB 压缩损失

- `KL(q(S|X) || p(S))`
- `KL(q(T|X) || p(T))`

### 解耦损失

建议至少做一种：

- 正交约束
- mutual information minimization
- adversarial discriminator

### 推荐训练顺序

1. Warmup
   - 先只训练主任务头
2. Bottleneck enable
   - 打开 `S/T` 分离
3. Disentanglement
   - 增加解耦损失
4. Robust fine-tune
   - 在 Hard/黑飞/RTK 噪声数据上强化训练

---

## Phase 3：影子模式（Shadow Mode）

不要立即在线控制。

先让模型只做推理和记录：

- C++ 仍按旧 dynamic 运行
- Python 模型只读取状态并输出建议
- 记录：
  - 规则动作
  - 模型建议
  - 若按模型建议执行会不会更好

影子模式输出文件建议：

- `robust_shadow_decisions.csv`

需要记录：

- time
- node_id
- rule_action
- robust_hint
- confidence
- actual_qos
- counterfactual_score

这样可以在不影响主流程的情况下评估收益。

---

## Phase 4：有限接管（Gated Online）

当影子模式证明有效后，再在以下位置引入受限控制：

### 优先接管顺序

1. 重分配触发
2. 功率微调
3. 速率微调
4. 信道排序微调

### 不建议第一阶段就接管的内容

- 全离散信道直接决策
- 全部动作联合端到端输出
- 完全替换现有规则

### 在线接入方式推荐

推荐两种方式：

#### 方案 A：Python sidecar

- C++ 导出当前状态到共享文件/pipe
- Python 常驻进程读取后返回 hint

优点：

- 训练和推理链路统一
- 上手快

缺点：

- 在线集成复杂
- 仿真同步成本高

#### 方案 B：ONNX 导出 + C++ 推理

- Python 训练
- 导出 ONNX
- C++ 中接 ONNX Runtime

优点：

- 与 NS-3 进程同域
- 线上更稳定

缺点：

- 工程接入较重

推荐顺序：

1. 先做 A 验证
2. 确认有效后再做 B

---

## 11. 文件级实现建议

建议新增目录：

```text
project_docs/robust_dynamic_ib/
  IMPLEMENTATION_PLAN.md

ml/robust_dynamic_ib/
  README.md
  train.py
  infer.py
  dataset.py
  model.py
  losses.py
  export_onnx.py

scratch/
  robust_state_adapter.h
  robust_state_adapter.cc
```

### 11.1 `robust_state_adapter`

职责：

- 从当前 C++ 状态中提取统一特征
- 做归一化
- 调用模型推理
- 返回 hint 给 `dynamic`

建议接口：

```cpp
struct RobustHint {
    double confidence;
    double riskScore;
    double reallocScore;
    double powerDelta;
    double rateDelta;
    std::vector<double> channelPreference;
};

RobustHint InferRobustHint(uint32_t nodeId, double currentTime);
```

### 11.2 接入点

建议接在：

- `PerformResourceReallocation()`
- `DynamicChannelAllocation()`
- `AdaptiveRateControl()`
- `DynamicPowerControl()`

推荐第一版只在 `PerformResourceReallocation()` 里做：

- 提前判断是否执行本轮 dynamic 逻辑
- 或只对高风险节点执行增强

---

## 12. 第一版最小可行产品（MVP）

如果要控制复杂度，第一版建议只做这一件事：

### MVP 任务

> 预测某节点在未来 1 秒内是否会发生“真实 QoS 恶化”，并据此决定是否触发更积极的动态重分配。

### 这样做的原因

- 不需要一开始就输出全动作
- 和现有规则融合成本最低
- 最容易验证收益
- 最符合鲁棒信息瓶颈“去掉假异常”的优势

### MVP 输入

- 当前与历史 2 秒内：
  - SINR
  - 干扰
  - 邻居数
  - PDR
  - 时延
  - 吞吐量
  - 位置/速度
  - 黑飞局部密度
  - NLOS 标志

### MVP 输出

- `riskScore_i`
- `reallocScore_i`
- `confidence_i`

### MVP 融合逻辑

```text
if confidence_i > threshold and reallocScore_i > threshold:
    对节点 i 启用增强 dynamic
else:
    使用原始规则
```

---

## 13. 训练数据如何从现有仿真里得到

你们这个项目的最大优势是：仿真器本身就是数据生成器。

建议自动批量生成以下组合数据：

- formation:
  - `v_formation`
  - `line`
  - `cross`
  - `triangle`
- difficulty:
  - `Easy`
  - `Moderate`
  - `Hard`
- strategy:
  - `static`
  - `dynamic`
- 噪声参数：
  - RTK noise/drift
  - interferer count
  - interferer rate/duty
  - buildings density

每组至少保存：

- QoS 序列
- 节点状态序列
- 拓扑演化序列
- 资源分配动作序列

然后构造训练对：

```text
state_window(t) -> future_qos / future_benefit / suggested_hint
```

---

## 14. 评估指标

不能只看分类准确率，必须看系统级收益。

### 14.1 学习模块指标

- 风险预测准确率
- 重分配触发 F1
- 动作建议 MAE / rank accuracy
- `S/T` 分离可视化质量

### 14.2 系统级指标

- 平均 PDR
- P99 delay
- 平均吞吐量
- QoS 波动标准差
- 重分配次数
- 无效重分配比例
- 在 Hard 场景下的稳定性

### 14.3 必做对比

- `static`
- 现有 `dynamic`
- `dynamic + robust trigger`
- `dynamic + robust hint`

---

## 15. 风险与应对

### 风险 1：模型学到“静态平均化”，导致反应变慢

应对：

- 加入未来收益标签
- 不只做去噪，还做风险预测

### 风险 2：在线推理太重

应对：

- 先节点级推理
- 每 0.5s 推一次，不要 10Hz 全量推

### 风险 3：训练数据分布和在线场景不一致

应对：

- 多场景批量仿真
- 按 formation/difficulty 分层采样

### 风险 4：模型建议破坏现有稳定规则

应对：

- 必须有 confidence gate
- 必须允许 fallback 到 rule-only

---

## 16. 推荐开发顺序

### 第 1 周

- 增加鲁棒训练日志导出
- 定义状态特征
- 明确训练标签

### 第 2 周

- 搭建 Python 数据集与训练脚本
- 训练第一个 `risk predictor + bottleneck encoder`

### 第 3 周

- 跑影子模式
- 分析模型建议和现有规则的偏差

### 第 4 周

- 接入 gated hint
- 做 Hard 场景专项评估

---

## 17. 最终建议

对于当前仓库，最现实的路线不是“直接把 LaT-IB 整篇论文塞进 dynamic”，而是：

1. 用其核心思想定义 `clean/noise` 双表示
2. 先服务于 `dynamic` 的状态净化和重分配触发
3. 以 Hybrid 方式逐步接入现有规则系统
4. 最终形成：

```text
Rule-based Dynamic + Robust IB State Adapter
```

这是当前工程复杂度、可验证性、论文创新性三者之间最平衡的路径。

---

## 18. 下一步建议

建议按下面顺序继续推进：

1. 先实现 `Phase 0` 的日志增强
2. 新建 `ml/robust_dynamic_ib/` 训练骨架
3. 先做 MVP：风险预测 + 重分配触发增强
4. 验证有效后再接功率/速率/信道微调

如果继续执行，下一份文档建议写：

- `FEATURE_SCHEMA.md`
  - 明确每个特征从哪个 C++ 变量导出
- `TRAINING_PIPELINE.md`
  - 明确数据集、标签生成、训练命令
- `ONLINE_INTEGRATION.md`
  - 明确 C++ 与 Python/ONNX 的运行时接口
