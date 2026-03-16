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
#include <set>

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
    // 干扰节点（已有 + 新增可配置项）
    bool enableInterference = false;
    uint32_t numInterferenceNodes = 0;
    double interferenceRateMbps = 0.5;     // ★ 新增：每个黑飞的发射速率 (Mbps)
    double interferenceDutyCycle = 0.1;    // ★ 新增：黑飞占空比 [0,1]
    
    // ★ 新增参数
    double nakagamiM = 0.0;                // Nakagami-m 衰落系数 (0=禁用)
    uint32_t macMaxRetries = 7;            // MAC层最大重传次数
    double noiseFigure = 7.0;             // 接收端噪声系数 (dB)
    double trafficLoadMbps = 0.2;         // 每节点业务总负载 (Mbps)
    std::string levelName = "Easy";
};
static DifficultyParams g_diffParams;
static Ptr<UniformRandomVariable> g_randVar;
static double g_pathLossExponent = 2.0;

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
    // double dataRateMin = 1.0;             // 最小数据速率(Mbps)
    // double dataRateMax = 11.0;            // 最大数据速率(Mbps)
    double dataRateMin = 6.0;             // 最小数据速率(Mbps) — 802.11a OFDM最低档
    double dataRateMax = 54.0;            // 最大数据速率(Mbps) — 802.11a OFDM最高档
    double rxSensitivity = -90.0;
    
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
    bool enableTDMA = true;
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
    // g_config.minX -= margin; // 移除向负方向的盲目扩充
    // g_config.maxX += margin;
    // g_config.minY -= margin;
    // g_config.maxY += margin;
    // 改为更保守的扩充，且尽量保持在 0 以上 (如果原始轨迹就在 0 以上)
    if (g_config.minX > 0) g_config.minX = std::max(0.0, g_config.minX - margin);
    else g_config.minX -= margin; // 如果本来就是负的，那说明确实需要飞到负区域
    
    g_config.maxX += margin;
    
    if (g_config.minY > 0) g_config.minY = std::max(0.0, g_config.minY - margin);
    else g_config.minY -= margin;

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

struct TDMAManager {
    bool enabled = false;
    
    double slotDuration  = 0.010;   // 每时隙 10ms
    double guardTime     = 0.001;   // 保护间隔 1ms
    double cycleDuration = 0.150;   // 帧周期（自动计算）
    uint32_t numGroups   = 1;       // 空间复用分组数
    double trafficStartTime = 3.0;  // 业务启动时间
    
    // 主时隙分配: nodeId → slotId
    std::map<uint32_t, uint32_t> slotAssignment;
    
    // 发送流
    struct FlowEntry {
        uint32_t dstId;
        Ptr<Socket> socket;
    };
    std::map<uint32_t, std::vector<FlowEntry>> nodeFlows;
    
    // ★ 动态重分配相关字段
    uint32_t basePacketsPerSlot = 4;    // 基准每时隙发包数
    uint32_t minPacketsPerSlot  = 2;    // 最低保底
    uint32_t maxPacketsPerSlot  = 10;   // 单时隙容量上限
    uint32_t bonusPktsPerSlot   = 2;    // 每个bonus时隙的发包数
    
    // per-node 主时隙发包预算（替代全局 packetsPerCycle）
    std::map<uint32_t, uint32_t> perNodePackets;
    
    // bonus 时隙: nodeId → 可额外发送的 slotId 列表
    std::map<uint32_t, std::vector<uint32_t>> bonusSlots;
    
    // QoS 紧迫度: nodeId → [0.0, 1.0]
    std::map<uint32_t, double> urgency;
    
    // 冲突矩阵（空间复用判定用）
    std::vector<std::vector<bool>> conflictMatrix;
    
    // 各时隙当前占用节点列表（用于 bonus 分配时检测冲突）
    std::vector<std::vector<uint32_t>> slotOccupants;
    
    // 重分配控制
    double reallocationInterval = 5.0;   // 重分配间隔 (秒)
    uint32_t lastLinkCount = 0;          // 上次拓扑链路数（检测拓扑剧变）
    
    // 统计
    uint32_t reallocationCount = 0;
    uint32_t recoloringCount   = 0;
};

