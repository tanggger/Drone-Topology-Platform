#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/stats-module.h"
#include "ns3/aodv-helper.h"
#include "ns3/vector.h"
#include "ns3/flow-monitor-module.h"
#include <map>
#include <cmath>

const double SIM_AREA_SIZE = 500.0;
const double COMM_RANGE = 250.0;
const double TOPOLOGY_UPDATE_INTERVAL = 30.0;
// const double PACKET_INTERVAL = 0.5;

NodeContainer nodes;
std::ofstream topologyFile("topology-changes.txt");
std::ofstream transmissionFile("node-transmissions.txt");
std::map<std::pair<uint32_t, uint32_t>, bool> activeLinks;
ApplicationContainer clientApps;

// [!新增] 全局记录活跃客户端 (src,dst)
std::set<std::pair<uint32_t, uint32_t>> activeClients;

// // 三维距离计算函数
// double CalculateDistance(Vector a, Vector b) {
//     return std::sqrt(std::pow(a.x-b.x,2) + std::pow(a.y-b.y,2) + std::pow(a.z-b.z,2));
// }
void ClearAllClientApplications() {
    // 不手动 Stop 应用，直接清空容器和记录
    clientApps = ApplicationContainer(); 
    activeClients.clear();               
}

uint32_t GetNodeIdByIp(Ipv4Address ip) {
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        if (nodes.Get(i)->GetObject<Ipv4>()->GetAddress(1,0).GetLocal() == ip) {
            return i;
        }
    }
    return UINT32_MAX;
}
void
PreEstablishAllRoutes(NodeContainer& nodes, uint16_t port,
                      double startTime, double stopTime)
{
    // 让每个节点都安装一个 UdpEchoServer，以便任何时候都能接收 Echo
    // 当然，你若已经在 main() 装过，就可以跳过这步
    // UdpEchoServerHelper serverHelper(port);
    // serverHelper.Install(nodes).Start(Seconds(0.0));

    // 遍历所有节点对 (i, j), i != j
    uint32_t n = nodes.GetN();
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = 0; j < n; ++j) {
            if (i == j) {continue;}

            // 取得目标节点 j 的 IP
            Ipv4Address dstIp = nodes.Get(j)->GetObject<Ipv4>()->GetAddress(1,0).GetLocal();

            // 创建一个最简单的 UdpEchoClient，发送 1 个数据包
            UdpEchoClientHelper client(dstIp, port);
            client.SetAttribute("MaxPackets", UintegerValue(1));
            client.SetAttribute("Interval", TimeValue(Seconds(1.0))); // 无关紧要, 因为只发1包
            client.SetAttribute("PacketSize", UintegerValue(32));     // 小一点就行
            
            // 安装到节点 i
            ApplicationContainer cApp = client.Install(nodes.Get(i));
            // 在指定时间窗口内运行
            cApp.Start(Seconds(startTime));
            cApp.Stop(Seconds(stopTime));
        }
    }
}

uint32_t GetNodeIdFromContext(const std::string &context) {
    // context 形如 "/NodeList/2/DeviceList/0/$ns3::WifiNetDevice/Mac/MacTx"
    // 我们只要抓出中间那个 "2" 就行
    std::string prefix = "/NodeList/";
    size_t startPos = context.find(prefix);
    if (startPos == std::string::npos) {
        return 0;
    }
    startPos += prefix.size();  // 跳过 "/NodeList/"
    size_t endPos = context.find('/', startPos);
    // endPos - startPos 就是节点编号的字符串长度
    std::string nodeIdStr = context.substr(startPos, endPos - startPos);
    return std::stoul(nodeIdStr);  // 转成 uint32_t
}


// 更新拓扑结构（随机）
void UpdateTopology(NodeContainer& nodes) {
    activeLinks.clear();

    // 随机数生成器
    Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
    uv->SetAttribute("Min", DoubleValue(0.0));
    uv->SetAttribute("Max", DoubleValue(1.0));

    // 获取每个节点的 MobilityModel
    std::vector<Ptr<MobilityModel>> mob(nodes.GetN());
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        mob[i] = nodes.Get(i)->GetObject<MobilityModel>();
    }

    double  linkProb = 0.1;
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        for (uint32_t j = i + 1; j < nodes.GetN(); ++j) {

            // 1) 计算节点 i, j 间距离
            double dist = CalculateDistance(mob[i]->GetPosition(), mob[j]->GetPosition());
            
            // 2) 只有在距 <= COMM_RANGE 才“有机会”随机激活
            if (dist <= COMM_RANGE) {
                double r = uv->GetValue(); 
                if (r < linkProb) {
                    activeLinks[{i,j}] = true;
                    activeLinks[{j,i}] = true;
                }
            }
        }
    }


    // 记录拓扑变化
    double timeNow = Simulator::Now().GetSeconds();
    topologyFile << "Time: " << timeNow << "s | Active Links: ";
    for (auto &link : activeLinks) {
        if (link.second) {
            topologyFile << link.first.first << "<->" << link.first.second << " ";
        }
    }
    topologyFile << "\n";
}

