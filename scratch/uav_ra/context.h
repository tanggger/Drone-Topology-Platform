#ifndef UAV_RA_CONTEXT_H
#define UAV_RA_CONTEXT_H

#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/internet-module.h"
#include "ns3/mobility-module.h"
#include "ns3/wifi-module.h"
#include "ns3/applications-module.h"
#include "ns3/flow-monitor-module.h"
#include "ns3/buildings-module.h"
#include "ns3/olsr-helper.h"
#include "ns3/propagation-module.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace ns3;

enum class OperationMode
{
    Cooperative,
    NonCooperative
};

enum class CommunicationMode
{
    Centralized,
    Distributed,
    Hybrid
};

enum class CooperativeFailureType
{
    NodeFailure,
    EnvironmentDegradation,
    ExternalInterference,
    LinkDegradation
};

enum class RecoveryPolicy
{
    GlobalRecovery,
    LocalRecovery
};

enum class RecoveryObjective
{
    Connectivity,
    Delay,
    Throughput,
    Pdr
};

inline std::string
OperationModeToString(OperationMode mode)
{
    return mode == OperationMode::NonCooperative ? "non_cooperative" : "cooperative";
}

inline bool
TryParseOperationMode(const std::string& value, OperationMode& mode)
{
    if (value == "cooperative")
    {
        mode = OperationMode::Cooperative;
        return true;
    }
    if (value == "non_cooperative")
    {
        mode = OperationMode::NonCooperative;
        return true;
    }
    return false;
}

inline std::string
CommunicationModeToString(CommunicationMode mode)
{
    switch (mode)
    {
    case CommunicationMode::Centralized:
        return "centralized";
    case CommunicationMode::Distributed:
        return "distributed";
    case CommunicationMode::Hybrid:
        return "hybrid";
    }
    return "centralized";
}

inline bool
TryParseCommunicationMode(const std::string& value, CommunicationMode& mode)
{
    if (value == "centralized")
    {
        mode = CommunicationMode::Centralized;
        return true;
    }
    if (value == "distributed")
    {
        mode = CommunicationMode::Distributed;
        return true;
    }
    if (value == "hybrid")
    {
        mode = CommunicationMode::Hybrid;
        return true;
    }
    return false;
}

inline std::string
CooperativeFailureTypeToString(CooperativeFailureType type)
{
    switch (type)
    {
    case CooperativeFailureType::NodeFailure:
        return "node_failure";
    case CooperativeFailureType::EnvironmentDegradation:
        return "environment_degradation";
    case CooperativeFailureType::ExternalInterference:
        return "external_interference";
    case CooperativeFailureType::LinkDegradation:
        return "link_degradation";
    }
    return "node_failure";
}

inline bool
TryParseCooperativeFailureType(const std::string& value, CooperativeFailureType& type)
{
    if (value == "node_failure")
    {
        type = CooperativeFailureType::NodeFailure;
        return true;
    }
    if (value == "environment_degradation")
    {
        type = CooperativeFailureType::EnvironmentDegradation;
        return true;
    }
    if (value == "external_interference")
    {
        type = CooperativeFailureType::ExternalInterference;
        return true;
    }
    if (value == "link_degradation")
    {
        type = CooperativeFailureType::LinkDegradation;
        return true;
    }
    return false;
}

inline std::string
RecoveryPolicyToString(RecoveryPolicy policy)
{
    return policy == RecoveryPolicy::LocalRecovery ? "local_recovery" : "global_recovery";
}

inline bool
TryParseRecoveryPolicy(const std::string& value, RecoveryPolicy& policy)
{
    if (value == "global_recovery")
    {
        policy = RecoveryPolicy::GlobalRecovery;
        return true;
    }
    if (value == "local_recovery")
    {
        policy = RecoveryPolicy::LocalRecovery;
        return true;
    }
    return false;
}

inline std::string
RecoveryObjectiveToString(RecoveryObjective objective)
{
    switch (objective)
    {
    case RecoveryObjective::Connectivity:
        return "connectivity";
    case RecoveryObjective::Delay:
        return "delay";
    case RecoveryObjective::Throughput:
        return "throughput";
    case RecoveryObjective::Pdr:
        return "pdr";
    }
    return "connectivity";
}