static TDMAManager g_tdma;
static std::ofstream g_tdmaLog;

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
            Vector vel = mob->GetVelocity();
            double speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
            g_posLog << currentTime << "," << i << "," << pos.x << "," << pos.y << "," << pos.z << ",0," << speed << "\n";
        }
    }
    // 记录黑飞节点 (node_type=1，nodeId 从 1000 起步，前端据此渲染红色敌机)
    for (uint32_t i = 0; i < g_interferenceNodes.GetN(); ++i) {
        Ptr<MobilityModel> mob = g_interferenceNodes.Get(i)->GetObject<MobilityModel>();
        if (mob) {
            Vector pos = mob->GetPosition();
            Vector vel = mob->GetVelocity(); 
            double speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
            
            // 在末尾通过逗号追加 speed
            g_posLog << currentTime << "," << (1000 + i) << "," << pos.x << "," << pos.y << "," << pos.z << ",1," << speed << "\n";
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

// ==================== SINR 计算工具 ====================

/** dBm → 线性功率 (mW) */
inline double dBmToMw(double dBm) {
    return std::pow(10.0, dBm / 10.0);
}

/** 线性功率 (mW) → dBm */
inline double mwToDbm(double mW) {
    return 10.0 * std::log10(std::max(1e-20, mW));
}

/** 路径损耗 (dB)，与物理信道使用相同的指数 */
double CalculatePathLoss(double dist) {
    if (dist < 1.0) dist = 1.0;
    return 46.68 + 10.0 * g_pathLossExponent * std::log10(dist);
}

/**
 * 计算接收端 dstId 处的总干扰功率 (mW)
 *
 * @param dstId       接收节点
 * @param excludeId   排除的发送节点（信号源本身不算干扰）
 * @param channelFilter  -1 = 所有节点都干扰（物理现实：单射频同频）
 *                       >=0 = 只计算该逻辑信道上的节点（多信道规划用）
 */
double CalculateInterference_mW(uint32_t dstId, uint32_t excludeId, 
                                 int channelFilter = -1) {
    double total_mW = 0.0;
    
    // ---- 计算发送方的载波感知范围 ----
    double senderPower = g_state.powerAssignment.count(excludeId) ?
                         g_state.powerAssignment[excludeId] : 20.0;
    // PathLoss(csRange) = senderPower - rxSensitivity
    // 46.68 + 10α·log10(csRange) = senderPower - rxSensitivity
    // csRange = 10^((senderPower - 46.68 - rxSens) / (10α))
    double csRange = std::pow(10.0, 
        (senderPower - 46.68 - g_config.rxSensitivity) / 
        (10.0 * g_pathLossExponent));
    csRange = std::min(csRange, 2000.0);  // 合理上界
    
    // ---- 来自其他 UAV 的干扰：只计隐藏终端 ----
    for (uint32_t k = 0; k < g_uavNodes.GetN(); ++k) {
        if (k == excludeId || k == dstId) continue;
        
        if (channelFilter >= 0) {
            auto it = g_state.channelAssignment.find(k);
            if (it != g_state.channelAssignment.end() && 
                (int)it->second != channelFilter) {
                continue;
            }
        }
        
        // ★ CSMA 感知：节点 k 能否听到发送方?
        double distToSender = CalculateDistance(
            g_uavNodes.Get(k), g_uavNodes.Get(excludeId));
        
        if (distToSender <= csRange) {
            // k 能听到发送方 → CSMA 退避 → 不构成干扰
            continue;
        }
        
        // k 是隐藏终端：无法感知发送方，可能同时发送
        double dist = CalculateDistance(g_uavNodes.Get(k), g_uavNodes.Get(dstId));
        double txK  = g_state.powerAssignment.count(k) ? 
                      g_state.powerAssignment[k] : 20.0;
        double rxK  = txK - CalculatePathLoss(dist);
        
        if (rxK > -100.0) {
            total_mW += dBmToMw(rxK);
        }
    }
    
    // ---- 来自黑飞节点的干扰（同样考虑 CSMA）----
    for (uint32_t k = 0; k < g_interferenceNodes.GetN(); ++k) {
        double distToSender = CalculateDistance(
            g_interferenceNodes.Get(k), g_uavNodes.Get(excludeId));
        
        double dist = CalculateDistance(
            g_interferenceNodes.Get(k), g_uavNodes.Get(dstId));
        double rxK  = 30.0 - CalculatePathLoss(dist);
        
        if (rxK > -100.0) {
            if (distToSender <= csRange) {
                // 黑飞在 CSMA 范围内：大部分时候退避，
                // 但高占空比仍导致 ~10% 碰撞概率
                total_mW += dBmToMw(rxK) * 0.1;
            } else {
                // 黑飞是隐藏终端：完全无法感知发送方
                total_mW += dBmToMw(rxK);
            }
        }
    }
    
    return total_mW;
}

/**
 * 估算链路 src→dst 的 SINR (dB)
 *
 * @param channelFilter  -1 = 物理现实, >=0 = 假设性信道规划
 */
double EstimateSINR(uint32_t srcId, uint32_t dstId, int channelFilter = -1) {
    double dist = CalculateDistance(g_uavNodes.Get(srcId), g_uavNodes.Get(dstId));
    
    // 信号功率
    double txPower     = g_state.powerAssignment.count(srcId) ?
                         g_state.powerAssignment[srcId] : 20.0;
    double rxPower_dBm = txPower - CalculatePathLoss(dist);
    double signal_mW   = dBmToMw(rxPower_dBm);
    
    // 热噪声：20MHz 带宽 @ 290K → -95 dBm
    double noise_mW = dBmToMw(-95.0);
    
    // 干扰
    double interference_mW = CalculateInterference_mW(dstId, srcId, channelFilter);
    
    // SINR = S / (I + N)
    double sinr = signal_mW / (interference_mW + noise_mW);
    return 10.0 * std::log10(std::max(1e-10, sinr));
}

/** SINR (dB) → 802.11a 最大可支持速率 (Mbps) */
double SINRToMaxRate(double sinr_dB) {
    // 基于 802.11a OFDM 调制解调门限（含 ~1dB 实现余量）
    if (sinr_dB >= 25.0) return 54.0;   // 64QAM 3/4
    if (sinr_dB >= 22.0) return 48.0;   // 64QAM 2/3
    if (sinr_dB >= 18.0) return 36.0;   // 16QAM 3/4
    if (sinr_dB >= 14.0) return 24.0;   // 16QAM 1/2
    if (sinr_dB >= 11.0) return 18.0;   // QPSK 3/4
    if (sinr_dB >=  9.0) return 12.0;   // QPSK 1/2
    if (sinr_dB >=  8.0) return  9.0;   // BPSK 3/4
    if (sinr_dB >=  6.0) return  6.0;   // BPSK 1/2
    return 0.0;  // 低于最低解调门限
}

/** 速率 (Mbps) → 所需最低 SINR (dB) */
double RateToMinSINR(double rate) {
    if (rate >= 54.0) return 25.0;
    if (rate >= 48.0) return 22.0;
    if (rate >= 36.0) return 18.0;
    if (rate >= 24.0) return 14.0;
    if (rate >= 18.0) return 11.0;
    if (rate >= 12.0) return  9.0;
    if (rate >=  9.0) return  8.0;
    return 6.0;
}

/**
 * 基于 SINR 的链路质量估计
 * 返回 [0, 1]，其中 0 = 无法解调，1 = 可支持最高速率
 */
double EstimateLinkQuality(uint32_t srcId, uint32_t dstId) {
    double dist = CalculateDistance(g_uavNodes.Get(srcId), g_uavNodes.Get(dstId));
    if (dist > 150.0) return 0.0;
    
    double sinr = EstimateSINR(srcId, dstId);
    
    // SINR 6dB → quality=0 (最低速率勉强可解)
    // SINR 25dB → quality=1 (可支持 54Mbps)
    double quality = (sinr - 6.0) / (25.0 - 6.0);
    return std::max(0.0, std::min(1.0, quality));
}

/**
 * SINR 驱动信道分配算法
 *
 * 改进：用实际干扰功率 (mW) 代替距离权重评分
 * - 距离权重无法区分"远处大功率"和"近处小功率"
 * - 功率级别评分直接反映物理干扰强度
 */
void DynamicChannelAllocation() {
    NS_LOG_INFO("执行 SINR 驱动信道分配...");
    
    uint32_t n = g_uavNodes.GetN();
    g_state.channelAssignment.clear();
    
    // 按度数降序排列（核心节点优先分配）
    std::vector<std::pair<uint32_t, uint32_t>> nodeDegrees;
    for (uint32_t i = 0; i < n; ++i) {
        nodeDegrees.push_back({(uint32_t)g_state.neighbors[i].size(), i});
    }
    std::sort(nodeDegrees.rbegin(), nodeDegrees.rend());
    
    for (auto& [degree, nodeId] : nodeDegrees) {
        // 对每个候选信道计算干扰功率 (mW)
        std::vector<double> channelInterference(g_config.numChannels, 0.0);
        
        // 统计当前各信道负载
        std::vector<uint32_t> channelLoad(g_config.numChannels, 0);
        for (auto& [nid, ch] : g_state.channelAssignment) {
            channelLoad[ch]++;
        }
        
        for (uint32_t ch = 0; ch < g_config.numChannels; ++ch) {
            
            // ---- 一跳同频干扰（主因素）----
            for (uint32_t neighborId : g_state.neighbors[nodeId]) {
                auto it = g_state.channelAssignment.find(neighborId);
                if (it != g_state.channelAssignment.end() && it->second == ch) {
                    double dist = CalculateDistance(
                        g_uavNodes.Get(nodeId), g_uavNodes.Get(neighborId));
                    double txK = g_state.powerAssignment.count(neighborId) ?
                                 g_state.powerAssignment[neighborId] : 20.0;
                    double rxPower = txK - CalculatePathLoss(dist);
                    // ★ 用实际功率级别评分，而非线性距离权重
                    channelInterference[ch] += dBmToMw(rxPower);
                }
            }
            
            // ---- 两跳隐藏终端干扰（次因素，权重 ×0.3）----
            for (uint32_t neighborId : g_state.neighbors[nodeId]) {
                for (uint32_t twoHop : g_state.neighbors[neighborId]) {
                    if (twoHop == nodeId) continue;
                    auto it = g_state.channelAssignment.find(twoHop);
                    if (it != g_state.channelAssignment.end() && it->second == ch) {
                        double dist = CalculateDistance(
                            g_uavNodes.Get(nodeId), g_uavNodes.Get(twoHop));
                        if (dist < 225.0) {  // 1.5 × commRange
                            double txK = g_state.powerAssignment.count(twoHop) ?
                                         g_state.powerAssignment[twoHop] : 20.0;
                            double rxPower = txK - CalculatePathLoss(dist);
                            channelInterference[ch] += dBmToMw(rxPower) * 0.3;
                        }
                    }
                }
            }
            
            // ---- 负载均衡惩罚（统一量纲：用伪干扰功率）----
            channelInterference[ch] += channelLoad[ch] * dBmToMw(-80.0);
        }
        
        // 选择干扰功率最低的信道
        uint32_t bestChannel = 0;
        double minInterference = channelInterference[0];
        for (uint32_t ch = 1; ch < g_config.numChannels; ++ch) {
            if (channelInterference[ch] < minInterference) {
                minInterference = channelInterference[ch];
                bestChannel = ch;
            }
        }
        
        g_state.channelAssignment[nodeId] = bestChannel;
    }
    
    // 日志
    std::vector<uint32_t> finalLoad(g_config.numChannels, 0);
    for (auto& [nid, ch] : g_state.channelAssignment) finalLoad[ch]++;
    std::string loadStr;
    for (uint32_t ch = 0; ch < g_config.numChannels; ++ch) {
        loadStr += "CH" + std::to_string(ch) + "=" + std::to_string(finalLoad[ch]) + " ";
    }
    NS_LOG_INFO("信道分配完成: " << loadStr);
}

/**
 * SINR 驱动功率控制
 *
 * 核心思路：
 *   1. 计算最差链路 SINR
 *   2. 与当前速率所需 SINR 比较
 *   3. SINR 不足 → 提升功率；SINR 过剩 → 降低功率（减少对邻居干扰）
 *   4. QoS 闭环：PDR 低于目标时额外补偿
 */
void DynamicPowerControl() {
    NS_LOG_INFO("执行 SINR 驱动功率控制...");
    
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        if (g_state.neighbors[i].empty()) {
            g_state.powerAssignment[i] = g_config.txPowerMax;
            continue;
        }
        
        // ---- 1. 计算当前最差链路 SINR ----
        double worstSINR = 100.0;
        for (uint32_t neighborId : g_state.neighbors[i]) {
            double sinr = EstimateSINR(i, neighborId);
            worstSINR = std::min(worstSINR, sinr);
        }
        
        // ---- 2. 目标 SINR = 当前速率的最低门限 + 3dB 余量 ----
        double currentRate = g_state.rateAssignment.count(i) ? 
                             g_state.rateAssignment[i] : 6.0;
        double targetSINR  = RateToMinSINR(currentRate) + 3.0;
        
        // ---- 3. SINR 差距 → 功率调整 ----
        double sinrGap     = targetSINR - worstSINR;
        double currentPower = g_state.powerAssignment.count(i) ? 
                              g_state.powerAssignment[i] : 20.0;
        double newPower     = currentPower;
        
        if (sinrGap > 0) {
            newPower += std::min(3.0, sinrGap);
        } else if (sinrGap < -3.0) {
            newPower -= std::min(3.0, (-sinrGap - 3.0) * 0.5);
        }
        
        // ---- 4. QoS 闭环：PDR 低于目标时额外补偿 ----
        double currentTime = Simulator::Now().GetSeconds();
        if (currentTime > 15.0  // ★ 路由收敛保护期
            && g_state.nodePDR.count(i) && g_state.nodePDR[i] > 0.0
            && g_state.nodePDR[i] < g_config.targetPDR) {
            double pdrGap = g_config.targetPDR - g_state.nodePDR[i];
            newPower += std::min(2.0, pdrGap * 5.0);
        }
        
        // ---- 5. 密度调节：邻居过多且 SINR 有余量时降功率 ----
        if (g_state.neighbors[i].size() > 5 && sinrGap < -3.0) {
            newPower -= std::min(1.0, (g_state.neighbors[i].size() - 5) * 0.3);
        }
        
        // 钳位到合法范围
        newPower = std::max(g_config.txPowerMin, std::min(g_config.txPowerMax, newPower));
        g_state.powerAssignment[i] = newPower;
    }
    
    NS_LOG_INFO("功率控制完成");
}

/**
 * SINR 驱动速率调整
 *
 * 直接用 SINR (dB) 查 802.11a 调制解调表得到最大可支持速率
 * 无需中间的 "链路质量" 抽象，避免二次映射误差
 */
void AdaptiveRateControl() {
    NS_LOG_INFO("执行 SINR 驱动速率调整...");

    double currentTime = Simulator::Now().GetSeconds();
    
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        if (g_state.neighbors[i].empty()) {
            g_state.rateAssignment[i] = g_config.dataRateMin;
            continue;
        }
        
        // ---- 1. 计算最差 / 最佳链路 SINR ----
        double worstSINR = 100.0;
        double bestSINR  = -100.0;
        for (uint32_t neighborId : g_state.neighbors[i]) {
            double sinr = EstimateSINR(i, neighborId);
            worstSINR = std::min(worstSINR, sinr);
            bestSINR  = std::max(bestSINR, sinr);
        }
        
        // ---- 2. 加权 SINR（70% 看最差，30% 看最好）----
        double effectiveSINR = 0.7 * worstSINR + 0.3 * bestSINR;
        
        // ---- 3. QoS 闭环惩罚 ----
        // PDR 低于目标 → 降低 effective SINR → 选更低速率 → 提高帧成功率
        // 前15秒路由收敛期内，不做QoS惩罚
        if (currentTime > 15.0) {
            if (g_state.nodePDR.count(i) && g_state.nodePDR[i] > 0.0
                && g_state.nodePDR[i] < g_config.targetPDR) {
                double penalty = (g_config.targetPDR - g_state.nodePDR[i]) * 20.0;
                effectiveSINR -= std::min(10.0, penalty);
            }
            
            if (g_state.nodeDelay.count(i) 
                && g_state.nodeDelay[i] > g_config.maxEndToEndDelay) {
                // ★ 修改：只在PDR也差时才降速率
                if (g_state.nodePDR.count(i) && g_state.nodePDR[i] < 0.7) {
                    effectiveSINR -= 3.0;
                }
            }
        }
        
        // ---- 4. 直接查表得到最大可支持速率 ----
        double dataRate = SINRToMaxRate(effectiveSINR);
        if (dataRate < 6.0) dataRate = 6.0;  // 最低保底
        
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
            double rate = g_state.rateAssignment[i];
            std::string rateMode;
            if      (rate >= 54.0) rateMode = "OfdmRate54Mbps";
            else if (rate >= 48.0) rateMode = "OfdmRate48Mbps";
            else if (rate >= 36.0) rateMode = "OfdmRate36Mbps";
            else if (rate >= 24.0) rateMode = "OfdmRate24Mbps";
            else if (rate >= 18.0) rateMode = "OfdmRate18Mbps";
            else if (rate >= 12.0) rateMode = "OfdmRate12Mbps";
            else if (rate >= 9.0)  rateMode = "OfdmRate9Mbps";
            else                   rateMode = "OfdmRate6Mbps";
            
            stationManager->SetAttribute("DataMode", StringValue(rateMode));
        }
    }
}

