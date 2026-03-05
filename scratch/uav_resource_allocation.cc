/*
 * uav_resource_allocation.cc
 * 
 * 无人机辅助无线通信资源分配仿真
 * 
 * 功能特性：
 * - 动态信道分配
 * - 功率控制
 * - 链路调度策略
 * - QoS性能监控（分组投递率、端到端时延）
 * - 拓扑自适应算法
 * 
 * 作者：基于 ns-3 UAV 仿真框架
 * 日期：2025
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/buildings-module.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <map>
#include <sstream>
#include <algorithm>
#include <limits>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("UAVResourceAllocation");

// ==================== 编队轨迹数据 ====================
struct TrajectoryPoint {
    double time;
    uint32_t nodeId;
    double x, y, z;
};

static std::vector<TrajectoryPoint> g_trajectoryData;
static std::map<uint32_t, std::vector<TrajectoryPoint>> g_nodeTrajectories;
static double g_trajectoryEndTime = 0.0;  // 轨迹数据的最大时间

// ==================== 难度与干扰参数 (Phase 4) ====================
struct DifficultyParams {
    double rtkNoiseStdDev = 0.0;
    double rtkDriftInterval = 0.0;
    double rtkDriftDuration = 0.0;
    double rtkDriftMagnitude = 0.0;
    bool enableInterference = false;
    uint32_t numInterferenceNodes = 0;
    std::string levelName = "Easy";
};
static DifficultyParams g_diffParams;
static Ptr<UniformRandomVariable> g_randVar;

// 注入 RTK 位置漂移和噪声
Vector ApplyRTKNoise(const Vector& originalPos, double time)
{
    Vector noisyPos = originalPos;
    if (g_diffParams.rtkNoiseStdDev == 0.0 && g_diffParams.rtkDriftInterval == 0.0) {
        return noisyPos;
    }
    
    // 基础高斯噪声
    Ptr<NormalRandomVariable> normalRand = CreateObject<NormalRandomVariable>();
    normalRand->SetAttribute("Mean", DoubleValue(0.0));
    normalRand->SetAttribute("Variance", DoubleValue(g_diffParams.rtkNoiseStdDev * g_diffParams.rtkNoiseStdDev));
    
    noisyPos.x += normalRand->GetValue();
    noisyPos.y += normalRand->GetValue();
    noisyPos.z += normalRand->GetValue() * 0.5;  // Z轴噪声较小
    
    // 周期性漂移
    if (g_diffParams.rtkDriftInterval > 0) {
        double cycleTime = fmod(time, g_diffParams.rtkDriftInterval);
        if (cycleTime < g_diffParams.rtkDriftDuration) {
            double driftFactor = 1.0 - exp(-3.0 * cycleTime / g_diffParams.rtkDriftDuration);
            if (!g_randVar) g_randVar = CreateObject<UniformRandomVariable>();
            noisyPos.x += g_diffParams.rtkDriftMagnitude * driftFactor * (g_randVar->GetValue() - 0.5) * 2.0;
            noisyPos.y += g_diffParams.rtkDriftMagnitude * driftFactor * (g_randVar->GetValue() - 0.5) * 2.0;
        }
    }
    
    return noisyPos;
}

// 从 data_rtk/ 加载编队轨迹文件
bool LoadFormationTrajectory(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开轨迹文件: " << filename << std::endl;
        return false;
    }

    std::string line;
    std::getline(file, line); // 跳过首行(注释或表头)

    g_trajectoryData.clear();
    g_nodeTrajectories.clear();

    double maxTime = 0.0;
    uint32_t maxNodeId = 0;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string token;
        std::vector<std::string> tokens;

        while (std::getline(iss, token, ',')) {
            tokens.push_back(token);
        }

        if (tokens.size() >= 5) {
            TrajectoryPoint point;
            point.time   = std::stod(tokens[0]);
            point.nodeId = std::stoul(tokens[1]);
            point.x      = std::stod(tokens[2]);
            point.y      = std::stod(tokens[3]);
            point.z      = std::stod(tokens[4]);

            g_trajectoryData.push_back(point);
            g_nodeTrajectories[point.nodeId].push_back(point);

            maxTime   = std::max(maxTime, point.time);
            maxNodeId = std::max(maxNodeId, point.nodeId);
        }
    }
    file.close();

    // 按时间排序并去除非严格递增的时间点
    for (auto& entry : g_nodeTrajectories) {
        auto& trajectory = entry.second;
        std::sort(trajectory.begin(), trajectory.end(),
                  [](const TrajectoryPoint& a, const TrajectoryPoint& b) {
                      return a.time < b.time;
                  });
        std::vector<TrajectoryPoint> cleaned;
        cleaned.reserve(trajectory.size());
        double lastTime = -std::numeric_limits<double>::infinity();
        for (const auto& pt : trajectory) {
            if (pt.time > lastTime) {
                cleaned.push_back(pt);
                lastTime = pt.time;
            }
        }
        trajectory.swap(cleaned);
        if (!trajectory.empty()) {
            maxTime = std::max(maxTime, trajectory.back().time);
        }
    }

    g_trajectoryEndTime = maxTime;

    std::cout << "成功加载编队轨迹数据:" << std::endl;
    std::cout << "  节点数量: " << (maxNodeId + 1) << std::endl;
    std::cout << "  轨迹时长: " << g_trajectoryEndTime << " 秒" << std::endl;
    std::cout << "  数据点数: " << g_trajectoryData.size() << std::endl;

    return true;
}

// 将轨迹数据设置为 WaypointMobilityModel
void SetupFormationMobility(NodeContainer& nodes)
{
    std::cout << "设置编队移动模型 (WaypointMobilityModel)..." << std::endl;

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::WaypointMobilityModel");
    mobility.Install(nodes);

    for (uint32_t nodeId = 0; nodeId < nodes.GetN(); ++nodeId) {
        Ptr<WaypointMobilityModel> waypoint =
            nodes.Get(nodeId)->GetObject<WaypointMobilityModel>();

        if (g_nodeTrajectories.find(nodeId) != g_nodeTrajectories.end()) {
            auto& trajectory = g_nodeTrajectories[nodeId];
            std::cout << "  节点 " << nodeId << ": " << trajectory.size() << " 个waypoint" << std::endl;
            for (const auto& point : trajectory) {
                Vector originalPos(point.x, point.y, point.z);
                Vector noisyPos = ApplyRTKNoise(originalPos, point.time);
                waypoint->AddWaypoint(Waypoint(Seconds(point.time), noisyPos));
            }
        } else {
            std::cerr << "警告: 节点 " << nodeId << " 没有轨迹数据，使用静止位置" << std::endl;
            waypoint->AddWaypoint(Waypoint(Seconds(0.0), Vector(0, 0, 50)));
            waypoint->AddWaypoint(Waypoint(Seconds(g_trajectoryEndTime), Vector(0, 0, 50)));
        }
    }
}

// ==================== 资源分配配置 ====================
struct ResourceAllocationConfig {
    // 仿真基础参数
    double duration = 200.0;              // 仿真时长(秒)
    uint32_t numUAVs = 15;                // UAV节点数量
    
    // 网络配置
    uint32_t numChannels = 3;             // 可用信道数量
    double txPowerMin = 10.0;             // 最小发射功率(dBm)
    double txPowerMax = 30.0;             // 最大发射功率(dBm)
    double dataRateMin = 1.0;             // 最小数据速率(Mbps)
    double dataRateMax = 11.0;            // 最大数据速率(Mbps)
    
    // QoS要求
    double targetPDR = 0.85;              // 目标分组投递率 (85%)
    double maxEndToEndDelay = 0.100;      // 最大端到端时延 (100ms)
    double minThroughput = 500000.0;      // 最小吞吐量 (500 Kbps)
    
    // 资源分配策略
    std::string allocationStrategy = "dynamic";  // dynamic, static, greedy, rl-based
    double reallocationInterval = 5.0;    // 资源重分配间隔(秒)
    
    // 拓扑参数
    double areaSize = 500.0;              // 仿真区域大小(米)
    double uavHeight = 50.0;              // UAV飞行高度(米)
    double maxSpeed = 20.0;               // 最大飞行速度(m/s)
    
    // 业务负载
    std::string trafficPattern = "mixed"; // cbr, poisson, mixed
    double packetSize = 1024;             // 数据包大小(字节)
    double packetRate = 100.0;            // 数据包发送速率(packets/s)
    
    // 输出配置
    std::string outputDir = "output/resource_allocation";
    bool enableVisualization = false;
};

// ==================== 资源分配状态 ====================
struct ResourceAllocationState {
    // 信道分配: nodeId -> channelId
    std::map<uint32_t, uint32_t> channelAssignment;
    
    // 功率分配: nodeId -> txPower (dBm)
    std::map<uint32_t, double> powerAssignment;
    
    // 数据速率分配: nodeId -> dataRate (Mbps)
    std::map<uint32_t, double> rateAssignment;
    
    // 链路质量: (srcId, dstId) -> linkQuality [0,1]
    std::map<std::pair<uint32_t, uint32_t>, double> linkQuality;
    
    // 性能指标
    std::map<uint32_t, double> nodePDR;          // 节点分组投递率
    std::map<uint32_t, double> nodeDelay;        // 节点平均时延
    std::map<uint32_t, double> nodeThroughput;   // 节点吞吐量
    
    // 拓扑信息
    std::vector<std::vector<bool>> adjacencyMatrix;  // 邻接矩阵
    std::map<uint32_t, std::vector<uint32_t>> neighbors; // 邻居列表
};

// ==================== 全局变量 ====================
ResourceAllocationConfig g_config;
ResourceAllocationState g_state;
NodeContainer g_uavNodes;
std::map<uint32_t, Ptr<Application>> g_applications;
Ptr<FlowMonitor> g_flowMonitor;
FlowMonitorHelper g_flowHelper;

// 统计文件流
std::ofstream g_resourceLog;
std::ofstream g_qosLog;
std::ofstream g_topologyLog;

// ==================== 资源分配算法 ====================

/**
 * \brief 计算两个节点之间的距离
 */
