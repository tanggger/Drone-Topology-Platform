#include "context.h"

NS_LOG_COMPONENT_DEFINE("UAVResourceAllocationOutput");

namespace
{

std::string BoolToJson(bool value)
{
    return value ? "true" : "false";
}

bool IsNonCooperativeMode()
{
    return g_environmentConfig.operationMode == OperationMode::NonCooperative;
}

bool IsCooperativeMode()
{
    return g_environmentConfig.operationMode == OperationMode::Cooperative;
}

std::string JsonEscape(const std::string& value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char c : value)
    {
        switch (c)
        {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        default:
            escaped += c;
            break;
        }
    }
    return escaped;
}

void WriteCooperativeModeSummaryJson()
{
    if (!IsCooperativeMode())
    {
        return;
    }

    std::ofstream out(g_config.outputDir + "/cooperative_mode_summary.json");
    out << "{\n";
    out << "  \"operationMode\": \"" << g_environmentSummary.operationMode << "\",\n";
    out << "  \"communicationMode\": \"" << g_environmentSummary.communicationMode << "\",\n";
    out << "  \"recoveryPolicy\": \"" << g_environmentSummary.recoveryPolicy << "\",\n";
    out << "  \"recoveryObjective\": \"" << g_environmentSummary.recoveryObjective << "\",\n";
    out << "  \"sceneType\": \"" << g_environmentSummary.sceneType << "\",\n";
    out << "  \"difficulty\": \"" << g_environmentSummary.difficulty << "\",\n";
    out << "  \"formation\": \"" << JsonEscape(g_environmentSummary.formationName) << "\",\n";
    out << "  \"leaderNodeId\": " << g_environmentSummary.leaderNodeId << ",\n";
    out << "  \"backupLeaderList\": \"" << JsonEscape(g_environmentSummary.backupLeaderList)
        << "\",\n";
    out << "  \"distributedHopLimit\": " << g_environmentSummary.distributedHopLimit << ",\n";
    out << "  \"failureType\": \"" << g_environmentSummary.cooperativeFailureType << "\",\n";
    out << "  \"failureTargetId\": " << g_environmentSummary.failureTargetId << ",\n";
    out << "  \"failureStartTime\": " << g_environmentSummary.failureStartTime << ",\n";
    out << "  \"failureDuration\": " << g_environmentSummary.failureDuration << ",\n";
    out << "  \"recoveryCooldown\": " << g_environmentSummary.recoveryCooldown << ",\n";
    out << "  \"actionFlags\": {\n";
    out << "    \"allowChannelReallocation\": "
        << BoolToJson(g_environmentSummary.allowChannelReallocation) << ",\n";
    out << "    \"allowPowerAdjustment\": "
        << BoolToJson(g_environmentSummary.allowPowerAdjustment) << ",\n";
    out << "    \"allowRateAdjustment\": "
        << BoolToJson(g_environmentSummary.allowRateAdjustment) << ",\n";
    out << "    \"allowRelayReselection\": "
        << BoolToJson(g_environmentSummary.allowRelayReselection) << ",\n";
    out << "    \"allowSlotReallocation\": "
        << BoolToJson(g_environmentSummary.allowSlotReallocation) << ",\n";
    out << "    \"allowRouteRebuild\": "
        << BoolToJson(g_environmentSummary.allowRouteRebuild) << "\n";
    out << "  }\n";
    out << "}\n";
}