/**
 * 空间TDMA分组算法 (Spatial TDMA via Greedy Graph Coloring)
 *
 * 核心思想：
 *   距离 > 2×commRange 的节点不会互相干扰，可以共享同一时隙
 *   → 将节点分组，同组节点同时发送，不同组节点时分复用
 *   → 分组数越少，帧越短，每节点吞吐量越高
 *
 * 算法：
 *   1. 构建冲突图：距离 < conflictRange 的节点有边
 *   2. 贪心图着色：度数大的节点优先分配颜色
 *   3. 颜色数 = 时隙数 = 分组数
 *
 * 示例：15个节点紧密编队 → ~15组（退化为纯TDMA）
 *        15个节点分散部署 → ~5组（3倍吞吐提升）
 */
void ComputeTDMASlots() {
    uint32_t n = g_uavNodes.GetN();
    double conflictRange = 300.0;  // 2 × commRange(150m)
    
    NS_LOG_INFO("计算 TDMA 空间复用分组 (冲突距离=" << conflictRange << "m)...");
    
    // ---- 1. 构建冲突邻接矩阵 ----
    std::vector<std::vector<bool>> conflict(n, std::vector<bool>(n, false));
    std::vector<uint32_t> conflictDegree(n, 0);
    
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = i + 1; j < n; ++j) {
            double dist = CalculateDistance(g_uavNodes.Get(i), g_uavNodes.Get(j));
            if (dist < conflictRange) {
                conflict[i][j] = conflict[j][i] = true;
                conflictDegree[i]++;
                conflictDegree[j]++;
            }
        }
    }
    
    // ---- 2. 按冲突度降序排列（高度数节点优先着色）----
    std::vector<std::pair<uint32_t, uint32_t>> nodeOrder;  // (degree, nodeId)
    for (uint32_t i = 0; i < n; ++i) {
        nodeOrder.push_back({conflictDegree[i], i});
    }
    std::sort(nodeOrder.rbegin(), nodeOrder.rend());
    
    // ---- 3. 贪心图着色 ----
    std::vector<int> color(n, -1);
    uint32_t numColors = 0;
    
    for (auto& [deg, nodeId] : nodeOrder) {
        // 收集冲突邻居已用的颜色
        std::set<int> usedColors;
        for (uint32_t j = 0; j < n; ++j) {
            if (conflict[nodeId][j] && color[j] >= 0) {
                usedColors.insert(color[j]);
            }
        }
        
        // 找最小可用颜色
        int c = 0;
        while (usedColors.count(c)) c++;
        
        color[nodeId] = c;
        if ((uint32_t)(c + 1) > numColors) numColors = c + 1;
    }
    
    // ---- 4. 至少1组 ----
    if (numColors == 0) numColors = 1;
    
    // ---- 5. 存储结果 ----
    g_tdma.numGroups = numColors;
    g_tdma.cycleDuration = numColors * g_tdma.slotDuration;
    
    for (uint32_t i = 0; i < n; ++i) {
        g_tdma.slotAssignment[i] = (uint32_t)color[i];
    }
    
    // ---- 6. 计算每周期发包数 ----
    // 应用层速率: 100Kbps × 2流 = 200Kbps/节点
    // 每周期数据量: 200000 × cycleDuration / 8  (字节)
    // 每周期包数: ceil(数据量 / packetSize)
    double dataPerCycle = g_diffParams.trafficLoadMbps * 1e6 * g_tdma.cycleDuration / 8.0;
    g_tdma.basePacketsPerSlot = (uint32_t)std::ceil(dataPerCycle / g_config.packetSize);
    g_tdma.basePacketsPerSlot = std::max(g_tdma.basePacketsPerSlot, g_tdma.minPacketsPerSlot);
    g_tdma.basePacketsPerSlot = std::min(g_tdma.basePacketsPerSlot, g_tdma.maxPacketsPerSlot);

    // ---- 7. 输出分组结果 ----
    std::cout << "TDMA 空间复用分组完成:" << std::endl;
    std::cout << "  分组数(时隙数): " << numColors << std::endl;
    std::cout << "  帧周期: " << g_tdma.cycleDuration * 1000.0 << " ms" << std::endl;
    std::cout << "  基准发包: " << g_tdma.basePacketsPerSlot << " 包/节点/时隙" << std::endl;
    
    double compressionRatio = (double)n / numColors;
    std::cout << "  空间复用增益: " << compressionRatio << "x "
              << "(纯TDMA需 " << n << " 时隙，空间TDMA仅需 " << numColors << " 时隙)" << std::endl;
    
    for (uint32_t g = 0; g < numColors; ++g) {
        std::cout << "  Slot " << g << ": [";
        bool first = true;
        for (uint32_t i = 0; i < n; ++i) {
            if (g_tdma.slotAssignment[i] == g) {
                if (!first) std::cout << ", ";
                std::cout << "UAV" << i;
                first = false;
            }
        }
        std::cout << "]" << std::endl;
    }
    
    // ---- 8. 构建冲突矩阵和时隙占用表（供动态重分配使用）----
    g_tdma.conflictMatrix = conflict;
    
    g_tdma.slotOccupants.clear();
    g_tdma.slotOccupants.resize(numColors);
    for (uint32_t i = 0; i < n; ++i) {
        g_tdma.slotOccupants[color[i]].push_back(i);
    }
    
    // 初始化 per-node 包预算为基准值
    for (uint32_t i = 0; i < n; ++i) {
        g_tdma.perNodePackets[i] = g_tdma.basePacketsPerSlot;
        g_tdma.bonusSlots[i].clear();
        g_tdma.urgency[i] = 0.0;
    }
    
    g_tdma.lastLinkCount = 0;
    for (uint32_t i = 0; i < n; ++i) {
        g_tdma.lastLinkCount += g_state.neighbors[i].size();
    }
    g_tdma.lastLinkCount /= 2;
}