double CalculateDistance(Ptr<Node> node1, Ptr<Node> node2) {
    Ptr<MobilityModel> mob1 = node1->GetObject<MobilityModel>();
    Ptr<MobilityModel> mob2 = node2->GetObject<MobilityModel>();
    
    if (!mob1 || !mob2) return 1e9;
    
    Vector pos1 = mob1->GetPosition();
    Vector pos2 = mob2->GetPosition();
    
    double dx = pos1.x - pos2.x;
    double dy = pos1.y - pos2.y;
    double dz = pos1.z - pos2.z;
    
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

/**
 * \brief 更新拓扑邻接矩阵
 */
void UpdateTopology() {
    uint32_t n = g_uavNodes.GetN();
    g_state.adjacencyMatrix.clear();
    g_state.adjacencyMatrix.resize(n, std::vector<bool>(n, false));
    g_state.neighbors.clear();
    
    double commRange = 150.0; // 通信距离阈值(米)
    
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = i + 1; j < n; ++j) {
            double dist = CalculateDistance(g_uavNodes.Get(i), g_uavNodes.Get(j));
            
            if (dist <= commRange) {
                g_state.adjacencyMatrix[i][j] = true;
                g_state.adjacencyMatrix[j][i] = true;
                g_state.neighbors[i].push_back(j);
                g_state.neighbors[j].push_back(i);
            }
        }
    }
}