void WriteCooperativeFailureTimelineJson()
{
    if (!IsCooperativeMode())
    {
        return;
    }

    std::ofstream out(g_config.outputDir + "/cooperative_failure_timeline.json");
    out << "{\n  \"events\": [\n";
    for (size_t i = 0; i < g_cooperativeRuntime.failureEvents.size(); ++i)
    {
        const auto& event = g_cooperativeRuntime.failureEvents[i];
        out << "    {\n";
        out << "      \"eventId\": " << i << ",\n";
        out << "      \"time\": " << event.time << ",\n";
        out << "      \"failureType\": \"" << JsonEscape(event.failureType) << "\",\n";
        out << "      \"targetNodeId\": " << event.targetNodeId << ",\n";
        out << "      \"targetRole\": \"" << JsonEscape(event.targetRole) << "\",\n";
        out << "      \"isLeaderTarget\": " << BoolToJson(event.isLeaderTarget) << ",\n";
        out << "      \"failureState\": \"" << JsonEscape(event.failureState) << "\",\n";
        out << "      \"affectedNeighborCount\": " << event.affectedNeighborCount << ",\n";
        out << "      \"affectedLinkCount\": " << event.affectedLinkCount << ",\n";
        out << "      \"effectSummary\": \"" << JsonEscape(event.effectSummary) << "\",\n";
        out << "      \"source\": \"" << JsonEscape(event.source) << "\"\n";
        out << "    }";
        if (i + 1 < g_cooperativeRuntime.failureEvents.size())
        {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n}\n";
}

void WriteCooperativeRecoveryTimelineJson()
{
    if (!IsCooperativeMode())
    {
        return;
    }

    std::ofstream out(g_config.outputDir + "/cooperative_recovery_timeline.json");
    out << "{\n  \"actions\": [\n";
    for (size_t i = 0; i < g_cooperativeRuntime.recoveryActions.size(); ++i)
    {
        const auto& action = g_cooperativeRuntime.recoveryActions[i];
        out << "    {\n";
        out << "      \"actionId\": " << i << ",\n";
        out << "      \"time\": " << action.time << ",\n";
        out << "      \"phase\": \"" << JsonEscape(action.phase) << "\",\n";
        out << "      \"communicationMode\": \"" << JsonEscape(action.communicationMode)
            << "\",\n";
        out << "      \"recoveryPolicy\": \"" << JsonEscape(action.recoveryPolicy) << "\",\n";
        out << "      \"effectiveRecoveryPolicy\": \""
            << JsonEscape(action.effectiveRecoveryPolicy) << "\",\n";
        out << "      \"triggerReason\": \"" << JsonEscape(action.decisionReason) << "\",\n";
        out << "      \"executorNodeId\": " << action.executorNodeId << ",\n";
        out << "      \"targetNodeIds\": \"" << JsonEscape(action.targetNodeIds) << "\",\n";
        out << "      \"actionType\": \"" << JsonEscape(action.actionType) << "\",\n";
        out << "      \"oldValue\": \"" << JsonEscape(action.oldValue) << "\",\n";
        out << "      \"newValue\": \"" << JsonEscape(action.newValue) << "\",\n";
        out << "      \"scope\": \"" << JsonEscape(action.scope) << "\",\n";
        out << "      \"expectedEffect\": \"" << JsonEscape(action.expectedEffect) << "\",\n";
        out << "      \"resultState\": \"" << JsonEscape(action.resultState) << "\"\n";
        out << "    }";
        if (i + 1 < g_cooperativeRuntime.recoveryActions.size())
        {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n}\n";
}

void WriteCooperativeMetricsTimeseriesJson()
{
    if (!IsCooperativeMode())
    {
        return;
    }

    std::ofstream out(g_config.outputDir + "/cooperative_metrics_timeseries.json");
    out << "{\n  \"samples\": [\n";
    for (size_t i = 0; i < g_cooperativeRuntime.metricsHistory.size(); ++i)
    {
        const auto& sample = g_cooperativeRuntime.metricsHistory[i];
        out << "    {\n";
        out << "      \"time\": " << sample.time << ",\n";
        out << "      \"phase\": \"" << JsonEscape(sample.phase) << "\",\n";
        out << "      \"connectivity\": " << sample.connectivity << ",\n";
        out << "      \"avgDegree\": " << sample.avgDegree << ",\n";
        out << "      \"pdr\": " << sample.pdr << ",\n";
        out << "      \"throughputMbps\": " << sample.throughputMbps << ",\n";
        out << "      \"delayMs\": " << sample.delayMs << ",\n";
        out << "      \"p99DelayMs\": " << sample.p99DelayMs << ",\n";
        if (std::isnan(sample.failureNeighborhoodPdr))
        {
            out << "      \"failureNeighborhoodPdr\": null,\n";
        }
        else
        {
            out << "      \"failureNeighborhoodPdr\": "
                << sample.failureNeighborhoodPdr << ",\n";
        }
        if (std::isnan(sample.failureNeighborhoodThroughputMbps))
        {
            out << "      \"failureNeighborhoodThroughputMbps\": null,\n";
        }
        else
        {
            out << "      \"failureNeighborhoodThroughputMbps\": "
                << sample.failureNeighborhoodThroughputMbps << ",\n";
        }
        if (std::isnan(sample.failureNeighborhoodDelayMs))
        {
            out << "      \"failureNeighborhoodDelayMs\": null,\n";
        }
        else
        {
            out << "      \"failureNeighborhoodDelayMs\": "
                << sample.failureNeighborhoodDelayMs << ",\n";
        }
        out << "      \"failureNeighborhoodNodeCount\": "
            << sample.failureNeighborhoodNodeCount << ",\n";
        out << "      \"failureTargetId\": " << sample.failureTargetId << ",\n";
        out << "      \"isFailureTargetFailed\": "
            << BoolToJson(sample.isFailureTargetFailed) << ",\n";
        if (std::isnan(sample.failureTargetPdr))
        {
            out << "      \"failureTargetPdr\": null,\n";
        }
        else
        {
            out << "      \"failureTargetPdr\": " << sample.failureTargetPdr << ",\n";
        }
        if (std::isnan(sample.failureTargetThroughputMbps))
        {
            out << "      \"failureTargetThroughputMbps\": null,\n";
        }
        else
        {
            out << "      \"failureTargetThroughputMbps\": "
                << sample.failureTargetThroughputMbps << ",\n";
        }
        if (std::isnan(sample.failureTargetDelayMs))
        {
            out << "      \"failureTargetDelayMs\": null,\n";
        }
        else
        {
            out << "      \"failureTargetDelayMs\": " << sample.failureTargetDelayMs << ",\n";
        }
        out << "      \"activeNodeCount\": " << sample.activeNodeCount << ",\n";
        out << "      \"leaderNodeId\": " << sample.leaderNodeId << ",\n";
        out << "      \"isLeaderAlive\": " << BoolToJson(sample.isLeaderAlive) << ",\n";
        if (std::isnan(sample.responseTimeSec))
        {
            out << "      \"responseTimeSec\": null,\n";
        }
        else
        {
            out << "      \"responseTimeSec\": " << sample.responseTimeSec << ",\n";
        }
        if (std::isnan(sample.recoveryTimeSec))
        {
            out << "      \"recoveryTimeSec\": null,\n";
        }
        else
        {
            out << "      \"recoveryTimeSec\": " << sample.recoveryTimeSec << ",\n";
        }
        if (std::isnan(sample.stabilizationTimeSec))
        {
            out << "      \"stabilizationTimeSec\": null\n";
        }
        else
        {
            out << "      \"stabilizationTimeSec\": " << sample.stabilizationTimeSec << "\n";
        }
        out << "    }";
        if (i + 1 < g_cooperativeRuntime.metricsHistory.size())
        {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n}\n";
}

void WriteCooperativeDashboardSnapshotJson(double pdr = 0.0,
                                           double throughputMbps = 0.0,
                                           double delayMs = 0.0)
{
    if (!IsCooperativeMode())
    {
        return;
    }

    std::ofstream out(g_config.outputDir + "/cooperative_dashboard_snapshot.json");
    double connectivity = 0.0;
    double avgDegree = 0.0;
    double p99DelayMs = 0.0;
    if (!g_cooperativeRuntime.metricsHistory.empty())
    {
        const auto& last = g_cooperativeRuntime.metricsHistory.back();
        connectivity = last.connectivity;
        avgDegree = last.avgDegree;
        p99DelayMs = last.p99DelayMs;
    }
    std::string finalPhase = "normal";
    if (g_cooperativeRuntime.leaderTransitionActive)
    {
        finalPhase = "transition";
    }
    else if (g_cooperativeRuntime.stabilizationActive ||
             g_cooperativeRuntime.stabilizationCompletedAt >= 0.0)
    {
        finalPhase = "stable";
    }
    else if (g_cooperativeRuntime.recoveryActive ||
             g_cooperativeRuntime.recoveryCompletedAt >= 0.0)
    {
        finalPhase = "recovery";
    }
    else if (g_cooperativeRuntime.failureActive)
    {
        finalPhase = "failure";
    }

    std::string recoveryStatus = "not_triggered";
    if (g_cooperativeRuntime.stabilizationCompletedAt >= 0.0)
    {
        recoveryStatus = "stable";
    }
    else if (g_cooperativeRuntime.recoveryCompletedAt >= 0.0)
    {
        recoveryStatus = "completed";
    }
    else if (!g_cooperativeRuntime.recoveryActions.empty() ||
             !std::isnan(g_cooperativeRuntime.responseTimeSec))
    {
        recoveryStatus = "active";
    }

    out << "{\n";
    out << "  \"time\": " << g_config.duration << ",\n";
    out << "  \"phase\": \"" << finalPhase << "\",\n";
    out << "  \"operationMode\": \"" << g_environmentSummary.operationMode << "\",\n";
    out << "  \"communicationMode\": \"" << g_environmentSummary.communicationMode << "\",\n";
    out << "  \"leaderNodeId\": " << g_cooperativeRuntime.activeLeaderNodeId << ",\n";
    out << "  \"backupLeaderId\": " << g_cooperativeRuntime.activeBackupLeaderNodeId << ",\n";
    out << "  \"isLeaderAlive\": " << BoolToJson(g_cooperativeRuntime.leaderAlive) << ",\n";
    out << "  \"failureActive\": " << BoolToJson(g_cooperativeRuntime.failureActive) << ",\n";
    out << "  \"failureType\": \"" << g_environmentSummary.cooperativeFailureType << "\",\n";
    out << "  \"failureTargetId\": " << g_environmentSummary.failureTargetId << ",\n";
    out << "  \"connectivity\": " << connectivity << ",\n";
    out << "  \"avgDegree\": " << avgDegree << ",\n";
    out << "  \"pdr\": " << pdr << ",\n";
    out << "  \"throughputMbps\": " << throughputMbps << ",\n";
    out << "  \"delayMs\": " << delayMs << ",\n";
    out << "  \"p99DelayMs\": " << p99DelayMs << ",\n";
    if (std::isnan(g_cooperativeRuntime.responseTimeSec))
    {
        out << "  \"responseTimeSec\": null,\n";
    }
    else
    {
        out << "  \"responseTimeSec\": " << g_cooperativeRuntime.responseTimeSec << ",\n";
    }
    if (std::isnan(g_cooperativeRuntime.recoveryTimeSec))
    {
        out << "  \"recoveryTimeSec\": null,\n";
    }
    else
    {
        out << "  \"recoveryTimeSec\": " << g_cooperativeRuntime.recoveryTimeSec << ",\n";
    }
    if (std::isnan(g_cooperativeRuntime.stabilizationTimeSec))
    {
        out << "  \"stabilizationTimeSec\": null,\n";
    }
    else
    {
        out << "  \"stabilizationTimeSec\": " << g_cooperativeRuntime.stabilizationTimeSec
            << ",\n";
    }
    out << "  \"latestRecoveryAction\": \""
        << JsonEscape(g_cooperativeRuntime.recoveryActions.empty()
                          ? ""
                          : g_cooperativeRuntime.recoveryActions.back().actionType)
        << "\",\n";
    out << "  \"recoveryStatus\": \"" << recoveryStatus << "\"\n";
    out << "}\n";
}

void WriteCooperativeJsonSkeletons()
{
    if (!IsCooperativeMode())
    {
        return;
    }
    WriteCooperativeModeSummaryJson();
    WriteCooperativeFailureTimelineJson();
    WriteCooperativeRecoveryTimelineJson();
    WriteCooperativeMetricsTimeseriesJson();
    WriteCooperativeDashboardSnapshotJson();
}

} // namespace

void InitializeOutputFiles()
{
    std::string cmdMkdir = "mkdir -p " + g_config.outputDir;
    int ret = system(cmdMkdir.c_str());
    (void)ret;

    g_resourceLog.open(g_config.outputDir + "/resource_allocation.csv");
    g_resourceDetailedLog.open(g_config.outputDir + "/resource_allocation_detailed.csv");
    g_qosLog.open(g_config.outputDir + "/qos_performance.csv");
    g_topologyLog.open(g_config.outputDir + "/topology_changes.csv");
    g_topologyEvolutionLog.open(g_config.outputDir + "/topology_evolution.csv");
    g_topologyDetailedLog.open(g_config.outputDir + "/topology_detailed.csv");
    g_posLog.open(g_config.outputDir + "/rtk-node-positions.csv");
    g_topoChangesLog.open(g_config.outputDir + "/rtk-topology-changes.txt");
    g_transLog.open(g_config.outputDir + "/rtk-node-transmissions.csv");
    g_tdmaLog.open(g_config.outputDir + "/tdma_schedule.csv");

    if (IsNonCooperativeMode())
    {
        g_observedSignalEventsLog.open(g_config.outputDir + "/observed_signal_events.csv");
        g_observedCommWindowsLog.open(g_config.outputDir + "/observed_comm_windows.csv");
        g_observedLinkEvidenceLog.open(g_config.outputDir + "/observed_link_evidence.csv");
        g_inferredTopologyEdgesLog.open(g_config.outputDir + "/inferred_topology_edges.csv");
        g_inferredGraphNodesLog.open(g_config.outputDir + "/inferred_graph_nodes.csv");
        g_keyNodeCandidatesLog.open(g_config.outputDir + "/key_node_candidates.csv");
    }
    if (IsCooperativeMode())
    {
        g_cooperativeFailureEventsLog.open(g_config.outputDir + "/cooperative_failure_events.csv");
        g_cooperativeRecoveryActionsLog.open(g_config.outputDir + "/cooperative_recovery_actions.csv");
        g_cooperativeRecoveryMetricsLog.open(g_config.outputDir + "/cooperative_recovery_metrics.csv");
        g_cooperativeDecisionTraceLog.open(g_config.outputDir + "/cooperative_decision_trace.csv");
    }

    g_tdmaLog << "time,node_id,slot_id,num_groups,packets_per_slot,bonus_slots,urgency\n";
    g_posLog << "time,nodeId,x,y,z,node_type,speed\n";
    g_transLog << "time,nodeId,eventType\n";
    g_resourceDetailedLog
        << "time,node_id,channel,tx_power,data_rate,neighbors,interference_dBm,worst_sinr_dB\n";
    g_topologyDetailedLog << "time,num_nodes,num_links,avg_degree,network_density\n";
    g_topologyEvolutionLog << "time,num_links,connectivity\n";
    g_topologyLog << "time,num_links,connectivity\n";

    if (g_observedSignalEventsLog.is_open())
    {
        g_observedSignalEventsLog
            << "eventTime,observedNodeId,txStartTime,txEndTime,txDuration,avgRxPowerDbm,"
               "channelId,centerFrequencyHz,signalDetected,coarsePosX,coarsePosY,"
               "positionConfidence,signalConfidence,overallConfidence,isMissing,"
               "missingReason,noiseLevel,observerId,sceneType,operationMode\n";
    }

    if (g_observedCommWindowsLog.is_open())
    {
        g_observedCommWindowsLog
            << "windowStart,windowEnd,observedNodeId,txStartTime,txEndTime,txDuration,"
               "avgRxPowerDbm,channelId,centerFrequencyHz,signalDetected,stateSequence,"
               "activeRatio,txCount,coarsePosX,coarsePosY,positionConfidence,"
               "signalConfidence,overallConfidence,isMissing,missingReason,noiseLevel,"
               "observerId,sceneType,operationMode\n";
    }

    if (g_observedLinkEvidenceLog.is_open())
    {
        g_observedLinkEvidenceLog
            << "windowStart,windowEnd,srcObservedNodeId,dstObservedNodeId,"
               "evidenceStrength,commCount,commDurationTotal,avgRxPowerDbm,channelId,"
               "centerFrequencyHz,observerCount,observerAgreementScore,"
               "edgeObservationConfidence,isMissing,missingReason,noiseLevel,sceneType,"
               "operationMode\n";
    }

    if (g_inferredTopologyEdgesLog.is_open())
    {
        g_inferredTopologyEdgesLog
            << "windowStart,windowEnd,srcObservedNodeId,dstObservedNodeId,edgeProbability,"
               "edgeConfidence,inferenceMethod,sceneType,operationMode\n";
    }

    if (g_inferredGraphNodesLog.is_open())
    {
        g_inferredGraphNodesLog
            << "windowStart,windowEnd,observedNodeId,incidentEdgeCount,weightedDegreeScore,"
               "avgIncidentProbability,avgIncidentConfidence,sceneType,operationMode\n";
    }

    if (g_keyNodeCandidatesLog.is_open())
    {
        g_keyNodeCandidatesLog
            << "windowStart,windowEnd,observedNodeId,rank,weightedDegreeScore,"
               "avgIncidentProbability,avgIncidentConfidence,keyNodeScore,keyNodeMethod,"
               "sceneType,operationMode\n";
    }
    if (g_cooperativeFailureEventsLog.is_open())
    {
        g_cooperativeFailureEventsLog
            << "time,failure_type,target_node_id,is_leader_target,failure_state,"
               "communication_mode,recovery_policy,scene_type,operation_mode\n";
    }
    if (g_cooperativeRecoveryActionsLog.is_open())
    {
        g_cooperativeRecoveryActionsLog
            << "time,communication_mode,recovery_policy,effective_recovery_policy,action_type,"
               "executor_node_id,target_node_ids,result_state,decision_reason,scene_type,"
               "operation_mode\n";
    }
    if (g_cooperativeRecoveryMetricsLog.is_open())
    {
        g_cooperativeRecoveryMetricsLog
            << "time,phase,connectivity,avg_degree,pdr,throughput_mbps,delay_ms,"
               "p99_delay_ms,response_time_sec,recovery_time_sec,stabilization_time_sec,"
               "failure_neighborhood_pdr,failure_neighborhood_throughput_mbps,"
               "failure_neighborhood_delay_ms,failure_neighborhood_node_count,"
               "failure_target_id,is_failure_target_failed,failure_target_pdr,"
               "failure_target_throughput_mbps,failure_target_delay_ms,"
               "leader_node_id,is_leader_alive,scene_type,operation_mode\n";
    }
    if (g_cooperativeDecisionTraceLog.is_open())
    {
        g_cooperativeDecisionTraceLog
            << "time,communication_mode,recovery_policy,leader_node_id,failure_active,"
               "recovery_active,stabilization_active,active_node_count,"
               "effective_recovery_policy,decision_reason,scene_type,operation_mode\n";
    }

    g_resourceLog << "time";
    for (uint32_t i = 0; i < g_config.numUAVs; ++i)
    {
        g_resourceLog << ",uav" << i << "_channel"
                      << ",uav" << i << "_power"
                      << ",uav" << i << "_rate";
    }
    g_resourceLog << std::endl;

    g_qosLog << "time";
    for (uint32_t i = 0; i < g_config.numUAVs; ++i)
    {
        g_qosLog << ",uav" << i << "_pdr"
                 << ",uav" << i << "_delay"
                 << ",uav" << i << "_throughput";
    }
    g_qosLog << std::endl;

    WriteCooperativeJsonSkeletons();
}

void WriteEnvironmentSummaryFile()
{
    std::ofstream summary(g_config.outputDir + "/environment_summary.json");
    summary << "{\n";
    summary << "  \"operationMode\": \"" << g_environmentSummary.operationMode << "\",\n";
    summary << "  \"sceneType\": \"" << g_environmentSummary.sceneType << "\",\n";
    summary << "  \"difficulty\": \"" << g_environmentSummary.difficulty << "\",\n";
    summary << "  \"formation\": \"" << JsonEscape(g_environmentSummary.formationName)
            << "\",\n";
    summary << "  \"baseModel\": \"" << g_environmentSummary.baseModel << "\",\n";
    summary << "  \"environmentSource\": \"" << g_environmentSummary.environmentSource << "\",\n";
    summary << "  \"geometryInputMode\": \"" << g_environmentSummary.geometryInputMode << "\",\n";
    summary << "  \"effectiveModelSummary\": \"" << g_environmentSummary.effectiveModelSummary
            << "\",\n";
    summary << "  \"environmentContributionSummary\": \""
            << g_environmentSummary.environmentContributionSummary << "\",\n";
    summary << "  \"hasBuildings\": " << BoolToJson(g_environmentSummary.hasBuildings) << ",\n";
    summary << "  \"hasVegetation\": " << BoolToJson(g_environmentSummary.hasVegetation) << ",\n";
    summary << "  \"hasWaterSurface\": " << BoolToJson(g_environmentSummary.hasWaterSurface)
            << ",\n";
    summary << "  \"reflectionAware\": " << BoolToJson(g_environmentSummary.reflectionAware)
            << ",\n";
    summary << "  \"shadowSigmaDb\": " << g_environmentSummary.shadowSigmaDb << ",\n";
    summary << "  \"nlosPenaltyDb\": " << g_environmentSummary.nlosPenaltyDb << ",\n";
    summary << "  \"vegetationLossDbPerM\": " << g_environmentSummary.vegetationLossDbPerM
            << ",\n";
    summary << "  \"interferenceFactor\": " << g_environmentSummary.interferenceFactor << ",\n";
    summary << "  \"connectivityRangeFactor\": "
            << g_environmentSummary.connectivityRangeFactor << ",\n";
    summary << "  \"pathLossExponent\": " << g_environmentSummary.pathLossExponent << ",\n";
    summary << "  \"rxSensitivity\": " << g_environmentSummary.rxSensitivity << ",\n";
    summary << "  \"txPower\": " << g_environmentSummary.txPower << ",\n";
    summary << "  \"noiseFigure\": " << g_environmentSummary.noiseFigure << ",\n";
    summary << "  \"trafficLoadMbps\": " << g_environmentSummary.trafficLoadMbps << ",\n";
    summary << "  \"numInterferenceNodes\": " << g_environmentSummary.numInterferenceNodes
            << ",\n";
    summary << "  \"observationEnabled\": "
            << BoolToJson(g_environmentSummary.observationEnabled) << ",\n";
    summary << "  \"observationWindowDurationSec\": "
            << g_environmentSummary.observationWindowDurationSec << ",\n";
    summary << "  \"observationSubslotCount\": "
            << g_environmentSummary.observationSubslotCount << ",\n";
    summary << "  \"observationSubslotDurationSec\": "
            << g_environmentSummary.observationSubslotDurationSec << ",\n";
    summary << "  \"trackCreateWindowCount\": "
            << g_environmentSummary.trackCreateWindowCount << ",\n";
    summary << "  \"trackDeleteWindowCount\": "
            << g_environmentSummary.trackDeleteWindowCount << ",\n";
    summary << "  \"observationRangeM\": " << g_environmentSummary.observationRangeM
            << ",\n";
    summary << "  \"observationRandomDropRate\": "
            << g_environmentSummary.observationRandomDropRate << ",\n";
    summary << "  \"observationPositionNoiseStdDevM\": "
            << g_environmentSummary.observationPositionNoiseStdDevM << ",\n";
    summary << "  \"observationPowerNoiseStdDevDb\": "
            << g_environmentSummary.observationPowerNoiseStdDevDb << ",\n";
    summary << "  \"observationObserverCount\": "
            << g_environmentSummary.observationObserverCount << ",\n";
    summary << "  \"observationTargetObjectCount\": "
            << g_environmentSummary.observationTargetObjectCount << ",\n";
    summary << "  \"communicationMode\": \"" << g_environmentSummary.communicationMode
            << "\",\n";
    summary << "  \"leaderNodeId\": " << g_environmentSummary.leaderNodeId << ",\n";
    summary << "  \"backupLeaderList\": \"" << g_environmentSummary.backupLeaderList
            << "\",\n";
    summary << "  \"distributedHopLimit\": "
            << g_environmentSummary.distributedHopLimit << ",\n";
    summary << "  \"cooperativeFailureType\": \""
            << g_environmentSummary.cooperativeFailureType << "\",\n";
    summary << "  \"failureTargetId\": " << g_environmentSummary.failureTargetId << ",\n";
    summary << "  \"failureStartTime\": " << g_environmentSummary.failureStartTime << ",\n";
    summary << "  \"failureDuration\": " << g_environmentSummary.failureDuration << ",\n";
    summary << "  \"recoveryPolicy\": \"" << g_environmentSummary.recoveryPolicy << "\",\n";
    summary << "  \"recoveryObjective\": \"" << g_environmentSummary.recoveryObjective
            << "\",\n";
    summary << "  \"recoveryCooldown\": " << g_environmentSummary.recoveryCooldown << ",\n";
    summary << "  \"allowChannelReallocation\": "
            << BoolToJson(g_environmentSummary.allowChannelReallocation) << ",\n";
    summary << "  \"allowPowerAdjustment\": "
            << BoolToJson(g_environmentSummary.allowPowerAdjustment) << ",\n";
    summary << "  \"allowRateAdjustment\": "
            << BoolToJson(g_environmentSummary.allowRateAdjustment) << ",\n";
    summary << "  \"allowRelayReselection\": "
            << BoolToJson(g_environmentSummary.allowRelayReselection) << ",\n";
    summary << "  \"allowSlotReallocation\": "
            << BoolToJson(g_environmentSummary.allowSlotReallocation) << ",\n";
    summary << "  \"allowRouteRebuild\": "
            << BoolToJson(g_environmentSummary.allowRouteRebuild) << ",\n";
    summary << "  \"buildingFeatureCount\": " << g_environmentSummary.buildingFeatureCount
            << ",\n";
    summary << "  \"forestFeatureCount\": " << g_environmentSummary.forestFeatureCount
            << ",\n";
    summary << "  \"waterFeatureCount\": " << g_environmentSummary.waterFeatureCount
            << ",\n";
    summary << "  \"openFieldFeatureCount\": " << g_environmentSummary.openFieldFeatureCount
            << ",\n";
    summary << "  \"maxGeometryHeightM\": " << g_environmentSummary.maxGeometryHeightM
            << ",\n";
    summary << "  \"avgBuildingHeightM\": " << g_environmentSummary.avgBuildingHeightM
            << ",\n";
    summary << "  \"buildingDensityPerKm2\": " << g_environmentSummary.buildingDensityPerKm2
            << ",\n";
    summary << "  \"buildingCoverageRatio\": " << g_environmentSummary.buildingCoverageRatio
            << ",\n";
    summary << "  \"avgStreetWidthM\": " << g_environmentSummary.avgStreetWidthM
            << ",\n";
    summary << "  \"losDecisionMode\": \"" << g_environmentSummary.losDecisionMode << "\",\n";
    summary << "  \"losBlockedPairRatio\": " << g_environmentSummary.losBlockedPairRatio
            << ",\n";
    summary << "  \"avgBuildingCrossingsPerPair\": "
            << g_environmentSummary.avgBuildingCrossingsPerPair << ",\n";
    summary << "  \"primaryForestDensityClass\": \""
            << g_environmentSummary.primaryForestDensityClass << "\",\n";
    summary << "  \"primaryWaterType\": \"" << g_environmentSummary.primaryWaterType << "\",\n";
    summary << "  \"primaryOpenFieldSurfaceType\": \""
            << g_environmentSummary.primaryOpenFieldSurfaceType << "\"\n";
    summary << "}\n";
}

void FinalizeSimulationOutputs()
{
    FinalizeObservedSignalEvents();

    std::cout << "\n========================================" << std::endl;
    std::cout << "仿真完成！" << std::endl;
    std::cout << "========================================" << std::endl;

    g_flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier =
        DynamicCast<Ipv4FlowClassifier>(g_flowHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = g_flowMonitor->GetFlowStats();

    double totalPDR = 0.0;
    double totalDelay = 0.0;
    double totalThroughput = 0.0;
    uint32_t flowCount = 0;

    std::ofstream flowStatsLog(g_config.outputDir + "/rtk-flow-stats.csv");
    flowStatsLog << "FlowId,Src,Dest,Tx,Rx,LossRate\n";

    for (auto& [flowId, flowStats] : stats)
    {
        if (flowStats.txPackets == 0)
        {
            continue;
        }

        Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(flowId);
        uint32_t srcId = (tuple.sourceAddress.Get() & 0xFF) - 1;
        uint32_t dstId = (tuple.destinationAddress.Get() & 0xFF) - 1;
        if (srcId >= g_uavNodes.GetN() || dstId >= g_uavNodes.GetN())
        {
            continue;
        }

        double pdr = static_cast<double>(flowStats.rxPackets) / flowStats.txPackets;
        double delay = flowStats.rxPackets > 0
                           ? flowStats.delaySum.GetSeconds() / flowStats.rxPackets
                           : 0.0;
        double throughput = flowStats.rxBytes * 8.0 / g_config.duration;

        totalPDR += pdr;
        totalDelay += delay;
        totalThroughput += throughput;
        flowCount++;

        double lossRate =
            (flowStats.txPackets - flowStats.rxPackets) * 100.0 / flowStats.txPackets;
        flowStatsLog << flowId << "," << srcId << "," << dstId << ","
                     << flowStats.txPackets << "," << flowStats.rxPackets << ","
                     << lossRate << "%\n";
    }
    flowStatsLog.close();

    if (flowCount > 0)
    {
        std::cout << "平均分组投递率: " << (totalPDR / flowCount * 100) << "%" << std::endl;
        std::cout << "平均端到端时延: " << (totalDelay / flowCount * 1000) << " ms"
                  << std::endl;
        std::cout << "总吞吐量: " << (totalThroughput / 1e6) << " Mbps" << std::endl;
        if (IsCooperativeMode())
        {
            WriteCooperativeModeSummaryJson();
            WriteCooperativeFailureTimelineJson();
            WriteCooperativeRecoveryTimelineJson();
            WriteCooperativeMetricsTimeseriesJson();
            WriteCooperativeDashboardSnapshotJson(totalPDR / flowCount,
                                                  totalThroughput / 1e6,
                                                  totalDelay / flowCount * 1000.0);
        }
    }

    std::cout << "输出文件保存在: " << g_config.outputDir << std::endl;
    std::cout << "========================================" << std::endl;

    g_posLog.close();
    g_topoChangesLog.close();
    g_transLog.close();
    if (g_tdmaLog.is_open())
    {
        g_tdmaLog.close();
    }

    g_resourceLog.close();
    g_resourceDetailedLog.close();
    g_qosLog.close();
    g_topologyLog.close();
    g_topologyEvolutionLog.close();
    g_topologyDetailedLog.close();
    if (g_observedSignalEventsLog.is_open())
    {
        g_observedSignalEventsLog.close();
    }
    if (g_observedCommWindowsLog.is_open())
    {
        g_observedCommWindowsLog.close();
    }
    if (g_observedLinkEvidenceLog.is_open())
    {
        g_observedLinkEvidenceLog.close();
    }
    if (g_inferredTopologyEdgesLog.is_open())
    {
        g_inferredTopologyEdgesLog.close();
    }
    if (g_inferredGraphNodesLog.is_open())
    {
        g_inferredGraphNodesLog.close();
    }
    if (g_keyNodeCandidatesLog.is_open())
    {
        g_keyNodeCandidatesLog.close();
    }
    if (g_cooperativeFailureEventsLog.is_open())
    {
        g_cooperativeFailureEventsLog.close();
    }
    if (g_cooperativeRecoveryActionsLog.is_open())
    {
        g_cooperativeRecoveryActionsLog.close();
    }
    if (g_cooperativeRecoveryMetricsLog.is_open())
    {
        g_cooperativeRecoveryMetricsLog.close();
    }
    if (g_cooperativeDecisionTraceLog.is_open())
    {
        g_cooperativeDecisionTraceLog.close();
    }

    Simulator::Destroy();
}