// ==================== 动态 TDMA 重分配 ====================

/**
 * 计算节点 QoS 紧迫度
 *
 * urgency = 0.0  → QoS 完全满足，可以让出资源
 * urgency = 1.0  → QoS 严重不达标，急需更多资源
 *
 * 综合考虑：PDR差距(70%), 时延超标(20%), 吞吐不足(10%)
 */
double ComputeQoSUrgency(uint32_t nodeId) {
    double urgency = 0.0;
    
    // ---- PDR 维度 (权重 70%) ----
    if (g_state.nodePDR.count(nodeId) && g_state.nodePDR[nodeId] > 0.0) {
        double pdrGap = g_config.targetPDR - g_state.nodePDR[nodeId];
        if (pdrGap > 0) {
            // PDR 差距归一化：差 0.85 → urgency=1.0
            urgency += std::min(1.0, pdrGap / g_config.targetPDR) * 0.70;
        }
    } else {
        // 尚无 PDR 数据（刚启动），给一个中等紧迫度
        urgency += 0.3;
    }
    
    // ---- 时延维度 (权重 20%) ----
    if (g_state.nodeDelay.count(nodeId) && g_state.nodeDelay[nodeId] > 0.0) {
        double delayRatio = g_state.nodeDelay[nodeId] / g_config.maxEndToEndDelay;
        if (delayRatio > 1.0) {
            urgency += std::min(1.0, (delayRatio - 1.0)) * 0.20;
        }
    }
    
    // ---- 吞吐量维度 (权重 10%) ----
    if (g_state.nodeThroughput.count(nodeId)) {
        double tputRatio = g_state.nodeThroughput[nodeId] / g_config.minThroughput;
        if (tputRatio < 1.0) {
            urgency += (1.0 - tputRatio) * 0.10;
        }
    }
    
    return std::max(0.0, std::min(1.0, urgency));
}

/**
 * 检查节点 nodeId 是否可以安全使用 slotId 作为 bonus 时隙
 *
 * 条件：该时隙内所有已有占用者都与 nodeId 不冲突（空间隔离）
 */
bool IsSlotCompatible(uint32_t nodeId, uint32_t slotId) {
    uint32_t n = g_uavNodes.GetN();
    
    if (slotId >= g_tdma.slotOccupants.size()) return false;
    
    // 不能是自己的主时隙（已经在发了）
    if (g_tdma.slotAssignment[nodeId] == slotId) return false;
    
    // 检查与该时隙所有占用者（主时隙 + 已分配的 bonus 节点）的冲突
    for (uint32_t occupant : g_tdma.slotOccupants[slotId]) {
        if (occupant >= n || nodeId >= n) continue;
        if (g_tdma.conflictMatrix[nodeId][occupant]) {
            return false;  // 存在空间冲突
        }
    }
    
    // 检查与已获得此 bonus 时隙的其他节点的冲突
    for (auto& [otherId, otherBonus] : g_tdma.bonusSlots) {
        if (otherId == nodeId) continue;
        for (uint32_t bs : otherBonus) {
            if (bs == slotId && nodeId < n && otherId < n) {
                if (g_tdma.conflictMatrix[nodeId][otherId]) {
                    return false;
                }
            }
        }
    }
    
    return true;
}

