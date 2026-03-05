#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/internet-module.h"
#include "ns3/applications-module.h"
#include <fstream>
#include <iostream>
#include <string>
#include <set>
#include <map>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <cmath>
#include <limits>
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-flow-classifier.h"

using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("RTKBenchmarkSimulation");

/*
 * Benchmark难度级别枚举
 */
enum DifficultyLevel {
    EASY,      // Basic-E: 理想空旷 + 低业务量
    MODERATE,  // Challenge-M: 城市散射 + 中等干扰 + 视频流
    HARD       // Stress-H: 密集多径 + 共信道干扰 + 高业务 + RTK漂移
};

/*
 * Benchmark配置结构体
 */
struct BenchmarkConfig {
    DifficultyLevel difficulty;
    std::string difficultyName;
    
    // 信道参数
    double pathLossExponent0;
    double pathLossExponent1;
    double pathLossExponent2;
    double nakagamiM0;
    double nakagamiM1;
    double nakagamiM2;
    
    // RTK误差参数
    double rtkNoiseStdDev;      // RTK位置噪声标准差
    double rtkDriftInterval;    // 漂移触发间隔
    double rtkDriftDuration;    // 漂移持续时间
    double rtkDriftMagnitude;   // 漂移幅度
    
    // WiFi参数
    std::string wifiStandard;   // "80211n" 或 "80211ac"
    uint32_t channelWidth;      // 20 或 40 MHz
    std::string rateControl;    // "ConstantRate" 或 "Minstrel"
    std::string dataMode;
    uint32_t maxRetries;
    
    // 业务流量参数
    double heartbeatRate;       // 心跳包速率 (packets/s)
    uint32_t heartbeatSize;     // 心跳包大小 (bytes)
    double videoDataRate;       // 视频数据速率 (Mbps)
    double sensorDataRate;      // 传感器/点云数据速率 (Mbps)
    
    // 通信触发参数
    double commTriggerProb;     // 通信触发概率
    double nearDistance;        // 近距离阈值
    double decaySpan;          // 概率衰减跨度
    double maxCommDistance;    // 最大通信距离
    
    // 干扰参数
    bool enableInterference;    // 是否启用外部干扰
    uint32_t numInterferenceNodes;  // 干扰节点数
};

/*
 * 全局变量定义
 */
static ofstream g_transFile;
static ofstream g_topoFile;
static ofstream g_posFile;
static ofstream g_configFile;
static std::vector< std::set< std::pair<uint32_t,uint32_t> > > g_intervalLinks;
static std::map<uint32_t, uint32_t> g_ipToNodeId;

struct TrajectoryPoint {
    double time;
    uint32_t nodeId;
    double x, y, z;
};

static std::vector<TrajectoryPoint> g_trajectoryData;
static std::map<uint32_t, std::vector<TrajectoryPoint>> g_nodeTrajectories;
static double g_simulationEndTime = 100.0;
static uint32_t g_numNodes = 20;
static BenchmarkConfig g_config;
static Ptr<UniformRandomVariable> g_randVar;

/*
 * 获取Benchmark配置
 */
