#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include "ns3/stats-module.h"
#include "ns3/vector.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/callback.h"
#include <map>
#include <set>

using namespace ns3;

const double SIM_AREA_SIZE = 500.0;
const double COMM_RANGE = 250.0;
const double TOPOLOGY_UPDATE_INTERVAL = 30.0;

NodeContainer nodes;
std::ofstream topologyFile("topology-changes.txt");
std::ofstream transmissionFile("node-transmissions.txt");

std::map<std::pair<uint32_t, uint32_t>, bool> activeLinks;
ApplicationContainer clientApps;
std::set<std::pair<uint32_t, uint32_t>> activeClients;

std::map<uint64_t, std::tuple<double, uint32_t, uint32_t>> packetUidToSrcDstTime;


void ClearAllClientApplications() {
    clientApps.Stop(Simulator::Now()); 
    clientApps = ApplicationContainer();
    activeClients.clear();
}

uint32_t GetNodeIdByIp(Ipv4Address ip) {
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        Ptr<Ipv4> ipv4 = nodes.Get(i)->GetObject<Ipv4>();
        if (!ipv4) {continue;}
        if (ipv4->GetAddress(1, 0).GetLocal() == ip) {
            return i;
        }
    }
    return UINT32_MAX;
}

uint32_t GetNodeIdFromContext(const std::string &context) {
    std::string prefix = "/NodeList/";
    size_t startPos = context.find(prefix);
    if (startPos == std::string::npos) {return 0;}
    startPos += prefix.size();
    size_t endPos = context.find('/', startPos);
    std::string nodeIdStr = context.substr(startPos, endPos - startPos);
    return std::stoul(nodeIdStr);
}

void UpdateTopology(NodeContainer& nodes) {
    activeLinks.clear();
    Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
    uv->SetAttribute("Min", DoubleValue(0.0));
    uv->SetAttribute("Max", DoubleValue(1.0));

    std::vector<Ptr<MobilityModel>> mob(nodes.GetN());
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        mob[i] = nodes.Get(i)->GetObject<MobilityModel>();
    }

    double linkProb = 0.1;
    for (uint32_t i = 0; i < nodes.GetN(); ++i) {
        for (uint32_t j = i + 1; j < nodes.GetN(); ++j) {
            double dist = CalculateDistance(mob[i]->GetPosition(), mob[j]->GetPosition());
            if (dist <= COMM_RANGE && uv->GetValue() < linkProb) {
                activeLinks[{i, j}] = true;
                activeLinks[{j, i}] = true;
            }
        }
    }

    double timeNow = Simulator::Now().GetSeconds();
    topologyFile << "Time: " << timeNow << "s | Active Links: ";
    for (auto &link : activeLinks) {
        if (link.second && link.first.first < link.first.second) {
            topologyFile << link.first.first << "<->" << link.first.second << " ";
        }
    }
    topologyFile << "\n";
}

void LogTransmission(uint32_t nodeId, const std::string& type, uint64_t uid) {
    double now = Simulator::Now().GetSeconds();

    // 仅记录 0 ~ 120s 的包
    if (now >= 0 && now <= 120.0) {
        Ptr<MobilityModel> mobility = nodes.Get(nodeId)->GetObject<MobilityModel>();
        Vector pos = mobility->GetPosition();

        // 输出调试信息
        auto it = packetUidToSrcDstTime.find(uid);
        if (it != packetUidToSrcDstTime.end()) {
            double createdAt = std::get<0>(it->second);
            uint32_t src = std::get<1>(it->second);
            uint32_t dst = std::get<2>(it->second);
            std::cout << "[TxTrace] UID: " << uid
                      << " | Sent at: " << now
                      << " | Created at: " << createdAt
                      << " | src: " << src << " -> dst: " << dst
                      << " | NodeId: " << nodeId << std::endl;
        } else {
            std::cout << "[TxTrace] UID: " << uid
                      << " | Sent at: " << now
                      << " | Unknown origin | NodeId: " << nodeId << std::endl;
        }

        // 原始文件记录
        transmissionFile << now << "," << nodeId << "," << type << "," << uid << ","
                         << pos.x << "," << pos.y << "," << pos.z << "\n";
    }
}


void TxTrace(std::string context, Ptr<const Packet> packet) {
    uint32_t nodeId = GetNodeIdFromContext(context);
    uint64_t packetUid = packet->GetUid();
    LogTransmission(nodeId, "DATA", packetUid);
}

void ServerReceive(Ptr<const Packet> pkt, const Address& srcAddr, const Address& dstAddr) {
    InetSocketAddress dstInet = InetSocketAddress::ConvertFrom(dstAddr);
    uint32_t serverNodeId = GetNodeIdByIp(dstInet.GetIpv4());
    if (serverNodeId != UINT32_MAX) {
        LogTransmission(serverNodeId, "ACK", pkt->GetUid());
    }
}

void RemoveClientRecord(uint32_t src, uint32_t dst) {
    activeClients.erase({src, dst});
}

void RecordPacketUid(uint32_t src, uint32_t dst, Ptr<const Packet> pkt) {
    double now = Simulator::Now().GetSeconds();
    uint64_t uid = pkt->GetUid();
    packetUidToSrcDstTime[uid] = std::make_tuple(now, src, dst);
    std::cout << "[PacketUID-Create] Time: " << now
              << " | UID: " << uid
              << " | src: " << src << " -> dst: " << dst << std::endl;
}