/**
 * 动态 TDMA 重分配主函数
 *
 * 调用频率：每 reallocationInterval 秒（默认 5s）
 *
 * 三层调节机制：
 *   层1: 主时隙包预算调节（快速，每次调用）
 *        → QoS差的节点在自己的时隙内发更多包
 *   层2: Bonus 时隙分配/回收（中速，每次调用）
 *        → QoS严重不达标的节点借用空闲时隙
 *   层3: 空间复用重着色（慢速，仅拓扑剧变时）
 *        → 重新计算冲突图和分组方案
 */
void DynamicTDMAReallocation() {
    if (!g_tdma.enabled) return;
    
    double currentTime = Simulator::Now().GetSeconds();
    uint32_t n = g_uavNodes.GetN();
    
    NS_LOG_INFO("时间 " << currentTime << "s: 执行动态 TDMA 重分配 (#" 
                << g_tdma.reallocationCount << ")");
    g_tdma.reallocationCount++;
    
    // ============ 层3: 拓扑剧变检测 → 重着色 ============
    uint32_t currentLinks = 0;
    for (uint32_t i = 0; i < n; ++i) {
        currentLinks += g_state.neighbors[i].size();
    }
    currentLinks /= 2;
    
    bool needRecolor = false;
    if (g_tdma.lastLinkCount > 0) {
        double linkChange = std::abs((double)currentLinks - (double)g_tdma.lastLinkCount) 
                           / g_tdma.lastLinkCount;
        if (linkChange > 0.20) {
            needRecolor = true;
            NS_LOG_INFO("  拓扑链路变化 " << (linkChange*100) << "% > 20%，触发重着色");
        }
    }
    
    if (needRecolor) {
        g_tdma.recoloringCount++;
        ComputeTDMASlots();  // 重新着色（内部会重置 perNodePackets 和 bonusSlots）
        g_tdma.lastLinkCount = currentLinks;
        
        // 重着色后跳过本轮的预算调节，让新分组先稳定一个周期
        if (g_tdmaLog.is_open()) {
            g_tdmaLog << currentTime << ",RECOLOR,"
                      << g_tdma.numGroups << ","
                      << g_tdma.cycleDuration * 1000.0 << "\n";
        }
        
        Simulator::Schedule(Seconds(g_tdma.reallocationInterval), 
                           &DynamicTDMAReallocation);
        return;
    }
    g_tdma.lastLinkCount = currentLinks;
    
    // ============ 计算全节点 QoS 紧迫度 ============
    double totalUrgency = 0.0;
    uint32_t urgentCount = 0;
    uint32_t satisfiedCount = 0;
    
    for (uint32_t i = 0; i < n; ++i) {
        g_tdma.urgency[i] = ComputeQoSUrgency(i);
        totalUrgency += g_tdma.urgency[i];
        if (g_tdma.urgency[i] > 0.3) urgentCount++;
        if (g_tdma.urgency[i] < 0.05) satisfiedCount++;
    }
    
    NS_LOG_INFO("  QoS 评估: 紧急=" << urgentCount 
                << " 满足=" << satisfiedCount
                << " 平均紧迫度=" << (n > 0 ? totalUrgency/n : 0));
    
    // ============ 层1: 主时隙包预算调节 ============
    // 策略：总预算守恒（不增加总流量，只在节点间重新分配）
    //   总预算 = n × basePacketsPerSlot
    //   每节点预算 = base × (1 + urgency × boostFactor) → 然后归一化
    
    uint32_t totalBudget = n * g_tdma.basePacketsPerSlot;
    
    // 计算原始权重
    std::vector<double> rawBudget(n);
    double budgetSum = 0.0;
    double boostFactor = 2.0;  // 最紧急节点可获得 3× 基准
    
    for (uint32_t i = 0; i < n; ++i) {
        rawBudget[i] = 1.0 + g_tdma.urgency[i] * boostFactor;
        budgetSum += rawBudget[i];
    }
    
    // 归一化使总预算守恒
    for (uint32_t i = 0; i < n; ++i) {
        double normalized = rawBudget[i] / budgetSum * totalBudget;
        uint32_t pkts = (uint32_t)std::round(normalized);
        pkts = std::max(g_tdma.minPacketsPerSlot, 
                        std::min(g_tdma.maxPacketsPerSlot, pkts));
        g_tdma.perNodePackets[i] = pkts;
    }
    
    // ============ 层2: Bonus 时隙分配/回收 ============
    
    // 2a. 回收：QoS 恢复正常的节点释放 bonus 时隙
    for (uint32_t i = 0; i < n; ++i) {
        if (g_tdma.urgency[i] < 0.10 && !g_tdma.bonusSlots[i].empty()) {
            NS_LOG_INFO("  节点 " << i << " QoS 恢复，回收 " 
                        << g_tdma.bonusSlots[i].size() << " 个 bonus 时隙");
            g_tdma.bonusSlots[i].clear();
        }
    }
    
    // 2b. 分配：QoS 严重不达标的节点尝试获取 bonus 时隙
    // 按紧迫度降序排列，优先保障最差的节点
    std::vector<std::pair<double, uint32_t>> urgencyRank;
    for (uint32_t i = 0; i < n; ++i) {
        urgencyRank.push_back({g_tdma.urgency[i], i});
    }
    std::sort(urgencyRank.rbegin(), urgencyRank.rend());
    
    uint32_t maxBonusSlotsPerNode = 2;  // 每节点最多 2 个 bonus 时隙
    
    for (auto& [urg, nodeId] : urgencyRank) {
        // 只给紧迫度 > 0.4 的节点分配 bonus
        if (urg < 0.40) break;
        
        // 已有足够 bonus 时隙
        if (g_tdma.bonusSlots[nodeId].size() >= maxBonusSlotsPerNode) continue;
        
        // 遍历所有时隙，寻找兼容的
        for (uint32_t slotId = 0; slotId < g_tdma.numGroups; ++slotId) {
            if (g_tdma.bonusSlots[nodeId].size() >= maxBonusSlotsPerNode) break;
            
            if (IsSlotCompatible(nodeId, slotId)) {
                g_tdma.bonusSlots[nodeId].push_back(slotId);
                NS_LOG_INFO("  节点 " << nodeId << " (urgency=" << urg 
                            << ") 获得 bonus 时隙 " << slotId);
            }
        }
    }
    
    // ============ 日志记录 ============
    if (g_tdmaLog.is_open()) {
        for (uint32_t i = 0; i < n; ++i) {
            std::string bonusStr = "";
            for (uint32_t bs : g_tdma.bonusSlots[i]) {
                if (!bonusStr.empty()) bonusStr += ";";
                bonusStr += std::to_string(bs);
            }
            if (bonusStr.empty()) bonusStr = "none";
            
            g_tdmaLog << currentTime << ","
                      << i << ","
                      << g_tdma.slotAssignment[i] << ","
                      << g_tdma.numGroups << ","
                      << g_tdma.perNodePackets[i] << ","
                      << bonusStr << ","
                      << g_tdma.urgency[i] << "\n";
        }
    }
    
    // 调度下次重分配
    Simulator::Schedule(Seconds(g_tdma.reallocationInterval), 
                       &DynamicTDMAReallocation);
}

