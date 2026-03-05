# 翼网全境 (Wing-Net Omni) 代码变更日志

---

## Phase 1：数据修复 — 让核心仿真真正输出干净数据

**日期**：2026-03-03
**涉及文件**：`scratch/uav_resource_allocation.cc`
**目标**：修复 `qos_performance.csv` 中数据全为 0 的问题，确保所有 15 架无人机的 QoS 数据都能被正确记录。

---

### 变更 1：修复 `srcId` 硬编码为 0 的 Bug

**位置**：`MonitorQoSPerformance()` 函数

**原始代码**：
```cpp
// 更新节点性能指标(简化处理:使用源节点)
// 在实际中需要根据IP地址映射到节点ID
uint32_t srcId = 0; // 需要实际映射
g_state.nodePDR[srcId] = pdr;
g_state.nodeDelay[srcId] = delay;
g_state.nodeThroughput[srcId] = throughput;
```

**修改后代码**：
```cpp
// 使用IP地址映射到节点ID (IP为 10.1.1.1 ~ 10.1.1.N)
Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);
uint32_t srcId = (tuple.sourceAddress.Get() & 0xFF) - 1;
if (srcId >= g_uavNodes.GetN()) continue; // 防越界
```

**改动性质**：✅ 明确 Bug（原作者注释 `// 需要实际映射` 自行标注了未完成）

**改动理由**：所有流（不论来自哪架飞机）都被写入了 `uav0` 那一列，其余 14 台飞机的列全为 0。通过从 IP 地址末字节（`10.1.1.X` 中的 `X`）反推飞机编号（`X-1`），实现正确的节点映射。

---

### 变更 2：修复多流情况下的数据覆盖问题

**位置**：`MonitorQoSPerformance()` 函数

**原始代码**：
```cpp
g_state.nodePDR[srcId] = pdr;      // 最后一条流直接覆盖之前的所有流
g_state.nodeDelay[srcId] = delay;
g_state.nodeThroughput[srcId] = throughput;
```

**修改后代码**：
```cpp
// 新增流计数器，用于在线增量平均
std::map<uint32_t, uint32_t> flowCount; // srcId -> 该源飞机的流数量

// 增量式滚动平均 (避免多流时最后一条覆盖前面)
g_state.nodePDR[srcId]   = (g_state.nodePDR[srcId] * flowCount[srcId] + pdr)   / (flowCount[srcId] + 1);
g_state.nodeDelay[srcId] = (g_state.nodeDelay[srcId] * flowCount[srcId] + delay) / (flowCount[srcId] + 1);
g_state.nodeThroughput[srcId] += throughput; // 吞吐量直接累加（多流叠加）
flowCount[srcId]++;
```

**改动性质**：✅ 逻辑 Bug

**改动理由**：当同一架飞机同时发出 2 条流时，第二条流的统计数据会直接覆盖第一条的，导致最终记录的 PDR / Delay 只是最后一条流的单点数值，不具代表性。改为在线增量均值后，PDR 和 Delay 是该飞机所有流的加权平均。

---

### 变更 3：将应用层流量从 `UdpClientHelper` 替换为 `OnOffHelper / PacketSinkHelper`

**位置**：`SetupMixedTraffic()` 函数

**原始代码**：
```cpp
// 原始：使用 UdpClientHelper + UdpServerHelper
// 存在随机目的地选择，导致部分飞机永远不做发送方
UdpClientHelper client(dstAddr, port);
client.SetAttribute("MaxPackets", UintegerValue(1000000));
client.SetAttribute("Interval", TimeValue(Seconds(1.0 / g_config.packetRate)));
...
uint32_t j = rand() % n;  // 随机目的地，可能某些飞机始终不发包
```