BenchmarkConfig GetBenchmarkConfig(DifficultyLevel level)
{
    BenchmarkConfig config;
    config.difficulty = level;
    
    switch (level) {
        case EASY:
            config.difficultyName = "Easy";
            // 信道：理想环境，极轻微衰落
            config.pathLossExponent0 = 2.0;  // 自由空间
            config.pathLossExponent1 = 2.1;
            config.pathLossExponent2 = 2.2;
            config.nakagamiM0 = 3.0;  // 很轻微的衰落
            config.nakagamiM1 = 2.5;
            config.nakagamiM2 = 2.0;
            // RTK误差：极小且稳定
            config.rtkNoiseStdDev = 0.01;
            config.rtkDriftInterval = 0;  // 无漂移
            config.rtkDriftDuration = 0;
            config.rtkDriftMagnitude = 0;
            // WiFi：最优配置，充足重传
            config.wifiStandard = "80211n";
            config.channelWidth = 20;
            config.rateControl = "ConstantRate";
            config.dataMode = "HtMcs7";  // 使用较高速率，因为信道好
            config.maxRetries = 10;  // 充足的重传机会
            // 业务：极轻负载
            config.heartbeatRate = 1.0;
            config.heartbeatSize = 200;
            config.videoDataRate = 0.1;   // 仅100kbps基础数据
            config.sensorDataRate = 0.01;  // 10kbps控制信号
            // 通信：统一距离参数（所有难度一致）
            config.commTriggerProb = 0.3;  // 降低触发频率
            config.nearDistance = 50.0;
            config.decaySpan = 150.0;
            config.maxCommDistance = 50.0;  // 统一到50m
            // 干扰：完全无干扰
            config.enableInterference = false;
            config.numInterferenceNodes = 0;
            break;
            
        case MODERATE:
            config.difficultyName = "Moderate";
            // 信道：城市环境，更严重衰落
            config.pathLossExponent0 = 2.8;
            config.pathLossExponent1 = 3.5;
            config.pathLossExponent2 = 4.0;
            config.nakagamiM0 = 1.0;  // 降低，更多衰落
            config.nakagamiM1 = 0.7;  // 比Rayleigh更差
            config.nakagamiM2 = 0.5;
            // RTK误差：周期性中等漂移
            config.rtkNoiseStdDev = 0.08;
            config.rtkDriftInterval = 15.0;  // 15秒周期
            config.rtkDriftDuration = 4.0;   // 持续4秒
            config.rtkDriftMagnitude = 0.5;  // 0.5米漂移
            // WiFi：固定高速率，极少重传！
            config.wifiStandard = "80211n";
            config.channelWidth = 20;
            config.rateControl = "ConstantRate";  // 固定速率不自适应
            config.dataMode = "HtMcs5";  // 较高速率增加误码
            config.maxRetries = 1;  // 仅1次重传！（从2降至1）
            // 业务：中高负载
            config.heartbeatRate = 3.0;  // 增加心跳频率
            config.heartbeatSize = 600;  // 增大心跳包
            config.videoDataRate = 2.0;  // 2Mbps视频流
            config.sensorDataRate = 0.8;  // 800kbps传感器数据
            // 通信：统一距离参数（所有难度一致）
            config.commTriggerProb = 0.6;  // 增加触发频率
            config.nearDistance = 50.0;    // 与Easy一致
            config.decaySpan = 150.0;      // 与Easy一致
            config.maxCommDistance = 50.0;  // 统一到50m
            // 干扰：强干扰（增强以提高丢包率）
            config.enableInterference = true;
            config.numInterferenceNodes = 8;  // 从5增至8个干扰节点
            break;
            
        case HARD:
            config.difficultyName = "Hard";
            // 信道：极度恶劣环境，极严重衰落
            config.pathLossExponent0 = 3.5;  // 极高路径损耗
            config.pathLossExponent1 = 4.2;
            config.pathLossExponent2 = 5.0;
            config.nakagamiM0 = 0.5;   // 极严重衰落
            config.nakagamiM1 = 0.3;   // 远差于Rayleigh
            config.nakagamiM2 = 0.2;   // 极度恶劣
            // RTK误差：频繁大幅漂移
            config.rtkNoiseStdDev = 0.2;   // 大基础噪声
            config.rtkDriftInterval = 8.0;  // 8秒周期，很频繁
            config.rtkDriftDuration = 6.0;  // 持续6秒
            config.rtkDriftMagnitude = 1.0; // 1米大幅漂移
            // WiFi：极激进配置，无重传！
            config.wifiStandard = "80211n";
            config.channelWidth = 20;  // 窄带宽增加拥塞
            config.rateControl = "ConstantRate";  // 固定高速率
            config.dataMode = "HtMcs7";  // 最高速率，最易出错
            config.maxRetries = 0;  // 无重传！一次失败即丢包
            // 业务：超高负载
            config.heartbeatRate = 8.0;  // 极高频心跳
            config.heartbeatSize = 1000;  // 大心跳包
            config.videoDataRate = 4.0;  // 4Mbps视频流
            config.sensorDataRate = 3.0;  // 3Mbps传感器数据（总计7Mbps）
            // 通信：统一距离参数（所有难度一致）
            config.commTriggerProb = 0.8;  // 极高触发频率
            config.nearDistance = 50.0;    // 与Easy一致
            config.decaySpan = 150.0;      // 与Easy一致
            config.maxCommDistance = 50.0;  // 统一到50m
            // 干扰：极强干扰（增强以提高丢包率）
            config.enableInterference = true;
            config.numInterferenceNodes = 16;  // 从12增至16个干扰节点！
            break;
    }
    
    return config;
}

/*
 * 输出配置信息到文件
 */
void SaveConfigurationInfo(const std::string& outputDir, const BenchmarkConfig& config)
{
    g_configFile.open(outputDir + "/benchmark-config.txt");
    g_configFile << "=== RTK Benchmark Configuration ===" << std::endl;
    g_configFile << "Difficulty Level: " << config.difficultyName << std::endl;
    g_configFile << std::endl;
    
    g_configFile << "[Channel Model]" << std::endl;
    g_configFile << "  Path Loss Exponents: " << config.pathLossExponent0 << ", " 
                 << config.pathLossExponent1 << ", " << config.pathLossExponent2 << std::endl;
    g_configFile << "  Nakagami m parameters: " << config.nakagamiM0 << ", "
                 << config.nakagamiM1 << ", " << config.nakagamiM2 << std::endl;
    g_configFile << std::endl;
    
    g_configFile << "[RTK Error Model]" << std::endl;
    g_configFile << "  Base Noise StdDev: " << config.rtkNoiseStdDev << " m" << std::endl;
    g_configFile << "  Drift Interval: " << config.rtkDriftInterval << std::endl;
    g_configFile << "  Drift Duration: " << config.rtkDriftDuration << std::endl;
    g_configFile << "  Drift Magnitude: " << config.rtkDriftMagnitude << " m" << std::endl;
    g_configFile << std::endl;
    
    g_configFile << "[WiFi Configuration]" << std::endl;
    g_configFile << "  Standard: " << config.wifiStandard << std::endl;
    g_configFile << "  Channel Width: " << config.channelWidth << " MHz" << std::endl;
    g_configFile << "  Rate Control: " << config.rateControl << std::endl;
    g_configFile << "  Data Mode: " << config.dataMode << std::endl;
    g_configFile << "  Max Retries: " << config.maxRetries << std::endl;
    g_configFile << std::endl;
    
    g_configFile << "[Traffic Pattern]" << std::endl;
    g_configFile << "  Heartbeat Rate: " << config.heartbeatRate << " pps" << std::endl;
    g_configFile << "  Heartbeat Size: " << config.heartbeatSize << " bytes" << std::endl;
    g_configFile << "  Video Data Rate: " << config.videoDataRate << " Mbps" << std::endl;
    g_configFile << "  Sensor Data Rate: " << config.sensorDataRate << " Mbps" << std::endl;
    g_configFile << std::endl;
    
    g_configFile << "[Communication Parameters]" << std::endl;
    g_configFile << "  Trigger Probability: " << config.commTriggerProb << std::endl;
    g_configFile << "  Near Distance: " << config.nearDistance << " m" << std::endl;
    g_configFile << "  Decay Span: " << config.decaySpan << " m" << std::endl;
    g_configFile << "  Max Comm Distance: " << config.maxCommDistance << " m" << std::endl;
    g_configFile << std::endl;
    
    g_configFile << "[Interference]" << std::endl;
    g_configFile << "  Enabled: " << (config.enableInterference ? "Yes" : "No") << std::endl;
    g_configFile << "  Interference Nodes: " << config.numInterferenceNodes << std::endl;
    
    g_configFile.close();
}

