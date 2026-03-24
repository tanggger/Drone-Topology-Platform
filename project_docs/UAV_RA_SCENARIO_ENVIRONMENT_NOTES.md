# UAV_RA 场景环境建模整理文档

## 1. 文档目的

这份文档先不改 `context.h`，只整理当前项目在场景环境层上的设计结论、推荐参数表、以及这些参数背后的文献和数据依据，供后续逐项核查。

本文档回答四个问题：

- `sceneType`、`mapFile`、`difficulty` 三者应该如何分工。
- `difficulty` 是否应该和具体场景一一绑定。
- 哪些环境量应该由地图几何直接计算，哪些应该由文献参数标定。
- 第一版 `urban / forest / lake / open-field` 参数表可以先如何收敛。

## 1.1 本文档已确认的实现决策

下面这些点已经在讨论中确认，后续代码设计默认以此为准：

- 第一阶段四个场景都要实现:
  - `urban`
  - `forest`
  - `lake`
  - `open-field`
- 第一阶段必须支持真实地图导入，且前端页面需要显示导入后的场景
- 第一版统一地图交换格式定为 `GeoJSON`
- 第一阶段不做“统一 GeoJSON 感知传播模型”，而是按场景分开实现，场景之间允许割裂
- 第一阶段采用 `B` 口径:
  - `urban` 必须真实地图驱动
  - `forest / lake / open-field` 先支持地图显示，但传播模型可以主要依赖场景参数
- 环境影响采用单一生效路径:
  - 空间传播影响进入 `ns-3 PHY`
  - `UpdateTopology / EstimateSINR / EstimateLinkQuality` 只做控制层估计和解释修正
  - 前端只展示环境摘要，不再额外二次改链路值
- `difficulty` 对外可以保留，但内部必须拆分
- 内部第一阶段按 4 类 preset 拆分:
  - `environmentPreset`
  - `interferencePreset`
  - `observationPreset`
  - `trafficPlatformPreset`
- 当前引用的 A2G 文献只作为“低空环境先验”和“第一阶段工程初始化依据”，不视为蜂群 U2U/A2A 链路的直接真值
- 前端主输出字段中不再使用 `effectivePathLossExponent`

## 2. 当前代码基线

当前 `scratch/uav_ra/` 里的入口已经具备了“场景环境层最小闭环”的雏形，但环境定义仍然分散。

### 2.1 当前入口位置

- 主入口: [`scratch/uav_ra/main.cc`](/home/tzx/ns-3.43/scratch/uav_ra/main.cc)
- 场景装配: [`scratch/uav_ra/simulation_setup.cc`](/home/tzx/ns-3.43/scratch/uav_ra/simulation_setup.cc)
- 干扰节点与环境生成: [`scratch/uav_ra/scenario_environment.cc`](/home/tzx/ns-3.43/scratch/uav_ra/scenario_environment.cc)
- 拓扑判定与链路质量: [`scratch/uav_ra/topology_control.cc`](/home/tzx/ns-3.43/scratch/uav_ra/topology_control.cc)
- 运行输出: [`scratch/uav_ra/output_runtime.cc`](/home/tzx/ns-3.43/scratch/uav_ra/output_runtime.cc)

### 2.2 当前代码里已经存在的环境相关逻辑

在 [`scratch/uav_ra/main.cc`](/home/tzx/ns-3.43/scratch/uav_ra/main.cc) 里目前已暴露：

- `difficulty`
- `mapFile`

在 [`scratch/uav_ra/simulation_setup.cc`](/home/tzx/ns-3.43/scratch/uav_ra/simulation_setup.cc) 里目前：

- `ApplyDifficultyProfile()` 会修改：
  - `pathLossExp`
  - `rxSensitivity`
  - `txPower`
  - RTK 噪声和漂移
  - 干扰节点数量与占空比
  - `Nakagami-m`
  - MAC 重传次数
  - `noiseFigure`
  - `trafficLoadMbps`
- 如果 `mapFile` 非空，则使用 `HybridBuildingsPropagationLossModel`
- 如果 `mapFile` 为空，则使用 `LogDistancePropagationLossModel`
- 如果 `nakagamiM > 0`，再叠加 `NakagamiPropagationLossModel`
- `LoadBuildingsFromMap()` 已经会从地图文件加载建筑 box，并更新场景边界

### 2.3 当前基线的主要问题

当前 `difficulty` 事实上混合了 4 类不同概念：

- 传播环境强弱
- 干扰压力
- 观测噪声压力
- 业务负载压力

这导致一个问题：`difficulty` 现在既像“场景类型”，又像“实验压测档位”，又像“系统条件预设”，职责不清。

### 2.4 当前代码里的 `difficulty` 实际参数表

下面这张表直接来自 [`scratch/uav_ra/simulation_setup.cc`](/home/tzx/ns-3.43/scratch/uav_ra/simulation_setup.cc) 的 `ApplyDifficultyProfile()`，用于说明当前实现到底在改什么。

