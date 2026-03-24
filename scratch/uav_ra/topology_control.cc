#include "context.h"

#include <queue>

NS_LOG_COMPONENT_DEFINE("UAVResourceAllocationTopology");

namespace
{

std::set<uint32_t> g_failedInterfaceNodes;

bool IsCooperativeMode()
{
    return g_environmentConfig.operationMode == OperationMode::Cooperative;
}

bool IsWaterSurfaceScene()
{
    return g_environmentSummary.sceneType == "lake" || g_environmentSummary.hasWaterSurface;
}

uint32_t GetLocalRecoveryScopeLimit()
{
    const auto mode = g_environmentConfig.cooperativeControlConfig.communicationMode;
    const uint32_t hopLimit =
        std::max<uint32_t>(1, g_environmentConfig.cooperativeControlConfig.distributedHopLimit);

    if (mode == CommunicationMode::Distributed)
    {
        return hopLimit >= 2 ? 8u : 4u;
    }
    if (mode == CommunicationMode::Hybrid)
    {
        return hopLimit >= 2 ? 10u : 6u;
    }
    return std::numeric_limits<uint32_t>::max();
}

void MarkCooperativeResponseStarted(double now)
{
    if (!std::isnan(g_cooperativeRuntime.responseTimeSec))
    {
        return;
    }
    if (g_cooperativeRuntime.currentFailureActivationTime < 0.0)
    {
        return;
    }

    g_cooperativeRuntime.responseTimeSec =
        std::max(0.0, now - g_cooperativeRuntime.currentFailureActivationTime);
}

bool IsCooperativeFailureWindowActive(double now)
{
    if (!IsCooperativeMode())
    {
        return false;
    }

    const auto& coop = g_environmentConfig.cooperativeControlConfig;
    if (coop.failureDuration <= 0.0)
    {
        return false;
    }
    return now >= coop.failureStartTime && now < coop.failureStartTime + coop.failureDuration;
}

bool IsCooperativeTargetNode(uint32_t nodeId)
{
    const auto& coop = g_environmentConfig.cooperativeControlConfig;
    return coop.failureTargetId >= 0 &&
           nodeId == static_cast<uint32_t>(coop.failureTargetId);
}

bool IsNodeCurrentlyFailed(uint32_t nodeId)
{
    if (!IsCooperativeMode() || !IsCooperativeFailureWindowActive(Simulator::Now().GetSeconds()))
    {
        return false;
    }

    const auto& coop = g_environmentConfig.cooperativeControlConfig;
    return coop.failureType == CooperativeFailureType::NodeFailure && IsCooperativeTargetNode(nodeId);
}

void SyncFailedNodeInterfaces()
{
    if (!IsCooperativeMode())
    {
        return;
    }

    for (uint32_t nodeId = 0; nodeId < g_uavNodes.GetN(); ++nodeId)
    {
        Ptr<Node> node = g_uavNodes.Get(nodeId);
        Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
        if (!ipv4)
        {
            continue;
        }

        const bool shouldBeDown = IsNodeCurrentlyFailed(nodeId);
        const bool isMarkedDown = g_failedInterfaceNodes.count(nodeId) > 0;
        if (shouldBeDown == isMarkedDown)
        {
            continue;
        }

        for (uint32_t ifIndex = 1; ifIndex < ipv4->GetNInterfaces(); ++ifIndex)
        {
            if (shouldBeDown)
            {
                ipv4->SetDown(ifIndex);
            }
            else
            {
                ipv4->SetUp(ifIndex);
            }
        }

        if (shouldBeDown)
        {
            g_failedInterfaceNodes.insert(nodeId);
        }
        else
        {
            g_failedInterfaceNodes.erase(nodeId);
        }
    }
}

bool IsNeighborOfFailureTarget(uint32_t nodeId)
{
    if (!IsCooperativeMode() || !IsCooperativeFailureWindowActive(Simulator::Now().GetSeconds()))
    {
        return false;
    }

    const auto& coop = g_environmentConfig.cooperativeControlConfig;
    if (coop.failureTargetId < 0)
    {
        return false;
    }
    uint32_t targetId = static_cast<uint32_t>(coop.failureTargetId);
    if (targetId >= g_uavNodes.GetN() || nodeId >= g_uavNodes.GetN() || nodeId == targetId)
    {
        return false;
    }

    return CalculateDistance(g_uavNodes.Get(targetId), g_uavNodes.Get(nodeId)) <= 150.0;
}

std::vector<uint32_t> GetPreferredFailureNeighbors(uint32_t targetId)
{
    std::vector<std::pair<double, uint32_t>> ranked;
    if (targetId >= g_uavNodes.GetN())
    {
        return {};
    }

    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i)
    {
        if (i == targetId || IsNodeCurrentlyFailed(i))
        {
            continue;
        }
        double dist = CalculateDistance(g_uavNodes.Get(targetId), g_uavNodes.Get(i));
        ranked.push_back({dist, i});
    }

    std::sort(ranked.begin(), ranked.end());
    std::vector<uint32_t> chosen;
    for (size_t i = 0; i < ranked.size() && i < 2; ++i)
    {
        chosen.push_back(ranked[i].second);
    }
    return chosen;
}

bool IsLinkInFailureScope(uint32_t srcId, uint32_t dstId)
{
    if (!IsCooperativeMode() || !IsCooperativeFailureWindowActive(Simulator::Now().GetSeconds()))
    {
        return false;
    }

    const auto& coop = g_environmentConfig.cooperativeControlConfig;
    if (coop.failureTargetId < 0)
    {
        return false;
    }
    uint32_t targetId = static_cast<uint32_t>(coop.failureTargetId);

    switch (coop.failureType)
    {
    case CooperativeFailureType::EnvironmentDegradation:
        return srcId == targetId || dstId == targetId;
    case CooperativeFailureType::LinkDegradation:
    {
        if (srcId != targetId && dstId != targetId)
        {
            return false;
        }
        uint32_t otherId = srcId == targetId ? dstId : srcId;
        auto preferred = GetPreferredFailureNeighbors(targetId);
        return std::find(preferred.begin(), preferred.end(), otherId) != preferred.end();
    }
    default:
        return false;
    }
}

uint32_t GetAffectedNeighborCountForFailure(int32_t targetNodeId)
{
    if (targetNodeId < 0 || static_cast<uint32_t>(targetNodeId) >= g_uavNodes.GetN())
    {
        return 0;
    }

    uint32_t count = 0;
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i)
    {
        if (i == static_cast<uint32_t>(targetNodeId))
        {
            continue;
        }
        if (CalculateDistance(g_uavNodes.Get(static_cast<uint32_t>(targetNodeId)), g_uavNodes.Get(i)) <=
            150.0)
        {
            count++;
        }
    }
    return count;
}

uint32_t GetAffectedLinkCountForFailure(CooperativeFailureType failureType, int32_t targetNodeId)
{
    if (targetNodeId < 0 || static_cast<uint32_t>(targetNodeId) >= g_uavNodes.GetN())
    {
        return 0;
    }

    switch (failureType)
    {
    case CooperativeFailureType::NodeFailure:
        return GetAffectedNeighborCountForFailure(targetNodeId);
    case CooperativeFailureType::EnvironmentDegradation:
        return GetAffectedNeighborCountForFailure(targetNodeId);
    case CooperativeFailureType::ExternalInterference:
        return GetAffectedNeighborCountForFailure(targetNodeId);
    case CooperativeFailureType::LinkDegradation:
        return static_cast<uint32_t>(GetPreferredFailureNeighbors(static_cast<uint32_t>(targetNodeId)).size());
    }
    return 0;
}

std::string BuildFailureEffectSummary(CooperativeFailureType failureType, int32_t targetNodeId)
{
    std::ostringstream oss;
    switch (failureType)
    {
    case CooperativeFailureType::NodeFailure:
        oss << "target node " << targetNodeId << " removed from cooperative set";
        break;
    case CooperativeFailureType::EnvironmentDegradation:
        oss << "target one-hop links receive +8dB path loss and reduced range";
        break;
    case CooperativeFailureType::ExternalInterference:
        oss << "target neighborhood receives elevated external interference";
        break;
    case CooperativeFailureType::LinkDegradation:
        oss << "top-2 preferred links around target " << targetNodeId << " are degraded";
        break;
    }
    return oss.str();
}

int32_t SelectBackupLeaderCandidate()
{
    const auto& backupList = g_environmentConfig.cooperativeControlConfig.backupLeaderList;
    for (uint32_t candidate : backupList)
    {
        if (candidate >= g_uavNodes.GetN())
        {
            continue;
        }
        if (candidate == g_cooperativeRuntime.activeLeaderNodeId || IsNodeCurrentlyFailed(candidate))
        {
            continue;
        }
        return static_cast<int32_t>(candidate);
    }

    struct CandidateScore
    {
        uint32_t nodeId = 0;
        uint32_t degree = 0;
        double avgLinkQuality = 0.0;
    };

    std::vector<CandidateScore> candidates;
    for (uint32_t nodeId : g_cooperativeRuntime.effectiveCooperativeNodes)
    {
        if (nodeId == g_cooperativeRuntime.activeLeaderNodeId || IsNodeCurrentlyFailed(nodeId))
        {
            continue;
        }
        CandidateScore score;
        score.nodeId = nodeId;
        score.degree = static_cast<uint32_t>(g_state.neighbors[nodeId].size());
        double sumQuality = 0.0;
        uint32_t qualityCount = 0;
        for (uint32_t neighborId : g_state.neighbors[nodeId])
        {
            auto it = g_state.linkQuality.find({nodeId, neighborId});
            if (it != g_state.linkQuality.end())
            {
                sumQuality += it->second;
                qualityCount++;
            }
        }
        score.avgLinkQuality = qualityCount > 0 ? sumQuality / qualityCount : 0.0;
        candidates.push_back(score);
    }

    if (candidates.empty())
    {
        return -1;
    }

    std::sort(candidates.begin(),
              candidates.end(),
              [](const CandidateScore& a, const CandidateScore& b) {
                  if (a.degree != b.degree)
                  {
                      return a.degree > b.degree;
                  }
                  if (std::abs(a.avgLinkQuality - b.avgLinkQuality) > 1e-9)
                  {
                      return a.avgLinkQuality > b.avgLinkQuality;
                  }
                  return a.nodeId < b.nodeId;
              });
    return static_cast<int32_t>(candidates.front().nodeId);
}

void AppendCooperativeRecoveryAction(const CooperativeRecoveryAction& action);

double GetCooperativeRangeFactorForLink(uint32_t srcId, uint32_t dstId)
{
    if (!IsCooperativeMode() || !IsCooperativeFailureWindowActive(Simulator::Now().GetSeconds()))
    {
        return 1.0;
    }

    if (g_environmentConfig.cooperativeControlConfig.failureType ==
            CooperativeFailureType::EnvironmentDegradation &&
        IsLinkInFailureScope(srcId, dstId))
    {
        return 0.80;
    }

    return 1.0;
}

double GetCooperativeExtraPathLossDb(uint32_t srcId, uint32_t dstId)
{
    if (!IsCooperativeMode() || !IsCooperativeFailureWindowActive(Simulator::Now().GetSeconds()))
    {
        return 0.0;
    }

    const auto failureType = g_environmentConfig.cooperativeControlConfig.failureType;
    if (failureType == CooperativeFailureType::EnvironmentDegradation &&
        IsLinkInFailureScope(srcId, dstId))
    {
        return 8.0;
    }
    if (failureType == CooperativeFailureType::LinkDegradation &&
        IsLinkInFailureScope(srcId, dstId))
    {
        return 10.0;
    }
    return 0.0;
}

