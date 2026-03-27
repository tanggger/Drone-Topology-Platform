#include "context.h"

NS_LOG_COMPONENT_DEFINE("UAVResourceAllocationNonCooperativeInference");

namespace
{

struct EdgeContribution
{
    double evidenceStrength = 0.0;
    double commCount = 0.0;
    double commDurationTotal = 0.0;
    double avgRxPowerDbm = 0.0;
    double confidence = 0.0;
    double noiseLevel = 0.0;
    double laggedPredictiveScoreForward = 0.0;
    double laggedPredictiveScoreBackward = 0.0;
    double directedResponseScoreForward = 0.0;
    double directedResponseScoreBackward = 0.0;
    double excitationScoreForward = 0.0;
    double excitationScoreBackward = 0.0;
    uint32_t observerId = 0;
    uint32_t channelId = 0;
    double centerFrequencyHz = std::numeric_limits<double>::quiet_NaN();
};

struct EdgeAggregate
{
    double windowStart = std::numeric_limits<double>::quiet_NaN();
    double windowEnd = std::numeric_limits<double>::quiet_NaN();
    uint32_t srcObservedNodeId = 0;
    uint32_t dstObservedNodeId = 0;
    std::vector<EdgeContribution> contributions;
    std::string sceneType;
    std::string operationMode;
};

struct NodeWindowSupport
{
    uint32_t observerCount = 0;
    double confidenceSum = 0.0;
    double activeRatioSum = 0.0;
};

double Clamp01Local(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

double AverageDirectionalCausalSupport(double lagged, double response, double excitation)
{
    return Clamp01Local((lagged + response + excitation) / 3.0);
}

double ComputeDirectionalityScore(double forwardSupport, double backwardSupport)
{
    return Clamp01Local(std::abs(forwardSupport - backwardSupport));
}

std::string DetermineDominantDirection(double forwardSupport, double backwardSupport)
{
    if (forwardSupport < 0.08 && backwardSupport < 0.08)
    {
        return "undetermined";
    }
    if (std::abs(forwardSupport - backwardSupport) <= 0.06)
    {
        return "bidirectional";
    }
    return forwardSupport > backwardSupport ? "src_to_dst" : "dst_to_src";
}

double ComputeStateSequenceOverlapScore(const std::string& a, const std::string& b)
{
    const size_t n = std::min(a.size(), b.size());
    if (n == 0)
    {
        return 0.0;
    }

    uint32_t overlap = 0;
    uint32_t unionCount = 0;
    for (size_t i = 0; i < n; ++i)
    {
        bool ai = a[i] == '1';
        bool bi = b[i] == '1';
        if (ai && bi)
        {
            overlap++;
        }
        if (ai || bi)
        {
            unionCount++;
        }
    }

    if (unionCount == 0)
    {
        return 0.0;
    }
    return static_cast<double>(overlap) / unionCount;
}

bool IsActiveState(char value)
{
    return value == '1';
}

double ComputeLaggedPredictiveScoreDirected(const std::string& source,
                                            const std::string& target,
                                            size_t lag = 1)
{
    const size_t n = std::min(source.size(), target.size());
    if (n <= lag)
    {
        return 0.0;
    }

    uint32_t activeCount = 0;
    uint32_t inactiveCount = 0;
    uint32_t activeFollowedByTarget = 0;
    uint32_t inactiveFollowedByTarget = 0;

    for (size_t i = lag; i < n; ++i)
    {
        const bool srcPrevActive = IsActiveState(source[i - lag]);
        const bool dstNowActive = IsActiveState(target[i]);
        if (srcPrevActive)
        {
            activeCount++;
            if (dstNowActive)
            {
                activeFollowedByTarget++;
            }
        }
        else
        {
            inactiveCount++;
            if (dstNowActive)
            {
                inactiveFollowedByTarget++;
            }
        }
    }

    if (activeCount == 0)
    {
        return 0.0;
    }

    const double pActive =
        static_cast<double>(activeFollowedByTarget) / static_cast<double>(activeCount);
    const double pInactive =
        inactiveCount > 0
            ? static_cast<double>(inactiveFollowedByTarget) / static_cast<double>(inactiveCount)
            : 0.0;
    return Clamp01Local(std::max(0.0, pActive - pInactive));
}

double ComputeDirectedResponseScoreDirected(const std::string& source,
                                            const std::string& target,
                                            size_t horizon = 2)
{
    const size_t n = std::min(source.size(), target.size());
    if (n < 2)
    {
        return 0.0;
    }

    uint32_t activationCount = 0;
    uint32_t responseCount = 0;
    uint32_t targetActiveCount = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (IsActiveState(target[i]))
        {
            targetActiveCount++;
        }
    }

    for (size_t i = 0; i < n; ++i)
    {
        const bool prevActive = i > 0 ? IsActiveState(source[i - 1]) : false;
        const bool nowActive = IsActiveState(source[i]);
        if (!nowActive || prevActive)
        {
            continue;
        }

        activationCount++;
        bool responded = false;
        for (size_t lag = 1; lag <= horizon && i + lag < n; ++lag)
        {
            if (IsActiveState(target[i + lag]))
            {
                responded = true;
                break;
            }
        }
        if (responded)
        {
            responseCount++;
        }
    }

    if (activationCount == 0)
    {
        return 0.0;
    }

    const double responseRate =
        static_cast<double>(responseCount) / static_cast<double>(activationCount);
    const double baselineRate =
        static_cast<double>(targetActiveCount) / static_cast<double>(n);
    return Clamp01Local(std::max(0.0, responseRate - baselineRate));
}

double ComputeExcitationScoreDirected(const std::string& source,
                                      const std::string& target,
                                      size_t horizon = 3)
{
    const size_t n = std::min(source.size(), target.size());
    if (n < 2)
    {
        return 0.0;
    }

    uint32_t sourceActiveCount = 0;
    double weightedResponse = 0.0;
    double maxPerActivation = 0.0;
    for (size_t lag = 1; lag <= horizon; ++lag)
    {
        maxPerActivation += 1.0 / static_cast<double>(lag);
    }

    for (size_t i = 0; i < n; ++i)
    {
        if (!IsActiveState(source[i]))
        {
            continue;
        }
        sourceActiveCount++;
        for (size_t lag = 1; lag <= horizon && i + lag < n; ++lag)
        {
            if (IsActiveState(target[i + lag]))
            {
                weightedResponse += 1.0 / static_cast<double>(lag);
            }
        }
    }

    if (sourceActiveCount == 0 || maxPerActivation <= 0.0)
    {
        return 0.0;
    }

    const double score =
        weightedResponse / (static_cast<double>(sourceActiveCount) * maxPerActivation);
    return Clamp01Local(score);
}

using EdgeKey = std::pair<uint32_t, uint32_t>;

EdgeKey MakeEdgeKey(uint32_t a, uint32_t b)
{
    return {std::min(a, b), std::max(a, b)};
}

double ComputeDirectCausalSupport(const InferredTopologyEdge& edge)
{
    return std::max(AverageDirectionalCausalSupport(edge.laggedPredictiveScoreForward,
                                                    edge.directedResponseScoreForward,
                                                    edge.excitationScoreForward),
                    AverageDirectionalCausalSupport(edge.laggedPredictiveScoreBackward,
                                                    edge.directedResponseScoreBackward,
                                                    edge.excitationScoreBackward));
}

void UpdateEdgeDynamicState(InferredTopologyEdge& edge,
                            const InferredTopologyEdge* previousEdge,
                            bool isCarryDecay)
{
    const double currentEvidence = edge.edgeProbability;
    if (previousEdge == nullptr)
    {
        edge.posteriorEdgeProbability = currentEvidence;
        edge.stabilityAge = currentEvidence >= 0.62 ? 1u : 0u;
        edge.weakeningAge = currentEvidence < 0.28 ? 1u : 0u;
    }
    else
    {
        const double previousPosterior =
            previousEdge->posteriorEdgeProbability > 1e-9 ? previousEdge->posteriorEdgeProbability
                                                          : previousEdge->edgeProbability;
        edge.posteriorEdgeProbability =
            Clamp01Local(0.68 * currentEvidence + 0.32 * previousPosterior);

        if (edge.posteriorEdgeProbability >= 0.60 && currentEvidence >= 0.46)
        {
            edge.stabilityAge = previousEdge->stabilityAge + 1;
        }
        else if (edge.posteriorEdgeProbability >= 0.45)
        {
            edge.stabilityAge = previousEdge->stabilityAge;
        }
        else
        {
            edge.stabilityAge = 0;
        }

        if (edge.posteriorEdgeProbability < 0.34 || currentEvidence < 0.22)
        {
            edge.weakeningAge = previousEdge->weakeningAge + 1;
        }
        else if (edge.posteriorEdgeProbability < 0.45)
        {
            edge.weakeningAge = previousEdge->weakeningAge;
        }
        else
        {
            edge.weakeningAge = 0;
        }
    }

    if (edge.weakeningAge >= 2 && (edge.posteriorEdgeProbability < 0.24 || isCarryDecay))
    {
        edge.edgeDynamicState = "vanished";
    }
    else if (edge.stabilityAge >= 2 && edge.posteriorEdgeProbability >= 0.58)
    {
        edge.edgeDynamicState = "stable";
    }
    else if (edge.posteriorEdgeProbability >= 0.40)
    {
        edge.edgeDynamicState = "emerging";
    }
    else
    {
        edge.edgeDynamicState = "weakening";
    }
}

void AppendObservedLinkEvidence(const ObservedLinkEvidence& evidence)
{
    g_observationRuntime.linkEvidence.push_back(evidence);
    if (!g_observedLinkEvidenceLog.is_open())
    {
        return;
    }

    auto writeOrNaN = [&](double value) {
        if (std::isnan(value))
        {
            g_observedLinkEvidenceLog << "nan";
        }
        else
        {
            g_observedLinkEvidenceLog << value;
        }
    };

    writeOrNaN(evidence.windowStart);
    g_observedLinkEvidenceLog << ",";
    writeOrNaN(evidence.windowEnd);
    g_observedLinkEvidenceLog << "," << evidence.srcObservedNodeId << ","
                              << evidence.dstObservedNodeId << ","
                              << evidence.evidenceStrength << ",";
    writeOrNaN(evidence.commCount);
    g_observedLinkEvidenceLog << ",";
    writeOrNaN(evidence.commDurationTotal);
    g_observedLinkEvidenceLog << ",";
    writeOrNaN(evidence.avgRxPowerDbm);
    g_observedLinkEvidenceLog << "," << evidence.channelId << ",";
    writeOrNaN(evidence.centerFrequencyHz);
    g_observedLinkEvidenceLog << "," << evidence.observerCount << ","
                              << evidence.observerAgreementScore << ","
                              << evidence.edgeObservationConfidence << ","
                              << evidence.laggedPredictiveScoreForward << ","
                              << evidence.laggedPredictiveScoreBackward << ","
                              << evidence.directedResponseScoreForward << ","
                              << evidence.directedResponseScoreBackward << ","
                              << evidence.excitationScoreForward << ","
                              << evidence.excitationScoreBackward << ","
                              << evidence.laggedPredictiveScore << ","
                              << evidence.directedResponseScore << ","
                              << evidence.excitationScore << ","
                              << evidence.directionalityScore << ","
                              << evidence.dominantDirection << ","
                              << (evidence.isMissing ? 1 : 0) << ","
                              << evidence.missingReason << "," << evidence.noiseLevel
                              << "," << evidence.sceneType << ","
                              << evidence.operationMode << "\n";
}

std::string DetermineFalseLinkSuppressionReasonBasic(const InferredTopologyEdge& edge)
{
    const double causalSupport = ComputeDirectCausalSupport(edge);

    if (edge.edgeConfidence < 0.38 || edge.edgeProbability < 0.42)
    {
        return "low_edge_confidence";
    }
    if (edge.edgeDynamicState == "vanished")
    {
        return "vanished_edge_state";
    }
    if (causalSupport < 0.10)
    {
        return "low_causality";
    }
    if (edge.edgeConfidence < 0.46 && edge.temporalContinuityScore < 0.20)
    {
        return "low_continuity";
    }
    if (edge.edgeConfidence < 0.44 && edge.directedResponseScore < 0.08 &&
        edge.excitationScore < 0.12)
    {
        return "weak_observer_agreement";
    }
    return "";
}

double ComputeTwoHopExplanationStrength(const InferredTopologyEdge& a,
                                        const InferredTopologyEdge& b)
{
    return Clamp01Local(0.5 * (ComputeDirectCausalSupport(a) + ComputeDirectCausalSupport(b)));
}

std::string DetermineConditionalSuppressionReason(
    const InferredTopologyEdge& edge,
    const std::map<EdgeKey, InferredTopologyEdge>& candidateEdgesByKey,
    int32_t& mediatorNodeId)
{
    mediatorNodeId = -1;
    const double directSupport =
        Clamp01Local(0.7 * ComputeDirectCausalSupport(edge) + 0.3 * edge.posteriorEdgeProbability);
    if (directSupport >= 0.58 || edge.edgeProbability >= 0.72)
    {
        return "";
    }

    const uint32_t src = edge.srcObservedNodeId;
    const uint32_t dst = edge.dstObservedNodeId;
    for (const auto& [candidateKey, candidateEdge] : candidateEdgesByKey)
    {
        (void)candidateKey;
        for (uint32_t mediator : {candidateEdge.srcObservedNodeId, candidateEdge.dstObservedNodeId})
        {
            if (mediator == src || mediator == dst)
            {
                continue;
            }

            auto leftIt = candidateEdgesByKey.find(MakeEdgeKey(src, mediator));
            auto rightIt = candidateEdgesByKey.find(MakeEdgeKey(mediator, dst));
            if (leftIt == candidateEdgesByKey.end() || rightIt == candidateEdgesByKey.end())
            {
                continue;
            }

            const double twoHopSupport =
                ComputeTwoHopExplanationStrength(leftIt->second, rightIt->second);
            if (twoHopSupport < 0.54)
            {
                continue;
            }

            mediatorNodeId = static_cast<int32_t>(mediator);
            if (edge.directionalityScore < 0.08 && twoHopSupport > directSupport * 1.25)
            {
                return "shared_cause_suspected";
            }
            if (ComputeDirectCausalSupport(edge) < 0.18 && twoHopSupport > directSupport * 1.15)
            {
                return "indirect_path_explained";
            }
        }
    }
    return "";
}

void AppendInferredTopologyEdge(const InferredTopologyEdge& edge, bool persistToRuntime = true)
{
    if (persistToRuntime)
    {
        g_observationRuntime.inferredEdges.push_back(edge);
    }
    if (!g_inferredTopologyEdgesLog.is_open())
    {
        return;
    }

    auto writeOrNaN = [&](double value) {
        if (std::isnan(value))
        {
            g_inferredTopologyEdgesLog << "nan";
        }
        else
        {
            g_inferredTopologyEdgesLog << value;
        }
    };

    writeOrNaN(edge.windowStart);
    g_inferredTopologyEdgesLog << ",";
    writeOrNaN(edge.windowEnd);
    g_inferredTopologyEdgesLog << "," << edge.srcObservedNodeId << ","
                               << edge.dstObservedNodeId << ","
                               << edge.edgeProbability << ","
                               << edge.edgeConfidence << ","
                               << edge.laggedPredictiveScoreForward << ","
                               << edge.laggedPredictiveScoreBackward << ","
                               << edge.directedResponseScoreForward << ","
                               << edge.directedResponseScoreBackward << ","
                               << edge.excitationScoreForward << ","
                               << edge.excitationScoreBackward << ","
                               << edge.laggedPredictiveScore << ","
                               << edge.directedResponseScore << ","
                               << edge.excitationScore << ","
                               << edge.directionalityScore << ","
                               << edge.dominantDirection << ","
                               << edge.temporalContinuityScore << ","
                               << edge.posteriorEdgeProbability << ","
                               << edge.edgeDynamicState << ","
                               << edge.stabilityAge << ","
                               << edge.weakeningAge << ","
                               << edge.edgeStage << ","
                               << edge.falseLinkSuppressionReason << ","
                               << edge.suppressionMediatorObservedNodeId << ","
                               << edge.inferenceMethod << ","
                               << edge.sceneType << ","
                               << edge.operationMode << "\n";
}

} // namespace