| difficulty | pathLossExp | rxSensitivity(dBm) | txPower(dBm) | rtkNoiseStdDev(m) | rtkDriftInterval(s) | rtkDriftDuration(s) | rtkDriftMagnitude(m) | enableInterference | numInterferenceNodes | nakagamiM | macMaxRetries | noiseFigure(dB) | trafficLoadMbps | interferenceRateMbps | interferenceDutyCycle |
|---|---:|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|---:|---:|---:|---:|
| `Easy` | `2.0` | `-90.0` | `23.0` | `0.0` | `0.0` | `0.0` | `0.0` | `false` | `0` | `0.0` | `7` | `7.0` | `0.2` | `0.5` | `0.1` |
| `Moderate` | `2.5` | `-85.0` | `23.0` | `0.08` | `15.0` | `4.0` | `0.5` | `true` | `8` | `0.7` | `1` | `15.0` | `2.8` | `4.0` | `0.7` |
| `Hard` | `3.0` | `-82.0` | `26.0` | `0.2` | `8.0` | `6.0` | `1.0` | `true` | `15` | `0.2` | `0` | `20.0` | `7.0` | `6.0` | `0.95` |

这张表能直观看出：当前 `difficulty` 同时控制了传播、感知、干扰、MAC 和业务五类参数，所以后续必须拆分职责。

## 3. 设计结论

### 3.1 `sceneType`、`mapFile`、`difficulty` 不应绑定成一层

推荐关系：

- `sceneType`: 回答“这是什么地方”
- `mapFile`: 回答“这个地方具体长什么样”
- `difficulty`: 回答“当前条件有多苛刻”

更准确地说：

`finalEnvironment = sceneBase(sceneType) + geometryAdjustment(mapFile) + conditionPreset(difficulty)`

### 3.2 不建议做成“一种难度对应一种场景”

不推荐：

- `Easy = open-field`
- `Hard = urban`

推荐：

- 每一种 `sceneType` 都可以有 `Easy / Moderate / Hard / Custom`

原因：

- 现实里不存在一个叫 `difficulty` 的物理量。
- 现实里真正变化的是遮挡、反射、植被厚度、干扰源强度、负载、噪声等具体因素。
- 因此 `difficulty` 更像“实验条件预设”或“压测档位”，而不是环境本体。

### 3.3 `difficulty` 仍然有工程意义，但建议降级使用

如果目标是：

- 真实场景复现

那么 `difficulty` 不应成为核心输入。

如果目标是：

- 快速演示
- 算法对比
- 压测鲁棒性
- 前端快速切换

那么 `difficulty` 仍然有价值，但更适合被解释为：

- `conditionPreset`
- `environmentPreset`
- `stressLevel`

本文档后续仍沿用 `difficulty` 这个名字，只是语义上视为“条件预设”。

## 4. 哪些量应由地图几何计算，哪些量应由文献标定

### 4.1 适合由 `mapFile` 或场景几何直接计算的量

- LoS / NLoS 是否成立
- 传播路径是否穿越建筑
- 穿越建筑次数
- 穿越植被长度
- 建筑密度
- 平均建筑高度
- 街道宽度
- 局部障碍高度
- 水域覆盖比例
- 岸线附近高度关系
- 局部连通距离修正的几何项

### 4.2 不适合只靠几何直接算死的量

- `shadowSigmaDb`
- `nlosPenaltyDb`
- `interferenceFactor`
- 小尺度衰落强度
- 多径随机波动强度
- 植被散射随机性
- 反射导致的统计抖动

这些量更适合：

- 用标准模型给基线
- 用实测文献给量级
- 再由地图几何做修正

### 4.3 结论

应采用“两层半”模型：

- 第一层: `sceneType` 提供基础参数
- 第二层: `mapFile` 提供几何修正
- 第三层: `difficulty` 提供实验条件预设修正

### 4.4 第一阶段必须遵守的“单一生效路径”

这是后续实现时最重要的约束，用来避免双重计数。

#### A. 进入 `ns-3 PHY` 的量

这些量只允许在物理层作为主生效位置出现：

- 建筑遮挡
- 建筑 LoS / NLoS
- 基础路损模型
- 森林附加衰减
- 水面反射敏感修正
- 小尺度衰落
- 接收功率主链路计算

第一阶段对应原则：

- `urban`: 建筑相关影响主要进入 `HybridBuildingsPropagationLossModel` 或其扩展路径
- `forest`: 植被衰减主要作为 PHY 侧附加损耗
- `lake`: 水面反射主要作为 PHY 侧反射敏感修正
- `open-field`: 开阔区基线传播主要进入 PHY

#### B. 只进入控制 / 估计层的量

这些量不再重复扣减接收功率，只用于控制策略或解释：

- `connectivityRangeFactor`
- 拓扑稳定性评分
- 恢复优先级
- 链路风险评分
- 控制层保守裕量
- 调度偏置

第一阶段对应原则：

- `UpdateTopology()` 不再对已经由 PHY 生效的建筑/森林/水面损耗再次做主惩罚
- `EstimateSINR()` 若继续保留自定义估计逻辑，只能读取 PHY 结果或做控制层近似，不允许对同一环境项再次完整扣减