double GetCooperativeExtraInterferenceMw(uint32_t dstId)
{
    if (!IsCooperativeMode() || !IsCooperativeFailureWindowActive(Simulator::Now().GetSeconds()))
    {
        return 0.0;
    }

    const auto failureType = g_environmentConfig.cooperativeControlConfig.failureType;
    if (failureType != CooperativeFailureType::ExternalInterference)
    {
        return 0.0;
    }

    if (!IsCooperativeTargetNode(dstId) && !IsNeighborOfFailureTarget(dstId))
    {
        return 0.0;
    }

    return std::pow(10.0, -89.0 / 10.0);
}

void RecordCooperativeFailureStateTransition(bool activeNow)
{
    if (!IsCooperativeMode())
    {
        return;
    }

    if (activeNow == g_cooperativeRuntime.lastFailureActiveState)
    {
        return;
    }

    CooperativeFailureEvent event;
    event.time = Simulator::Now().GetSeconds();
    const auto failureType = g_environmentConfig.cooperativeControlConfig.failureType;
    event.failureType =
        CooperativeFailureTypeToString(failureType);
    event.targetNodeId = g_environmentConfig.cooperativeControlConfig.failureTargetId;
    event.isLeaderTarget =
        event.targetNodeId >= 0 &&
        static_cast<uint32_t>(event.targetNodeId) == g_cooperativeRuntime.activeLeaderNodeId;
    event.targetRole = event.isLeaderTarget ? "leader" : "follower";
    event.failureState = activeNow ? "activated" : "cleared";
    event.affectedNeighborCount = GetAffectedNeighborCountForFailure(event.targetNodeId);
    event.affectedLinkCount = GetAffectedLinkCountForFailure(failureType, event.targetNodeId);
    event.effectSummary = BuildFailureEffectSummary(failureType, event.targetNodeId);
    event.source = "simulation_configured_fault";

    g_cooperativeRuntime.failureEvents.push_back(event);
    if (g_cooperativeFailureEventsLog.is_open())
    {
        g_cooperativeFailureEventsLog
            << event.time << ","
            << event.failureType << ","
            << event.targetNodeId << ","
            << (event.isLeaderTarget ? "true" : "false") << ","
            << event.failureState << ","
            << g_environmentSummary.communicationMode << ","
            << g_environmentSummary.recoveryPolicy << ","
            << g_environmentSummary.sceneType << ","
            << g_environmentSummary.operationMode << "\n";
    }

    g_cooperativeRuntime.lastFailureActiveState = activeNow;
}

void ApplyCooperativeFailureState()
{
    if (!IsCooperativeMode())
    {
        return;
    }

    const bool failureActive = IsCooperativeFailureWindowActive(Simulator::Now().GetSeconds());
    g_cooperativeRuntime.failureActive = failureActive;
    if (failureActive && !g_cooperativeRuntime.lastFailureActiveState)
    {
        g_cooperativeRuntime.currentFailureActivationTime = Simulator::Now().GetSeconds();
        g_cooperativeRuntime.lastRecoveryTriggerTime = -1.0;
        g_cooperativeRuntime.lastRecoveryActionTime = -1.0;
        g_cooperativeRuntime.leaderFailureDetectedAt = -1.0;
        g_cooperativeRuntime.leaderSwitchCompletedAt = -1.0;
        g_cooperativeRuntime.recoveryCompletedAt = -1.0;
        g_cooperativeRuntime.stabilizationCompletedAt = -1.0;
        g_cooperativeRuntime.responseTimeSec = std::numeric_limits<double>::quiet_NaN();
        g_cooperativeRuntime.recoveryTimeSec = std::numeric_limits<double>::quiet_NaN();
        g_cooperativeRuntime.stabilizationTimeSec = std::numeric_limits<double>::quiet_NaN();
        g_cooperativeRuntime.recoveryActive = false;
        g_cooperativeRuntime.stabilizationActive = false;
        g_cooperativeRuntime.leaderTransitionActive = false;
        g_cooperativeRuntime.lastLeaderSwitchReason = "no_switch";
        g_cooperativeRuntime.currentPhase = "failure";
    }
    if (!failureActive)
    {
        if (!g_cooperativeRuntime.recoveryActive && !g_cooperativeRuntime.stabilizationActive &&
            g_cooperativeRuntime.recoveryCompletedAt < 0.0)
        {
            g_cooperativeRuntime.currentPhase = "normal";
        }
    }
    RecordCooperativeFailureStateTransition(failureActive);
    SyncFailedNodeInterfaces();

    g_cooperativeRuntime.effectiveCooperativeNodes.clear();
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i)
    {
        if (!IsNodeCurrentlyFailed(i))
        {
            g_cooperativeRuntime.effectiveCooperativeNodes.insert(i);
        }
    }

    const auto& coop = g_environmentConfig.cooperativeControlConfig;
    g_cooperativeRuntime.leaderAlive = !IsNodeCurrentlyFailed(g_cooperativeRuntime.activeLeaderNodeId);
    if (coop.failureTargetId >= 0 && failureActive &&
        coop.failureType == CooperativeFailureType::NodeFailure &&
        static_cast<uint32_t>(coop.failureTargetId) == g_cooperativeRuntime.activeLeaderNodeId)
    {
        g_cooperativeRuntime.leaderAlive = false;
    }

    const bool leaderFailureScenario =
        failureActive &&
        coop.failureType == CooperativeFailureType::NodeFailure &&
        coop.failureTargetId >= 0 &&
        static_cast<uint32_t>(coop.failureTargetId) == g_cooperativeRuntime.activeLeaderNodeId &&
        (coop.communicationMode == CommunicationMode::Centralized ||
         coop.communicationMode == CommunicationMode::Hybrid);

    if (leaderFailureScenario)
    {
        const double now = Simulator::Now().GetSeconds();
        constexpr double kLeaderDetectDelaySec = 0.5;
        constexpr double kLeaderTransitionSec = 1.0;

        if (g_cooperativeRuntime.leaderFailureDetectedAt < 0.0 &&
            g_cooperativeRuntime.currentFailureActivationTime >= 0.0 &&
            now >= g_cooperativeRuntime.currentFailureActivationTime + kLeaderDetectDelaySec)
        {
            g_cooperativeRuntime.leaderFailureDetectedAt = now;
            g_cooperativeRuntime.activeBackupLeaderNodeId = SelectBackupLeaderCandidate();
            g_cooperativeRuntime.leaderTransitionActive = true;
            g_cooperativeRuntime.currentPhase = "transition";
            MarkCooperativeResponseStarted(now);
            g_cooperativeRuntime.lastLeaderSwitchReason =
                g_cooperativeRuntime.activeBackupLeaderNodeId >= 0
                    ? "leader_failure_detected_transition_started"
                    : "leader_failure_detected_no_backup_candidate";
        }

        if (g_cooperativeRuntime.leaderTransitionActive &&
            g_cooperativeRuntime.leaderFailureDetectedAt >= 0.0 &&
            now >= g_cooperativeRuntime.leaderFailureDetectedAt + kLeaderTransitionSec)
        {
            if (g_cooperativeRuntime.activeBackupLeaderNodeId >= 0)
            {
                const uint32_t previousLeader = g_cooperativeRuntime.activeLeaderNodeId;
                g_cooperativeRuntime.activeLeaderNodeId =
                    static_cast<uint32_t>(g_cooperativeRuntime.activeBackupLeaderNodeId);
                g_cooperativeRuntime.leaderAlive = true;
                g_cooperativeRuntime.leaderTransitionActive = false;
                g_cooperativeRuntime.leaderSwitchCompletedAt = now;
                g_cooperativeRuntime.lastLeaderSwitchReason = "backup_leader_switch_completed";

                CooperativeRecoveryAction action;
                action.time = now;
                action.phase = "transition";
                action.communicationMode = CommunicationModeToString(coop.communicationMode);
                action.recoveryPolicy = g_environmentSummary.recoveryPolicy;
                action.effectiveRecoveryPolicy = g_cooperativeRuntime.effectiveRecoveryPolicy;
                action.actionType = "leader_switch_completed";
                action.executorNodeId = static_cast<int32_t>(g_cooperativeRuntime.activeLeaderNodeId);
                action.targetNodeIds = std::to_string(g_cooperativeRuntime.activeLeaderNodeId);
                action.oldValue = std::to_string(previousLeader);
                action.newValue = std::to_string(g_cooperativeRuntime.activeLeaderNodeId);
                action.scope = "control_plane";
                action.expectedEffect = "restore leader-driven coordination after transition";
                action.resultState = "executed";
                action.decisionReason = g_cooperativeRuntime.lastLeaderSwitchReason;
                AppendCooperativeRecoveryAction(action);

                g_cooperativeRuntime.activeBackupLeaderNodeId = SelectBackupLeaderCandidate();
            }
            else
            {
                g_cooperativeRuntime.lastLeaderSwitchReason = "leader_switch_failed_no_candidate";
            }
        }
    }
    else
    {
        g_cooperativeRuntime.leaderTransitionActive = false;
    }
}

void AppendInferredGraphNode(const InferredGraphNode& node)
{
    g_observationRuntime.inferredGraphNodes.push_back(node);
    if (!g_inferredGraphNodesLog.is_open())
    {
        return;
    }

    auto writeOrNaN = [&](double value) {
        if (std::isnan(value))
        {
            g_inferredGraphNodesLog << "nan";
        }
        else
        {
            g_inferredGraphNodesLog << value;
        }
    };

    writeOrNaN(node.windowStart);
    g_inferredGraphNodesLog << ",";
    writeOrNaN(node.windowEnd);
    g_inferredGraphNodesLog << "," << node.observedNodeId << ","
                            << node.incidentEdgeCount << ","
                            << node.weightedDegreeScore << ","
                            << node.avgIncidentProbability << ","
                            << node.avgIncidentConfidence << ","
                            << node.sceneType << ","
                            << node.operationMode << "\n";
}

void AppendKeyNodeCandidate(const KeyNodeCandidate& candidate)
{
    g_observationRuntime.keyNodeCandidates.push_back(candidate);
    if (!g_keyNodeCandidatesLog.is_open())
    {
        return;
    }

    auto writeOrNaN = [&](double value) {
        if (std::isnan(value))
        {
            g_keyNodeCandidatesLog << "nan";
        }
        else
        {
            g_keyNodeCandidatesLog << value;
        }
    };

    writeOrNaN(candidate.windowStart);
    g_keyNodeCandidatesLog << ",";
    writeOrNaN(candidate.windowEnd);
    g_keyNodeCandidatesLog << "," << candidate.observedNodeId << ","
                           << candidate.rank << ","
                           << candidate.weightedDegreeScore << ","
                           << candidate.avgIncidentProbability << ","
                           << candidate.avgIncidentConfidence << ","
                           << candidate.keyNodeScore << ","
                           << candidate.keyNodeMethod << ","
                           << candidate.sceneType << ","
                           << candidate.operationMode << "\n";
}

struct CooperativeNetworkStateSnapshot
{
    double time = 0.0;
    uint32_t numLinks = 0;
    double connectivity = 0.0;
    double avgDegree = 0.0;
    double avgPdr = 0.0;
    double avgThroughputMbps = 0.0;
    double avgDelayMs = 0.0;
    double p99DelayMs = 0.0;
    uint32_t activeNodeCount = 0;
};

struct EffectiveRecoveryDecision
{
    RecoveryPolicy effectivePolicy = RecoveryPolicy::GlobalRecovery;
    std::string reason = "requested_policy_applied";
};

struct FailureNeighborhoodMetrics
{
    double avgPdr = std::numeric_limits<double>::quiet_NaN();
    double avgThroughputMbps = std::numeric_limits<double>::quiet_NaN();
    double avgDelayMs = std::numeric_limits<double>::quiet_NaN();
    uint32_t nodeCount = 0;
};