void BuildObservedLinkEvidenceForWindow(const std::vector<ObservedCommWindow>& windowBatch)
{
    if (windowBatch.empty())
    {
        return;
    }

    std::map<uint32_t, std::vector<const ObservedCommWindow*>> windowsByObserver;
    for (const auto& window : windowBatch)
    {
        if (!window.signalDetected)
        {
            continue;
        }
        windowsByObserver[window.observerId].push_back(&window);
    }

    std::map<EdgeKey, EdgeAggregate> edgeAggregates;
    std::vector<ObservedLinkEvidence> generatedEvidenceBatch;

    for (const auto& [observerId, observerWindows] : windowsByObserver)
    {
        (void)observerId;
        for (size_t i = 0; i < observerWindows.size(); ++i)
        {
            for (size_t j = i + 1; j < observerWindows.size(); ++j)
            {
                const ObservedCommWindow& a = *observerWindows[i];
                const ObservedCommWindow& b = *observerWindows[j];
                if (a.observedNodeId == b.observedNodeId)
                {
                    continue;
                }
                if (a.channelId != b.channelId)
                {
                    continue;
                }

                const double overlapScore =
                    ComputeStateSequenceOverlapScore(a.stateSequence, b.stateSequence);
                if (overlapScore <= 0.0)
                {
                    continue;
                }

                const uint32_t srcId = std::min(a.observedNodeId, b.observedNodeId);
                const uint32_t dstId = std::max(a.observedNodeId, b.observedNodeId);
                const EdgeKey edgeKey{srcId, dstId};

                EdgeAggregate& aggregate = edgeAggregates[edgeKey];
                aggregate.windowStart = a.windowStart;
                aggregate.windowEnd = a.windowEnd;
                aggregate.srcObservedNodeId = srcId;
                aggregate.dstObservedNodeId = dstId;
                aggregate.sceneType = a.sceneType;
                aggregate.operationMode = a.operationMode;

                EdgeContribution contribution;
                contribution.observerId = a.observerId;
                contribution.channelId = a.channelId;
                contribution.centerFrequencyHz = a.centerFrequencyHz;
                contribution.evidenceStrength =
                    Clamp01Local(std::min(a.activeRatio, b.activeRatio) * overlapScore);
                contribution.commCount = std::min(a.txCount, b.txCount);
                contribution.commDurationTotal = std::min(a.txDuration, b.txDuration);
                contribution.avgRxPowerDbm = (a.avgRxPowerDbm + b.avgRxPowerDbm) * 0.5;
                contribution.confidence =
                    Clamp01Local((a.overallConfidence + b.overallConfidence) * 0.5);
                contribution.noiseLevel = std::max(a.noiseLevel, b.noiseLevel);
                const double laggedAB =
                    ComputeLaggedPredictiveScoreDirected(a.stateSequence, b.stateSequence);
                const double laggedBA =
                    ComputeLaggedPredictiveScoreDirected(b.stateSequence, a.stateSequence);
                contribution.laggedPredictiveScoreForward = laggedAB;
                contribution.laggedPredictiveScoreBackward = laggedBA;

                const double responseAB =
                    ComputeDirectedResponseScoreDirected(a.stateSequence, b.stateSequence);
                const double responseBA =
                    ComputeDirectedResponseScoreDirected(b.stateSequence, a.stateSequence);
                contribution.directedResponseScoreForward = responseAB;
                contribution.directedResponseScoreBackward = responseBA;

                const double excitationAB =
                    ComputeExcitationScoreDirected(a.stateSequence, b.stateSequence);
                const double excitationBA =
                    ComputeExcitationScoreDirected(b.stateSequence, a.stateSequence);
                contribution.excitationScoreForward = excitationAB;
                contribution.excitationScoreBackward = excitationBA;
                aggregate.contributions.push_back(contribution);
            }
        }
    }

    for (const auto& [edgeKey, aggregate] : edgeAggregates)
    {
        (void)edgeKey;
        if (aggregate.contributions.empty())
        {
            continue;
        }

        ObservedLinkEvidence evidence;
        evidence.windowStart = aggregate.windowStart;
        evidence.windowEnd = aggregate.windowEnd;
        evidence.srcObservedNodeId = aggregate.srcObservedNodeId;
        evidence.dstObservedNodeId = aggregate.dstObservedNodeId;
        evidence.sceneType = aggregate.sceneType;
        evidence.operationMode = aggregate.operationMode;
        evidence.observerCount = aggregate.contributions.size();
        evidence.channelId = aggregate.contributions.front().channelId;
        evidence.centerFrequencyHz = aggregate.contributions.front().centerFrequencyHz;
        evidence.isMissing = false;

        double evidenceStrengthSum = 0.0;
        double commCountSum = 0.0;
        double commDurationSum = 0.0;
        double avgRxPowerSum = 0.0;
        double confidenceSum = 0.0;
        double noiseLevelSum = 0.0;
        double laggedPredictiveScoreForwardSum = 0.0;
        double laggedPredictiveScoreBackwardSum = 0.0;
        double directedResponseScoreForwardSum = 0.0;
        double directedResponseScoreBackwardSum = 0.0;
        double excitationScoreForwardSum = 0.0;
        double excitationScoreBackwardSum = 0.0;
        double minStrength = std::numeric_limits<double>::max();
        double maxStrength = 0.0;

        for (const auto& contribution : aggregate.contributions)
        {
            evidenceStrengthSum += contribution.evidenceStrength;
            commCountSum += contribution.commCount;
            commDurationSum += contribution.commDurationTotal;
            avgRxPowerSum += contribution.avgRxPowerDbm;
            confidenceSum += contribution.confidence;
            noiseLevelSum += contribution.noiseLevel;
            laggedPredictiveScoreForwardSum += contribution.laggedPredictiveScoreForward;
            laggedPredictiveScoreBackwardSum += contribution.laggedPredictiveScoreBackward;
            directedResponseScoreForwardSum += contribution.directedResponseScoreForward;
            directedResponseScoreBackwardSum += contribution.directedResponseScoreBackward;
            excitationScoreForwardSum += contribution.excitationScoreForward;
            excitationScoreBackwardSum += contribution.excitationScoreBackward;
            minStrength = std::min(minStrength, contribution.evidenceStrength);
            maxStrength = std::max(maxStrength, contribution.evidenceStrength);
        }

        const double contributionCount =
            static_cast<double>(aggregate.contributions.size());
        evidence.evidenceStrength = evidenceStrengthSum / contributionCount;
        evidence.commCount = commCountSum / contributionCount;
        evidence.commDurationTotal = commDurationSum / contributionCount;
        evidence.avgRxPowerDbm = avgRxPowerSum / contributionCount;
        evidence.noiseLevel = noiseLevelSum / contributionCount;
        evidence.laggedPredictiveScoreForward = laggedPredictiveScoreForwardSum / contributionCount;
        evidence.laggedPredictiveScoreBackward =
            laggedPredictiveScoreBackwardSum / contributionCount;
        evidence.directedResponseScoreForward =
            directedResponseScoreForwardSum / contributionCount;
        evidence.directedResponseScoreBackward =
            directedResponseScoreBackwardSum / contributionCount;
        evidence.excitationScoreForward = excitationScoreForwardSum / contributionCount;
        evidence.excitationScoreBackward = excitationScoreBackwardSum / contributionCount;
        evidence.laggedPredictiveScore =
            std::max(evidence.laggedPredictiveScoreForward, evidence.laggedPredictiveScoreBackward);
        evidence.directedResponseScore =
            std::max(evidence.directedResponseScoreForward, evidence.directedResponseScoreBackward);
        evidence.excitationScore =
            std::max(evidence.excitationScoreForward, evidence.excitationScoreBackward);
        const double forwardSupport = AverageDirectionalCausalSupport(
            evidence.laggedPredictiveScoreForward,
            evidence.directedResponseScoreForward,
            evidence.excitationScoreForward);
        const double backwardSupport = AverageDirectionalCausalSupport(
            evidence.laggedPredictiveScoreBackward,
            evidence.directedResponseScoreBackward,
            evidence.excitationScoreBackward);
        evidence.directionalityScore = ComputeDirectionalityScore(forwardSupport, backwardSupport);
        evidence.dominantDirection = DetermineDominantDirection(forwardSupport, backwardSupport);

        if (aggregate.contributions.size() == 1)
        {
            evidence.observerAgreementScore = 1.0;
        }
        else
        {
            const double denom = std::max(1e-9, maxStrength);
            evidence.observerAgreementScore =
                Clamp01Local(1.0 - (maxStrength - minStrength) / denom);
        }

        const double avgConfidence = confidenceSum / contributionCount;
        evidence.edgeObservationConfidence =
            Clamp01Local(avgConfidence * (0.5 + 0.5 * evidence.observerAgreementScore));

        AppendObservedLinkEvidence(evidence);
        generatedEvidenceBatch.push_back(evidence);
    }

    BuildInferredTopologyEdgesForWindow(windowBatch, generatedEvidenceBatch);
}