/*
 * IPv4传输事件回调
 */
static void Ipv4Tracer(std::string context, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    Ptr<Packet> pktCopy = packet->Copy();
    Ipv4Header ipHeader;
    pktCopy->RemoveHeader(ipHeader);

    if (ipHeader.GetProtocol() != 6) return;

    TcpHeader tcpHeader;
    pktCopy->RemoveHeader(tcpHeader);

    bool isAck = tcpHeader.GetFlags() & TcpHeader::ACK;
    uint32_t payloadSize = pktCopy->GetSize();
    if (payloadSize == 0 && !isAck) return;

    uint32_t nodeId = 0;
    size_t pos1 = context.find("/NodeList/") + 10;
    size_t pos2 = context.find("/", pos1);
    if (pos1 != std::string::npos && pos2 != std::string::npos) {
        nodeId = atoi(context.substr(pos1, pos2 - pos1).c_str());
    }

    std::string eventType;
    if (context.find("/Tx") != std::string::npos) {
        eventType = (payloadSize > 0) ? "Tx Data" : "Tx Ack";
    } else {
        eventType = (payloadSize > 0) ? "Rx Data" : "Rx Ack";
    }

    g_transFile << std::fixed << std::setprecision(3)
                << Simulator::Now().GetSeconds() << ","
                << nodeId << ","
                << eventType << std::endl;

    uint32_t peerNodeId = 0;
    Ipv4Address peerIp = (context.find("/Tx") != std::string::npos)
                         ? ipHeader.GetDestination()
                         : ipHeader.GetSource();
    auto it = g_ipToNodeId.find(peerIp.Get());
    if (it != g_ipToNodeId.end()) {
        peerNodeId = it->second;
    }

    if (peerNodeId != nodeId) {
        uint32_t a = std::min(nodeId, peerNodeId);
        uint32_t b = std::max(nodeId, peerNodeId);
        uint32_t idx = (uint32_t) std::floor(Simulator::Now().GetSeconds() / 5.0);
        if (!g_intervalLinks.empty()) {
            idx = std::min(idx, (uint32_t)(g_intervalLinks.size() - 1));
        }
        g_intervalLinks[idx].insert(std::make_pair(a, b));
    }
}

/*
 * 拓扑输出
 */
static void TopologyOutput(uint32_t index)
{
    double start = index * 5.0;
    double end = start + 5.0;
    g_topoFile << std::fixed << std::setprecision(0) 
               << start << "-" << end << ": ";
    if (g_intervalLinks[index].empty()) {
        g_topoFile << "none";
    } else {
        bool first = true;
        for (auto pr : g_intervalLinks[index]) {
            if (!first) {
                g_topoFile << ", ";
            }
            g_topoFile << "Node" << pr.first << "-Node" << pr.second;
            first = false;
        }
    }
    g_topoFile << std::endl;
    g_intervalLinks[index].clear();
}

/*
 * 应用RTK位置噪声
 */
Vector ApplyRTKNoise(const Vector& originalPos, double time)
{
    Vector noisyPos = originalPos;
    
    // 基础高斯噪声
    Ptr<NormalRandomVariable> normalRand = CreateObject<NormalRandomVariable>();
    normalRand->SetAttribute("Mean", DoubleValue(0.0));
    normalRand->SetAttribute("Variance", DoubleValue(g_config.rtkNoiseStdDev * g_config.rtkNoiseStdDev));
    
    noisyPos.x += normalRand->GetValue();
    noisyPos.y += normalRand->GetValue();
    noisyPos.z += normalRand->GetValue() * 0.5;  // Z轴噪声较小
    
    // 周期性漂移
    if (g_config.rtkDriftInterval > 0) {
        double cycleTime = fmod(time, g_config.rtkDriftInterval);
        if (cycleTime < g_config.rtkDriftDuration) {
            // 漂移期间
            double driftFactor = 1.0 - exp(-3.0 * cycleTime / g_config.rtkDriftDuration);
            noisyPos.x += g_config.rtkDriftMagnitude * driftFactor * (g_randVar->GetValue() - 0.5) * 2.0;
            noisyPos.y += g_config.rtkDriftMagnitude * driftFactor * (g_randVar->GetValue() - 0.5) * 2.0;
        }
    }
    
    return noisyPos;
}

/*
 * 进度显示函数
 * 定期显示仿真进度，避免看起来像"卡住"
 */