#### C. 只进入输出 / 前端解释层的量

- `sceneType`
- `environmentSource`
- `effectiveModelSummary`
- `environmentContributionSummary`
- 地图图层启用状态
- 当前场景使用的传播模型名称

这些字段只用于展示，不再改变链路结果。

### 4.5 双重计数的禁区

后续实现时必须避免下面这些情况：

- 建筑已经由 `HybridBuildingsPropagationLossModel` 计算一次，控制层又按 `nlosPenaltyDb` 再扣一次
- 森林已经在 PHY 中按穿越长度累计衰减，控制层又把 `connectivityRangeFactor` 当成第二次路径损耗
- 湖面已经在 PHY 中引入 reflection-sensitive 修正，前端或控制层又再叠加一次“湖面链路较差”惩罚

一句话约束：

同一种环境影响只能有一个“主生效层”，其余层只能读结果、做解释或做调度偏置。

## 5. 支撑资料与原始来源

以下资料按“标准基线 > 实测论文 > 数据集”排序。

### 5.0 使用这些文献时的理论定位

当前系统的核心通信关系更接近低空无人机蜂群 `U2U / A2A mesh`，而本文档引用的部分资料是 `A2G` 或空地混合场景。

因此本文档明确采用下面的口径：

- 这些 `A2G` 文献不作为本项目蜂群 `U2U / A2A` 链路的直接真值
- 它们只作为“低空环境先验”和“第一阶段工程初始化依据”
- 第一阶段先用它们标定环境强弱的量级
- 后续若要提高理论严格性，应补充低空 `U2U / A2A` 文献或实测数据做再校准

这条说明对 `urban / forest / lake / open-field` 都成立。

### 5.1 城市 / 建筑类基线

1. 3GPP TR 38.901

- 用途:
  - `UMa / UMi / RMa` 路损基线
  - LOS probability
  - shadow fading 标准差
  - 显式地把城市、街谷、乡村场景分开
- 关键数据:
  - `RMa`: 阴影衰落标准差约 `4 dB (LOS)`、`8 dB (NLOS)`
  - `UMa`: 阴影衰落标准差约 `4 dB (LOS)`、`6 dB (NLOS)`
  - `UMi Street Canyon`: 阴影衰落标准差约 `4 dB (LOS)`、`7.82 dB (NLOS)`
- 说明:
  - 这些值适合作为 `urban` 与 `open-field / rural` 的 shadow fading 基线
  - 如果后续建筑地图存在，LoS/NLoS 应优先由几何判定，而不是只靠概率
- 链接:
  - <https://www.etsi.org/deliver/etsi_tr/138900_138999/138901/17.00.00_60/tr_138901v170000p.pdf>

2. ITU-R M.2135

- 用途:
  - 没有完整 GIS 建筑数据时，给城市结构默认值
- 关键数据:
  - `Dense urban`: 平均建筑高度 `30 m`，道路宽度 `20 m`
  - `Urban`: 平均建筑高度 `20 m`，道路宽度 `20 m`
  - `Suburban`: 平均建筑高度 `10 m`，道路宽度 `20 m`
  - `Residential`: 平均建筑高度 `8 m`，道路宽度 `20 m`
  - `Rural`: 平均建筑高度 `5 m`，道路宽度 `20 m`
- 说明:
  - 这些值很适合在 `mapFile` 不存在时，作为城市或开阔区默认几何统计量
- 链接:
  - <https://www.itu.int/dms_pub/itu-r/opb/rep/r-rep-m.2135-2008-pdf-e.pdf>

3. Al-Hourani et al., GLOBECOM 2014

- 用途:
  - 城市 A2G 环境的概率 LoS/NLoS 经验参数
  - 可用于 `urban` 的 `nlosPenaltyDb`、`losBaseProb` 的经验标定
- 关键数据:
  - 文中采用的典型环境参数表:
    - `Suburban`: `alpha = 0.1`, `beta = 750`, `gamma = 8`
    - `Urban`: `alpha = 0.3`, `beta = 500`, `gamma = 15`
    - `Dense Urban`: `alpha = 0.5`, `beta = 300`, `gamma = 20`
    - `High-rise Urban`: `alpha = 0.5`, `beta = 300`, `gamma = 50`
- 说明:
  - 这组值本质是几何统计环境参数，不是最终代码里必须照抄的字段。
  - 它适合支撑“城市遮挡强于普通 urban，dense urban / high-rise urban 更强”的层级设计。
- DOI:
  - <https://doi.org/10.1109/GLOCOM.2014.7037248>
- 可检索到表格摘要的页面:
  - <https://www.researchgate.net/profile/Akram-Al-Hourani/publication/263582265_Modeling_Air-to-Ground_Path_Loss_for_Low_Altitude_Platforms_in_Urban_Environments/links/53e355be0cf2187dccf38884/Modeling-Air-to-Ground-Path-Loss-for-Low-Altitude-Platforms-in-Urban-Environments.pdf>

### 5.2 森林 / 植被类