void BuildInferredTopologyEdgesForWindow(
    const std::vector<ObservedCommWindow>& windowBatch,
    const std::vector<ObservedLinkEvidence>& evidenceBatch)
{
    if (windowBatch.empty())
    {
        return;
    }

    std::map<uint32_t, NodeWindowSupport> nodeSupports;
    std::set<uint32_t> allObservers;
    std::vector<InferredTopologyEdge> generatedEdges;
    for (const auto& window : windowBatch)
    {
        allObservers.insert(window.observerId);
        if (!window.signalDetected)
        {
            continue;
        }

        NodeWindowSupport& support = nodeSupports[window.observedNodeId];
        support.observerCount++;
        support.confidenceSum += window.overallConfidence;
        support.activeRatioSum += window.activeRatio;
    }

    const double totalObserverCount =
        std::max<size_t>(1, allObservers.size());

    const double windowStart = windowBatch.front().windowStart;
    const double windowEnd = windowBatch.front().windowEnd;
    const std::string sceneType = windowBatch.front().sceneType;
    const std::string operationMode = windowBatch.front().operationMode;

    for (const auto& evidence : evidenceBatch)
    {
        auto srcIt = nodeSupports.find(evidence.srcObservedNodeId);
        auto dstIt = nodeSupports.find(evidence.dstObservedNodeId);
        if (srcIt == nodeSupports.end() || dstIt == nodeSupports.end())
        {
            continue;
        }

        const NodeWindowSupport& srcSupport = srcIt->second;
        const NodeWindowSupport& dstSupport = dstIt->second;

        const double srcObserverCoverage =
            Clamp01Local(srcSupport.observerCount / totalObserverCount);
        const double dstObserverCoverage =
            Clamp01Local(dstSupport.observerCount / totalObserverCount);
        const double nodeSupportFactor =
            Clamp01Local((srcObserverCoverage + dstObserverCoverage) * 0.5);

        const double srcConfidence =
            srcSupport.observerCount > 0
                ? srcSupport.confidenceSum / srcSupport.observerCount
                : 0.0;
        const double dstConfidence =
            dstSupport.observerCount > 0
                ? dstSupport.confidenceSum / dstSupport.observerCount
                : 0.0;
        const double avgNodeConfidence =
            Clamp01Local((srcConfidence + dstConfidence) * 0.5);

        const double observerCoverageFactor =
            Clamp01Local(evidence.observerCount / totalObserverCount);

        InferredTopologyEdge edge;
        edge.windowStart = evidence.windowStart;
        edge.windowEnd = evidence.windowEnd;
        edge.srcObservedNodeId = evidence.srcObservedNodeId;
        edge.dstObservedNodeId = evidence.dstObservedNodeId;
        edge.laggedPredictiveScoreForward = evidence.laggedPredictiveScoreForward;
        edge.laggedPredictiveScoreBackward = evidence.laggedPredictiveScoreBackward;
        edge.directedResponseScoreForward = evidence.directedResponseScoreForward;
        edge.directedResponseScoreBackward = evidence.directedResponseScoreBackward;
        edge.excitationScoreForward = evidence.excitationScoreForward;
        edge.excitationScoreBackward = evidence.excitationScoreBackward;
        edge.laggedPredictiveScore = evidence.laggedPredictiveScore;
        edge.directedResponseScore = evidence.directedResponseScore;
        edge.excitationScore = evidence.excitationScore;
        edge.directionalityScore = evidence.directionalityScore;
        edge.dominantDirection = evidence.dominantDirection;
        const EdgeKey edgeKey = MakeEdgeKey(edge.srcObservedNodeId, edge.dstObservedNodeId);
        auto previousIt = g_observationRuntime.previousInferredEdgeByPair.find(edgeKey);
        const bool hadPrevious = previousIt != g_observationRuntime.previousInferredEdgeByPair.end();
        edge.temporalContinuityScore =
            hadPrevious
                ? (previousIt->second.posteriorEdgeProbability > 1e-9
                       ? previousIt->second.posteriorEdgeProbability
                       : previousIt->second.edgeProbability)
                : 0.0;
        edge.inferenceMethod = "directed_dynamic_causal_graph_v3";
        edge.sceneType = evidence.sceneType;
        edge.operationMode = evidence.operationMode;
        edge.edgeProbability = Clamp01Local(0.28 * evidence.evidenceStrength +
                                            0.16 * evidence.observerAgreementScore +
                                            0.12 * evidence.edgeObservationConfidence +
                                            0.10 * observerCoverageFactor +
                                            0.08 * nodeSupportFactor +
                                            0.10 * evidence.laggedPredictiveScore +
                                            0.06 * evidence.directedResponseScore +
                                            0.04 * evidence.excitationScore +
                                            0.06 * evidence.directionalityScore);
        edge.edgeConfidence = Clamp01Local(0.40 * evidence.edgeObservationConfidence +
                                           0.18 * avgNodeConfidence +
                                           0.14 * evidence.observerAgreementScore +
                                           0.10 * evidence.laggedPredictiveScore +
                                           0.06 * evidence.directedResponseScore +
                                           0.05 * evidence.excitationScore +
                                           0.07 * evidence.directionalityScore);
        UpdateEdgeDynamicState(edge, hadPrevious ? &previousIt->second : nullptr, false);
        edge.edgeProbability = edge.posteriorEdgeProbability;

        generatedEdges.push_back(edge);
    }

    std::set<EdgeKey> presentEdgeKeys;
    for (const auto& edge : generatedEdges)
    {
        presentEdgeKeys.insert(MakeEdgeKey(edge.srcObservedNodeId, edge.dstObservedNodeId));
    }

    for (const auto& [edgeKey, previousEdge] : g_observationRuntime.previousInferredEdgeByPair)
    {
        if (presentEdgeKeys.count(edgeKey) > 0)
        {
            continue;
        }
        if (previousEdge.edgeProbability < 0.35)
        {
            continue;
        }

        InferredTopologyEdge carry = previousEdge;
        carry.windowStart = windowStart;
        carry.windowEnd = windowEnd;
        carry.sceneType = sceneType;
        carry.operationMode = operationMode;
        carry.temporalContinuityScore =
            previousEdge.posteriorEdgeProbability > 1e-9 ? previousEdge.posteriorEdgeProbability
                                                         : previousEdge.edgeProbability;
        carry.edgeProbability = Clamp01Local(previousEdge.edgeProbability * 0.60);
        carry.edgeConfidence = Clamp01Local(previousEdge.edgeConfidence * 0.75);
        carry.inferenceMethod = "directed_dynamic_causal_graph_v3_decay";
        UpdateEdgeDynamicState(carry, &previousEdge, true);
        carry.edgeProbability = carry.posteriorEdgeProbability;
        generatedEdges.push_back(carry);
    }

    std::map<EdgeKey, InferredTopologyEdge> candidateEdgesByKey;
    for (const auto& edge : generatedEdges)
    {
        candidateEdgesByKey[MakeEdgeKey(edge.srcObservedNodeId, edge.dstObservedNodeId)] = edge;
    }

    std::map<EdgeKey, InferredTopologyEdge> nextPreviousEdges;
    std::vector<InferredTopologyEdge> finalEdges;
    for (auto edge : generatedEdges)
    {
        const EdgeKey edgeKey = MakeEdgeKey(edge.srcObservedNodeId, edge.dstObservedNodeId);
        edge.edgeStage = "candidate";
        edge.falseLinkSuppressionReason.clear();
        edge.suppressionMediatorObservedNodeId = -1;
        AppendInferredTopologyEdge(edge, false);

        std::string suppressionReason = DetermineFalseLinkSuppressionReasonBasic(edge);
        int32_t mediatorNodeId = -1;
        if (suppressionReason.empty())
        {
            suppressionReason =
                DetermineConditionalSuppressionReason(edge, candidateEdgesByKey, mediatorNodeId);
        }
        if (!suppressionReason.empty())
        {
            InferredTopologyEdge filtered = edge;
            filtered.edgeStage = "filtered_out";
            filtered.falseLinkSuppressionReason = suppressionReason;
            filtered.suppressionMediatorObservedNodeId = mediatorNodeId;
            AppendInferredTopologyEdge(filtered, false);
            continue;
        }

        edge.edgeStage = "final";
        edge.falseLinkSuppressionReason.clear();
        edge.suppressionMediatorObservedNodeId = -1;
        nextPreviousEdges[edgeKey] = edge;
        AppendInferredTopologyEdge(edge, true);
        finalEdges.push_back(edge);
    }
    g_observationRuntime.previousInferredEdgeByPair = std::move(nextPreviousEdges);

    BuildGraphRepresentationForWindow(finalEdges);
}