struct FailureTargetMetrics
{
    int32_t targetNodeId = -1;
    bool valid = false;
    bool failed = false;
    double pdr = std::numeric_limits<double>::quiet_NaN();
    double throughputMbps = std::numeric_limits<double>::quiet_NaN();
    double delayMs = std::numeric_limits<double>::quiet_NaN();
};

void AppendCooperativeRecoveryAction(const CooperativeRecoveryAction& action);
FailureNeighborhoodMetrics CollectFailureNeighborhoodMetrics();
FailureTargetMetrics CollectFailureTargetMetrics();

bool IsRecoveryCriteriaSatisfied(const CooperativeNetworkStateSnapshot& snapshot)
{
    const bool waterScene = IsWaterSurfaceScene();
    const FailureNeighborhoodMetrics localMetrics = CollectFailureNeighborhoodMetrics();
    const double currentPdr =
        localMetrics.nodeCount > 0 ? localMetrics.avgPdr : snapshot.avgPdr;
    const double currentThroughputMbps =
        localMetrics.nodeCount > 0 ? localMetrics.avgThroughputMbps : snapshot.avgThroughputMbps;
    const double currentDelayMs =
        localMetrics.nodeCount > 0 ? localMetrics.avgDelayMs : snapshot.avgDelayMs;
    uint32_t businessPassCount = 0;
    double connectivityThreshold = 0.80;
    if (g_cooperativeRuntime.baselineValid &&
        !std::isnan(g_cooperativeRuntime.baselineConnectivity) &&
        g_cooperativeRuntime.baselineConnectivity > 1e-6)
    {
        const double ratio = waterScene ? 0.95 : 0.90;
        connectivityThreshold =
            std::max(waterScene ? 0.85 : 0.80,
                     g_cooperativeRuntime.baselineConnectivity * ratio);
        connectivityThreshold = std::min(1.0, connectivityThreshold);
    }

    double pdrThreshold = g_config.targetPDR;
    const double baselinePdr =
        !std::isnan(g_cooperativeRuntime.baselineLocalPdr) &&
                g_cooperativeRuntime.baselineLocalPdr > 1e-6
            ? g_cooperativeRuntime.baselineLocalPdr
            : g_cooperativeRuntime.baselinePdr;
    if (g_cooperativeRuntime.baselineValid && !std::isnan(baselinePdr) && baselinePdr > 1e-6)
    {
        pdrThreshold =
            std::max(waterScene ? 0.20 : 0.18,
                     std::min(g_config.targetPDR, baselinePdr * 0.90));
    }
    if (currentPdr >= pdrThreshold)
    {
        businessPassCount++;
    }

    double throughputThresholdMbps = waterScene ? 0.10 : 0.20;
    const double baselineThroughputMbps =
        !std::isnan(g_cooperativeRuntime.baselineLocalThroughputMbps) &&
                g_cooperativeRuntime.baselineLocalThroughputMbps > 1e-6
            ? g_cooperativeRuntime.baselineLocalThroughputMbps
            : g_cooperativeRuntime.baselineThroughputMbps;
    if (g_cooperativeRuntime.baselineValid && !std::isnan(baselineThroughputMbps) &&
        baselineThroughputMbps > 1e-6)
    {
        throughputThresholdMbps =
            std::max(waterScene ? 0.10 : 0.20,
                     baselineThroughputMbps * 0.8);
    }
    if (currentThroughputMbps >= throughputThresholdMbps)
    {
        businessPassCount++;
    }

    double delayThresholdMs = 100.0;
    const double baselineDelayMs =
        !std::isnan(g_cooperativeRuntime.baselineLocalDelayMs) &&
                g_cooperativeRuntime.baselineLocalDelayMs > 1e-6
            ? g_cooperativeRuntime.baselineLocalDelayMs
            : g_cooperativeRuntime.baselineDelayMs;
    if (g_cooperativeRuntime.baselineValid && !std::isnan(baselineDelayMs) &&
        baselineDelayMs > 1e-6)
    {
        delayThresholdMs =
            std::max(waterScene ? 140.0 : 100.0,
                     baselineDelayMs * (waterScene ? 1.30 : 1.25));
    }
    if (currentDelayMs <= delayThresholdMs)
    {
        businessPassCount++;
    }

    const bool structurallyRecovered = snapshot.connectivity >= connectivityThreshold;
    const bool businessRecovered = businessPassCount >= 2;
    return structurallyRecovered && businessRecovered;
}

bool IsStabilizationWindowSatisfied(double now)
{
    constexpr double kStableWindowSec = 3.0;
    const bool waterScene = IsWaterSurfaceScene();
    if (g_cooperativeRuntime.recoveryCompletedAt < 0.0)
    {
        return false;
    }

    const double windowStart = now - kStableWindowSec;
    if (windowStart < g_cooperativeRuntime.recoveryCompletedAt)
    {
        return false;
    }
    if (g_cooperativeRuntime.lastRecoveryActionTime >= windowStart)
    {
        return false;
    }

    double minConnectivity = std::numeric_limits<double>::infinity();
    double maxConnectivity = -std::numeric_limits<double>::infinity();
    double minPdr = std::numeric_limits<double>::infinity();
    double maxPdr = -std::numeric_limits<double>::infinity();
    double minThroughput = std::numeric_limits<double>::infinity();
    double maxThroughput = -std::numeric_limits<double>::infinity();
    double minDelay = std::numeric_limits<double>::infinity();
    double maxDelay = -std::numeric_limits<double>::infinity();
    size_t count = 0;

    for (const auto& sample : g_cooperativeRuntime.metricsHistory)
    {
        if (sample.time + 1e-9 < windowStart)
        {
            continue;
        }
        minConnectivity = std::min(minConnectivity, sample.connectivity);
        maxConnectivity = std::max(maxConnectivity, sample.connectivity);
        minPdr = std::min(minPdr, sample.pdr);
        maxPdr = std::max(maxPdr, sample.pdr);
        minThroughput = std::min(minThroughput, sample.throughputMbps);
        maxThroughput = std::max(maxThroughput, sample.throughputMbps);
        minDelay = std::min(minDelay, sample.delayMs);
        maxDelay = std::max(maxDelay, sample.delayMs);
        count++;
    }

    if (count < 2)
    {
        return false;
    }

    if (maxConnectivity - minConnectivity > (waterScene ? 0.08 : 0.06))
    {
        return false;
    }
    if (maxPdr - minPdr > (waterScene ? 0.15 : 0.10))
    {
        return false;
    }
    if (maxDelay - minDelay > (waterScene ? 60.0 : 30.0))
    {
        return false;
    }

    if (g_cooperativeRuntime.baselineValid &&
        !std::isnan(g_cooperativeRuntime.baselineThroughputMbps) &&
        g_cooperativeRuntime.baselineThroughputMbps > 1e-6)
    {
        const double lower =
            g_cooperativeRuntime.baselineThroughputMbps * (waterScene ? 0.70 : 0.75);
        const double upper =
            g_cooperativeRuntime.baselineThroughputMbps * (waterScene ? 1.35 : 1.25);
        if (minThroughput < lower || maxThroughput > upper)
        {
            return false;
        }
    }
    else
    {
        const double reference = std::max(0.1, maxThroughput);
        if (maxThroughput - minThroughput >
            reference * (waterScene ? 0.35 : 0.2))
        {
            return false;
        }
    }

    return true;
}

void UpdateCooperativePhaseAndTimers(const CooperativeNetworkStateSnapshot& snapshot)
{
    const double now = snapshot.time;
    const bool hasFailureEpisode =
        g_cooperativeRuntime.failureActive ||
        g_cooperativeRuntime.currentFailureActivationTime >= 0.0 ||
        g_cooperativeRuntime.lastRecoveryTriggerTime >= 0.0 ||
        g_cooperativeRuntime.recoveryActive ||
        g_cooperativeRuntime.stabilizationActive ||
        g_cooperativeRuntime.leaderTransitionActive ||
        g_cooperativeRuntime.recoveryCompletedAt >= 0.0;

    if (!hasFailureEpisode)
    {
        g_cooperativeRuntime.currentPhase = "normal";
        g_cooperativeRuntime.recoveryActive = false;
        g_cooperativeRuntime.stabilizationActive = false;
        return;
    }

    if (g_cooperativeRuntime.leaderTransitionActive)
    {
        g_cooperativeRuntime.currentPhase = "transition";
        g_cooperativeRuntime.stabilizationActive = false;
        return;
    }

    const bool criteriaSatisfied = IsRecoveryCriteriaSatisfied(snapshot);

    if (criteriaSatisfied)
    {
        MarkCooperativeResponseStarted(now);
        if (g_cooperativeRuntime.recoveryCompletedAt < 0.0 &&
            g_cooperativeRuntime.currentFailureActivationTime >= 0.0)
        {
            g_cooperativeRuntime.recoveryCompletedAt = now;
            g_cooperativeRuntime.recoveryTimeSec =
                now - g_cooperativeRuntime.currentFailureActivationTime;
            g_cooperativeRuntime.recoveryActive = false;
        }

        if (IsStabilizationWindowSatisfied(now))
        {
            if (g_cooperativeRuntime.stabilizationCompletedAt < 0.0 &&
                g_cooperativeRuntime.recoveryCompletedAt >= 0.0)
            {
                g_cooperativeRuntime.stabilizationCompletedAt = now;
                g_cooperativeRuntime.stabilizationTimeSec =
                    now - g_cooperativeRuntime.recoveryCompletedAt;
            }
            g_cooperativeRuntime.stabilizationActive = true;
            g_cooperativeRuntime.currentPhase = "stabilization";
        }
        else
        {
            g_cooperativeRuntime.stabilizationActive = false;
            g_cooperativeRuntime.currentPhase = "recovery";
        }
    }
    else
    {
        if (!g_cooperativeRuntime.failureActive && g_cooperativeRuntime.lastRecoveryTriggerTime < 0.0)
        {
            g_cooperativeRuntime.currentPhase = "normal";
        }
        else if (g_cooperativeRuntime.recoveryActive || g_cooperativeRuntime.lastRecoveryTriggerTime >= 0.0)
        {
            g_cooperativeRuntime.currentPhase = "recovery";
        }
        else if (g_cooperativeRuntime.failureActive)
        {
            g_cooperativeRuntime.currentPhase = "failure";
        }
        else
        {
            g_cooperativeRuntime.currentPhase = "normal";
        }
        g_cooperativeRuntime.stabilizationActive = false;
        g_cooperativeRuntime.stabilizationCompletedAt = -1.0;
        g_cooperativeRuntime.stabilizationTimeSec = std::numeric_limits<double>::quiet_NaN();
    }
}

CooperativeNetworkStateSnapshot CollectCooperativeNetworkState()
{
    CooperativeNetworkStateSnapshot snapshot;
    snapshot.time = Simulator::Now().GetSeconds();

    const uint32_t n = g_uavNodes.GetN();
    for (uint32_t i = 0; i < n; ++i)
    {
        snapshot.numLinks += g_state.neighbors[i].size();
    }
    snapshot.numLinks /= 2;

    if (n > 1)
    {
        const uint32_t maxLinks = n * (n - 1) / 2;
        snapshot.connectivity =
            maxLinks > 0 ? static_cast<double>(snapshot.numLinks) / maxLinks : 0.0;
        snapshot.avgDegree = 2.0 * snapshot.numLinks / n;
    }

    snapshot.activeNodeCount =
        static_cast<uint32_t>(g_cooperativeRuntime.effectiveCooperativeNodes.size());

    std::vector<double> delaysMs;
    double pdrSum = 0.0;
    double throughputSumMbps = 0.0;
    double delaySumMs = 0.0;
    uint32_t metricNodeCount = 0;

    for (uint32_t nodeId : g_cooperativeRuntime.effectiveCooperativeNodes)
    {
        pdrSum += g_state.nodePDR[nodeId];
        throughputSumMbps += g_state.nodeThroughput[nodeId] / 1e6;
        const double delayMs = g_state.nodeDelay[nodeId] * 1000.0;
        delaySumMs += delayMs;
        delaysMs.push_back(delayMs);
        metricNodeCount++;
    }

    if (metricNodeCount > 0)
    {
        snapshot.avgPdr = pdrSum / metricNodeCount;
        snapshot.avgThroughputMbps = throughputSumMbps / metricNodeCount;
        snapshot.avgDelayMs = delaySumMs / metricNodeCount;
    }

    if (!delaysMs.empty())
    {
        std::sort(delaysMs.begin(), delaysMs.end());
        size_t idx = static_cast<size_t>(std::ceil(0.99 * delaysMs.size())) - 1;
        idx = std::min(idx, delaysMs.size() - 1);
        snapshot.p99DelayMs = delaysMs[idx];
    }

    return snapshot;
}

