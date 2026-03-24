/*
 * uav_resource_allocation.cc
 *
 * 第一轮拆分后保留主入口职责：
 * - 参数解析
 * - 场景/网络装配
 * - 调度与运行
 * - 最终统计与收尾
 */

#include "context.h"

NS_LOG_COMPONENT_DEFINE("UAVResourceAllocation");

std::vector<TrajectoryPoint> g_trajectoryData;
std::map<uint32_t, std::vector<TrajectoryPoint>> g_nodeTrajectories;
double g_trajectoryEndTime = 0.0;
DifficultyParams g_diffParams;
ScenarioEnvironmentConfig g_environmentConfig;
EnvironmentSummary g_environmentSummary;
std::vector<SceneOverlayRegion> g_forestRegions;
std::vector<SceneOverlayRegion> g_waterRegions;
std::vector<SceneOverlayRegion> g_openFieldRegions;
ObservationRuntimeState g_observationRuntime;
CooperativeRuntimeState g_cooperativeRuntime;
Ptr<UniformRandomVariable> g_randVar;
double g_pathLossExponent = 2.0;

ResourceAllocationConfig g_config;
ResourceAllocationState g_state;
TDMAManager g_tdma;
std::ofstream g_tdmaLog;

NodeContainer g_uavNodes;
NodeContainer g_interferenceNodes;
std::map<uint32_t, Ptr<Application>> g_applications;
Ptr<FlowMonitor> g_flowMonitor;
FlowMonitorHelper g_flowHelper;

std::ofstream g_resourceLog;
std::ofstream g_qosLog;
std::ofstream g_topologyLog;
std::ofstream g_topologyEvolutionLog;
std::ofstream g_topologyDetailedLog;
std::ofstream g_resourceDetailedLog;
std::ofstream g_posLog;
std::ofstream g_topoChangesLog;
std::ofstream g_transLog;
std::ofstream g_observedSignalEventsLog;
std::ofstream g_observedCommWindowsLog;
std::ofstream g_observedLinkEvidenceLog;
std::ofstream g_inferredTopologyEdgesLog;
std::ofstream g_inferredGraphNodesLog;
std::ofstream g_keyNodeCandidatesLog;
std::ofstream g_cooperativeFailureEventsLog;
std::ofstream g_cooperativeRecoveryActionsLog;
std::ofstream g_cooperativeRecoveryMetricsLog;
std::ofstream g_cooperativeDecisionTraceLog;

namespace
{

std::vector<uint32_t>
ParseNodeIdList(const std::string& raw)
{
    std::vector<uint32_t> ids;
    if (raw.empty())
    {
        return ids;
    }

    std::stringstream ss(raw);
    std::string token;
    while (std::getline(ss, token, ','))
    {
        if (token.empty())
        {
            continue;
        }
        ids.push_back(static_cast<uint32_t>(std::stoul(token)));
    }
    return ids;
}

} // namespace