4. ITU-R P.833-10

- 用途:
  - 植被衰减模型
  - 为 `forest` 场景的 `vegetationLossDbPerM` 提供直接依据
- 关键数据:
  - 混合林示例中，特定衰减 `gamma` 约为:
    - `466.475 MHz`: `0.12 dB/m`
    - `949.0 MHz`: `0.17 dB/m`
    - `1852.2 MHz`: `0.30 dB/m`
    - `2117.5 MHz`: `0.34 dB/m`
  - 最大衰减 `A_m` 对应约为:
    - `18.0 dB`
    - `26.5 dB`
    - `29.0 dB`
    - `34.1 dB`
- 说明:
  - 这些值说明：森林不是简单“半径缩短”，而是更适合做“按穿越植被长度累计衰减”。
  - 在 2 GHz 左右，`0.20 - 0.35 dB/m` 作为工程初值是合理的。
- 链接:
  - <https://www.itu.int/dms_pubrec/itu-r/rec/p/R-REC-P.833-10-202109-I!!PDF-E.pdf>

5. Leite et al., Sensors 2022

- 用途:
  - 森林 / 湖面实测 A2G 参数
  - 支撑 `forest` 和 `lake` 的 path loss 与 shadowing 初值
- 频段与场景:
  - `915 MHz`
  - `Lake`
  - `Mixed region`
  - `Caatinga` 植被区
- 关键数据:
  - 路损指数:
    - `Lake`, `80 m`, `1 km/h`: `2.9`
    - `Lake`, `80 m`, `3 km/h`: `2.0`
    - `Mixed region`, `80 m`, `1 km/h`: `3.7`
    - `Mixed region`, `80 m`, `3 km/h`: `3.8`
    - `Caatinga`, `80 m`, `1 km/h`: `3.7`
    - `Caatinga`, `80 m`, `3 km/h`: `1.9`
  - 阴影标准差:
    - `Lake`, `80 m`: `1.5940 dB` 和 `1.3563 dB`
    - `Mixed region`, `80 m`: `1.8036 dB` 和 `1.4659 dB`
    - `Caatinga`, `80 m`: `2.6579 dB` 和 `3.0010 dB`
  - 低空 `8 m` 的湖面场景出现了异常负指数，说明近地面强反射和 Fresnel 区效应会显著扰动简单单斜率模型
- 说明:
  - 这篇文章很适合支撑两个结论:
    - 湖面 / 水面场景不应简单等同于普通 open-field
    - 植被场景的 path loss 和 shadowing 明显高于湖面
- 论文:
  - <https://doi.org/10.3390/s22010065>
- 可直接查看表格摘要的全文页面:
  - <https://pmc.ncbi.nlm.nih.gov/articles/PMC8747279/>

### 5.3 湖面 / 水面 / 开阔场

6. Chiu et al., WOCC 2021

- 用途:
  - 支撑 `lake / open-water` 应采用反射敏感模型
- 关键结论:
  - 对短距离 UAV A2G 通信，`two-ray ground-reflection model` 比 `single-ray model` 更适合
- 说明:
  - 该文非常适合支撑代码里单独设一个 `reflectionAware` 或 `twoRayPreferred` 开关
- DOI:
  - <https://doi.org/10.1109/WOCC53213.2021.9603250>
- 可检索摘要页面:
  - <https://colab.ws/articles/10.1109%2FWOCC53213.2021.9603250>

7. Li et al., Data in Brief 2025

- 用途:
  - 提供 `sports field / farmland / over-water` 三类开放测量数据
  - 适合作为后续校准 `open-field` 与 `lake` 的数据集来源
- 关键结论:
  - 数据集覆盖 `sports field`、`farmland`、`over-water`
  - 重点体现了不同高度下的传播时延和衰减特征
  - `over-water` 场景能直接支撑你后续做“高度相关的反射敏感链路质量”
- 说明:
  - 这篇更偏数据资源，不建议直接从摘要硬提一个最终常数写死；更适合做第二阶段校准数据源
- 论文:
  - <https://doi.org/10.1016/j.dib.2025.112086>
- 数据集:
  - <https://doi.org/10.17632/8p6r8ft4fz.1>
- 可查看摘要:
  - <https://pubmed.ncbi.nlm.nih.gov/41114292/>

### 5.4 农田 / 野地 / 乡村开阔区

8. Li et al., Drones 2023

- 用途:
  - 支撑 `open-field / farmland` 的 A2G 基线
- 关键结论:
  - 在 `3.6 GHz` 农业场景下进行了 A2G 测量
  - 论文重点在“实测 + RT + ANN”建模
- 说明:
  - 该文更适合支撑“开阔农田不应直接等同城市或林区”，以及作为后续高频段开阔区校准的补充来源
- DOI:
  - <https://doi.org/10.3390/drones7120701>
- 摘要页:
  - <https://doaj.org/article/03dd867bdf8844799503cab88cb62422>

9. AERPAW rural A2G dataset / VTC 2024

- 用途:
  - 支撑农村低空 A2G 模型与开放数据校准