inline bool
TryParseRecoveryObjective(const std::string& value, RecoveryObjective& objective)
{
    if (value == "connectivity")
    {
        objective = RecoveryObjective::Connectivity;
        return true;
    }
    if (value == "delay")
    {
        objective = RecoveryObjective::Delay;
        return true;
    }
    if (value == "throughput")
    {
        objective = RecoveryObjective::Throughput;
        return true;
    }
    if (value == "pdr")
    {
        objective = RecoveryObjective::Pdr;
        return true;
    }
    return false;
}

struct TrajectoryPoint
{
    double time;
    uint32_t nodeId;
    double x;
    double y;
    double z;
};

struct DifficultyParams
{
    double rtkNoiseStdDev = 0.0;
    double rtkDriftInterval = 0.0;
    double rtkDriftDuration = 0.0;
    double rtkDriftMagnitude = 0.0;
    bool enableInterference = false;
    uint32_t numInterferenceNodes = 0;
    double interferenceRateMbps = 0.5;
    double interferenceDutyCycle = 0.1;
    double nakagamiM = 0.0;
    uint32_t macMaxRetries = 7;
    double noiseFigure = 7.0;
    double trafficLoadMbps = 0.2;
    std::string levelName = "Easy";
};

struct EnvironmentPreset
{
    std::string sceneType = "open-field";
    std::string baseModel = "RMa-like LogDistance baseline";
    double shadowSigmaDb = 4.0;
    double nlosPenaltyDb = 6.0;
    double vegetationLossDbPerM = 0.0;
    double losBaseProb = 0.85;
    double interferenceFactor = 1.0;
    double connectivityRangeFactor = 1.0;
    bool hasBuildings = false;
    bool hasVegetation = false;
    bool hasWaterSurface = false;
    bool reflectionAware = false;
    double pathLossExponent = 2.5;
};

struct InterferencePreset
{
    bool enableInterference = false;
    uint32_t numInterferenceNodes = 0;
    double interferenceRateMbps = 0.5;
    double interferenceDutyCycle = 0.1;
};

struct ObservationPreset
{
    double rtkNoiseStdDev = 0.0;
    double rtkDriftInterval = 0.0;
    double rtkDriftDuration = 0.0;
    double rtkDriftMagnitude = 0.0;
    bool observationEnabled = false;
    double windowDurationSec = 0.5;
    uint32_t subslotCount = 10;
    double subslotDurationSec = 0.05;
    uint32_t trackCreateWindowCount = 2;
    uint32_t trackDeleteWindowCount = 4;
    double observationRangeM = 220.0;
    double randomDropRate = 0.10;
    double positionNoiseStdDevM = 15.0;
    double powerNoiseStdDevDb = 3.0;
};

struct TrafficPlatformPreset
{
    double trafficLoadMbps = 0.2;
    uint32_t macMaxRetries = 7;
    double noiseFigure = 7.0;
    double rxSensitivity = -90.0;
    double txPower = 23.0;
    double nakagamiM = 0.0;
};

struct CooperativeControlConfig
{
    CommunicationMode communicationMode = CommunicationMode::Centralized;
    uint32_t leaderNodeId = 0;
    std::vector<uint32_t> backupLeaderList;
    uint32_t distributedHopLimit = 1;
    CooperativeFailureType failureType = CooperativeFailureType::NodeFailure;
    int32_t failureTargetId = -1;
    double failureStartTime = -1.0;
    double failureDuration = -1.0;
    RecoveryPolicy recoveryPolicy = RecoveryPolicy::GlobalRecovery;
    RecoveryObjective recoveryObjective = RecoveryObjective::Connectivity;
    double recoveryCooldown = 1.0;
    bool allowChannelReallocation = true;
    bool allowPowerAdjustment = true;
    bool allowRateAdjustment = true;
    bool allowRelayReselection = true;
    bool allowSlotReallocation = true;
    bool allowRouteRebuild = true;
};

struct ScenarioEnvironmentConfig
{
    OperationMode operationMode = OperationMode::Cooperative;
    std::string sceneType = "open-field";
    std::string difficulty = "Easy";
    std::string formationName = "random_walk";
    std::string mapFile;
    bool hasMapGeometry = false;
    bool useBuildingGeometry = false;
    std::string geometryInputMode = "none";
    std::string environmentSource = "scene-base only";