bool ShouldTriggerRecovery(const CooperativeNetworkStateSnapshot& snapshot)
{
    if (!IsCooperativeMode() || !g_cooperativeRuntime.failureActive)
    {
        return false;
    }

    const double now = snapshot.time;
    if (g_cooperativeRuntime.lastRecoveryTriggerTime >= 0.0 &&
        now - g_cooperativeRuntime.lastRecoveryTriggerTime <
            g_environmentConfig.cooperativeControlConfig.recoveryCooldown)
    {
        return false;
    }

    return !IsRecoveryCriteriaSatisfied(snapshot);
}

EffectiveRecoveryDecision ResolveEffectiveRecoveryDecision()
{
    EffectiveRecoveryDecision decision;
    const auto requested = g_environmentConfig.cooperativeControlConfig.recoveryPolicy;
    decision.effectivePolicy = requested;

    if (g_cooperativeRuntime.leaderTransitionActive)
    {
        decision.effectivePolicy = RecoveryPolicy::LocalRecovery;
        decision.reason = "leader_failure_transition_local_autonomy";
        return decision;
    }

    if (g_environmentConfig.cooperativeControlConfig.communicationMode ==
            CommunicationMode::Distributed &&
        requested == RecoveryPolicy::GlobalRecovery)
    {
        decision.effectivePolicy = RecoveryPolicy::LocalRecovery;
        decision.reason = "distributed_mode_downgraded_to_local_recovery";
        return decision;
    }

    if (requested == RecoveryPolicy::LocalRecovery)
    {
        decision.reason = "requested_local_recovery_applied";
    }
    else
    {
        decision.reason = "requested_global_recovery_applied";
    }
    return decision;
}

void AppendCooperativeRecoveryAction(const CooperativeRecoveryAction& action)
{
    g_cooperativeRuntime.recoveryActions.push_back(action);
    g_cooperativeRuntime.lastRecoveryActionTime = action.time;
    MarkCooperativeResponseStarted(action.time);
    g_cooperativeRuntime.stabilizationActive = false;
    g_cooperativeRuntime.stabilizationCompletedAt = -1.0;
    g_cooperativeRuntime.stabilizationTimeSec = std::numeric_limits<double>::quiet_NaN();
    if (!g_cooperativeRecoveryActionsLog.is_open())
    {
        return;
    }

    auto escapeCsv = [](const std::string& value) {
        if (value.find_first_of(",\"") == std::string::npos)
        {
            return value;
        }
        std::string escaped = "\"";
        for (char c : value)
        {
            if (c == '"')
            {
                escaped += "\"\"";
            }
            else
            {
                escaped += c;
            }
        }
        escaped += "\"";
        return escaped;
    };

    g_cooperativeRecoveryActionsLog
        << action.time << ","
        << action.communicationMode << ","
        << action.recoveryPolicy << ","
        << action.effectiveRecoveryPolicy << ","
        << action.actionType << ","
        << action.executorNodeId << ","
        << escapeCsv(action.targetNodeIds) << ","
        << action.resultState << ","
        << escapeCsv(action.decisionReason) << ","
        << g_environmentSummary.sceneType << ","
        << g_environmentSummary.operationMode << "\n";
}

void RecordCooperativeDecisionTrace(const CooperativeNetworkStateSnapshot& snapshot)
{
    if (!g_cooperativeDecisionTraceLog.is_open())
    {
        return;
    }

    g_cooperativeDecisionTraceLog
        << snapshot.time << ","
        << g_environmentSummary.communicationMode << ","
        << g_environmentSummary.recoveryPolicy << ","
        << g_cooperativeRuntime.activeLeaderNodeId << ","
        << (g_cooperativeRuntime.failureActive ? "true" : "false") << ","
        << (g_cooperativeRuntime.recoveryActive ? "true" : "false") << ","
        << (g_cooperativeRuntime.stabilizationActive ? "true" : "false") << ","
        << snapshot.activeNodeCount << ","
        << g_cooperativeRuntime.effectiveRecoveryPolicy << ","
        << g_cooperativeRuntime.lastDecisionReason << ","
        << g_environmentSummary.sceneType << ","
        << g_environmentSummary.operationMode << "\n";
}

void RecordCooperativeRecoveryMetrics(const CooperativeNetworkStateSnapshot& snapshot)
{
    const FailureNeighborhoodMetrics localMetrics = CollectFailureNeighborhoodMetrics();
    const FailureTargetMetrics targetMetrics = CollectFailureTargetMetrics();

    CooperativeRecoveryMetrics metrics;
    metrics.time = snapshot.time;
    metrics.phase = g_cooperativeRuntime.currentPhase;
    metrics.connectivity = snapshot.connectivity;
    metrics.avgDegree = snapshot.avgDegree;
    metrics.pdr = snapshot.avgPdr;
    metrics.throughputMbps = snapshot.avgThroughputMbps;
    metrics.delayMs = snapshot.avgDelayMs;
    metrics.p99DelayMs = snapshot.p99DelayMs;
    metrics.failureNeighborhoodPdr = localMetrics.avgPdr;
    metrics.failureNeighborhoodThroughputMbps = localMetrics.avgThroughputMbps;
    metrics.failureNeighborhoodDelayMs = localMetrics.avgDelayMs;
    metrics.failureNeighborhoodNodeCount = localMetrics.nodeCount;
    metrics.failureTargetId = targetMetrics.targetNodeId;
    metrics.isFailureTargetFailed = targetMetrics.failed;
    metrics.failureTargetPdr = targetMetrics.pdr;
    metrics.failureTargetThroughputMbps = targetMetrics.throughputMbps;
    metrics.failureTargetDelayMs = targetMetrics.delayMs;
    metrics.activeNodeCount = snapshot.activeNodeCount;
    metrics.leaderNodeId = g_cooperativeRuntime.activeLeaderNodeId;
    metrics.isLeaderAlive = g_cooperativeRuntime.leaderAlive;
    if (!std::isnan(g_cooperativeRuntime.responseTimeSec))
    {
        metrics.responseTimeSec = g_cooperativeRuntime.responseTimeSec;
    }
    metrics.recoveryTimeSec = g_cooperativeRuntime.recoveryTimeSec;
    metrics.stabilizationTimeSec = g_cooperativeRuntime.stabilizationTimeSec;

    g_cooperativeRuntime.metricsHistory.push_back(metrics);
    if (!g_cooperativeRecoveryMetricsLog.is_open())
    {
        return;
    }

    g_cooperativeRecoveryMetricsLog
        << metrics.time << ","
        << metrics.phase << ","
        << metrics.connectivity << ","
        << metrics.avgDegree << ","
        << metrics.pdr << ","
        << metrics.throughputMbps << ","
        << metrics.delayMs << ","
        << metrics.p99DelayMs << ",";
    if (std::isnan(metrics.responseTimeSec))
    {
        g_cooperativeRecoveryMetricsLog << "nan";
    }
    else
    {
        g_cooperativeRecoveryMetricsLog << metrics.responseTimeSec;
    }
    g_cooperativeRecoveryMetricsLog << ",";
    if (std::isnan(metrics.recoveryTimeSec))
    {
        g_cooperativeRecoveryMetricsLog << "nan";
    }
    else
    {
        g_cooperativeRecoveryMetricsLog << metrics.recoveryTimeSec;
    }
    g_cooperativeRecoveryMetricsLog << ",";
    if (std::isnan(metrics.stabilizationTimeSec))
    {
        g_cooperativeRecoveryMetricsLog << "nan";
    }
    else
    {
        g_cooperativeRecoveryMetricsLog << metrics.stabilizationTimeSec;
    }
    g_cooperativeRecoveryMetricsLog << ",";
    if (std::isnan(metrics.failureNeighborhoodPdr))
    {
        g_cooperativeRecoveryMetricsLog << "nan";
    }
    else
    {
        g_cooperativeRecoveryMetricsLog << metrics.failureNeighborhoodPdr;
    }
    g_cooperativeRecoveryMetricsLog << ",";
    if (std::isnan(metrics.failureNeighborhoodThroughputMbps))
    {
        g_cooperativeRecoveryMetricsLog << "nan";
    }
    else
    {
        g_cooperativeRecoveryMetricsLog << metrics.failureNeighborhoodThroughputMbps;
    }
    g_cooperativeRecoveryMetricsLog << ",";
    if (std::isnan(metrics.failureNeighborhoodDelayMs))
    {
        g_cooperativeRecoveryMetricsLog << "nan";
    }
    else
    {
        g_cooperativeRecoveryMetricsLog << metrics.failureNeighborhoodDelayMs;
    }
    g_cooperativeRecoveryMetricsLog << ","
                                   << metrics.failureNeighborhoodNodeCount << ","
                                   << metrics.failureTargetId << ","
                                   << (metrics.isFailureTargetFailed ? "true" : "false") << ",";
    if (std::isnan(metrics.failureTargetPdr))
    {
        g_cooperativeRecoveryMetricsLog << "nan";
    }
    else
    {
        g_cooperativeRecoveryMetricsLog << metrics.failureTargetPdr;
    }
    g_cooperativeRecoveryMetricsLog << ",";
    if (std::isnan(metrics.failureTargetThroughputMbps))
    {
        g_cooperativeRecoveryMetricsLog << "nan";
    }
    else
    {
        g_cooperativeRecoveryMetricsLog << metrics.failureTargetThroughputMbps;
    }
    g_cooperativeRecoveryMetricsLog << ",";
    if (std::isnan(metrics.failureTargetDelayMs))
    {
        g_cooperativeRecoveryMetricsLog << "nan";
    }
    else
    {
        g_cooperativeRecoveryMetricsLog << metrics.failureTargetDelayMs;
    }
    g_cooperativeRecoveryMetricsLog << ","
                                   << g_cooperativeRuntime.activeLeaderNodeId << ","
                                   << (g_cooperativeRuntime.leaderAlive ? "true" : "false") << ","
                                   << g_environmentSummary.sceneType << ","
                                   << g_environmentSummary.operationMode << "\n";
}

