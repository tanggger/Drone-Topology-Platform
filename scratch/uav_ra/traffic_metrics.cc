#include "context.h"

NS_LOG_COMPONENT_DEFINE("UAVResourceAllocationTraffic");

namespace
{

constexpr uint32_t kObservedTrackIdBase = 200000;
constexpr uint32_t kInterferenceTargetObjectBase = 1000;
constexpr double kObservationEventPollIntervalSec = 0.1;
constexpr double kTargetTxStartTimeSec = 1.0;
constexpr uint32_t kObservedChannelId = 0;
constexpr double kObservedCenterFrequencyHz = 5.18e9;

double Clamp01(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

double GetInterferenceDutyCycle()
{
    return std::max(0.01, std::min(0.99, g_diffParams.interferenceDutyCycle));
}

bool IsInterferenceNodeActiveAtTime(double currentTime)
{
    if (currentTime < kTargetTxStartTimeSec || currentTime >= g_config.duration)
    {
        return false;
    }

    double onTime = GetInterferenceDutyCycle();
    double cycle = 1.0;
    double phase = std::fmod(currentTime - kTargetTxStartTimeSec, cycle);
    if (phase < 0.0)
    {
        phase += cycle;
    }
    return phase < onTime;
}

bool IsCooperativeModeActive()
{
    return g_environmentConfig.operationMode == OperationMode::Cooperative;
}

bool IsFailureWindowActiveForTraffic(double now)
{
    if (!IsCooperativeModeActive())
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

bool IsTrafficNodeCurrentlyFailed(uint32_t nodeId)
{
    if (!IsFailureWindowActiveForTraffic(Simulator::Now().GetSeconds()))
    {
        return false;
    }

    const auto& coop = g_environmentConfig.cooperativeControlConfig;
    return coop.failureType == CooperativeFailureType::NodeFailure &&
           coop.failureTargetId >= 0 &&
           nodeId == static_cast<uint32_t>(coop.failureTargetId);
}

Ptr<NormalRandomVariable> GetSharedNormalRand()
{
    static Ptr<NormalRandomVariable> normal = CreateObject<NormalRandomVariable>();
    return normal;
}

double SampleGaussian(double stddev)
{
    if (stddev <= 0.0)
    {
        return 0.0;
    }

    Ptr<NormalRandomVariable> normal = GetSharedNormalRand();
    normal->SetAttribute("Mean", DoubleValue(0.0));
    normal->SetAttribute("Variance", DoubleValue(stddev * stddev));
    return normal->GetValue();
}

double ComputeObservedRxPowerDbm(const Vector& observerPos, const Vector& targetPos)
{
    double dx = observerPos.x - targetPos.x;
    double dy = observerPos.y - targetPos.y;
    double dz = observerPos.z - targetPos.z;
    double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    double txPowerDbm = 30.0;
    return txPowerDbm - CalculatePathLoss(std::max(1.0, dist));
}

void AppendObservedCommWindow(const ObservedCommWindow& window)
{
    g_observationRuntime.commWindows.push_back(window);
    if (!g_observedCommWindowsLog.is_open())
    {
        return;
    }

    auto writeOrNaN = [&](double value) {
        if (std::isnan(value))
        {
            g_observedCommWindowsLog << "nan";
        }
        else
        {
            g_observedCommWindowsLog << value;
        }
    };

    writeOrNaN(window.windowStart);
    g_observedCommWindowsLog << ",";
    writeOrNaN(window.windowEnd);
    g_observedCommWindowsLog << "," << window.observedNodeId << ",";
    writeOrNaN(window.txStartTime);
    g_observedCommWindowsLog << ",";
    writeOrNaN(window.txEndTime);
    g_observedCommWindowsLog << ",";
    writeOrNaN(window.txDuration);
    g_observedCommWindowsLog << ",";
    writeOrNaN(window.avgRxPowerDbm);
    g_observedCommWindowsLog << "," << window.channelId << ",";
    writeOrNaN(window.centerFrequencyHz);
    g_observedCommWindowsLog << "," << (window.signalDetected ? 1 : 0) << ","
                             << window.stateSequence << "," << window.activeRatio << ",";
    writeOrNaN(window.txCount);
    g_observedCommWindowsLog << ",";
    writeOrNaN(window.coarsePosX);
    g_observedCommWindowsLog << ",";
    writeOrNaN(window.coarsePosY);
    g_observedCommWindowsLog << "," << window.positionConfidence << ","
                             << window.signalConfidence << "," << window.overallConfidence
                             << "," << (window.isMissing ? 1 : 0) << ","
                             << window.missingReason << "," << window.noiseLevel << ","
                             << window.observerId << "," << window.sceneType << ","
                             << window.operationMode << "\n";
}

void AppendObservedSignalEvent(const ObservedSignalEvent& event)
{
    g_observationRuntime.signalEvents.push_back(event);
    if (!g_observedSignalEventsLog.is_open())
    {
        return;
    }

    auto writeOrNaN = [&](double value) {
        if (std::isnan(value))
        {
            g_observedSignalEventsLog << "nan";
        }
        else
        {
            g_observedSignalEventsLog << value;
        }
    };

    writeOrNaN(event.eventTime);
    g_observedSignalEventsLog << "," << event.observedNodeId << ",";
    writeOrNaN(event.txStartTime);
    g_observedSignalEventsLog << ",";
    writeOrNaN(event.txEndTime);
    g_observedSignalEventsLog << ",";
    writeOrNaN(event.txDuration);
    g_observedSignalEventsLog << ",";
    writeOrNaN(event.avgRxPowerDbm);
    g_observedSignalEventsLog << "," << event.channelId << ",";
    writeOrNaN(event.centerFrequencyHz);
    g_observedSignalEventsLog << "," << (event.signalDetected ? 1 : 0) << ",";
    writeOrNaN(event.coarsePosX);
    g_observedSignalEventsLog << ",";
    writeOrNaN(event.coarsePosY);
    g_observedSignalEventsLog << "," << event.positionConfidence << ","
                              << event.signalConfidence << "," << event.overallConfidence
                              << "," << (event.isMissing ? 1 : 0) << ","
                              << event.missingReason << "," << event.noiseLevel << ","
                              << event.observerId << "," << event.sceneType << ","
                              << event.operationMode << "\n";
}

void FlushActiveObservedEvent(const std::pair<uint32_t, uint32_t>& key)
{
    auto it = g_observationRuntime.activeEventAccumulators.find(key);
    if (it == g_observationRuntime.activeEventAccumulators.end())
    {
        return;
    }

    const ActiveObservedEventAccumulator& acc = it->second;
    if (!acc.active)
    {
        g_observationRuntime.activeEventAccumulators.erase(it);
        return;
    }

    ObservedSignalEvent event;
    event.eventTime = acc.segmentStartTime;
    event.observedNodeId = acc.observedNodeId;
    event.observerId = acc.observerId;
    event.channelId = acc.channelId;
    event.centerFrequencyHz = acc.centerFrequencyHz;
    event.signalDetected = acc.signalDetected;
    event.isMissing = acc.isMissing;
    event.missingReason = acc.missingReason;
    event.sceneType = acc.sceneType;
    event.operationMode = acc.operationMode;
    event.signalSortingGroup = acc.signalSortingGroup;
    event.nodeSignalAssociation = acc.nodeSignalAssociation;
    event.disambiguationStatus = acc.disambiguationStatus;
    event.activityPatternScore = acc.activityPatternScore;

    if (acc.signalDetected && acc.sampleCount > 0)
    {
        event.txStartTime = acc.segmentStartTime;
        event.txEndTime = acc.segmentEndTime;
        event.txDuration = std::max(0.0, acc.segmentEndTime - acc.segmentStartTime);
        event.avgRxPowerDbm = acc.avgRxPowerDbmSum / acc.sampleCount;
        event.coarsePosX = acc.coarsePosXSum / acc.sampleCount;
        event.coarsePosY = acc.coarsePosYSum / acc.sampleCount;
    }
    else
    {
        event.txStartTime = std::numeric_limits<double>::quiet_NaN();
        event.txEndTime = std::numeric_limits<double>::quiet_NaN();
        event.txDuration = std::numeric_limits<double>::quiet_NaN();
        event.avgRxPowerDbm = std::numeric_limits<double>::quiet_NaN();
        event.coarsePosX = std::numeric_limits<double>::quiet_NaN();
        event.coarsePosY = std::numeric_limits<double>::quiet_NaN();
    }

    if (acc.sampleCount > 0)
    {
        event.positionConfidence = acc.positionConfidenceSum / acc.sampleCount;
        event.signalConfidence = acc.signalConfidenceSum / acc.sampleCount;
        event.overallConfidence = acc.overallConfidenceSum / acc.sampleCount;
        event.noiseLevel = acc.noiseLevelSum / acc.sampleCount;
    }

    AppendObservedSignalEvent(event);
    g_observationRuntime.activeEventAccumulators.erase(it);
}

void FlushAllActiveObservedEvents()
{
    std::vector<std::pair<uint32_t, uint32_t>> keys;
    keys.reserve(g_observationRuntime.activeEventAccumulators.size());
    for (const auto& [key, _] : g_observationRuntime.activeEventAccumulators)
    {
        keys.push_back(key);
    }
    for (const auto& key : keys)
    {
        FlushActiveObservedEvent(key);
    }
}

void UpdateWindowActivityAccumulator(uint32_t observerId,
                                     uint32_t targetObjectKey,
                                     bool signalDetected,
                                     const std::string& missingReason,
                                     double intervalStart,
                                     double intervalEnd,
                                     double observedPosX,
                                     double observedPosY,
                                     double avgRxPowerDbm,
                                     double positionConfidence,
                                     double signalConfidence,
                                     double overallConfidence,
                                     double currentTime)
{
    auto& acc =
        g_observationRuntime.windowActivityByObserverTarget[{observerId, targetObjectKey}];
    const uint32_t subslotCount =
        g_environmentConfig.observationPreset.subslotCount;
    const double subslotDuration =
        g_environmentConfig.observationPreset.subslotDurationSec;
    const double windowDuration =
        g_environmentConfig.observationPreset.windowDurationSec;
    const double windowStart =
        std::max(0.0, std::floor(currentTime / windowDuration) * windowDuration);
    if (acc.activeSubslots.size() != subslotCount)
    {
        acc.activeSubslots.assign(subslotCount, 0);
    }
    acc.signalSortingGroup = "grp_target_" + std::to_string(targetObjectKey);
    acc.activityPatternScore = GetInterferenceDutyCycle();

    if (signalDetected)
    {
        acc.hadSignalDetected = true;
        acc.detectedSampleCount++;
        acc.avgRxPowerDbmSum += avgRxPowerDbm;
        acc.coarsePosXSum += observedPosX;
        acc.coarsePosYSum += observedPosY;
        acc.positionConfidenceSum += positionConfidence;
        acc.signalConfidenceSum += signalConfidence;
        acc.overallConfidenceSum += overallConfidence;
        acc.lastObservedTime = currentTime;
        acc.nodeSignalAssociation = "associated";
        acc.disambiguationStatus = "stable";

        if (!acc.activeDetectedSegmentOpen)
        {
            acc.activeDetectedSegmentOpen = true;
            acc.activeDetectedSegmentStart = intervalStart;
            if (std::isnan(acc.firstDetectedTxStart))
            {
                acc.firstDetectedTxStart = intervalStart;
            }
        }

        for (uint32_t subslotIdx = 0; subslotIdx < subslotCount; ++subslotIdx)
        {
            double subslotStart = windowStart + subslotIdx * subslotDuration;
            double subslotEnd = subslotStart + subslotDuration;
            bool overlaps = intervalStart < subslotEnd && intervalEnd > subslotStart;
            if (overlaps)
            {
                acc.activeSubslots[subslotIdx] = 1;
            }
        }
    }
    else
    {
        acc.hadMissingActivity = true;
        acc.missingSampleCount++;
        if (acc.missingReason.empty())
        {
            acc.missingReason = missingReason;
        }
        if (acc.nodeSignalAssociation.empty())
        {
            acc.nodeSignalAssociation = "unobserved";
        }
        if (acc.disambiguationStatus.empty())
        {
            acc.disambiguationStatus = "uncertain";
        }

        if (acc.activeDetectedSegmentOpen)
        {
            acc.detectedTxCount++;
            acc.lastDetectedTxEnd = intervalStart;
            acc.detectedDurationTotal +=
                std::max(0.0, intervalStart - acc.activeDetectedSegmentStart);
            acc.activeDetectedSegmentOpen = false;
            acc.activeDetectedSegmentStart = std::numeric_limits<double>::quiet_NaN();
        }
    }
}

void ResetUnstableTrackState(ObservedTrackState& state)
{
    state.isStable = false;
    state.isActive = false;
    state.consecutiveObservedWindows = 0;
    state.consecutiveMissingWindows = 0;
    state.firstObservedTime = std::numeric_limits<double>::quiet_NaN();
    state.lastObservedTime = std::numeric_limits<double>::quiet_NaN();
    state.lastWindowStart = std::numeric_limits<double>::quiet_NaN();
    state.lastWindowEnd = std::numeric_limits<double>::quiet_NaN();
    state.lastCoarsePosX = std::numeric_limits<double>::quiet_NaN();
    state.lastCoarsePosY = std::numeric_limits<double>::quiet_NaN();
    state.positionConfidence = 0.0;
    state.signalConfidence = 0.0;
    state.overallConfidence = 0.0;
    state.signalSortingGroup.clear();
    state.nodeSignalAssociation.clear();
    state.disambiguationStatus.clear();
    state.activityPatternScore = 0.0;
}

std::string BuildStateSequence(const ObservationWindowActivityAccumulator& activity)
{
    std::string stateSequence;
    stateSequence.reserve(activity.activeSubslots.size());
    for (uint8_t bit : activity.activeSubslots)
    {
        stateSequence.push_back(bit ? '1' : '0');
    }
    return stateSequence;
}

} // namespace

bool ObservationModeEnabled()
{
    return g_environmentConfig.operationMode == OperationMode::NonCooperative &&
           g_environmentConfig.observationPreset.observationEnabled;
}

uint32_t GetTargetObjectKeyFromInterferenceIndex(uint32_t interferenceIndex)
{
    return kInterferenceTargetObjectBase + interferenceIndex;
}

bool IsObserverId(uint32_t observerId)
{
    return g_observationRuntime.observerIdSet.count(observerId) > 0;
}

bool IsTargetObjectKey(uint32_t targetObjectKey)
{
    return g_observationRuntime.targetObjectKeys.count(targetObjectKey) > 0;
}

uint32_t AllocateObservedTrackIdForTarget(uint32_t targetObjectKey)
{
    auto it = g_observationRuntime.observedTrackIdByTargetObject.find(targetObjectKey);
    if (it != g_observationRuntime.observedTrackIdByTargetObject.end())
    {
        return it->second;
    }

    uint32_t observedTrackId =
        std::max(g_observationRuntime.nextObservedTrackId, kObservedTrackIdBase);
    g_observationRuntime.observedTrackIdByTargetObject[targetObjectKey] = observedTrackId;
    g_observationRuntime.targetObjectByObservedTrackId[observedTrackId] = targetObjectKey;
    g_observationRuntime.nextObservedTrackId = observedTrackId + 1;
    return observedTrackId;
}

bool TryGetObservedTrackIdForTarget(uint32_t targetObjectKey, uint32_t& observedNodeId)
{
    auto it = g_observationRuntime.observedTrackIdByTargetObject.find(targetObjectKey);
    if (it == g_observationRuntime.observedTrackIdByTargetObject.end())
    {
        return false;
    }
    observedNodeId = it->second;
    return true;
}

void InitializeObservationNamespaces()
{
    g_observationRuntime = ObservationRuntimeState();
    g_observationRuntime.nextObservedTrackId = kObservedTrackIdBase;

    if (!ObservationModeEnabled())
    {
        return;
    }

    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i)
    {
        g_observationRuntime.observerIds.push_back(i);
        g_observationRuntime.observerIdSet.insert(i);
    }

    for (uint32_t i = 0; i < g_interferenceNodes.GetN(); ++i)
    {
        uint32_t targetObjectKey = GetTargetObjectKeyFromInterferenceIndex(i);
        if (g_observationRuntime.observerIdSet.count(targetObjectKey) > 0)
        {
            continue;
        }
        g_observationRuntime.targetObjectKeys.insert(targetObjectKey);
        AllocateObservedTrackIdForTarget(targetObjectKey);
    }

    NS_LOG_INFO("非合作观测命名空间已初始化: observers="
                << g_observationRuntime.observerIds.size()
                << ", targets=" << g_observationRuntime.targetObjectKeys.size()
                << ", nextObservedTrackId=" << g_observationRuntime.nextObservedTrackId);
}

void FinalizeObservedSignalEvents()
{
    FlushAllActiveObservedEvents();
}

void UpdateObservedTrackStates()
{
    if (!ObservationModeEnabled())
    {
        return;
    }

    const double windowDuration =
        g_environmentConfig.observationPreset.windowDurationSec;
    const double currentTime = Simulator::Now().GetSeconds();
    const double windowEnd = currentTime;
    const double windowStart = std::max(0.0, windowEnd - windowDuration);
    const uint32_t createThreshold =
        g_environmentConfig.observationPreset.trackCreateWindowCount;
    const uint32_t deleteThreshold =
        g_environmentConfig.observationPreset.trackDeleteWindowCount;

    std::vector<ObservedCommWindow> generatedWindowsForThisWindow;
    for (uint32_t observerId : g_observationRuntime.observerIds)
    {
        auto& trackMap = g_observationRuntime.trackStatesByObserver[observerId];
        for (uint32_t targetObjectKey : g_observationRuntime.targetObjectKeys)
        {
            uint32_t observedNodeId = 0;
            if (!TryGetObservedTrackIdForTarget(targetObjectKey, observedNodeId))
            {
                continue;
            }

            auto activityIt = g_observationRuntime.windowActivityByObserverTarget.find(
                {observerId, targetObjectKey});
            const bool observedSuccess =
                activityIt != g_observationRuntime.windowActivityByObserverTarget.end() &&
                activityIt->second.hadSignalDetected;
            const bool hadMissingActivity =
                activityIt != g_observationRuntime.windowActivityByObserverTarget.end() &&
                activityIt->second.hadMissingActivity;

            auto stateIt = trackMap.find(observedNodeId);
            const bool hasExistingState = stateIt != trackMap.end();
            const bool wasStable = hasExistingState && stateIt->second.isStable;

            if (!observedSuccess && !hasExistingState)
            {
                continue;
            }

            ObservedTrackState& state = trackMap[observedNodeId];
            if (state.observedNodeId == 0)
            {
                state.observerId = observerId;
                state.observedNodeId = observedNodeId;
            }

            state.lastWindowStart = windowStart;
            state.lastWindowEnd = windowEnd;

            if (observedSuccess)
            {
                auto& activity = activityIt->second;
                if (activity.activeDetectedSegmentOpen)
                {
                    activity.detectedTxCount++;
                    activity.lastDetectedTxEnd = windowEnd;
                    activity.detectedDurationTotal +=
                        std::max(0.0, windowEnd - activity.activeDetectedSegmentStart);
                    activity.activeDetectedSegmentOpen = false;
                    activity.activeDetectedSegmentStart =
                        std::numeric_limits<double>::quiet_NaN();
                }

                state.consecutiveObservedWindows++;
                state.consecutiveMissingWindows = 0;
                state.isActive = true;
                if (std::isnan(state.firstObservedTime))
                {
                    state.firstObservedTime = windowStart;
                }
                state.lastObservedTime = activity.lastObservedTime;

                if (activity.detectedSampleCount > 0)
                {
                    state.lastCoarsePosX =
                        activity.coarsePosXSum / activity.detectedSampleCount;
                    state.lastCoarsePosY =
                        activity.coarsePosYSum / activity.detectedSampleCount;
                    state.positionConfidence =
                        activity.positionConfidenceSum / activity.detectedSampleCount;
                    state.signalConfidence =
                        activity.signalConfidenceSum / activity.detectedSampleCount;
                    state.overallConfidence =
                        activity.overallConfidenceSum / activity.detectedSampleCount;
                }

                state.signalSortingGroup = activity.signalSortingGroup;
                state.nodeSignalAssociation = activity.nodeSignalAssociation;
                state.disambiguationStatus = activity.disambiguationStatus;
                state.activityPatternScore = activity.activityPatternScore;

                if (!state.isStable &&
                    state.consecutiveObservedWindows >= createThreshold)
                {
                    state.isStable = true;
                }
            }
            else if (hadMissingActivity)
            {
                state.consecutiveObservedWindows = 0;
                state.consecutiveMissingWindows++;
                state.isActive = false;
            }
            else
            {
                state.consecutiveObservedWindows = 0;
                state.consecutiveMissingWindows = 0;
                state.isActive = false;
                if (!state.isStable)
                {
                    ResetUnstableTrackState(state);
                    trackMap.erase(observedNodeId);
                }
            }

            bool shouldOutputWindow =
                trackMap.find(observedNodeId) != trackMap.end() &&
                trackMap[observedNodeId].isStable;
            bool eraseAfterOutput = false;
            if (hadMissingActivity && wasStable &&
                state.consecutiveMissingWindows >= deleteThreshold)
            {
                shouldOutputWindow = true;
                eraseAfterOutput = true;
            }

            if (shouldOutputWindow)
            {
                ObservedCommWindow window;
                window.windowStart = windowStart;
                window.windowEnd = windowEnd;
                window.observedNodeId = observedNodeId;
                window.channelId = kObservedChannelId;
                window.centerFrequencyHz = kObservedCenterFrequencyHz;
                window.observerId = observerId;
                window.sceneType = g_environmentConfig.sceneType;
                window.operationMode =
                    OperationModeToString(g_environmentConfig.operationMode);
                window.noiseLevel =
                    g_environmentConfig.observationPreset.powerNoiseStdDevDb;

                if (observedSuccess)
                {
                    const auto& activity = activityIt->second;
                    window.signalDetected = true;
                    window.isMissing = false;
                    window.missingReason.clear();
                    window.txStartTime = activity.firstDetectedTxStart;
                    window.txEndTime = activity.lastDetectedTxEnd;
                    window.txDuration = activity.detectedDurationTotal;
                    window.txCount = activity.detectedTxCount;
                    window.avgRxPowerDbm =
                        activity.detectedSampleCount > 0
                            ? activity.avgRxPowerDbmSum / activity.detectedSampleCount
                            : std::numeric_limits<double>::quiet_NaN();
                    window.coarsePosX =
                        activity.detectedSampleCount > 0
                            ? activity.coarsePosXSum / activity.detectedSampleCount
                            : std::numeric_limits<double>::quiet_NaN();
                    window.coarsePosY =
                        activity.detectedSampleCount > 0
                            ? activity.coarsePosYSum / activity.detectedSampleCount
                            : std::numeric_limits<double>::quiet_NaN();
                    window.positionConfidence =
                        activity.detectedSampleCount > 0
                            ? activity.positionConfidenceSum / activity.detectedSampleCount
                            : 0.0;
                    window.signalConfidence =
                        activity.detectedSampleCount > 0
                            ? activity.signalConfidenceSum / activity.detectedSampleCount
                            : 0.0;
                    window.overallConfidence =
                        activity.detectedSampleCount > 0
                            ? activity.overallConfidenceSum / activity.detectedSampleCount
                            : 0.0;
                    window.stateSequence = BuildStateSequence(activity);
                    uint32_t activeSlots = 0;
                    for (uint8_t bit : activity.activeSubslots)
                    {
                        activeSlots += bit ? 1 : 0;
                    }
                    window.activeRatio = activity.activeSubslots.empty()
                                             ? 0.0
                                             : static_cast<double>(activeSlots) /
                                                   activity.activeSubslots.size();
                }
                else if (hadMissingActivity)
                {
                    window.signalDetected = false;
                    window.isMissing = true;
                    window.missingReason = activityIt->second.missingReason;
                    window.txStartTime = std::numeric_limits<double>::quiet_NaN();
                    window.txEndTime = std::numeric_limits<double>::quiet_NaN();
                    window.txDuration = std::numeric_limits<double>::quiet_NaN();
                    window.txCount = std::numeric_limits<double>::quiet_NaN();
                    window.avgRxPowerDbm = std::numeric_limits<double>::quiet_NaN();
                    window.coarsePosX = std::numeric_limits<double>::quiet_NaN();
                    window.coarsePosY = std::numeric_limits<double>::quiet_NaN();
                    window.positionConfidence = 0.0;
                    window.signalConfidence = 0.0;
                    window.overallConfidence = 0.0;
                    window.stateSequence.assign(
                        g_environmentConfig.observationPreset.subslotCount, '0');
                    window.activeRatio = 0.0;
                }
                else
                {
                    window.signalDetected = false;
                    window.isMissing = false;
                    window.missingReason.clear();
                    window.txStartTime = std::numeric_limits<double>::quiet_NaN();
                    window.txEndTime = std::numeric_limits<double>::quiet_NaN();
                    window.txDuration = 0.0;
                    window.txCount = 0.0;
                    window.avgRxPowerDbm = std::numeric_limits<double>::quiet_NaN();
                    window.coarsePosX = std::numeric_limits<double>::quiet_NaN();
                    window.coarsePosY = std::numeric_limits<double>::quiet_NaN();
                    window.positionConfidence = 0.0;
                    window.signalConfidence = 0.0;
                    window.overallConfidence = 0.0;
                    window.stateSequence.assign(
                        g_environmentConfig.observationPreset.subslotCount, '0');
                    window.activeRatio = 0.0;
                }

                AppendObservedCommWindow(window);
                generatedWindowsForThisWindow.push_back(window);
            }

            if (eraseAfterOutput)
            {
                trackMap.erase(observedNodeId);
            }
        }

    }

    BuildObservedLinkEvidenceForWindow(generatedWindowsForThisWindow);

    g_observationRuntime.windowActivityByObserverTarget.clear();

    if (currentTime + windowDuration <= g_config.duration)
    {
        Simulator::Schedule(Seconds(windowDuration), &UpdateObservedTrackStates);
    }
}

void MonitorObservedSignalEvents()
{
    if (!ObservationModeEnabled())
    {
        return;
    }

    double currentTime = Simulator::Now().GetSeconds();
    bool targetActiveNow = IsInterferenceNodeActiveAtTime(currentTime);

    for (uint32_t observerId : g_observationRuntime.observerIds)
    {
        Ptr<MobilityModel> observerMob = g_uavNodes.Get(observerId)->GetObject<MobilityModel>();
        if (!observerMob)
        {
            continue;
        }

        Vector observerPos = observerMob->GetPosition();

        for (uint32_t targetIndex = 0; targetIndex < g_interferenceNodes.GetN(); ++targetIndex)
        {
            uint32_t targetObjectKey = GetTargetObjectKeyFromInterferenceIndex(targetIndex);
            if (!IsTargetObjectKey(targetObjectKey) || IsObserverId(targetObjectKey))
            {
                continue;
            }

            std::pair<uint32_t, uint32_t> activeKey{observerId, targetObjectKey};
            if (!targetActiveNow)
            {
                FlushActiveObservedEvent(activeKey);
                continue;
            }

            Ptr<MobilityModel> targetMob =
                g_interferenceNodes.Get(targetIndex)->GetObject<MobilityModel>();
            if (!targetMob)
            {
                FlushActiveObservedEvent(activeKey);
                continue;
            }

            Vector targetPos = targetMob->GetPosition();
            double dx = observerPos.x - targetPos.x;
            double dy = observerPos.y - targetPos.y;
            double dz = observerPos.z - targetPos.z;
            double dist = std::sqrt(dx * dx + dy * dy + dz * dz);

            bool isMissing = false;
            std::string missingReason;
            if (dist > g_environmentConfig.observationPreset.observationRangeM)
            {
                isMissing = true;
                missingReason = "range";
            }
            else if (IsObservationPathOccluded(observerPos, targetPos))
            {
                isMissing = true;
                missingReason = "occlusion";
            }
            else
            {
                if (!g_randVar)
                {
                    g_randVar = CreateObject<UniformRandomVariable>();
                }
                if (g_randVar->GetValue() <
                    g_environmentConfig.observationPreset.randomDropRate)
                {
                    isMissing = true;
                    missingReason = "random_drop";
                }
            }

            bool signalDetected = !isMissing;
            uint32_t observedNodeId =
                AllocateObservedTrackIdForTarget(targetObjectKey);

            auto it = g_observationRuntime.activeEventAccumulators.find(activeKey);
            bool stateChanged =
                it == g_observationRuntime.activeEventAccumulators.end() ||
                it->second.signalDetected != signalDetected ||
                it->second.missingReason != missingReason;
            if (stateChanged && it != g_observationRuntime.activeEventAccumulators.end())
            {
                FlushActiveObservedEvent(activeKey);
            }

            ActiveObservedEventAccumulator& acc =
                g_observationRuntime.activeEventAccumulators[activeKey];
            if (stateChanged || !acc.active)
            {
                acc = ActiveObservedEventAccumulator();
                acc.active = true;
                acc.signalDetected = signalDetected;
                acc.isMissing = isMissing;
                acc.missingReason = missingReason;
                acc.segmentStartTime = currentTime;
                acc.observerId = observerId;
                acc.targetObjectKey = targetObjectKey;
                acc.observedNodeId = observedNodeId;
                acc.channelId = kObservedChannelId;
                acc.centerFrequencyHz = kObservedCenterFrequencyHz;
                acc.sceneType = g_environmentConfig.sceneType;
                acc.operationMode =
                    OperationModeToString(g_environmentConfig.operationMode);
                acc.signalSortingGroup =
                    "grp_target_" + std::to_string(targetObjectKey);
                acc.nodeSignalAssociation = signalDetected ? "associated" : "unobserved";
                acc.disambiguationStatus = signalDetected ? "stable" : "uncertain";
                acc.activityPatternScore = GetInterferenceDutyCycle();
            }

            acc.segmentEndTime = std::min(g_config.duration,
                                          currentTime + kObservationEventPollIntervalSec);
            acc.sampleCount++;
            acc.noiseLevelSum +=
                g_environmentConfig.observationPreset.powerNoiseStdDevDb;

            double distanceRatio = Clamp01(
                dist / std::max(1.0, g_environmentConfig.observationPreset.observationRangeM));
            double positionConfidence = signalDetected ? (0.95 - 0.45 * distanceRatio) : 0.10;
            double signalConfidence = signalDetected ? (0.90 - 0.50 * distanceRatio) : 0.05;
            double overallConfidence =
                Clamp01((positionConfidence + signalConfidence) * 0.5);

            acc.positionConfidenceSum += Clamp01(positionConfidence);
            acc.signalConfidenceSum += Clamp01(signalConfidence);
            acc.overallConfidenceSum += overallConfidence;

            if (signalDetected)
            {
                double observedPosX =
                    targetPos.x +
                    SampleGaussian(g_environmentConfig.observationPreset.positionNoiseStdDevM);
                double observedPosY =
                    targetPos.y +
                    SampleGaussian(g_environmentConfig.observationPreset.positionNoiseStdDevM);
                double rxPowerDbm =
                    ComputeObservedRxPowerDbm(observerPos, targetPos) +
                    SampleGaussian(g_environmentConfig.observationPreset.powerNoiseStdDevDb);

                acc.avgRxPowerDbmSum += rxPowerDbm;
                acc.coarsePosXSum += observedPosX;
                acc.coarsePosYSum += observedPosY;

                UpdateWindowActivityAccumulator(observerId,
                                                targetObjectKey,
                                                true,
                                                missingReason,
                                                currentTime,
                                                std::min(g_config.duration,
                                                         currentTime +
                                                             kObservationEventPollIntervalSec),
                                                observedPosX,
                                                observedPosY,
                                                rxPowerDbm,
                                                Clamp01(positionConfidence),
                                                Clamp01(signalConfidence),
                                                overallConfidence,
                                                currentTime);
            }
            else
            {
                UpdateWindowActivityAccumulator(observerId,
                                                targetObjectKey,
                                                false,
                                                missingReason,
                                                currentTime,
                                                std::min(g_config.duration,
                                                         currentTime +
                                                             kObservationEventPollIntervalSec),
                                                std::numeric_limits<double>::quiet_NaN(),
                                                std::numeric_limits<double>::quiet_NaN(),
                                                std::numeric_limits<double>::quiet_NaN(),
                                                Clamp01(positionConfidence),
                                                Clamp01(signalConfidence),
                                                overallConfidence,
                                                currentTime);
            }
        }
    }

    if (currentTime + kObservationEventPollIntervalSec < g_config.duration)
    {
        Simulator::Schedule(Seconds(kObservationEventPollIntervalSec),
                            &MonitorObservedSignalEvents);
    }
}

void LogPositions() {
    double currentTime = Simulator::Now().GetSeconds();
    // 记录正常无人机集群 (node_type=0)
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        Ptr<MobilityModel> mob = g_uavNodes.Get(i)->GetObject<MobilityModel>();
        if (mob) {
            Vector pos = mob->GetPosition();
            Vector vel = mob->GetVelocity();
            double speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
            g_posLog << currentTime << "," << i << "," << pos.x << "," << pos.y << "," << pos.z << ",0," << speed << "\n";
        }
    }
    // 记录黑飞节点 (node_type=1，nodeId 从 1000 起步，前端据此渲染红色敌机)
    for (uint32_t i = 0; i < g_interferenceNodes.GetN(); ++i) {
        Ptr<MobilityModel> mob = g_interferenceNodes.Get(i)->GetObject<MobilityModel>();
        if (mob) {
            Vector pos = mob->GetPosition();
            Vector vel = mob->GetVelocity(); 
            double speed = std::sqrt(vel.x * vel.x + vel.y * vel.y + vel.z * vel.z);
            
            // 在末尾通过逗号追加 speed
            g_posLog << currentTime << "," << (1000 + i) << "," << pos.x << "," << pos.y << "," << pos.z << ",1," << speed << "\n";
        }
    }
    // Record positions every 0.1s to allow smooth animation
    Simulator::Schedule(Seconds(0.1), &LogPositions);
}

