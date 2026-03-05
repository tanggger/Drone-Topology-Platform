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
#include <algorithm>
#include <iomanip>
#include "ns3/flow-monitor-helper.h"
#include "ns3/ipv4-flow-classifier.h"


using namespace ns3;
using namespace std;

NS_LOG_COMPONENT_DEFINE("UavSimulation");

static ofstream g_transFile;
static ofstream g_topoFile;
static std::vector< std::set< std::pair<uint32_t,uint32_t> > > g_intervalLinks(20);
static std::map<uint32_t, uint32_t> g_ipToNodeId;

// *** 新增：记录位置用的文件流 ***
static ofstream g_posFile;

// IPv4发送事件回调
static void Ipv4Tracer(std::string context, Ptr<const Packet> packet, Ptr<Ipv4> ipv4, uint32_t interface)
{
    Ptr<Packet> pktCopy = packet->Copy();
    Ipv4Header ipHeader;
    pktCopy->RemoveHeader(ipHeader);

    if (ipHeader.GetProtocol() != 6) return; // 只处理 TCP

    TcpHeader tcpHeader;
    pktCopy->RemoveHeader(tcpHeader);

    bool isAck = tcpHeader.GetFlags() & TcpHeader::ACK;
    uint32_t payloadSize = pktCopy->GetSize();
    if (payloadSize == 0 && !isAck) return; // SYN/FIN控制包，不记录

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
        uint32_t idx = std::min((uint32_t)std::floor(Simulator::Now().GetSeconds() / 5.0), (uint32_t)19);
        g_intervalLinks[idx].insert(std::make_pair(a, b));
    }
}

// 每5秒调用，输出拓扑活动链路
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
    // 清空当前区间集合，为下一个区间重新统计
    g_intervalLinks[index].clear();
}

// *** 新增：记录所有节点位置的函数，每隔1秒调用一次 ***
static void RecordPositions(NodeContainer nodes)
{
    double now = Simulator::Now().GetSeconds();
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        Ptr<MobilityModel> mob = nodes.Get(i)->GetObject<MobilityModel>();
        Vector pos = mob->GetPosition();  // x, y, z
        // 这里存成 CSV 格式: time, nodeId, x, y, z
        g_posFile << now << "," << i << "," 
                  << pos.x << "," << pos.y << "," << pos.z << std::endl;
    }
    // 下一秒再次调度
    if (now < 100.0) {
        Simulator::Schedule(Seconds(1.0), &RecordPositions, nodes);
    }
}