/**
 * 发送单个 TDMA 数据包
 */
void SendTDMAPacket(uint32_t nodeId, uint32_t flowIdx) {
    auto it = g_tdma.nodeFlows.find(nodeId);
    if (it == g_tdma.nodeFlows.end()) return;
    
    auto& flows = it->second;
    if (flowIdx >= flows.size()) return;
    
    Ptr<Packet> pkt = Create<Packet>((uint32_t)g_config.packetSize);
    int sent = flows[flowIdx].socket->Send(pkt);
    
    // 记录发送事件（供前端可视化）
    if (sent > 0 && g_transLog.is_open()) {
        g_transLog << Simulator::Now().GetSeconds() << "," 
                   << nodeId << ",TX_TDMA\n";
    }
}

/**
 * Bonus 时隙突发发送
 *
 * 在借用的 bonus 时隙内发送额外数据包
 * 每帧由 TDMABurstSend 调度，非自递归（避免旧 bonus 泄漏）
 */
void TDMABonusBurst(uint32_t nodeId, uint32_t bonusSlotId) {
    // 安全检查：确认 bonus 时隙仍然有效（可能已被回收）
    auto it = g_tdma.bonusSlots.find(nodeId);
    if (it == g_tdma.bonusSlots.end()) return;
    auto& slots = it->second;
    bool stillValid = false;
    for (uint32_t s : slots) {
        if (s == bonusSlotId) { stillValid = true; break; }
    }
    if (!stillValid) return;
    
    // 发送 bonusPktsPerSlot 个包
    auto flowIt = g_tdma.nodeFlows.find(nodeId);
    if (flowIt == g_tdma.nodeFlows.end() || flowIt->second.empty()) return;
    
    uint32_t numFlows = (uint32_t)flowIt->second.size();
    uint32_t totalPkts = g_tdma.bonusPktsPerSlot;
    double effectiveSlot = g_tdma.slotDuration - 2.0 * g_tdma.guardTime;
    double pktInterval = (totalPkts > 1) ? effectiveSlot / (totalPkts - 1) : 0.0;
    
    for (uint32_t p = 0; p < totalPkts; ++p) {
        double offset = g_tdma.guardTime + p * pktInterval;
        uint32_t flowIdx = p % numFlows;
        Simulator::Schedule(Seconds(offset), &SendTDMAPacket, nodeId, flowIdx);
    }
    
    // 记录 bonus 发送事件
    if (g_transLog.is_open()) {
        g_transLog << Simulator::Now().GetSeconds() << "," 
                   << nodeId << ",TX_BONUS_SLOT" << bonusSlotId << "\n";
    }
}

/**
 * TDMA 时隙突发发送
 *
 * 在分配给该节点的时隙内，集中发送所有累积数据包
 * 发送完毕后自动调度下一帧的突发
 *
 * 时序示意:
 *   |<--------- cycleDuration --------->|
 *   | Slot0 | Slot1 | ... | Slot(G-1)  |
 *   |  ↑ 节点A在此发送突发包             |
 *   |       |  ↑ 节点B在此发送            |
 *   |                                   |
 *   └─── 自动调度到下一帧的同一时隙 ──────┘
 */
/**
 * 主时隙突发发送 + Bonus 时隙调度
 *
 * 每帧周期调用一次，完成两件事：
 *   1. 在主时隙内发送 perNodePackets[nodeId] 个包
 *   2. 如果有 bonus 时隙，调度 TDMABonusBurst
 */
