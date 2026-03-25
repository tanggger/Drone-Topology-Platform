# 场景真实性增强实现总结

## 1. 文档目的

本文档不再描述“准备怎么改”，而是总结当前场景真实性增强已经完成的实现内容，说明：

- 四类场景现在如何影响无人机集群通信
- 本轮新增的真实性增强项分别是怎么实现的
- 这些增强当前已经接到哪些代码模块、输出字段和前端接口
- 目前还剩哪些边界问题

主要对应代码：

- 场景参数与传播模型：
  [simulation_setup.cc](/home/tzx/ns-3.43/scratch/uav_ra/simulation_setup.cc)
- 地图几何、轨迹与遮挡：
  [scenario_environment.cc](/home/tzx/ns-3.43/scratch/uav_ra/scenario_environment.cc)
- 拓扑连通与控制层链路估计：
  [topology_control.cc](/home/tzx/ns-3.43/scratch/uav_ra/topology_control.cc)
- 全局结构与环境摘要：
  [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
- 输出文件：
  [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)
- 参数入口：
  [main.cc](/home/tzx/ns-3.43/scratch/uav_ra/main.cc)
- API 透传：
  [app.py](/home/tzx/ns-3.43/api_server/app.py)
- 验证矩阵：
  [run_current_feature_validation.py](/home/tzx/ns-3.43/tools/run_current_feature_validation.py)

## 2. 当前总体结论

当前四类场景已经不只是通过固定 `path loss` 参数做粗略区分，而是通过以下路径共同影响仿真：

- 传播模型切换
- 场景特定附加损耗层
- 连通范围与链路估计修正
- 非合作观测难度修正
- 合作恢复和网络层压力指标
- 场景几何摘要与环境输出

当前已经完成的四项真实性增强是：

1. `lake` 水面波动与深衰落增强
2. `urban` 高度自适应与街谷修正
3. `forest` 频段/带宽/极化绑定
4. 场景到网络层的显式压力模型

## 3. 四类场景当前如何影响通信

### 3.1 urban

当前 `urban` 通过以下机制影响通信：

- `HybridBuildingsPropagationLossModel`
- 建筑几何真实加载
- 更高的阴影衰落和 NLOS 惩罚
- 更小的连通范围因子
- 高度自适应附加损耗层

主要效果：

- 低空更容易受楼宇遮挡和街谷效应影响
- 接近或高于平均楼高时，链路条件会改善
- 合作模式下网络重构压力也更高

### 3.2 forest

当前 `forest` 通过以下机制影响通信：

- `LogDistance` 基础传播
- `ForestOverlayPropagationLossModel`
- 基于森林 polygon 穿越深度累计损耗
- 植被损耗率与频段、带宽、极化绑定
- 非合作观测中的森林遮挡与缺失判定

主要效果：

- 链路更像“持续性衰减”而不是楼宇式硬切断
- 不同无线参数下，森林退化程度会不同
- 非合作观测更容易受到遮挡与随机缺失影响

### 3.3 lake

当前 `lake` 通过以下机制影响通信：

- `TwoRayGroundPropagationLossModel`
- 水面波动附加损耗层
- 深衰落概率与最大衰落深度
- 反射引起的附加时延抖动
- 控制层复用同口径的水面链路修正

主要效果：

- 平均 LOS 和平均连通性通常优于 `urban`
- 但链路波动、局部深衰落和时延抖动更强
- 不再只是“更容易通信”的乐观场景

### 3.4 open-field

当前 `open-field` 保持为对照基线：

- `RMa-like LogDistance baseline`
- 中性阴影、NLOS、干扰和连通范围参数
- 没有建筑切断
- 没有森林附加穿透损耗
- 没有湖面波动增强

主要作用：

- 作为低遮挡、低复杂度参考场景
- 用来对比其他场景的真实性增强效果

## 4. 本轮已实现的真实性增强

### 4.1 lake：水面波动与深衰落增强

已实现内容：

- 在 `TwoRayGround` 后叠加了 `WaterSurfaceOverlayPropagationLossModel`
- 新增场景参数：
  - `lakeVolatilityJitterDb`
  - `lakeDeepFadeProbability`
  - `lakeDeepFadeMaxDb`
  - `lakeReflectionDelayJitterMs`
- 控制层 `CalculatePathLoss(const Vector&, const Vector&)` 已复用同口径修正
- 输出摘要中已写入上述参数和增强后的模型说明

主要代码位置：

- [simulation_setup.cc](/home/tzx/ns-3.43/scratch/uav_ra/simulation_setup.cc)
- [topology_control.cc](/home/tzx/ns-3.43/scratch/uav_ra/topology_control.cc)
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

当前效果：

- `lake` 不再只是均值更好
- 已开始体现局部波动更强、深衰落更频繁的链路行为

### 4.2 urban：高度自适应与街谷修正

已实现内容：

- 新增 `UrbanAltitudeAdaptivePropagationLossModel`
- 基于平均楼高和相对飞行高度修正附加损耗
- 结合街谷影响因子修正链路条件
- 控制层和拓扑连通判定使用同一套高度修正规则

新增场景参数：

- `urbanAltitudePenaltyDbLow`
- `urbanAltitudeGainDbHigh`
- `urbanStreetCanyonFactor`

主要代码位置：

- [simulation_setup.cc](/home/tzx/ns-3.43/scratch/uav_ra/simulation_setup.cc)
- [topology_control.cc](/home/tzx/ns-3.43/scratch/uav_ra/topology_control.cc)
- [scenario_environment.cc](/home/tzx/ns-3.43/scratch/uav_ra/scenario_environment.cc)

额外修复：

- 已修复 `urban + HybridBuildingsPropagationLossModel` 下轨迹存在负高度样本导致的 `z < 0` 断言
- 现在轨迹加载与移动入口会对建筑模型场景做最小正高度钳制

### 4.3 forest：频段/带宽/极化绑定

已实现内容：

- 将森林附加损耗从固定值改成函数输出
- 当前损耗率已绑定：
  - `carrierFrequencyGHz`
  - `channelBandwidthMHz`
  - `polarizationMode`
- 非合作观测输出中的中心频率也与环境配置保持一致

新增场景参数：

- `carrierFrequencyGHz`
- `channelBandwidthMHz`
- `polarizationMode`

主要代码位置：

- [simulation_setup.cc](/home/tzx/ns-3.43/scratch/uav_ra/simulation_setup.cc)
- [traffic_metrics.cc](/home/tzx/ns-3.43/scratch/uav_ra/traffic_metrics.cc)
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

### 4.4 场景到网络层的显式压力模型

已实现内容：

- 新增场景级网络层压力参数：
  - `reroutePressureFactor`
  - `controlMessageUrgencyFactor`
  - `relayInstabilityFactor`
  - `formationReconfigPenalty`
- 在合作模式运行时新增并输出：
  - `routeChangeCount`
  - `relaySwitchCount`
  - `controlDeadlineMissCount`
  - `routePressureScore`
- 已把这些指标接入：
  - 恢复触发条件
  - 稳定性判定
  - 仪表盘快照
  - 时间序列输出

主要代码位置：

- [context.h](/home/tzx/ns-3.43/scratch/uav_ra/context.h)
- [topology_control.cc](/home/tzx/ns-3.43/scratch/uav_ra/topology_control.cc)
- [output_runtime.cc](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

## 5. 参数入口与前端对接状态

这批增强参数现在已经是正式入口，不再只是内部常量。

### 5.1 命令行入口

已在 [main.cc](/home/tzx/ns-3.43/scratch/uav_ra/main.cc) 支持：

- `--urbanAltitudePenaltyDbLow`
- `--urbanAltitudeGainDbHigh`
- `--urbanStreetCanyonFactor`
- `--lakeVolatilityJitterDb`
- `--lakeDeepFadeProbability`
- `--lakeDeepFadeMaxDb`
- `--lakeReflectionDelayJitterMs`
- `--carrierFrequencyGHz`
- `--channelBandwidthMHz`
- `--polarizationMode`
- `--reroutePressureFactor`
- `--controlMessageUrgencyFactor`
- `--relayInstabilityFactor`
- `--formationReconfigPenalty`

### 5.2 API 入口

已在 [app.py](/home/tzx/ns-3.43/api_server/app.py) 支持前端请求透传上述全部字段。

### 5.3 前端文档

已在 [FRONTEND_API_INTEGRATION_GUIDE.md](/home/tzx/ns-3.43/FRONTEND_API_INTEGRATION_GUIDE.md) 写入对应字段说明与请求示例。

## 6. 输出与验证状态

### 6.1 环境摘要输出

`environment_summary.json` 已写入本轮新增参数，可用于：

- 前端场景信息展示
- 验证脚本汇总
- 结果可追溯性检查

### 6.2 验证脚本

验证脚本 [run_current_feature_validation.py](/home/tzx/ns-3.43/tools/run_current_feature_validation.py) 已接入这批参数，并新增了 4 组专项验证：

- `realism_urban_altitude_profile`
- `realism_forest_radio_profile`
- `realism_lake_volatility_profile`
- `realism_open_field_pressure_profile`

同时新增专项图：

- `plots/scene_realism_profiles.png`

## 7. 当前仍然存在的边界

这部分已经完成第一版实现，但仍有边界，不应表述为“最终版高保真模型”。

当前主要剩余项：

- `urban`
  - 仍未做到基于局部建筑簇的更细粒度高度自适应
- `forest`
  - 还没有扩展到更多无线制式和更复杂极化实验
- `lake`
  - 还没有单独输出 RSSI/SINR 抖动时间序列
  - 深衰落模式仍是轻量模型，不是更复杂的角度细分模型
- 网络层压力
  - 目前主要接在合作模式
  - 非合作仍主要体现链路后果和打击效果，尚未接同等级显式压力链

## 8. 当前可接受的对外表述

当前可以这样表述：

- `urban` 已体现建筑遮挡、NLOS 惩罚、连通范围缩小以及高度自适应修正
- `forest` 已体现植被附加损耗、观测遮挡以及频段/带宽/极化绑定
- `lake` 已体现更高 LOS、反射敏感传播以及水面波动/深衰落增强
- `open-field` 保持为低遮挡基线场景
- 合作模式已额外体现网络层显式重构压力

同时应保留一句边界说明：

- 当前场景真实性增强已完成第一版工程实现，具备可运行、可验证、可展示能力，但仍保留进一步精细化空间