void ComputeTDMASlots() {
    uint32_t n = g_uavNodes.GetN();
    double conflictRange = 300.0;  // 2 × commRange(150m)
    
    NS_LOG_INFO("计算 TDMA 空间复用分组 (冲突距离=" << conflictRange << "m)...");
    
    // ---- 1. 构建冲突邻接矩阵 ----
    std::vector<std::vector<bool>> conflict(n, std::vector<bool>(n, false));
    std::vector<uint32_t> conflictDegree(n, 0);
    
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = i + 1; j < n; ++j) {
            double dist = CalculateDistance(g_uavNodes.Get(i), g_uavNodes.Get(j));
            if (dist < conflictRange) {
                conflict[i][j] = conflict[j][i] = true;
                conflictDegree[i]++;
                conflictDegree[j]++;
            }
        }
    }
    
    // ---- 2. 按冲突度降序排列（高度数节点优先着色）----
    std::vector<std::pair<uint32_t, uint32_t>> nodeOrder;  // (degree, nodeId)
    for (uint32_t i = 0; i < n; ++i) {
        nodeOrder.push_back({conflictDegree[i], i});
    }
    std::sort(nodeOrder.rbegin(), nodeOrder.rend());
    
    // ---- 3. 贪心图着色 ----
    std::vector<int> color(n, -1);
    uint32_t numColors = 0;
    
    for (auto& [deg, nodeId] : nodeOrder) {
        // 收集冲突邻居已用的颜色
        std::set<int> usedColors;
        for (uint32_t j = 0; j < n; ++j) {
            if (conflict[nodeId][j] && color[j] >= 0) {
                usedColors.insert(color[j]);
            }
        }
        
        // 找最小可用颜色
        int c = 0;
        while (usedColors.count(c)) c++;
        
        color[nodeId] = c;
        if ((uint32_t)(c + 1) > numColors) numColors = c + 1;
    }
    
    // ---- 4. 至少1组 ----
    if (numColors == 0) numColors = 1;
    
    // ---- 5. 存储结果 ----
    g_tdma.numGroups = numColors;
    g_tdma.cycleDuration = numColors * g_tdma.slotDuration;
    
    for (uint32_t i = 0; i < n; ++i) {
        g_tdma.slotAssignment[i] = (uint32_t)color[i];
    }
    
    // ---- 6. 计算每周期发包数 ----
    // 应用层速率: 100Kbps × 2流 = 200Kbps/节点
    // 每周期数据量: 200000 × cycleDuration / 8  (字节)
    // 每周期包数: ceil(数据量 / packetSize)
    double dataPerCycle = g_diffParams.trafficLoadMbps * 1e6 * g_tdma.cycleDuration / 8.0;
    g_tdma.basePacketsPerSlot = (uint32_t)std::ceil(dataPerCycle / g_config.packetSize);
    g_tdma.basePacketsPerSlot = std::max(g_tdma.basePacketsPerSlot, g_tdma.minPacketsPerSlot);
    g_tdma.basePacketsPerSlot = std::min(g_tdma.basePacketsPerSlot, g_tdma.maxPacketsPerSlot);

    // ---- 7. 输出分组结果 ----
    std::cout << "TDMA 空间复用分组完成:" << std::endl;
    std::cout << "  分组数(时隙数): " << numColors << std::endl;
    std::cout << "  帧周期: " << g_tdma.cycleDuration * 1000.0 << " ms" << std::endl;
    std::cout << "  基准发包: " << g_tdma.basePacketsPerSlot << " 包/节点/时隙" << std::endl;
    
    double compressionRatio = (double)n / numColors;
    std::cout << "  空间复用增益: " << compressionRatio << "x "
              << "(纯TDMA需 " << n << " 时隙，空间TDMA仅需 " << numColors << " 时隙)" << std::endl;
    
    for (uint32_t g = 0; g < numColors; ++g) {
        std::cout << "  Slot " << g << ": [";
        bool first = true;
        for (uint32_t i = 0; i < n; ++i) {
            if (g_tdma.slotAssignment[i] == g) {
                if (!first) std::cout << ", ";
                std::cout << "UAV" << i;
                first = false;
            }
        }
        std::cout << "]" << std::endl;
    }
    
    // ---- 8. 构建冲突矩阵和时隙占用表（供动态重分配使用）----
    g_tdma.conflictMatrix = conflict;
    
    g_tdma.slotOccupants.clear();
    g_tdma.slotOccupants.resize(numColors);
    for (uint32_t i = 0; i < n; ++i) {
        g_tdma.slotOccupants[color[i]].push_back(i);
    }
    
    // 初始化 per-node 包预算为基准值
    for (uint32_t i = 0; i < n; ++i) {
        g_tdma.perNodePackets[i] = g_tdma.basePacketsPerSlot;
        g_tdma.bonusSlots[i].clear();
        g_tdma.urgency[i] = 0.0;
    }
    
    g_tdma.lastLinkCount = 0;
    for (uint32_t i = 0; i < n; ++i) {
        g_tdma.lastLinkCount += g_state.neighbors[i].size();
    }
    g_tdma.lastLinkCount /= 2;
}