/**
 * \brief 基于距离的链路质量估计
 */
double EstimateLinkQuality(uint32_t srcId, uint32_t dstId) {
    Ptr<Node> src = g_uavNodes.Get(srcId);
    Ptr<Node> dst = g_uavNodes.Get(dstId);
    
    double dist = CalculateDistance(src, dst);
    double maxDist = 150.0;
    
    // 简单的路径损耗模型
    if (dist > maxDist) return 0.0;
    
    // 链路质量随距离递减
    double quality = 1.0 - (dist / maxDist);
    return std::max(0.0, std::min(1.0, quality));
}

/**
 * \brief 动态信道分配算法
 * 
 * 采用图着色算法，避免相邻节点使用相同信道
 */
void DynamicChannelAllocation() {
    NS_LOG_INFO("执行动态信道分配...");
    
    uint32_t n = g_uavNodes.GetN();
    std::vector<uint32_t> channelUsage(g_config.numChannels, 0);
    
    // 按节点度数排序(度数大的优先分配)
    std::vector<std::pair<uint32_t, uint32_t>> nodeDegrees;
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t degree = g_state.neighbors[i].size();
        nodeDegrees.push_back({degree, i});
    }
    std::sort(nodeDegrees.rbegin(), nodeDegrees.rend());
    
    // 为每个节点分配信道
    for (auto& [degree, nodeId] : nodeDegrees) {
        std::vector<bool> usedChannels(g_config.numChannels, false);
        
        // 标记邻居节点使用的信道
        for (uint32_t neighborId : g_state.neighbors[nodeId]) {
            if (g_state.channelAssignment.find(neighborId) != g_state.channelAssignment.end()) {
                uint32_t ch = g_state.channelAssignment[neighborId];
                usedChannels[ch] = true;
            }
        }
        
        // 选择使用最少的可用信道
        uint32_t bestChannel = 0;
        uint32_t minUsage = channelUsage[0];
        
        for (uint32_t ch = 0; ch < g_config.numChannels; ++ch) {
            if (!usedChannels[ch] && channelUsage[ch] < minUsage) {
                bestChannel = ch;
                minUsage = channelUsage[ch];
            }
        }
        
        g_state.channelAssignment[nodeId] = bestChannel;
        channelUsage[bestChannel]++;
    }
    
    NS_LOG_INFO("信道分配完成");
}

/**
 * \brief 动态功率控制算法
 * 
 * 根据链路距离和干扰情况调整发射功率
 */
void DynamicPowerControl() {
    NS_LOG_INFO("执行动态功率控制...");
    
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        if (g_state.neighbors[i].empty()) {
            // 没有邻居，使用最大功率
            g_state.powerAssignment[i] = g_config.txPowerMax;
            continue;
        }
        
        // 计算到所有邻居的平均距离
        double avgDist = 0.0;
        for (uint32_t neighborId : g_state.neighbors[i]) {
            avgDist += CalculateDistance(g_uavNodes.Get(i), g_uavNodes.Get(neighborId));
        }
        avgDist /= g_state.neighbors[i].size();
        
        // 根据距离调整功率 (距离越远，功率越大)
        double maxDist = 150.0;
        double powerRatio = std::min(1.0, avgDist / maxDist);
        double txPower = g_config.txPowerMin + 
                         powerRatio * (g_config.txPowerMax - g_config.txPowerMin);
        
        g_state.powerAssignment[i] = txPower;
    }
    
    NS_LOG_INFO("功率控制完成");
}

/**
 * \brief 自适应速率调整算法
 * 
 * 根据链路质量动态调整数据传输速率
 */
void AdaptiveRateControl() {
    NS_LOG_INFO("执行自适应速率调整...");
    
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        if (g_state.neighbors[i].empty()) {
            g_state.rateAssignment[i] = g_config.dataRateMin;
            continue;
        }
        
        // 计算到所有邻居的平均链路质量
        double avgQuality = 0.0;
        for (uint32_t neighborId : g_state.neighbors[i]) {
            avgQuality += EstimateLinkQuality(i, neighborId);
        }
        avgQuality /= g_state.neighbors[i].size();
        
        // 根据链路质量调整速率
        double dataRate = g_config.dataRateMin + 
                          avgQuality * (g_config.dataRateMax - g_config.dataRateMin);
        
        g_state.rateAssignment[i] = dataRate;
    }
    
    NS_LOG_INFO("速率调整完成");
}