// 记录传输事件（包括ACK）
void LogTransmission(uint32_t nodeId, const std::string& type) {
    double now = Simulator::Now().GetSeconds();
    if (now >= 15.0 && now <= 30.0) // 仅在[15s,30s]视为地面侦测到
    {
        Ptr<Node> node = nodes.Get(nodeId);
        Ptr<MobilityModel> mobility = node->GetObject<MobilityModel>();
        Vector pos = mobility->GetPosition();
        
        transmissionFile << now << ","
                         << nodeId << ","
                         << type << ","
                         << pos.x << "," << pos.y << "," << pos.z << "\n";
    }
}

// 数据包发送回调
void TxTrace(std::string context, Ptr<const Packet> packet) {
    uint32_t nodeId = GetNodeIdFromContext(context);
    LogTransmission(nodeId, "DATA");
}


void ClientReceiveAck(Ptr<const Packet> packet, const Address& address) {
    InetSocketAddress inetAddr = InetSocketAddress::ConvertFrom(address);
    uint32_t nodeId = GetNodeIdByIp(inetAddr.GetIpv4());
    if(nodeId != UINT32_MAX) {
        LogTransmission(nodeId, "ACK_RECEIVED");
    }
}

// 服务器接收数据包回调（更名为ServerReceive）
void ServerReceive(Ptr<const Packet> pkt, const Address& srcAddr, const Address& dstAddr)
{
    // 服务器是 dstAddr 对应的节点
    InetSocketAddress dstInet = InetSocketAddress::ConvertFrom(dstAddr);
    uint32_t serverNodeId = GetNodeIdByIp(dstInet.GetIpv4());

    // 如果查到有效 ID，就记录 ACK 事件
    if (serverNodeId != UINT32_MAX) {
        LogTransmission(serverNodeId, "ACK"); 
    }
}

void RemoveClientRecord(uint32_t src, uint32_t dst) {
    activeClients.erase({src, dst});
}

void CreateClientApplication(uint32_t src, uint32_t dst) {
    // [!新增] 避免重复创建
    if (activeClients.find({src, dst}) != activeClients.end()) {
        return;
    }
    activeClients.insert({src, dst});

    UdpEchoClientHelper client(
        nodes.Get(dst)->GetObject<Ipv4>()->GetAddress(1,0).GetLocal(), 
        2000
    );
    client.SetAttribute("MaxPackets", UintegerValue(1));   
    client.SetAttribute("Interval", TimeValue(Seconds(1.0))); 
    client.SetAttribute("PacketSize", UintegerValue(512));

    ApplicationContainer app = client.Install(nodes.Get(src));
    // [!修改] 严格限制客户端存活时间（1秒足够发送1个包）
    app.Start(Simulator::Now());
    app.Stop(Simulator::Now() + Seconds(5.0)); 

    // [!新增] 应用停止时自动清理记录
    app.Get(0)->TraceConnectWithoutContext(
        "Destroy", 
        MakeBoundCallback(&RemoveClientRecord, src, dst)
    );

    // 绑定接收回调...
    clientApps.Add(app);
}

// 修改后的周期发送函数
void ScheduleTransmissions() 
{
    // 收集当前所有激活链路（只取 i<j 避免重复）
    std::vector<std::pair<uint32_t,uint32_t>> activeList;
    for (auto &link : activeLinks) {
        if (link.second && link.first.first < link.first.second) {
            activeList.push_back(link.first);
        }
    }
    
    if (!activeList.empty()) {
        if (activeClients.empty()) {
            Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
            uv->SetAttribute("Min", DoubleValue(0.0));
            uv->SetAttribute("Max", DoubleValue(activeList.size()));
            
            // 生成 [0, activeList.size()-1] 间整数
            auto idx = (uint32_t) uv->GetValue();
            if (idx == activeList.size()) {
                idx = activeList.size() - 1; // 边界保护
            }

            uint32_t src = activeList[idx].first;
            uint32_t dst = activeList[idx].second;

            CreateClientApplication(src, dst);
            CreateClientApplication(dst, src);
        }
    }

    Simulator::Schedule(Seconds(0.5), &ScheduleTransmissions);
}