- 说明:
  - 当前更适合记为“后续校准数据源”，不适合直接抽一个最终常数写死
- 数据页:
  - <https://aerpaw.org/dataset/gnu-radio-based-air-to-ground-channel-sounding-data/>
- 数据 DOI:
  - <https://doi.org/10.5061/dryad.7h44j105p>
- 论文 DOI:
  - <https://doi.org/10.1109/VTC2024-Fall63153.2024.10757825>

## 6. 第一版场景分类建议

第一阶段建议只保留 4 类：

- `urban`
- `forest`
- `lake`
- `open-field`

说明：

- `lake` 可在前端显示为 `lake / open-water`
- `open-field` 可覆盖 `wild / rural / farmland` 的默认开阔区

这样好处是：

- 分类最少，先能跑通闭环
- 物理差异足够明显
- 便于和前端展示统一

## 6.1 第一阶段输入边界

第一阶段输入边界已经明确如下：

- 统一交换格式使用 `GeoJSON`
- 第一阶段不引入新的统一多图层传播底座
- 场景之间允许分开实现
- 四个场景都要能在前端显示地图

第一阶段各场景的输入要求：

- `urban`
  - 必须真实地图驱动
  - 重点读取建筑 footprint / 高度等信息
- `forest`
  - 第一阶段先支持森林区域地图显示
  - 传播模型可以先主要依赖场景参数
- `lake`
  - 第一阶段先支持水域区域地图显示
  - 传播模型可以先主要依赖场景参数
- `open-field`
  - 第一阶段先支持区域地图显示
  - 传播模型可以先主要依赖场景参数

这意味着：

- 第一阶段 `forest / lake / open-field` 不强制要求完整几何驱动传播
- 但地图格式、前端显示和后端场景识别入口要先统一留好

## 6.2 第一阶段 GeoJSON 字段契约

第一阶段必须定义统一字段契约，避免前端和后端各自解析一套属性名。

### 6.2.1 坐标与单位约定

- 几何坐标采用仿真平面坐标
- 不直接使用经纬度作为仿真输入
- 坐标单位统一为 `m`
- 所有高度字段单位统一为 `m`

### 6.2.2 所有 feature 的统一必填字段

| 字段名 | 类型 | 说明 |
|---|---|---|
| `feature_type` | `string` | 取值: `building` / `forest` / `water` / `open_field` |
| `scene_type` | `string` | 取值: `urban` / `forest` / `lake` / `open-field` |
| `name` | `string` | 要素名称，可用于前端显示 |
| `enabled` | `boolean` | 该要素是否启用 |

### 6.2.3 按场景的专属字段

`building`：

| 字段名 | 类型 | 说明 |
|---|---|---|
| `height_m` | `number` | 建筑高度 |
| `material` | `string` | 建筑材料类型，第一阶段可选 |

`forest`：

| 字段名 | 类型 | 说明 |
|---|---|---|
| `canopy_height_m` | `number` | 树冠高度 |
| `density_class` | `string` | 稠密度，如 `low` / `medium` / `high` |

`water`：

| 字段名 | 类型 | 说明 |
|---|---|---|
| `water_type` | `string` | 如 `lake` / `river`，第一阶段重点使用 `lake` |

`open_field`：

| 字段名 | 类型 | 说明 |
|---|---|---|
| `surface_type` | `string` | 如 `grass` / `soil` / `farmland` |

### 6.2.4 第一阶段解析原则

- 前端和后端都只认上面这套字段名
- 字段缺失时按场景默认值回退
- `urban` 第一阶段优先使用 `height_m`
- `forest / lake / open-field` 第一阶段可先以 `feature_type` 和 `scene_type` 驱动显示与场景识别

## 7. 第一版参数框架建议

第一版先围绕以下字段做收敛，后续再决定是否写入 `context.h`：

- `sceneType`
- `baseModel`
- `shadowSigmaDb`
- `nlosPenaltyDb`
- `vegetationLossDbPerM`
- `losBaseProb`
- `interferenceFactor`
- `connectivityRangeFactor`
- `hasBuildings`
- `reflectionAware`

字段含义：

- `baseModel`: 该场景的基础传播模型
- `shadowSigmaDb`: 大尺度阴影衰落标准差
- `nlosPenaltyDb`: NLoS 相对 LoS 的额外惩罚
- `vegetationLossDbPerM`: 植被穿越深度的单位长度衰减
- `losBaseProb`: 没有几何图时的默认 LoS 倾向
- `interferenceFactor`: 工程归一化因子，用于把环境影响映射到干扰估计
- `connectivityRangeFactor`: 工程归一化因子，用于缩放拓扑连通阈值
- `reflectionAware`: 是否启用水面/地面反射敏感逻辑

## 8. 第一阶段单值默认参数表

下面这张表已经改成“第一阶段单值默认”，用于后续直接落代码。

说明：

- 这些值是第一阶段工程默认值，不是最终物理真值
- `shadowSigmaDb`、`vegetationLossDbPerM`、城市环境层级主要由标准和文献支撑
- `interferenceFactor`、`connectivityRangeFactor` 和一部分 `nlosPenaltyDb` 仍属于工程映射量
- `urban` 若存在建筑 GeoJSON，应优先由建筑传播模型主导；表中值主要用于 fallback 和解释摘要