    EnvironmentPreset environmentPreset;
    InterferencePreset interferencePreset;
    ObservationPreset observationPreset;
    TrafficPlatformPreset trafficPlatformPreset;
    CooperativeControlConfig cooperativeControlConfig;
};

struct EnvironmentSummary
{
    std::string operationMode = "cooperative";
    std::string sceneType = "open-field";
    std::string difficulty = "Easy";
    std::string formationName = "random_walk";
    std::string baseModel = "RMa-like LogDistance baseline";
    std::string environmentSource = "scene-base only";
    std::string geometryInputMode = "none";
    std::string effectiveModelSummary = "open-field baseline";
    std::string environmentContributionSummary = "scene-base only";
    bool hasBuildings = false;
    bool hasVegetation = false;
    bool hasWaterSurface = false;
    bool reflectionAware = false;
    double shadowSigmaDb = 4.0;
    double nlosPenaltyDb = 6.0;
    double vegetationLossDbPerM = 0.0;
    double interferenceFactor = 1.0;
    double connectivityRangeFactor = 1.0;
    double pathLossExponent = 2.5;
    double rxSensitivity = -90.0;
    double txPower = 23.0;
    double noiseFigure = 7.0;
    double trafficLoadMbps = 0.2;
    uint32_t numInterferenceNodes = 0;
    bool observationEnabled = false;
    double observationWindowDurationSec = 0.5;
    uint32_t observationSubslotCount = 10;
    double observationSubslotDurationSec = 0.05;
    uint32_t trackCreateWindowCount = 2;
    uint32_t trackDeleteWindowCount = 4;
    double observationRangeM = 220.0;
    double observationRandomDropRate = 0.10;
    double observationPositionNoiseStdDevM = 15.0;
    double observationPowerNoiseStdDevDb = 3.0;
    uint32_t observationObserverCount = 0;
    uint32_t observationTargetObjectCount = 0;
    std::string communicationMode = "centralized";
    uint32_t leaderNodeId = 0;
    std::string backupLeaderList = "";
    uint32_t distributedHopLimit = 1;
    std::string cooperativeFailureType = "node_failure";
    int32_t failureTargetId = -1;
    double failureStartTime = 0.0;
    double failureDuration = 0.0;
    std::string recoveryPolicy = "global_recovery";
    std::string recoveryObjective = "connectivity";
    double recoveryCooldown = 1.0;
    bool allowChannelReallocation = true;
    bool allowPowerAdjustment = true;
    bool allowRateAdjustment = true;
    bool allowRelayReselection = true;
    bool allowSlotReallocation = true;
    bool allowRouteRebuild = true;
    uint32_t buildingFeatureCount = 0;
    uint32_t forestFeatureCount = 0;
    uint32_t waterFeatureCount = 0;
    uint32_t openFieldFeatureCount = 0;
    double maxGeometryHeightM = 0.0;
    double avgBuildingHeightM = 0.0;
    double buildingDensityPerKm2 = 0.0;
    double buildingCoverageRatio = 0.0;
    double avgStreetWidthM = 0.0;
    std::string losDecisionMode = "fallback-losBaseProb";
    double losBlockedPairRatio = 0.0;
    double avgBuildingCrossingsPerPair = 0.0;
    std::string primaryForestDensityClass = "";
    std::string primaryWaterType = "";
    std::string primaryOpenFieldSurfaceType = "";
};

struct SceneOverlayRegion
{
    std::string featureType;
    std::string sceneType;
    std::string name;
    std::string densityClass;
    std::string waterType;
    std::string surfaceType;
    double heightM = 0.0;
    double weight = 1.0;
    std::vector<Vector> points;
};

struct ObservedSignalEvent
{
    double eventTime = std::numeric_limits<double>::quiet_NaN();
    uint32_t observedNodeId = 0;
    double txStartTime = std::numeric_limits<double>::quiet_NaN();
    double txEndTime = std::numeric_limits<double>::quiet_NaN();
    double txDuration = std::numeric_limits<double>::quiet_NaN();
    double avgRxPowerDbm = std::numeric_limits<double>::quiet_NaN();
    uint32_t channelId = 0;
    double centerFrequencyHz = std::numeric_limits<double>::quiet_NaN();
    bool signalDetected = false;
    double coarsePosX = std::numeric_limits<double>::quiet_NaN();
    double coarsePosY = std::numeric_limits<double>::quiet_NaN();
    double positionConfidence = 0.0;
    double signalConfidence = 0.0;
    double overallConfidence = 0.0;
    bool isMissing = false;
    std::string missingReason;
    double noiseLevel = 0.0;
    uint32_t observerId = 0;
    std::string sceneType;
    std::string operationMode;

