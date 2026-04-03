# DG-Mamba: Robust and Efficient Dynamic Graph Structure Learning with Selective State Space Models

## 基本信息

- 原始文件：[34382-Article Text-38449-1-2-20250410.pdf](/home/tzx/FengruCup/ns-3.43/project_docs/34382-Article%20Text-38449-1-2-20250410.pdf)
- 题目：DG-Mamba: Robust and Efficient Dynamic Graph Structure Learning with Selective State Space Models
- 作者：Haonan Yuan, Qingyun Sun, Zhaonan Wang, Xingcheng Fu, Cheng Ji, Yongjian Wang, Bo Jin, Jianxin Li
- 会议：AAAI 2025

## 论文核心问题

动态图在真实场景中往往存在：

- 结构缺失
- 结构噪声
- 时序依赖复杂
- 传统动态图结构学习复杂度高

这篇论文关注的是：如何高效、鲁棒地学习动态图结构。

## 核心思想

论文提出 `DG-Mamba`，核心是将动态图结构学习与 `Selective State Space Model` 结合起来。

从公开摘要可以提炼出的重点是：

- 动态图存在时空耦合演化模式
- 传统方法常带来二次复杂度
- 过度依赖启发式先验
- 需要更高效地学习动态结构并捕捉长时依赖

因此，这篇工作强调：

- 用更高效的动态消息传递处理动态图
- 用状态空间模型学习跨时间依赖
- 增强鲁棒性

## 对你们当前非合作拓扑推理的价值

这篇是三篇里和你们当前后端最接近的一篇。

你们现在的动态处理主要在：
[non_cooperative_inference.cc](/home/tzx/FengruCup/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc#L286)

当前做法主要是：

- 当前窗口证据和上一窗口后验做平滑
- 根据阈值判断 `stable / emerging / weakening / vanished`
- 对消失边做衰减延续

这套逻辑能工作，但本质上还是：

- 滑窗
- 经验系数
- 状态规则判别

DG-Mamba 对你们最大的启发是：

可以把“边如何随时间演化”交给时序结构学习，而不是只靠固定规则。

## 如果放进当前项目，推荐的实现方式

### 第一阶段：轻量时序特征版

先不引入 Mamba 本体，只做一个时序学习实验：

- 取最近 K 个窗口的边特征
- 输入包括：
  - `edgeProbability`
  - `laggedPredictiveScore`
  - `directedResponseScore`
  - `excitationScore`
  - `observerAgreement`
  - `edgeObservationConfidence`
  - `dynamicState`
- 输出：
  - 下一窗口该边是否存在
  - 下一窗口边概率

这一步可先用简单时序模型做基线。

### 第二阶段：动态图快照学习版

将每个时间窗口的推理图看成动态图快照序列，学习：

- 哪些边是持续结构
- 哪些边是暂时噪声
- 哪些边代表真实拓扑演化

这一阶段对应替代你们当前的后验平滑和状态迁移规则。

### 第三阶段：完整动态图结构学习版

如果前两步效果明确，再尝试：

- 构建动态图结构学习模块
- 学习多窗口图表示
- 用更强的时序模型替代现有的手工动态规则

## 最适合改造的代码位置

- 动态后验融合：
  [non_cooperative_inference.cc](/home/tzx/FengruCup/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc#L286)
- 动态状态判断：
  [non_cooperative_inference.cc](/home/tzx/FengruCup/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc#L318)
- 消失边衰减与延续：
  [non_cooperative_inference.cc](/home/tzx/FengruCup/ns-3.43/scratch/uav_ra/non_cooperative_inference.cc#L883)

## 实现难度

- 算法理解难度：中等偏高
- 工程接入难度：高
- 与现有代码兼容性：中等

难点主要在：

- 动态图样本构造
- 多窗口特征组织
- 训练流程
- 在线推理复杂度
- 与现有 C++ 推理链路集成

## 总体判断

如果目标是提升你们当前的“动态拓扑跟踪能力”，这篇最值得深入研究。

它适合解决的问题是：

- 当前滑窗平滑过于经验化
- 动态边演化建模较弱
- 对突发变化、缺失观测和噪声的适应能力有限

## 备注

当前环境无法稳定完成 PDF 全文版式转换，因此本 Markdown 文件整理的是：

- 论文主题
- 核心技术方向
- 对现有后端的可落地映射
- 建议的实现阶段与工程难度