// ==================== 物理层资源下发 (确保算法生效) ====================
void ApplyResourceAssignments() {
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        Ptr<Node> node = g_uavNodes.Get(i);
        Ptr<NetDevice> device = node->GetDevice(0);
        Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(device);
        if (!wifiDevice) continue;

        // 1. 下发信道分配 (直接修改物理层频率)
        Ptr<WifiPhy> phy = wifiDevice->GetPhy();
        if (phy) {
            // NS-3 3.43 新写法: 设置工作信道
            // 映射 0, 1, 2 -> 36, 40, 44 (5GHz UNII-1)
            uint8_t channelNumber = 36 + g_state.channelAssignment[i] * 4;
            
            // ChannelTuple = std::tuple<uint8_t, double(MHz_u), WifiPhyBand, uint8_t>
            // 802.11a 默认为 20MHz, 5GHz, primary20Index=0
            phy->SetOperatingChannel(WifiPhy::ChannelTuple{channelNumber, 20.0, WIFI_PHY_BAND_5GHZ, 0});
            
            // 2. 下发功率控制 (修改发射功率)
            double txPower = g_state.powerAssignment[i];
            phy->SetTxPowerStart(txPower);
            phy->SetTxPowerEnd(txPower);
        }

        // 3. 下发速率调整 (修改速率管理器参数)
        Ptr<WifiRemoteStationManager> stationManager = wifiDevice->GetRemoteStationManager();
        if (stationManager) {
            std::string rateMode = "OfdmRate6Mbps";
            double rate = g_state.rateAssignment[i];
            if (rate >= 54.0) rateMode = "OfdmRate54Mbps";
            else if (rate >= 48.0) rateMode = "OfdmRate48Mbps";
            else if (rate >= 36.0) rateMode = "OfdmRate36Mbps";
            else if (rate >= 24.0) rateMode = "OfdmRate24Mbps";
            else if (rate >= 18.0) rateMode = "OfdmRate18Mbps";
            else if (rate >= 12.0) rateMode = "OfdmRate12Mbps";
            
            stationManager->SetAttribute("DataMode", StringValue(rateMode));
        }
    }
}

/**
 * \brief 资源重分配主函数
 */
void PerformResourceReallocation() {
    double currentTime = Simulator::Now().GetSeconds();
    NS_LOG_INFO("时间 " << currentTime << "s: 开始资源重分配");
    
    // 1. 更新拓扑信息
    UpdateTopology();
    
    // 2. 如果策略是 dynamic，则执行智能抗干扰集群调度
    if (g_config.allocationStrategy == "dynamic") {
        // 执行信道分配
        DynamicChannelAllocation();
        
        // 执行功率控制
        DynamicPowerControl();
        
        // 执行速率调整
        AdaptiveRateControl();
    } else {
        // static (Baseline): 啥都不干，坐以待毙（被干扰、被建筑物挡死）
        NS_LOG_INFO("Baseline (static) 模式，保持固定粗放的资源分配");
    }
    
    // 4.5 物理下发：让所有計算好的参数在此刻真实作用于模拟器
    ApplyResourceAssignments();
    
    // 5. 记录资源分配结果
    g_resourceLog << currentTime;
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        g_resourceLog << "," << g_state.channelAssignment[i]
                     << "," << g_state.powerAssignment[i]
                     << "," << g_state.rateAssignment[i];
    }
    g_resourceLog << std::endl;
    
    // 6. 调度下次重分配
    Simulator::Schedule(Seconds(g_config.reallocationInterval), 
                        &PerformResourceReallocation);
    
    NS_LOG_INFO("资源重分配完成");
}

// ==================== 性能监控 ====================

/**
 * \brief 计算QoS性能指标
 */
void MonitorQoSPerformance() {
    double currentTime = Simulator::Now().GetSeconds();
    
    // 使用FlowMonitor收集统计数据
    g_flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(
        g_flowHelper.GetClassifier());
    
    FlowMonitor::FlowStatsContainer stats = g_flowMonitor->GetFlowStats();
    
    // 初始化性能指标和流计数(用于后续求平均)
    std::map<uint32_t, uint32_t> flowCount; // srcId -> 流的数量
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        g_state.nodePDR[i] = 0.0;
        g_state.nodeDelay[i] = 0.0;
        g_state.nodeThroughput[i] = 0.0;
        flowCount[i] = 0;
    }
    
    // 统计每个流的性能
    for (auto& [flowId, flowStats] : stats) {
        if (flowStats.txPackets == 0) continue; // 跳过空流
        
        // 使用IP地址映射到节点ID (IP为 10.1.1.1 ~ 10.1.1.N)
        Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);
        uint32_t srcId = (tuple.sourceAddress.Get() & 0xFF) - 1;
        if (srcId >= g_uavNodes.GetN()) continue; // 防越界
        
        // 计算PDR (本次流)
        double pdr = (double)flowStats.rxPackets / flowStats.txPackets;
        
        // 计算平均时延 (本次流)
        double delay = 0.0;
        if (flowStats.rxPackets > 0) {
            delay = flowStats.delaySum.GetSeconds() / flowStats.rxPackets;
        }
        
        // 计算吞吐量 bps (本次流, 基于接收字节)
        double throughput = flowStats.rxBytes * 8.0 / currentTime;
        
        // ★ 关键修复: 累加后除以流数量，避免多流时最后一条覆盖前面
        g_state.nodePDR[srcId]        = (g_state.nodePDR[srcId] * flowCount[srcId] + pdr) / (flowCount[srcId] + 1);
        g_state.nodeDelay[srcId]      = (g_state.nodeDelay[srcId] * flowCount[srcId] + delay) / (flowCount[srcId] + 1);
        g_state.nodeThroughput[srcId] += throughput; // 吞吐量直接累加(多流叠加)
        flowCount[srcId]++;
    }
    
    // 记录QoS性能
    g_qosLog << currentTime;
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        g_qosLog << "," << g_state.nodePDR[i]
                << "," << g_state.nodeDelay[i]
                << "," << g_state.nodeThroughput[i];
    }
    g_qosLog << std::endl;
    
    // 定期监控
    Simulator::Schedule(Seconds(1.0), &MonitorQoSPerformance);
}