    // P1 增强观测/预处理字段
    std::string signalSortingGroup;
    std::string nodeSignalAssociation;
    std::string disambiguationStatus;
    double activityPatternScore = 0.0;
};

struct ObservedCommWindow
{
    double windowStart = std::numeric_limits<double>::quiet_NaN();
    double windowEnd = std::numeric_limits<double>::quiet_NaN();
    uint32_t observedNodeId = 0;
    double txStartTime = std::numeric_limits<double>::quiet_NaN();
    double txEndTime = std::numeric_limits<double>::quiet_NaN();
    double txDuration = std::numeric_limits<double>::quiet_NaN();
    double avgRxPowerDbm = std::numeric_limits<double>::quiet_NaN();
    uint32_t channelId = 0;
    double centerFrequencyHz = std::numeric_limits<double>::quiet_NaN();
    bool signalDetected = false;
    std::string stateSequence;
    double activeRatio = 0.0;
    double txCount = std::numeric_limits<double>::quiet_NaN();
    double coarsePosX = std::numeric_limits<double>::quiet_NaN();
    double coarsePosY = std::numeric_limits<double>::quiet_NaN();
    double positionConfidence = 0.0;
    double signalConfidence = 0.0;
    double overallConfidence = 0.0;
    bool isMissing = false;
    std::string missingReason;
    double noiseLevel = 0.0;
    uint32_t observerId = 0;
    std::string sceneType;
    std::string operationMode;

    // P1 增强观测/预处理字段
    std::string signalSortingGroup;
    std::string nodeSignalAssociation;
    std::string disambiguationStatus;
    double activityPatternScore = 0.0;
};

struct ObservedLinkEvidence
{
    double windowStart = std::numeric_limits<double>::quiet_NaN();
    double windowEnd = std::numeric_limits<double>::quiet_NaN();
    uint32_t srcObservedNodeId = 0;
    uint32_t dstObservedNodeId = 0;
    double evidenceStrength = 0.0;
    double commCount = std::numeric_limits<double>::quiet_NaN();
    double commDurationTotal = std::numeric_limits<double>::quiet_NaN();
    double avgRxPowerDbm = std::numeric_limits<double>::quiet_NaN();
    uint32_t channelId = 0;
    double centerFrequencyHz = std::numeric_limits<double>::quiet_NaN();
    uint32_t observerCount = 0;
    double observerAgreementScore = 0.0;
    double edgeObservationConfidence = 0.0;
    bool isMissing = false;
    std::string missingReason;
    double noiseLevel = 0.0;
    std::string sceneType;
    std::string operationMode;
};

struct InferredTopologyEdge
{
    double windowStart = std::numeric_limits<double>::quiet_NaN();
    double windowEnd = std::numeric_limits<double>::quiet_NaN();
    uint32_t srcObservedNodeId = 0;
    uint32_t dstObservedNodeId = 0;
    double edgeProbability = 0.0;
    double edgeConfidence = 0.0;
    std::string inferenceMethod;
    std::string sceneType;
    std::string operationMode;
};

struct InferredGraphNode
{
    double windowStart = std::numeric_limits<double>::quiet_NaN();
    double windowEnd = std::numeric_limits<double>::quiet_NaN();
    uint32_t observedNodeId = 0;
    uint32_t incidentEdgeCount = 0;
    double weightedDegreeScore = 0.0;
    double avgIncidentProbability = 0.0;
    double avgIncidentConfidence = 0.0;
    std::string sceneType;
    std::string operationMode;
};

struct KeyNodeCandidate
{
    double windowStart = std::numeric_limits<double>::quiet_NaN();
    double windowEnd = std::numeric_limits<double>::quiet_NaN();
    uint32_t observedNodeId = 0;
    uint32_t rank = 0;
    double weightedDegreeScore = 0.0;
    double avgIncidentProbability = 0.0;
    double avgIncidentConfidence = 0.0;
    double keyNodeScore = 0.0;
    std::string keyNodeMethod;
    std::string sceneType;
    std::string operationMode;
};