static void PrintSimProgress()
{
    double now = Simulator::Now().GetSeconds();
    double progress = (now / g_simulationEndTime) * 100.0;
    
    std::cout << "\r进度: " << std::fixed << std::setprecision(1) << progress << "% "
              << "(" << (int)now << "/" << (int)g_simulationEndTime << "s)   " << std::flush;
    
    if (now < g_simulationEndTime - 10.0) {
        Simulator::Schedule(Seconds(10.0), &PrintSimProgress);
    }
}

/*
 * 节点位置记录
 */
static void RecordPositions(NodeContainer nodes)
{
    double now = Simulator::Now().GetSeconds();
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        Ptr<MobilityModel> mob = nodes.Get(i)->GetObject<MobilityModel>();
        Vector pos = mob->GetPosition();
        g_posFile << now << "," << i << "," 
                  << pos.x << "," << pos.y << "," << pos.z << std::endl;
    }
    
    if (now < g_simulationEndTime - 1.0) {
        Simulator::Schedule(Seconds(1.0), &RecordPositions, nodes);
    }
}

/*
 * RTK轨迹数据加载
 */
bool LoadTrajectoryData(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开轨迹文件: " << filename << std::endl;
        return false;
    }

    std::string line;
    std::getline(file, line);

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
            point.time = std::stod(tokens[0]);
            point.nodeId = std::stoul(tokens[1]);
            point.x = std::stod(tokens[2]);
            point.y = std::stod(tokens[3]);
            point.z = std::stod(tokens[4]);
            
            g_trajectoryData.push_back(point);
            g_nodeTrajectories[point.nodeId].push_back(point);
            
            maxTime = std::max(maxTime, point.time);
            maxNodeId = std::max(maxNodeId, point.nodeId);
        }
    }
    
    file.close();
    
    // 排序并清理轨迹数据
    for (auto &entry : g_nodeTrajectories) {
        auto &trajectory = entry.second;
        std::sort(trajectory.begin(), trajectory.end(), [](const TrajectoryPoint &a, const TrajectoryPoint &b) {
            return a.time < b.time;
        });
        std::vector<TrajectoryPoint> cleaned;
        cleaned.reserve(trajectory.size());
        double lastAcceptedTime = -std::numeric_limits<double>::infinity();
        for (const auto &pt : trajectory) {
            if (pt.time > lastAcceptedTime) {
                cleaned.push_back(pt);
                lastAcceptedTime = pt.time;
            }
        }
        trajectory.swap(cleaned);
        if (!trajectory.empty()) {
            maxTime = std::max(maxTime, trajectory.back().time);
        }
    }
    
    g_simulationEndTime = maxTime;
    g_numNodes = maxNodeId + 1;
    
    std::cout << "成功加载轨迹数据:" << std::endl;
    std::cout << "  节点数量: " << g_numNodes << std::endl;
    std::cout << "  仿真时长: " << g_simulationEndTime << std::endl;
    std::cout << "  数据点数: " << g_trajectoryData.size() << std::endl;
    
    return true;
}

/*
 * 设置RTK移动模型（带噪声）
 */
void SetupRTKMobility(NodeContainer& nodes)
{
    std::cout << "设置基于RTK数据的移动模型（带误差模拟）..." << std::endl;
    
    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::WaypointMobilityModel");
    mobility.Install(nodes);
    
    for (uint32_t nodeId = 0; nodeId < nodes.GetN(); ++nodeId) {
        Ptr<WaypointMobilityModel> waypoint = 
            nodes.Get(nodeId)->GetObject<WaypointMobilityModel>();
        
        if (g_nodeTrajectories.find(nodeId) != g_nodeTrajectories.end()) {
            auto& trajectory = g_nodeTrajectories[nodeId];
            
            std::cout << "  节点 " << nodeId << ": " << trajectory.size() << " waypoints" << std::endl;
            
            for (const auto& point : trajectory) {
                Vector originalPos(point.x, point.y, point.z);
                Vector noisyPos = ApplyRTKNoise(originalPos, point.time);
                waypoint->AddWaypoint(Waypoint(Seconds(point.time), noisyPos));
            }
        } else {
            std::cerr << "警告: 节点 " << nodeId << " 没有轨迹数据" << std::endl;
            waypoint->AddWaypoint(Waypoint(Seconds(0.0), Vector(0, 0, 25)));
            waypoint->AddWaypoint(Waypoint(Seconds(g_simulationEndTime), Vector(0, 0, 25)));
        }
    }
}

/*
 * 全局变量用于通信调度
 */
static NodeContainer* g_nodesPtr = nullptr;
static Ipv4InterfaceContainer* g_interfacesPtr = nullptr;
static uint16_t g_sinkPort = 9999;
static uint16_t g_udpSinkPort = 9998;

/*
 * 通信事件触发函数
 */
