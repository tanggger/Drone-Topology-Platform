/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */

#include <iostream>
#include <fstream>
#include <vector>
#include <ctime>
#include <iomanip>

// ns-3 核心库
#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"

// Wi-Fi 与移动模型
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"

// 应用与流量监控
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"

using namespace ns3;

// ---------------- 全局配置参数 ----------------

// 模拟区域大小 (平面为 500m x 500m)
static double SIM_AREA_SIZE = 500.0;

// 通信距离阈值
static double COMM_RANGE = 250.0;

// 链路激活概率 (在距离内时，以 10% 概率激活)
static double linkProb = 0.1;

// 拓扑更新周期 (每 30s 更新一次激活链路)
static double TOPOLOGY_UPDATE_INTERVAL = 30.0;

// 节点数
static uint32_t numNodes = 10;

// 仿真总时长
static double simulationTime = 60.0;

// 前 5s 预热时间 (不进行链路激活等操作)
static double warmupTime = 5.0;

// 发送调度周期 (每 0.5s 从激活链路里随机选一条双向发送)
static double sendInterval = 0.5;

// 数据包大小 (UdpEchoClient)
static uint32_t packetSize = 512;

// 输出文件
static std::string topologyFileName = "topology-changes.txt";
static std::string transmissionFileName = "node-transmissions.txt";
static std::string flowmonFileName = "uav-flowmon.xml";

// 记录当前激活的链路 (无向：i<->j)
static std::vector<std::pair<uint32_t, uint32_t>> activeLinks;

// 记录哪些 (src->dst) 的 Client 应用已经建立，用于避免重复创建
static std::set<std::pair<uint32_t, uint32_t>> activeClientPairs;

// 输出文件流
static std::ofstream g_topologyOut;
static std::ofstream g_transmissionOut;

// 节点容器 & 设备容器
NodeContainer nodes;
NetDeviceContainer devices;

// ---------------- 工具函数：距离计算 ----------------
double CalculateDistance (Ptr<Node> n1, Ptr<Node> n2)
{
    Ptr<MobilityModel> mob1 = n1->GetObject<MobilityModel> ();
    Ptr<MobilityModel> mob2 = n2->GetObject<MobilityModel> ();
    return mob1->GetDistanceFrom (mob2);
}

// ---------------- 日志记录函数 ----------------
void LogTopologyChange (double time, uint32_t node1, uint32_t node2)
{
  // 记录链路激活 (time, node1, node2)
    g_topologyOut << std::fixed << std::setprecision(3)
                << time << "s: Link(" << node1 << "<->" << node2 << ") activated" << std::endl;
}

// type 可为 "DATA" 或 "ACK" 等
void LogTransmission (double time, uint32_t nodeId, const std::string &type, uint32_t packetUid)
{
    Vector pos = nodes.Get (nodeId)->GetObject<MobilityModel> ()->GetPosition ();
    g_transmissionOut << std::fixed << std::setprecision(4)
                    << time << "s  Node " << nodeId
                    << "  (" << pos.x << "," << pos.y << "," << pos.z << ")  "
                    << "PacketUID=" << packetUid
                    << "  Type=" << type << std::endl;
}

// ---------------- WiFi 层发送回调 ----------------
static void PhyTxTrace(std::string context, 
                      Ptr<const Packet> packet,
                      [[maybe_unused]] WifiMode mode,
                      [[maybe_unused]] WifiPreamble preamble,
                      [[maybe_unused]] uint8_t txPower)
{
    // 更稳健的节点ID解析方法
    uint32_t nodeId = 0;
    const std::string key = "/NodeList/";
    size_t nodePos = context.find(key);
    
    if (nodePos != std::string::npos) {
        size_t start = nodePos + key.length();
        size_t end = context.find('/', start);
        if (end != std::string::npos) {
            try {
                nodeId = std::stoi(context.substr(start, end - start));
            } catch (const std::exception& e) {
                // 
                std::cout<<"Error: "<<e.what()<<std::endl;
                return;
            }
        }
    }

    // 记录传输信息
    uint32_t uid = packet->GetUid();
    double now = Simulator::Now().GetSeconds();
    LogTransmission(now, nodeId, "DATA", uid);
}

