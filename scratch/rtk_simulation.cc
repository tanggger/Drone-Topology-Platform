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

NS_LOG_COMPONENT_DEFINE("RTKBasedSimulation");

/*
 * 全局变量定义
 * 用于存储仿真过程中的各种状态信息和数据
 */
static ofstream g_transFile;
static ofstream g_topoFile;
static ofstream g_posFile;
static std::vector< std::set< std::pair<uint32_t,uint32_t> > > g_intervalLinks(20);
static std::map<uint32_t, uint32_t> g_ipToNodeId;

/*
 * RTK轨迹数据结构
 * 用于存储单个轨迹点的时间、节点ID和三维坐标信息
 */
struct TrajectoryPoint {
    double time;
    uint32_t nodeId;
    double x, y, z;
};

/*
 * 全局轨迹数据存储
 * 包含所有轨迹数据、按节点分组的轨迹以及仿真参数
 */
static std::vector<TrajectoryPoint> g_trajectoryData;
static std::map<uint32_t, std::vector<TrajectoryPoint>> g_nodeTrajectories;
static double g_simulationEndTime = 100.0;
static uint32_t g_numNodes = 20;

/*
 * IPv4传输事件回调函数
 * 监控网络层的数据包传输事件，记录TCP数据包的发送和接收情况
 * 同时维护拓扑链路信息，用于后续的拓扑变化分析
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
        // 将索引限制在已分配的时间窗口范围内，避免固定为 100s
        if (!g_intervalLinks.empty()) {
            idx = std::min(idx, (uint32_t)(g_intervalLinks.size() - 1));
        }
        g_intervalLinks[idx].insert(std::make_pair(a, b));
    }
}

/*
 * 拓扑输出函数
 * 将指定时间间隔内的活动链路信息输出到拓扑变化文件中
 * 用于分析网络拓扑的动态变化情况
 */
static void TopologyOutput(uint32_t index)
{
    double start = index * 5.0;
    double end = start + 5.0;
    g_topoFile << std::fixed << std::setprecision(0) 
               << start << "-" << end << "s: ";
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
 * 节点位置记录函数
 * 定期记录所有节点的当前位置信息到位置轨迹文件中
 * 使用递归调度实现持续的位置监控
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
 * RTK轨迹数据加载函数
 * 从CSV格式的轨迹文件中读取节点移动轨迹数据
 * 解析并存储轨迹点信息，同时确定仿真参数
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
    
    // 对每个节点的轨迹按时间升序排序，并移除非严格递增的时间点（避免Waypoint因时间不递增触发致命错误）
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
            // 若 pt.time <= lastAcceptedTime，则丢弃该点
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
    std::cout << "  仿真时长: " << g_simulationEndTime << " 秒" << std::endl;
    std::cout << "  数据点数: " << g_trajectoryData.size() << std::endl;
    
    return true;
}

/*
 * RTK移动模型设置函数
 * 基于加载的RTK轨迹数据为每个节点设置WaypointMobilityModel
 * 将轨迹点转换为NS-3的waypoint格式，实现精确的节点移动
 */
void SetupRTKMobility(NodeContainer& nodes)
{
    std::cout << "设置基于RTK数据的移动模型..." << std::endl;
    
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
                waypoint->AddWaypoint(Waypoint(Seconds(point.time), 
                                             Vector(point.x, point.y, point.z)));
            }
        } else {
            std::cerr << "警告: 节点 " << nodeId << " 没有轨迹数据" << std::endl;
            waypoint->AddWaypoint(Waypoint(Seconds(0.0), Vector(0, 0, 25)));
            waypoint->AddWaypoint(Waypoint(Seconds(g_simulationEndTime), Vector(0, 0, 25)));
        }
    }
}

/*
 * 智能通信调度函数
 * 实现基于节点间距离的智能通信调度机制
 * 根据节点位置动态决定通信机会，距离越近通信成功概率越高
 */