// ==================== 动态 TDMA 重分配 ====================

/**
 * 计算节点 QoS 紧迫度
 *
 * urgency = 0.0  → QoS 完全满足，可以让出资源
 * urgency = 1.0  → QoS 严重不达标，急需更多资源
 *
 * 综合考虑：PDR差距(70%), 时延超标(20%), 吞吐不足(10%)
 */
double ComputeQoSUrgency(uint32_t nodeId) {
    double urgency = 0.0;
    
    // ---- PDR 维度 (权重 70%) ----
    if (g_state.nodePDR.count(nodeId) && g_state.nodePDR[nodeId] > 0.0) {
        double pdrGap = g_config.targetPDR - g_state.nodePDR[nodeId];
        if (pdrGap > 0) {
            // PDR 差距归一化：差 0.85 → urgency=1.0
            urgency += std::min(1.0, pdrGap / g_config.targetPDR) * 0.70;
        }
    } else {
        // 尚无 PDR 数据（刚启动），给一个中等紧迫度
        urgency += 0.3;
    }
    
    // ---- 时延维度 (权重 20%) ----
    if (g_state.nodeDelay.count(nodeId) && g_state.nodeDelay[nodeId] > 0.0) {
        double delayRatio = g_state.nodeDelay[nodeId] / g_config.maxEndToEndDelay;
        if (delayRatio > 1.0) {
            urgency += std::min(1.0, (delayRatio - 1.0)) * 0.20;
        }
    }
    
    // ---- 吞吐量维度 (权重 10%) ----
    if (g_state.nodeThroughput.count(nodeId)) {
        double tputRatio = g_state.nodeThroughput[nodeId] / g_config.minThroughput;
        if (tputRatio < 1.0) {
            urgency += (1.0 - tputRatio) * 0.10;
        }
    }
    
    return std::max(0.0, std::min(1.0, urgency));
}

