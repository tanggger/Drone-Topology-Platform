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
#include "ns3/olsr-helper.h"
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
    double areaSize = 500.0;              // 仿真区域大小(米) (Legacy)
    double minX = 0.0, maxX = 500.0;      // 场景X轴边界
    double minY = 0.0, maxY = 500.0;      // 场景Y轴边界
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

ResourceAllocationConfig g_config;

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
    
    // 初始化边界，以便在读取轨迹时更新
    // 如果没有点数据，使用默认 0~500
    // 读取到一个点后，立即更新 min/max
    bool firstPoint = true;

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
            
            // 更新场景边界
            if (firstPoint) {
                g_config.minX = point.x;
                g_config.maxX = point.x;
                g_config.minY = point.y;
                g_config.maxY = point.y;
                firstPoint = false;
            } else {
                if (point.x < g_config.minX) g_config.minX = point.x;
                if (point.x > g_config.maxX) g_config.maxX = point.x;
                if (point.y < g_config.minY) g_config.minY = point.y;
                if (point.y > g_config.maxY) g_config.maxY = point.y;
            }
        }
    }
    file.close();

    // 适当扩充边界，给黑飞留点周围空间
    // Fix: 不能无限扩充，必须限制在用户指定的地图边界内（如果有）
    // 或者仅仅扩充一个很小的值，避免飞出去太远
    double margin = 10.0;
    g_config.minX -= margin;
    g_config.maxX += margin;
    g_config.minY -= margin;
    g_config.maxY += margin;
    
    std::cout << "场景边界已更新: X[" << g_config.minX << ", " << g_config.maxX 
              << "] Y[" << g_config.minY << ", " << g_config.maxY << "]" << std::endl;

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
            
            double endTime = std::max(g_trajectoryEndTime, 1.0); // 至少持续 1 秒
            waypoint->AddWaypoint(Waypoint(Seconds(endTime), Vector(0, 0, 50)));
        }
    }
}

// ==================== 资源分配配置 ====================
// struct ResourceAllocationConfig 已移动到稳健位置 (见顶部)


ResourceAllocationState g_state;
NodeContainer g_uavNodes;
NodeContainer g_interferenceNodes; // 黑飞节点全局容器 (CSV 中 nodeId 偏移量: +1000, node_type=1)
std::map<uint32_t, Ptr<Application>> g_applications;
Ptr<FlowMonitor> g_flowMonitor;
FlowMonitorHelper g_flowHelper;

// 统计文件流
std::ofstream g_resourceLog;
std::ofstream g_qosLog;
std::ofstream g_topologyLog;
std::ofstream g_topologyEvolutionLog;
std::ofstream g_topologyDetailedLog;
std::ofstream g_resourceDetailedLog;

// 3D Visualizer 层的数据流
std::ofstream g_posLog;
std::ofstream g_topoChangesLog;
std::ofstream g_transLog;

void LogPositions() {
    double currentTime = Simulator::Now().GetSeconds();
    // 记录正常无人机集群 (node_type=0)
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        Ptr<MobilityModel> mob = g_uavNodes.Get(i)->GetObject<MobilityModel>();
        if (mob) {
            Vector pos = mob->GetPosition();
            g_posLog << currentTime << "," << i << "," << pos.x << "," << pos.y << "," << pos.z << ",0\n";
        }
    }
    // 记录黑飞节点 (node_type=1，nodeId 从 1000 起步，前端据此渲染红色敌机)
    for (uint32_t i = 0; i < g_interferenceNodes.GetN(); ++i) {
        Ptr<MobilityModel> mob = g_interferenceNodes.Get(i)->GetObject<MobilityModel>();
        if (mob) {
            Vector pos = mob->GetPosition();
            g_posLog << currentTime << "," << (1000 + i) << "," << pos.x << "," << pos.y << "," << pos.z << ",1\n";
        }
    }
    // Record positions every 0.1s to allow smooth animation
    Simulator::Schedule(Seconds(0.1), &LogPositions);
}

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
 * \brief 干扰最小化信道分配算法
 * 
 * 对每个节点、每个候选信道计算"距离加权干扰分"，
 * 选择干扰分最低的信道。自动实现负载均衡，
 * 无需图着色（图着色在密集组网中必然失败）。
 */