void CreateClientApplication(uint32_t src, uint32_t dst) {
    ClearAllClientApplications();
    std::cout << "[CreateClientApp] Time: " << Simulator::Now().GetSeconds()
            << " | src: " << src << " -> dst: " << dst 
            << " | activeClients size: " << activeClients.size() << std::endl;
    if (activeClients.find({src, dst}) != activeClients.end()) {return;}
    activeClients.insert({src, dst});

    Ipv4Address dstIp = nodes.Get(dst)->GetObject<Ipv4>()->GetAddress(1, 0).GetLocal();
    UdpEchoClientHelper client(dstIp, 2000);
    client.SetAttribute("MaxPackets", UintegerValue(1));
    client.SetAttribute("Interval", TimeValue(Seconds(0.01)));
    client.SetAttribute("PacketSize", UintegerValue(512));

    ApplicationContainer app = client.Install(nodes.Get(src));

    // app.Get(0)->TraceConnectWithoutContext("Tx", MakeBoundCallback(&RecordPacketUid, src, dst));

    app.Start(Simulator::Now());
    app.Stop(Simulator::Now() + Seconds(0.5));

    app.Get(0)->TraceConnectWithoutContext("Destroy", MakeBoundCallback(&RemoveClientRecord, src, dst));
    clientApps.Add(app);
}

void ScheduleTransmissions() {
    std::vector<std::pair<uint32_t, uint32_t>> activeList;
    for (auto &link : activeLinks) {
        if (link.second && link.first.first < link.first.second) {
            activeList.push_back(link.first);
        }
    }

    if (!activeList.empty()) {
        Ptr<UniformRandomVariable> uv = CreateObject<UniformRandomVariable>();
        uv->SetAttribute("Min", DoubleValue(0.0));
        uv->SetAttribute("Max", DoubleValue(activeList.size()));
        uint32_t idx = std::min((uint32_t)uv->GetValue(), (uint32_t)activeList.size() - 1);
        uint32_t src = activeList[idx].first;
        uint32_t dst = activeList[idx].second;
        CreateClientApplication(src, dst);
        CreateClientApplication(dst, src);
    }

    std::cout << "当前时间: " << Simulator::Now().GetSeconds()
              << "s | 候选链路数: " << activeList.size()
              << " | 活跃客户端数: " << activeClients.size() << std::endl;
}

void ScheduleAllTransmissions(double startTime, double endTime, double interval) {
    for (double t = startTime; t <= endTime; t += interval) {
        Simulator::Schedule(Seconds(t), &ScheduleTransmissions);
    }
}


int main(int argc, char *argv[]) {
    uint32_t numNodes = 10;
    double simulationTime = 60.0;
    double warmupTime = 5.0;

    SeedManager::SetSeed(12345);
    nodes.Create(numNodes);

    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::RandomBoxPositionAllocator",
        "X", StringValue("ns3::UniformRandomVariable[Min=0|Max=500]"),
        "Y", StringValue("ns3::UniformRandomVariable[Min=0|Max=500]"),
        "Z", StringValue("ns3::UniformRandomVariable[Min=50|Max=150]"));
    mobility.SetMobilityModel("ns3::GaussMarkovMobilityModel",
        "MeanVelocity", StringValue("ns3::UniformRandomVariable[Min=10|Max=20]"),
        "Bounds", BoxValue(Box(0, SIM_AREA_SIZE, 0, SIM_AREA_SIZE, 50, 150)));
    mobility.Install(nodes);

    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    YansWifiPhyHelper phy;
    phy.SetChannel(channel.Create());
    phy.Set("TxPowerStart", DoubleValue(28.0));
    phy.Set("TxPowerEnd", DoubleValue(28.0));
    phy.Set("RxSensitivity", DoubleValue(-90.0));

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac", "QosSupported", BooleanValue(true));

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ac);
    wifi.SetRemoteStationManager("ns3::MinstrelHtWifiManager");

    NetDeviceContainer devices = wifi.Install(phy, mac, nodes);

    InternetStackHelper stack;
    stack.Install(nodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    UdpEchoServerHelper ackServer(2000);
    ApplicationContainer servers = ackServer.Install(nodes);
    servers.Start(Seconds(0.0));
    servers.Stop(Seconds(simulationTime));

    Config::Connect(
        "/NodeList/*/DeviceList/*/$ns3::WifiNetDevice/Mac/MacTx",
        MakeCallback(&TxTrace));

    for (uint32_t i = 0; i < servers.GetN(); ++i) {
        Ptr<Application> app = servers.Get(i);
        app->TraceConnectWithoutContext("RxWithAddresses", MakeCallback(&ServerReceive));
    }

    Simulator::Schedule(Seconds(warmupTime), &UpdateTopology, nodes);
    ScheduleAllTransmissions(warmupTime, simulationTime, 0.5);
    for (double t = TOPOLOGY_UPDATE_INTERVAL; t < simulationTime; t += TOPOLOGY_UPDATE_INTERVAL) {
        Simulator::Schedule(Seconds(t), &UpdateTopology, nodes);
    }

    FlowMonitorHelper flowmon;
    Ptr<FlowMonitor> monitor = flowmon.InstallAll();

    Simulator::Stop(Seconds(simulationTime));
    Simulator::Run();

    monitor->SerializeToXmlFile("uav-flowmon.xml", true, true);
    topologyFile.close();
    transmissionFile.close();
    Simulator::Destroy();

    return 0;
}