std::set<uint32_t> BuildFailureNeighborhoodNodes(bool applyScopeLimit)
{
    std::set<uint32_t> scope;
    const int32_t target = g_environmentConfig.cooperativeControlConfig.failureTargetId;
    if (target < 0 || static_cast<uint32_t>(target) >= g_uavNodes.GetN())
    {
        return scope;
    }

    const uint32_t targetId = static_cast<uint32_t>(target);
    const uint32_t hopLimit =
        std::max<uint32_t>(1, g_environmentConfig.cooperativeControlConfig.distributedHopLimit);
    const uint32_t scopeLimit = applyScopeLimit ? GetLocalRecoveryScopeLimit()
                                                : std::numeric_limits<uint32_t>::max();

    std::set<uint32_t> preferredSet;
    for (uint32_t neighborId : GetPreferredFailureNeighbors(targetId))
    {
        preferredSet.insert(neighborId);
    }

    std::vector<int32_t> hopDistance(g_uavNodes.GetN(), -1);
    std::queue<uint32_t> bfs;
    hopDistance[targetId] = 0;
    bfs.push(targetId);
    while (!bfs.empty())
    {
        const uint32_t current = bfs.front();
        bfs.pop();
        if (static_cast<uint32_t>(hopDistance[current]) >= hopLimit)
        {
            continue;
        }

        for (uint32_t neighborId : g_state.neighbors[current])
        {
            if (neighborId >= g_uavNodes.GetN() || IsNodeCurrentlyFailed(neighborId))
            {
                continue;
            }
            if (hopDistance[neighborId] >= 0)
            {
                continue;
            }
            hopDistance[neighborId] = hopDistance[current] + 1;
            bfs.push(neighborId);
        }
    }

    struct RankedScopeNode
    {
        uint32_t hop = 0;
        uint32_t preferredRank = 1;
        double distance = 0.0;
        uint32_t nodeId = 0;
    };

    std::vector<RankedScopeNode> ranked;
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i)
    {
        if (i == targetId || IsNodeCurrentlyFailed(i))
        {
            continue;
        }
        if (hopDistance[i] <= 0 || static_cast<uint32_t>(hopDistance[i]) > hopLimit)
        {
            continue;
        }

        ranked.push_back({static_cast<uint32_t>(hopDistance[i]),
                          preferredSet.count(i) ? 0u : 1u,
                          CalculateDistance(g_uavNodes.Get(targetId), g_uavNodes.Get(i)),
                          i});
    }

    std::sort(ranked.begin(),
              ranked.end(),
              [](const RankedScopeNode& lhs, const RankedScopeNode& rhs) {
                  if (lhs.hop != rhs.hop)
                  {
                      return lhs.hop < rhs.hop;
                  }
                  if (lhs.preferredRank != rhs.preferredRank)
                  {
                      return lhs.preferredRank < rhs.preferredRank;
                  }
                  if (std::abs(lhs.distance - rhs.distance) > 1e-6)
                  {
                      return lhs.distance < rhs.distance;
                  }
                  return lhs.nodeId < rhs.nodeId;
              });

    for (const auto& candidate : ranked)
    {
        scope.insert(candidate.nodeId);
        if (scope.size() >= scopeLimit)
        {
            break;
        }
    }
    return scope;
}

std::set<uint32_t> BuildRecoveryScopeNodes()
{
    const auto mode = g_environmentConfig.cooperativeControlConfig.communicationMode;
    if (mode == CommunicationMode::Centralized &&
        g_environmentConfig.cooperativeControlConfig.recoveryPolicy == RecoveryPolicy::GlobalRecovery)
    {
        std::set<uint32_t> scope;
        for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i)
        {
            if (!IsNodeCurrentlyFailed(i))
            {
                scope.insert(i);
            }
        }
        return scope;
    }
    return BuildFailureNeighborhoodNodes(true);
}

FailureNeighborhoodMetrics CollectFailureNeighborhoodMetrics()
{
    FailureNeighborhoodMetrics metrics;
    std::set<uint32_t> scope = BuildFailureNeighborhoodNodes(false);
    if (scope.empty())
    {
        return metrics;
    }

    double pdrSum = 0.0;
    double throughputSumMbps = 0.0;
    double delaySumMs = 0.0;

    for (uint32_t nodeId : scope)
    {
        if (nodeId >= g_uavNodes.GetN() || IsNodeCurrentlyFailed(nodeId))
        {
            continue;
        }

        pdrSum += g_state.nodePDR[nodeId];
        throughputSumMbps += g_state.nodeThroughput[nodeId] / 1e6;
        delaySumMs += g_state.nodeDelay[nodeId] * 1000.0;
        metrics.nodeCount++;
    }

    if (metrics.nodeCount > 0)
    {
        metrics.avgPdr = pdrSum / metrics.nodeCount;
        metrics.avgThroughputMbps = throughputSumMbps / metrics.nodeCount;
        metrics.avgDelayMs = delaySumMs / metrics.nodeCount;
    }
    return metrics;
}

FailureTargetMetrics CollectFailureTargetMetrics()
{
    FailureTargetMetrics metrics;
    const int32_t target = g_environmentConfig.cooperativeControlConfig.failureTargetId;
    if (target < 0 || static_cast<uint32_t>(target) >= g_uavNodes.GetN())
    {
        return metrics;
    }

    metrics.targetNodeId = target;
    metrics.valid = true;
    metrics.failed = IsNodeCurrentlyFailed(static_cast<uint32_t>(target));

    const uint32_t targetId = static_cast<uint32_t>(target);
    if (g_state.nodePDR.count(targetId))
    {
        metrics.pdr = g_state.nodePDR[targetId];
    }
    if (g_state.nodeThroughput.count(targetId))
    {
        metrics.throughputMbps = g_state.nodeThroughput[targetId] / 1e6;
    }
    if (g_state.nodeDelay.count(targetId))
    {
        metrics.delayMs = g_state.nodeDelay[targetId] * 1000.0;
    }
    return metrics;
}

void LocalChannelReallocationForNodes(const std::set<uint32_t>& scope)
{
    for (uint32_t nodeId : scope)
    {
        uint32_t bestChannel = g_state.channelAssignment[nodeId];
        double bestScore = std::numeric_limits<double>::infinity();
        for (uint32_t ch = 0; ch < g_config.numChannels; ++ch)
        {
            double score = 0.0;
            for (uint32_t neighborId : g_state.neighbors[nodeId])
            {
                if (g_state.channelAssignment.count(neighborId) &&
                    g_state.channelAssignment[neighborId] == ch)
                {
                    double dist = CalculateDistance(g_uavNodes.Get(nodeId), g_uavNodes.Get(neighborId));
                    double rxDbm = (g_state.powerAssignment.count(neighborId) ?
                                    g_state.powerAssignment[neighborId] : 20.0) -
                                   CalculatePathLoss(dist);
                    score += std::pow(10.0, rxDbm / 10.0);
                }
            }
            if (score < bestScore)
            {
                bestScore = score;
                bestChannel = ch;
            }
        }
        g_state.channelAssignment[nodeId] = bestChannel;
    }
}

void LocalPowerAdjustmentForNodes(const std::set<uint32_t>& scope)
{
    for (uint32_t nodeId : scope)
    {
        if (g_state.neighbors[nodeId].empty())
        {
            continue;
        }
        double worstSINR = 100.0;
        for (uint32_t neighborId : g_state.neighbors[nodeId])
        {
            worstSINR = std::min(worstSINR, EstimateSINR(nodeId, neighborId));
        }
        double currentRate = g_state.rateAssignment.count(nodeId) ? g_state.rateAssignment[nodeId] : 6.0;
        double targetSINR = RateToMinSINR(currentRate) + 2.0;
        double currentPower = g_state.powerAssignment.count(nodeId) ? g_state.powerAssignment[nodeId] : 20.0;
        double newPower = currentPower;
        if (worstSINR < targetSINR)
        {
            newPower += std::min(2.0, targetSINR - worstSINR);
        }
        else if (worstSINR > targetSINR + 4.0)
        {
            newPower -= 1.0;
        }
        g_state.powerAssignment[nodeId] =
            std::max(g_config.txPowerMin, std::min(g_config.txPowerMax, newPower));
    }
}

void LocalRateAdjustmentForNodes(const std::set<uint32_t>& scope)
{
    for (uint32_t nodeId : scope)
    {
        if (g_state.neighbors[nodeId].empty())
        {
            continue;
        }
        double worstSINR = 100.0;
        double bestSINR = -100.0;
        for (uint32_t neighborId : g_state.neighbors[nodeId])
        {
            double sinr = EstimateSINR(nodeId, neighborId);
            worstSINR = std::min(worstSINR, sinr);
            bestSINR = std::max(bestSINR, sinr);
        }
        double effectiveSINR = 0.8 * worstSINR + 0.2 * bestSINR;
        g_state.rateAssignment[nodeId] = std::max(6.0, SINRToMaxRate(effectiveSINR));
    }
}

void ExecuteCentralizedRecovery(RecoveryPolicy effectivePolicy, const std::string& decisionReason)
{
    std::set<uint32_t> scope;
    if (effectivePolicy == RecoveryPolicy::GlobalRecovery)
    {
        if (g_environmentConfig.cooperativeControlConfig.allowChannelReallocation)
        {
            DynamicChannelAllocation();
        }
        if (g_environmentConfig.cooperativeControlConfig.allowRateAdjustment)
        {
            AdaptiveRateControl();
        }
        if (g_environmentConfig.cooperativeControlConfig.allowPowerAdjustment)
        {
            DynamicPowerControl();
        }
    }
    else
    {
        scope = BuildRecoveryScopeNodes();
        if (g_environmentConfig.cooperativeControlConfig.allowChannelReallocation)
        {
            LocalChannelReallocationForNodes(scope);
        }
        if (g_environmentConfig.cooperativeControlConfig.allowRateAdjustment)
        {
            LocalRateAdjustmentForNodes(scope);
        }
        if (g_environmentConfig.cooperativeControlConfig.allowPowerAdjustment)
        {
            LocalPowerAdjustmentForNodes(scope);
        }
    }

    CooperativeRecoveryAction action;
    action.time = Simulator::Now().GetSeconds();
    action.phase = g_cooperativeRuntime.currentPhase;
    action.communicationMode = "centralized";
    action.recoveryPolicy = g_environmentSummary.recoveryPolicy;
    action.effectiveRecoveryPolicy = RecoveryPolicyToString(effectivePolicy);
    action.actionType = effectivePolicy == RecoveryPolicy::GlobalRecovery
                            ? "centralized_global_control"
                            : "centralized_local_orchestration";
    action.executorNodeId = static_cast<int32_t>(g_cooperativeRuntime.activeLeaderNodeId);
    if (effectivePolicy == RecoveryPolicy::GlobalRecovery)
    {
        action.targetNodeIds = std::to_string(g_environmentSummary.failureTargetId);
    }
    else
    {
        std::ostringstream oss;
        bool first = true;
        for (uint32_t nodeId : scope)
        {
            if (!first)
            {
                oss << ",";
            }
            oss << nodeId;
            first = false;
        }
        action.targetNodeIds = oss.str();
    }
    action.oldValue = effectivePolicy == RecoveryPolicy::GlobalRecovery ? "global_policy_pending" :
                                                                       "local_scope_pending";
    action.newValue = effectivePolicy == RecoveryPolicy::GlobalRecovery ? "global_resources_updated" :
                                                                       "local_resources_updated";
    action.scope = effectivePolicy == RecoveryPolicy::GlobalRecovery ? "network_wide" :
                                                                   "failure_neighborhood";
    action.expectedEffect = effectivePolicy == RecoveryPolicy::GlobalRecovery ?
                                "restore global connectivity and rebalance network" :
                                "repair affected neighborhood without global reconfiguration";
    action.resultState = "executed";
    action.decisionReason = decisionReason;
    AppendCooperativeRecoveryAction(action);
}