| sceneType | 第一阶段默认主模型 | shadowSigmaDb | nlosPenaltyDb | vegetationLossDbPerM | losBaseProb | interferenceFactor | connectivityRangeFactor | hasBuildings | reflectionAware | 说明 |
|---|---|---:|---:|---:|---:|---:|---:|---|---|---|
| `urban` | `HybridBuildingsPropagationLossModel`，无建筑时退回 `LogDistance urban fallback` | `6.0` | `18` | `0.0` | `0.45` | `1.30` | `0.72` | `true` | `false` | 默认按 `UMa/UMi` 中间强度收敛，建筑几何优先 |
| `forest` | `LogDistance + vegetation attenuation` | `6.5` | `10` | `0.30` | `0.60` | `1.20` | `0.68` | `false` | `false` | 第一阶段森林传播先由场景参数主导，地图主要负责显示 |
| `lake` | `FSPL/LogDistance baseline + reflection-sensitive correction` | `2.0` | `3` | `0.0` | `0.95` | `0.85` | `1.10` | `false` | `true` | 第一阶段湖面传播先按强 LoS + 反射敏感处理 |
| `open-field` | `RMa-like LogDistance baseline` | `4.0` | `6` | `0.00` | `0.85` | `0.95` | `1.00` | `false` | `false` | 作为默认开阔对照组 |

## 9. 仅在兼容旧代码时使用的临时折算值

这一节只用于兼容旧代码路径，不作为第一阶段推荐建模方式，也不作为前端主展示字段来源。

如果某些旧逻辑暂时仍然只能吃“一个等效 path loss exponent”，可以临时用下面这组折算值过渡，但不要把它理解成最终物理真值。

| sceneType | equivalentPathLossExponent | 说明 |
|---|---:|---|
| `urban` | `3.3` | 代表建筑阻挡、街谷绕射和较强 NLoS 的折中结果 |
| `forest` | `3.4` | 代表 rural/open 基线之上再叠加持续植被衰减 |
| `lake` | `2.1` | 代表强 LoS，但允许保留反射敏感逻辑 |
| `open-field` | `2.5` | 代表开阔野地 / 农田的系统级折中基线 |

这些值的来源不是单篇论文直接给出的最终定值，而是按以下原则做的工程折中：

- `urban` 取高于 `open-field` 的明显退化量级
- `forest` 不低于 `urban`，因为其持续穿林损耗会压缩连通距离
- `lake` 接近自由空间，但不直接设为 `2.0`
- `open-field` 作为默认对照组

限制说明：

- 这些值不应用作前端主摘要字段
- `lake` 和 `forest` 最终都不应被长期压缩成单一指数
- 后续一旦 PHY 侧模型稳定，应逐步去掉对这一折算表的依赖

## 10. `difficulty` 的建议定义

### 10.1 结论

`difficulty` 不应和 `sceneType` 绑定。

更合理的做法是：

- `sceneType` 决定基础环境
- `mapFile` 决定具体几何
- `difficulty` 决定条件预设

对外可以继续保留一个 `difficulty` 开关，但内部不再直接以单对象生效。

### 10.1.1 第一阶段内部拆分方案

第一阶段内部固定拆成 4 类 preset：

1. `environmentPreset`
- 负责场景传播和环境强弱
- 包含:
  - `shadowSigmaDb`
  - `nlosPenaltyDb`
  - `vegetationLossDbPerM`
  - `reflectionAware`
  - `connectivityRangeFactor`

2. `interferencePreset`
- 负责外部干扰条件
- 包含:
  - `numInterferenceNodes`
  - `interferenceRateMbps`
  - `interferenceDutyCycle`

3. `observationPreset`
- 负责观测与定位误差
- 包含:
  - `rtkNoiseStdDev`
  - `rtkDriftInterval`
  - `rtkDriftDuration`
  - `rtkDriftMagnitude`

4. `trafficPlatformPreset`
- 负责业务负载与无线平台配置
- 包含:
  - `trafficLoadMbps`
  - `macMaxRetries`
  - `noiseFigure`
  - `rxSensitivity`
  - `txPower`

这样做的目标不是让外部配置更复杂，而是让内部职责清楚、实验可解释。

### 10.2 推荐乘子表

| difficulty | shadowMultiplier | nlosMultiplier | interferenceMultiplier | rangeMultiplier | 解释 |
|---|---:|---:|---:|---:|---|
| `Easy` | `0.85` | `0.85` | `0.90` | `1.10` | 轻遮挡、轻干扰、留更多连通裕量 |
| `Moderate` | `1.00` | `1.00` | `1.00` | `1.00` | 默认基线 |
| `Hard` | `1.20` | `1.20` | `1.15` | `0.88` | 遮挡、散射、干扰和连通压缩同时增强 |
| `Custom` | 用户自定义 | 用户自定义 | 用户自定义 | 用户自定义 | 直接面向实验 |