struct ObservedTrackState
{
    uint32_t observerId = 0;
    uint32_t observedNodeId = 0;
    bool isStable = false;
    bool isActive = false;
    uint32_t consecutiveObservedWindows = 0;
    uint32_t consecutiveMissingWindows = 0;
    double firstObservedTime = std::numeric_limits<double>::quiet_NaN();
    double lastObservedTime = std::numeric_limits<double>::quiet_NaN();
    double lastWindowStart = std::numeric_limits<double>::quiet_NaN();
    double lastWindowEnd = std::numeric_limits<double>::quiet_NaN();
    double lastCoarsePosX = std::numeric_limits<double>::quiet_NaN();
    double lastCoarsePosY = std::numeric_limits<double>::quiet_NaN();
    double positionConfidence = 0.0;
    double signalConfidence = 0.0;
    double overallConfidence = 0.0;
    std::string signalSortingGroup;
    std::string nodeSignalAssociation;
    std::string disambiguationStatus;
    double activityPatternScore = 0.0;
};

struct ActiveObservedEventAccumulator
{
    bool active = false;
    bool signalDetected = false;
    bool isMissing = false;
    std::string missingReason;
    double segmentStartTime = std::numeric_limits<double>::quiet_NaN();
    double segmentEndTime = std::numeric_limits<double>::quiet_NaN();
    uint32_t observerId = 0;
    uint32_t targetObjectKey = 0;
    uint32_t observedNodeId = 0;
    uint32_t channelId = 0;
    double centerFrequencyHz = std::numeric_limits<double>::quiet_NaN();
    double avgRxPowerDbmSum = 0.0;
    double coarsePosXSum = 0.0;
    double coarsePosYSum = 0.0;
    double positionConfidenceSum = 0.0;
    double signalConfidenceSum = 0.0;
    double overallConfidenceSum = 0.0;
    double noiseLevelSum = 0.0;
    uint32_t sampleCount = 0;
    std::string sceneType;
    std::string operationMode;
    std::string signalSortingGroup;
    std::string nodeSignalAssociation;
    std::string disambiguationStatus;
    double activityPatternScore = 0.0;
};

struct ObservationWindowActivityAccumulator
{
    bool hadSignalDetected = false;
    bool hadMissingActivity = false;
    std::string missingReason;
    uint32_t detectedSampleCount = 0;
    uint32_t missingSampleCount = 0;
    double avgRxPowerDbmSum = 0.0;
    double coarsePosXSum = 0.0;
    double coarsePosYSum = 0.0;
    double positionConfidenceSum = 0.0;
    double signalConfidenceSum = 0.0;
    double overallConfidenceSum = 0.0;
    double firstDetectedTxStart = std::numeric_limits<double>::quiet_NaN();
    double lastDetectedTxEnd = std::numeric_limits<double>::quiet_NaN();
    double detectedDurationTotal = 0.0;
    uint32_t detectedTxCount = 0;
    bool activeDetectedSegmentOpen = false;
    double activeDetectedSegmentStart = std::numeric_limits<double>::quiet_NaN();
    std::vector<uint8_t> activeSubslots;
    double lastObservedTime = std::numeric_limits<double>::quiet_NaN();
    std::string signalSortingGroup;
    std::string nodeSignalAssociation;
    std::string disambiguationStatus;
    double activityPatternScore = 0.0;
};

struct ObservationRuntimeState
{
    std::vector<ObservedSignalEvent> signalEvents;
    std::vector<ObservedCommWindow> commWindows;
    std::vector<ObservedLinkEvidence> linkEvidence;
    std::vector<InferredTopologyEdge> inferredEdges;
    std::vector<InferredGraphNode> inferredGraphNodes;
    std::vector<KeyNodeCandidate> keyNodeCandidates;
    std::vector<uint32_t> observerIds;
    std::set<uint32_t> observerIdSet;
    std::set<uint32_t> targetObjectKeys;
    std::map<uint32_t, uint32_t> observedTrackIdByTargetObject;
    std::map<uint32_t, uint32_t> targetObjectByObservedTrackId;
    std::map<uint32_t, std::map<uint32_t, ObservedTrackState>> trackStatesByObserver;
    std::map<std::pair<uint32_t, uint32_t>, ObservationWindowActivityAccumulator>
        windowActivityByObserverTarget;
    std::map<std::pair<uint32_t, uint32_t>, ActiveObservedEventAccumulator>
        activeEventAccumulators;
    uint32_t nextObservedTrackId = 200000;
};