void ExecuteDistributedRecovery(RecoveryPolicy effectivePolicy, const std::string& decisionReason)
{
    std::set<uint32_t> scope = BuildRecoveryScopeNodes();
    if (scope.empty())
    {
        return;
    }

    if (g_environmentConfig.cooperativeControlConfig.allowChannelReallocation)
    {
        LocalChannelReallocationForNodes(scope);
    }
    if (g_environmentConfig.cooperativeControlConfig.allowRateAdjustment)
    {
        LocalRateAdjustmentForNodes(scope);
    }
    if (g_environmentConfig.cooperativeControlConfig.allowPowerAdjustment)
    {
        LocalPowerAdjustmentForNodes(scope);
    }

    CooperativeRecoveryAction action;
    action.time = Simulator::Now().GetSeconds();
    action.phase = g_cooperativeRuntime.currentPhase;
    action.communicationMode = "distributed";
    action.recoveryPolicy = g_environmentSummary.recoveryPolicy;
    action.effectiveRecoveryPolicy = RecoveryPolicyToString(effectivePolicy);
    action.actionType = "distributed_local_adjustment";
    action.executorNodeId = -1;
    {
        std::ostringstream oss;
        bool first = true;
        for (uint32_t nodeId : scope)
        {
            if (!first)
            {
                oss << ",";
            }
            oss << nodeId;
            first = false;
        }
        action.targetNodeIds = oss.str();
    }
    action.oldValue = "local_scope_pending";
    action.newValue = "local_neighbor_actions_executed";
    action.scope = "failure_neighborhood";
    action.expectedEffect = "distributed nodes coordinate local repair within hop limit";
    action.resultState = "executed";
    action.decisionReason = decisionReason;
    AppendCooperativeRecoveryAction(action);
}

void ExecuteHybridRecovery(RecoveryPolicy effectivePolicy, const std::string& decisionReason)
{
    std::set<uint32_t> scope = BuildRecoveryScopeNodes();

    if (effectivePolicy == RecoveryPolicy::GlobalRecovery)
    {
        if (g_environmentConfig.cooperativeControlConfig.allowChannelReallocation)
        {
            DynamicChannelAllocation();
        }
        if (g_environmentConfig.cooperativeControlConfig.allowRateAdjustment)
        {
            AdaptiveRateControl();
        }
        if (g_environmentConfig.cooperativeControlConfig.allowPowerAdjustment)
        {
            DynamicPowerControl();
        }
    }
    if (!scope.empty())
    {
        if (g_environmentConfig.cooperativeControlConfig.allowRateAdjustment)
        {
            LocalRateAdjustmentForNodes(scope);
        }
        if (g_environmentConfig.cooperativeControlConfig.allowPowerAdjustment)
        {
            LocalPowerAdjustmentForNodes(scope);
        }
    }

    CooperativeRecoveryAction action;
    action.time = Simulator::Now().GetSeconds();
    action.phase = g_cooperativeRuntime.currentPhase;
    action.communicationMode = "hybrid";
    action.recoveryPolicy = g_environmentSummary.recoveryPolicy;
    action.effectiveRecoveryPolicy = RecoveryPolicyToString(effectivePolicy);
    action.actionType = effectivePolicy == RecoveryPolicy::GlobalRecovery
                            ? "hybrid_leader_global_plus_local_adjustment"
                            : "hybrid_local_orchestration";
    action.executorNodeId = static_cast<int32_t>(g_cooperativeRuntime.activeLeaderNodeId);
    {
        std::ostringstream oss;
        bool first = true;
        for (uint32_t nodeId : scope)
        {
            if (!first)
            {
                oss << ",";
            }
            oss << nodeId;
            first = false;
        }
        action.targetNodeIds = oss.str();
    }
    action.oldValue = effectivePolicy == RecoveryPolicy::GlobalRecovery ? "leader_global_and_local_pending" :
                                                                       "local_scope_pending";
    action.newValue = effectivePolicy == RecoveryPolicy::GlobalRecovery ? "leader_global_plus_local_updated" :
                                                                       "hybrid_local_updated";
    action.scope = effectivePolicy == RecoveryPolicy::GlobalRecovery ? "leader_global_plus_failure_neighborhood" :
                                                                   "failure_neighborhood";
    action.expectedEffect = effectivePolicy == RecoveryPolicy::GlobalRecovery ?
                                "leader rebalances global channels while local nodes recover links" :
                                "leader keeps orchestration while only local repair is applied";
    action.resultState = "executed";
    action.decisionReason = decisionReason;
    AppendCooperativeRecoveryAction(action);
}

} // namespace