### 10.2.1 第一阶段默认映射表

第一阶段 `difficulty` 的默认模板，优先沿用当前代码中的参数量级，再把它拆分映射到 4 类 preset。

唯一明确调整：

- `Hard` 不再提高 `txPower`
- 第一阶段 `txPower` 默认统一保持 `23 dBm`

| difficulty | environmentPreset | interferencePreset | observationPreset | trafficPlatformPreset |
|---|---|---|---|---|
| `Easy` | `pathLossExp=2.0`, `nakagamiM=0.0` | `numInterferenceNodes=0`, `rate=0.5`, `duty=0.1` | `rtkNoise=0.0`, `driftInterval=0.0`, `driftDuration=0.0`, `driftMagnitude=0.0` | `trafficLoad=0.2`, `macMaxRetries=7`, `noiseFigure=7.0`, `rxSensitivity=-90.0`, `txPower=23.0` |
| `Moderate` | `pathLossExp=2.5`, `nakagamiM=0.7` | `numInterferenceNodes=8`, `rate=4.0`, `duty=0.7` | `rtkNoise=0.08`, `driftInterval=15.0`, `driftDuration=4.0`, `driftMagnitude=0.5` | `trafficLoad=2.8`, `macMaxRetries=1`, `noiseFigure=15.0`, `rxSensitivity=-85.0`, `txPower=23.0` |
| `Hard` | `pathLossExp=3.0`, `nakagamiM=0.2` | `numInterferenceNodes=15`, `rate=6.0`, `duty=0.95` | `rtkNoise=0.2`, `driftInterval=8.0`, `driftDuration=6.0`, `driftMagnitude=1.0` | `trafficLoad=7.0`, `macMaxRetries=0`, `noiseFigure=20.0`, `rxSensitivity=-82.0`, `txPower=23.0` |

说明：

- 这张表主要用于第一阶段初始化和兼容现有代码
- 后续若把 `pathLossExp` 完全替换为分场景 PHY 传播模型，它可以继续保留为 fallback 或兼容字段

### 10.3 为什么它仍有意义

它不是现实中的原生物理量，但对工程系统有价值：

- 便于做前端预设
- 便于做实验对比
- 便于做压测
- 在缺少完整真实地图时先跑通系统

### 10.4 为什么不能让它主导真实建模

因为真实环境里变化的是：

- 建筑密度
- 街宽
- 植被厚度
- 水面反射
- 干扰源强度
- 节点负载
- 观测噪声

而不是一个叫 `Hard` 的统一物理量。

### 10.5 当前 `difficulty` 参数的迁移归属

后续改造时，当前 [`scratch/uav_ra/simulation_setup.cc`](/home/tzx/ns-3.43/scratch/uav_ra/simulation_setup.cc) 里混在一起的量应按下表迁移：

| 当前量 | 迁移归属 |
|---|---|
| `pathLossExp` | `environmentPreset` 或场景传播模型内部默认值 |
| `numInterferenceNodes` | `interferencePreset` |
| `interferenceRateMbps` | `interferencePreset` |
| `interferenceDutyCycle` | `interferencePreset` |
| `rtkNoiseStdDev` | `observationPreset` |
| `rtkDriftInterval` | `observationPreset` |
| `rtkDriftDuration` | `observationPreset` |
| `rtkDriftMagnitude` | `observationPreset` |
| `trafficLoadMbps` | `trafficPlatformPreset` |
| `macMaxRetries` | `trafficPlatformPreset` |
| `noiseFigure` | `trafficPlatformPreset` |
| `rxSensitivity` | `trafficPlatformPreset` |
| `txPower` | `trafficPlatformPreset` |
| `nakagamiM` | 优先进入 PHY 传播模型配置，语义上归 `environmentPreset` |

## 11. 地图驱动修正建议

第一阶段地图读取与传播模型的关系，不同场景并不完全一致。

### 11.0 总原则

- `urban`：真实地图驱动传播
- `forest / lake / open-field`：第一阶段先支持真实地图显示，但传播主逻辑可以先由场景参数驱动
- 四个场景都使用 `GeoJSON` 作为交换格式
- 第一阶段允许不同场景的实现方式不完全统一

### 11.1 城市场景

城市最适合做“几何驱动”：

- 从建筑 box 计算 LoS
- 统计建筑密度
- 统计平均楼高
- 统计街道宽度
- 统计穿越建筑次数

若 `mapFile` 有建筑：

- LoS / NLoS 应优先由几何决定
- `urban` 的 `losBaseProb` 只作为 fallback

### 11.2 森林场景

森林不应强行套城市建筑范式，而应定义：

- 林区 polygon
- 树冠高度
- 植被厚度
- 穿越植被长度

第一阶段明确实现路径：

- 基础模型使用 `LogDistance`
- 在其上新增一层森林附加损耗
- 第一阶段森林附加损耗先按场景参数驱动
- 若后续补几何计算，再升级为 `forestLossDb = vegetationLossDbPerM * vegetationDepth`

### 11.3 湖面场景