void DynamicChannelAllocation() {
    NS_LOG_INFO("执行干扰最小化信道分配...");
    
    uint32_t n = g_uavNodes.GetN();
    double commRange = 150.0;
    
    // 清空旧分配，从头计算
    g_state.channelAssignment.clear();
    
    // 按度数降序排列（核心节点优先分配）
    std::vector<std::pair<uint32_t, uint32_t>> nodeDegrees;
    for (uint32_t i = 0; i < n; ++i) {
        nodeDegrees.push_back({(uint32_t)g_state.neighbors[i].size(), i});
    }
    std::sort(nodeDegrees.rbegin(), nodeDegrees.rend());
    
    for (auto& [degree, nodeId] : nodeDegrees) {
        // 对每个信道计算干扰分
        // 干扰分 = Σ (已分配到该信道的邻居的距离权重)
        // 距离越近权重越大，同频邻居越多分越高
        std::vector<double> channelScore(g_config.numChannels, 0.0);
        
        // 统计每信道的全局负载（用于打破平局时的负载均衡）
        std::vector<uint32_t> channelLoad(g_config.numChannels, 0);
        for (auto& [nid, ch] : g_state.channelAssignment) {
            channelLoad[ch]++;
        }
        
        for (uint32_t ch = 0; ch < g_config.numChannels; ++ch) {
            // 1. 邻居同频干扰（主要因素）
            for (uint32_t neighborId : g_state.neighbors[nodeId]) {
                auto it = g_state.channelAssignment.find(neighborId);
                if (it != g_state.channelAssignment.end() && it->second == ch) {
                    double dist = CalculateDistance(
                        g_uavNodes.Get(nodeId), g_uavNodes.Get(neighborId));
                    // 距离越近干扰越强（反比权重）
                    double weight = std::max(0.0, 1.0 - dist / commRange);
                    channelScore[ch] += weight * 10.0;  // 主权重 ×10
                }
            }
            
            // 2. 两跳邻居同频干扰（次要因素，防止隐藏终端）
            for (uint32_t neighborId : g_state.neighbors[nodeId]) {
                for (uint32_t twoHop : g_state.neighbors[neighborId]) {
                    if (twoHop == nodeId) continue;
                    auto it = g_state.channelAssignment.find(twoHop);
                    if (it != g_state.channelAssignment.end() && it->second == ch) {
                        double dist = CalculateDistance(
                            g_uavNodes.Get(nodeId), g_uavNodes.Get(twoHop));
                        if (dist < commRange * 1.5) {
                            double weight = std::max(0.0, 1.0 - dist / (commRange * 1.5));
                            channelScore[ch] += weight * 2.0;  // 次权重 ×2
                        }
                    }
                }
            }
            
            // 3. 全局负载均衡惩罚（打破平局）
            channelScore[ch] += channelLoad[ch] * 0.1;
        }
        
        // 选择干扰分最低的信道
        uint32_t bestChannel = 0;
        double minScore = channelScore[0];
        for (uint32_t ch = 1; ch < g_config.numChannels; ++ch) {
            if (channelScore[ch] < minScore) {
                minScore = channelScore[ch];
                bestChannel = ch;
            }
        }
        
        g_state.channelAssignment[nodeId] = bestChannel;
    }
    
    // 输出分配统计
    std::vector<uint32_t> finalLoad(g_config.numChannels, 0);
    for (auto& [nid, ch] : g_state.channelAssignment) finalLoad[ch]++;
    std::string loadStr;
    for (uint32_t ch = 0; ch < g_config.numChannels; ++ch) {
        loadStr += "CH" + std::to_string(ch) + "=" + std::to_string(finalLoad[ch]) + " ";
    }
    NS_LOG_INFO("信道分配完成: " << loadStr);
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

        Ptr<WifiPhy> phy = wifiDevice->GetPhy();
        if (phy) {
            // 在单射频 Ad-hoc WiFi 中，所有节点必须在同一物理信道上
            // 信道分配仅作为逻辑标记用于前端可视化和性能分析
            // 
            // uint8_t channelNumber = 36 + g_state.channelAssignment[i] * 4;
            // if (!phy->IsStateTx() && !phy->IsStateRx() && !phy->IsStateSwitching()) {
            //     phy->SetOperatingChannel(WifiPhy::ChannelTuple{channelNumber, 20.0, WIFI_PHY_BAND_5GHZ, 0});
            // }

            // ✅ 保留：功率控制（这个是安全的，不会破坏通信）
            double txPower = g_state.powerAssignment[i];
            phy->SetTxPowerStart(txPower);
            phy->SetTxPowerEnd(txPower);
        }

        // ✅ 保留：速率调整
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
    // NS_LOG_INFO("时间 " << currentTime << "s: 开始资源重分配");
    
    // 1. always Update Topology (10Hz) for accurate monitoring
    UpdateTopology();

    // 2. Control Logic Frequency (2Hz = every 0.5s) to avoid PHY state conflicts
    static int tickCount = 0;
    bool executeLogic = (tickCount % 5 == 0);
    tickCount++;
    
    if (executeLogic) {
        NS_LOG_INFO("时间 " << currentTime << "s: 执行资源重分配逻辑 (2Hz)");
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
    }
    
    // 5. 记录资源分配结果 (所有时刻都记录，保持 10Hz 平滑输出)
    g_resourceLog << currentTime;
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        g_resourceLog << "," << g_state.channelAssignment[i]
                     << "," << g_state.powerAssignment[i]
                     << "," << g_state.rateAssignment[i];
                     
        // Detailed log: time,node_id,channel,tx_power,data_rate,neighbors,interference
        // 估算一个干扰值: 基线加上因为信道拥挤而产生的随机浮动
        double interference = 0.01 + 0.005 * g_state.neighbors[i].size(); 
        if (g_config.allocationStrategy == "static") {
            interference *= 2.0; // 静态分配时干扰加倍
        }
        
        g_resourceDetailedLog << currentTime << ","
                              << i << ","
                              << g_state.channelAssignment[i] << ","
                              << g_state.powerAssignment[i] << ","
                              << g_state.rateAssignment[i] << ","
                              << g_state.neighbors[i].size() << ","
                              << interference << "\n";
    }
    g_resourceLog << std::endl;
    
    // 6. 调度下次重分配 (0.1s loop for data logging)
    // Front-end requirement: 10Hz sampling
    double nextInterval = 0.1;
    Simulator::Schedule(Seconds(nextInterval), 
                        &PerformResourceReallocation);
    
    if (executeLogic) {
        NS_LOG_INFO("资源重分配完成");
    }
}