**修改后代码**：
```cpp
// 改为 OnOffHelper (CBR 恒定比特率) + PacketSinkHelper (标准接收器)
// 保证每架飞机 i 都发出 2 条流：
//   流1: i → (i+1)%n    (相邻节点，模拟近距离飞控通信)
//   流2: i → (i+n/2)%n  (跨区节点，模拟远程图传通信)
uint32_t dst1 = (i + 1) % n;
uint32_t dst2 = (i + n / 2) % n;

OnOffHelper onoff("ns3::UdpSocketFactory", InetSocketAddress(dstAddr, port));
onoff.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
onoff.SetAttribute("DataRate", DataRateValue(DataRate("100kb/s")));
```

**改动性质**：⚠️ 设计改动（有意为之，语义上更符合物流编队场景）

**改动理由**：
- **根本问题**：原来的随机目的地选择可能造成某些飞机从来没有被选为发送方，FlowMonitor 按 `srcId` 统计，这些飞机的 CSV 列就永远是 0。
- **流量模型**：`UdpClientHelper` 在 NS-3 中较老，`OnOffHelper+PacketSink` 是工业界标准的满载 UDP 测试模型，对 FlowMonitor 的兼容性更好。
- **语义变化**：从"随机 Ad-Hoc 通信"改为"有序的环形编队通信"，更贴合本项目的城市物流编队场景（飞机和编队邻机保持固定的通信关系）。

**对原始仿真语义的影响**：如需还原"随机目的地"的流量模式，可在以后新增一个 `--trafficMode=random` 命令行参数分支。

---

### 变更 4：将路由表初始化移至 `SetupMixedTraffic()` 内部

**位置**：`main()` 函数 → 移入 `SetupMixedTraffic()` 末尾

**原始代码**：
```cpp
// 在 main() 中，流量安装之前
Ipv4GlobalRoutingHelper::PopulateRoutingTables();
// 设置业务流量
SetupMixedTraffic();
```

**修改后代码**：
```cpp
void SetupMixedTraffic() {
    // ... 安装所有应用 ...
    
    // 强制路由表在应用安装完毕后立即初始化（非常关键）
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
}
```

**改动性质**：✅ 合理的结构改动（无功能风险）

**改动理由**：路由表必须在所有节点和应用全部安装完毕后才能调用，放在 `SetupMixedTraffic()` 末尾更能保证执行顺序的正确性，同时消除了在 `main()` 中重复调用两次的问题。

---

### 变更 5：WiFi 物理层从 `802.11b` 升级为 `802.11a`

**位置**：`main()` 函数的 WiFi 配置区段

**原始代码**：
```cpp
wifi.SetStandard(WIFI_STANDARD_80211b);
wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
    "DataMode", StringValue("DsssRate11Mbps"),
    "ControlMode", StringValue("DsssRate1Mbps"));
YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
```

**修改后代码**：
```cpp
wifi.SetStandard(WIFI_STANDARD_80211a); // 5GHz OFDM, 最高 54Mbps
wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
    "DataMode",    StringValue("OfdmRate54Mbps"),
    "ControlMode", StringValue("OfdmRate6Mbps"));

// 使用 Log-Distance 传播损耗模型 (自由空间衰减指数 = 2.0)
YansWifiChannelHelper wifiChannel;
wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
wifiChannel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
    "Exponent", DoubleValue(2.0),
    "ReferenceDistance", DoubleValue(1.0),
    "ReferenceLoss", DoubleValue(46.6777));

wifiPhy.Set("TxPowerStart", DoubleValue(23.0)); // 23 dBm
wifiPhy.Set("TxPowerEnd",   DoubleValue(23.0));
wifiPhy.Set("RxSensitivity", DoubleValue(-82.0));
```

**改动性质**：⚠️ 有依据的设计改动（风险较高，需关注）

