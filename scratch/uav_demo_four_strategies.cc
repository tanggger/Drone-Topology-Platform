/*
 * uav_demo_four_strategies.cc
 * 
 * 演示四种资源分配策略的简单UAV仿真程序
 */

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include <iostream>
#include <fstream>
#include <cmath>

using namespace ns3;

NS_LOG_COMPONENT_DEFINE("UAVDemoFourStrategies");

// 全局变量
std::string g_strategy = "static";
uint32_t g_numUAVs = 15;
uint32_t g_numChannels = 3;
double g_duration = 100.0;
std::string g_outputDir = "output/demo";

// 简单的资源分配函数
void AllocateChannels(NodeContainer nodes, std::map<uint32_t, uint32_t>& channelAssignment) {
    if (g_strategy == "static") {
        // 静态轮询分配
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            channelAssignment[i] = i % g_numChannels;
        }
    }
    else if (g_strategy == "greedy") {
        // 贪心：基于节点ID的简单策略
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            channelAssignment[i] = (i * 2) % g_numChannels;
        }
    }
    else if (g_strategy == "graph_coloring") {
        // 图着色：模拟着色算法
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            channelAssignment[i] = (i / 5) % g_numChannels;
        }
    }
    else {  // interference_aware
        // 干扰感知：交替分配
        for (uint32_t i = 0; i < nodes.GetN(); ++i) {
            channelAssignment[i] = ((i / 3) + (i % 3)) % g_numChannels;
        }
    }
}

