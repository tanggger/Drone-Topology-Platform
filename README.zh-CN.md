# Fengru_Skycore_Backend

[English](README.md) | [简体中文](README.zh-CN.md)

Fengru_Skycore_Backend 是一个基于 `ns-3.43` 构建、以后端为核心的无人机集群通信仿真与对抗平台。

项目主要支持两类任务模式：

- **合作模式**：我方已知网络结构，进行通信规划、故障恢复与鲁棒性评估。
- **非合作模式**：我方只能观测敌方网络中的含噪通信痕迹，并据此推断拓扑、识别关键节点、评估打击效果。

这个仓库并不只是一个普通的 ns-3 工作区。当前自定义后端已经实现了：

- 场景感知的通信仿真
- 合作模式下的恢复与失效切换逻辑
- 非合作模式下的观测、拓扑推断、目标推荐、打击执行与效果评估
- 用于前端联调的 Flask API

---

## 目录

- [1. 项目范围](#1-项目范围)
- [2. 当前后端状态](#2-当前后端状态)
- [3. 系统架构](#3-系统架构)
- [4. 核心后端流程](#4-核心后端流程)
- [5. 场景建模](#5-场景建模)
- [6. 合作模式](#6-合作模式)
- [7. 非合作模式](#7-非合作模式)
- [8. 已实现算法](#8-已实现算法)
- [9. API 与前端集成](#9-api-与前端集成)
- [10. 输出与数据契约](#10-输出与数据契约)
- [11. 仓库结构](#11-仓库结构)
- [12. 构建与运行](#12-构建与运行)
- [13. 关键项目文档](#13-关键项目文档)

---

## 1. 项目范围

当前后端主要覆盖三条能力线。

### 1.1 场景真实性

仿真器支持四类场景：

- `urban`
- `forest`
- `lake`
- `open-field`

这些场景不只是标签不同，还会影响：

- 传播模型行为
- 路径损耗与 NLOS 惩罚
- 连通范围
- 干扰压力
- 观测难度
- 恢复阈值
- 网络层路由压力

### 1.2 合作通信

在合作模式下，系统对已知的友方集群网络进行建模，并支持：

- 通信模式切换
- 节点失效与扰动注入
- 集中式、分布式与混合式恢复
- 主节点失效切换
- 恢复动作日志
- 韧性指标与时间线输出

### 1.3 非合作推断与打击

在非合作模式下，系统支持完整链路：

1. 观测含噪敌方通信事件
2. 形成观测窗口
3. 构建边证据
4. 推断敌方拓扑
5. 识别关键节点
6. 推荐打击目标
7. 执行 `node_strike`
8. 评估打击前后全局与局部损伤

---

## 2. 当前后端状态

相对于 [`project_docs/TWO_WEEK_EXECUTION_PLAN.md`](project_docs/TWO_WEEK_EXECUTION_PLAN.md) 中的执行计划，当前后端整体上已经处于最终集成阶段。

### 已完成

- 统一的场景环境层
- 合作模式闭环
- 非合作模式观测、推断、打击、评估闭环
- 面向前端的 API 聚合
- 结构化 JSON/CSV 输出契约

### 基本完成

- 面向前端的数据契约
- 项目级文档
- 最终演示与可视化对齐

### 当前可用性

后端已经可用于：

- 前端集成
- 场景演示
- 算法对比
- 打击效果验证
- 韧性分析

---

## 3. 系统架构

自定义后端主要集中在 `scratch/uav_ra/` 和 `api_server/`。

### 3.1 主要执行层

#### `scratch/uav_ra/main.cc`

职责：

- 解析运行参数
- 选择运行模式
- 串联仿真阶段
- 启动 ns-3 主执行流程

#### `scratch/uav_ra/simulation_setup.cc`

职责：

- 构建场景配置
- 初始化无线模型与协议栈
- 应用难度与真实性参数
- 加载与地图几何相关的场景设置

#### `scratch/uav_ra/scenario_environment.cc`

职责：

- 加载并解析地图几何
- 管理轨迹与环境交互
- 应用建筑、植被、水面等场景叠加效果

#### `scratch/uav_ra/topology_control.cc`

职责：

- 拓扑更新
- 链路估计
- 合作控制
- 恢复决策逻辑
- 路由、转发与控制压力指标

#### `scratch/uav_ra/traffic_metrics.cc`

职责：

- 业务流生成与 TDMA
- QoS 指标统计
- 拓扑与传输日志记录
- 流级运行统计

#### `scratch/uav_ra/non_cooperative_inference.cc`

职责：

- 观测窗口处理
- 边证据融合
- 因果拓扑推断
- 动态边跟踪
- 假边抑制

#### `scratch/uav_ra/non_cooperative_attack.cc`

职责：

- 关键节点排序
- 目标推荐
- 观测轨迹与实体绑定
- 打击执行
- 效果评估

#### `scratch/uav_ra/output_runtime.cc`

职责：

- 初始化输出文件
- 写入结构化 JSON 与 CSV 输出
- 聚合前端所需运行指标

#### `api_server/app.py`

职责：

- 暴露 HTTP 接口
- 启动仿真任务
- 将输出聚合成适合前端消费的负载
- 提供 manifest 与结构化结果读取

### 3.2 数据流

后端的高层流程如下：

1. 前端或 CLI 提供场景与模式参数
2. 后端构建场景与无线环境
3. ns-3 执行移动、通信与日志记录
4. 运行合作或非合作专项逻辑
5. 将结构化结果写出为 JSON/CSV
6. API 将结果聚合为 `/frontend` 负载

---

## 4. 核心后端流程

### 4.1 合作模式流程

合作模式的处理链路为：

1. 初始化已知友方集群拓扑
2. 在选定通信模式下进行通信规划
3. 注入故障或扰动
4. 检测退化
5. 触发恢复
6. 执行恢复动作
7. 评估恢复完成与稳定化
8. 导出时间线与指标

支持的通信模式：

- `centralized`
- `distributed`
- `hybrid`

支持的故障类型：

- `node_failure`
- `environment_degradation`
- `external_interference`
- `link_degradation`

核心输出：

- `cooperative_mode_summary.json`
- `cooperative_failure_timeline.json`
- `cooperative_recovery_timeline.json`
- `cooperative_metrics_timeseries.json`
- `cooperative_dashboard_snapshot.json`
- `cooperative_failure_events.csv`
- `cooperative_recovery_actions.csv`
- `cooperative_recovery_metrics.csv`
- `cooperative_decision_trace.csv`

### 4.2 非合作模式流程

非合作模式的处理链路为：

1. 观测含噪敌方通信痕迹
2. 将痕迹分组为观测窗口
3. 构造边证据
4. 推断敌方通信图
5. 对关键节点进行排序
6. 推荐打击目标
7. 将观测轨迹绑定到可执行实体
8. 执行打击
9. 评估打击前后损伤

核心输出：

- `observed_signal_events.csv`
- `observed_comm_windows.csv`
- `observed_link_evidence.csv`
- `inferred_topology_edges.csv`
- `inferred_graph_nodes.csv`
- `key_node_candidates.csv`
- `noncooperative_attack_recommendations.csv`
- `noncooperative_attack_plan.json`
- `noncooperative_attack_events.csv`
- `noncooperative_target_binding.csv`
- `noncooperative_attack_effect_metrics.csv`
- `noncooperative_pre_post_comparison.json`

---

## 5. 场景建模

当前场景真实性已经不再停留在静态预设层面。

### 5.1 城市场景

已实现效果：

- 建筑物感知传播
- 高度相关通信惩罚与增益
- 街谷因子
- 基于几何统计的建筑参数

典型输出字段包括：

- `avgBuildingHeightM`
- `avgStreetWidthM`
- `urbanAltitudePenaltyDbLow`
- `urbanAltitudeGainDbHigh`
- `urbanStreetCanyonFactor`

### 5.2 森林场景

已实现效果：

- 植被衰减叠加
- 更强的观测困难度
- 与频率、带宽、极化相关的植被损耗

典型输出字段包括：

- `vegetationLossDbPerM`
- `carrierFrequencyGHz`
- `channelBandwidthMHz`
- `polarizationMode`

### 5.3 湖面场景

已实现效果：

- 基于 `TwoRayGround` 的开阔水面传播
- 水面波动性
- 深衰落概率
- 反射时延抖动

典型输出字段包括：

- `lakeVolatilityJitterDb`
- `lakeDeepFadeProbability`
- `lakeDeepFadeMaxDb`
- `lakeReflectionDelayJitterMs`

### 5.4 开阔场景

已实现效果：

- 低遮挡基线通信环境
- 更低的几何复杂度
- 便于对比的基线拓扑行为

### 5.5 网络层真实性

场景影响并不只作用于 PHY 层，后端还会跟踪：

- 路由变化
- 中继切换
- 控制时限违约
- 路由压力

这些指标用于揭示不同场景是否会显著增加重路由与协同压力。

相关真实性设计说明：

- [`project_docs/SCENE_REALISM_ENHANCEMENT_PLAN.md`](project_docs/SCENE_REALISM_ENHANCEMENT_PLAN.md)

---

## 6. 合作模式

合作模式后端围绕恢复、韧性与可解释性组织。

### 6.1 恢复逻辑

系统支持：

- 全局恢复
- 局部恢复
- 主节点失效切换
- 冻结失效邻域评估
- 网络级恢复压力指标

### 6.2 恢复评估

恢复是否完成并不由单一指标决定，而是综合考虑：

- 全局连通性
- 局部或失效邻域 QoS
- 恢复与稳定化时间

### 6.3 前端可直接展示的内容

前端可以直接渲染：

- 当前模式摘要
- 主节点与备份主节点
- 故障时间线
- 恢复动作时间线
- 连通性、PDR、吞吐、时延曲线
- 失效邻域指标
- 失效目标指标
- 路由与中继压力指标

合作模式闭环相关文档：

- [`project_docs/COOPERATIVE_MODE_CLOSURE_TASK_SHEET.md`](project_docs/COOPERATIVE_MODE_CLOSURE_TASK_SHEET.md)

---

## 7. 非合作模式

当前非合作模式后端主要由两层组成：

### 7.1 观测与推断层

这一层提供：

- 观测事件
- 观测窗口
- 边证据
- 推断边
- 推断图节点
- 关键节点候选

### 7.2 打击闭环层

这一层提供：

- 推荐记录
- 打击计划
- 绑定记录
- 打击事件
- 全局效果指标
- 目标邻域效果指标
- 打击前后对比摘要

当前打击闭环已经可用于：

- 推荐目标评估
- 手动目标执行
- 场景对比
- 打击效果分析

相关闭环文档：

- [`project_docs/NON_COOPERATIVE_ATTACK_CLOSURE_PLAN.md`](project_docs/NON_COOPERATIVE_ATTACK_CLOSURE_PLAN.md)

---

## 8. 已实现算法

这一节聚焦于理解系统逻辑所需的后端核心算法。

### 8.1 合作模式算法

合作模式采用的是规则驱动的跨层恢复控制器，而不是单一的整体优化器。

已实现逻辑包括：

- 按通信模式分支的控制逻辑
- 失效切换处理
- 冻结局部恢复范围
- 路由与中继压力跟踪
- 恢复完成与稳定化评估

它更接近一个可解释的工程控制基线，而不是学习型策略。

### 8.2 非合作推断算法

非合作侧目前已经完成两轮算法增强。

### 基线层

- 观测窗口聚合
- 边证据融合
- 加权结构型节点评分

### 第一轮增强

- 因果边评分
- 时间连续性先验
- 双阶段假边抑制
- 多特征关键节点融合
- 局部桥接与移除后损伤评分

### 第二轮增强

- 有向因果边推断
- 条件式假边抑制
- 动态边状态跟踪
- 更强的局部子图关键节点特征

已实现的评分族包括：

- `laggedPredictiveScoreForward`
- `laggedPredictiveScoreBackward`
- `directedResponseScoreForward`
- `directedResponseScoreBackward`
- `excitationScoreForward`
- `excitationScoreBackward`
- `directionalityScore`
- `dominantDirection`
- `posteriorEdgeProbability`
- `edgeDynamicState`
- `stabilityAge`
- `weakeningAge`
- `directionalInfluenceScore`
- `twoHopReachabilityScore`
- `interClusterBridgeScore`
- `localCutRiskScore`
- `neighborRedundancyPenalty`

当前推荐方法版本：

- `directed_dynamic_graph_bridge_fusion_v4`

算法设计说明：

- [`project_docs/NON_COOPERATIVE_INFERENCE_ALGORITHM_ENHANCEMENT_PLAN.md`](project_docs/NON_COOPERATIVE_INFERENCE_ALGORITHM_ENHANCEMENT_PLAN.md)

### 8.3 推荐有效性评估

仓库中还提供了一个脚本，用于评估当前推荐算法是否真正有效：

- 推荐目标
- 仅结构基线
- 随机基线
- oracle 最优候选

该评估用于判断是否还需要继续增强算法。

相关脚本：

- `tools/run_recommendation_effectiveness_evaluation.py`

---

## 9. API 与前端集成

后端已经提供面向前端的 API 层。

### 9.1 主要接口

- `GET /api/health`
- `POST /api/simulate`
- `GET /api/results/<task_id>/frontend`
- `GET /api/results/<task_id>/manifest`
- `GET /api/results/<task_id>`
- `GET /api/maps`
- `GET /api/map_data/<map_name>`
- `POST /api/upload_osm`

### 9.2 前端结果负载

主结果负载包括：

- `meta`
- `shared`
- `cooperative`
- `non_cooperative`
- `manifest`

### 9.3 前端文档

面向前端的项目文档位于：

- [`project_docs/frontend/FRONTEND_API_INTEGRATION_GUIDE.md`](project_docs/frontend/FRONTEND_API_INTEGRATION_GUIDE.md)
- [`project_docs/frontend/FRONTEND_FIELD_REFERENCE.md`](project_docs/frontend/FRONTEND_FIELD_REFERENCE.md)
- [`project_docs/frontend/FRONTEND_HANDOFF_CHECKLIST.md`](project_docs/frontend/FRONTEND_HANDOFF_CHECKLIST.md)

---

## 10. 输出与数据契约

后端同时产出原始输出与面向前端的结构化输出。

### 10.1 共享输出

- `rtk-node-positions.csv`
- `rtk-node-transmissions.csv`
- `rtk-topology-changes.txt`
- `resource_allocation.csv`
- `resource_allocation_detailed.csv`
- `qos_performance.csv`
- `topology_evolution.csv`
- `topology_detailed.csv`
- `rtk-flow-stats.csv`
- `environment_summary.json`

### 10.2 合作模式输出

- `cooperative_mode_summary.json`
- `cooperative_failure_timeline.json`
- `cooperative_recovery_timeline.json`
- `cooperative_metrics_timeseries.json`
- `cooperative_dashboard_snapshot.json`
- `cooperative_failure_events.csv`
- `cooperative_recovery_actions.csv`
- `cooperative_recovery_metrics.csv`
- `cooperative_decision_trace.csv`

### 10.3 非合作模式输出

- `observed_signal_events.csv`
- `observed_comm_windows.csv`
- `observed_link_evidence.csv`
- `inferred_topology_edges.csv`
- `inferred_graph_nodes.csv`
- `key_node_candidates.csv`
- `noncooperative_attack_recommendations.csv`
- `noncooperative_attack_plan.json`
- `noncooperative_attack_events.csv`
- `noncooperative_target_binding.csv`
- `noncooperative_attack_effect_metrics.csv`
- `noncooperative_pre_post_comparison.json`

---

## 11. 仓库结构

本项目最关键的目录如下：

```text
api_server/                 用于前端集成的 Flask API
data_map/                   场景地图与几何输入
data_rtk/                   轨迹输入
project_docs/               设计、闭环、算法与前端文档
scratch/uav_ra/             自定义 ns-3 后端实现
tools/                      验证与评估脚本
rtk/                        轨迹生成与预处理
visualization/              绘图与回放辅助工具
```

`src/`、`examples/`、`utils/` 等目录中的上游 ns-3 源码树保持原样。

---

## 12. 构建与运行

### 12.1 编译

无论你是要直接运行仿真程序，还是要启动后端 API 与前端联调，都统一使用下面两条命令完成构建：

```bash
./ns3 configure --build-profile=optimized --enable-examples --enable-tests
./ns3 build uav_resource_allocation
```

### 12.2 直接运行仿真

如果不需要前端，可以直接运行 ns-3 仿真程序。

典型入口：

```bash
./ns3 run "uav_resource_allocation --help"
```

示例：

```bash
./ns3 run "uav_resource_allocation --strategy=graph_coloring --numUAVs=15 --numChannels=3 --duration=200 --outputDir=output/manual_run"
```

本项目直接运行的仿真入口为 `uav_resource_allocation`。

### 12.3 启动后端 API

如果需要前端联调或通过 HTTP 提交任务，请在编译完成后启动 Flask API：

```bash
python3 api_server/app.py
```

本地访问地址：

```text
http://127.0.0.1:5000
```

服务默认绑定在 `0.0.0.0:5000`。

### 12.4 运行验证脚本

完整功能验证：

```bash
python3 tools/run_current_feature_validation.py
```

推荐有效性评估：

```bash
python3 tools/run_recommendation_effectiveness_evaluation.py
```

---

## 13. 关键项目文档

执行与闭环文档：

- [`project_docs/TWO_WEEK_EXECUTION_PLAN.md`](project_docs/TWO_WEEK_EXECUTION_PLAN.md)
- [`project_docs/COOPERATIVE_MODE_CLOSURE_TASK_SHEET.md`](project_docs/COOPERATIVE_MODE_CLOSURE_TASK_SHEET.md)
- [`project_docs/NON_COOPERATIVE_ATTACK_CLOSURE_PLAN.md`](project_docs/NON_COOPERATIVE_ATTACK_CLOSURE_PLAN.md)
- [`project_docs/SCENE_REALISM_ENHANCEMENT_PLAN.md`](project_docs/SCENE_REALISM_ENHANCEMENT_PLAN.md)
- [`project_docs/NON_COOPERATIVE_INFERENCE_ALGORITHM_ENHANCEMENT_PLAN.md`](project_docs/NON_COOPERATIVE_INFERENCE_ALGORITHM_ENHANCEMENT_PLAN.md)

前端集成文档：

- [`project_docs/frontend/FRONTEND_API_INTEGRATION_GUIDE.md`](project_docs/frontend/FRONTEND_API_INTEGRATION_GUIDE.md)
- [`project_docs/frontend/FRONTEND_FIELD_REFERENCE.md`](project_docs/frontend/FRONTEND_FIELD_REFERENCE.md)
- [`project_docs/frontend/FRONTEND_HANDOFF_CHECKLIST.md`](project_docs/frontend/FRONTEND_HANDOFF_CHECKLIST.md)

---

## 最后说明

这份 README 的目标，是让前端同学和项目协作者能快速理解整个后端逻辑。

如果你需要：

- 精确的 API 请求与响应格式，请看前端 API 指南
- 字段级语义说明，请看前端字段参考
- 页面与组件的集成顺序，请看前端交接清单

当前后端已经处于可以直接开展前端集成的状态。