int main(int argc, char* argv[])
{
    OperationMode operationMode = OperationMode::Cooperative;
    std::string operationModeArg = "cooperative";
    CommunicationMode communicationMode = CommunicationMode::Centralized;
    std::string communicationModeArg = "centralized";
    CooperativeFailureType cooperativeFailureType = CooperativeFailureType::NodeFailure;
    std::string cooperativeFailureTypeArg = "node_failure";
    RecoveryPolicy recoveryPolicy = RecoveryPolicy::GlobalRecovery;
    std::string recoveryPolicyArg = "global_recovery";
    RecoveryObjective recoveryObjective = RecoveryObjective::Connectivity;
    std::string recoveryObjectiveArg = "connectivity";
    // 编队模式参数 (空=随机游走, v_formation/cross/line/triangle=编队轨迹)
    std::string formation = "";
    // 难度参数: Easy / Moderate / Hard  (对应 benchmark 三级配置)
    std::string difficulty = "Easy";
    // 城市建筑物地图文件
    std::string mapFile = "";
    // 场景类型
    std::string sceneType = "";
    uint32_t leaderNodeId = 0;
    std::string backupLeaderListArg;
    uint32_t distributedHopLimit = 1;
    int32_t failureTargetId = -1;
    double failureStartTime = -1.0;
    double failureDuration = -1.0;
    double recoveryCooldown = 1.0;
    bool allowChannelReallocation = true;
    bool allowPowerAdjustment = true;
    bool allowRateAdjustment = true;
    bool allowRelayReselection = true;
    bool allowSlotReallocation = true;
    bool allowRouteRebuild = true;
    
    // 解析命令行参数
    CommandLine cmd;
    cmd.AddValue("duration",   "仿真时长(秒)",              g_config.duration);
    cmd.AddValue("numUAVs",   "UAV节点数量",               g_config.numUAVs);
    cmd.AddValue("numChannels","可用信道数量",              g_config.numChannels);
    cmd.AddValue("strategy",  "资源分配策略",               g_config.allocationStrategy);
    cmd.AddValue("targetPDR", "目标分组投递率",             g_config.targetPDR);
    cmd.AddValue("maxDelay",  "最大端到端时延(秒)",         g_config.maxEndToEndDelay);
    cmd.AddValue("outputDir", "输出目录",                   g_config.outputDir);
    cmd.AddValue("operationMode",
                 "任务模式 (cooperative/non_cooperative)",
                 operationModeArg);
    cmd.AddValue("communicationMode",
                 "合作通信模式 (centralized/distributed/hybrid)",
                 communicationModeArg);
    cmd.AddValue("leaderNodeId", "合作模式 Leader 节点 ID", leaderNodeId);
    cmd.AddValue("backupLeaderList",
                 "合作模式备份 Leader 列表，逗号分隔",
                 backupLeaderListArg);
    cmd.AddValue("distributedHopLimit",
                 "distributed 模式局部视图 hop 数",
                 distributedHopLimit);
    cmd.AddValue("cooperativeFailureType",
                 "合作模式故障类型",
                 cooperativeFailureTypeArg);
    cmd.AddValue("failureTargetId", "合作模式故障目标节点 ID", failureTargetId);
    cmd.AddValue("failureStartTime", "合作模式故障开始时间", failureStartTime);
    cmd.AddValue("failureDuration", "合作模式故障持续时间", failureDuration);
    cmd.AddValue("recoveryPolicy", "合作模式恢复策略", recoveryPolicyArg);
    cmd.AddValue("recoveryObjective", "合作模式恢复目标", recoveryObjectiveArg);
    cmd.AddValue("recoveryCooldown", "合作模式恢复冷却时间", recoveryCooldown);
    cmd.AddValue("allowChannelReallocation", "允许信道重分配", allowChannelReallocation);
    cmd.AddValue("allowPowerAdjustment", "允许功率调整", allowPowerAdjustment);
    cmd.AddValue("allowRateAdjustment", "允许速率调整", allowRateAdjustment);
    cmd.AddValue("allowRelayReselection", "允许邻居/中继切换", allowRelayReselection);
    cmd.AddValue("allowSlotReallocation", "允许 TDMA 时隙重分配", allowSlotReallocation);
    cmd.AddValue("allowRouteRebuild", "允许路由重构", allowRouteRebuild);
    cmd.AddValue("formation", "编队模式 (v_formation/cross/line/triangle，空=随机游走)", formation);
    cmd.AddValue("sceneType", "场景类型 (urban/forest/lake/open-field，空=自动推断)", sceneType);
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

    if (!TryParseOperationMode(operationModeArg, operationMode))
    {
        std::cerr << "无效 operationMode: " << operationModeArg
                  << "，仅支持 cooperative / non_cooperative" << std::endl;
        return 1;
    }
    if (!TryParseCommunicationMode(communicationModeArg, communicationMode))
    {
        std::cerr << "无效 communicationMode: " << communicationModeArg
                  << "，仅支持 centralized / distributed / hybrid" << std::endl;
        return 1;
    }
    if (!TryParseCooperativeFailureType(cooperativeFailureTypeArg, cooperativeFailureType))
    {
        std::cerr << "无效 cooperativeFailureType: " << cooperativeFailureTypeArg
                  << "，仅支持 node_failure / environment_degradation / external_interference / link_degradation"
                  << std::endl;
        return 1;
    }
    if (!TryParseRecoveryPolicy(recoveryPolicyArg, recoveryPolicy))
    {
        std::cerr << "无效 recoveryPolicy: " << recoveryPolicyArg
                  << "，仅支持 global_recovery / local_recovery" << std::endl;
        return 1;
    }
    if (!TryParseRecoveryObjective(recoveryObjectiveArg, recoveryObjective))
    {
        std::cerr << "无效 recoveryObjective: " << recoveryObjectiveArg
                  << "，仅支持 connectivity / delay / throughput / pdr" << std::endl;
        return 1;
    }

    CooperativeControlConfig cooperativeConfig;
    cooperativeConfig.communicationMode = communicationMode;
    cooperativeConfig.leaderNodeId = leaderNodeId;
    cooperativeConfig.backupLeaderList = ParseNodeIdList(backupLeaderListArg);
    cooperativeConfig.distributedHopLimit = std::max<uint32_t>(1, std::min<uint32_t>(2, distributedHopLimit));
    cooperativeConfig.failureType = cooperativeFailureType;
    cooperativeConfig.failureTargetId = failureTargetId;
    cooperativeConfig.failureStartTime = failureStartTime;
    cooperativeConfig.failureDuration = failureDuration;
    cooperativeConfig.recoveryPolicy = recoveryPolicy;
    cooperativeConfig.recoveryObjective = recoveryObjective;
    cooperativeConfig.recoveryCooldown = recoveryCooldown;
    cooperativeConfig.allowChannelReallocation = allowChannelReallocation;
    cooperativeConfig.allowPowerAdjustment = allowPowerAdjustment;
    cooperativeConfig.allowRateAdjustment = allowRateAdjustment;
    cooperativeConfig.allowRelayReselection = allowRelayReselection;
    cooperativeConfig.allowSlotReallocation = allowSlotReallocation;
    cooperativeConfig.allowRouteRebuild = allowRouteRebuild;
    
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
    std::cout << "任务模式: " << OperationModeToString(operationMode) << std::endl;
    if (operationMode == OperationMode::Cooperative)
    {
        std::cout << "合作通信模式: " << CommunicationModeToString(cooperativeConfig.communicationMode)
                  << std::endl;
        std::cout << "Leader 节点: " << cooperativeConfig.leaderNodeId << std::endl;
        std::cout << "恢复策略: " << RecoveryPolicyToString(cooperativeConfig.recoveryPolicy)
                  << std::endl;
        std::cout << "恢复目标: " << RecoveryObjectiveToString(cooperativeConfig.recoveryObjective)
                  << std::endl;
    }
    std::cout << "难度等级: " << difficulty << std::endl;
    std::cout << "目标PDR: " << g_config.targetPDR * 100 << "%" << std::endl;
    std::cout << "最大时延: " << g_config.maxEndToEndDelay * 1000 << " ms" << std::endl;
    std::cout << "分配策略: " << g_config.allocationStrategy << std::endl;
    std::cout << "速率范围: [" << g_config.dataRateMin << ", " << g_config.dataRateMax << "] Mbps" << std::endl;
    std::cout << "MAC调度: " << (g_config.enableTDMA ? "软TDMA (空间复用)" : "CSMA/CA (标准竞争)") << std::endl; 
    std::cout << "========================================" << std::endl;
    
    SetupSimulationInfrastructure(useFormation,
                                  operationMode,
                                  sceneType,
                                  difficulty,
                                  useFormation ? formation : "random_walk",
                                  mapFile,
                                  cooperativeConfig,
                                  customPathLossExp,
                                  customRxSensitivity,
                                  customTxPower);
    InitializeOutputFiles();
    WriteEnvironmentSummaryFile();
    std::cout << "操作模式: " << g_environmentSummary.operationMode << std::endl;
    std::cout << "场景类型: " << g_environmentSummary.sceneType << std::endl;
    std::cout << "环境模型: " << g_environmentSummary.effectiveModelSummary << std::endl;
    std::cout << "环境来源: " << g_environmentSummary.environmentSource << std::endl;
    if (operationMode == OperationMode::Cooperative)
    {
        std::cout << "合作摘要: mode=" << g_environmentSummary.communicationMode
                  << ", leader=" << g_environmentSummary.leaderNodeId
                  << ", failure=" << g_environmentSummary.cooperativeFailureType
                  << ", recovery=" << g_environmentSummary.recoveryPolicy
                  << std::endl;
    }
    
    // 调度资源分配和监控 (Align start time with QoS monitoring)
    Simulator::Schedule(Seconds(0.1), &PerformResourceReallocation);
    // 初始启动 QoS 监控 (需与 LogPositions 同步)
    Simulator::Schedule(Seconds(0.1), &MonitorQoSPerformance);
    Simulator::Schedule(Seconds(0.1), &LogTopologyChange);
    if (operationMode == OperationMode::NonCooperative)
    {
        Simulator::Schedule(Seconds(0.1), &MonitorObservedSignalEvents);
        Simulator::Schedule(Seconds(g_environmentConfig.observationPreset.windowDurationSec),
                            &UpdateObservedTrackStates);
    }
    Simulator::Schedule(Seconds(0.1), &LogPositions); // 启动位置记录
    
    // 设置包收发记录 (性能瓶颈: 每一包都写磁盘，严重拖慢仿真)
    // 如需调试丢包细节，请取消注释
    // Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Tx", MakeCallback(&Ipv4RxTxTracer));
    // Config::Connect("/NodeList/*/$ns3::Ipv4L3Protocol/Rx", MakeCallback(&Ipv4RxTxTracer));
    
    // 运行仿真
    std::cout << "\n开始仿真..." << std::endl;
    Simulator::Stop(Seconds(g_config.duration));
    Simulator::Run();
    
    FinalizeSimulationOutputs();
    
    return 0;
}