int main(int argc, char *argv[])
{
    NodeContainer nodes;
    nodes.Create(20);

    LogComponentEnable("OnOffApplication", LOG_LEVEL_INFO);
    LogComponentEnable("PacketSink", LOG_LEVEL_INFO);

    // *** 修改移动模型：使用ConstantVelocityMobilityModel实现整体向前移动 ***
    MobilityHelper mobility;
    
    // 设置初始位置分配器 - 在起始区域内随机分布
    mobility.SetPositionAllocator("ns3::RandomBoxPositionAllocator",
        "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=100.0]"),    // 起始X范围缩小
        "Y", StringValue("ns3::UniformRandomVariable[Min=50.0|Max=250.0]"),   // Y方向保持较大范围
        "Z", StringValue("ns3::UniformRandomVariable[Min=20.0|Max=40.0]")     // 高度在20-40米之间
    );
    
    // 使用恒定速度移动模型
    mobility.SetMobilityModel("ns3::ConstantVelocityMobilityModel");
    mobility.Install(nodes);
    
    // 为每个节点设置向前的速度向量（带有小幅随机扰动）
    Ptr<UniformRandomVariable> randVel = CreateObject<UniformRandomVariable>();
    randVel->SetStream(42);  // 设置随机种子
    
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        Ptr<ConstantVelocityMobilityModel> cvMobility = 
            nodes.Get(i)->GetObject<ConstantVelocityMobilityModel>();
        
        // 基础向前速度：主要向X正方向移动
        double baseVelX = 12.0;  // 基础前进速度 12 m/s
        double baseVelY = 0.0;   // Y方向基础速度为0
        double baseVelZ = 0.0;   // Z方向基础速度为0
        
        // 添加随机扰动（模拟编队飞行中的小幅调整）
        double perturbX = randVel->GetValue(-2.0, 2.0);   // X方向扰动 ±2 m/s
        double perturbY = randVel->GetValue(-3.0, 3.0);   // Y方向扰动 ±3 m/s  
        double perturbZ = randVel->GetValue(-1.0, 1.0);   // Z方向扰动 ±1 m/s
        
        Vector velocity(baseVelX + perturbX, baseVelY + perturbY, baseVelZ + perturbZ);
        cvMobility->SetVelocity(velocity);
    }

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ac);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("VhtMcs0"), 
                                 "ControlMode", StringValue("VhtMcs0"));

    // 修改无线信道模型，添加路径损失和多径衰落
    YansWifiChannelHelper channel;
    
    // 添加路径损失模型 - 使用三段对数距离模型
    channel.AddPropagationLoss("ns3::ThreeLogDistancePropagationLossModel",
                              "Distance0", DoubleValue(1.0),
                              "Distance1", DoubleValue(100.0),  // 从200降低到100
                              "Distance2", DoubleValue(250.0),  // 从500降低到250
                              "Exponent0", DoubleValue(2.5),    // 从3.0降低到2.5
                              "Exponent1", DoubleValue(3.0),    // 从3.5降低到3.0
                              "Exponent2", DoubleValue(3.5),    // 从4.0降低到3.5
                              "ReferenceLoss", DoubleValue(46.6777));

    // 添加多径衰落模型 - 使用Nakagami模型但减轻衰落
    channel.AddPropagationLoss("ns3::NakagamiPropagationLossModel",
                              "Distance1", DoubleValue(50.0),  // 从80降低到50
                              "Distance2", DoubleValue(150.0), // 从320降低到150
                              "m0", DoubleValue(1.5),    // 从1.0提高到1.5
                              "m1", DoubleValue(1.0),    // 从0.75提高到1.0
                              "m2", DoubleValue(0.75));  // 从0.5提高到0.75

    // 添加延迟模型
    channel.SetPropagationDelay("ns3::ConstantSpeedPropagationDelayModel",
                               "Speed", DoubleValue(299792458));  // 光速传播

    Ptr<YansWifiChannel> wifiChannel = channel.Create();
    YansWifiPhyHelper phy;
    phy.SetChannel(wifiChannel);
    phy.Set("TxPowerStart", DoubleValue(33.0));  // 从28.0提高到33.0
    phy.Set("TxPowerEnd", DoubleValue(33.0));    // 从28.0提高到33.0
    phy.Set("RxSensitivity", DoubleValue(-93.0)); // 从-90.0提高到-93.0
    // 添加噪声模型
    phy.Set("RxNoiseFigure", DoubleValue(6.0)); // 从7.0降低到6.0

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    InternetStackHelper stack;
    
    // 配置TCP参数
    Config::SetDefault("ns3::TcpSocket::ConnTimeout", TimeValue(Seconds(5.0)));  // 连接超时时间
    Config::SetDefault("ns3::TcpSocket::ConnCount", UintegerValue(6));  // 连接尝试次数
    Config::SetDefault("ns3::TcpSocket::DataRetries", UintegerValue(6)); // 数据重传次数
    Config::SetDefault("ns3::TcpSocket::DelAckTimeout", TimeValue(Seconds(0.2))); // 延迟ACK超时
    Config::SetDefault("ns3::TcpSocketBase::MinRto", TimeValue(Seconds(0.5))); // 最小RTO值
    Config::SetDefault("ns3::TcpL4Protocol::SocketType", StringValue("ns3::TcpNewReno")); // 使用NewReno拥塞控制
    
    stack.Install(nodes);
    Ipv4AddressHelper address;
    address.SetBase("10.0.0.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        Ipv4Address ip = interfaces.GetAddress(i);
        g_ipToNodeId[ip.Get()] = nodes.Get(i)->GetId();
    }

    uint16_t sinkPort = 9999;
    PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), sinkPort));
    ApplicationContainer sinkApps = sinkHelper.Install(nodes);
    sinkApps.Start(Seconds(0.0));
    sinkApps.Stop(Seconds(100.0));

    Ptr<UniformRandomVariable> rand = CreateObject<UniformRandomVariable>();
    rand->SetStream(1);
    double step = 0.1;
    for (double t = 0.0; t < 100.0; t += step) {
        if (rand->GetValue() < 0.6) {  // 从0.8减少到0.6，降低通信尝试频率
            uint32_t sender = rand->GetInteger(0, nodes.GetN() - 1);
            uint32_t receiver = rand->GetInteger(0, nodes.GetN() - 1);
            while (receiver == sender) {
                receiver = rand->GetInteger(0, nodes.GetN() - 1);
            }
            
            // 计算节点间距离，如果距离太远，则跳过此次传输尝试
            Ptr<MobilityModel> senderMobility = nodes.Get(sender)->GetObject<MobilityModel>();
            Ptr<MobilityModel> receiverMobility = nodes.Get(receiver)->GetObject<MobilityModel>();
            double distance = senderMobility->GetDistanceFrom(receiverMobility);
            if (distance > 150.0) { // 调整距离限制从200米降到150米，适应编队飞行
                continue;
            }
            
            OnOffHelper onoff("ns3::TcpSocketFactory", Address());
            onoff.SetAttribute("PacketSize", UintegerValue(512));
            onoff.SetAttribute("DataRate", StringValue("2Mbps"));  // 从5Mbps降到2Mbps
            onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=0.008]")); // 从0.005增加到0.008
            onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0.0]"));

            Address remoteAddress(InetSocketAddress(interfaces.GetAddress(receiver), sinkPort));
            onoff.SetAttribute("Remote", AddressValue(remoteAddress));

            ApplicationContainer app = onoff.Install(nodes.Get(sender));
            app.Start(Seconds(t));
            app.Stop(Seconds(t + 0.05));  // 从0.02增加到0.05
        }
    }

    // 打开输出文件（CSV 格式记录）
    g_transFile.open("node-transmissions.csv");
    // 在表头加入适当列名
    g_transFile << "time_s,nodeId,eventType" << std::endl;

    g_topoFile.open("topology-changes.txt");

    // *** 新增：记录节点位置的文件，CSV 格式 ***
    g_posFile.open("node-positions.csv");
    g_posFile << "time_s,nodeId,x,y,z" << std::endl;

    Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Tx", MakeCallback(&Ipv4Tracer));
    Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Rx", MakeCallback(&Ipv4Tracer));

    FlowMonitorHelper flowmonHelper;
    Ptr<FlowMonitor> flowmon = flowmonHelper.InstallAll();

    // 每5秒输出拓扑活动链路
    for (uint32_t idx = 0; idx < 20; ++idx) {
        Simulator::Schedule(Seconds((idx + 1) * 5.0), &TopologyOutput, idx);
    }

    // *** 安排每1秒调用一次记录节点位置的函数 ***
    Simulator::Schedule(Seconds(1.0), &RecordPositions, nodes);

    Simulator::Stop(Seconds(100.0));
    Simulator::Run();

    // *** 在仿真结束后，计算并输出 FlowMonitor 结果 ***
    flowmon->CheckForLostPackets();
    // 把详细数据序列化到 XML，供以后深入分析
    flowmon->SerializeToXmlFile("flowmon.xml", true, true);

    // 也可以将关键数据统计到 CSV
    ofstream flowStats("flow-stats.csv");
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
    return 0;
}