// void ScheduleTransmissions() 
// {
//     uint32_t n = nodes.GetN();

//     Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
//     uv->SetAttribute("Min", DoubleValue(0.0));
//     uv->SetAttribute("Max", DoubleValue(n));

//     if (activeClients.empty()) {
//         uint32_t src = uv->GetInteger(0, n - 1);
//         uint32_t dst = src;
//         while (dst == src) {
//             dst = uv->GetInteger(0, n - 1);
//         }

//         CreateClientApplication(src, dst);
//         CreateClientApplication(dst, src);
//     }

//     Simulator::Schedule(Seconds(1.0), &ScheduleTransmissions);
// }

int main(int argc , char *argv[]) {
    uint32_t numNodes = 10;
    double simulationTime = 120.0;
    SeedManager::SetSeed(12345);

    nodes.Create(numNodes);
    double warmupTime = 5.0;

    // 三维移动模型配置
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::RandomBoxPositionAllocator",
        "X", StringValue("ns3::UniformRandomVariable[Min=0|Max=" + std::to_string(SIM_AREA_SIZE) + "]"),
        "Y", StringValue("ns3::UniformRandomVariable[Min=0|Max=" + std::to_string(SIM_AREA_SIZE) + "]"),
        "Z", StringValue("ns3::UniformRandomVariable[Min=50|Max=150]"));
    mobility.SetMobilityModel("ns3::GaussMarkovMobilityModel",
        "MeanVelocity", StringValue("ns3::UniformRandomVariable[Min=10|Max=20]"),
        "Bounds", BoxValue(Box(0, SIM_AREA_SIZE, 0, SIM_AREA_SIZE, 50, 150)));
    mobility.Install(nodes);

    // 无线网络配置
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.Set("TxPowerStart", DoubleValue(28.0));
    phy.Set("TxPowerEnd", DoubleValue(28.0));

    // 提高接收灵敏度，让边缘信号更容易被解码, e.g. -90 dBm
    phy.Set("RxSensitivity", DoubleValue(-90.0));
    phy.SetChannel(channel.Create());

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac",
               "QosSupported", BooleanValue(true));
    
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ac);
    wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager");
    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    // 协议栈配置
    InternetStackHelper stack;
    AodvHelper aodv;
    aodv.Set("ActiveRouteTimeout", TimeValue(Seconds(20))); 
    aodv.Set("HelloInterval", TimeValue(Seconds(0.5)));
    aodv.Set("EnableHello", BooleanValue(true));

    stack.SetRoutingHelper(aodv);
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    // 初始化ACK服务器
    UdpEchoServerHelper ackServer(2000);
    ApplicationContainer servers = ackServer.Install(nodes);
    servers.Start(Seconds(0.0));
    servers.Stop(Seconds(simulationTime));

    // 绑定回调函数
    Config::Connect("/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacTx", MakeCallback(&TxTrace));


    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        std::cout << "Attached Rx trace to server node " << i << std::endl;
        Ptr<UdpEchoServer> server = servers.Get(i)->GetObject<UdpEchoServer>();
        if (server) {
            server->TraceConnectWithoutContext(
                "RxWithAddresses",
                MakeCallback(&ServerReceive)
            );
            std::cout << "Attached Rx trace to server node " << i << std::endl;
        } else {
            std::cout << "Warning: failed to get UdpEchoServer from node " << i << std::endl;
        }
    }


    PreEstablishAllRoutes(nodes, 2000, 0.1 /* startTime */, 4.0 /* stopTime */);
    Simulator::Schedule(Seconds(warmupTime), &UpdateTopology, nodes);
    Simulator::Schedule(Seconds(warmupTime), &ScheduleTransmissions);    

    // 调度拓扑更新和包发送
    // Simulator::Schedule(Seconds(10.0), &UpdateTopology, nodes);
    // Simulator::Schedule(Seconds(10.0), &ScheduleTransmissions);
    for (double t = TOPOLOGY_UPDATE_INTERVAL; t < simulationTime; t += TOPOLOGY_UPDATE_INTERVAL) {
        Simulator::Schedule(Seconds(t), &UpdateTopology, nodes);
    }

    // 流量监控
    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();
    
    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    // 结果输出
    monitor->SerializeToXmlFile("uav-flowmon.xml", true, true);
    topologyFile.close();
    transmissionFile.close();
    Simulator::Destroy();
    return 0;
}