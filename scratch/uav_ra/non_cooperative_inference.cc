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
                              << (evidence.isMissing ? 1 : 0) << ","
                              << evidence.missingReason << "," << evidence.noiseLevel
                              << "," << evidence.sceneType << ","
                              << evidence.operationMode << "\n";
}

void AppendInferredTopologyEdge(const InferredTopologyEdge& edge)
{
    g_observationRuntime.inferredEdges.push_back(edge);
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

    using EdgeKey = std::pair<uint32_t, uint32_t>;
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
    if (windowBatch.empty() || evidenceBatch.empty())
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
        edge.inferenceMethod = "window_evidence_fusion_v1";
        edge.sceneType = evidence.sceneType;
        edge.operationMode = evidence.operationMode;
        edge.edgeProbability = Clamp01Local(0.40 * evidence.evidenceStrength +
                                            0.20 * evidence.observerAgreementScore +
                                            0.15 * evidence.edgeObservationConfidence +
                                            0.15 * observerCoverageFactor +
                                            0.10 * nodeSupportFactor);
        edge.edgeConfidence = Clamp01Local(0.55 * evidence.edgeObservationConfidence +
                                           0.25 * avgNodeConfidence +
                                           0.20 * evidence.observerAgreementScore);

        AppendInferredTopologyEdge(edge);
        generatedEdges.push_back(edge);
    }

    BuildGraphRepresentationForWindow(generatedEdges);
}