/**
 * \brief 记录拓扑变化
 */
void LogTopologyChange() {
    double currentTime = Simulator::Now().GetSeconds();
    
    // 统计活跃链路数
    uint32_t numLinks = 0;
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        numLinks += g_state.neighbors[i].size();
    }
    numLinks /= 2; // 无向图
    
    // 计算网络连通性
    double connectivity = 0.0;
    uint32_t n = g_uavNodes.GetN();
    if (n > 1) {
        uint32_t maxLinks = n * (n - 1) / 2;
        connectivity = (double)numLinks / maxLinks;
    }
    
    g_topologyLog << currentTime << "," << numLinks << "," << connectivity << std::endl;
    
    // 定期记录
    Simulator::Schedule(Seconds(2.0), &LogTopologyChange);
}

// ==================== 应用层业务生成 ====================

/**
 * \brief 为节点对安装UDP应用
 */
void InstallUdpApplication(Ptr<Node> srcNode, Ptr<Node> dstNode, 
                           uint16_t port, double startTime, double stopTime) {
    // 获取目的节点IP地址
    Ptr<Ipv4> ipv4 = dstNode->GetObject<Ipv4>();
    if (!ipv4) return;
    
    Ipv4Address dstAddr = ipv4->GetAddress(1, 0).GetLocal();
    
    // 创建UDP客户端
    UdpClientHelper client(dstAddr, port);
    client.SetAttribute("MaxPackets", UintegerValue(1000000));
    client.SetAttribute("Interval", TimeValue(Seconds(1.0 / g_config.packetRate)));
    client.SetAttribute("PacketSize", UintegerValue(g_config.packetSize));
    
    ApplicationContainer clientApp = client.Install(srcNode);
    clientApp.Start(Seconds(startTime));
    clientApp.Stop(Seconds(stopTime));
    
    // 创建UDP服务器
    UdpServerHelper server(port);
    ApplicationContainer serverApp = server.Install(dstNode);
    serverApp.Start(Seconds(startTime));
    serverApp.Stop(Seconds(stopTime));
}

/**
 * \brief 设置混合业务模式 - 保证每架UAV都有发送流量 (Fix: all-UAV coverage)
 */
void SetupMixedTraffic() {
    NS_LOG_INFO("设置业务流量 (OnOff CBR UDP, 全节点覆盖)...");
    
    uint16_t port = 9000;
    uint32_t n = g_uavNodes.GetN();
    
    // 关键修复: 每架无人机 i 都有2条发送流
    // 流1: i -> (i+1)%n  (相邻节点, 模拟近距离飞控通信)
    // 流2: i -> (i + n/2)%n (跨区节点, 模拟远程图传通信)
    // 这样保证每个 uavX_pdr 列都能被填写到非零数据
    for (uint32_t i = 0; i < n; ++i) {
        // 目的节点列表: 确保两个目标都不是自己
        uint32_t dst1 = (i + 1) % n;
        uint32_t dst2 = (i + n / 2) % n;
        if (dst2 == i) dst2 = (i + 2) % n; // 极端情况: n=2时规避自发自收
        
        uint32_t dsts[2] = {dst1, dst2};
        
        for (int k = 0; k < 2; ++k) {
            uint32_t j = dsts[k];
            
            Ptr<Ipv4> ipv4 = g_uavNodes.Get(j)->GetObject<Ipv4>();
            Ipv4Address dstAddr = ipv4->GetAddress(1, 0).GetLocal();
            
            // 接收端 (PacketSink) - 安装在目的节点上
            PacketSinkHelper sink("ns3::UdpSocketFactory", 
                                  InetSocketAddress(Ipv4Address::GetAny(), port));
            ApplicationContainer sinkApp = sink.Install(g_uavNodes.Get(j));
            sinkApp.Start(Seconds(0.5));
            sinkApp.Stop(Seconds(g_config.duration));
            
            // 发送端 (OnOff CBR) - 安装在源节点 i 上
            OnOffHelper onoff("ns3::UdpSocketFactory", 
                              InetSocketAddress(dstAddr, port));
            onoff.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
            onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
            // 每条流 ~100Kbps CBR
            onoff.SetAttribute("DataRate", DataRateValue(DataRate("100kb/s")));
            onoff.SetAttribute("PacketSize", UintegerValue((uint32_t)g_config.packetSize));
            
            ApplicationContainer clientApp = onoff.Install(g_uavNodes.Get(i));
            // 每架飞机错开 0.1s 启动，彻底避免 ARP 广播风暴
            clientApp.Start(Seconds(1.0 + i * 0.1));
            clientApp.Stop(Seconds(g_config.duration));
            
            port++;
        }
    }
    
    // 强制路由表在发包前初始化 (非常关键)
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    NS_LOG_INFO("混合业务与路由设置完成，共 " << (g_uavNodes.GetN() * 2) << " 条流");
}