**改动理由**：
- **性能崩溃问题**：15 架飞机 + 30 条 CBR 流在 `802.11b` 的 11Mbps 单信道上运行，总负载远超信道容量，导致 MAC 层碰撞严重，网络整体 PDR 只有 5%，绝大多数飞机数据全为 0。
- **物理现实性**：现代工业无人机（如大疆企业系列）普遍使用 5.8GHz 频段，`802.11a` 的 5GHz 更符合实际。
- **需注意**：如需和 `802.11b` 基线场景对比，运行时指定不同 `outputDir` 并手动修改 WiFi 标准。

---

## Phase 1 改动总结

| # | 改动位置 | 改动性质 | 是否影响原始语义 |
|---|---|---|---|
| 1 | `MonitorQoSPerformance` - srcId 映射 | ✅ 明确 Bug 修复 | 否 |
| 2 | `MonitorQoSPerformance` - 多流平均 | ✅ 逻辑 Bug 修复 | 否 |
| 3 | `SetupMixedTraffic` - 流量模型 | ⚠️ 设计改动 | **是：流量分布从随机→固定环形** |
| 4 | 路由表初始化位置 | ✅ 结构优化 | 否 |
| 5 | WiFi 物理层标准 | ⚠️ 设计改动 | **是：工作频段从 2.4GHz→5GHz** |

**Phase 1 最终结果验证（200 秒仿真）**：
- 平均 PDR：**97.6%** （原来：基本为 0%）
- 平均端到端时延：**0.69 ms** （原来：NaN/无法统计）
- 总吞吐量：**2.98 Mbps** （原来：~0.2 Mbps 且数据不可信）
- 全部 15 架 UAV 的 CSV 列均有非零数据 ✅

---

## 后续开发计划

- **Phase 2**：引入真实 V 字编队轨迹，用 `WaypointMobilityModel` 替换 `RandomWalk2d`，输出 `positions.csv`
- **Phase 3**：打通 Benchmark 三级难度（Easy/Moderate/Hard）批量测试脚本
- **Phase 4**：移植 RTK 漂移噪声和黑飞干扰节点模块
- **Phase 5**：JSON 场景配置接口，打通前端场景编辑器 → 后端物理引擎链路

---

## Phase 2: Real Mobility Modeling (2026-03-04)

**目标**: 将随机游走(Random Walk)模型替换为基于 RTK 真实轨迹数据的编队模型。

**核心改动内容**:
1. **轨迹数据加载引擎**:
   - 在 `uav_resource_allocation.cc` 中新增了 `LoadFormationTrajectory` 函数。
   - 支持解析 `data_rtk/` 目录下的 CSV/TXT 轨迹文件（格式: `time, nodeId, x, y, z`）。
   - 实现了轨迹点时间排序和非递增时间点过滤，确保 `WaypointMobilityModel` 的稳定性。

2. **移动模型升级**:
   - 引入了 `ns3::WaypointMobilityModel`。
   - 新增 `SetupFormationMobility` 函数，将加载的轨迹点批量注入到每个 UAV 节点的移动栈中。
   - 保留了 `RandomWalk2dMobilityModel` 作为缺省（Fall-back）模式，确保代码兼容性。

3. **仿真逻辑优化**:
   - **参数化控制**: 引入了 `--formation` 命令行参数。支持的值: `v_formation`, `cross`, `line`, `triangle`。
   - **自动适配时长**: 仿真时长 `duration` 现在会根据轨迹文件的实际长度自动取最小值，防止空运行。
   - **动态输出路径**: 运行编队仿真时，输出目录会自动从 `output/resource_allocation` 切换到对应编队的子目录（如 `output/resource_allocation_v_formation`），方便前端批量分类展示。

**运行示例**:
```bash
./ns3 run "uav_resource_allocation --formation=v_formation --duration=200"
```

**验证结果**:
- 成功在 `output/resource_allocation_v_formation` 生成了 15 架无人机的全量 QoS 数据。
- PDR 达到 100%，平均时延 0.38ms（稳定队形下的链路质量显著高于随机游走）。
- 轨迹平滑，未触发 Waypoint 越界异常。