static void TriggerCommunication()
{
    if (g_randVar->GetValue() < g_config.commTriggerProb) {
        uint32_t sender = g_randVar->GetInteger(0, g_nodesPtr->GetN() - 1);
        uint32_t receiver = g_randVar->GetInteger(0, g_nodesPtr->GetN() - 1);
        
        while (receiver == sender) {
            receiver = g_randVar->GetInteger(0, g_nodesPtr->GetN() - 1);
        }
        
        Ptr<MobilityModel> senderMob = g_nodesPtr->Get(sender)->GetObject<MobilityModel>();
        Ptr<MobilityModel> receiverMob = g_nodesPtr->Get(receiver)->GetObject<MobilityModel>();
        double distance = senderMob->GetDistanceFrom(receiverMob);
        
        double commProb = 1.0;
        if (distance > g_config.nearDistance) {
            commProb = std::max(0.1, 1.0 - (distance - g_config.nearDistance) / g_config.decaySpan);
        }
        
        if (g_randVar->GetValue() < commProb && distance < g_config.maxCommDistance) {
            // 拆分为：传感器(TCP) + 视频(UDP)
            double sensorRateMbps = std::max(0.0, g_config.sensorDataRate);
            double videoRateMbps = std::max(0.0, g_config.videoDataRate);

            InetSocketAddress tcpRemote(g_interfacesPtr->GetAddress(receiver), g_sinkPort);
            InetSocketAddress udpRemote(g_interfacesPtr->GetAddress(receiver), g_udpSinkPort);

            if (g_config.difficulty == EASY) {
                double totalRate = std::max(0.01, sensorRateMbps + videoRateMbps);
                std::stringstream rateStr; rateStr << totalRate << "Mbps";
            OnOffHelper onoff("ns3::TcpSocketFactory", Address());
                onoff.SetAttribute("PacketSize", UintegerValue(800));
                onoff.SetAttribute("DataRate", StringValue(rateStr.str()));
                onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.02]"));
                onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
                onoff.SetAttribute("Remote", AddressValue(tcpRemote));
            ApplicationContainer app = onoff.Install(g_nodesPtr->Get(sender));
            app.Start(Seconds(0.001));
                app.Stop(Seconds(0.06));
            } else if (g_config.difficulty == MODERATE) {
                if (sensorRateMbps > 0.0) {
                    std::stringstream rateStr; rateStr << std::max(0.05, sensorRateMbps) << "Mbps";
                    OnOffHelper tcpOn("ns3::TcpSocketFactory", Address());
                    tcpOn.SetAttribute("PacketSize", UintegerValue(1000));
                    tcpOn.SetAttribute("DataRate", StringValue(rateStr.str()));
                    tcpOn.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.05]"));
                    tcpOn.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
                    tcpOn.SetAttribute("Remote", AddressValue(tcpRemote));
                    ApplicationContainer a1 = tcpOn.Install(g_nodesPtr->Get(sender));
                    a1.Start(Seconds(0.001));
                    a1.Stop(Seconds(0.12));
                }
                if (videoRateMbps > 0.0) {
                    std::stringstream rateStr; rateStr << videoRateMbps << "Mbps";
                    OnOffHelper udpOn("ns3::UdpSocketFactory", Address());
                    udpOn.SetAttribute("PacketSize", UintegerValue(1200));
                    udpOn.SetAttribute("DataRate", StringValue(rateStr.str()));
                    udpOn.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.08]"));
                    udpOn.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
                    udpOn.SetAttribute("Remote", AddressValue(udpRemote));
                    ApplicationContainer a2 = udpOn.Install(g_nodesPtr->Get(sender));
                    a2.Start(Seconds(0.001));
                    a2.Stop(Seconds(0.15));
                }
            } else { // HARD
                if (sensorRateMbps > 0.0) {
                    std::stringstream rateStr; rateStr << std::max(0.1, sensorRateMbps) << "Mbps";
                    OnOffHelper tcpOn("ns3::TcpSocketFactory", Address());
                    tcpOn.SetAttribute("PacketSize", UintegerValue(1200));
                    tcpOn.SetAttribute("DataRate", StringValue(rateStr.str()));
                    tcpOn.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.10]"));
                    tcpOn.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
                    tcpOn.SetAttribute("Remote", AddressValue(tcpRemote));
                    ApplicationContainer a1 = tcpOn.Install(g_nodesPtr->Get(sender));
                    a1.Start(Seconds(0.001));
                    a1.Stop(Seconds(0.25));
                }
                if (videoRateMbps > 0.0) {
                    std::stringstream rateStr; rateStr << videoRateMbps << "Mbps";
                    OnOffHelper udpOn("ns3::UdpSocketFactory", Address());
                    udpOn.SetAttribute("PacketSize", UintegerValue(1400));
                    udpOn.SetAttribute("DataRate", StringValue(rateStr.str()));
                    udpOn.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.20]"));
                    udpOn.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
                    udpOn.SetAttribute("Remote", AddressValue(udpRemote));
                    ApplicationContainer a2 = udpOn.Install(g_nodesPtr->Get(sender));
                    a2.Start(Seconds(0.001));
                    a2.Stop(Seconds(0.30));
                }
            }
        }
    }
}

/*
 * 智能通信调度
 */
void ScheduleIntelligentCommunication(NodeContainer& nodes, Ipv4InterfaceContainer& interfaces)
{
    std::cout << "设置智能通信调度 (" << g_config.difficultyName << " 模式)..." << std::endl;
    
    g_nodesPtr = &nodes;
    g_interfacesPtr = &interfaces;
    
    // 安装 TCP 与 UDP 接收端
    PacketSinkHelper tcpSinkHelper("ns3::TcpSocketFactory", 
                               InetSocketAddress(Ipv4Address::GetAny(), g_sinkPort));
    ApplicationContainer tcpSinks = tcpSinkHelper.Install(nodes);
    tcpSinks.Start(Seconds(0.0));
    tcpSinks.Stop(Seconds(g_simulationEndTime));

    PacketSinkHelper udpSinkHelper("ns3::UdpSocketFactory",
                               InetSocketAddress(Ipv4Address::GetAny(), g_udpSinkPort));
    ApplicationContainer udpSinks = udpSinkHelper.Install(nodes);
    udpSinks.Start(Seconds(0.0));
    udpSinks.Stop(Seconds(g_simulationEndTime));
    
    // 根据难度设置通信事件频率（Hard 更频繁）
    double step = 0.2;
    if (g_config.difficulty == EASY) {
        step = 0.3;
    } else if (g_config.difficulty == HARD) {
        step = 0.1;
    }
    
    for (double t = 1.0; t < g_simulationEndTime - 1.0; t += step) {
        Simulator::Schedule(Seconds(t), MakeCallback(&TriggerCommunication));
    }
}