/**
 * 检查节点 nodeId 是否可以安全使用 slotId 作为 bonus 时隙
 *
 * 条件：该时隙内所有已有占用者都与 nodeId 不冲突（空间隔离）
 */
bool IsSlotCompatible(uint32_t nodeId, uint32_t slotId) {
    uint32_t n = g_uavNodes.GetN();
    
    if (slotId >= g_tdma.slotOccupants.size()) return false;
    
    // 不能是自己的主时隙（已经在发了）
    if (g_tdma.slotAssignment[nodeId] == slotId) return false;
    
    // 检查与该时隙所有占用者（主时隙 + 已分配的 bonus 节点）的冲突
    for (uint32_t occupant : g_tdma.slotOccupants[slotId]) {
        if (occupant >= n || nodeId >= n) continue;
        if (g_tdma.conflictMatrix[nodeId][occupant]) {
            return false;  // 存在空间冲突
        }
    }
    
    // 检查与已获得此 bonus 时隙的其他节点的冲突
    for (auto& [otherId, otherBonus] : g_tdma.bonusSlots) {
        if (otherId == nodeId) continue;
        for (uint32_t bs : otherBonus) {
            if (bs == slotId && nodeId < n && otherId < n) {
                if (g_tdma.conflictMatrix[nodeId][otherId]) {
                    return false;
                }
            }
        }
    }
    
    return true;
}