double CalculateDistance(Ptr<Node> node1, Ptr<Node> node2) {
    Ptr<MobilityModel> mob1 = node1->GetObject<MobilityModel>();
    Ptr<MobilityModel> mob2 = node2->GetObject<MobilityModel>();
    
    if (!mob1 || !mob2) return 1e9;
    
    Vector pos1 = mob1->GetPosition();
    Vector pos2 = mob2->GetPosition();
    
    double dx = pos1.x - pos2.x;
    double dy = pos1.y - pos2.y;
    double dz = pos1.z - pos2.z;
    
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

void BuildGraphRepresentationForWindow(const std::vector<InferredTopologyEdge>& inferredBatch)
{
    if (inferredBatch.empty())
    {
        return;
    }

    struct NodeAggregate
    {
        uint32_t incidentEdgeCount = 0;
        double weightedDegreeScore = 0.0;
        double probabilitySum = 0.0;
        double confidenceSum = 0.0;
        std::string sceneType;
        std::string operationMode;
        double windowStart = std::numeric_limits<double>::quiet_NaN();
        double windowEnd = std::numeric_limits<double>::quiet_NaN();
    };

    std::map<uint32_t, NodeAggregate> nodeAggregates;
    for (const auto& edge : inferredBatch)
    {
        for (uint32_t nodeId : {edge.srcObservedNodeId, edge.dstObservedNodeId})
        {
            NodeAggregate& agg = nodeAggregates[nodeId];
            agg.incidentEdgeCount++;
            agg.weightedDegreeScore += edge.edgeProbability;
            agg.probabilitySum += edge.edgeProbability;
            agg.confidenceSum += edge.edgeConfidence;
            agg.sceneType = edge.sceneType;
            agg.operationMode = edge.operationMode;
            agg.windowStart = edge.windowStart;
            agg.windowEnd = edge.windowEnd;
        }
    }

    std::vector<KeyNodeCandidate> candidates;
    candidates.reserve(nodeAggregates.size());

    double maxWeightedDegree = 0.0;
    for (const auto& [nodeId, agg] : nodeAggregates)
    {
        (void)nodeId;
        maxWeightedDegree = std::max(maxWeightedDegree, agg.weightedDegreeScore);
    }
    maxWeightedDegree = std::max(1e-9, maxWeightedDegree);

    for (const auto& [nodeId, agg] : nodeAggregates)
    {
        InferredGraphNode node;
        node.windowStart = agg.windowStart;
        node.windowEnd = agg.windowEnd;
        node.observedNodeId = nodeId;
        node.incidentEdgeCount = agg.incidentEdgeCount;
        node.weightedDegreeScore = agg.weightedDegreeScore;
        node.avgIncidentProbability =
            agg.incidentEdgeCount > 0 ? agg.probabilitySum / agg.incidentEdgeCount : 0.0;
        node.avgIncidentConfidence =
            agg.incidentEdgeCount > 0 ? agg.confidenceSum / agg.incidentEdgeCount : 0.0;
        node.sceneType = agg.sceneType;
        node.operationMode = agg.operationMode;
        AppendInferredGraphNode(node);

        KeyNodeCandidate candidate;
        candidate.windowStart = node.windowStart;
        candidate.windowEnd = node.windowEnd;
        candidate.observedNodeId = node.observedNodeId;
        candidate.weightedDegreeScore = node.weightedDegreeScore;
        candidate.avgIncidentProbability = node.avgIncidentProbability;
        candidate.avgIncidentConfidence = node.avgIncidentConfidence;
        const double normalizedDegree = node.weightedDegreeScore / maxWeightedDegree;
        candidate.keyNodeScore =
            std::max(0.0,
                     std::min(1.0, 0.6 * normalizedDegree +
                                       0.25 * node.avgIncidentProbability +
                                       0.15 * node.avgIncidentConfidence));
        candidate.keyNodeMethod = "weighted_degree_gate_v1";
        candidate.sceneType = node.sceneType;
        candidate.operationMode = node.operationMode;
        candidates.push_back(candidate);
    }

    std::sort(candidates.begin(),
              candidates.end(),
              [](const KeyNodeCandidate& a, const KeyNodeCandidate& b) {
                  if (a.keyNodeScore != b.keyNodeScore)
                  {
                      return a.keyNodeScore > b.keyNodeScore;
                  }
                  return a.observedNodeId < b.observedNodeId;
              });

    uint32_t rank = 1;
    for (auto& candidate : candidates)
    {
        candidate.rank = rank++;
        AppendKeyNodeCandidate(candidate);
    }
}

/**
 * \brief 更新拓扑邻接矩阵
 */
void UpdateTopology() {
    ApplyCooperativeFailureState();
    uint32_t n = g_uavNodes.GetN();
    g_state.adjacencyMatrix.clear();
    g_state.adjacencyMatrix.resize(n, std::vector<bool>(n, false));
    g_state.neighbors.clear();
    
    double commRange = 150.0 * g_environmentSummary.connectivityRangeFactor;
    
    for (uint32_t i = 0; i < n; ++i) {
        if (IsNodeCurrentlyFailed(i)) {
            continue;
        }
        for (uint32_t j = i + 1; j < n; ++j) {
            if (IsNodeCurrentlyFailed(j)) {
                continue;
            }
            double dist = CalculateDistance(g_uavNodes.Get(i), g_uavNodes.Get(j));
            double linkRange = commRange * GetCooperativeRangeFactorForLink(i, j);

            if (dist <= linkRange) {
                g_state.adjacencyMatrix[i][j] = true;
                g_state.adjacencyMatrix[j][i] = true;
                g_state.neighbors[i].push_back(j);
                g_state.neighbors[j].push_back(i);
            }
        }
    }
}

// ==================== SINR 计算工具 ====================

/** dBm → 线性功率 (mW) */
inline double dBmToMw(double dBm) {
    return std::pow(10.0, dBm / 10.0);
}

/** 线性功率 (mW) → dBm */
inline double mwToDbm(double mW) {
    return 10.0 * std::log10(std::max(1e-20, mW));
}

/** 路径损耗 (dB)，与物理信道使用相同的指数 */
double CalculatePathLoss(double dist) {
    if (dist < 1.0) dist = 1.0;
    return 46.68 + 10.0 * g_pathLossExponent * std::log10(dist);
}

/**
 * 计算接收端 dstId 处的总干扰功率 (mW)
 *
 * @param dstId       接收节点
 * @param excludeId   排除的发送节点（信号源本身不算干扰）
 * @param channelFilter  -1 = 所有节点都干扰（物理现实：单射频同频）
 *                       >=0 = 只计算该逻辑信道上的节点（多信道规划用）
 */
double CalculateInterference_mW(uint32_t dstId, uint32_t excludeId,
                                int channelFilter) {
    double total_mW = 0.0;

    if (dstId < g_uavNodes.GetN() && IsNodeCurrentlyFailed(dstId))
    {
        return total_mW;
    }
    
    // ---- 计算发送方的载波感知范围 ----
    double senderPower = g_state.powerAssignment.count(excludeId) ?
                         g_state.powerAssignment[excludeId] : 20.0;
    // PathLoss(csRange) = senderPower - rxSensitivity
    // 46.68 + 10α·log10(csRange) = senderPower - rxSensitivity
    // csRange = 10^((senderPower - 46.68 - rxSens) / (10α))
    double csRange = std::pow(10.0, 
        (senderPower - 46.68 - g_config.rxSensitivity) / 
        (10.0 * g_pathLossExponent));
    csRange = std::min(csRange, 2000.0);  // 合理上界
    
    // ---- 来自其他 UAV 的干扰：只计隐藏终端 ----
    for (uint32_t k = 0; k < g_uavNodes.GetN(); ++k) {
        if (k == excludeId || k == dstId) continue;
        if (IsNodeCurrentlyFailed(k)) continue;
        
        if (channelFilter >= 0) {
            auto it = g_state.channelAssignment.find(k);
            if (it != g_state.channelAssignment.end() && 
                (int)it->second != channelFilter) {
                continue;
            }
        }
        
        // ★ CSMA 感知：节点 k 能否听到发送方?
        double distToSender = CalculateDistance(
            g_uavNodes.Get(k), g_uavNodes.Get(excludeId));
        
        if (distToSender <= csRange) {
            // k 能听到发送方 → CSMA 退避 → 不构成干扰
            continue;
        }
        
        // k 是隐藏终端：无法感知发送方，可能同时发送
        double dist = CalculateDistance(g_uavNodes.Get(k), g_uavNodes.Get(dstId));
        double txK  = g_state.powerAssignment.count(k) ? 
                      g_state.powerAssignment[k] : 20.0;
        double rxK  = txK - CalculatePathLoss(dist);
        
        if (rxK > -100.0) {
            total_mW += dBmToMw(rxK);
        }
    }
    
    // ---- 来自黑飞节点的干扰（同样考虑 CSMA）----
    for (uint32_t k = 0; k < g_interferenceNodes.GetN(); ++k) {
        double distToSender = CalculateDistance(
            g_interferenceNodes.Get(k), g_uavNodes.Get(excludeId));
        
        double dist = CalculateDistance(
            g_interferenceNodes.Get(k), g_uavNodes.Get(dstId));
        double rxK  = 30.0 - CalculatePathLoss(dist);
        
        if (rxK > -100.0) {
            if (distToSender <= csRange) {
                // 黑飞在 CSMA 范围内：大部分时候退避，
                // 但高占空比仍导致 ~10% 碰撞概率
                total_mW += dBmToMw(rxK) * 0.1;
            } else {
                // 黑飞是隐藏终端：完全无法感知发送方
                total_mW += dBmToMw(rxK);
            }
        }
    }

    total_mW += GetCooperativeExtraInterferenceMw(dstId);
    
    return total_mW;
}

/**
 * 估算链路 src→dst 的 SINR (dB)
 *
 * @param channelFilter  -1 = 物理现实, >=0 = 假设性信道规划
 */
double EstimateSINR(uint32_t srcId, uint32_t dstId, int channelFilter) {
    if ((srcId < g_uavNodes.GetN() && IsNodeCurrentlyFailed(srcId)) ||
        (dstId < g_uavNodes.GetN() && IsNodeCurrentlyFailed(dstId)))
    {
        return -100.0;
    }
    double dist = CalculateDistance(g_uavNodes.Get(srcId), g_uavNodes.Get(dstId));
    
    // 信号功率
    double txPower     = g_state.powerAssignment.count(srcId) ?
                         g_state.powerAssignment[srcId] : 20.0;
    double rxPower_dBm = txPower - CalculatePathLoss(dist) -
                         GetCooperativeExtraPathLossDb(srcId, dstId);
    double signal_mW   = dBmToMw(rxPower_dBm);
    
    // 热噪声：20MHz 带宽 @ 290K → -95 dBm
    double noise_mW = dBmToMw(-95.0);
    
    // 干扰
    double interference_mW = CalculateInterference_mW(dstId, srcId, channelFilter);
    
    // SINR = S / (I + N)
    double sinr = signal_mW / (interference_mW + noise_mW);
    return 10.0 * std::log10(std::max(1e-10, sinr));
}

/** SINR (dB) → 802.11a 最大可支持速率 (Mbps) */
double SINRToMaxRate(double sinr_dB) {
    // 基于 802.11a OFDM 调制解调门限（含 ~1dB 实现余量）
    if (sinr_dB >= 25.0) return 54.0;   // 64QAM 3/4
    if (sinr_dB >= 22.0) return 48.0;   // 64QAM 2/3
    if (sinr_dB >= 18.0) return 36.0;   // 16QAM 3/4
    if (sinr_dB >= 14.0) return 24.0;   // 16QAM 1/2
    if (sinr_dB >= 11.0) return 18.0;   // QPSK 3/4
    if (sinr_dB >=  9.0) return 12.0;   // QPSK 1/2
    if (sinr_dB >=  8.0) return  9.0;   // BPSK 3/4
    if (sinr_dB >=  6.0) return  6.0;   // BPSK 1/2
    return 0.0;  // 低于最低解调门限
}

/** 速率 (Mbps) → 所需最低 SINR (dB) */
double RateToMinSINR(double rate) {
    if (rate >= 54.0) return 25.0;
    if (rate >= 48.0) return 22.0;
    if (rate >= 36.0) return 18.0;
    if (rate >= 24.0) return 14.0;
    if (rate >= 18.0) return 11.0;
    if (rate >= 12.0) return  9.0;
    if (rate >=  9.0) return  8.0;
    return 6.0;
}

/**
 * 基于 SINR 的链路质量估计
 * 返回 [0, 1]，其中 0 = 无法解调，1 = 可支持最高速率
 */
double EstimateLinkQuality(uint32_t srcId, uint32_t dstId) {
    double dist = CalculateDistance(g_uavNodes.Get(srcId), g_uavNodes.Get(dstId));
    if (IsNodeCurrentlyFailed(srcId) || IsNodeCurrentlyFailed(dstId)) return 0.0;
    if (dist > 150.0 * GetCooperativeRangeFactorForLink(srcId, dstId)) return 0.0;
    
    double sinr = EstimateSINR(srcId, dstId);
    
    // SINR 6dB → quality=0 (最低速率勉强可解)
    // SINR 25dB → quality=1 (可支持 54Mbps)
    double quality = (sinr - 6.0) / (25.0 - 6.0);
    return std::max(0.0, std::min(1.0, quality));
}

/**
 * SINR 驱动信道分配算法
 *
 * 改进：用实际干扰功率 (mW) 代替距离权重评分
 * - 距离权重无法区分"远处大功率"和"近处小功率"
 * - 功率级别评分直接反映物理干扰强度
 */
void DynamicChannelAllocation() {
    NS_LOG_INFO("执行 SINR 驱动信道分配...");
    
    uint32_t n = g_uavNodes.GetN();
    g_state.channelAssignment.clear();
    
    // 按度数降序排列（核心节点优先分配）
    std::vector<std::pair<uint32_t, uint32_t>> nodeDegrees;
    for (uint32_t i = 0; i < n; ++i) {
        nodeDegrees.push_back({(uint32_t)g_state.neighbors[i].size(), i});
    }
    std::sort(nodeDegrees.rbegin(), nodeDegrees.rend());
    
    for (auto& [degree, nodeId] : nodeDegrees) {
        // 对每个候选信道计算干扰功率 (mW)
        std::vector<double> channelInterference(g_config.numChannels, 0.0);
        
        // 统计当前各信道负载
        std::vector<uint32_t> channelLoad(g_config.numChannels, 0);
        for (auto& [nid, ch] : g_state.channelAssignment) {
            channelLoad[ch]++;
        }
        
        for (uint32_t ch = 0; ch < g_config.numChannels; ++ch) {
            
            // ---- 一跳同频干扰（主因素）----
            for (uint32_t neighborId : g_state.neighbors[nodeId]) {
                auto it = g_state.channelAssignment.find(neighborId);
                if (it != g_state.channelAssignment.end() && it->second == ch) {
                    double dist = CalculateDistance(
                        g_uavNodes.Get(nodeId), g_uavNodes.Get(neighborId));
                    double txK = g_state.powerAssignment.count(neighborId) ?
                                 g_state.powerAssignment[neighborId] : 20.0;
                    double rxPower = txK - CalculatePathLoss(dist);
                    // ★ 用实际功率级别评分，而非线性距离权重
                    channelInterference[ch] += dBmToMw(rxPower);
                }
            }
            
            // ---- 两跳隐藏终端干扰（次因素，权重 ×0.3）----
            for (uint32_t neighborId : g_state.neighbors[nodeId]) {
                for (uint32_t twoHop : g_state.neighbors[neighborId]) {
                    if (twoHop == nodeId) continue;
                    auto it = g_state.channelAssignment.find(twoHop);
                    if (it != g_state.channelAssignment.end() && it->second == ch) {
                        double dist = CalculateDistance(
                            g_uavNodes.Get(nodeId), g_uavNodes.Get(twoHop));
                        if (dist < 225.0) {  // 1.5 × commRange
                            double txK = g_state.powerAssignment.count(twoHop) ?
                                         g_state.powerAssignment[twoHop] : 20.0;
                            double rxPower = txK - CalculatePathLoss(dist);
                            channelInterference[ch] += dBmToMw(rxPower) * 0.3;
                        }
                    }
                }
            }
            
            // ---- 负载均衡惩罚（统一量纲：用伪干扰功率）----
            channelInterference[ch] += channelLoad[ch] * dBmToMw(-80.0);
        }
        
        // 选择干扰功率最低的信道
        uint32_t bestChannel = 0;
        double minInterference = channelInterference[0];
        for (uint32_t ch = 1; ch < g_config.numChannels; ++ch) {
            if (channelInterference[ch] < minInterference) {
                minInterference = channelInterference[ch];
                bestChannel = ch;
            }
        }
        
        g_state.channelAssignment[nodeId] = bestChannel;
    }
    
    // 日志
    std::vector<uint32_t> finalLoad(g_config.numChannels, 0);
    for (auto& [nid, ch] : g_state.channelAssignment) finalLoad[ch]++;
    std::string loadStr;
    for (uint32_t ch = 0; ch < g_config.numChannels; ++ch) {
        loadStr += "CH" + std::to_string(ch) + "=" + std::to_string(finalLoad[ch]) + " ";
    }
    NS_LOG_INFO("信道分配完成: " << loadStr);
}

/**
 * SINR 驱动功率控制
 *
 * 核心思路：
 *   1. 计算最差链路 SINR
 *   2. 与当前速率所需 SINR 比较
 *   3. SINR 不足 → 提升功率；SINR 过剩 → 降低功率（减少对邻居干扰）
 *   4. QoS 闭环：PDR 低于目标时额外补偿
 */
void DynamicPowerControl() {
    NS_LOG_INFO("执行 SINR 驱动功率控制...");
    
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        if (g_state.neighbors[i].empty()) {
            g_state.powerAssignment[i] = g_config.txPowerMax;
            continue;
        }
        
        // ---- 1. 计算当前最差链路 SINR ----
        double worstSINR = 100.0;
        for (uint32_t neighborId : g_state.neighbors[i]) {
            double sinr = EstimateSINR(i, neighborId);
            worstSINR = std::min(worstSINR, sinr);
        }
        
        // ---- 2. 目标 SINR = 当前速率的最低门限 + 3dB 余量 ----
        double currentRate = g_state.rateAssignment.count(i) ? 
                             g_state.rateAssignment[i] : 6.0;
        double targetSINR  = RateToMinSINR(currentRate) + 3.0;
        
        // ---- 3. SINR 差距 → 功率调整 ----
        double sinrGap     = targetSINR - worstSINR;
        double currentPower = g_state.powerAssignment.count(i) ? 
                              g_state.powerAssignment[i] : 20.0;
        double newPower     = currentPower;
        
        if (sinrGap > 0) {
            newPower += std::min(3.0, sinrGap);
        } else if (sinrGap < -3.0) {
            newPower -= std::min(3.0, (-sinrGap - 3.0) * 0.5);
        }
        
        // ---- 4. QoS 闭环：PDR 低于目标时额外补偿 ----
        double currentTime = Simulator::Now().GetSeconds();
        if (currentTime > 15.0  // ★ 路由收敛保护期
            && g_state.nodePDR.count(i) && g_state.nodePDR[i] > 0.0
            && g_state.nodePDR[i] < g_config.targetPDR) {
            double pdrGap = g_config.targetPDR - g_state.nodePDR[i];
            newPower += std::min(2.0, pdrGap * 5.0);
        }
        
        // ---- 5. 密度调节：邻居过多且 SINR 有余量时降功率 ----
        if (g_state.neighbors[i].size() > 5 && sinrGap < -3.0) {
            newPower -= std::min(1.0, (g_state.neighbors[i].size() - 5) * 0.3);
        }
        
        // 钳位到合法范围
        newPower = std::max(g_config.txPowerMin, std::min(g_config.txPowerMax, newPower));
        g_state.powerAssignment[i] = newPower;
    }
    
    NS_LOG_INFO("功率控制完成");
}