/*
 * 创建干扰节点
 */
void CreateInterferenceNodes(NodeContainer& allNodes, Ptr<YansWifiChannel> channel)
{
    if (!g_config.enableInterference || g_config.numInterferenceNodes == 0) {
        return;
    }
    
    std::cout << "创建 " << g_config.numInterferenceNodes << " 个干扰节点..." << std::endl;
    
    NodeContainer interferenceNodes;
    interferenceNodes.Create(g_config.numInterferenceNodes);
    
    // 随机位置（使用ConstantPositionMobilityModel因为RandomWalk3dMobilityModel在某些ns-3版本中不可用）
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::RandomBoxPositionAllocator",
        "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=200.0]"),
        "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=300.0]"),
        "Z", StringValue("ns3::UniformRandomVariable[Min=20.0|Max=50.0]"));
    mobility.SetMobilityModel("ns3::ConstantPositionMobilityModel");
    mobility.Install(interferenceNodes);
    
    // 配置WiFi设备
    WifiHelper wifi;
    if (g_config.wifiStandard == "80211ac") {
        wifi.SetStandard(WIFI_STANDARD_80211ac);
    } else {
        wifi.SetStandard(WIFI_STANDARD_80211n);
    }
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("HtMcs0"),
                                 "ControlMode", StringValue("HtMcs0"));
    
    YansWifiPhyHelper phy;
    phy.SetChannel(channel);
    phy.Set("TxPowerStart", DoubleValue(30.0));
    phy.Set("TxPowerEnd", DoubleValue(30.0));
    
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer interferenceDevices = wifi.Install(phy, mac, interferenceNodes);
    
    InternetStackHelper stack;
    stack.Install(interferenceNodes);
    
    Ipv4AddressHelper address;
    address.SetBase("10.1.0.0", "255.255.255.0");
    address.Assign(interferenceDevices);
    
    // 让干扰节点持续发送数据
    uint16_t port = 8888;
    for (uint32_t i = 0; i < interferenceNodes.GetN(); ++i) {
        PacketSinkHelper sinkHelper("ns3::UdpSocketFactory",
                                   InetSocketAddress(Ipv4Address::GetAny(), port));
        ApplicationContainer sink = sinkHelper.Install(interferenceNodes.Get(i));
        sink.Start(Seconds(0.0));
        sink.Stop(Seconds(g_simulationEndTime));
        
        // 广播干扰包 - 根据难度调整干扰强度
        std::string dataRate = "500kbps";
        double onTime = 0.1;
        double offTime = 0.9;
        uint32_t pktSize = 512;
        
        if (g_config.difficulty == MODERATE) {
            dataRate = "4Mbps";  // 强干扰（从3增至4）
            onTime = 0.7;  // 70%占空比（从50%增至70%）
            offTime = 0.3;
            pktSize = 1300;  // 增大包长
        } else if (g_config.difficulty == HARD) {
            dataRate = "6Mbps";  // 极强干扰（从5增至6）
            onTime = 0.95;  // 95%时间在发送！（从90%增至95%）
            offTime = 0.05;  // 仅5%时间空闲
            pktSize = 1472;  // 最大包
        }
        
        OnOffHelper onoff("ns3::UdpSocketFactory",
                         InetSocketAddress(Ipv4Address("255.255.255.255"), port));
        onoff.SetAttribute("PacketSize", UintegerValue(pktSize));
        onoff.SetAttribute("DataRate", StringValue(dataRate));
        onoff.SetAttribute("OnTime",
            StringValue("ns3::ConstantRandomVariable[Constant=" + std::to_string(onTime) + "]"));
        onoff.SetAttribute("OffTime",
            StringValue("ns3::ConstantRandomVariable[Constant=" + std::to_string(offTime) + "]"));
        
        ApplicationContainer app = onoff.Install(interferenceNodes.Get(i));
        app.Start(Seconds(1.0));
        app.Stop(Seconds(g_simulationEndTime));
    }
}

/*
 * 主函数
 */