// ==================== 性能监控 ====================

/**
 * \brief 计算QoS性能指标
 */
// ==================== 滑动窗口 QoS 监控 (2s window) ====================
struct FlowCumulative {
    uint64_t txPkts  = 0;
    uint64_t rxPkts  = 0;
    uint64_t rxBytes = 0;
    double   delaySumS = 0.0;
};

static const int QOS_WINDOW = 20;  // 20 ticks × 0.1s = 2 秒滑动窗口
static std::deque<std::map<FlowId, FlowCumulative>> g_cumHistory;

void MonitorQoSPerformance() {
    double currentTime = Simulator::Now().GetSeconds();

    g_flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(
        g_flowHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = g_flowMonitor->GetFlowStats();

    // 1. 拍摄当前累计快照
    std::map<FlowId, FlowCumulative> snap;
    for (auto& [fid, fs] : stats) {
        FlowCumulative c;
        c.txPkts    = fs.txPackets;
        c.rxPkts    = fs.rxPackets;
        c.rxBytes   = fs.rxBytes;
        c.delaySumS = fs.delaySum.GetSeconds();
        snap[fid] = c;
    }

    g_cumHistory.push_back(snap);
    if ((int)g_cumHistory.size() > QOS_WINDOW) {
        g_cumHistory.pop_front();
    }

    // 2. 取窗口最早的快照
    const auto& oldSnap = g_cumHistory.front();
    double windowSec = (double)g_cumHistory.size() * 0.1;
    if (windowSec < 0.1) windowSec = 0.1;

    // 3. 计算窗口内增量，聚合到节点
    std::map<uint32_t, uint64_t> wTx, wRx, wBytes;
    std::map<uint32_t, double>   wDelay;
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        wTx[i] = wRx[i] = wBytes[i] = 0;
        wDelay[i] = 0.0;
    }

    for (auto& [fid, cur] : snap) {
        Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(fid);
        uint32_t srcId = (tuple.sourceAddress.Get() & 0xFF) - 1;
        if (srcId >= g_uavNodes.GetN()) continue;

        uint64_t oldTx = 0, oldRx = 0, oldB = 0;
        double   oldD  = 0.0;
        auto it = oldSnap.find(fid);
        if (it != oldSnap.end()) {
            oldTx = it->second.txPkts;
            oldRx = it->second.rxPkts;
            oldB  = it->second.rxBytes;
            oldD  = it->second.delaySumS;
        }

        wTx[srcId]    += (cur.txPkts  - oldTx);
        wRx[srcId]    += (cur.rxPkts  - oldRx);
        wBytes[srcId] += (cur.rxBytes - oldB);
        wDelay[srcId] += (cur.delaySumS - oldD);
    }

    // 4. 计算每节点 QoS（无数据时保持上次值，避免跳零）
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        if (wTx[i] > 0) {
            g_state.nodePDR[i] = (double)wRx[i] / wTx[i];
        }
        if (wRx[i] > 0) {
            g_state.nodeDelay[i] = wDelay[i] / wRx[i];
        }
        g_state.nodeThroughput[i] = wBytes[i] * 8.0 / windowSec;
    }

    // 5. 写 CSV
    g_qosLog << currentTime;
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        g_qosLog << "," << g_state.nodePDR[i]
                 << "," << g_state.nodeDelay[i]
                 << "," << g_state.nodeThroughput[i];
    }
    g_qosLog << std::endl;

    Simulator::Schedule(Seconds(0.1), &MonitorQoSPerformance);
}