// ---------------- UdpEchoServer 收包回调 ----------------
void ServerReceive(std::string context, Ptr<const Packet> packet)
{
    // 解析上下文获取节点ID
    uint32_t nodeId = 0;
    std::string delimiter = "/";
    size_t nodePos = context.find("NodeList/");
    if (nodePos != std::string::npos) {
        size_t start = nodePos + 9; // "NodeList/" 占9个字符
        size_t end = context.find(delimiter, start);
        if (end != std::string::npos) {
            std::string nodeIdStr = context.substr(start, end - start);
            nodeId = std::stoi(nodeIdStr);
        }
    }

    // 记录传输信息
    uint32_t uid = packet->GetUid();
    double now = Simulator::Now().GetSeconds();
    LogTransmission(now, nodeId, "ACK", uid);
}

// ---------------- 删除某条客户端会话记录 ----------------
void RemoveClientRecord (uint32_t src, uint32_t dst)
{
    auto it = activeClientPairs.find (std::make_pair (src, dst));
    if (it != activeClientPairs.end ())
    {
        activeClientPairs.erase (it);
    }
    }

// ---------------- 客户端应用结束时的回调 ----------------
void ClientEndCallback(Ptr<Application> app, uint32_t src, uint32_t dst)
{
    // 应用结束 -> 从活跃 set 中移除
    RemoveClientRecord(src, dst);
}

// ---------------- 创建一个 UdpEchoClient ----------------
void CreateClientApplication (uint32_t src, uint32_t dst)
{
    // 避免重复创建相同 (src->dst) 的客户端
    auto pairKey = std::make_pair (src, dst);
    if (activeClientPairs.find(pairKey) != activeClientPairs.end())
    {
        // 已经存在，则不再重复创建
        return;
    }

    // 获取目标节点 IP
    Ptr<Ipv4> ipv4 = nodes.Get (dst)->GetObject<Ipv4> ();
    Ipv4Address dstAddr = ipv4->GetAddress (1,0).GetLocal (); // 接口索引 (1,0)，视分配情况而定

    UdpEchoClientHelper clientHelper (dstAddr, 2000);
    clientHelper.SetAttribute ("MaxPackets", UintegerValue (1));
    clientHelper.SetAttribute ("Interval", TimeValue (Seconds (0.01)));
    clientHelper.SetAttribute ("PacketSize", UintegerValue (packetSize));

    ApplicationContainer clientApps = clientHelper.Install (nodes.Get (src));
    // 当前时间启动，持续 1s，完毕后自动销毁
    //   double startTime = Simulator::Now ().GetSeconds ();
    clientApps.Start (Seconds (0.0));
    clientApps.Stop  (Seconds (1.0));

    // 监听应用销毁事件
    Ptr<Application> app = clientApps.Get (0);
    app->TraceConnectWithoutContext("Destroy", MakeCallback(&ClientEndCallback));

    // 记录该客户端已激活
    activeClientPairs.insert (pairKey);
}

// ---------------- 定时选取一条激活链路，发起双向通信 ----------------
void ScheduleTransmissions ()
{
    if (activeLinks.empty())
    {
        // 当前没有激活链路
        return;
    }

    // 从 activeLinks 随机选一条
    uint32_t idx = rand() % activeLinks.size();
    auto link = activeLinks[idx];
    uint32_t nodeA = link.first;
    uint32_t nodeB = link.second;

    // 分别创建 (A->B) 和 (B->A) 的客户端应用
    CreateClientApplication (nodeA, nodeB);
    CreateClientApplication (nodeB, nodeA);
}

// ---------------- 循环调度：每隔 sendInterval 秒执行一次 ScheduleTransmissions() ----------------
void ScheduleAllTransmissions (double startTime, double endTime, double interval)
{
    double t = startTime;
    while (t < endTime)
    {
        Simulator::Schedule (Seconds(t), &ScheduleTransmissions);
        t += interval;
    }
}

// ---------------- 动态更新拓扑 (激活/关闭链路) ----------------
void UpdateTopology ()
{
    double now = Simulator::Now().GetSeconds ();

    // 先清空当前激活链路
    activeLinks.clear ();

    // 遍历节点对 (i,j)
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        for (uint32_t j = i+1; j < numNodes; ++j)
        {
            double dist = CalculateDistance (nodes.Get(i), nodes.Get(j));
            if (dist <= COMM_RANGE)
            {
                // 在距离阈值内时，以 linkProb=0.1 概率激活
                double rnd = (double)rand() / RAND_MAX;
                if (rnd < linkProb)
                {
                    activeLinks.push_back (std::make_pair(i, j));
                    // 记录链路激活
                    LogTopologyChange (now, i, j);
                }
            }
        }
    }
}

