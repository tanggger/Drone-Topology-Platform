#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/wifi-module.h"
#include "ns3/mobility-module.h"
#include "ns3/applications-module.h"
#include <fstream>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("UavClusterSimulation");

std::ofstream txTraceFile;
std::ofstream topoTraceFile;
std::map<std::pair<uint32_t, uint32_t>, bool> linkStatusMap;

uint32_t ParseNodeId(const std::string& context) {
    size_t nodeStart = context.find("/NodeList/");
    if (nodeStart == std::string::npos) return UINT32_MAX;
    nodeStart += 10;
    size_t nodeEnd = context.find("/", nodeStart);
    return std::stoi(context.substr(nodeStart, nodeEnd - nodeStart));
}

void TraceTcpTx(std::string context, Ptr<const Packet> packet, const Address& addr) {
    uint32_t txNodeId = ParseNodeId(context);
    Ipv4Address destAddr = InetSocketAddress::ConvertFrom(addr).GetIpv4();
    uint32_t rxNodeId = UINT32_MAX;
    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i) {
        auto ipv4 = NodeList::GetNode(i)->GetObject<Ipv4>();
        if (ipv4 && ipv4->GetNInterfaces() > 1 && ipv4->GetAddress(1, 0).GetLocal() == destAddr) {
            rxNodeId = i;
            break;
        }
    }
    if (rxNodeId != UINT32_MAX) {
        auto link = (txNodeId < rxNodeId) ? std::make_pair(txNodeId, rxNodeId) : std::make_pair(rxNodeId, txNodeId);
        linkStatusMap[link] = true;
    }
    txTraceFile << Simulator::Now().GetSeconds() << "\t" << txNodeId << "\tTX" << std::endl;
}

void TraceTcpRx(std::string context, Ptr<const Packet> packet, const Address& from) {
    uint32_t rxNodeId = ParseNodeId(context);
    Ipv4Address srcAddr = InetSocketAddress::ConvertFrom(from).GetIpv4();

    uint32_t txNodeId = UINT32_MAX;
    for (uint32_t i = 0; i < NodeList::GetNNodes(); ++i) {
        auto ipv4 = NodeList::GetNode(i)->GetObject<Ipv4>();
        if (ipv4 && ipv4->GetNInterfaces() > 1) {
            if (ipv4->GetAddress(1, 0).GetLocal() == srcAddr) {
                txNodeId = i;
                break;
            }
        }
    }

    if (txNodeId != UINT32_MAX) {
        auto link = (txNodeId < rxNodeId) ? std::make_pair(txNodeId, rxNodeId)
                                          : std::make_pair(rxNodeId, txNodeId);
        linkStatusMap[link] = true;
    }

    txTraceFile << Simulator::Now().GetSeconds()
                << "\t" << rxNodeId << "\tRX_FROM_" << txNodeId << std::endl;
}


void LogTopology() {
    topoTraceFile << Simulator::Now().GetSeconds() << "\t";
    for (auto& link : linkStatusMap) {
        if (link.second) {
            topoTraceFile << link.first.first << "-" << link.first.second << " ";
        }
    }
    topoTraceFile << std::endl;
    linkStatusMap.clear();
    Simulator::Schedule(Seconds(10), &LogTopology);
}

int main(int argc, char *argv[]) {
    uint32_t numUavs = 20;
    double simTime = 100.0;

    NodeContainer uavNodes;
    uavNodes.Create(numUavs);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel");
    mobility.Install(uavNodes);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211ac);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
        "DataMode", StringValue("VhtMcs9"),
        "ControlMode", StringValue("VhtMcs0"));

    YansWifiPhyHelper phy;
    phy.Set("TxPowerStart", DoubleValue(28.0));
    phy.Set("TxPowerEnd", DoubleValue(28.0));
    phy.Set("RxSensitivity", DoubleValue(-90.0));
    YansWifiChannelHelper channel = YansWifiChannelHelper::Default();
    phy.SetChannel(channel.Create());

    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");

    NetDeviceContainer devices = wifi.Install(phy, mac, uavNodes);
    InternetStackHelper stack;
    stack.Install(uavNodes);

    Ipv4AddressHelper address;
    address.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = address.Assign(devices);

    Ptr<UniformRandomVariable> startRv = CreateObject<UniformRandomVariable>();
    startRv->SetAttribute("Min", DoubleValue(1.0));
    startRv->SetAttribute("Max", DoubleValue(simTime - 1));

    for (uint32_t i = 0; i < numUavs; ++i) {
        uint32_t destIndex = (i + 1) % numUavs;
        Ipv4Address destAddr = interfaces.GetAddress(destIndex);

        PacketSinkHelper sinkHelper("ns3::TcpSocketFactory", InetSocketAddress(Ipv4Address::GetAny(), 9));
        ApplicationContainer sinkApps = sinkHelper.Install(uavNodes.Get(destIndex));
        sinkApps.Start(Seconds(0.0));
        sinkApps.Stop(Seconds(simTime));

        Ptr<PacketSink> sink = DynamicCast<PacketSink>(sinkApps.Get(0));
        sink->TraceConnect("Rx", "", MakeCallback(&TraceTcpRx));

        BulkSendHelper bulkHelper("ns3::TcpSocketFactory", InetSocketAddress(destAddr, 9));
        bulkHelper.SetAttribute("MaxBytes", UintegerValue(512));
        ApplicationContainer bulkApps = bulkHelper.Install(uavNodes.Get(i));
        double startTime = startRv->GetValue();
        bulkApps.Start(Seconds(startTime));
        bulkApps.Stop(Seconds(simTime));

        Ptr<Socket> socket = Socket::CreateSocket(uavNodes.Get(i), TcpSocketFactory::GetTypeId());
        socket->TraceConnect("Tx", "", MakeCallback(&TraceTcpTx));
    }

    txTraceFile.open("node-transmissions.txt");
    topoTraceFile.open("topology-changes.txt");

    Simulator::Schedule(Seconds(10), &LogTopology);
    Simulator::Stop(Seconds(simTime));
    Simulator::Run();
    Simulator::Destroy();

    txTraceFile.close();
    topoTraceFile.close();

    return 0;
}