/**
 * 动态 TDMA 重分配主函数
 *
 * 调用频率：每 reallocationInterval 秒（默认 5s）
 *
 * 三层调节机制：
 *   层1: 主时隙包预算调节（快速，每次调用）
 *        → QoS差的节点在自己的时隙内发更多包
 *   层2: Bonus 时隙分配/回收（中速，每次调用）
 *        → QoS严重不达标的节点借用空闲时隙
 *   层3: 空间复用重着色（慢速，仅拓扑剧变时）
 *        → 重新计算冲突图和分组方案
 */
void DynamicTDMAReallocation() {
    if (!g_tdma.enabled) return;
    
    double currentTime = Simulator::Now().GetSeconds();
    uint32_t n = g_uavNodes.GetN();
    
    NS_LOG_INFO("时间 " << currentTime << "s: 执行动态 TDMA 重分配 (#" 
                << g_tdma.reallocationCount << ")");
    g_tdma.reallocationCount++;
    
    // ============ 层3: 拓扑剧变检测 → 重着色 ============
    uint32_t currentLinks = 0;
    for (uint32_t i = 0; i < n; ++i) {
        currentLinks += g_state.neighbors[i].size();
    }
    currentLinks /= 2;
    
    bool needRecolor = false;
    if (g_tdma.lastLinkCount > 0) {
        double linkChange = std::abs((double)currentLinks - (double)g_tdma.lastLinkCount) 
                           / g_tdma.lastLinkCount;
        if (linkChange > 0.20) {
            needRecolor = true;
            NS_LOG_INFO("  拓扑链路变化 " << (linkChange*100) << "% > 20%，触发重着色");
        }
    }
    
    if (needRecolor) {
        g_tdma.recoloringCount++;
        ComputeTDMASlots();  // 重新着色（内部会重置 perNodePackets 和 bonusSlots）
        g_tdma.lastLinkCount = currentLinks;
        
        // 重着色后跳过本轮的预算调节，让新分组先稳定一个周期
        if (g_tdmaLog.is_open()) {
            g_tdmaLog << currentTime << ",RECOLOR,"
                      << g_tdma.numGroups << ","
                      << g_tdma.cycleDuration * 1000.0 << "\n";
        }
        
        Simulator::Schedule(Seconds(g_tdma.reallocationInterval), 
                           &DynamicTDMAReallocation);
        return;
    }
    g_tdma.lastLinkCount = currentLinks;
    
    // ============ 计算全节点 QoS 紧迫度 ============
    double totalUrgency = 0.0;
    uint32_t urgentCount = 0;
    uint32_t satisfiedCount = 0;
    
    for (uint32_t i = 0; i < n; ++i) {
        g_tdma.urgency[i] = ComputeQoSUrgency(i);
        totalUrgency += g_tdma.urgency[i];
        if (g_tdma.urgency[i] > 0.3) urgentCount++;
        if (g_tdma.urgency[i] < 0.05) satisfiedCount++;
    }
    
    NS_LOG_INFO("  QoS 评估: 紧急=" << urgentCount 
                << " 满足=" << satisfiedCount
                << " 平均紧迫度=" << (n > 0 ? totalUrgency/n : 0));
    
    // ============ 层1: 主时隙包预算调节 ============
    // 策略：总预算守恒（不增加总流量，只在节点间重新分配）
    //   总预算 = n × basePacketsPerSlot
    //   每节点预算 = base × (1 + urgency × boostFactor) → 然后归一化
    
    uint32_t totalBudget = n * g_tdma.basePacketsPerSlot;
    
    // 计算原始权重
    std::vector<double> rawBudget(n);
    double budgetSum = 0.0;
    double boostFactor = 2.0;  // 最紧急节点可获得 3× 基准
    
    for (uint32_t i = 0; i < n; ++i) {
        rawBudget[i] = 1.0 + g_tdma.urgency[i] * boostFactor;
        budgetSum += rawBudget[i];
    }
    
    // 归一化使总预算守恒
    for (uint32_t i = 0; i < n; ++i) {
        double normalized = rawBudget[i] / budgetSum * totalBudget;
        uint32_t pkts = (uint32_t)std::round(normalized);
        pkts = std::max(g_tdma.minPacketsPerSlot, 
                        std::min(g_tdma.maxPacketsPerSlot, pkts));
        g_tdma.perNodePackets[i] = pkts;
    }
    
    // ============ 层2: Bonus 时隙分配/回收 ============
    
    // 2a. 回收：QoS 恢复正常的节点释放 bonus 时隙
    for (uint32_t i = 0; i < n; ++i) {
        if (g_tdma.urgency[i] < 0.10 && !g_tdma.bonusSlots[i].empty()) {
            NS_LOG_INFO("  节点 " << i << " QoS 恢复，回收 " 
                        << g_tdma.bonusSlots[i].size() << " 个 bonus 时隙");
            g_tdma.bonusSlots[i].clear();
        }
    }
    
    // 2b. 分配：QoS 严重不达标的节点尝试获取 bonus 时隙
    // 按紧迫度降序排列，优先保障最差的节点
    std::vector<std::pair<double, uint32_t>> urgencyRank;
    for (uint32_t i = 0; i < n; ++i) {
        urgencyRank.push_back({g_tdma.urgency[i], i});
    }
    std::sort(urgencyRank.rbegin(), urgencyRank.rend());
    
    uint32_t maxBonusSlotsPerNode = 2;  // 每节点最多 2 个 bonus 时隙
    
    for (auto& [urg, nodeId] : urgencyRank) {
        // 只给紧迫度 > 0.4 的节点分配 bonus
        if (urg < 0.40) break;
        
        // 已有足够 bonus 时隙
        if (g_tdma.bonusSlots[nodeId].size() >= maxBonusSlotsPerNode) continue;
        
        // 遍历所有时隙，寻找兼容的
        for (uint32_t slotId = 0; slotId < g_tdma.numGroups; ++slotId) {
            if (g_tdma.bonusSlots[nodeId].size() >= maxBonusSlotsPerNode) break;
            
            if (IsSlotCompatible(nodeId, slotId)) {
                g_tdma.bonusSlots[nodeId].push_back(slotId);
                NS_LOG_INFO("  节点 " << nodeId << " (urgency=" << urg 
                            << ") 获得 bonus 时隙 " << slotId);
            }
        }
    }
    
    // ============ 日志记录 ============
    if (g_tdmaLog.is_open()) {
        for (uint32_t i = 0; i < n; ++i) {
            std::string bonusStr = "";
            for (uint32_t bs : g_tdma.bonusSlots[i]) {
                if (!bonusStr.empty()) bonusStr += ";";
                bonusStr += std::to_string(bs);
            }
            if (bonusStr.empty()) bonusStr = "none";
            
            g_tdmaLog << currentTime << ","
                      << i << ","
                      << g_tdma.slotAssignment[i] << ","
                      << g_tdma.numGroups << ","
                      << g_tdma.perNodePackets[i] << ","
                      << bonusStr << ","
                      << g_tdma.urgency[i] << "\n";
        }
    }
    
    // 调度下次重分配
    Simulator::Schedule(Seconds(g_tdma.reallocationInterval), 
                       &DynamicTDMAReallocation);
}