// ==================== 创建恶意干扰/黑飞节点 (Phase 4) ====================
void CreateInterferenceNodes(Ptr<YansWifiChannel> channel, Ipv4AddressHelper& ipv4)
{
    if (!g_diffParams.enableInterference || g_diffParams.numInterferenceNodes == 0) return;

    std::cout << "创建 " << g_diffParams.numInterferenceNodes << " 个无赖干扰节点(黑飞)..." << std::endl;
    
    NodeContainer interferenceNodes;
    interferenceNodes.Create(g_diffParams.numInterferenceNodes);
    
    // 干扰节点位置：随机固定在区域内
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::RandomBoxPositionAllocator",
        "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string(g_config.areaSize) + "]"),
        "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + std::to_string(g_config.areaSize) + "]"),
        "Z", StringValue("ns3::UniformRandomVariable[Min=" + std::to_string(g_config.uavHeight - 10) + "|Max=" + std::to_string(g_config.uavHeight + 10) + "]"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(interferenceNodes);
    
    WifiHelper wifi; // 干扰节点使用相同标准发包
    wifi.SetStandard(WIFI_STANDARD_80211a);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("OfdmRate6Mbps"),
                                 "ControlMode", StringValue("OfdmRate6Mbps"));
    
    YansWifiPhyHelper phy;
    phy.SetChannel(channel);
    phy.Set("TxPowerStart", DoubleValue(30.0)); // 干扰节点发射功率更强，模拟恶意压制
    phy.Set("TxPowerEnd", DoubleValue(30.0));
    
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer interferenceDevices = wifi.Install(phy, mac, interferenceNodes);
    
    InternetStackHelper stack;
    stack.Install(interferenceNodes);
    
    // 使用传入的 ipv4 统一分配，避免子网冲突
    Ipv4InterfaceContainer interferenceInterfaces = ipv4.Assign(interferenceDevices);
    
    uint16_t port = 8888;
    for (uint32_t i = 0; i < interferenceNodes.GetN(); ++i) {
        // 垃圾广播
        std::string dataRate = "500kbps";
        double onTime = 0.1;
        double offTime = 0.9;
        uint32_t pktSize = 512;
        
        if (g_diffParams.levelName == "Moderate") {
            dataRate = "4Mbps";
            onTime = 0.7;
            offTime = 0.3;
            pktSize = 1300;
        } else if (g_diffParams.levelName == "Hard") {
            dataRate = "6Mbps";
            onTime = 0.95;  // 95%占空比疯狂发包
            offTime = 0.05;
            pktSize = 1472;
        }
        
        OnOffHelper onoff("ns3::UdpSocketFactory",
                         InetSocketAddress(Ipv4Address("255.255.255.255"), port));
        onoff.SetAttribute("PacketSize", UintegerValue(pktSize));
        onoff.SetAttribute("DataRate", StringValue(dataRate));
        onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=" + std::to_string(onTime) + "]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=" + std::to_string(offTime) + "]"));
        
        ApplicationContainer app = onoff.Install(interferenceNodes.Get(i));
        app.Start(Seconds(1.0));
        app.Stop(Seconds(g_config.duration));
    }
}

// ==================== 主函数 ====================