struct CooperativeFailureEvent
{
    double time = std::numeric_limits<double>::quiet_NaN();
    std::string failureType;
    int32_t targetNodeId = -1;
    std::string targetRole = "follower";
    bool isLeaderTarget = false;
    std::string failureState;
    uint32_t affectedNeighborCount = 0;
    uint32_t affectedLinkCount = 0;
    std::string effectSummary;
    std::string source = "simulation";
};

struct CooperativeRecoveryAction
{
    double time = std::numeric_limits<double>::quiet_NaN();
    std::string phase = "normal";
    std::string communicationMode;
    std::string recoveryPolicy;
    std::string effectiveRecoveryPolicy;
    std::string actionType;
    int32_t executorNodeId = -1;
    std::string targetNodeIds;
    std::string oldValue;
    std::string newValue;
    std::string scope;
    std::string expectedEffect;
    std::string resultState;
    std::string decisionReason;
};

struct CooperativeRecoveryMetrics
{
    double time = std::numeric_limits<double>::quiet_NaN();
    std::string phase = "normal";
    double connectivity = 0.0;
    double avgDegree = 0.0;
    double pdr = 0.0;
    double throughputMbps = 0.0;
    double delayMs = 0.0;
    double p99DelayMs = 0.0;
    double failureNeighborhoodPdr = std::numeric_limits<double>::quiet_NaN();
    double failureNeighborhoodThroughputMbps = std::numeric_limits<double>::quiet_NaN();
    double failureNeighborhoodDelayMs = std::numeric_limits<double>::quiet_NaN();
    uint32_t failureNeighborhoodNodeCount = 0;
    int32_t failureTargetId = -1;
    bool isFailureTargetFailed = false;
    double failureTargetPdr = std::numeric_limits<double>::quiet_NaN();
    double failureTargetThroughputMbps = std::numeric_limits<double>::quiet_NaN();
    double failureTargetDelayMs = std::numeric_limits<double>::quiet_NaN();
    uint32_t activeNodeCount = 0;
    uint32_t leaderNodeId = 0;
    bool isLeaderAlive = true;
    double responseTimeSec = std::numeric_limits<double>::quiet_NaN();
    double recoveryTimeSec = std::numeric_limits<double>::quiet_NaN();
    double stabilizationTimeSec = std::numeric_limits<double>::quiet_NaN();
};

struct CooperativeRuntimeState
{
    uint32_t activeLeaderNodeId = 0;
    int32_t activeBackupLeaderNodeId = -1;
    bool failureActive = false;
    bool lastFailureActiveState = false;
    bool recoveryActive = false;
    bool stabilizationActive = false;
    bool leaderTransitionActive = false;
    bool leaderAlive = true;
    std::string currentPhase = "normal";
    std::string effectiveRecoveryPolicy = "global_recovery";
    std::string lastDecisionReason = "policy_not_evaluated";
    double currentFailureActivationTime = -1.0;
    double lastRecoveryTriggerTime = -1.0;
    double lastRecoveryActionTime = -1.0;
    double leaderFailureDetectedAt = -1.0;
    double leaderSwitchCompletedAt = -1.0;
    bool baselineValid = false;
    double baselineConnectivity = std::numeric_limits<double>::quiet_NaN();
    double baselinePdr = std::numeric_limits<double>::quiet_NaN();
    double baselineThroughputMbps = std::numeric_limits<double>::quiet_NaN();
    double baselineDelayMs = std::numeric_limits<double>::quiet_NaN();
    double baselineLocalPdr = std::numeric_limits<double>::quiet_NaN();
    double baselineLocalThroughputMbps = std::numeric_limits<double>::quiet_NaN();
    double baselineLocalDelayMs = std::numeric_limits<double>::quiet_NaN();
    double responseTimeSec = std::numeric_limits<double>::quiet_NaN();
    double recoveryTimeSec = std::numeric_limits<double>::quiet_NaN();
    double stabilizationTimeSec = std::numeric_limits<double>::quiet_NaN();
    double recoveryCompletedAt = -1.0;
    double stabilizationCompletedAt = -1.0;
    std::string lastLeaderSwitchReason = "no_switch";
    std::set<uint32_t> effectiveCooperativeNodes;
    std::vector<CooperativeFailureEvent> failureEvents;
    std::vector<CooperativeRecoveryAction> recoveryActions;
    std::vector<CooperativeRecoveryMetrics> metricsHistory;
};