/**
 * SINR 驱动速率调整
 *
 * 直接用 SINR (dB) 查 802.11a 调制解调表得到最大可支持速率
 * 无需中间的 "链路质量" 抽象，避免二次映射误差
 */
void AdaptiveRateControl() {
    NS_LOG_INFO("执行 SINR 驱动速率调整...");

    double currentTime = Simulator::Now().GetSeconds();
    
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        if (g_state.neighbors[i].empty()) {
            g_state.rateAssignment[i] = g_config.dataRateMin;
            continue;
        }
        
        // ---- 1. 计算最差 / 最佳链路 SINR ----
        double worstSINR = 100.0;
        double bestSINR  = -100.0;
        for (uint32_t neighborId : g_state.neighbors[i]) {
            double sinr = EstimateSINR(i, neighborId);
            worstSINR = std::min(worstSINR, sinr);
            bestSINR  = std::max(bestSINR, sinr);
        }
        
        // ---- 2. 加权 SINR（70% 看最差，30% 看最好）----
        double effectiveSINR = 0.7 * worstSINR + 0.3 * bestSINR;
        
        // ---- 3. QoS 闭环惩罚 ----
        // PDR 低于目标 → 降低 effective SINR → 选更低速率 → 提高帧成功率
        // 前15秒路由收敛期内，不做QoS惩罚
        if (currentTime > 15.0) {
            if (g_state.nodePDR.count(i) && g_state.nodePDR[i] > 0.0
                && g_state.nodePDR[i] < g_config.targetPDR) {
                double penalty = (g_config.targetPDR - g_state.nodePDR[i]) * 20.0;
                effectiveSINR -= std::min(10.0, penalty);
            }
            
            if (g_state.nodeDelay.count(i) 
                && g_state.nodeDelay[i] > g_config.maxEndToEndDelay) {
                // ★ 修改：只在PDR也差时才降速率
                if (g_state.nodePDR.count(i) && g_state.nodePDR[i] < 0.7) {
                    effectiveSINR -= 3.0;
                }
            }
        }
        
        // ---- 4. 直接查表得到最大可支持速率 ----
        double dataRate = SINRToMaxRate(effectiveSINR);
        if (dataRate < 6.0) dataRate = 6.0;  // 最低保底
        
        g_state.rateAssignment[i] = dataRate;
    }
    
    NS_LOG_INFO("速率调整完成");
}


// ==================== 物理层资源下发 (确保算法生效) ====================
void ApplyResourceAssignments() {
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        Ptr<Node> node = g_uavNodes.Get(i);
        Ptr<NetDevice> device = node->GetDevice(0);
        Ptr<WifiNetDevice> wifiDevice = DynamicCast<WifiNetDevice>(device);
        if (!wifiDevice) continue;

        Ptr<WifiPhy> phy = wifiDevice->GetPhy();
        if (phy) {
            // 在单射频 Ad-hoc WiFi 中，所有节点必须在同一物理信道上
            // 信道分配仅作为逻辑标记用于前端可视化和性能分析
            // 
            // uint8_t channelNumber = 36 + g_state.channelAssignment[i] * 4;
            // if (!phy->IsStateTx() && !phy->IsStateRx() && !phy->IsStateSwitching()) {
            //     phy->SetOperatingChannel(WifiPhy::ChannelTuple{channelNumber, 20.0, WIFI_PHY_BAND_5GHZ, 0});
            // }

            // ✅ 保留：功率控制（这个是安全的，不会破坏通信）
            double txPower = g_state.powerAssignment[i];
            phy->SetTxPowerStart(txPower);
            phy->SetTxPowerEnd(txPower);
        }

        // ✅ 保留：速率调整
        Ptr<WifiRemoteStationManager> stationManager = wifiDevice->GetRemoteStationManager();
        if (stationManager) {
            double rate = g_state.rateAssignment[i];
            std::string rateMode;
            if      (rate >= 54.0) rateMode = "OfdmRate54Mbps";
            else if (rate >= 48.0) rateMode = "OfdmRate48Mbps";
            else if (rate >= 36.0) rateMode = "OfdmRate36Mbps";
            else if (rate >= 24.0) rateMode = "OfdmRate24Mbps";
            else if (rate >= 18.0) rateMode = "OfdmRate18Mbps";
            else if (rate >= 12.0) rateMode = "OfdmRate12Mbps";
            else if (rate >= 9.0)  rateMode = "OfdmRate9Mbps";
            else                   rateMode = "OfdmRate6Mbps";
            
            stationManager->SetAttribute("DataMode", StringValue(rateMode));
        }
    }
}

/**
 * 空间TDMA分组算法 (Spatial TDMA via Greedy Graph Coloring)
 *
 * 核心思想：
 *   距离 > 2×commRange 的节点不会互相干扰，可以共享同一时隙
 *   → 将节点分组，同组节点同时发送，不同组节点时分复用
 *   → 分组数越少，帧越短，每节点吞吐量越高
 *
 * 算法：
 *   1. 构建冲突图：距离 < conflictRange 的节点有边
 *   2. 贪心图着色：度数大的节点优先分配颜色
 *   3. 颜色数 = 时隙数 = 分组数
 *
 * 示例：15个节点紧密编队 → ~15组（退化为纯TDMA）
 *        15个节点分散部署 → ~5组（3倍吞吐提升）
 */

void PerformResourceReallocation() {
    double currentTime = Simulator::Now().GetSeconds();
    // NS_LOG_INFO("时间 " << currentTime << "s: 开始资源重分配");
    
    // 1. always Update Topology (10Hz) for accurate monitoring
    UpdateTopology();

    // 2. Control Logic Frequency (2Hz = every 0.5s) to avoid PHY state conflicts
    static int tickCount = 0;
    bool executeLogic = (tickCount % 5 == 0);
    tickCount++;
    
    if (executeLogic) {
        NS_LOG_INFO("时间 " << currentTime << "s: 执行资源重分配逻辑 (2Hz)");
        if (IsCooperativeMode()) {
            CooperativeNetworkStateSnapshot beforeState = CollectCooperativeNetworkState();
            if (!g_cooperativeRuntime.failureActive && !g_cooperativeRuntime.recoveryActive &&
                !g_cooperativeRuntime.stabilizationActive) {
                const FailureNeighborhoodMetrics localBaseline =
                    CollectFailureNeighborhoodMetrics();
                g_cooperativeRuntime.baselineValid = true;
                g_cooperativeRuntime.baselineConnectivity = beforeState.connectivity;
                g_cooperativeRuntime.baselinePdr = beforeState.avgPdr;
                g_cooperativeRuntime.baselineThroughputMbps = beforeState.avgThroughputMbps;
                g_cooperativeRuntime.baselineDelayMs = beforeState.avgDelayMs;
                g_cooperativeRuntime.baselineLocalPdr = localBaseline.avgPdr;
                g_cooperativeRuntime.baselineLocalThroughputMbps =
                    localBaseline.avgThroughputMbps;
                g_cooperativeRuntime.baselineLocalDelayMs = localBaseline.avgDelayMs;
            }
            EffectiveRecoveryDecision recoveryDecision = ResolveEffectiveRecoveryDecision();
            g_cooperativeRuntime.effectiveRecoveryPolicy =
                RecoveryPolicyToString(recoveryDecision.effectivePolicy);
            g_cooperativeRuntime.lastDecisionReason = recoveryDecision.reason;

            bool triggerRecovery = ShouldTriggerRecovery(beforeState);
            if (triggerRecovery) {
                g_cooperativeRuntime.recoveryActive = true;
                g_cooperativeRuntime.currentPhase = "recovery";
                if (g_cooperativeRuntime.lastRecoveryTriggerTime < 0.0) {
                    g_cooperativeRuntime.lastRecoveryTriggerTime = currentTime;
                }
                MarkCooperativeResponseStarted(currentTime);
            } else if (g_cooperativeRuntime.failureActive) {
                g_cooperativeRuntime.currentPhase = "failure";
            } else if (!g_cooperativeRuntime.recoveryActive &&
                       !g_cooperativeRuntime.stabilizationActive) {
                g_cooperativeRuntime.currentPhase = "normal";
            }

            if (g_config.allocationStrategy == "dynamic") {
                if (g_cooperativeRuntime.recoveryActive) {
                    switch (g_environmentConfig.cooperativeControlConfig.communicationMode) {
                    case CommunicationMode::Centralized:
                        ExecuteCentralizedRecovery(recoveryDecision.effectivePolicy,
                                                   recoveryDecision.reason);
                        break;
                    case CommunicationMode::Distributed:
                        ExecuteDistributedRecovery(recoveryDecision.effectivePolicy,
                                                   recoveryDecision.reason);
                        break;
                    case CommunicationMode::Hybrid:
                        ExecuteHybridRecovery(recoveryDecision.effectivePolicy,
                                              recoveryDecision.reason);
                        break;
                    }
                } else {
                    DynamicChannelAllocation();
                    AdaptiveRateControl();
                    DynamicPowerControl();
                }
            } else {
                NS_LOG_INFO("Baseline (static) 模式，保持固定粗放的资源分配");
            }

            ApplyResourceAssignments();

            CooperativeNetworkStateSnapshot afterState = CollectCooperativeNetworkState();
            UpdateCooperativePhaseAndTimers(afterState);
            RecordCooperativeDecisionTrace(afterState);
            RecordCooperativeRecoveryMetrics(afterState);
        } else {
            if (g_config.allocationStrategy == "dynamic") {
                DynamicChannelAllocation();
                AdaptiveRateControl();
                DynamicPowerControl();
            } else {
                NS_LOG_INFO("Baseline (static) 模式，保持固定粗放的资源分配");
            }
            
            // 4.5 物理下发：让所有計算好的参数在此刻真实作用于模拟器
            ApplyResourceAssignments();
        }
    }
    
    // 5. 记录资源分配结果 (所有时刻都记录，保持 10Hz 平滑输出)
    g_resourceLog << currentTime;
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        g_resourceLog << "," << g_state.channelAssignment[i]
                     << "," << g_state.powerAssignment[i]
                     << "," << g_state.rateAssignment[i];
                     
        double interference_mW = CalculateInterference_mW(i, i);
        double interference_dBm = mwToDbm(interference_mW + 1e-20);
        
        double worstSINR = 0.0;
        if (!g_state.neighbors[i].empty()) {
            worstSINR = 100.0;
            for (uint32_t neighborId : g_state.neighbors[i]) {
                worstSINR = std::min(worstSINR, EstimateSINR(i, neighborId));
            }
        }
        
        g_resourceDetailedLog << currentTime << ","
                              << i << ","
                              << g_state.channelAssignment[i] << ","
                              << g_state.powerAssignment[i] << ","
                              << g_state.rateAssignment[i] << ","
                              << g_state.neighbors[i].size() << ","
                              << interference_dBm << ","
                              << worstSINR << "\n";
    }
    g_resourceLog << std::endl;
    
    // 6. 调度下次重分配 (0.1s loop for data logging)
    // Front-end requirement: 10Hz sampling
    double nextInterval = 0.1;
    Simulator::Schedule(Seconds(nextInterval), 
                        &PerformResourceReallocation);
    
    if (executeLogic) {
        NS_LOG_INFO("资源重分配完成");
    }
}