/**
 * 发送单个 TDMA 数据包
 */
void SendTDMAPacket(uint32_t nodeId, uint32_t flowIdx) {
    auto it = g_tdma.nodeFlows.find(nodeId);
    if (it == g_tdma.nodeFlows.end()) return;
    
    auto& flows = it->second;
    if (flowIdx >= flows.size()) return;
    if (IsTrafficNodeCurrentlyFailed(nodeId)) return;

    Ptr<Packet> pkt = Create<Packet>((uint32_t)g_config.packetSize);
    int sent = flows[flowIdx].socket->Send(pkt);
    
    // 记录发送事件（供前端可视化）
    if (sent > 0 && g_transLog.is_open()) {
        g_transLog << Simulator::Now().GetSeconds() << "," 
                   << nodeId << ",TX_TDMA\n";
    }
}

/**
 * Bonus 时隙突发发送
 *
 * 在借用的 bonus 时隙内发送额外数据包
 * 每帧由 TDMABurstSend 调度，非自递归（避免旧 bonus 泄漏）
 */
void TDMABonusBurst(uint32_t nodeId, uint32_t bonusSlotId) {
    if (IsTrafficNodeCurrentlyFailed(nodeId)) return;

    // 安全检查：确认 bonus 时隙仍然有效（可能已被回收）
    auto it = g_tdma.bonusSlots.find(nodeId);
    if (it == g_tdma.bonusSlots.end()) return;
    auto& slots = it->second;
    bool stillValid = false;
    for (uint32_t s : slots) {
        if (s == bonusSlotId) { stillValid = true; break; }
    }
    if (!stillValid) return;
    
    // 发送 bonusPktsPerSlot 个包
    auto flowIt = g_tdma.nodeFlows.find(nodeId);
    if (flowIt == g_tdma.nodeFlows.end() || flowIt->second.empty()) return;
    
    uint32_t numFlows = (uint32_t)flowIt->second.size();
    uint32_t totalPkts = g_tdma.bonusPktsPerSlot;
    double effectiveSlot = g_tdma.slotDuration - 2.0 * g_tdma.guardTime;
    double pktInterval = (totalPkts > 1) ? effectiveSlot / (totalPkts - 1) : 0.0;
    
    for (uint32_t p = 0; p < totalPkts; ++p) {
        double offset = g_tdma.guardTime + p * pktInterval;
        uint32_t flowIdx = p % numFlows;
        Simulator::Schedule(Seconds(offset), &SendTDMAPacket, nodeId, flowIdx);
    }
    
    // 记录 bonus 发送事件
    if (g_transLog.is_open()) {
        g_transLog << Simulator::Now().GetSeconds() << "," 
                   << nodeId << ",TX_BONUS_SLOT" << bonusSlotId << "\n";
    }
}

/**
 * TDMA 时隙突发发送
 *
 * 在分配给该节点的时隙内，集中发送所有累积数据包
 * 发送完毕后自动调度下一帧的突发
 *
 * 时序示意:
 *   |<--------- cycleDuration --------->|
 *   | Slot0 | Slot1 | ... | Slot(G-1)  |
 *   |  ↑ 节点A在此发送突发包             |
 *   |       |  ↑ 节点B在此发送            |
 *   |                                   |
 *   └─── 自动调度到下一帧的同一时隙 ──────┘
 */
/**
 * 主时隙突发发送 + Bonus 时隙调度
 *
 * 每帧周期调用一次，完成两件事：
 *   1. 在主时隙内发送 perNodePackets[nodeId] 个包
 *   2. 如果有 bonus 时隙，调度 TDMABonusBurst
 */