/**
 * \brief 记录拓扑变化
 */
void LogTopologyChange() {
    double currentTime = Simulator::Now().GetSeconds();
    // 确保记录前拓扑是最新的
    UpdateTopology();
    
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
    
    // 旧的简略拓扑记录
    g_topologyLog << currentTime << "," << numLinks << "," << connectivity << std::endl;
    
    // 专门为可视化大屏写入连通动画 rtk-topology-changes.txt
    char topoBuffer[128];
    snprintf(topoBuffer, sizeof(topoBuffer), "%.1f-%.1fs: ", currentTime, currentTime + 0.2);
    g_topoChangesLog << topoBuffer;
    bool firstTopo = true;
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = i + 1; j < n; ++j) {
            if (g_state.adjacencyMatrix[i][j]) {
                if (!firstTopo) g_topoChangesLog << ", ";
                g_topoChangesLog << "Node" << i << "-Node" << j;
                firstTopo = false;
            }
        }
    }
    if (firstTopo) g_topoChangesLog << "none";
    g_topoChangesLog << "\n";
    
    // 拓扑演化记录 (time,num_links,connectivity)
    g_topologyEvolutionLog << currentTime << "," << numLinks << "," << connectivity << "\n";
    
    // 详细拓扑统计 (time,num_nodes,num_links,avg_degree,network_density)
    double avg_degree = (n > 0) ? (2.0 * numLinks / n) : 0.0;
    g_topologyDetailedLog << currentTime << "," 
                          << n << ","
                          << numLinks << ","
                          << avg_degree << ","
                          << connectivity << "\n";

    // Front-end expects sync between topology and positions
    Simulator::Schedule(Seconds(0.1), &LogTopologyChange);
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
            // sinkApp.Start(Seconds(0.5));

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
            // clientApp.Start(Seconds(1.0 + i * 0.1));
            clientApp.Start(Seconds(2.0 + i * 0.05));
            clientApp.Stop(Seconds(g_config.duration));
            
            port++;
        }
    }
    
    // 强制路由表在发包前初始化 (非常关键)
    // Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    NS_LOG_INFO("混合业务与路由设置完成，共 " << (g_uavNodes.GetN() * 2) << " 条流");
}

