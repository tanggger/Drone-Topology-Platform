/*
 * simple-example.cc
 * 
 * 简单使用示例
 * 展示如何使用UAV仿真框架的基本功能
 */

#include "ns3/core-module.h"
#include "ns3/uav-sim-helper.h"

using namespace ns3;
using namespace ns3::uavsim;

NS_LOG_COMPONENT_DEFINE("UavSimSimpleExample");

int main(int argc, char *argv[])
{
    // 命令行参数
    std::string scenario = "reconnaissance";
    std::string difficulty = "easy";
    double duration = 100.0;
    uint32_t numNodes = 20;
    std::string trajectoryFile = "data_rtk/mobility_trace_cross.txt";
    std::string outputDir = "output/simple_example";
    
    CommandLine cmd;
    cmd.AddValue("scenario", "场景类型 (reconnaissance/formation/emergency/swarm)", scenario);
    cmd.AddValue("difficulty", "难度级别 (easy/moderate/hard)", difficulty);
    cmd.AddValue("duration", "仿真时长(秒)", duration);
    cmd.AddValue("nodes", "节点数量", numNodes);
    cmd.AddValue("trajectory", "轨迹文件路径", trajectoryFile);
    cmd.AddValue("output", "输出目录", outputDir);
    cmd.Parse(argc, argv);
    
    // 打印可用插件
    std::cout << "\n可用插件列表:" << std::endl;
    PluginRegistry::Instance().PrintPlugins();
    
    // 创建仿真助手
    UavSimHelper sim;
    
    // 基础配置
    sim.SetName("Simple Example")
       .SetDuration(duration)
       .SetNumNodes(numNodes)
       .SetOutputDir(outputDir);
    
    // 选择场景
    if (scenario == "reconnaissance") {
        sim.ReconnaissanceScenario();
    } else if (scenario == "formation") {
        sim.FormationScenario();
    } else if (scenario == "emergency") {
        sim.EmergencyScenario();
    } else if (scenario == "swarm") {
        sim.SwarmScenario();
    } else {
        // 自定义配置
        sim.UseMobility("rtk-mobility")
           .UseChannel("ideal-channel")
           .UseTraffic("distance-based-traffic");
    }
    
    // 选择难度
    if (difficulty == "easy") {
        sim.Easy();
    } else if (difficulty == "moderate") {
        sim.Moderate();
    } else if (difficulty == "hard") {
        sim.Hard();
    }
    
    // 设置轨迹文件
    sim.SetParam("mobility.trajectory_file", trajectoryFile);
    
    // 添加数据采集
    sim.ClearCollectors()
       .UseCollector("transmission-collector")
       .UseCollector("topology-collector")
       .UseCollector("position-collector")
       .UseCollector("flowmon-collector");
    
    // 运行仿真
    sim.Run();
    
    std::cout << "\n仿真完成！输出文件位于: " << outputDir << std::endl;
    
    return 0;
}