void TDMABurstSend(uint32_t nodeId) {
    if (Simulator::Now().GetSeconds() >= g_config.duration - 0.5) return;
    if (IsTrafficNodeCurrentlyFailed(nodeId)) {
        Simulator::Schedule(Seconds(g_tdma.cycleDuration), &TDMABurstSend, nodeId);
        return;
    }
    
    auto it = g_tdma.nodeFlows.find(nodeId);
    if (it == g_tdma.nodeFlows.end() || it->second.empty()) {
        Simulator::Schedule(Seconds(g_tdma.cycleDuration), &TDMABurstSend, nodeId);
        return;
    }
    
    // ---- 1. 主时隙突发 ----
    auto& flows = it->second;
    uint32_t numFlows = (uint32_t)flows.size();
    
    // ★ 使用 per-node 动态包预算
    uint32_t totalPkts = g_tdma.perNodePackets.count(nodeId) ? 
                         g_tdma.perNodePackets[nodeId] : g_tdma.basePacketsPerSlot;
    
    double effectiveSlot = g_tdma.slotDuration - 2.0 * g_tdma.guardTime;
    double pktInterval = (totalPkts > 1) ? effectiveSlot / (totalPkts - 1) : 0.0;
    
    for (uint32_t p = 0; p < totalPkts; ++p) {
        double offset = g_tdma.guardTime + p * pktInterval;
        uint32_t flowIdx = p % numFlows;
        Simulator::Schedule(Seconds(offset), &SendTDMAPacket, nodeId, flowIdx);
    }
    
    // ---- 2. 调度 Bonus 时隙突发 ----
    auto bonusIt = g_tdma.bonusSlots.find(nodeId);
    if (bonusIt != g_tdma.bonusSlots.end()) {
        uint32_t mySlot = g_tdma.slotAssignment[nodeId];
        
        for (uint32_t bonusSlotId : bonusIt->second) {
            // 计算 bonus 时隙相对于当前主时隙的时间偏移
            double delay;
            if (bonusSlotId > mySlot) {
                delay = (bonusSlotId - mySlot) * g_tdma.slotDuration;
            } else {
                // 在帧内更早的位置 → 等到下一帧的该位置
                // 不过由于 TDMABurstSend 在主时隙开始时触发，
                // 更早的 bonus 已经过了，需要绕一圈
                delay = (g_tdma.numGroups - mySlot + bonusSlotId) * g_tdma.slotDuration;
            }
            
            Simulator::Schedule(Seconds(delay), &TDMABonusBurst, nodeId, bonusSlotId);
        }
    }
    
    // ---- 3. 调度下一帧的主时隙突发 ----
    Simulator::Schedule(Seconds(g_tdma.cycleDuration), &TDMABurstSend, nodeId);
}

/**
 * \brief 资源重分配主函数
 */

struct FlowCumulative {
    uint64_t txPkts  = 0;
    uint64_t rxPkts  = 0;
    uint64_t rxBytes = 0;
    double   delaySumS = 0.0;
};

static const int QOS_WINDOW = 20;  // 20 ticks × 0.1s = 2 秒滑动窗口
static std::deque<std::map<FlowId, FlowCumulative>> g_cumHistory;

void MonitorQoSPerformance() {
    double currentTime = Simulator::Now().GetSeconds();

    g_flowMonitor->CheckForLostPackets();
    Ptr<Ipv4FlowClassifier> classifier = DynamicCast<Ipv4FlowClassifier>(
        g_flowHelper.GetClassifier());
    FlowMonitor::FlowStatsContainer stats = g_flowMonitor->GetFlowStats();

    // 1. 拍摄当前累计快照
    std::map<FlowId, FlowCumulative> snap;
    for (auto& [fid, fs] : stats) {
        FlowCumulative c;
        c.txPkts    = fs.txPackets;
        c.rxPkts    = fs.rxPackets;
        c.rxBytes   = fs.rxBytes;
        c.delaySumS = fs.delaySum.GetSeconds();
        snap[fid] = c;
    }

    g_cumHistory.push_back(snap);
    if ((int)g_cumHistory.size() > QOS_WINDOW) {
        g_cumHistory.pop_front();
    }

    // 2. 取窗口最早的快照
    const auto& oldSnap = g_cumHistory.front();
    double windowSec = (double)g_cumHistory.size() * 0.1;
    if (windowSec < 0.1) windowSec = 0.1;

    // 3. 计算窗口内增量，聚合到节点
    std::map<uint32_t, uint64_t> wTx, wRx, wBytes;
    std::map<uint32_t, double>   wDelay;
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        wTx[i] = wRx[i] = wBytes[i] = 0;
        wDelay[i] = 0.0;
    }

    for (auto& [fid, cur] : snap) {
        Ipv4FlowClassifier::FiveTuple tuple = classifier->FindFlow(fid);
        uint32_t srcId = (tuple.sourceAddress.Get() & 0xFF) - 1;
        if (srcId >= g_uavNodes.GetN()) continue;

        uint64_t oldTx = 0, oldRx = 0, oldB = 0;
        double   oldD  = 0.0;
        auto it = oldSnap.find(fid);
        if (it != oldSnap.end()) {
            oldTx = it->second.txPkts;
            oldRx = it->second.rxPkts;
            oldB  = it->second.rxBytes;
            oldD  = it->second.delaySumS;
        }

        wTx[srcId]    += (cur.txPkts  - oldTx);
        wRx[srcId]    += (cur.rxPkts  - oldRx);
        wBytes[srcId] += (cur.rxBytes - oldB);
        wDelay[srcId] += (cur.delaySumS - oldD);
    }

    // 4. 计算每节点 QoS
    const bool trafficWarm = currentTime >= g_tdma.trafficStartTime + windowSec;
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        const bool failedNode = IsTrafficNodeCurrentlyFailed(i);
        if (wTx[i] > 0) {
            g_state.nodePDR[i] = (double)wRx[i] / wTx[i];
        } else if (failedNode || trafficWarm) {
            g_state.nodePDR[i] = 0.0;
        }
        if (wRx[i] > 0) {
            g_state.nodeDelay[i] = wDelay[i] / wRx[i];
        } else if (failedNode || (trafficWarm && wTx[i] > 0)) {
            g_state.nodeDelay[i] = g_config.maxEndToEndDelay;
        } else if (trafficWarm && wTx[i] == 0) {
            g_state.nodeDelay[i] = g_config.maxEndToEndDelay;
        }
        g_state.nodeThroughput[i] = wBytes[i] * 8.0 / windowSec;
    }

    // 5. 写 CSV
    g_qosLog << currentTime;
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        g_qosLog << "," << g_state.nodePDR[i]
                 << "," << g_state.nodeDelay[i]
                 << "," << g_state.nodeThroughput[i];
    }
    g_qosLog << std::endl;

    Simulator::Schedule(Seconds(0.1), &MonitorQoSPerformance);
}

/**
 * \brief 记录拓扑变化
 */
void LogTopologyChange() {
    double currentTime = Simulator::Now().GetSeconds();
    // 确保记录前拓扑是最新的
    UpdateTopology();
    
    // 统计活跃链路数
    uint32_t numLinks = 0;
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i) {
        numLinks += g_state.neighbors[i].size();
    }
    numLinks /= 2; // 无向图
    
    // 计算网络连通性
    double connectivity = 0.0;
    uint32_t n = g_uavNodes.GetN();
    if (n > 1) {
        uint32_t maxLinks = n * (n - 1) / 2;
        connectivity = (double)numLinks / maxLinks;
    }
    
    // 旧的简略拓扑记录
    g_topologyLog << currentTime << "," << numLinks << "," << connectivity << std::endl;
    
    // 专门为可视化大屏写入连通动画 rtk-topology-changes.txt
    char topoBuffer[128];
    snprintf(topoBuffer, sizeof(topoBuffer), "%.1f-%.1fs: ", currentTime, currentTime + 0.2);
    g_topoChangesLog << topoBuffer;
    bool firstTopo = true;
    for (uint32_t i = 0; i < n; ++i) {
        for (uint32_t j = i + 1; j < n; ++j) {
            if (g_state.adjacencyMatrix[i][j]) {
                if (!firstTopo) g_topoChangesLog << ", ";
                g_topoChangesLog << "Node" << i << "-Node" << j;
                firstTopo = false;
            }
        }
    }
    if (firstTopo) g_topoChangesLog << "none";
    g_topoChangesLog << "\n";
    
    // 拓扑演化记录 (time,num_links,connectivity)
    g_topologyEvolutionLog << currentTime << "," << numLinks << "," << connectivity << "\n";
    
    // 详细拓扑统计 (time,num_nodes,num_links,avg_degree,network_density)
    double avg_degree = (n > 0) ? (2.0 * numLinks / n) : 0.0;
    g_topologyDetailedLog << currentTime << "," 
                          << n << ","
                          << numLinks << ","
                          << avg_degree << ","
                          << connectivity << "\n";

    // Front-end expects sync between topology and positions
    Simulator::Schedule(Seconds(0.1), &LogTopologyChange);
}

// ==================== 应用层业务生成 ====================

/**
 * \brief 为节点对安装UDP应用
 */