湖面不是遮挡主导，而是反射主导。

建议关注：

- 水域 polygon
- 节点高度
- 相对岸线位置
- 是否低空飞行

第一阶段明确实现路径：

- 地图先负责显示水域范围
- 基础模型使用自由空间或开阔基线
- 第一阶段直接采用 `two-ray / water-reflection` 路径
- LoS 倾向接近强 LoS
- 低空时通过 two-ray 反映反射敏感性

### 11.4 开阔野地 / 农田

建议作为默认对照组：

- 障碍少
- 建筑少
- 反射适中
- 连通性最稳定

第一阶段建议逻辑：

- 地图先负责显示区域范围
- 传播主逻辑先使用 `open-field` 场景默认参数

## 12. 对当前代码改造的含义

这一轮文档整理对应到代码层，结论是：

### 12.1 需要拆开的职责

当前 [`scratch/uav_ra/simulation_setup.cc`](/home/tzx/ns-3.43/scratch/uav_ra/simulation_setup.cc) 里的 `ApplyDifficultyProfile()` 后续最好拆成 4 类内部 preset：

- `environmentPreset`
- `interferencePreset`
- `observationPreset`
- `trafficPlatformPreset`

### 12.2 当前最值得保留的能力

- 现有 `mapFile -> LoadBuildingsFromMap()` 的思路是对的
- 现有 `mapFile 非空时使用 HybridBuildingsPropagationLossModel` 的方向也是对的

### 12.3 当前最值得修正的点

不要再让 `difficulty` 同时决定：

- path loss exponent
- RTK 噪声
- 干扰节点
- 业务负载
- PHY 灵敏度

而应改为：

- `sceneType` 决定传播基础
- `mapFile` 决定结构修正
- `difficulty` 只做外部入口，再在内部展开成 4 类 preset

### 12.4 实现时必须遵守的边界

- 真实空间传播影响优先进入 `ns-3 PHY`
- `topology_control` 只做控制层估计和解释修正
- 前端输出只做展示，不再改链路值
- 第一阶段不追求四个场景统一传播代码路径，允许分场景实现

## 13. 第一阶段建议输出给前端的字段

前端第一阶段只需要以下字段就够：

- `sceneType`
- `baseModel`
- `hasBuildings`
- `reflectionAware`
- `shadowSigmaDb`
- `nlosPenaltyDb`
- `vegetationLossDbPerM`
- `interferenceFactor`
- `connectivityRangeFactor`
- `effectiveModelSummary`
- `environmentContributionSummary`
- `environmentSource`

其中 `environmentSource` 建议显示：

- `scene-base only`
- `scene-base + map geometry`
- `scene-base + map geometry + difficulty`

这样前端能解释“当前连通性变化来自哪里”。

补充说明：

- 不再把 `effectivePathLossExponent` 作为前端主字段
- `lake` 和 `forest` 不适合被压缩成单一指数给前端解释
- 前端应优先展示“当前模型是什么、影响来自哪里”，而不是一个单值指数

## 14. 当前可执行的结论

本阶段最稳的工程方案是：

1. 保留 `sceneType`
2. 保留 `mapFile`
3. 保留 `difficulty`，但仅作为外部入口
4. 内部把 `difficulty` 展开成 4 类 preset
5. 第一阶段统一使用 `GeoJSON` 作为地图交换格式
6. 四个场景都实现，但第一阶段允许分场景路径
7. `urban` 必须真实地图驱动传播
8. `forest / lake / open-field` 第一阶段先支持地图显示，传播主逻辑可先依赖场景参数
9. 环境影响按“PHY 主生效 + 控制层解释修正 + 前端摘要展示”执行
10. 前端输出同时展示：
   - 当前场景类型
   - 当前关键环境参数
   - 是否有建筑 / 植被 / 水面反射
   - 当前环境值是如何得出的

## 15. 需要特别注意的限制

### 15.1 `interferenceFactor` 没有直接标准常数

文献通常直接给：

- path loss
- shadowing
- LoS/NLoS
- delay spread

而不是直接给一个统一的 `interferenceFactor = 1.3`。

因此本文中的：

- `interferenceFactor`
- `connectivityRangeFactor`

都属于工程映射量，不是标准原始常数。

### 15.2 湖面场景的“路径损耗指数”不能机械理解

Leite 2022 里低空湖面出现过异常负指数，这恰恰说明：

- 水面场景不能只靠单一指数
- 最终应优先做反射敏感逻辑

### 15.3 `forest` 最终最好用“深度损耗”而不是只改 exponent

如果后续只靠一个 `pathLossExponent` 来表达森林，会把“穿越 10 m 林区”和“穿越 100 m 林区”混为一谈，解释性不足。

## 16. 下一步建议

这份文档核查通过后，再进入代码设计阶段。

推荐下一步：

1. 基于本文档确定字段命名
2. 把参数按 `base / geometry / final` 三层拆开
3. 再决定 `context.h` 中的结构体设计
4. 最后接 `simulation_setup.cc`、`topology_control.cc` 和输出摘要