struct ResourceAllocationConfig
{
    double duration = 200.0;
    uint32_t numUAVs = 15;

    uint32_t numChannels = 3;
    double txPowerMin = 10.0;
    double txPowerMax = 30.0;
    double dataRateMin = 6.0;
    double dataRateMax = 54.0;
    double rxSensitivity = -90.0;

    double targetPDR = 0.85;
    double maxEndToEndDelay = 0.100;
    double minThroughput = 500000.0;

    std::string allocationStrategy = "dynamic";
    double reallocationInterval = 5.0;

    double areaSize = 500.0;
    double minX = 0.0;
    double maxX = 500.0;
    double minY = 0.0;
    double maxY = 500.0;
    double uavHeight = 50.0;
    double maxSpeed = 20.0;

    std::string trafficPattern = "mixed";
    double packetSize = 1024;
    double packetRate = 100.0;

    std::string outputDir = "output/resource_allocation";
    bool enableVisualization = false;
    bool enableTDMA = true;
};

struct ResourceAllocationState
{
    std::map<uint32_t, uint32_t> channelAssignment;
    std::map<uint32_t, double> powerAssignment;
    std::map<uint32_t, double> rateAssignment;
    std::map<std::pair<uint32_t, uint32_t>, double> linkQuality;
    std::map<uint32_t, double> nodePDR;
    std::map<uint32_t, double> nodeDelay;
    std::map<uint32_t, double> nodeThroughput;
    std::vector<std::vector<bool>> adjacencyMatrix;
    std::map<uint32_t, std::vector<uint32_t>> neighbors;
};

struct TDMAManager
{
    bool enabled = false;

    double slotDuration = 0.010;
    double guardTime = 0.001;
    double cycleDuration = 0.150;
    uint32_t numGroups = 1;
    double trafficStartTime = 3.0;

    std::map<uint32_t, uint32_t> slotAssignment;

    struct FlowEntry
    {
        uint32_t dstId;
        Ptr<Socket> socket;
    };
    std::map<uint32_t, std::vector<FlowEntry>> nodeFlows;

    uint32_t basePacketsPerSlot = 4;
    uint32_t minPacketsPerSlot = 2;
    uint32_t maxPacketsPerSlot = 10;
    uint32_t bonusPktsPerSlot = 2;

    std::map<uint32_t, uint32_t> perNodePackets;
    std::map<uint32_t, std::vector<uint32_t>> bonusSlots;
    std::map<uint32_t, double> urgency;
    std::vector<std::vector<bool>> conflictMatrix;
    std::vector<std::vector<uint32_t>> slotOccupants;

    double reallocationInterval = 5.0;
    uint32_t lastLinkCount = 0;

    uint32_t reallocationCount = 0;
    uint32_t recoloringCount = 0;
};

extern std::vector<TrajectoryPoint> g_trajectoryData;
extern std::map<uint32_t, std::vector<TrajectoryPoint>> g_nodeTrajectories;
extern double g_trajectoryEndTime;
extern DifficultyParams g_diffParams;
extern ScenarioEnvironmentConfig g_environmentConfig;
extern EnvironmentSummary g_environmentSummary;
extern std::vector<SceneOverlayRegion> g_forestRegions;
extern std::vector<SceneOverlayRegion> g_waterRegions;
extern std::vector<SceneOverlayRegion> g_openFieldRegions;
extern ObservationRuntimeState g_observationRuntime;
extern CooperativeRuntimeState g_cooperativeRuntime;
extern Ptr<UniformRandomVariable> g_randVar;
extern double g_pathLossExponent;

extern ResourceAllocationConfig g_config;
extern ResourceAllocationState g_state;
extern TDMAManager g_tdma;
extern std::ofstream g_tdmaLog;

extern NodeContainer g_uavNodes;
extern NodeContainer g_interferenceNodes;
extern std::map<uint32_t, Ptr<Application>> g_applications;
extern Ptr<FlowMonitor> g_flowMonitor;
extern FlowMonitorHelper g_flowHelper;