// ---------------- 定时更新拓扑的调度函数 ----------------
void ScheduleTopologyUpdates ()
{
    double now = Simulator::Now().GetSeconds ();
    if (now < simulationTime)
    {
        // 更新拓扑
        UpdateTopology ();
        // 下一次调度
        Simulator::Schedule (Seconds (TOPOLOGY_UPDATE_INTERVAL), &ScheduleTopologyUpdates);
    }
}

int main (int argc, char *argv[])
{
    // 为了结果可复现，这里简单设置随机种子，也可不设
    // 也可以通过命令行参数 --RngRun=xxx 去修改
    srand ((unsigned int) time(NULL));

    // 解析命令行参数（若需要）
    CommandLine cmd (__FILE__);
    cmd.Parse (argc, argv);

    // 打开输出文件
    g_topologyOut.open (topologyFileName.c_str ());
    g_transmissionOut.open (transmissionFileName.c_str ());
    if (!g_topologyOut.is_open () || !g_transmissionOut.is_open ())
    {
        std::cerr << "Cannot open output file(s)." << std::endl;
        return -1;
    }

    // 创建节点
    nodes.Create (numNodes);

    // ---------------- 配置移动模型 ----------------
    // 初始位置分配器：在 [0,500], [0,500], [50,150] 范围内随机分配
    Ptr<UniformRandomVariable> posx = CreateObject<UniformRandomVariable> ();
    posx->SetAttribute ("Min", DoubleValue (0.0));
    posx->SetAttribute ("Max", DoubleValue (SIM_AREA_SIZE));

    Ptr<UniformRandomVariable> posy = CreateObject<UniformRandomVariable> ();
    posy->SetAttribute ("Min", DoubleValue (0.0));
    posy->SetAttribute ("Max", DoubleValue (SIM_AREA_SIZE));

    Ptr<UniformRandomVariable> posz = CreateObject<UniformRandomVariable> ();
    posz->SetAttribute ("Min", DoubleValue (50.0));
    posz->SetAttribute ("Max", DoubleValue (150.0));

    Ptr<ListPositionAllocator> positionAlloc = CreateObject<ListPositionAllocator> ();
    for (uint32_t i = 0; i < numNodes; ++i)
    {
        double x = posx->GetValue ();
        double y = posy->GetValue ();
        double z = posz->GetValue ();
        positionAlloc->Add (Vector (x, y, z));
    }

    MobilityHelper mobility;
    mobility.SetPositionAllocator (positionAlloc);

    // 高斯马尔可夫模型
    // 设置初始速度在 [10,20] 之间，再在仿真中随机漫步
    mobility.SetMobilityModel ("ns3::GaussMarkovMobilityModel",
                            "Bounds", BoxValue (Box (0, SIM_AREA_SIZE, 0, SIM_AREA_SIZE, 50, 150)),
                            "TimeStep", TimeValue (Seconds (1.0)),
                            "Alpha", DoubleValue (0.85),
                            "MeanVelocity", StringValue ("ns3::UniformRandomVariable[Min=10|Max=20]"),
                            "MeanDirection", StringValue ("ns3::UniformRandomVariable[Min=0|Max=6.2831853]"),
                            "MeanPitch", StringValue ("ns3::ConstantRandomVariable[Constant=0.0]"),
                            "NormalVelocity", StringValue ("ns3::NormalRandomVariable[Mean=0|Variance=1.0]"),
                            "NormalDirection", StringValue ("ns3::NormalRandomVariable[Mean=0|Variance=1.0]"),
                            "NormalPitch", StringValue ("ns3::NormalRandomVariable[Mean=0|Variance=1.0]"));
    mobility.Install (nodes);

    // ---------------- 配置 WiFi 802.11ac (Adhoc) ----------------
    WifiHelper wifi;
    wifi.SetStandard (WIFI_STANDARD_80211ac);

    YansWifiChannelHelper channel;
    channel.SetPropagationDelay ("ns3::ConstantSpeedPropagationDelayModel");
    channel.AddPropagationLoss ("ns3::FriisPropagationLossModel");

    YansWifiPhyHelper phy;
    phy.SetChannel (channel.Create ());


    // 发射功率固定 28dBm，接收灵敏度 -90dBm
    phy.Set ("TxPowerStart", DoubleValue (28.0));
    phy.Set ("TxPowerEnd", DoubleValue (28.0));
    phy.Set ("RxSensitivity", DoubleValue (-90.0));

    WifiMacHelper mac;
    mac.SetType ("ns3::AdhocWifiMac",
                "QosSupported", BooleanValue (true));

    devices = wifi.Install (phy, mac, nodes);

    // ---------------- 不使用任何路由协议，但仍需安装网络协议栈以赋予 IP 地址 ----------------
    // 注意：默认的 InternetStackHelper 里会包含默认路由模块（如 Ipv4L3Protocol），
    // 但只要我们不调用 PopulateRoutingTables() 或安装主动路由协议，实际上就不会进行多跳路由。
    InternetStackHelper stack;
    stack.Install (nodes);

    // IP 地址分配
    Ipv4AddressHelper address;
    // 这里给所有节点分配来自 10.1.0.0/24 网段的地址
    address.SetBase ("10.1.0.0", "255.255.255.0");
    address.Assign (devices);

    // ---------------- 配置应用：每个节点安装一个 UdpEchoServer (端口2000) ----------------
    uint16_t port = 2000;
    UdpEchoServerHelper echoServer(port);
    ApplicationContainer serverApps = echoServer.Install(nodes);

    serverApps.Start(Seconds(0.0));
    serverApps.Stop(Seconds(simulationTime));

    for (uint32_t i = 0; i < serverApps.GetN(); ++i) {
        Ptr<Application> app = serverApps.Get(i);
        Ptr<UdpEchoServer> server = DynamicCast<UdpEchoServer>(app);
        if (server) {
            // 生成正确的上下文路径
            std::ostringstream oss;
            oss << "/NodeList/" << server->GetNode()->GetId() 
                << "/ApplicationList/" << i 
                << "/$ns3::UdpEchoServer/Rx";
                
            // 使用带上下文的TraceConnect
            server->TraceConnect("Rx", oss.str(), MakeCallback(&ServerReceive));
        }
    }
    // ---------------- 配置 WiFi 层回调：捕获 PhyTxBegin 事件 ----------------
    // 用来记录数据发送 (DATA)
    for (uint32_t i = 0; i < devices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(devices.Get(i));
        if (!wifiDev) {continue;}

        // 获取物理层对象
        Ptr<WifiPhy> phy = wifiDev->GetPhy();
        
        // 构建完整的上下文路径
        std::ostringstream oss;
        oss << "/NodeList/" << wifiDev->GetNode()->GetId()
            << "/DeviceList/" << i
            << "/$ns3::WifiNetDevice/Phy/State/PhyTxBegin";
        
        // 使用带上下文的TraceConnect
        phy->TraceConnect("PhyTxBegin", 
                        oss.str(), 
                        MakeCallback(&PhyTxTrace));
    }

    // ---------------- 设置拓扑更新 + 流量调度 ----------------
    // 在 warmupTime 之后第一次更新拓扑，然后每隔 TOPOLOGY_UPDATE_INTERVAL 更新一次
    Simulator::Schedule (Seconds (warmupTime), &ScheduleTopologyUpdates);

    // 在 warmupTime 之后，每隔 sendInterval 执行一次随机发送选取，一直到 simulationTime
    ScheduleAllTransmissions (warmupTime, simulationTime, sendInterval);

    // ---------------- FlowMonitor ----------------
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll ();

    // ---------------- 启动仿真 ----------------
    Simulator::Stop (Seconds (simulationTime));
    Simulator::Run ();

    // ---------------- 仿真结束，输出 FlowMonitor 结果 ----------------
    monitor->SerializeToXmlFile (flowmonFileName, true, true);

    Simulator::Destroy ();

    g_topologyOut.close ();
    g_transmissionOut.close ();

    std::cout << "Simulation finished. Check " << topologyFileName
            << ", " << transmissionFileName
            << " and " << flowmonFileName << " for outputs.\n";

    return 0;
    }