int main(int argc, char *argv[])
{
    std::string trajectoryFile = "data_rtk/mobility_trace.txt";
    std::string outputDir = "benchmark";
    std::string formationType = "cross";
    std::string difficultyStr = "easy";
    double maxSimTime = 0.0;  // 0表示使用完整轨迹时长
    
    CommandLine cmd;
    cmd.AddValue("trajectory", "轨迹数据文件路径", trajectoryFile);
    cmd.AddValue("outputDir", "输出结果文件夹路径", outputDir);
    cmd.AddValue("formation", "飞行形态 (cross/line/triangle/v_formation)", formationType);
    cmd.AddValue("difficulty", "难度级别 (easy/moderate/hard)", difficultyStr);
    cmd.AddValue("maxTime", "最大仿真时长（秒），0表示使用完整轨迹", maxSimTime);
    cmd.Parse(argc, argv);
    
    // 解析难度级别
    DifficultyLevel difficulty;
    std::transform(difficultyStr.begin(), difficultyStr.end(), difficultyStr.begin(), ::tolower);
    if (difficultyStr == "easy") {
        difficulty = EASY;
    } else if (difficultyStr == "moderate") {
        difficulty = MODERATE;
    } else if (difficultyStr == "hard") {
        difficulty = HARD;
    } else {
        std::cerr << "未知的难度级别: " << difficultyStr << std::endl;
        return 1;
    }
    
    g_config = GetBenchmarkConfig(difficulty);
    g_randVar = CreateObject<UniformRandomVariable>();
    g_randVar->SetStream(42);
    
    // 构建输出目录
    std::string finalOutputDir = outputDir + "/" + formationType + "_" + g_config.difficultyName;
    
#ifdef _WIN32
    std::string mkdirCmd = "mkdir " + outputDir + " 2>nul & mkdir " + finalOutputDir + " 2>nul";
#else
    std::string mkdirCmd = "mkdir -p " + finalOutputDir;
#endif
    int mkdirRet = system(mkdirCmd.c_str());
    (void)mkdirRet;
    
    std::cout << "======================================" << std::endl;
    std::cout << "RTK Benchmark 仿真" << std::endl;
    std::cout << "飞行形态: " << formationType << std::endl;
    std::cout << "难度级别: " << g_config.difficultyName << std::endl;
    std::cout << "======================================" << std::endl;
    
    if (!LoadTrajectoryData(trajectoryFile)) {
        std::cerr << "轨迹数据加载失败" << std::endl;
        return 1;
    }
    
    // 限制仿真时长
    if (maxSimTime > 0 && maxSimTime < g_simulationEndTime) {
        std::cout << "限制仿真时长: " << maxSimTime << " 秒 (原轨迹: " << g_simulationEndTime << " 秒)" << std::endl;
        g_simulationEndTime = maxSimTime;
    }
    
    SaveConfigurationInfo(finalOutputDir, g_config);
    
    NodeContainer nodes;
    nodes.Create(g_numNodes);
    
    SetupRTKMobility(nodes);
    
    // 配置WiFi
    WifiHelper wifi;
    if (g_config.wifiStandard == "80211ac") {
        wifi.SetStandard(WIFI_STANDARD_80211ac);
    } else {
        wifi.SetStandard(WIFI_STANDARD_80211n);
    }
    
    if (g_config.rateControl == "Minstrel") {
        if (g_config.wifiStandard == "80211ac") {
            wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager");
        } else {
            wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager");
        }
    } else {
        wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                     "DataMode", StringValue(g_config.dataMode),
                                     "ControlMode", StringValue(g_config.dataMode));
    }

    YansWifiChannelHelper channel;
    channel.AddPropagationLoss("ns3::ThreeLogDistancePropagationLossModel",
                              "Distance0", DoubleValue(1.0),
                              "Distance1", DoubleValue(100.0),
                              "Distance2", DoubleValue(250.0),
                              "Exponent0", DoubleValue(g_config.pathLossExponent0),
                              "Exponent1", DoubleValue(g_config.pathLossExponent1),
                              "Exponent2", DoubleValue(g_config.pathLossExponent2),
                              "ReferenceLoss", DoubleValue(46.6777));

    channel.AddPropagationLoss("ns3::NakagamiPropagationLossModel",
                              "Distance1", DoubleValue(50.0),
                              "Distance2", DoubleValue(150.0),
                              "m0", DoubleValue(g_config.nakagamiM0),
                              "m1", DoubleValue(g_config.nakagamiM1),
                              "m2", DoubleValue(g_config.nakagamiM2));

    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel");

    Ptr<YansWifiChannel> wifiChannel = channel.Create();
    YansWifiPhyHelper phy;
    phy.SetChannel(wifiChannel);
    phy.Set("ChannelSettings", StringValue("{0, " + std::to_string(g_config.channelWidth) + ", BAND_5GHZ, 0}"));
    
    // 根据难度调整物理层参数
    double txPower = 33.0;      // 发射功率
    double rxSensitivity = -93.0;  // 接收灵敏度
    double noiseFigure = 6.0;   // 噪声系数
    
    if (g_config.difficulty == MODERATE) {
        txPower = 25.0;  // 大幅降低发射功率
        rxSensitivity = -82.0;  // 大幅降低灵敏度（更容易丢包）
        noiseFigure = 15.0;  // 高噪声
    } else if (g_config.difficulty == HARD) {
        txPower = 20.0;  // 极低发射功率
        rxSensitivity = -75.0;  // 极差灵敏度（极易丢包）
        noiseFigure = 20.0;  // 极高噪声系数
    }
    
    phy.Set("TxPowerStart", DoubleValue(txPower));
    phy.Set("TxPowerEnd", DoubleValue(txPower));
    phy.Set("RxSensitivity", DoubleValue(rxSensitivity));
    phy.Set("RxNoiseFigure", DoubleValue(noiseFigure));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    InternetStackHelper stack;
    
    // 根据难度调整TCP参数
    if (g_config.difficulty == EASY) {
        Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(Seconds(10.0)));  // 充足的连接超时
        Config::SetDefault("ns3::TcpSocket::ConnCount", UintegerValue(10));  // 多次连接尝试
        Config::SetDefault("ns3::TcpSocket::DataRetries", UintegerValue(g_config.maxRetries));
        Config::SetDefault("ns3::TcpSocket::DelAckTimeout", TimeValue(Seconds(0.1)));  // 快速确认
        Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue(Seconds(0.2)));  // 短RTO
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
        Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(131072));  // 大缓冲区
        Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(131072));
    } else if (g_config.difficulty == MODERATE) {
    Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(Seconds(5.0)));
        Config::SetDefault("ns3::TcpSocket::ConnCount", UintegerValue(5));
    Config::SetDefault("ns3::TcpSocket::DataRetries", UintegerValue(g_config.maxRetries));
    Config::SetDefault("ns3::TcpSocket::DelAckTimeout", TimeValue(Seconds(0.2)));
    Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue(Seconds(0.5)));
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
        Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(65536));  // 中等缓冲区
        Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(65536));
    } else {  // HARD
        Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(Seconds(2.0)));  // 短连接超时
        Config::SetDefault("ns3::TcpSocket::ConnCount", UintegerValue(2));  // 少量连接尝试
        Config::SetDefault("ns3::TcpSocket::DataRetries", UintegerValue(g_config.maxRetries));  // 仅1次重传
        Config::SetDefault("ns3::TcpSocket::DelAckTimeout", TimeValue(Seconds(0.5)));  // 延迟确认
        Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue(Seconds(1.0)));  // 长RTO
        Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
        Config::SetDefault("ns3::TcpSocket::SndBufSize", UintegerValue(32768));  // 小缓冲区
        Config::SetDefault("ns3::TcpSocket::RcvBufSize", UintegerValue(32768));
    }
    
    stack.Install(nodes);
    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        Ipv4Address ip = interfaces.GetAddress(i);
        g_ipToNodeId[ip.Get()] = nodes.Get(i)->GetId();
    }
    
    // 创建干扰节点
    CreateInterferenceNodes(nodes, wifiChannel);

    ScheduleIntelligentCommunication(nodes, interfaces);

    g_transFile.open(finalOutputDir + "/node-transmissions.csv");
    g_transFile << "time_s,nodeId,eventType" << std::endl;

    g_topoFile.open(finalOutputDir + "/topology-changes.txt");

    g_posFile.open(finalOutputDir + "/node-positions.csv");
    g_posFile << "time_s,nodeId,x,y,z" << std::endl;

    Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Tx", MakeCallback(&Ipv4Tracer));
    Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Rx", MakeCallback(&Ipv4Tracer));

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> flowmon = flowmonHelper.InstallAll();

    uint32_t numIntervals = (uint32_t)std::ceil(g_simulationEndTime / 5.0);
    g_intervalLinks.resize(numIntervals);
    for (uint32_t idx = 0; idx < numIntervals; ++idx) {
        Simulator::Schedule(Seconds((idx + 1) * 5.0), &TopologyOutput, idx);
    }

    Simulator::Schedule(Seconds(1.0), &RecordPositions, nodes);
    Simulator::Schedule(Seconds(10.0), &PrintSimProgress);

    Simulator::Stop(Seconds(g_simulationEndTime));
    
    std::cout << "开始仿真（预计耗时：" << (int)(g_simulationEndTime / 60) << " 分钟）..." << std::endl;
    Simulator::Run();
    std::cout << std::endl << "仿真完成" << std::endl;

    flowmon->CheckForLostPackets();
    flowmon->SerializeToXmlFile(finalOutputDir + "/flowmon.xml", true, true);

    ofstream flowStats(finalOutputDir + "/flow-stats.csv");
    flowStats << "FlowId,SrcAddr,DestAddr,TxPackets,RxPackets,LostPackets,"
              << "PacketLossRate(%),Throughput(bps),DelaySum" << std::endl;
    
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(flowmonHelper.GetClassifier());
    std::map<FlowId, FlowMonitor::FlowStats> stats = flowmon->GetFlowStats();
    
    for (auto const &iter : stats) {
        FlowId id = iter.first;
        FlowMonitor::FlowStats st = iter.second;
        Ipv4FlowClassifier::FiveTuple t = classifier->FindFlow(id);
        double throughPut = 0.0;
        if (st.timeLastRxPacket.GetSeconds() - st.timeFirstTxPacket.GetSeconds() > 0) {
            throughPut = (st.rxBytes * 8.0) /
                (st.timeLastRxPacket.GetSeconds() - st.timeFirstTxPacket.GetSeconds());
        }
        flowStats << id << ","
                  << t.sourceAddress << ","
                  << t.destinationAddress << ","
                  << st.txPackets << ","
                  << st.rxPackets << ","
                  << (st.txPackets - st.rxPackets) << ","
                  << (st.txPackets > 0 ? (100.0 * (st.txPackets - st.rxPackets) / st.txPackets) : 0.0) << ","
                  << throughPut << ","
                  << st.delaySum.GetSeconds() << std::endl;
    }
    flowStats.close();

    Simulator::Destroy();
    g_transFile.close();
    g_topoFile.close();
    g_posFile.close();
    
    std::cout << "\n仿真完成！输出文件位于: " << finalOutputDir << std::endl;
    std::cout << "  - benchmark-config.txt: 配置信息" << std::endl;
    std::cout << "  - node-transmissions.csv: 传输事件" << std::endl;
    std::cout << "  - topology-changes.txt: 拓扑变化" << std::endl;
    std::cout << "  - node-positions.csv: 节点位置" << std::endl;
    std::cout << "  - flowmon.xml: FlowMonitor详细数据" << std::endl;
    std::cout << "  - flow-stats.csv: 流统计数据" << std::endl;

    return 0;
}

