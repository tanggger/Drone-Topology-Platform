#include "non_cooperative_attack.h"

#include <queue>

NS_LOG_COMPONENT_DEFINE("UAVResourceAllocationNonCooperativeAttack");

namespace
{

constexpr double kWindowEpsilon = 1e-6;
constexpr double kMinEdgeWeight = 1e-6;
constexpr double kAttackMonitorIntervalSec = 0.1;
constexpr double kPreAttackBaselineWindowSec = 4.0;
constexpr double kImmediatePostAttackWindowSec = 2.0;
constexpr double kUnreachableDelayPenaltyMs = 250.0;
constexpr double kEnemyTxPowerDbm = 30.0;

struct TruthGraphLink
{
    uint32_t neighborId = 0;
    double linkPdr = 0.0;
    double throughputMbps = 0.0;
    double delayMs = 0.0;
};

struct TruthGraph
{
    std::vector<uint32_t> nodeIds;
    std::map<uint32_t, size_t> nodeIndex;
    std::vector<std::vector<TruthGraphLink>> adjacency;
};

struct TruthGraphMetrics
{
    uint32_t nodeCount = 0;
    double connectivityRatio = std::numeric_limits<double>::quiet_NaN();
    double pdr = std::numeric_limits<double>::quiet_NaN();
    double throughputMbps = std::numeric_limits<double>::quiet_NaN();
    double delayMs = std::numeric_limits<double>::quiet_NaN();
};

bool ResolveObservedTargetBinding(uint32_t observedNodeId,
                                  NonCooperativeTargetBindingResult& bindingResult,
                                  bool persistResult);

struct LatestInferenceBatch
{
    double windowStart = std::numeric_limits<double>::quiet_NaN();
    double windowEnd = std::numeric_limits<double>::quiet_NaN();
    std::vector<InferredTopologyEdge> edges;
};

double Clamp01Local(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

double EstimateEnemyLinkRxPowerDbm(uint32_t srcId, uint32_t dstId)
{
    if (srcId >= g_interferenceNodes.GetN() || dstId >= g_interferenceNodes.GetN())
    {
        return -std::numeric_limits<double>::infinity();
    }

    const double distance = CalculateDistance(g_interferenceNodes.Get(srcId),
                                              g_interferenceNodes.Get(dstId));
    return kEnemyTxPowerDbm - CalculatePathLoss(std::max(1.0, distance));
}

double EstimateEnemyLinkPdr(double rxPowerDbm)
{
    const double margin = rxPowerDbm - g_config.rxSensitivity;
    return Clamp01Local((margin + 5.0) / 30.0);
}

double EstimateEnemyLinkThroughputMbps(double rxPowerDbm, double linkPdr)
{
    const double margin = std::max(0.0, rxPowerDbm - g_config.rxSensitivity);
    return std::max(0.0, std::min(6.0, 0.25 * margin) * linkPdr);
}

double EstimateEnemyLinkDelayMs(double linkPdr)
{
    return 5.0 + 35.0 * (1.0 - Clamp01Local(linkPdr));
}

TruthGraph BuildEnemyTruthGraph(const std::set<uint32_t>* allowedNodeIds = nullptr)
{
    TruthGraph graph;

    for (uint32_t nodeId = 0; nodeId < g_interferenceNodes.GetN(); ++nodeId)
    {
        if (allowedNodeIds && allowedNodeIds->count(nodeId) == 0)
        {
            continue;
        }
        graph.nodeIndex[nodeId] = graph.nodeIds.size();
        graph.nodeIds.push_back(nodeId);
    }

    graph.adjacency.resize(graph.nodeIds.size());
    for (size_t i = 0; i < graph.nodeIds.size(); ++i)
    {
        for (size_t j = i + 1; j < graph.nodeIds.size(); ++j)
        {
            const uint32_t srcId = graph.nodeIds[i];
            const uint32_t dstId = graph.nodeIds[j];
            if (IsNonCooperativeEntityNodeStruck(srcId) ||
                IsNonCooperativeEntityNodeStruck(dstId))
            {
                continue;
            }
            const double rxPowerDbm = EstimateEnemyLinkRxPowerDbm(srcId, dstId);
            const double linkPdr = EstimateEnemyLinkPdr(rxPowerDbm);
            if (linkPdr < 0.15)
            {
                continue;
            }

            TruthGraphLink a;
            a.neighborId = dstId;
            a.linkPdr = linkPdr;
            a.throughputMbps = EstimateEnemyLinkThroughputMbps(rxPowerDbm, linkPdr);
            a.delayMs = EstimateEnemyLinkDelayMs(linkPdr);
            graph.adjacency[i].push_back(a);

            TruthGraphLink b = a;
            b.neighborId = srcId;
            graph.adjacency[j].push_back(b);
        }
    }

    return graph;
}

std::set<uint32_t> BuildTargetNeighborhoodEntityNodes(uint32_t rootNodeId, uint32_t hopLimit)
{
    std::set<uint32_t> result;
    if (rootNodeId >= g_interferenceNodes.GetN())
    {
        return result;
    }

    const TruthGraph graph = BuildEnemyTruthGraph();
    auto rootIt = graph.nodeIndex.find(rootNodeId);
    if (rootIt == graph.nodeIndex.end())
    {
        return result;
    }

    std::queue<std::pair<uint32_t, uint32_t>> q;
    std::set<uint32_t> visited;
    q.push({rootNodeId, 0});
    visited.insert(rootNodeId);
    result.insert(rootNodeId);

    while (!q.empty())
    {
        const auto [nodeId, depth] = q.front();
        q.pop();
        if (depth >= hopLimit)
        {
            continue;
        }

        const size_t idx = graph.nodeIndex.at(nodeId);
        for (const auto& link : graph.adjacency[idx])
        {
            if (!visited.insert(link.neighborId).second)
            {
                continue;
            }
            result.insert(link.neighborId);
            q.push({link.neighborId, depth + 1});
        }
    }

    return result;
}

TruthGraphMetrics ComputeTruthGraphMetrics(const std::set<uint32_t>* allowedNodeIds = nullptr)
{
    const TruthGraph graph = BuildEnemyTruthGraph(allowedNodeIds);
    TruthGraphMetrics metrics;
    metrics.nodeCount = static_cast<uint32_t>(graph.nodeIds.size());

    if (graph.nodeIds.empty())
    {
        metrics.connectivityRatio = 0.0;
        metrics.pdr = 0.0;
        metrics.throughputMbps = 0.0;
        metrics.delayMs = kUnreachableDelayPenaltyMs;
        return metrics;
    }

    if (graph.nodeIds.size() == 1)
    {
        metrics.connectivityRatio = 1.0;
        metrics.pdr = 1.0;
        metrics.throughputMbps = 6.0;
        metrics.delayMs = 5.0;
        return metrics;
    }

    double reachablePairs = 0.0;
    double totalPairs = 0.0;
    double pdrSum = 0.0;
    double throughputSum = 0.0;
    double delaySum = 0.0;

    for (size_t source = 0; source < graph.nodeIds.size(); ++source)
    {
        const size_t n = graph.nodeIds.size();
        std::vector<double> distance(n, std::numeric_limits<double>::infinity());
        std::vector<int32_t> predecessor(n, -1);
        using QueueEntry = std::pair<double, size_t>;
        std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> pq;
        distance[source] = 0.0;
        pq.push({0.0, source});

        while (!pq.empty())
        {
            const auto [dist, nodeIndex] = pq.top();
            pq.pop();
            if (dist > distance[nodeIndex] + 1e-12)
            {
                continue;
            }

            for (const auto& link : graph.adjacency[nodeIndex])
            {
                const size_t neighborIndex = graph.nodeIndex.at(link.neighborId);
                const double candidate = dist + link.delayMs;
                if (candidate + 1e-12 < distance[neighborIndex])
                {
                    distance[neighborIndex] = candidate;
                    predecessor[neighborIndex] = static_cast<int32_t>(nodeIndex);
                    pq.push({candidate, neighborIndex});
                }
            }
        }

        for (size_t target = source + 1; target < graph.nodeIds.size(); ++target)
        {
            totalPairs += 1.0;
            if (!std::isfinite(distance[target]))
            {
                delaySum += kUnreachableDelayPenaltyMs;
                continue;
            }

            reachablePairs += 1.0;
            double pathPdr = 1.0;
            double pathThroughput = std::numeric_limits<double>::infinity();
            double pathDelay = 0.0;
            int32_t cursor = static_cast<int32_t>(target);
            while (cursor != static_cast<int32_t>(source) && cursor >= 0)
            {
                const int32_t pred = predecessor[cursor];
                if (pred < 0)
                {
                    pathPdr = 0.0;
                    pathThroughput = 0.0;
                    pathDelay = kUnreachableDelayPenaltyMs;
                    break;
                }

                const uint32_t predNodeId = graph.nodeIds[static_cast<size_t>(pred)];
                const uint32_t nodeId = graph.nodeIds[static_cast<size_t>(cursor)];
                const auto& links = graph.adjacency[static_cast<size_t>(pred)];
                auto it = std::find_if(links.begin(),
                                       links.end(),
                                       [&](const TruthGraphLink& link) {
                                           return link.neighborId == nodeId;
                                       });
                if (it == links.end())
                {
                    pathPdr = 0.0;
                    pathThroughput = 0.0;
                    pathDelay = kUnreachableDelayPenaltyMs;
                    break;
                }
                (void)predNodeId;
                pathPdr *= it->linkPdr;
                pathThroughput = std::min(pathThroughput, it->throughputMbps);
                pathDelay += it->delayMs;
                cursor = pred;
            }

            pdrSum += std::max(0.0, pathPdr);
            throughputSum += std::isfinite(pathThroughput) ? pathThroughput : 0.0;
            delaySum += pathDelay;
        }
    }

    metrics.connectivityRatio = totalPairs > 0.0 ? reachablePairs / totalPairs : 1.0;
    metrics.pdr = totalPairs > 0.0 ? pdrSum / totalPairs : 1.0;
    metrics.throughputMbps = totalPairs > 0.0 ? throughputSum / totalPairs : 0.0;
    metrics.delayMs = totalPairs > 0.0 ? delaySum / totalPairs : kUnreachableDelayPenaltyMs;
    return metrics;
}

std::string DetermineAttackPhase(double now, const NonCooperativeAttackPlan& plan)
{
    if (!plan.userTriggeredExecution || plan.attackExecuteTime < 0.0)
    {
        return "pre_attack";
    }
    if (now < plan.attackExecuteTime)
    {
        return "pre_attack";
    }
    if (now < plan.attackExecuteTime + kImmediatePostAttackWindowSec)
    {
        return "immediate_post_attack";
    }
    if (std::isfinite(plan.evaluationWindowEnd) && now + kWindowEpsilon >= plan.evaluationWindowEnd)
    {
        return "final";
    }
    return "recovery";
}

bool AttackMetricsWindowActive(double now, const NonCooperativeAttackPlan& plan)
{
    if (!plan.userTriggeredExecution || plan.attackExecuteTime < 0.0)
    {
        return false;
    }
    const double windowStart = std::max(0.0, plan.attackExecuteTime - kPreAttackBaselineWindowSec);
    const double windowEnd = std::isfinite(plan.evaluationWindowEnd)
                                 ? plan.evaluationWindowEnd
                                 : plan.attackExecuteTime;
    return now + kWindowEpsilon >= windowStart &&
           now <= windowEnd + kAttackMonitorIntervalSec + kWindowEpsilon;
}

TruthGraphMetrics ComputeBaselineMetricsFromHistory(const std::string& targetScope)
{
    TruthGraphMetrics baseline;
    double connectivitySum = 0.0;
    double pdrSum = 0.0;
    double throughputSum = 0.0;
    double delaySum = 0.0;
    uint32_t count = 0;
    for (const auto& sample : g_nonCooperativeAttackRuntime.effectMetrics)
    {
        if (sample.phase != "pre_attack" || sample.targetScope != targetScope)
        {
            continue;
        }
        connectivitySum += sample.connectivityRatio;
        pdrSum += sample.pdr;
        throughputSum += sample.throughputMbps;
        delaySum += sample.delayMs;
        count++;
    }

    if (count == 0)
    {
        return baseline;
    }

    baseline.nodeCount = count;
    baseline.connectivityRatio = connectivitySum / count;
    baseline.pdr = pdrSum / count;
    baseline.throughputMbps = throughputSum / count;
    baseline.delayMs = delaySum / count;
    return baseline;
}

double ComputeRecoveryProgress(const TruthGraphMetrics& baseline, const TruthGraphMetrics& current)
{
    if (!std::isfinite(baseline.connectivityRatio) || baseline.connectivityRatio <= 1e-9)
    {
        return std::numeric_limits<double>::quiet_NaN();
    }

    const double connectivityScore =
        Clamp01Local(current.connectivityRatio / std::max(1e-9, baseline.connectivityRatio));
    const double pdrScore = Clamp01Local(current.pdr / std::max(1e-9, baseline.pdr));
    const double throughputScore =
        Clamp01Local(current.throughputMbps / std::max(1e-9, baseline.throughputMbps));
    const double delayScore =
        Clamp01Local(std::max(1e-9, baseline.delayMs) / std::max(1e-9, current.delayMs));
    return (connectivityScore + pdrScore + throughputScore + delayScore) / 4.0;
}

void AppendAttackEffectSample(double now,
                              const std::string& phase,
                              const std::string& targetScope,
                              const TruthGraphMetrics& metrics,
                              double recoveryProgress)
{
    NonCooperativeAttackEffectMetric sample;
    sample.time = now;
    sample.phase = phase;
    sample.targetScope = targetScope;
    sample.connectivityRatio = metrics.connectivityRatio;
    sample.pdr = metrics.pdr;
    sample.throughputMbps = metrics.throughputMbps;
    sample.delayMs = metrics.delayMs;
    sample.recoveryProgress = recoveryProgress;
    sample.recommendedObservedNodeId =
        g_nonCooperativeAttackRuntime.currentRecommendedObservedNodeId;
    sample.confirmedObservedNodeId =
        g_nonCooperativeAttackRuntime.currentConfirmedObservedNodeId;
    sample.executedObservedNodeId =
        g_nonCooperativeAttackRuntime.executedObservedNodeId;

    if (!std::isnan(g_nonCooperativeAttackRuntime.actualAttackExecutionTime))
    {
        if (!std::isnan(g_nonCooperativeAttackRuntime.recoveryCompletedAt) &&
            now >= g_nonCooperativeAttackRuntime.recoveryCompletedAt)
        {
            sample.damageDuration =
                g_nonCooperativeAttackRuntime.recoveryCompletedAt -
                g_nonCooperativeAttackRuntime.actualAttackExecutionTime;
        }
        else
        {
            sample.damageDuration =
                std::max(0.0, now - g_nonCooperativeAttackRuntime.actualAttackExecutionTime);
        }
    }

    g_nonCooperativeAttackRuntime.effectMetrics.push_back(sample);
    if (g_nonCooperativeAttackEffectMetricsLog.is_open())
    {
        g_nonCooperativeAttackEffectMetricsLog
            << sample.time << "," << sample.phase << "," << sample.targetScope << ","
            << sample.connectivityRatio << "," << sample.pdr << ","
            << sample.throughputMbps << "," << sample.delayMs << ","
            << sample.damageDuration << "," << sample.recoveryProgress << ","
            << sample.recommendedObservedNodeId << ","
            << sample.confirmedObservedNodeId << ","
            << sample.executedObservedNodeId << "\n";
    }
}

void PrepareAttackNeighborhood(const NonCooperativeAttackPlan& plan)
{
    if (g_nonCooperativeAttackRuntime.targetNeighborhoodFrozen ||
        plan.confirmedObservedNodeId < 0)
    {
        return;
    }

    int32_t resolvedEntityNodeId = -1;
    NonCooperativeTargetBindingResult bindingResult;
    if (ResolveObservedTargetBinding(static_cast<uint32_t>(plan.confirmedObservedNodeId),
                                     bindingResult,
                                     false) &&
        bindingResult.executedEntityNodeId >= 0)
    {
        resolvedEntityNodeId = bindingResult.executedEntityNodeId;
    }
    else
    {
        const auto mapIt = g_observationRuntime.targetObjectByObservedTrackId.find(
            static_cast<uint32_t>(plan.confirmedObservedNodeId));
        if (mapIt == g_observationRuntime.targetObjectByObservedTrackId.end())
        {
            return;
        }
        const uint32_t targetObjectKey = mapIt->second;
        const int32_t derivedEntityNodeId =
            static_cast<int32_t>(targetObjectKey -
                                 GetTargetObjectKeyFromInterferenceIndex(0));
        if (derivedEntityNodeId < 0 ||
            static_cast<uint32_t>(derivedEntityNodeId) >= g_interferenceNodes.GetN())
        {
            return;
        }
        resolvedEntityNodeId = derivedEntityNodeId;
    }

    g_nonCooperativeAttackRuntime.targetNeighborhoodEntityNodeIds = BuildTargetNeighborhoodEntityNodes(
        static_cast<uint32_t>(resolvedEntityNodeId),
        std::max<uint32_t>(1,
                           g_environmentConfig.nonCooperativeAttackConfig.attackNeighborhoodHop));
    g_nonCooperativeAttackRuntime.targetNeighborhoodFrozen =
        !g_nonCooperativeAttackRuntime.targetNeighborhoodEntityNodeIds.empty();
}

LatestInferenceBatch CollectLatestInferenceBatch()
{
    LatestInferenceBatch batch;
    if (g_observationRuntime.inferredEdges.empty())
    {
        return batch;
    }

    double latestWindowEnd = -std::numeric_limits<double>::infinity();
    double latestWindowStart = std::numeric_limits<double>::quiet_NaN();
    for (const auto& edge : g_observationRuntime.inferredEdges)
    {
        if (std::isnan(edge.windowEnd))
        {
            continue;
        }
        if (edge.windowEnd > latestWindowEnd + kWindowEpsilon)
        {
            latestWindowEnd = edge.windowEnd;
            latestWindowStart = edge.windowStart;
        }
    }

    if (!std::isfinite(latestWindowEnd))
    {
        return batch;
    }

    batch.windowStart = latestWindowStart;
    batch.windowEnd = latestWindowEnd;
    for (const auto& edge : g_observationRuntime.inferredEdges)
    {
        if (std::isnan(edge.windowEnd) ||
            std::abs(edge.windowEnd - latestWindowEnd) > kWindowEpsilon)
        {
            continue;
        }
        if (!std::isnan(latestWindowStart) &&
            !std::isnan(edge.windowStart) &&
            std::abs(edge.windowStart - latestWindowStart) > kWindowEpsilon)
        {
            continue;
        }
        batch.edges.push_back(edge);
    }
    return batch;
}

std::vector<double> NormalizeMetric(const std::vector<double>& values)
{
    std::vector<double> normalized(values.size(), 0.0);
    if (values.empty())
    {
        return normalized;
    }

    double minValue = std::numeric_limits<double>::infinity();
    double maxValue = -std::numeric_limits<double>::infinity();
    for (double value : values)
    {
        if (!std::isfinite(value))
        {
            continue;
        }
        minValue = std::min(minValue, value);
        maxValue = std::max(maxValue, value);
    }

    if (!std::isfinite(minValue) || !std::isfinite(maxValue))
    {
        return normalized;
    }

    if (std::abs(maxValue - minValue) <= 1e-9)
    {
        const double fill = maxValue > 0.0 ? 1.0 : 0.0;
        std::fill(normalized.begin(), normalized.end(), fill);
        return normalized;
    }

    for (size_t i = 0; i < values.size(); ++i)
    {
        const double value = values[i];
        if (!std::isfinite(value))
        {
            normalized[i] = 0.0;
            continue;
        }
        normalized[i] = Clamp01Local((value - minValue) / (maxValue - minValue));
    }
    return normalized;
}

std::vector<double> ComputeWeightedDegree(
    const std::vector<std::vector<std::pair<size_t, double>>>& adjacency)
{
    std::vector<double> degree(adjacency.size(), 0.0);
    for (size_t i = 0; i < adjacency.size(); ++i)
    {
        for (const auto& [neighbor, weight] : adjacency[i])
        {
            (void)neighbor;
            degree[i] += weight;
        }
    }
    return degree;
}

std::vector<double> ComputeWeightedCloseness(
    const std::vector<std::vector<std::pair<size_t, double>>>& adjacency)
{
    const size_t n = adjacency.size();
    std::vector<double> closeness(n, 0.0);
    for (size_t source = 0; source < n; ++source)
    {
        std::vector<double> distance(n, std::numeric_limits<double>::infinity());
        using QueueEntry = std::pair<double, size_t>;
        std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> pq;
        distance[source] = 0.0;
        pq.push({0.0, source});

        while (!pq.empty())
        {
            const auto [dist, node] = pq.top();
            pq.pop();
            if (dist > distance[node] + 1e-12)
            {
                continue;
            }
            for (const auto& [neighbor, weight] : adjacency[node])
            {
                const double edgeCost = 1.0 / std::max(weight, kMinEdgeWeight);
                const double candidate = dist + edgeCost;
                if (candidate + 1e-12 < distance[neighbor])
                {
                    distance[neighbor] = candidate;
                    pq.push({candidate, neighbor});
                }
            }
        }

        double sumDistance = 0.0;
        uint32_t reachable = 0;
        for (size_t i = 0; i < n; ++i)
        {
            if (i == source || !std::isfinite(distance[i]))
            {
                continue;
            }
            sumDistance += distance[i];
            reachable++;
        }
        if (reachable > 0 && sumDistance > 1e-9)
        {
            closeness[source] = static_cast<double>(reachable) / sumDistance;
        }
    }
    return closeness;
}

std::vector<double> ComputeWeightedPageRank(
    const std::vector<std::vector<std::pair<size_t, double>>>& adjacency)
{
    const size_t n = adjacency.size();
    if (n == 0)
    {
        return {};
    }

    std::vector<double> rank(n, 1.0 / static_cast<double>(n));
    std::vector<double> nextRank(n, 0.0);
    std::vector<double> outWeight(n, 0.0);
    for (size_t i = 0; i < n; ++i)
    {
        for (const auto& [neighbor, weight] : adjacency[i])
        {
            (void)neighbor;
            outWeight[i] += weight;
        }
    }

    constexpr double damping = 0.85;
    for (uint32_t iter = 0; iter < 20; ++iter)
    {
        std::fill(nextRank.begin(), nextRank.end(), (1.0 - damping) / static_cast<double>(n));
        double sinkContribution = 0.0;
        for (size_t i = 0; i < n; ++i)
        {
            if (outWeight[i] <= 1e-9)
            {
                sinkContribution += rank[i];
                continue;
            }
            for (const auto& [neighbor, weight] : adjacency[i])
            {
                nextRank[neighbor] += damping * rank[i] * (weight / outWeight[i]);
            }
        }
        const double sinkShare = damping * sinkContribution / static_cast<double>(n);
        for (double& value : nextRank)
        {
            value += sinkShare;
        }
        rank.swap(nextRank);
    }
    return rank;
}

std::vector<double> ComputeWeightedBetweenness(
    const std::vector<std::vector<std::pair<size_t, double>>>& adjacency)
{
    const size_t n = adjacency.size();
    std::vector<double> betweenness(n, 0.0);

    for (size_t source = 0; source < n; ++source)
    {
        std::vector<std::vector<size_t>> predecessors(n);
        std::vector<double> sigma(n, 0.0);
        std::vector<double> distance(n, std::numeric_limits<double>::infinity());
        std::vector<size_t> stack;
        stack.reserve(n);

        using QueueEntry = std::pair<double, size_t>;
        std::priority_queue<QueueEntry, std::vector<QueueEntry>, std::greater<QueueEntry>> pq;
        sigma[source] = 1.0;
        distance[source] = 0.0;
        pq.push({0.0, source});

        while (!pq.empty())
        {
            const auto [dist, node] = pq.top();
            pq.pop();
            if (dist > distance[node] + 1e-12)
            {
                continue;
            }
            stack.push_back(node);
            for (const auto& [neighbor, weight] : adjacency[node])
            {
                const double edgeCost = 1.0 / std::max(weight, kMinEdgeWeight);
                const double candidate = dist + edgeCost;
                if (candidate + 1e-12 < distance[neighbor])
                {
                    distance[neighbor] = candidate;
                    pq.push({candidate, neighbor});
                    predecessors[neighbor].clear();
                    predecessors[neighbor].push_back(node);
                    sigma[neighbor] = sigma[node];
                }
                else if (std::abs(candidate - distance[neighbor]) <= 1e-12)
                {
                    predecessors[neighbor].push_back(node);
                    sigma[neighbor] += sigma[node];
                }
            }
        }

        std::vector<double> dependency(n, 0.0);
        while (!stack.empty())
        {
            const size_t node = stack.back();
            stack.pop_back();
            for (size_t pred : predecessors[node])
            {
                if (sigma[node] <= 1e-12)
                {
                    continue;
                }
                dependency[pred] += (sigma[pred] / sigma[node]) * (1.0 + dependency[node]);
            }
            if (node != source)
            {
                betweenness[node] += dependency[node];
            }
        }
    }

    for (double& value : betweenness)
    {
        value *= 0.5;
    }
    return betweenness;
}

std::vector<double> ComputeKShell(
    const std::vector<std::vector<std::pair<size_t, double>>>& adjacency)
{
    const size_t n = adjacency.size();
    std::vector<uint32_t> degree(n, 0);
    std::vector<bool> removed(n, false);
    std::vector<double> shellIndex(n, 0.0);
    for (size_t i = 0; i < n; ++i)
    {
        degree[i] = static_cast<uint32_t>(adjacency[i].size());
    }

    uint32_t remaining = static_cast<uint32_t>(n);
    uint32_t currentShell = 1;
    while (remaining > 0)
    {
        bool removedAny = false;
        std::queue<size_t> q;
        for (size_t i = 0; i < n; ++i)
        {
            if (!removed[i] && degree[i] <= currentShell)
            {
                q.push(i);
            }
        }

        while (!q.empty())
        {
            const size_t node = q.front();
            q.pop();
            if (removed[node])
            {
                continue;
            }
            removed[node] = true;
            removedAny = true;
            remaining--;
            shellIndex[node] = static_cast<double>(currentShell);
            for (const auto& [neighbor, weight] : adjacency[node])
            {
                (void)weight;
                if (removed[neighbor] || degree[neighbor] == 0)
                {
                    continue;
                }
                degree[neighbor]--;
                if (degree[neighbor] <= currentShell)
                {
                    q.push(neighbor);
                }
            }
        }

        if (!removedAny)
        {
            currentShell++;
        }
    }
    return shellIndex;
}

void PopulateRecommendationsFromLatestInference()
{
    g_nonCooperativeAttackRuntime.recommendations.clear();
    g_nonCooperativeAttackRuntime.recommendationAvailable = false;
    g_nonCooperativeAttackRuntime.currentRecommendedObservedNodeId = -1;
    g_nonCooperativeAttackRuntime.lastRecommendationReason = "no_inference_batch";

    const LatestInferenceBatch batch = CollectLatestInferenceBatch();
    if (batch.edges.empty())
    {
        return;
    }

    std::map<uint32_t, size_t> nodeIndex;
    std::vector<uint32_t> nodes;
    for (const auto& edge : batch.edges)
    {
        for (uint32_t observedNodeId : {edge.srcObservedNodeId, edge.dstObservedNodeId})
        {
            if (nodeIndex.count(observedNodeId))
            {
                continue;
            }
            nodeIndex[observedNodeId] = nodes.size();
            nodes.push_back(observedNodeId);
        }
    }
    if (nodes.empty())
    {
        return;
    }

    std::vector<std::vector<std::pair<size_t, double>>> adjacency(nodes.size());
    for (const auto& edge : batch.edges)
    {
        const size_t src = nodeIndex.at(edge.srcObservedNodeId);
        const size_t dst = nodeIndex.at(edge.dstObservedNodeId);
        const double weight =
            std::max(kMinEdgeWeight, edge.edgeProbability * std::max(0.1, edge.edgeConfidence));
        adjacency[src].push_back({dst, weight});
        adjacency[dst].push_back({src, weight});
    }

    const std::vector<double> degree = ComputeWeightedDegree(adjacency);
    const std::vector<double> betweenness = ComputeWeightedBetweenness(adjacency);
    const std::vector<double> closeness = ComputeWeightedCloseness(adjacency);
    const std::vector<double> pagerank = ComputeWeightedPageRank(adjacency);
    const std::vector<double> kshell = ComputeKShell(adjacency);

    const std::vector<double> degreeNorm = NormalizeMetric(degree);
    const std::vector<double> betweennessNorm = NormalizeMetric(betweenness);
    const std::vector<double> closenessNorm = NormalizeMetric(closeness);
    const std::vector<double> pagerankNorm = NormalizeMetric(pagerank);
    const std::vector<double> kshellNorm = NormalizeMetric(kshell);

    std::vector<NonCooperativeAttackRecommendation> recommendations;
    recommendations.reserve(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        NonCooperativeAttackRecommendation rec;
        rec.windowStart = batch.windowStart;
        rec.windowEnd = batch.windowEnd;
        rec.recommendedObservedNodeId = nodes[i];
        rec.weightedDegreeCentrality = degreeNorm[i];
        rec.weightedBetweennessCentrality = betweennessNorm[i];
        rec.weightedClosenessCentrality = closenessNorm[i];
        rec.weightedPageRank = pagerankNorm[i];
        rec.weightedKShell = kshellNorm[i];
        rec.recommendedScore =
            (degreeNorm[i] + betweennessNorm[i] + closenessNorm[i] + pagerankNorm[i] +
             kshellNorm[i]) /
            5.0;
        rec.recommendationReason =
            "equal_weight_fusion(degree|betweenness|closeness|pagerank|kshell)";
        recommendations.push_back(rec);
    }

    std::sort(recommendations.begin(),
              recommendations.end(),
              [](const NonCooperativeAttackRecommendation& lhs,
                 const NonCooperativeAttackRecommendation& rhs) {
                  if (std::abs(lhs.recommendedScore - rhs.recommendedScore) > 1e-9)
                  {
                      return lhs.recommendedScore > rhs.recommendedScore;
                  }
                  return lhs.recommendedObservedNodeId < rhs.recommendedObservedNodeId;
              });

    for (size_t i = 0; i < recommendations.size(); ++i)
    {
        recommendations[i].recommendationRank = static_cast<uint32_t>(i + 1);
        if (g_nonCooperativeAttackRecommendationsLog.is_open())
        {
            const auto& rec = recommendations[i];
            g_nonCooperativeAttackRecommendationsLog
                << rec.windowStart << "," << rec.windowEnd << ","
                << rec.recommendedObservedNodeId << "," << rec.recommendedScore << ","
                << rec.recommendationRank << "," << rec.recommendationReason << ","
                << rec.inferenceMethod << "," << rec.weightedDegreeCentrality << ","
                << rec.weightedBetweennessCentrality << ","
                << rec.weightedClosenessCentrality << "," << rec.weightedPageRank << ","
                << rec.weightedKShell << "\n";
        }
    }

    g_nonCooperativeAttackRuntime.recommendations = recommendations;
    g_nonCooperativeAttackRuntime.recommendationAvailable = !recommendations.empty();
    g_nonCooperativeAttackRuntime.lastRecommendationUpdateTime =
        Simulator::Now().GetSeconds();
    if (!recommendations.empty())
    {
        g_nonCooperativeAttackRuntime.currentRecommendedObservedNodeId =
            static_cast<int32_t>(recommendations.front().recommendedObservedNodeId);
        g_nonCooperativeAttackRuntime.lastRecommendationReason =
            recommendations.front().recommendationReason;
    }
}

bool NonCooperativeAttackEnabled()
{
    return g_environmentConfig.operationMode == OperationMode::NonCooperative &&
           g_environmentConfig.nonCooperativeAttackConfig.enabled;
}

bool ApplyNodeStrikeToEntity(uint32_t interferenceNodeId)
{
    if (interferenceNodeId >= g_interferenceNodes.GetN())
    {
        return false;
    }

    Ptr<Node> node = g_interferenceNodes.Get(interferenceNodeId);
    if (!node)
    {
        return false;
    }

    Ptr<Ipv4> ipv4 = node->GetObject<Ipv4>();
    if (ipv4)
    {
        for (uint32_t ifIndex = 1; ifIndex < ipv4->GetNInterfaces(); ++ifIndex)
        {
            ipv4->SetDown(ifIndex);
        }
    }

    const Time now = Simulator::Now();
    for (uint32_t appIndex = 0; appIndex < node->GetNApplications(); ++appIndex)
    {
        Ptr<Application> app = node->GetApplication(appIndex);
        if (!app)
        {
            continue;
        }
        app->SetStopTime(now);
    }

    return true;
}

void AppendAttackExecutionEvent(const NonCooperativeTargetBindingResult& bindingResult,
                                bool nodeRemoved)
{
    NonCooperativeAttackEvent event;
    event.eventTime = Simulator::Now().GetSeconds();
    event.attackType =
        NonCooperativeAttackTypeToString(g_environmentConfig.nonCooperativeAttackConfig.attackType);
    event.recommendedObservedNodeId =
        g_nonCooperativeAttackRuntime.currentRecommendedObservedNodeId;
    event.confirmedObservedNodeId = g_nonCooperativeAttackRuntime.currentConfirmedObservedNodeId;
    event.executedObservedNodeId = static_cast<int32_t>(bindingResult.observedNodeId);
    event.targetBindingStatus = bindingResult.bindingStatus;
    event.isTrueTargetHit = bindingResult.isTrueCriticalTarget;
    event.targetMismatchType = bindingResult.isTrueCriticalTarget
                                   ? "critical_target_hit"
                                   : bindingResult.mismatchType;
    event.nodeRemoved = nodeRemoved;
    event.executedEntityNodeId = bindingResult.executedEntityNodeId;
    event.boundTargetObjectKey = bindingResult.boundTargetObjectKey;
    g_nonCooperativeAttackRuntime.attackEvents.push_back(event);
    if (g_nonCooperativeAttackEventsLog.is_open())
    {
        g_nonCooperativeAttackEventsLog
            << event.eventTime << "," << event.attackType << ","
            << event.recommendedObservedNodeId << ","
            << event.confirmedObservedNodeId << ","
            << event.executedObservedNodeId << "," << event.targetBindingStatus << ","
            << (event.isTrueTargetHit ? 1 : 0) << ","
            << event.targetMismatchType << "," << (event.nodeRemoved ? 1 : 0) << ","
            << event.executedEntityNodeId << "," << event.boundTargetObjectKey << "\n";
    }
}

void ExecuteConfiguredNodeStrike()
{
    if (!NonCooperativeAttackEnabled() || g_nonCooperativeAttackRuntime.attackExecuted)
    {
        return;
    }

    NonCooperativeAttackPlan plan;
    if (!BuildCurrentNonCooperativeAttackPlan(plan) || !plan.userTriggeredExecution ||
        plan.confirmedObservedNodeId < 0)
    {
        return;
    }

    NonCooperativeTargetBindingResult bindingResult;
    const bool bindingOk = TryBindObservedTargetForStrike(
        static_cast<uint32_t>(plan.confirmedObservedNodeId), bindingResult);

    g_nonCooperativeAttackRuntime.attackPlan.targetBindingStatus = bindingResult.bindingStatus;
    g_nonCooperativeAttackRuntime.attackPlan.confirmedObservedNodeId =
        plan.confirmedObservedNodeId;
    g_nonCooperativeAttackRuntime.attackPlan.recommendedObservedNodeId =
        plan.recommendedObservedNodeId;

    if (!bindingOk || bindingResult.executedEntityNodeId < 0)
    {
        AppendAttackExecutionEvent(bindingResult, false);
        g_nonCooperativeAttackRuntime.attackExecuted = true;
        g_nonCooperativeAttackRuntime.executedObservedNodeId =
            static_cast<int32_t>(bindingResult.observedNodeId);
        g_nonCooperativeAttackRuntime.actualAttackExecutionTime =
            Simulator::Now().GetSeconds();
        g_nonCooperativeAttackRuntime.attackPlan.strikeExecuteTime =
            g_nonCooperativeAttackRuntime.actualAttackExecutionTime;
        g_nonCooperativeAttackRuntime.attackPlan.targetBindingStatus =
            bindingResult.bindingStatus;
        return;
    }

    const uint32_t observedNodeId = bindingResult.observedNodeId;
    const uint32_t targetObjectKey = static_cast<uint32_t>(bindingResult.boundTargetObjectKey);
    const uint32_t entityNodeId = static_cast<uint32_t>(bindingResult.executedEntityNodeId);
    if (g_nonCooperativeAttackRuntime.targetNeighborhoodEntityNodeIds.empty())
    {
        g_nonCooperativeAttackRuntime.targetNeighborhoodEntityNodeIds =
            BuildTargetNeighborhoodEntityNodes(
                entityNodeId,
                std::max<uint32_t>(1,
                                   g_environmentConfig.nonCooperativeAttackConfig.attackNeighborhoodHop));
    }
    const bool nodeRemoved = ApplyNodeStrikeToEntity(entityNodeId);

    g_nonCooperativeAttackRuntime.attackExecuted = true;
    g_nonCooperativeAttackRuntime.executedObservedNodeId =
        static_cast<int32_t>(observedNodeId);
    g_nonCooperativeAttackRuntime.executedTargetObjectKey =
        static_cast<int32_t>(targetObjectKey);
    g_nonCooperativeAttackRuntime.executedEntityNodeId =
        static_cast<int32_t>(entityNodeId);
    g_nonCooperativeAttackRuntime.actualAttackExecutionTime =
        Simulator::Now().GetSeconds();
    g_nonCooperativeAttackRuntime.attackPlan.strikeExecuteTime =
        g_nonCooperativeAttackRuntime.actualAttackExecutionTime;
    g_nonCooperativeAttackRuntime.attackPlan.targetBindingStatus =
        bindingResult.bindingStatus;

    if (nodeRemoved)
    {
        g_nonCooperativeAttackRuntime.struckObservedNodeIds.insert(observedNodeId);
        g_nonCooperativeAttackRuntime.struckTargetObjectKeys.insert(targetObjectKey);
        g_nonCooperativeAttackRuntime.struckEntityNodeIds.insert(entityNodeId);
    }

    AppendAttackExecutionEvent(bindingResult, nodeRemoved);
}

bool ResolveObservedTargetBinding(uint32_t observedNodeId,
                                  NonCooperativeTargetBindingResult& bindingResult,
                                  bool persistResult)
{
    bindingResult = NonCooperativeTargetBindingResult();
    bindingResult.eventTime = Simulator::Now().GetSeconds();
    bindingResult.observedNodeId = observedNodeId;

    const auto mapIt =
        g_observationRuntime.targetObjectByObservedTrackId.find(observedNodeId);
    if (mapIt == g_observationRuntime.targetObjectByObservedTrackId.end())
    {
        bindingResult.bindingStatus = "missing_track";
        bindingResult.mismatchType = "track_not_found";
        if (persistResult)
        {
            g_nonCooperativeAttackRuntime.targetBindings.push_back(bindingResult);
        }
        return false;
    }

    bindingResult.boundTargetObjectKey = static_cast<int32_t>(mapIt->second);
    bindingResult.executedEntityNodeId =
        static_cast<int32_t>(mapIt->second - GetTargetObjectKeyFromInterferenceIndex(0));

    bool anyStable = false;
    bool anyActive = false;
    double bestConfidence = 0.0;
    for (const auto& [observerId, tracks] : g_observationRuntime.trackStatesByObserver)
    {
        (void)observerId;
        auto it = tracks.find(observedNodeId);
        if (it == tracks.end())
        {
            continue;
        }
        anyStable = anyStable || it->second.isStable;
        anyActive = anyActive || it->second.isActive;
        bestConfidence = std::max(bestConfidence, it->second.overallConfidence);
    }

    bindingResult.isTrackStable = anyStable;
    bindingResult.isTrackActive = anyActive;
    bindingResult.bindingConfidence = bestConfidence;
    bindingResult.isTrueCriticalTarget =
        !g_observationRuntime.keyNodeCandidates.empty() &&
        g_observationRuntime.keyNodeCandidates.front().observedNodeId == observedNodeId;

    bool success = false;
    if (anyStable && anyActive)
    {
        bindingResult.bindingStatus = "binding_success";
        bindingResult.mismatchType = "not_evaluated";
        success = true;
    }
    else if (!anyActive)
    {
        bindingResult.bindingStatus = "stale_track";
        bindingResult.mismatchType = "track_inactive";
    }
    else
    {
        bindingResult.bindingStatus = "unstable_track";
        bindingResult.mismatchType = "track_not_stable";
    }

    if (persistResult)
    {
        g_nonCooperativeAttackRuntime.targetBindings.push_back(bindingResult);
        if (g_nonCooperativeTargetBindingLog.is_open())
        {
            g_nonCooperativeTargetBindingLog
                << bindingResult.eventTime << "," << bindingResult.observedNodeId << ","
                << bindingResult.bindingStatus << "," << bindingResult.bindingConfidence
                << "," << (bindingResult.isTrackStable ? 1 : 0) << ","
                << (bindingResult.isTrackActive ? 1 : 0) << ","
                << bindingResult.boundTargetObjectKey << ","
                << bindingResult.executedEntityNodeId << ","
                << (bindingResult.isTrueCriticalTarget ? 1 : 0) << ","
                << bindingResult.mismatchType << "\n";
        }
    }
    return success;
}

} // namespace

void InitializeNonCooperativeAttackState()
{
    g_nonCooperativeAttackRuntime = NonCooperativeAttackRuntimeState();
    g_nonCooperativeAttackRuntime.initialized = true;
    g_nonCooperativeAttackRuntime.attackPlan.operationMode = "non_cooperative";
    g_nonCooperativeAttackRuntime.attackPlan.sceneType = g_environmentSummary.sceneType;
    g_nonCooperativeAttackRuntime.attackPlan.attackType =
        g_environmentSummary.nonCooperativeAttackType;
}

void UpdateNonCooperativeAttackRecommendations()
{
    if (!NonCooperativeAttackEnabled())
    {
        return;
    }

    PopulateRecommendationsFromLatestInference();
    Simulator::Schedule(Seconds(g_environmentConfig.observationPreset.windowDurationSec),
                        &UpdateNonCooperativeAttackRecommendations);
}

bool BuildCurrentNonCooperativeAttackPlan(NonCooperativeAttackPlan& plan)
{
    if (g_environmentConfig.operationMode != OperationMode::NonCooperative)
    {
        return false;
    }
    if (!g_environmentConfig.nonCooperativeAttackConfig.enabled)
    {
        return false;
    }

    const auto& cfg = g_environmentConfig.nonCooperativeAttackConfig;
    const NonCooperativeAttackPlan previousPlan = g_nonCooperativeAttackRuntime.attackPlan;
    plan = previousPlan;
    plan.operationMode = "non_cooperative";
    plan.sceneType = g_environmentSummary.sceneType;
    plan.attackType = NonCooperativeAttackTypeToString(cfg.attackType);
    plan.recommendedObservedNodeId =
        g_nonCooperativeAttackRuntime.currentRecommendedObservedNodeId;
    plan.confirmedObservedNodeId =
        cfg.manualStrikeTarget >= 0 ? cfg.manualStrikeTarget : plan.recommendedObservedNodeId;
    plan.userTriggeredExecution =
        cfg.manualStrikeTarget >= 0 && cfg.attackExecuteTime >= 0.0;
    plan.attackExecuteTime = cfg.attackExecuteTime;
    if (!g_nonCooperativeAttackRuntime.attackExecuted)
    {
        plan.targetBindingStatus =
            plan.userTriggeredExecution ? "pending_binding" : "execution_not_requested";
        plan.strikeExecuteTime = std::numeric_limits<double>::quiet_NaN();
    }
    if (cfg.attackExecuteTime >= 0.0)
    {
        plan.evaluationWindowStart = cfg.attackExecuteTime;
        plan.evaluationWindowEnd = cfg.attackExecuteTime + cfg.attackEvaluationDuration;
    }
    g_nonCooperativeAttackRuntime.attackPlan = plan;
    g_nonCooperativeAttackRuntime.currentConfirmedObservedNodeId =
        plan.confirmedObservedNodeId;
    return true;
}

bool TryBindObservedTargetForStrike(uint32_t observedNodeId,
                                    NonCooperativeTargetBindingResult& bindingResult)
{
    return ResolveObservedTargetBinding(observedNodeId, bindingResult, true);
}

void MonitorNonCooperativeAttackExecution()
{
    if (!NonCooperativeAttackEnabled())
    {
        return;
    }

    NonCooperativeAttackPlan plan;
    if (BuildCurrentNonCooperativeAttackPlan(plan) && plan.userTriggeredExecution &&
        !g_nonCooperativeAttackRuntime.attackExecuted)
    {
        PrepareAttackNeighborhood(plan);
        if (plan.attackExecuteTime >= 0.0 &&
            Simulator::Now().GetSeconds() + kWindowEpsilon >= plan.attackExecuteTime)
        {
            ExecuteConfiguredNodeStrike();
        }
    }

    if (Simulator::Now().GetSeconds() + kAttackMonitorIntervalSec < g_config.duration)
    {
        Simulator::Schedule(Seconds(kAttackMonitorIntervalSec),
                            &MonitorNonCooperativeAttackExecution);
    }
}

void MonitorNonCooperativeAttackEffectMetrics()
{
    if (!NonCooperativeAttackEnabled())
    {
        return;
    }

    const double now = Simulator::Now().GetSeconds();
    NonCooperativeAttackPlan plan;
    if (!BuildCurrentNonCooperativeAttackPlan(plan) || !AttackMetricsWindowActive(now, plan))
    {
        if (now + kAttackMonitorIntervalSec < g_config.duration)
        {
            Simulator::Schedule(Seconds(kAttackMonitorIntervalSec),
                                &MonitorNonCooperativeAttackEffectMetrics);
        }
        return;
    }

    PrepareAttackNeighborhood(plan);

    const std::string phase = DetermineAttackPhase(now, plan);
    const TruthGraphMetrics globalMetrics = ComputeTruthGraphMetrics();
    const TruthGraphMetrics globalBaseline = ComputeBaselineMetricsFromHistory("global");
    const double globalRecoveryProgress = ComputeRecoveryProgress(globalBaseline, globalMetrics);
    AppendAttackEffectSample(now, phase, "global", globalMetrics, globalRecoveryProgress);

    if (!g_nonCooperativeAttackRuntime.targetNeighborhoodEntityNodeIds.empty())
    {
        const TruthGraphMetrics neighborhoodMetrics =
            ComputeTruthGraphMetrics(&g_nonCooperativeAttackRuntime.targetNeighborhoodEntityNodeIds);
        const TruthGraphMetrics neighborhoodBaseline =
            ComputeBaselineMetricsFromHistory("target_neighborhood");
        const double neighborhoodRecoveryProgress =
            ComputeRecoveryProgress(neighborhoodBaseline, neighborhoodMetrics);
        AppendAttackEffectSample(now,
                                 phase,
                                 "target_neighborhood",
                                 neighborhoodMetrics,
                                 neighborhoodRecoveryProgress);

        if (phase == "recovery" &&
            std::isnan(g_nonCooperativeAttackRuntime.recoveryCompletedAt) &&
            std::isfinite(globalRecoveryProgress) &&
            std::isfinite(neighborhoodRecoveryProgress) &&
            globalRecoveryProgress >= 0.95 &&
            neighborhoodRecoveryProgress >= 0.95)
        {
            g_nonCooperativeAttackRuntime.recoveryCompletedAt = now;
        }
    }

    if (phase == "final")
    {
        g_nonCooperativeAttackRuntime.finalEffectSampleRecorded = true;
    }

    if (!g_nonCooperativeAttackRuntime.finalEffectSampleRecorded &&
        now + kAttackMonitorIntervalSec < g_config.duration)
    {
        Simulator::Schedule(Seconds(kAttackMonitorIntervalSec),
                            &MonitorNonCooperativeAttackEffectMetrics);
    }
}

bool IsNonCooperativeObservedTrackStruck(uint32_t observedNodeId)
{
    return g_nonCooperativeAttackRuntime.struckObservedNodeIds.count(observedNodeId) > 0;
}

bool IsNonCooperativeTargetObjectStruck(uint32_t targetObjectKey)
{
    return g_nonCooperativeAttackRuntime.struckTargetObjectKeys.count(targetObjectKey) > 0;
}

bool IsNonCooperativeEntityNodeStruck(uint32_t interferenceNodeId)
{
    return g_nonCooperativeAttackRuntime.struckEntityNodeIds.count(interferenceNodeId) > 0;
}