int main(int argc, char *argv[]) {
    // 解析命令行参数
    CommandLine cmd;
    cmd.AddValue("strategy", "资源分配策略 (static/greedy/graph_coloring/interference_aware)", g_strategy);
    cmd.AddValue("numUAVs", "UAV节点数量", g_numUAVs);
    cmd.AddValue("numChannels", "可用信道数量", g_numChannels);
    cmd.AddValue("duration", "仿真时长(秒)", g_duration);
    cmd.AddValue("outputDir", "输出目录", g_outputDir);
    cmd.Parse(argc, argv);
    
    std::cout << "\n========================================" << std::endl;
    std::cout << "UAV资源分配仿真 - " << g_strategy << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "节点数量: " << g_numUAVs << std::endl;
    std::cout << "信道数量: " << g_numChannels << std::endl;
    std::cout << "仿真时长: " << g_duration << " 秒" << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // 创建输出目录
    std::string mkdirCmd = "mkdir -p " + g_outputDir;
    [[maybe_unused]] int ret = system(mkdirCmd.c_str());
    
    // 创建节点
    NodeContainer uavNodes;
    uavNodes.Create(g_numUAVs);
    
    // 配置移动模型
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::RandomBoxPositionAllocator",
                                  "X", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"),
                                  "Y", StringValue("ns3::UniformRandomVariable[Min=0.0|Max=500.0]"),
                                  "Z", StringValue("ns3::ConstantRandomVariable[Constant=50.0]"));
    
    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                             "Bounds", RectangleValue(Rectangle(0, 500, 0, 500)),
                             "Speed", StringValue("ns3::UniformRandomVariable[Min=5.0|Max=15.0]"),
                             "Distance", DoubleValue(50.0));
    mobility.Install(uavNodes);
    
    // 配置WiFi
    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211b);
    
    YansWifiPhyHelper wifiPhy;
    YansWifiChannelHelper wifiChannel = YansWifiChannelHelper::Default();
    wifiPhy.SetChannel(wifiChannel.Create());
    
    WifiMacHelper wifiMac;
    wifiMac.SetType("ns3::AdhocWifiMac");
    
    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, uavNodes);
    
    // 安装协议栈
    InternetStackHelper internet;
    internet.Install(uavNodes);
    
    // 分配IP地址
    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interfaces = ipv4.Assign(devices);
    
    // 配置路由
    Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    // 执行资源分配
    std::map<uint32_t, uint32_t> channelAssignment;
    AllocateChannels(uavNodes, channelAssignment);
    
    // 输出信道分配结果
    std::cout << "信道分配结果:" << std::endl;
    std::map<uint32_t, uint32_t> channelCounts;
    for (const auto& pair : channelAssignment) {
        channelCounts[pair.second]++;
    }
    for (const auto& pair : channelCounts) {
        std::cout << "  信道 " << pair.first << ": " << pair.second << " 个节点" << std::endl;
    }
    std::cout << std::endl;
    
    // 安装UDP应用 - 简化的点对点流量
    uint16_t port = 9000;
    
    // 只创建几个流来测试
    for (uint32_t i = 0; i < std::min(g_numUAVs, (uint32_t)5); ++i) {
        uint32_t j = (i + 5) % g_numUAVs;  // 选择较远的节点
        
        Ptr<Node> srcNode = uavNodes.Get(i);
        Ptr<Node> dstNode = uavNodes.Get(j);
        
        Ptr<Ipv4> ipv4Dst = dstNode->GetObject<Ipv4>();
        if (!ipv4Dst || ipv4Dst->GetNInterfaces() < 2) continue;
        
        Ipv4Address dstAddr = ipv4Dst->GetAddress(1, 0).GetLocal();
        
        // UDP Echo Client
        UdpEchoClientHelper echoClient(dstAddr, port + i);
        echoClient.SetAttribute("MaxPackets", UintegerValue(1000));
        echoClient.SetAttribute("Interval", TimeValue(Seconds(0.1)));
        echoClient.SetAttribute("PacketSize", UintegerValue(1024));
        
        ApplicationContainer clientApp = echoClient.Install(srcNode);
        clientApp.Start(Seconds(2.0));
        clientApp.Stop(Seconds(g_duration - 2.0));
        
        // UDP Echo Server
        UdpEchoServerHelper echoServer(port + i);
        ApplicationContainer serverApp = echoServer.Install(dstNode);
        serverApp.Start(Seconds(1.0));
        serverApp.Stop(Seconds(g_duration));
    }
    
    // 安装FlowMonitor
    FlowMonitorHelper flowHelper;
    Ptr<FlowMonitor> flowMonitor = flowHelper.InstallAll();
    
    // 运行仿真
    std::cout << "开始仿真..." << std::endl;
    Simulator::Stop(Seconds(g_duration));
    Simulator::Run();
    
    // 收集统计数据
    flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(
        flowHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = flowMonitor->GetFlowStats();
    
    double totalPDR = 0.0;
    double totalDelay = 0.0;
    double totalThroughput = 0.0;
    uint32_t flowCount = 0;
    
    for (auto& pair : stats) {
        if (pair.second.txPackets > 0) {
            double pdr = static_cast<double>(pair.second.rxPackets) / pair.second.txPackets;
            totalPDR += pdr;
            flowCount++;
            
            if (pair.second.rxPackets > 0) {
                double delay = pair.second.delaySum.GetSeconds() / pair.second.rxPackets;
                totalDelay += delay;
            }
            
            double throughput = pair.second.rxBytes * 8.0 / g_duration;
            totalThroughput += throughput;
        }
    }
    
    // 计算平均值
    double avgPDR = (flowCount > 0) ? (totalPDR / flowCount * 100) : 0.0;
    double avgDelay = (flowCount > 0) ? (totalDelay / flowCount * 1000) : 0.0;
    double totalThroughputMbps = totalThroughput / 1e6;
    
    // 输出结果
    std::cout << "\n========================================" << std::endl;
    std::cout << "仿真完成！" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "策略: " << g_strategy << std::endl;
    std::cout << "平均PDR: " << avgPDR << "%" << std::endl;
    std::cout << "平均时延: " << avgDelay << " ms" << std::endl;
    std::cout << "总吞吐量: " << totalThroughputMbps << " Mbps" << std::endl;
    
    // QoS满足情况
    bool pdrMet = (avgPDR >= 85.0);
    bool delayMet = (avgDelay <= 100.0);
    
    std::cout << "\nQoS满足情况:" << std::endl;
    std::cout << "  PDR要求(≥85%): " << (pdrMet ? "✓ 满足" : "✗ 不满足") << std::endl;
    std::cout << "  时延要求(≤100ms): " << (delayMet ? "✓ 满足" : "✗ 不满足") << std::endl;
    std::cout << "========================================\n" << std::endl;
    
    // 保存结果到文件
    std::ofstream resultFile(g_outputDir + "/summary.txt");
    resultFile << "UAV资源分配仿真结果\n";
    resultFile << "==================\n\n";
    resultFile << "策略: " << g_strategy << "\n";
    resultFile << "节点数量: " << g_numUAVs << "\n";
    resultFile << "信道数量: " << g_numChannels << "\n";
    resultFile << "仿真时长: " << g_duration << " 秒\n\n";
    resultFile << "性能指标:\n";
    resultFile << "  平均PDR: " << avgPDR << "%\n";
    resultFile << "  平均时延: " << avgDelay << " ms\n";
    resultFile << "  总吞吐量: " << totalThroughputMbps << " Mbps\n\n";
    resultFile << "QoS满足情况:\n";
    resultFile << "  PDR: " << (pdrMet ? "满足" : "不满足") << "\n";
    resultFile << "  时延: " << (delayMet ? "满足" : "不满足") << "\n";
    resultFile.close();
    
    std::cout << "结果已保存到: " << g_outputDir << "/summary.txt\n" << std::endl;
    
    // 清理
    Simulator::Destroy();
    
    return 0;
}