void TDMABurstSend(uint32_t nodeId) {
    if (Simulator::Now().GetSeconds() >= g_config.duration - 0.5) return;
    
    auto it = g_tdma.nodeFlows.find(nodeId);
    if (it == g_tdma.nodeFlows.end() || it->second.empty()) {
        Simulator::Schedule(Seconds(g_tdma.cycleDuration), &TDMABurstSend, nodeId);
        return;
    }
    
    // ---- 1. 主时隙突发 ----
    auto& flows = it->second;
    uint32_t numFlows = (uint32_t)flows.size();
    
    // ★ 使用 per-node 动态包预算
    uint32_t totalPkts = g_tdma.perNodePackets.count(nodeId) ? 
                         g_tdma.perNodePackets[nodeId] : g_tdma.basePacketsPerSlot;
    
    double effectiveSlot = g_tdma.slotDuration - 2.0 * g_tdma.guardTime;
    double pktInterval = (totalPkts > 1) ? effectiveSlot / (totalPkts - 1) : 0.0;
    
    for (uint32_t p = 0; p < totalPkts; ++p) {
        double offset = g_tdma.guardTime + p * pktInterval;
        uint32_t flowIdx = p % numFlows;
        Simulator::Schedule(Seconds(offset), &SendTDMAPacket, nodeId, flowIdx);
    }
    
    // ---- 2. 调度 Bonus 时隙突发 ----
    auto bonusIt = g_tdma.bonusSlots.find(nodeId);
    if (bonusIt != g_tdma.bonusSlots.end()) {
        uint32_t mySlot = g_tdma.slotAssignment[nodeId];
        
        for (uint32_t bonusSlotId : bonusIt->second) {
            // 计算 bonus 时隙相对于当前主时隙的时间偏移
            double delay;
            if (bonusSlotId > mySlot) {
                delay = (bonusSlotId - mySlot) * g_tdma.slotDuration;
            } else {
                // 在帧内更早的位置 → 等到下一帧的该位置
                // 不过由于 TDMABurstSend 在主时隙开始时触发，
                // 更早的 bonus 已经过了，需要绕一圈
                delay = (g_tdma.numGroups - mySlot + bonusSlotId) * g_tdma.slotDuration;
            }
            
            Simulator::Schedule(Seconds(delay), &TDMABonusBurst, nodeId, bonusSlotId);
        }
    }
    
    // ---- 3. 调度下一帧的主时隙突发 ----
    Simulator::Schedule(Seconds(g_tdma.cycleDuration), &TDMABurstSend, nodeId);
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
            DynamicChannelAllocation();
            AdaptiveRateControl();
            DynamicPowerControl();
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
                     
        double interference_mW = CalculateInterference_mW(i, i);
        double interference_dBm = mwToDbm(interference_mW + 1e-20);
        
        double worstSINR = 0.0;
        if (!g_state.neighbors[i].empty()) {
            worstSINR = 100.0;
            for (uint32_t neighborId : g_state.neighbors[i]) {
                worstSINR = std::min(worstSINR, EstimateSINR(i, neighborId));
            }
        }
        
        g_resourceDetailedLog << currentTime << ","
                              << i << ","
                              << g_state.channelAssignment[i] << ","
                              << g_state.powerAssignment[i] << ","
                              << g_state.rateAssignment[i] << ","
                              << g_state.neighbors[i].size() << ","
                              << interference_dBm << ","
                              << worstSINR << "\n";
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
            uint32_t perFlowKbps = (uint32_t)(g_diffParams.trafficLoadMbps * 1000.0 / 2.0);
            perFlowKbps = std::max(perFlowKbps, (uint32_t)10);  // 最低 10kbps
            std::string flowRateStr = std::to_string(perFlowKbps) + "kb/s";
            onoff.SetAttribute("DataRate", DataRateValue(DataRate(flowRateStr)));

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

/**
 * 设置 TDMA 调度的业务流量
 *
 * 与 SetupMixedTraffic 的区别：
 *   - 发送端使用原始 Socket API + 定时突发，而非 OnOff 连续流
 *   - 只在分配的时隙内发送，消除 UAV 集群内部碰撞
 *   - 接收端仍使用 PacketSink（始终监听）
 *
 * 流量拓扑保持一致：每节点2条流
 *   流1: i → (i+1)%n  (近距离飞控)
 *   流2: i → (i+n/2)%n (远程图传)
 */
void SetupTDMATraffic() {
    NS_LOG_INFO("设置 TDMA 调度业务流量...");
    
    uint32_t n = g_uavNodes.GetN();
    uint16_t port = 9000;
    
    // ---- 1. 计算空间复用分组 ----
    ComputeTDMASlots();
    
    // ---- 2. 为每个节点创建发送 Socket 和接收 PacketSink ----
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t dst1 = (i + 1) % n;
        uint32_t dst2 = (i + n / 2) % n;
        if (dst2 == i) dst2 = (i + 2) % n;
        
        uint32_t dsts[2] = {dst1, dst2};
        
        for (int k = 0; k < 2; ++k) {
            uint32_t j = dsts[k];
            
            // 接收端: PacketSink（始终监听，无需TDMA控制）
            Ptr<Ipv4> dstIpv4 = g_uavNodes.Get(j)->GetObject<Ipv4>();
            Ipv4Address dstAddr = dstIpv4->GetAddress(1, 0).GetLocal();
            
            PacketSinkHelper sink("ns3::UdpSocketFactory",
                                  InetSocketAddress(Ipv4Address::GetAny(), port));
            ApplicationContainer sinkApp = sink.Install(g_uavNodes.Get(j));
            sinkApp.Start(Seconds(0.5));
            sinkApp.Stop(Seconds(g_config.duration));
            
            // 发送端: 原始 UDP Socket
            Ptr<Socket> socket = Socket::CreateSocket(
                g_uavNodes.Get(i), UdpSocketFactory::GetTypeId());
            socket->Bind();
            socket->Connect(InetSocketAddress(dstAddr, port));
            
            // 记录到 TDMA 管理器
            TDMAManager::FlowEntry flow;
            flow.dstId  = j;
            flow.socket = socket;
            g_tdma.nodeFlows[i].push_back(flow);
            
            port++;
        }
    }
    
    // ---- 3. 调度每个节点的首次突发 ----
    // 每个节点在自己的时隙起始时刻开始第一次突发
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t mySlot = g_tdma.slotAssignment[i];
        double firstBurstTime = g_tdma.trafficStartTime + mySlot * g_tdma.slotDuration;
        
        Simulator::Schedule(Seconds(firstBurstTime), &TDMABurstSend, i);
    }

    //   在业务启动后等待一段时间收集 QoS 数据再开始调节
    double firstReallocationTime = g_tdma.trafficStartTime + g_tdma.reallocationInterval;
    Simulator::Schedule(Seconds(firstReallocationTime), &DynamicTDMAReallocation);
    NS_LOG_INFO("  动态 TDMA 重分配已启用: 首次触发 @" << firstReallocationTime 
                << "s, 间隔 " << g_tdma.reallocationInterval << "s");
    
    // ---- 4. 写入 TDMA 调度日志（初始状态）----
    if (g_tdmaLog.is_open()) {
        for (uint32_t i = 0; i < n; ++i) {
            std::string bonusStr = "none";
            g_tdmaLog << 0.0 << "," 
                      << i << ","
                      << g_tdma.slotAssignment[i] << ","
                      << g_tdma.numGroups << ","
                      << g_tdma.perNodePackets[i] << ","
                      << bonusStr << ","
                      << g_tdma.urgency[i] << "\n";
        }
    }
    
    NS_LOG_INFO("TDMA 业务设置完成: " << n << " 节点, " 
                << n * 2 << " 条流, "
                << g_tdma.numGroups << " 个时隙/帧, "
                << "帧周期 " << g_tdma.cycleDuration * 1000.0 << "ms, "
                << "基准 " << g_tdma.basePacketsPerSlot << " 包/时隙");
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
            // 使用动态计算的场景边界，并强制限制在非负区域 (0,0) 以上，防止生成到负半轴
            double safeMinX = std::max(0.0, g_config.minX + margin);
            double safeMinY = std::max(0.0, g_config.minY + margin);
            double safeMaxX = std::max(safeMinX + 1.0, g_config.maxX - margin); // 确保 max > min
            double safeMaxY = std::max(safeMinY + 1.0, g_config.maxY - margin);

            curX = rng->GetValue(safeMinX, safeMaxX);
            curY = rng->GetValue(safeMinY, safeMaxY);
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
        double rateMbps = g_diffParams.interferenceRateMbps;
        double onTime   = std::max(0.01, std::min(0.99, g_diffParams.interferenceDutyCycle));
        double offTime  = 1.0 - onTime;
        
        // 速率字符串
        std::string dataRate;
        if (rateMbps >= 1.0) {
            dataRate = std::to_string((int)rateMbps) + "Mbps";
        } else {
            dataRate = std::to_string((int)(rateMbps * 1000)) + "kbps";
        }
        
        // 包大小随速率适配
        uint32_t pktSize = 512;
        if (rateMbps >= 4.0) pktSize = 1300;
        if (rateMbps >= 6.0) pktSize = 1472;
        
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
    cmd.AddValue("tdmaInterval", "TDMA 重分配间隔(秒)", g_tdma.reallocationInterval);

    double customPathLossExp   = 2.0;
    double customRxSensitivity = -90.0;
    double customTxPower       = 23.0;
    
    cmd.AddValue("nakagamiM",     "Nakagami-m 衰落系数 (0=禁用, 3.0=强LOS, 0.2=极度散射)",
                 g_diffParams.nakagamiM);
    cmd.AddValue("pathLossExp",   "路径损耗指数 (2.0=自由空间, 3.5=城市密集)",
                 customPathLossExp);
    cmd.AddValue("macRetries",    "MAC层最大重传次数 (0=无重传, 10=高容错)",
                 g_diffParams.macMaxRetries);
    cmd.AddValue("rxSens",        "接收灵敏度 dBm (-93=高灵敏, -75=低灵敏)",
                 customRxSensitivity);
    cmd.AddValue("noiseFigure",   "噪声系数 dB (6=理想, 20=恶劣)",
                 g_diffParams.noiseFigure);
    cmd.AddValue("txPower",       "发射功率 dBm",
                 customTxPower);
    cmd.AddValue("rtkNoise",      "RTK基础噪声标准差 (米)",
                 g_diffParams.rtkNoiseStdDev);
    cmd.AddValue("rtkDriftMag",   "RTK漂移幅度 (米, 0=无漂移)",
                 g_diffParams.rtkDriftMagnitude);
    cmd.AddValue("rtkDriftInt",   "RTK漂移周期 (秒, 0=无漂移)",
                 g_diffParams.rtkDriftInterval);
    cmd.AddValue("rtkDriftDur",   "RTK漂移持续时间 (秒)",
                 g_diffParams.rtkDriftDuration);
    cmd.AddValue("trafficLoad",   "每节点业务总负载 Mbps (0.1=轻载, 7.0=重载)",
                 g_diffParams.trafficLoadMbps);
    cmd.AddValue("numInterfere",  "黑飞干扰节点数量",
                 g_diffParams.numInterferenceNodes);
    cmd.AddValue("interfereRate", "黑飞发射速率 Mbps",
                 g_diffParams.interferenceRateMbps);
    cmd.AddValue("interfereDuty", "黑飞占空比 (0.0~1.0)",
                 g_diffParams.interferenceDutyCycle);
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
    std::cout << "分配策略: " << g_config.allocationStrategy << std::endl;
    std::cout << "速率范围: [" << g_config.dataRateMin << ", " << g_config.dataRateMax << "] Mbps" << std::endl;
    std::cout << "MAC调度: " << (g_config.enableTDMA ? "软TDMA (空间复用)" : "CSMA/CA (标准竞争)") << std::endl; 
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

    g_tdmaLog.open(g_config.outputDir + "/tdma_schedule.csv");
    g_tdmaLog << "time,node_id,slot_id,num_groups,packets_per_slot,bonus_slots,urgency\n";
    
    // 写入CSV表头
    g_posLog << "time,nodeId,x,y,z,node_type,speed\n";
    g_transLog << "time,nodeId,eventType\n";
    g_resourceDetailedLog << "time,node_id,channel,tx_power,data_rate,neighbors,interference_dBm,worst_sinr_dB\n";
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
    g_pathLossExponent = pathLossExp;
    double rxSensitivity = -90.0;  // Easy 默认 (更灵敏)
    g_config.rxSensitivity = rxSensitivity;
    double txPower       = 23.0;   // 23 dBm
    // Phase 4: 初始化难度参数
    g_diffParams.levelName = difficulty;

    if (difficulty == "Custom") {
        // Custom 模式：直接使用 CLI 传入的值（不覆盖）
        pathLossExp   = customPathLossExp;
        g_pathLossExponent = pathLossExp;
        rxSensitivity = customRxSensitivity;
        g_config.rxSensitivity = rxSensitivity;
        txPower       = customTxPower;
        g_diffParams.enableInterference = (g_diffParams.numInterferenceNodes > 0);
        
        std::cout << "=== Custom 模式参数 ===" << std::endl;
        std::cout << "  Nakagami-m:     " << g_diffParams.nakagamiM << std::endl;
        std::cout << "  PathLossExp:    " << pathLossExp << std::endl;
        std::cout << "  MAC重传:        " << g_diffParams.macMaxRetries << std::endl;
        std::cout << "  RxSensitivity:  " << rxSensitivity << " dBm" << std::endl;
        std::cout << "  NoiseFigure:    " << g_diffParams.noiseFigure << " dB" << std::endl;
        std::cout << "  TxPower:        " << txPower << " dBm" << std::endl;
        std::cout << "  RTK噪声σ:      " << g_diffParams.rtkNoiseStdDev << " m" << std::endl;
        std::cout << "  RTK漂移:        " << g_diffParams.rtkDriftMagnitude << " m / " 
                  << g_diffParams.rtkDriftInterval << " s" << std::endl;
        std::cout << "  业务负载:       " << g_diffParams.trafficLoadMbps << " Mbps/节点" << std::endl;
        std::cout << "  黑飞节点:       " << g_diffParams.numInterferenceNodes 
                  << " × " << g_diffParams.interferenceRateMbps << "Mbps @ "
                  << (g_diffParams.interferenceDutyCycle * 100) << "%" << std::endl;
        std::cout << "========================" << std::endl;
        
    } else if (difficulty == "Moderate") {
        // pathLossExp   = 2.7;
        // rxSensitivity = -82.0;
        // txPower       = 23.0;

        pathLossExp   = 2.5;     // 从 2.7 微调到 2.5
        g_pathLossExponent = pathLossExp;
        rxSensitivity = -85.0;   // 从 -82 改为 -85
        g_config.rxSensitivity = rxSensitivity;
        txPower       = 23.0;
        
        g_diffParams.rtkNoiseStdDev = 0.08;
        g_diffParams.rtkDriftInterval = 15.0;
        g_diffParams.rtkDriftDuration = 4.0;
        g_diffParams.rtkDriftMagnitude = 0.5;
        g_diffParams.enableInterference = true;
        g_diffParams.numInterferenceNodes = 8;

        g_diffParams.nakagamiM = 0.7;
        g_diffParams.macMaxRetries = 1;
        g_diffParams.noiseFigure = 15.0;
        g_diffParams.trafficLoadMbps = 2.8;
        g_diffParams.interferenceRateMbps = 4.0;
        g_diffParams.interferenceDutyCycle = 0.7;
    } else if (difficulty == "Hard") {
        // pathLossExp   = 3.5;   // 对齐 benchmark Hard: 3.5, 4.2, 5 递进
        // rxSensitivity = -74.0; // 信号更难被接收
        // txPower       = 26.0;  // 适当增大发射功率以补偿损耗

        pathLossExp   = 3.0;     // 从 3.5 降到 3.0（城市环境合理值）
        g_pathLossExponent = pathLossExp;
        rxSensitivity = -82.0;   // 从 -74 改为 -82（仍比 Easy 的 -90 差）
        g_config.rxSensitivity = rxSensitivity;
        txPower       = 26.0;    // 不变
        
        g_diffParams.rtkNoiseStdDev = 0.2;
        g_diffParams.rtkDriftInterval = 8.0;
        g_diffParams.rtkDriftDuration = 6.0;
        g_diffParams.rtkDriftMagnitude = 1.0;
        g_diffParams.enableInterference = true;
        g_diffParams.numInterferenceNodes = 15;

        g_diffParams.nakagamiM = 0.2;
        g_diffParams.macMaxRetries = 0;
        g_diffParams.noiseFigure = 20.0;
        g_diffParams.trafficLoadMbps = 7.0;
        g_diffParams.interferenceRateMbps = 6.0;
        g_diffParams.interferenceDutyCycle = 0.95;
    } else {
        g_diffParams.nakagamiM = 0.0;           // 不加衰落
        g_diffParams.macMaxRetries = 7;          // ns-3 默认
        g_diffParams.noiseFigure = 7.0;          // ns-3 默认
        g_diffParams.trafficLoadMbps = 0.2;      // 保持现有
        g_diffParams.interferenceRateMbps = 0.5;
        g_diffParams.interferenceDutyCycle = 0.1;
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

    if (g_diffParams.nakagamiM > 0.0) {
        wifiChannel.AddPropagationLoss("ns3::NakagamiPropagationLossModel",
                                        "m0", DoubleValue(g_diffParams.nakagamiM),
                                        "m1", DoubleValue(g_diffParams.nakagamiM),
                                        "m2", DoubleValue(g_diffParams.nakagamiM));
        std::cout << "Nakagami-m 衰落已启用: m=" << g_diffParams.nakagamiM 
                  << (g_diffParams.nakagamiM >= 2.0 ? " (近Rician/强LOS)" :
                     g_diffParams.nakagamiM >= 0.8 ? " (近Rayleigh)" : " (极度散射)")
                  << std::endl;
    }
    
    YansWifiPhyHelper wifiPhy;
    Ptr<YansWifiChannel> theChannel = wifiChannel.Create();
    wifiPhy.SetChannel(theChannel);
    wifiPhy.Set("TxPowerStart", DoubleValue(txPower));
    wifiPhy.Set("TxPowerEnd",   DoubleValue(txPower));
    wifiPhy.Set("RxSensitivity", DoubleValue(rxSensitivity));
    wifiPhy.Set("RxNoiseFigure", DoubleValue(g_diffParams.noiseFigure));
    
    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, g_uavNodes);

    for (uint32_t i = 0; i < devices.GetN(); ++i) {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(devices.Get(i));
        if (wifiDev) {
            Ptr<WifiRemoteStationManager> mgr = wifiDev->GetRemoteStationManager();
            if (mgr) {
                mgr->SetAttribute("MaxSsrc", UintegerValue(g_diffParams.macMaxRetries));
                mgr->SetAttribute("MaxSlrc", UintegerValue(g_diffParams.macMaxRetries));
            }
        }
    }
    
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
    g_tdma.enabled = true;
    SetupTDMATraffic();
    
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
    if (g_tdmaLog.is_open()) g_tdmaLog.close();
    
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