void ScheduleIntelligentCommunication(NodeContainer& nodes, Ipv4InterfaceContainer& interfaces)
{
    std::cout << "设置基于距离的智能通信调度..." << std::endl;
    
    uint16_t sinkPort = 9999;
    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", 
                               InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    ApplicationContainer sinkApps = sinkHelper.Install(nodes);
    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(Seconds(g_simulationEndTime));
    
    Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
    rand->SetStream(1);
    
    double step = 0.2;
    
    for (double t = 1.0; t < g_simulationEndTime - 1.0; t += step) {
        Simulator::Schedule(Seconds(t), [=, &nodes, &interfaces]() {
            if (rand->GetValue() < 0.4) {
                uint32_t sender = rand->GetInteger(0, nodes.GetN() - 1);
                uint32_t receiver = rand->GetInteger(0, nodes.GetN() - 1);
                
                while (receiver == sender) {
                    receiver = rand->GetInteger(0, nodes.GetN() - 1);
                }
                
                Ptr<MobilityModel> senderMob = nodes.Get(sender)->GetObject<MobilityModel>();
                Ptr<MobilityModel> receiverMob = nodes.Get(receiver)->GetObject<MobilityModel>();
                double distance = senderMob->GetDistanceFrom(receiverMob);
                
                // 缩短通信触发距离为原来的一半：近距离阈值 50->25，衰减跨度 150->75，最大可通信距离 200->100
                double commProb = 1.0;
                const double nearDistance = 25.0;
                const double decaySpan = 75.0;
                const double maxCommDistance = 100.0;
                if (distance > nearDistance) {
                    commProb = std::max(0.1, 1.0 - (distance - nearDistance) / decaySpan);
                }
                
                if (rand->GetValue() < commProb && distance < maxCommDistance) {
                    OnOffHelper onoff("ns3::TcpSocketFactory", Address());
                    onoff.SetAttribute("PacketSize", UintegerValue(512));
                    onoff.SetAttribute("DataRate", StringValue("1Mbps"));
                    onoff.SetAttribute("OnTime", 
                        StringValue("ns3::ConstantRandomVariable[Constant=0.01]"));
                    onoff.SetAttribute("OffTime", 
                        StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));
                    
                    Address remoteAddress(InetSocketAddress(
                        interfaces.GetAddress(receiver), sinkPort));
                    onoff.SetAttribute("Remote", AddressValue(remoteAddress));
                    
                    ApplicationContainer app = onoff.Install(nodes.Get(sender));
                    app.Start(Seconds(0.001));
                    app.Stop(Seconds(0.05));
                }
            }
        });
    }
}

/*
 * 主函数
 * 程序入口点，负责整个仿真流程的协调和控制
 * 包括参数解析、数据加载、网络配置、仿真运行和结果输出
 */