extern std::ofstream g_resourceLog;
extern std::ofstream g_qosLog;
extern std::ofstream g_topologyLog;
extern std::ofstream g_topologyEvolutionLog;
extern std::ofstream g_topologyDetailedLog;
extern std::ofstream g_resourceDetailedLog;
extern std::ofstream g_posLog;
extern std::ofstream g_topoChangesLog;
extern std::ofstream g_transLog;
extern std::ofstream g_observedSignalEventsLog;
extern std::ofstream g_observedCommWindowsLog;
extern std::ofstream g_observedLinkEvidenceLog;
extern std::ofstream g_inferredTopologyEdgesLog;
extern std::ofstream g_inferredGraphNodesLog;
extern std::ofstream g_keyNodeCandidatesLog;
extern std::ofstream g_cooperativeFailureEventsLog;
extern std::ofstream g_cooperativeRecoveryActionsLog;
extern std::ofstream g_cooperativeRecoveryMetricsLog;
extern std::ofstream g_cooperativeDecisionTraceLog;

Vector ApplyRTKNoise(const Vector& originalPos, double time);
bool LoadFormationTrajectory(const std::string& filename);
void SetupFormationMobility(NodeContainer& nodes);
void CreateInterferenceNodes(Ptr<YansWifiChannel> channel);
bool LoadSceneGeometryFromMap(const std::string& mapFile);
void RefreshUrbanLosSummary();
bool IsObservationPathOccluded(const Vector& observerPos, const Vector& targetPos);

void LogPositions();

double CalculateDistance(Ptr<Node> node1, Ptr<Node> node2);
void UpdateTopology();
double CalculatePathLoss(double dist);
double CalculateInterference_mW(uint32_t dstId, uint32_t excludeId,
                                int channelFilter = -1);
double EstimateSINR(uint32_t srcId, uint32_t dstId, int channelFilter = -1);
double SINRToMaxRate(double sinr_dB);
double RateToMinSINR(double rateMbps);
double EstimateLinkQuality(uint32_t srcId, uint32_t dstId);
void DynamicChannelAllocation();
void DynamicPowerControl();
void AdaptiveRateControl();
void ApplyResourceAssignments();
void PerformResourceReallocation();

void ComputeTDMASlots();
double ComputeQoSUrgency(uint32_t nodeId);
bool IsSlotCompatible(uint32_t nodeId, uint32_t slotId);
void DynamicTDMAReallocation();
void SendTDMAPacket(uint32_t nodeId, uint32_t flowIdx);
void TDMABonusBurst(uint32_t nodeId, uint32_t bonusSlotId);
void TDMABurstSend(uint32_t nodeId);
void MonitorQoSPerformance();
void LogTopologyChange();
void InstallUdpApplication(Ptr<Node> srcNode, Ptr<Node> dstNode, uint16_t port,
                           double startTime, double stopTime);
void SetupMixedTraffic();
void SetupTDMATraffic();
void InitializeObservationNamespaces();
bool ObservationModeEnabled();
bool IsObserverId(uint32_t observerId);
bool IsTargetObjectKey(uint32_t targetObjectKey);
uint32_t AllocateObservedTrackIdForTarget(uint32_t targetObjectKey);
bool TryGetObservedTrackIdForTarget(uint32_t targetObjectKey, uint32_t& observedNodeId);
uint32_t GetTargetObjectKeyFromInterferenceIndex(uint32_t interferenceIndex);
void MonitorObservedSignalEvents();
void UpdateObservedTrackStates();
void FinalizeObservedSignalEvents();
void BuildObservedLinkEvidenceForWindow(
    const std::vector<ObservedCommWindow>& windowBatch);
void BuildInferredTopologyEdgesForWindow(
    const std::vector<ObservedCommWindow>& windowBatch,
    const std::vector<ObservedLinkEvidence>& evidenceBatch);
void BuildGraphRepresentationForWindow(
    const std::vector<InferredTopologyEdge>& inferredBatch);
void SetupSimulationInfrastructure(bool useFormation,
                                   OperationMode operationMode,
                                   const std::string& sceneType,
                                   const std::string& difficulty,
                                   const std::string& formationName,
                                   const std::string& mapFile,
                                   const CooperativeControlConfig& cooperativeConfig,
                                   double customPathLossExp,
                                   double customRxSensitivity,
                                   double customTxPower);
void InitializeOutputFiles();
void WriteEnvironmentSummaryFile();
void FinalizeSimulationOutputs();

#endif