int main(int argc, char *argv[])
{
    // 编队模式参数 (空=随机游走, v_formation/cross/line/triangle=编队轨迹)
    std::string formation = "";
    // 难度参数: Easy / Moderate / Hard  (对应 benchmark 三级配置)
    std::string difficulty = "Easy";
    // 城市建筑物地图文件
    std::string mapFile = "";
    
    // 解析命令行参数
    CommandLine cmd;
    cmd.AddValue("duration",   "仿真时长(秒)",              g_config.duration);
    cmd.AddValue("numUAVs",   "UAV节点数量",               g_config.numUAVs);
    cmd.AddValue("numChannels","可用信道数量",              g_config.numChannels);
    cmd.AddValue("strategy",  "资源分配策略",               g_config.allocationStrategy);
    cmd.AddValue("targetPDR", "目标分组投递率",             g_config.targetPDR);
    cmd.AddValue("maxDelay",  "最大端到端时延(秒)",         g_config.maxEndToEndDelay);
    cmd.AddValue("outputDir", "输出目录",                   g_config.outputDir);
    cmd.AddValue("formation", "编队模式 (v_formation/cross/line/triangle，空=随机游走)", formation);
    cmd.AddValue("difficulty","难度等级 (Easy/Moderate/Hard)", difficulty);
    cmd.AddValue("mapFile",   "自定义城市建筑物地图 (可为空)", mapFile);
    cmd.Parse(argc, argv);
    
    // 如果指定了编队模式，尝试加载轨迹文件
    bool useFormation = false;
    if (!formation.empty()) {
        std::string traceFile = "data_rtk/mobility_trace_" + formation + ".txt";
        std::cout << "加载编队轨迹: " << traceFile << std::endl;
        if (LoadFormationTrajectory(traceFile)) {
            useFormation = true;
            // 从轨迹数据中读取节点数量
            uint32_t maxNodeId = 0;
            for (const auto& entry : g_nodeTrajectories) {
                maxNodeId = std::max(maxNodeId, entry.first);
            }
            g_config.numUAVs = maxNodeId + 1;
            // 仿真时长取用户指定值和轨迹时长的较小值（避免超出轨迹范围）
            if (g_config.duration > g_trajectoryEndTime) {
                g_config.duration = g_trajectoryEndTime;
            }
            // 自动设置输出目录（包含编队名和难度）
            if (g_config.outputDir == "output/resource_allocation") {
                g_config.outputDir = "output/resource_allocation_" + formation + "_" + difficulty;
            }
            std::cout << "编队模式: " << formation << std::endl;
        } else {
            std::cerr << "轨迹文件加载失败，回退到随机游走模式" << std::endl;
        }
    }
    
    // 启用日志
    LogComponentEnable("UAVResourceAllocation", LOG_LEVEL_INFO);
    
    std::cout << "========================================" << std::endl;
    std::cout << "无人机辅助无线通信资源分配仿真" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "节点数量: " << g_config.numUAVs << std::endl;
    std::cout << "信道数量: " << g_config.numChannels << std::endl;
    std::cout << "仿真时长: " << g_config.duration << " 秒" << std::endl;
    std::cout << "移动模型: " << (useFormation ? ("编队轨迹 [" + formation + "]") : "随机游走") << std::endl;
    std::cout << "难度等级: " << difficulty << std::endl;
    std::cout << "目标PDR: " << g_config.targetPDR * 100 << "%" << std::endl;
    std::cout << "最大时延: " << g_config.maxEndToEndDelay * 1000 << " ms" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 创建输出目录
    std::string cmd_mkdir = "mkdir -p " + g_config.outputDir;
    int ret = system(cmd_mkdir.c_str());
    (void)ret;
    
    // 打开统计文件
    g_resourceLog.open(g_config.outputDir + "/resource_allocation.csv");
    g_qosLog.open(g_config.outputDir + "/qos_performance.csv");
    g_topologyLog.open(g_config.outputDir + "/topology_evolution.csv");
    
    // 写入CSV表头
    g_resourceLog << "time";
    for (uint32_t i = 0; i < g_config.numUAVs; ++i) {
        g_resourceLog << ",uav" << i << "_channel"
                     << ",uav" << i << "_power"
                     << ",uav" << i << "_rate";
    }
    g_resourceLog << std::endl;
    
    g_qosLog << "time";
    for (uint32_t i = 0; i < g_config.numUAVs; ++i) {
        g_qosLog << ",uav" << i << "_pdr"
                << ",uav" << i << "_delay"
                << ",uav" << i << "_throughput";
    }
    g_qosLog << std::endl;
    
    g_topologyLog << "time,num_links,connectivity" << std::endl;
    
    // 创建UAV节点
    g_uavNodes.Create(g_config.numUAVs);
    
    // 配置移动模型
    if (useFormation) {
        // 使用编队轨迹 (WaypointMobilityModel)
        SetupFormationMobility(g_uavNodes);
    } else {
        // 默认：3D随机游走
        std::cout << "使用随机游走移动模型" << std::endl;
        MobilityHelper mobility;
        mobility.SetPositionAllocator("ns3::RandomBoxPositionAllocator",
                                      "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + 
                                                       std::to_string(g_config.areaSize) + "]"),
                                      "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" + 
                                                       std::to_string(g_config.areaSize) + "]"),
                                      "Z", StringValue("ns3::UniformRandomVariable[Min=" + 
                                                       std::to_string(g_config.uavHeight - 10) + "|Max=" +
                                                       std::to_string(g_config.uavHeight + 10) + "]"));
        
        mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                                 "Bounds", RectangleValue(Rectangle(0, g_config.areaSize, 
                                                                    0, g_config.areaSize)),
                                 "Speed", StringValue("ns3::UniformRandomVariable[Min=5.0|Max=" + 
                                                      std::to_string(g_config.maxSpeed) + "]"),
                                 "Distance", DoubleValue(50.0));
        mobility.Install(g_uavNodes);
    }
    
    // 配置WiFi (Ad-hoc模式, 802.11a 5GHz 54Mbps - 适合UAV高密度场景)
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211a); // 5GHz, 多信道, 更高吞吐
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",    StringValue("OfdmRate54Mbps"),
                                 "ControlMode", StringValue("OfdmRate6Mbps"));
    
    WifiMacHelper wifiMac;
    wifiMac.SetType("ns3::AdhocWifiMac");
    
    // ---- 根据难度等级配置信道参数 ----
    // Easy:     理想城市环境: 低路径损耗, 高接收灵敏度
    // Moderate: 标准城郊环境: 中等路径损耗
    // Hard:     复杂遮蔽环境: 高路径损耗, 低接收灵敏度
    double pathLossExp   = 2.0;    // Easy 默认
    double rxSensitivity = -90.0;  // Easy 默认 (更灵敏)
    double txPower       = 23.0;   // 23 dBm
    // Phase 4: 初始化难度参数
    g_diffParams.levelName = difficulty;
    if (difficulty == "Moderate") {
        pathLossExp   = 2.7;
        rxSensitivity = -82.0;
        txPower       = 23.0;
        
        g_diffParams.rtkNoiseStdDev = 0.08;
        g_diffParams.rtkDriftInterval = 15.0;
        g_diffParams.rtkDriftDuration = 4.0;
        g_diffParams.rtkDriftMagnitude = 0.5;
        g_diffParams.enableInterference = true;
        g_diffParams.numInterferenceNodes = 8;
    } else if (difficulty == "Hard") {
        pathLossExp   = 3.5;   // 对齐 benchmark Hard: 3.5, 4.2, 5 递进
        rxSensitivity = -74.0; // 信号更难被接收
        txPower       = 26.0;  // 适当增大发射功率以补偿损耗
        
        g_diffParams.rtkNoiseStdDev = 0.2;
        g_diffParams.rtkDriftInterval = 8.0;
        g_diffParams.rtkDriftDuration = 6.0;
        g_diffParams.rtkDriftMagnitude = 1.0;
        g_diffParams.enableInterference = true;
        g_diffParams.numInterferenceNodes = 15;
    }
    std::cout << "信道参数: PathLossExp=" << pathLossExp
              << ", RxSens=" << rxSensitivity << "dBm" << std::endl;

    YansWifiChannelHelper wifiChannel;
    wifiChannel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");
    
    // Phase 5: 如果传入了城市建筑物地图，开启宏伟的建筑物遮蔽射线追踪损耗模型
    bool hasBuildings = !mapFile.empty();
    if (hasBuildings) {
        std::cout << "🧱 初始化高级云边协同建筑射线追踪损耗模型 (HybridBuildingsPropagationLossModel)..." << std::endl;
        wifiChannel.AddPropagationLoss("ns3::HybridBuildingsPropagationLossModel");
    } else {
        wifiChannel.AddPropagationLoss("ns3::LogDistancePropagationLossModel",
                                       "Exponent",         DoubleValue(pathLossExp),
                                       "ReferenceDistance", DoubleValue(1.0),
                                       "ReferenceLoss",     DoubleValue(46.6777)); // 5GHz @ 1m
    }
    
    YansWifiPhyHelper wifiPhy;
    Ptr<YansWifiChannel> theChannel = wifiChannel.Create();
    wifiPhy.SetChannel(theChannel);
    wifiPhy.Set("TxPowerStart", DoubleValue(txPower));
    wifiPhy.Set("TxPowerEnd",   DoubleValue(txPower));
    wifiPhy.Set("RxSensitivity", DoubleValue(rxSensitivity));
    
    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, g_uavNodes);
    
    // 安装网络协议栈
    InternetStackHelper internet;
    internet.Install(g_uavNodes);
    
    // 分配IP地址
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);
    
    // 创建恶意干扰节点 (Phase 4)
    CreateInterferenceNodes(theChannel, ipv4);
    
    // Phase 5: 生成实体建筑障碍物并安装感知
    if (hasBuildings) {
        std::cout << "🚧 正在从 " << mapFile << " 加载三维物理实体建筑..." << std::endl;
        std::ifstream bFile(mapFile);
        if (bFile.is_open()) {
            std::string line;
            while (std::getline(bFile, line)) {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream iss(line);
                double x1, x2, y1, y2, z1, z2;
                if (iss >> x1 >> x2 >> y1 >> y2 >> z1 >> z2) {
                    Ptr<Building> building = CreateObject<Building>();
                    building->SetBoundaries(Box(x1, x2, y1, y2, z1, z2));
                    building->SetExtWallsType(Building::ConcreteWithWindows); // 钢筋混凝土强遮蔽
                    building->SetNFloors(std::max(1, (int)(z2 / 3.0))); // 每层按3米算
                }
            }
        }
        
        // 赋予全网节点空间建筑感知交互能力，开启真实物理遮蔽阻断
        BuildingsHelper::Install(NodeContainer::GetGlobal());
    }
    
    // 设置路由协议 (OLSR)
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    // 设置业务流量
    SetupMixedTraffic();
    
    // 安装FlowMonitor
    g_flowMonitor = g_flowHelper.InstallAll();
    
    // 初始化资源分配状态
    g_state.adjacencyMatrix.resize(g_config.numUAVs, 
                                   std::vector<bool>(g_config.numUAVs, false));
    
    for (uint32_t i = 0; i < g_config.numUAVs; ++i) {
        if (g_config.allocationStrategy == "static") {
            g_state.channelAssignment[i] = 0;    // Baseline: 所有人挤在同一个信道 (互相碰撞倒退)
            g_state.powerAssignment[i] = 20.0;   // Baseline: 全程定死发射功率 (无法穿墙)
            g_state.rateAssignment[i] = 6.0;     // Baseline: 定死速率
        } else {
            g_state.channelAssignment[i] = i % g_config.numChannels;
            g_state.powerAssignment[i] = 20.0; 
            g_state.rateAssignment[i] = 6.0;   
        }
    }
    
    // 初始下发
    ApplyResourceAssignments();
    
    // 调度资源分配和监控
    Simulator::Schedule(Seconds(0.5), &PerformResourceReallocation);
    Simulator::Schedule(Seconds(1.0), &MonitorQoSPerformance);
    Simulator::Schedule(Seconds(2.0), &LogTopologyChange);
    
    // 运行仿真
    std::cout << "\n开始仿真..." << std::endl;
    Simulator::Stop(Seconds(g_config.duration));
    Simulator::Run();
    
    // 输出最终统计
    std::cout << "\n========================================" << std::endl;
    std::cout << "仿真完成！" << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 输出FlowMonitor统计
    g_flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(
        g_flowHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = g_flowMonitor->GetFlowStats();
    
    double totalPDR = 0.0;
    double totalDelay = 0.0;
    double totalThroughput = 0.0;
    uint32_t flowCount = 0;
    
    for (auto& [flowId, flowStats] : stats) {
        if (flowStats.txPackets > 0) {
            double pdr = (double)flowStats.rxPackets / flowStats.txPackets;
            double delay = flowStats.delaySum.GetSeconds() / flowStats.rxPackets;
            double throughput = flowStats.rxBytes * 8.0 / g_config.duration;
            
            totalPDR += pdr;
            totalDelay += delay;
            totalThroughput += throughput;
            flowCount++;
        }
    }
    
    if (flowCount > 0) {
        std::cout << "平均分组投递率: " << (totalPDR / flowCount * 100) << "%" << std::endl;
        std::cout << "平均端到端时延: " << (totalDelay / flowCount * 1000) << " ms" << std::endl;
        std::cout << "总吞吐量: " << (totalThroughput / 1e6) << " Mbps" << std::endl;
    }
    
    std::cout << "输出文件保存在: " << g_config.outputDir << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 关闭文件
    g_resourceLog.close();
    g_qosLog.close();
    g_topologyLog.close();
    
    // 清理
    Simulator::Destroy();
    
    return 0;
}