int main(int argc, char *argv[])
{
    std::string trajectoryFile = "sim_input/mobility_trace.txt";
    std::string outputDir = "sim_output";
    
    CommandLine cmd;
    cmd.AddValue("trajectory", "轨迹数据文件路径", trajectoryFile);
    cmd.AddValue("outputDir", "输出结果文件夹路径", outputDir);
    cmd.Parse(argc, argv);
    
    // 确保输出目录存在
#ifdef _WIN32
    {
        std::string mkdirCmd = "mkdir " + outputDir;
        int mkdirRet = system(mkdirCmd.c_str());
        (void)mkdirRet;
    }
#else
    {
        std::string mkdirCmd = "mkdir -p " + outputDir;
        int mkdirRet = system(mkdirCmd.c_str());
        (void)mkdirRet;
    }
#endif
    
    if (!LoadTrajectoryData(trajectoryFile)) {
        std::cerr << "轨迹数据加载失败，使用默认参数" << std::endl;
        g_numNodes = 20;
        g_simulationEndTime = 100.0;
    }
    
    std::cout << "开始RTK轨迹仿真..." << std::endl;
    std::cout << "节点数量: " << g_numNodes << std::endl;
    std::cout << "仿真时长: " << g_simulationEndTime << " 秒" << std::endl;
    
    NodeContainer nodes;
    nodes.Create(g_numNodes);
    
    // LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
    // LogComponentEnable("PacketSink", LOG_LEVEL_INFO);
    
    if (!g_trajectoryData.empty()) {
        SetupRTKMobility(nodes);
    } else {
        std::cout << "使用默认移动模型..." << std::endl;
        MobilityHelper mobility;
        mobility.SetPositionAllocator("ns3::RandomBoxPositionAllocator",
            "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=100.0]"),
            "Y", StringValue("ns3::UniformRandomVariable[Min=50.0|Max=250.0]"),
            "Z", StringValue("ns3::UniformRandomVariable[Min=20.0|Max=40.0]"));
        mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
        mobility.Install(nodes);
    }
    
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ac);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("VhtMcs0"), 
                                 "ControlMode", StringValue("VhtMcs0"));

    YansWifiChannelHelper channel;
    channel.AddPropagationLoss("ns3::ThreeLogDistancePropagationLossModel",
                              "Distance0", DoubleValue(1.0),
                              "Distance1", DoubleValue(100.0),
                              "Distance2", DoubleValue(250.0),
                              "Exponent0", DoubleValue(2.5),
                              "Exponent1", DoubleValue(3.0),
                              "Exponent2", DoubleValue(3.5),
                              "ReferenceLoss", DoubleValue(46.6777));

    channel.AddPropagationLoss("ns3::NakagamiPropagationLossModel",
                              "Distance1", DoubleValue(50.0),
                              "Distance2", DoubleValue(150.0),
                              "m0", DoubleValue(1.5),
                              "m1", DoubleValue(1.0),
                              "m2", DoubleValue(0.75));

    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel",
                               "Speed", DoubleValue(299792458));

    Ptr<YansWifiChannel> wifiChannel = channel.Create();
    YansWifiPhyHelper phy;
    phy.SetChannel(wifiChannel);
    phy.Set("TxPowerStart", DoubleValue(33.0));
    phy.Set("TxPowerEnd", DoubleValue(33.0));
    phy.Set("RxSensitivity", DoubleValue(-93.0));
    phy.Set("RxNoiseFigure", DoubleValue(6.0));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    InternetStackHelper stack;
    Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(Seconds(5.0)));
    Config::SetDefault("ns3::TcpSocket::ConnCount", UintegerValue(6));
    Config::SetDefault("ns3::TcpSocket::DataRetries", UintegerValue(6));
    Config::SetDefault("ns3::TcpSocket::DelAckTimeout", TimeValue(Seconds(0.2)));
    Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue(Seconds(0.5)));
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno"));
    
    stack.Install(nodes);
    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        Ipv4Address ip = interfaces.GetAddress(i);
        g_ipToNodeId[ip.Get()] = nodes.Get(i)->GetId();
    }

    ScheduleIntelligentCommunication(nodes, interfaces);

    g_transFile.open(outputDir + "/rtk-node-transmissions.csv");
    g_transFile << "time_s,nodeId,eventType" << std::endl;

    g_topoFile.open(outputDir + "/rtk-topology-changes.txt");

    g_posFile.open(outputDir + "/rtk-node-positions.csv");
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

    Simulator::Stop(Seconds(g_simulationEndTime));
    Simulator::Run();

    flowmon->CheckForLostPackets();
    flowmon->SerializeToXmlFile(outputDir + "/rtk-flowmon.xml", true, true);

    ofstream flowStats(outputDir + "/rtk-flow-stats.csv");
    flowStats << "FlowId,SrcAddr,DestAddr,TxPackets,RxPackets,LostPackets,"
              << "PacketLossRate(%),Throughput(bps),DelaySum(s)" << std::endl;
    
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
    
    std::cout << "仿真完成！输出文件:" << std::endl;
    std::cout << "  - " << outputDir << "/rtk-node-transmissions.csv: 传输事件" << std::endl;
    std::cout << "  - " << outputDir << "/rtk-topology-changes.txt: 拓扑变化" << std::endl;
    std::cout << "  - " << outputDir << "/rtk-node-positions.csv: 节点位置" << std::endl;
    std::cout << "  - " << outputDir << "/rtk-flowmon.xml: FlowMonitor详细数据" << std::endl;
    std::cout << "  - " << outputDir << "/rtk-flow-stats.csv: 流统计数据" << std::endl;

    return 0;
}