void InstallUdpApplication(Ptr<Node> srcNode, Ptr<Node> dstNode, 
                           uint16_t port, double startTime, double stopTime) {
    // 获取目的节点IP地址
    Ptr<Ipv4> ipv4 = dstNode->GetObject<Ipv4>();
    if (!ipv4) return;
    
    Ipv4Address dstAddr = ipv4->GetAddress(1, 0).GetLocal();
    
    // 创建UDP客户端
    UdpClientHelper client(dstAddr, port);
    client.SetAttribute("MaxPackets", UintegerValue(1000000));
    client.SetAttribute("Interval", TimeValue(Seconds(1.0 / g_config.packetRate)));
    client.SetAttribute("PacketSize", UintegerValue(g_config.packetSize));
    
    ApplicationContainer clientApp = client.Install(srcNode);
    clientApp.Start(Seconds(startTime));
    clientApp.Stop(Seconds(stopTime));
    
    // 创建UDP服务器
    UdpServerHelper server(port);
    ApplicationContainer serverApp = server.Install(dstNode);
    serverApp.Start(Seconds(startTime));
    serverApp.Stop(Seconds(stopTime));
}

/**
 * \brief 设置混合业务模式 - 保证每架UAV都有发送流量 (Fix: all-UAV coverage)
 */
void SetupMixedTraffic() {
    NS_LOG_INFO("设置业务流量 (OnOff CBR UDP, 全节点覆盖)...");
    
    uint16_t port = 9000;
    uint32_t n = g_uavNodes.GetN();
    
    // 关键修复: 每架无人机 i 都有2条发送流
    // 流1: i -> (i+1)%n  (相邻节点, 模拟近距离飞控通信)
    // 流2: i -> (i + n/2)%n (跨区节点, 模拟远程图传通信)
    // 这样保证每个 uavX_pdr 列都能被填写到非零数据
    for (uint32_t i = 0; i < n; ++i) {
        // 目的节点列表: 确保两个目标都不是自己
        uint32_t dst1 = (i + 1) % n;
        uint32_t dst2 = (i + n / 2) % n;
        if (dst2 == i) dst2 = (i + 2) % n; // 极端情况: n=2时规避自发自收
        
        uint32_t dsts[2] = {dst1, dst2};
        
        for (int k = 0; k < 2; ++k) {
            uint32_t j = dsts[k];
            
            Ptr<Ipv4> ipv4 = g_uavNodes.Get(j)->GetObject<Ipv4>();
            Ipv4Address dstAddr = ipv4->GetAddress(1, 0).GetLocal();
            
            // 接收端 (PacketSink) - 安装在目的节点上
            PacketSinkHelper sink("ns3::UdpSocketFactory", 
                                  InetSocketAddress(Ipv4Address::GetAny(), port));
            ApplicationContainer sinkApp = sink.Install(g_uavNodes.Get(j));
            // sinkApp.Start(Seconds(0.5));

            sinkApp.Start(Seconds(0.5));
            sinkApp.Stop(Seconds(g_config.duration));
            // 发送端 (OnOff CBR) - 安装在源节点 i 上
            OnOffHelper onoff("ns3::UdpSocketFactory", 
                              InetSocketAddress(dstAddr, port));
            onoff.SetAttribute("OnTime",  StringValue("ns3::ConstantRandomVariable[Constant=1]"));
            onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=0]"));
            // 每条流 ~100Kbps CBR
            uint32_t perFlowKbps = (uint32_t)(g_diffParams.trafficLoadMbps * 1000.0 / 2.0);
            perFlowKbps = std::max(perFlowKbps, (uint32_t)10);  // 最低 10kbps
            std::string flowRateStr = std::to_string(perFlowKbps) + "kb/s";
            onoff.SetAttribute("DataRate", DataRateValue(DataRate(flowRateStr)));

            onoff.SetAttribute("PacketSize", UintegerValue((uint32_t)g_config.packetSize));
            
            ApplicationContainer clientApp = onoff.Install(g_uavNodes.Get(i));
            // 每架飞机错开 0.1s 启动，彻底避免 ARP 广播风暴
            // clientApp.Start(Seconds(1.0 + i * 0.1));
            clientApp.Start(Seconds(2.0 + i * 0.05));
            clientApp.Stop(Seconds(g_config.duration));
            
            port++;
        }
    }
    
    // 强制路由表在发包前初始化 (非常关键)
    // Ipv4GlobalRoutingHelper::PopulateRoutingTables();
    
    NS_LOG_INFO("混合业务与路由设置完成，共 " << (g_uavNodes.GetN() * 2) << " 条流");
}

/**
 * 设置 TDMA 调度的业务流量
 *
 * 与 SetupMixedTraffic 的区别：
 *   - 发送端使用原始 Socket API + 定时突发，而非 OnOff 连续流
 *   - 只在分配的时隙内发送，消除 UAV 集群内部碰撞
 *   - 接收端仍使用 PacketSink（始终监听）
 *
 * 流量拓扑保持一致：每节点2条流
 *   流1: i → (i+1)%n  (近距离飞控)
 *   流2: i → (i+n/2)%n (远程图传)
 */
void SetupTDMATraffic() {
    NS_LOG_INFO("设置 TDMA 调度业务流量...");
    
    uint32_t n = g_uavNodes.GetN();
    uint16_t port = 9000;
    
    // ---- 1. 计算空间复用分组 ----
    ComputeTDMASlots();
    
    // ---- 2. 为每个节点创建发送 Socket 和接收 PacketSink ----
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t dst1 = (i + 1) % n;
        uint32_t dst2 = (i + n / 2) % n;
        if (dst2 == i) dst2 = (i + 2) % n;
        
        uint32_t dsts[2] = {dst1, dst2};
        
        for (int k = 0; k < 2; ++k) {
            uint32_t j = dsts[k];
            
            // 接收端: PacketSink（始终监听，无需TDMA控制）
            Ptr<Ipv4> dstIpv4 = g_uavNodes.Get(j)->GetObject<Ipv4>();
            Ipv4Address dstAddr = dstIpv4->GetAddress(1, 0).GetLocal();
            
            PacketSinkHelper sink("ns3::UdpSocketFactory",
                                  InetSocketAddress(Ipv4Address::GetAny(), port));
            ApplicationContainer sinkApp = sink.Install(g_uavNodes.Get(j));
            sinkApp.Start(Seconds(0.5));
            sinkApp.Stop(Seconds(g_config.duration));
            
            // 发送端: 原始 UDP Socket
            Ptr<Socket> socket = Socket::CreateSocket(
                g_uavNodes.Get(i), UdpSocketFactory::GetTypeId());
            socket->Bind();
            socket->Connect(InetSocketAddress(dstAddr, port));
            
            // 记录到 TDMA 管理器
            TDMAManager::FlowEntry flow;
            flow.dstId  = j;
            flow.socket = socket;
            g_tdma.nodeFlows[i].push_back(flow);
            
            port++;
        }
    }

    // 让 TDMA 业务启动时间随仿真时长自适应，避免短时烟雾测试还未发包就结束。
    // 约束：
    // 1. 接收端 0.5s 后已开始监听；
    // 2. 首次业务尽量在前 20% 时长内启动；
    // 3. 长时仿真也不必等到过晚才开始发包。
    g_tdma.trafficStartTime = std::clamp(g_config.duration * 0.2, 0.6, 1.5);
    
    // ---- 3. 调度每个节点的首次突发 ----
    // 每个节点在自己的时隙起始时刻开始第一次突发
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t mySlot = g_tdma.slotAssignment[i];
        double firstBurstTime = g_tdma.trafficStartTime + mySlot * g_tdma.slotDuration;
        
        Simulator::Schedule(Seconds(firstBurstTime), &TDMABurstSend, i);
    }

    //   在业务启动后等待一段时间收集 QoS 数据再开始调节
    double firstReallocationTime = g_tdma.trafficStartTime + g_tdma.reallocationInterval;
    if (firstReallocationTime < g_config.duration) {
        Simulator::Schedule(Seconds(firstReallocationTime), &DynamicTDMAReallocation);
        NS_LOG_INFO("  动态 TDMA 重分配已启用: 首次触发 @" << firstReallocationTime 
                    << "s, 间隔 " << g_tdma.reallocationInterval << "s");
    } else {
        NS_LOG_INFO("  仿真时长较短，跳过首次 TDMA 重分配调度 "
                    << "(trafficStart=" << g_tdma.trafficStartTime
                    << "s, duration=" << g_config.duration << "s)");
    }
    
    // ---- 4. 写入 TDMA 调度日志（初始状态）----
    if (g_tdmaLog.is_open()) {
        for (uint32_t i = 0; i < n; ++i) {
            std::string bonusStr = "none";
            g_tdmaLog << 0.0 << "," 
                      << i << ","
                      << g_tdma.slotAssignment[i] << ","
                      << g_tdma.numGroups << ","
                      << g_tdma.perNodePackets[i] << ","
                      << bonusStr << ","
                      << g_tdma.urgency[i] << "\n";
        }
    }
    
    NS_LOG_INFO("TDMA 业务设置完成: " << n << " 节点, " 
                << n * 2 << " 条流, "
                << g_tdma.numGroups << " 个时隙/帧, "
                << "帧周期 " << g_tdma.cycleDuration * 1000.0 << "ms, "
                << "基准 " << g_tdma.basePacketsPerSlot << " 包/时隙");
}

// ==================== 创建恶意干扰/黑飞节点 (Phase 4) ====================