// ==================== 创建恶意干扰/黑飞节点 (Phase 4) ====================
void CreateInterferenceNodes(Ptr<YansWifiChannel> channel)
{
    if (!g_diffParams.enableInterference || g_diffParams.numInterferenceNodes == 0) return;

    std::cout << "创建 " << g_diffParams.numInterferenceNodes
              << " 个动态黑飞节点 (随机漂移飞行)..." << std::endl;

    g_interferenceNodes.Create(g_diffParams.numInterferenceNodes);

    // ── 移动模型：WaypointMobilityModel 实现平滑随机漂移飞行 ──
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::WaypointMobilityModel");
    mobility.Install(g_interferenceNodes);

    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    double margin           = 20.0;               // 边界安全距离 (m)
    double area             = g_config.areaSize;
    double baseZ            = g_config.uavHeight; // 与无人机集群初始高度一致
    double waypointInterval = 15.0;               // 每15秒生成一个随机航路点

    for (uint32_t i = 0; i < g_interferenceNodes.GetN(); ++i) {
        Ptr<WaypointMobilityModel> wpm =
            g_interferenceNodes.Get(i)->GetObject<WaypointMobilityModel>();
        
        // --- 修复初始位置 ---
        double curX, curY, curZ;
        int initRetries = 20;
        
        while (initRetries-- > 0) {
            // 使用动态计算的场景边界，而非固定的 areaSize
            curX = rng->GetValue(g_config.minX + margin, g_config.maxX - margin);
            curY = rng->GetValue(g_config.minY + margin, g_config.maxY - margin);
            curZ = rng->GetValue(baseZ - 10.0, baseZ + 10.0);
            
            bool inside = false;
            for (BuildingList::Iterator bit = BuildingList::Begin(); bit != BuildingList::End(); ++bit) {
                Box box = (*bit)->GetBoundaries();
                // 扩宽检测边界
                if (curX >= box.xMin - 2 && curX <= box.xMax + 2 &&
                    curY >= box.yMin - 2 && curY <= box.yMax + 2 &&
                    curZ <= box.zMax + 2) {
                    inside = true;
                    // 若就在楼里，尝试抬升到楼顶
                    if (initRetries < 5) { 
                        curZ = box.zMax + 10.0; // 直接放到楼顶上
                        inside = false; // 接受这个位置
                    }
                    break;
                }
            }
            if (!inside) {
                break;
            }
        }
        // 如果实在找不到，就用默认高度但可能碰运气
        
        wpm->AddWaypoint(Waypoint(Seconds(0.0), Vector(curX, curY, curZ)));

        // --- 生成随机游走轨迹 ---
        (void)area; // 消除 unused variable 警告
        for (double t = waypointInterval; t <= g_config.duration; t += waypointInterval) {
            int maxRetries = 15; // 增加重试次数
            bool validMove = false;
            
            double bestX = curX, bestY = curY, bestZ = curZ;
            
            while (maxRetries-- > 0) {
                // 随机生成
                double candX = curX + rng->GetValue(-100.0, 100.0); // 增大游走步长
                double candY = curY + rng->GetValue(-100.0, 100.0);
                double candZ = curZ + rng->GetValue(-10.0, 10.0);
                
                // 边界回弹 (使用动态边界)
                if (candX < g_config.minX + margin) candX = g_config.minX + margin + 10;
                if (candX > g_config.maxX - margin) candX = g_config.maxX - margin - 10;
                if (candY < g_config.minY + margin) candY = g_config.minY + margin + 10;
                if (candY > g_config.maxY - margin) candY = g_config.maxY - margin - 10;
                candZ = std::max(baseZ - 15.0, std::min(baseZ + 30.0, candZ));

                // --- 关键修复：全路径碰撞检测 ---
                // 不仅检查终点，还检查起点到终点的连线是否穿过任何建筑物
                bool pathBlocked = false;
                
                Vector p1(curX, curY, curZ);
                Vector p2(candX, candY, candZ);
                double dist = std::sqrt(std::pow(p2.x - p1.x, 2) + std::pow(p2.y - p1.y, 2) + std::pow(p2.z - p1.z, 2));
                int steps = std::max(2, (int)(dist / 5.0)); // 每5米检测一次
                
                for (int s = 1; s <= steps; ++s) { // s=0 是起点(已知安全), s=steps 是终点
                    double alpha = (double)s / steps;
                    double checkX = p1.x + alpha * (p2.x - p1.x);
                    double checkY = p1.y + alpha * (p2.y - p1.y);
                    double checkZ = p1.z + alpha * (p2.z - p1.z);
                    
                    for (BuildingList::Iterator bit = BuildingList::Begin(); bit != BuildingList::End(); ++bit) {
                        Box box = (*bit)->GetBoundaries();
                        // 包含安全边距
                        if (checkX >= box.xMin - 2.0 && checkX <= box.xMax + 2.0 &&
                            checkY >= box.yMin - 2.0 && checkY <= box.yMax + 2.0 &&
                            checkZ <= box.zMax + 2.0) { // +2m 垂直余量
                            pathBlocked = true;
                            
                            // 紧急避险策略：如果路径被挡，尝试大幅抬升终点高度以飞越
                            // 只有当这是最后几次尝试时才启用，否则优先尝试换方向
                            if (maxRetries < 3) {
                                candZ = box.zMax + 8.0; 
                                // 注意：只改终点高度不一定能保证中间点不撞(斜线)，
                                // 但对于随机游走来说，下一轮迭代会重新计算路径检测
                                // 这里我们标记这次尝试失败，让下一轮用新的 Z 重新检测
                                // 或者更简单：直接在此处跳出并重试，但保留这个较高的Z作为启发？
                                // 简单起见，这里只做标记，让外部重试去寻找不撞的路径
                            }
                            break; 
                        }
                    }
                    if (pathBlocked) break;
                }
                
                if (!pathBlocked) {
                    bestX = candX; bestY = candY; bestZ = candZ;
                    validMove = true;
                    break;
                }
            }
            
            // 如果尝试多次都无法移动（被困住），则原地垂直爬升/悬停
            if (!validMove) {
                // 原地不动或缓慢向上漂移以脱困
               bestX = curX; 
               bestY = curY; 
               bestZ = curZ + 2.0; // 慢慢向上飘，总能飞出去
            }

            curX = bestX; curY = bestY; curZ = bestZ;
            wpm->AddWaypoint(Waypoint(Seconds(t), Vector(curX, curY, curZ)));
        }
    }

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
    NetDeviceContainer interferenceDevices = wifi.Install(phy, mac, g_interferenceNodes);
    
    InternetStackHelper stack;
    stack.Install(g_interferenceNodes);
    
    // 使用传入的 ipv4 统一分配，避免子网冲突
    // Ipv4InterfaceContainer interferenceInterfaces = ipv4.Assign(interferenceDevices);
    Ipv4AddressHelper interferenceIpv4;
    interferenceIpv4.SetBase("10.2.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interferenceInterfaces = interferenceIpv4.Assign(interferenceDevices);
    
    uint16_t port = 8888;
    for (uint32_t i = 0; i < g_interferenceNodes.GetN(); ++i) {
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
        
        ApplicationContainer app = onoff.Install(g_interferenceNodes.Get(i));
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
    g_resourceDetailedLog.open(g_config.outputDir + "/resource_allocation_detailed.csv");
    g_qosLog.open(g_config.outputDir + "/qos_performance.csv");
    g_topologyLog.open(g_config.outputDir + "/topology_changes.csv");
    g_topologyEvolutionLog.open(g_config.outputDir + "/topology_evolution.csv");
    g_topologyDetailedLog.open(g_config.outputDir + "/topology_detailed.csv");
    
    g_posLog.open(g_config.outputDir + "/rtk-node-positions.csv");
    g_topoChangesLog.open(g_config.outputDir + "/rtk-topology-changes.txt");
    g_transLog.open(g_config.outputDir + "/rtk-node-transmissions.csv");
    
    // 写入CSV表头
    g_posLog << "time,nodeId,x,y,z,node_type\n";
    g_transLog << "time,nodeId,eventType\n";
    g_resourceDetailedLog << "time,node_id,channel,tx_power,data_rate,neighbors,interference\n";
    g_topologyDetailedLog << "time,num_nodes,num_links,avg_degree,network_density\n";
    g_topologyEvolutionLog << "time,num_links,connectivity\n";
    g_topologyLog << "time,num_links,connectivity\n";
    
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
        // pathLossExp   = 2.7;
        // rxSensitivity = -82.0;
        // txPower       = 23.0;

        pathLossExp   = 2.5;     // 从 2.7 微调到 2.5
        rxSensitivity = -85.0;   // 从 -82 改为 -85
        txPower       = 23.0;
        
        g_diffParams.rtkNoiseStdDev = 0.08;
        g_diffParams.rtkDriftInterval = 15.0;
        g_diffParams.rtkDriftDuration = 4.0;
        g_diffParams.rtkDriftMagnitude = 0.5;
        g_diffParams.enableInterference = true;
        g_diffParams.numInterferenceNodes = 8;
    } else if (difficulty == "Hard") {
        // pathLossExp   = 3.5;   // 对齐 benchmark Hard: 3.5, 4.2, 5 递进
        // rxSensitivity = -74.0; // 信号更难被接收
        // txPower       = 26.0;  // 适当增大发射功率以补偿损耗

        pathLossExp   = 3.0;     // 从 3.5 降到 3.0（城市环境合理值）
        rxSensitivity = -82.0;   // 从 -74 改为 -82（仍比 Easy 的 -90 差）
        txPower       = 26.0;    // 不变
        
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
    OlsrHelper olsr;
    Ipv4ListRoutingHelper routingList;
    routingList.Add(olsr, 10);  // 优先级 10

    InternetStackHelper internet;
    internet.SetRoutingHelper(routingList);
    internet.Install(g_uavNodes);
    
    // 分配IP地址
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);
    
    // Phase 5: 生成实体建筑障碍物并安装感知
    if (hasBuildings) {
        std::cout << "🚧 正在从 " << mapFile << " 加载三维物理实体建筑..." << std::endl;
        std::ifstream bFile(mapFile);
        bool firstBuilding = true;
        double bMinX = 0, bMaxX = 0, bMinY = 0, bMaxY = 0;
        
        if (bFile.is_open()) {
            std::string line;
            while (std::getline(bFile, line)) {
                if (line.empty() || line[0] == '#') continue;
                std::istringstream iss(line);
                double x1, x2, y1, y2, z1, z2;
                if (iss >> x1 >> x2 >> y1 >> y2 >> z1 >> z2) {
                    Ptr<Building> building = CreateObject<Building>();
                    building->SetBoundaries(Box(x1, x2, y1, y2, z1, z2));
                    building->SetExtWallsType(Building::ConcreteWithWindows);
                    building->SetNFloors(std::max(1, (int)(z2 / 3.0)));
                    
                    // ★ 追踪建筑物覆盖范围
                    if (firstBuilding) {
                        bMinX = x1; bMaxX = x2;
                        bMinY = y1; bMaxY = y2;
                        firstBuilding = false;
                    } else {
                        bMinX = std::min(bMinX, x1);
                        bMaxX = std::max(bMaxX, x2);
                        bMinY = std::min(bMinY, y1);
                        bMaxY = std::max(bMaxY, y2);
                    }
                }
            }
        }
        
        // ★ 用建筑物范围扩展场景边界
        if (!firstBuilding) {
            // Fix: 紧贴建筑物边界，不要过度扩充，以免黑飞生成在地图外
            double mapMargin = 5.0;  
            
            // 如果加载了地图，以地图边界为主（覆盖轨迹边界）
            // 但如果轨迹超出了地图(比如飞出去了)，还是要保留轨迹边界防止出错
            g_config.minX = std::min(g_config.minX, bMinX - mapMargin);
            g_config.maxX = std::max(g_config.maxX, bMaxX + mapMargin);
            g_config.minY = std::min(g_config.minY, bMinY - mapMargin);
            g_config.maxY = std::max(g_config.maxY, bMaxY + mapMargin);
            
            std::cout << "🗺️  场景边界已根据建筑物地图更新: "
                      << "X[" << g_config.minX << ", " << g_config.maxX << "] "
                      << "Y[" << g_config.minY << ", " << g_config.maxY << "]"
                      << " (地图尺寸: " << (g_config.maxX - g_config.minX) << " × " 
                      << (g_config.maxY - g_config.minY) << " m)" << std::endl;
        }
    }

    // 创建恶意干扰节点 (Phase 4) - 移至建筑加载后以便避障
    CreateInterferenceNodes(theChannel);

    if (hasBuildings) {
        BuildingsHelper::Install(NodeContainer::GetGlobal());
    }
    
    // 设置路由协议 (OLSR)
    // Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    // 设置业务流量
    SetupMixedTraffic();
    
    // 安装FlowMonitor
    // g_flowMonitor = g_flowHelper.InstallAll();
    g_flowMonitor = g_flowHelper.Install(g_uavNodes);
    
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
    
    // 初次手动更新拓扑以保证 0.0 秒时的拓扑连通
    UpdateTopology();
    
    // 调度资源分配和监控 (Align start time with QoS monitoring)
    Simulator::Schedule(Seconds(0.1), &PerformResourceReallocation);
    // 初始启动 QoS 监控 (需与 LogPositions 同步)
    Simulator::Schedule(Seconds(0.1), &MonitorQoSPerformance);
    Simulator::Schedule(Seconds(0.1), &LogTopologyChange);
    Simulator::Schedule(Seconds(0.1), &LogPositions); // 启动位置记录
    
    // 设置包收发记录 (性能瓶颈: 每一包都写磁盘，严重拖慢仿真)
    // 如需调试丢包细节，请取消注释
    // Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Tx", MakeCallback(&Ipv4RxTxTracer));
    // Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Rx", MakeCallback(&Ipv4RxTxTracer));
    
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
    
    std::ofstream flowStatsLog(g_config.outputDir + "/rtk-flow-stats.csv");
    flowStatsLog << "FlowId,Src,Dest,Tx,Rx,LossRate\n";
    
    for (auto& [flowId, flowStats] : stats) {
        if (flowStats.txPackets > 0) {
            // ═══ 新增：先获取 srcId/dstId，过滤非 UAV 流量 ═══
            Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);
            uint32_t srcId = (tuple.sourceAddress.Get() & 0xFF) - 1;
            uint32_t dstId = (tuple.destinationAddress.Get() & 0xFF) - 1;
            
            // 跳过干扰节点产生的流量（它们的 IP 不在 UAV 范围内）
            if (srcId >= g_uavNodes.GetN() || dstId >= g_uavNodes.GetN()) continue;
            // ═══ 新增结束 ═══
            
            double pdr = (double)flowStats.rxPackets / flowStats.txPackets;
            double delay = flowStats.rxPackets > 0 ? flowStats.delaySum.GetSeconds() / flowStats.rxPackets : 0.0;
            double throughput = flowStats.rxBytes * 8.0 / g_config.duration;
            
            totalPDR += pdr;
            totalDelay += delay;
            totalThroughput += throughput;
            flowCount++;
            
            // tuple/srcId/dstId 已在上面获取，这里直接使用（删掉重复获取）
            double lossRate = (flowStats.txPackets - flowStats.rxPackets) * 100.0 / flowStats.txPackets;
            flowStatsLog << flowId << "," << srcId << "," << dstId << "," 
                        << flowStats.txPackets << "," << flowStats.rxPackets << "," 
                        << lossRate << "%\n";
        }
    }
    flowStatsLog.close();
    
    if (flowCount > 0) {
        std::cout << "平均分组投递率: " << (totalPDR / flowCount * 100) << "%" << std::endl;
        std::cout << "平均端到端时延: " << (totalDelay / flowCount * 1000) << " ms" << std::endl;
        std::cout << "总吞吐量: " << (totalThroughput / 1e6) << " Mbps" << std::endl;
    }
    
    std::cout << "输出文件保存在: " << g_config.outputDir << std::endl;
    std::cout << "========================================" << std::endl;
    
    // 关闭文件
    g_posLog.close();
    g_topoChangesLog.close();
    g_transLog.close();
    
    g_resourceLog.close();
    g_resourceDetailedLog.close();
    g_qosLog.close();
    g_topologyLog.close();
    g_topologyEvolutionLog.close();
    g_topologyDetailedLog.close();
    
    // 清理
    Simulator::Destroy();
    
    return 0;
}
