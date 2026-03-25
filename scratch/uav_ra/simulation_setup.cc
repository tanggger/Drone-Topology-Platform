#include "context.h"

NS_LOG_COMPONENT_DEFINE("UAVResourceAllocationSetup");

namespace ns3
{

class ForestOverlayPropagationLossModel : public PropagationLossModel
{
  public:
    static TypeId GetTypeId();

  private:
    double DoCalcRxPower(double txPowerDbm,
                         Ptr<MobilityModel> a,
                         Ptr<MobilityModel> b) const override;
    int64_t DoAssignStreams(int64_t stream) override;
};

class WaterSurfaceOverlayPropagationLossModel : public PropagationLossModel
{
  public:
    static TypeId GetTypeId();

  private:
    double DoCalcRxPower(double txPowerDbm,
                         Ptr<MobilityModel> a,
                         Ptr<MobilityModel> b) const override;
    int64_t DoAssignStreams(int64_t stream) override;
};

class UrbanAltitudeAdaptivePropagationLossModel : public PropagationLossModel
{
  public:
    static TypeId GetTypeId();

  private:
    double DoCalcRxPower(double txPowerDbm,
                         Ptr<MobilityModel> a,
                         Ptr<MobilityModel> b) const override;
    int64_t DoAssignStreams(int64_t stream) override;
};

NS_OBJECT_ENSURE_REGISTERED(ForestOverlayPropagationLossModel);
NS_OBJECT_ENSURE_REGISTERED(WaterSurfaceOverlayPropagationLossModel);
NS_OBJECT_ENSURE_REGISTERED(UrbanAltitudeAdaptivePropagationLossModel);

TypeId
ForestOverlayPropagationLossModel::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::ForestOverlayPropagationLossModel")
            .SetParent<PropagationLossModel>()
            .SetGroupName("Propagation")
            .AddConstructor<ForestOverlayPropagationLossModel>();
    return tid;
}

TypeId
WaterSurfaceOverlayPropagationLossModel::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::WaterSurfaceOverlayPropagationLossModel")
            .SetParent<PropagationLossModel>()
            .SetGroupName("Propagation")
            .AddConstructor<WaterSurfaceOverlayPropagationLossModel>();
    return tid;
}

TypeId
UrbanAltitudeAdaptivePropagationLossModel::GetTypeId()
{
    static TypeId tid =
        TypeId("ns3::UrbanAltitudeAdaptivePropagationLossModel")
            .SetParent<PropagationLossModel>()
            .SetGroupName("Propagation")
            .AddConstructor<UrbanAltitudeAdaptivePropagationLossModel>();
    return tid;
}

namespace
{

bool PointInPolygon2d(const Vector& point, const std::vector<Vector>& polygon)
{
    if (polygon.size() < 3)
    {
        return false;
    }

    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++)
    {
        const Vector& pi = polygon[i];
        const Vector& pj = polygon[j];
        bool intersect = ((pi.y > point.y) != (pj.y > point.y)) &&
                         (point.x < (pj.x - pi.x) * (point.y - pi.y) /
                                            std::max(1e-9, (pj.y - pi.y)) +
                                        pi.x);
        if (intersect)
        {
            inside = !inside;
        }
    }
    return inside;
}

double EstimateSegmentLengthInsidePolygon(const Vector& src,
                                          const Vector& dst,
                                          const std::vector<Vector>& polygon)
{
    if (polygon.size() < 3)
    {
        return 0.0;
    }

    const double dx = dst.x - src.x;
    const double dy = dst.y - src.y;
    const double dz = dst.z - src.z;
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 1e-6)
    {
        return 0.0;
    }

    const int samples = std::max(12, static_cast<int>(dist / 10.0));
    int insideSamples = 0;
    for (int i = 0; i <= samples; ++i)
    {
        double alpha = static_cast<double>(i) / samples;
        Vector point(src.x + alpha * dx, src.y + alpha * dy, src.z + alpha * dz);
        if (PointInPolygon2d(point, polygon))
        {
            ++insideSamples;
        }
    }

    return dist * static_cast<double>(insideSamples) / (samples + 1);
}

double EstimateForestOverlayLossDb(const Vector& src, const Vector& dst)
{
    if (g_forestRegions.empty() || g_environmentSummary.vegetationLossDbPerM <= 0.0)
    {
        return 0.0;
    }

    double totalLoss = 0.0;
    for (const auto& region : g_forestRegions)
    {
        double depth = EstimateSegmentLengthInsidePolygon(src, dst, region.points);
        if (depth <= 0.0)
        {
            continue;
        }
        totalLoss += depth * g_environmentSummary.vegetationLossDbPerM * region.weight;
    }

    return std::min(24.0, totalLoss);
}

double EstimateForestVegetationLossRateDbPerM(double baseLossDbPerM,
                                              double carrierFrequencyGHz,
                                              double channelBandwidthMHz,
                                              const std::string& polarizationMode)
{
    if (baseLossDbPerM <= 0.0)
    {
        return 0.0;
    }

    const double freqGHz = std::max(0.7, carrierFrequencyGHz);
    const double bandwidthMHz = std::max(5.0, channelBandwidthMHz);
    const double freqFactor = std::pow(freqGHz / 5.18, 0.65);
    const double bandwidthFactor =
        1.0 + 0.04 * std::log2(std::max(1.0, bandwidthMHz / 20.0));

    double polarizationFactor = 1.0;
    if (polarizationMode == "horizontal")
    {
        polarizationFactor = 1.10;
    }
    else if (polarizationMode == "dual")
    {
        polarizationFactor = 1.05;
    }

    return baseLossDbPerM * freqFactor * bandwidthFactor * polarizationFactor;
}

double Clamp01(double value)
{
    return std::max(0.0, std::min(1.0, value));
}

double EstimateWaterSurfaceExposure(const Vector& src, const Vector& dst)
{
    const double dx = dst.x - src.x;
    const double dy = dst.y - src.y;
    const double dz = dst.z - src.z;
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 1e-6)
    {
        return 0.0;
    }

    if (!g_waterRegions.empty())
    {
        double weightedExposure = 0.0;
        for (const auto& region : g_waterRegions)
        {
            const double depth = EstimateSegmentLengthInsidePolygon(src, dst, region.points);
            if (depth <= 0.0)
            {
                continue;
            }
            weightedExposure += (depth / dist) * region.weight;
        }
        if (weightedExposure > 0.0)
        {
            return Clamp01(weightedExposure);
        }
    }

    if (g_environmentSummary.sceneType == "lake" || g_environmentSummary.hasWaterSurface ||
        g_environmentConfig.sceneType == "lake")
    {
        return 1.0;
    }

    return 0.0;
}

double EstimateWaterSurfaceVolatilityLossDb(const Vector& src,
                                            const Vector& dst,
                                            double now)
{
    if (!g_environmentSummary.reflectionAware ||
        g_environmentSummary.lakeVolatilityJitterDb <= 0.0)
    {
        return 0.0;
    }

    const double exposure = EstimateWaterSurfaceExposure(src, dst);
    if (exposure <= 0.0)
    {
        return 0.0;
    }

    const double dx = dst.x - src.x;
    const double dy = dst.y - src.y;
    const double horizontalDist = std::sqrt(dx * dx + dy * dy);
    const double avgAltitude = 0.5 * (src.z + dst.z);
    const double heightDiff = std::abs(src.z - dst.z);
    constexpr double kPi = 3.14159265358979323846;
    const double elevationAngle =
        std::atan2(std::max(1.0, heightDiff), std::max(1.0, horizontalDist));
    const double grazingFactor =
        Clamp01(1.10 - elevationAngle / (kPi / 3.0)) *
        Clamp01(1.15 - avgAltitude / 180.0);
    const double geometryFactor = std::max(0.20, 0.35 + 0.65 * grazingFactor);

    const double phaseSeed = src.x * 0.013 + src.y * 0.017 + dst.x * 0.019 +
                             dst.y * 0.023 + (src.z + dst.z) * 0.007;
    const double oscillation =
        Clamp01(0.5 + 0.32 * std::sin(0.55 * now + phaseSeed) +
                0.18 * std::sin(1.35 * now + phaseSeed * 1.7));
    const double jitterLoss =
        g_environmentSummary.lakeVolatilityJitterDb * exposure * geometryFactor * oscillation;

    double deepFadeLoss = 0.0;
    if (g_environmentSummary.lakeDeepFadeProbability > 0.0 &&
        g_environmentSummary.lakeDeepFadeMaxDb > 0.0)
    {
        const double fadeGate =
            Clamp01(0.5 + 0.5 * std::sin(0.28 * now + phaseSeed * 2.3));
        const double triggerThreshold =
            Clamp01(1.0 - g_environmentSummary.lakeDeepFadeProbability);
        if (fadeGate > triggerThreshold)
        {
            const double fadeStrength =
                (fadeGate - triggerThreshold) /
                std::max(1e-3, 1.0 - triggerThreshold);
            deepFadeLoss = g_environmentSummary.lakeDeepFadeMaxDb * exposure *
                           geometryFactor * fadeStrength;
        }
    }

    return jitterLoss + deepFadeLoss;
}

double EstimateUrbanAltitudeAdjustmentDb(const Vector& src, const Vector& dst)
{
    if (!g_environmentSummary.hasBuildings ||
        (g_environmentSummary.sceneType != "urban" &&
         g_environmentConfig.sceneType != "urban"))
    {
        return 0.0;
    }

    const double avgBuildingHeight = std::max(5.0, g_environmentSummary.avgBuildingHeightM);
    const double avgAltitude = 0.5 * (src.z + dst.z);
    const double relativeAltitude = avgAltitude / avgBuildingHeight;

    const double streetWidth = g_environmentSummary.avgStreetWidthM > 1e-6
                                   ? g_environmentSummary.avgStreetWidthM
                                   : 12.0;
    const double narrowStreetFactor = Clamp01((20.0 - streetWidth) / 20.0);
    const double densityFactor =
        Clamp01(g_environmentSummary.buildingDensityPerKm2 / 15000.0);
    const double coverageFactor = Clamp01(g_environmentSummary.buildingCoverageRatio);
    const double canyonFactor = Clamp01(0.4 * coverageFactor + 0.3 * densityFactor +
                                        0.3 * narrowStreetFactor);

    if (relativeAltitude < 0.85 && g_environmentSummary.urbanAltitudePenaltyDbLow > 0.0)
    {
        const double severity = (0.85 - relativeAltitude) / 0.85;
        return g_environmentSummary.urbanAltitudePenaltyDbLow * severity *
               (0.55 + 0.45 * canyonFactor);
    }

    if (relativeAltitude > 1.05 && g_environmentSummary.urbanAltitudeGainDbHigh > 0.0)
    {
        const double relief = Clamp01((relativeAltitude - 1.05) / 0.75);
        return -g_environmentSummary.urbanAltitudeGainDbHigh * relief *
               (0.45 + 0.55 * (1.0 - 0.5 * canyonFactor));
    }

    return 0.0;
}

} // namespace

double
ForestOverlayPropagationLossModel::DoCalcRxPower(double txPowerDbm,
                                                 Ptr<MobilityModel> a,
                                                 Ptr<MobilityModel> b) const
{
    if (!a || !b)
    {
        return txPowerDbm;
    }

    return txPowerDbm - EstimateForestOverlayLossDb(a->GetPosition(), b->GetPosition());
}

int64_t
ForestOverlayPropagationLossModel::DoAssignStreams(int64_t stream)
{
    return 0;
}

double
WaterSurfaceOverlayPropagationLossModel::DoCalcRxPower(double txPowerDbm,
                                                       Ptr<MobilityModel> a,
                                                       Ptr<MobilityModel> b) const
{
    if (!a || !b)
    {
        return txPowerDbm;
    }

    return txPowerDbm - EstimateWaterSurfaceVolatilityLossDb(a->GetPosition(),
                                                             b->GetPosition(),
                                                             Simulator::Now().GetSeconds());
}

int64_t
WaterSurfaceOverlayPropagationLossModel::DoAssignStreams(int64_t stream)
{
    return 0;
}

double
UrbanAltitudeAdaptivePropagationLossModel::DoCalcRxPower(double txPowerDbm,
                                                         Ptr<MobilityModel> a,
                                                         Ptr<MobilityModel> b) const
{
    if (!a || !b)
    {
        return txPowerDbm;
    }

    return txPowerDbm - EstimateUrbanAltitudeAdjustmentDb(a->GetPosition(), b->GetPosition());
}

int64_t
UrbanAltitudeAdaptivePropagationLossModel::DoAssignStreams(int64_t stream)
{
    return 0;
}

} // namespace ns3

namespace
{

std::string NormalizeSceneType(const std::string& requestedSceneType,
                               const std::string& mapFile)
{
    if (requestedSceneType.empty())
    {
        return mapFile.empty() ? "open-field" : "urban";
    }
    if (requestedSceneType == "open" || requestedSceneType == "wild" ||
        requestedSceneType == "rural" || requestedSceneType == "farmland")
    {
        return "open-field";
    }
    if (requestedSceneType == "open-water")
    {
        return "lake";
    }
    return requestedSceneType;
}

EnvironmentPreset BuildSceneBasePreset(const std::string& sceneType)
{
    EnvironmentPreset preset;
    preset.sceneType = sceneType;

    if (sceneType == "urban")
    {
        preset.baseModel = "HybridBuildingsPropagationLossModel";
        preset.shadowSigmaDb = 6.0;
        preset.nlosPenaltyDb = 18.0;
        preset.vegetationLossDbPerM = 0.0;
        preset.losBaseProb = 0.45;
        preset.interferenceFactor = 1.30;
        preset.connectivityRangeFactor = 0.72;
        preset.hasBuildings = true;
        preset.hasVegetation = false;
        preset.hasWaterSurface = false;
        preset.reflectionAware = false;
        preset.pathLossExponent = 3.3;
        preset.urbanAltitudePenaltyDbLow = 7.0;
        preset.urbanAltitudeGainDbHigh = 6.0;
        preset.urbanStreetCanyonFactor = 1.0;
        preset.reroutePressureFactor = 1.30;
        preset.controlMessageUrgencyFactor = 1.25;
        preset.relayInstabilityFactor = 1.20;
        preset.formationReconfigPenalty = 1.25;
        preset.carrierFrequencyGHz = 5.18;
        preset.channelBandwidthMHz = 20.0;
        preset.polarizationMode = "vertical";
        preset.lakeVolatilityJitterDb = 0.0;
        preset.lakeDeepFadeProbability = 0.0;
        preset.lakeDeepFadeMaxDb = 0.0;
        preset.lakeReflectionDelayJitterMs = 0.0;
        return preset;
    }

    if (sceneType == "forest")
    {
        preset.baseModel = "LogDistance + vegetation attenuation";
        preset.shadowSigmaDb = 6.5;
        preset.nlosPenaltyDb = 10.0;
        preset.vegetationLossDbPerM = 0.30;
        preset.losBaseProb = 0.60;
        preset.interferenceFactor = 1.20;
        preset.connectivityRangeFactor = 0.68;
        preset.hasBuildings = false;
        preset.hasVegetation = true;
        preset.hasWaterSurface = false;
        preset.reflectionAware = false;
        preset.pathLossExponent = 3.4;
        preset.urbanAltitudePenaltyDbLow = 0.0;
        preset.urbanAltitudeGainDbHigh = 0.0;
        preset.urbanStreetCanyonFactor = 0.0;
        preset.reroutePressureFactor = 1.20;
        preset.controlMessageUrgencyFactor = 1.10;
        preset.relayInstabilityFactor = 1.15;
        preset.formationReconfigPenalty = 1.15;
        preset.carrierFrequencyGHz = 5.18;
        preset.channelBandwidthMHz = 20.0;
        preset.polarizationMode = "vertical";
        preset.lakeVolatilityJitterDb = 0.0;
        preset.lakeDeepFadeProbability = 0.0;
        preset.lakeDeepFadeMaxDb = 0.0;
        preset.lakeReflectionDelayJitterMs = 0.0;
        return preset;
    }

    if (sceneType == "lake")
    {
        preset.baseModel = "FSPL/LogDistance baseline + reflection-sensitive correction";
        preset.shadowSigmaDb = 2.0;
        preset.nlosPenaltyDb = 3.0;
        preset.vegetationLossDbPerM = 0.0;
        preset.losBaseProb = 0.95;
        preset.interferenceFactor = 0.85;
        preset.connectivityRangeFactor = 1.10;
        preset.hasBuildings = false;
        preset.hasVegetation = false;
        preset.hasWaterSurface = true;
        preset.reflectionAware = true;
        preset.pathLossExponent = 2.1;
        preset.urbanAltitudePenaltyDbLow = 0.0;
        preset.urbanAltitudeGainDbHigh = 0.0;
        preset.urbanStreetCanyonFactor = 0.0;
        preset.reroutePressureFactor = 1.05;
        preset.controlMessageUrgencyFactor = 1.15;
        preset.relayInstabilityFactor = 1.10;
        preset.formationReconfigPenalty = 1.08;
        preset.carrierFrequencyGHz = 5.18;
        preset.channelBandwidthMHz = 20.0;
        preset.polarizationMode = "vertical";
        preset.lakeVolatilityJitterDb = 4.0;
        preset.lakeDeepFadeProbability = 0.18;
        preset.lakeDeepFadeMaxDb = 8.0;
        preset.lakeReflectionDelayJitterMs = 18.0;
        return preset;
    }

    preset.baseModel = "RMa-like LogDistance baseline";
    preset.shadowSigmaDb = 4.0;
    preset.nlosPenaltyDb = 6.0;
    preset.vegetationLossDbPerM = 0.0;
    preset.losBaseProb = 0.85;
    preset.interferenceFactor = 0.95;
    preset.connectivityRangeFactor = 1.00;
    preset.hasBuildings = false;
    preset.hasVegetation = false;
    preset.hasWaterSurface = false;
    preset.reflectionAware = false;
    preset.pathLossExponent = 2.5;
    preset.urbanAltitudePenaltyDbLow = 0.0;
    preset.urbanAltitudeGainDbHigh = 0.0;
    preset.urbanStreetCanyonFactor = 0.0;
    preset.reroutePressureFactor = 1.00;
    preset.controlMessageUrgencyFactor = 1.00;
    preset.relayInstabilityFactor = 1.00;
    preset.formationReconfigPenalty = 1.00;
    preset.carrierFrequencyGHz = 5.18;
    preset.channelBandwidthMHz = 20.0;
    preset.polarizationMode = "vertical";
    preset.lakeVolatilityJitterDb = 0.0;
    preset.lakeDeepFadeProbability = 0.0;
    preset.lakeDeepFadeMaxDb = 0.0;
    preset.lakeReflectionDelayJitterMs = 0.0;
    return preset;
}

void ApplyDifficultyMultipliers(EnvironmentPreset& preset,
                                const std::string& difficulty)
{
    double shadowMultiplier = 1.0;
    double nlosMultiplier = 1.0;
    double interferenceMultiplier = 1.0;
    double rangeMultiplier = 1.0;

    if (difficulty == "Easy")
    {
        shadowMultiplier = 0.85;
        nlosMultiplier = 0.85;
        interferenceMultiplier = 0.90;
        rangeMultiplier = 1.10;
    }
    else if (difficulty == "Hard")
    {
        shadowMultiplier = 1.20;
        nlosMultiplier = 1.20;
        interferenceMultiplier = 1.15;
        rangeMultiplier = 0.88;
    }

    preset.shadowSigmaDb *= shadowMultiplier;
    preset.nlosPenaltyDb *= nlosMultiplier;
    preset.interferenceFactor *= interferenceMultiplier;
    preset.connectivityRangeFactor *= rangeMultiplier;
}

ObservationPreset BuildObservationPreset(OperationMode operationMode,
                                         const std::string& sceneType,
                                         const std::string& difficulty)
{
    ObservationPreset preset;

    if (sceneType == "urban")
    {
        preset.observationRangeM = 180.0;
        preset.randomDropRate = 0.15;
        preset.positionNoiseStdDevM = 20.0;
        preset.powerNoiseStdDevDb = 4.0;
    }
    else if (sceneType == "forest")
    {
        // Forest should be harder than open-field, but still produce enough detections
        // for track creation and graph inference.
        preset.observationRangeM = 210.0;
        preset.randomDropRate = 0.12;
        preset.positionNoiseStdDevM = 20.0;
        preset.powerNoiseStdDevDb = 4.0;
    }
    else if (sceneType == "lake")
    {
        preset.observationRangeM = 260.0;
        preset.randomDropRate = 0.08;
        preset.positionNoiseStdDevM = 12.0;
        preset.powerNoiseStdDevDb = 2.0;
    }
    else
    {
        preset.observationRangeM = 220.0;
        preset.randomDropRate = 0.10;
        preset.positionNoiseStdDevM = 15.0;
        preset.powerNoiseStdDevDb = 3.0;
    }

    preset.observationEnabled = (operationMode == OperationMode::NonCooperative);
    preset.windowDurationSec = 0.5;
    preset.subslotCount = 10;
    preset.subslotDurationSec = 0.05;
    preset.trackCreateWindowCount = 2;
    preset.trackDeleteWindowCount = 4;

    if (difficulty == "Moderate")
    {
        preset.rtkNoiseStdDev = 0.08;
        preset.rtkDriftInterval = 15.0;
        preset.rtkDriftDuration = 4.0;
        preset.rtkDriftMagnitude = 0.5;
    }
    else if (difficulty == "Hard")
    {
        preset.rtkNoiseStdDev = 0.2;
        preset.rtkDriftInterval = 8.0;
        preset.rtkDriftDuration = 6.0;
        preset.rtkDriftMagnitude = 1.0;
    }
    else if (difficulty == "Custom")
    {
        preset.rtkNoiseStdDev = g_diffParams.rtkNoiseStdDev;
        preset.rtkDriftInterval = g_diffParams.rtkDriftInterval;
        preset.rtkDriftDuration = g_diffParams.rtkDriftDuration;
        preset.rtkDriftMagnitude = g_diffParams.rtkDriftMagnitude;
    }
    return preset;
}

InterferencePreset BuildInterferencePreset(const std::string& difficulty)
{
    InterferencePreset preset;
    if (difficulty == "Moderate")
    {
        preset.enableInterference = true;
        preset.numInterferenceNodes = 8;
        preset.interferenceRateMbps = 4.0;
        preset.interferenceDutyCycle = 0.7;
    }
    else if (difficulty == "Hard")
    {
        preset.enableInterference = true;
        preset.numInterferenceNodes = 15;
        preset.interferenceRateMbps = 6.0;
        preset.interferenceDutyCycle = 0.95;
    }
    else if (difficulty == "Custom")
    {
        preset.enableInterference = (g_diffParams.numInterferenceNodes > 0);
        preset.numInterferenceNodes = g_diffParams.numInterferenceNodes;
        preset.interferenceRateMbps = g_diffParams.interferenceRateMbps;
        preset.interferenceDutyCycle = g_diffParams.interferenceDutyCycle;
    }
    return preset;
}

TrafficPlatformPreset BuildTrafficPlatformPreset(const std::string& difficulty)
{
    TrafficPlatformPreset preset;
    if (difficulty == "Moderate")
    {
        preset.trafficLoadMbps = 2.8;
        preset.macMaxRetries = 1;
        preset.noiseFigure = 15.0;
        preset.rxSensitivity = -85.0;
        preset.txPower = 23.0;
        preset.nakagamiM = 0.7;
    }
    else if (difficulty == "Hard")
    {
        preset.trafficLoadMbps = 7.0;
        preset.macMaxRetries = 0;
        preset.noiseFigure = 20.0;
        preset.rxSensitivity = -82.0;
        preset.txPower = 23.0;
        preset.nakagamiM = 0.2;
    }
    else if (difficulty == "Custom")
    {
        preset.trafficLoadMbps = g_diffParams.trafficLoadMbps;
        preset.macMaxRetries = g_diffParams.macMaxRetries;
        preset.noiseFigure = g_diffParams.noiseFigure;
        preset.rxSensitivity = g_config.rxSensitivity;
        preset.txPower = g_config.txPowerMax;
        preset.nakagamiM = g_diffParams.nakagamiM;
    }
    return preset;
}

void BuildScenarioEnvironmentConfig(OperationMode operationMode,
                                    const std::string& requestedSceneType,
                                    const std::string& difficulty,
                                    const std::string& formationName,
                                    const std::string& mapFile,
                                    const CooperativeControlConfig& cooperativeConfig,
                                    const NonCooperativeAttackConfig& nonCooperativeAttackConfig,
                                    const SceneRealismOverrides& sceneRealismOverrides,
                                    double customPathLossExp,
                                    double customRxSensitivity,
                                    double customTxPower)
{
    g_environmentConfig = ScenarioEnvironmentConfig();
    g_environmentConfig.operationMode = operationMode;
    g_environmentConfig.sceneType = NormalizeSceneType(requestedSceneType, mapFile);
    g_environmentConfig.difficulty = difficulty;
    g_environmentConfig.formationName = formationName.empty() ? "random_walk" : formationName;
    g_environmentConfig.mapFile = mapFile;
    g_environmentConfig.hasMapGeometry = !mapFile.empty();
    g_environmentConfig.useBuildingGeometry =
        g_environmentConfig.hasMapGeometry && g_environmentConfig.sceneType == "urban";
    bool isGeoJsonInput =
        mapFile.size() >= 5 &&
        (mapFile.rfind(".json") == mapFile.size() - 5 ||
         (mapFile.size() >= 8 && mapFile.rfind(".geojson") == mapFile.size() - 8));
    g_environmentConfig.geometryInputMode =
        g_environmentConfig.hasMapGeometry
            ? (g_environmentConfig.useBuildingGeometry
                   ? (isGeoJsonInput ? "geojson-building-geometry" : "building-geometry")
                   : (isGeoJsonInput ? "geojson-scene-overlay" : "scene-overlay"))
            : "none";

    g_environmentConfig.environmentPreset =
        BuildSceneBasePreset(g_environmentConfig.sceneType);
    ApplyDifficultyMultipliers(g_environmentConfig.environmentPreset, difficulty);
    g_environmentConfig.observationPreset =
        BuildObservationPreset(operationMode, g_environmentConfig.sceneType, difficulty);
    g_environmentConfig.interferencePreset = BuildInterferencePreset(difficulty);
    g_environmentConfig.trafficPlatformPreset = BuildTrafficPlatformPreset(difficulty);
    g_environmentConfig.cooperativeControlConfig = cooperativeConfig;
    g_environmentConfig.nonCooperativeAttackConfig = nonCooperativeAttackConfig;
    g_environmentConfig.sceneRealismOverrides = sceneRealismOverrides;

    if (operationMode == OperationMode::Cooperative)
    {
        if (g_environmentConfig.cooperativeControlConfig.failureStartTime < 0.0)
        {
            g_environmentConfig.cooperativeControlConfig.failureStartTime = g_config.duration * 0.4;
        }
        if (g_environmentConfig.cooperativeControlConfig.failureDuration < 0.0)
        {
            g_environmentConfig.cooperativeControlConfig.failureDuration = g_config.duration * 0.2;
        }
        if (g_environmentConfig.cooperativeControlConfig.failureTargetId < 0)
        {
            g_environmentConfig.cooperativeControlConfig.failureTargetId =
                g_config.numUAVs > 1 ? 1 : 0;
        }
    }
    if (operationMode == OperationMode::NonCooperative &&
        g_environmentConfig.nonCooperativeAttackConfig.enabled)
    {
        g_environmentConfig.nonCooperativeAttackConfig.attackEvaluationDuration =
            std::max(1.0, g_environmentConfig.nonCooperativeAttackConfig.attackEvaluationDuration);
        g_environmentConfig.nonCooperativeAttackConfig.attackNeighborhoodHop =
            std::max<uint32_t>(1, g_environmentConfig.nonCooperativeAttackConfig.attackNeighborhoodHop);
    }

    if (difficulty == "Custom")
    {
        g_environmentConfig.environmentPreset.pathLossExponent = customPathLossExp;
        g_environmentConfig.trafficPlatformPreset.rxSensitivity = customRxSensitivity;
        g_environmentConfig.trafficPlatformPreset.txPower = customTxPower;
    }

    if (g_environmentConfig.sceneType == "forest")
    {
        g_environmentConfig.environmentPreset.vegetationLossDbPerM =
            EstimateForestVegetationLossRateDbPerM(
                g_environmentConfig.environmentPreset.vegetationLossDbPerM,
                g_environmentConfig.environmentPreset.carrierFrequencyGHz,
                g_environmentConfig.environmentPreset.channelBandwidthMHz,
                g_environmentConfig.environmentPreset.polarizationMode);
    }

    if (sceneRealismOverrides.enabled)
    {
        auto& preset = g_environmentConfig.environmentPreset;
        if (sceneRealismOverrides.urbanAltitudePenaltyDbLow >= 0.0)
        {
            preset.urbanAltitudePenaltyDbLow = sceneRealismOverrides.urbanAltitudePenaltyDbLow;
        }
        if (sceneRealismOverrides.urbanAltitudeGainDbHigh >= 0.0)
        {
            preset.urbanAltitudeGainDbHigh = sceneRealismOverrides.urbanAltitudeGainDbHigh;
        }
        if (sceneRealismOverrides.urbanStreetCanyonFactor >= 0.0)
        {
            preset.urbanStreetCanyonFactor = sceneRealismOverrides.urbanStreetCanyonFactor;
        }
        if (sceneRealismOverrides.lakeVolatilityJitterDb >= 0.0)
        {
            preset.lakeVolatilityJitterDb = sceneRealismOverrides.lakeVolatilityJitterDb;
        }
        if (sceneRealismOverrides.lakeDeepFadeProbability >= 0.0)
        {
            preset.lakeDeepFadeProbability = sceneRealismOverrides.lakeDeepFadeProbability;
        }
        if (sceneRealismOverrides.lakeDeepFadeMaxDb >= 0.0)
        {
            preset.lakeDeepFadeMaxDb = sceneRealismOverrides.lakeDeepFadeMaxDb;
        }
        if (sceneRealismOverrides.lakeReflectionDelayJitterMs >= 0.0)
        {
            preset.lakeReflectionDelayJitterMs = sceneRealismOverrides.lakeReflectionDelayJitterMs;
        }
        if (sceneRealismOverrides.carrierFrequencyGHz > 0.0)
        {
            preset.carrierFrequencyGHz = sceneRealismOverrides.carrierFrequencyGHz;
        }
        if (sceneRealismOverrides.channelBandwidthMHz > 0.0)
        {
            preset.channelBandwidthMHz = sceneRealismOverrides.channelBandwidthMHz;
        }
        if (!sceneRealismOverrides.polarizationMode.empty())
        {
            preset.polarizationMode = sceneRealismOverrides.polarizationMode;
        }
        if (sceneRealismOverrides.reroutePressureFactor >= 0.0)
        {
            preset.reroutePressureFactor = sceneRealismOverrides.reroutePressureFactor;
        }
        if (sceneRealismOverrides.controlMessageUrgencyFactor >= 0.0)
        {
            preset.controlMessageUrgencyFactor =
                sceneRealismOverrides.controlMessageUrgencyFactor;
        }
        if (sceneRealismOverrides.relayInstabilityFactor >= 0.0)
        {
            preset.relayInstabilityFactor = sceneRealismOverrides.relayInstabilityFactor;
        }
        if (sceneRealismOverrides.formationReconfigPenalty >= 0.0)
        {
            preset.formationReconfigPenalty = sceneRealismOverrides.formationReconfigPenalty;
        }

        if (g_environmentConfig.sceneType == "forest")
        {
            preset.vegetationLossDbPerM = EstimateForestVegetationLossRateDbPerM(
                preset.vegetationLossDbPerM,
                preset.carrierFrequencyGHz,
                preset.channelBandwidthMHz,
                preset.polarizationMode);
        }
    }

    if (!g_environmentConfig.hasMapGeometry)
    {
        g_environmentConfig.environmentSource =
            difficulty == "Moderate" ? "scene-base only" : "scene-base + difficulty";
    }
    else
    {
        g_environmentConfig.environmentSource =
            difficulty == "Moderate" ? "scene-base + map geometry"
                                     : "scene-base + map geometry + difficulty";
    }
}

void ApplyScenarioEnvironmentToLegacyState()
{
    const auto& env = g_environmentConfig.environmentPreset;
    const auto& obs = g_environmentConfig.observationPreset;
    const auto& interf = g_environmentConfig.interferencePreset;
    const auto& platform = g_environmentConfig.trafficPlatformPreset;
    const auto& coop = g_environmentConfig.cooperativeControlConfig;

    g_pathLossExponent = env.pathLossExponent;
    g_config.rxSensitivity = platform.rxSensitivity;
    g_config.txPowerMax = platform.txPower;

    g_diffParams.levelName = g_environmentConfig.difficulty;
    g_diffParams.rtkNoiseStdDev = obs.rtkNoiseStdDev;
    g_diffParams.rtkDriftInterval = obs.rtkDriftInterval;
    g_diffParams.rtkDriftDuration = obs.rtkDriftDuration;
    g_diffParams.rtkDriftMagnitude = obs.rtkDriftMagnitude;
    g_diffParams.enableInterference = interf.enableInterference;
    g_diffParams.numInterferenceNodes = interf.numInterferenceNodes;
    g_diffParams.interferenceRateMbps = interf.interferenceRateMbps;
    g_diffParams.interferenceDutyCycle = interf.interferenceDutyCycle;
    g_diffParams.nakagamiM = platform.nakagamiM;
    g_diffParams.macMaxRetries = platform.macMaxRetries;
    g_diffParams.noiseFigure = platform.noiseFigure;
    g_diffParams.trafficLoadMbps = platform.trafficLoadMbps;

    g_environmentSummary = EnvironmentSummary();
    g_environmentSummary.operationMode =
        OperationModeToString(g_environmentConfig.operationMode);
    g_environmentSummary.sceneType = g_environmentConfig.sceneType;
    g_environmentSummary.difficulty = g_environmentConfig.difficulty;
    g_environmentSummary.formationName = g_environmentConfig.formationName;
    g_environmentSummary.baseModel = env.baseModel;
    g_environmentSummary.environmentSource = g_environmentConfig.environmentSource;
    g_environmentSummary.geometryInputMode = g_environmentConfig.geometryInputMode;
    g_environmentSummary.effectiveModelSummary =
        env.sceneType == "lake" && env.reflectionAware
            ? env.baseModel + " + water-surface volatility overlay"
            : env.sceneType == "urban" && env.hasBuildings
                  ? env.baseModel + " + altitude-aware urban overlay"
                  : env.baseModel;
    g_environmentSummary.environmentContributionSummary =
        env.sceneType + " scene, source=" + g_environmentConfig.environmentSource;
    g_environmentSummary.hasBuildings =
        env.hasBuildings && g_environmentConfig.useBuildingGeometry;
    g_environmentSummary.hasVegetation = env.hasVegetation;
    g_environmentSummary.hasWaterSurface = env.hasWaterSurface;
    g_environmentSummary.reflectionAware = env.reflectionAware;
    g_environmentSummary.shadowSigmaDb = env.shadowSigmaDb;
    g_environmentSummary.nlosPenaltyDb = env.nlosPenaltyDb;
    g_environmentSummary.vegetationLossDbPerM = env.vegetationLossDbPerM;
    g_environmentSummary.interferenceFactor = env.interferenceFactor;
    g_environmentSummary.connectivityRangeFactor = env.connectivityRangeFactor;
    g_environmentSummary.pathLossExponent = env.pathLossExponent;
    g_environmentSummary.urbanAltitudePenaltyDbLow = env.urbanAltitudePenaltyDbLow;
    g_environmentSummary.urbanAltitudeGainDbHigh = env.urbanAltitudeGainDbHigh;
    g_environmentSummary.urbanStreetCanyonFactor = env.urbanStreetCanyonFactor;
    g_environmentSummary.reroutePressureFactor = env.reroutePressureFactor;
    g_environmentSummary.controlMessageUrgencyFactor = env.controlMessageUrgencyFactor;
    g_environmentSummary.relayInstabilityFactor = env.relayInstabilityFactor;
    g_environmentSummary.formationReconfigPenalty = env.formationReconfigPenalty;
    g_environmentSummary.carrierFrequencyGHz = env.carrierFrequencyGHz;
    g_environmentSummary.channelBandwidthMHz = env.channelBandwidthMHz;
    g_environmentSummary.polarizationMode = env.polarizationMode;
    g_environmentSummary.lakeVolatilityJitterDb = env.lakeVolatilityJitterDb;
    g_environmentSummary.lakeDeepFadeProbability = env.lakeDeepFadeProbability;
    g_environmentSummary.lakeDeepFadeMaxDb = env.lakeDeepFadeMaxDb;
    g_environmentSummary.lakeReflectionDelayJitterMs = env.lakeReflectionDelayJitterMs;
    g_environmentSummary.rxSensitivity = platform.rxSensitivity;
    g_environmentSummary.txPower = platform.txPower;
    g_environmentSummary.noiseFigure = platform.noiseFigure;
    g_environmentSummary.trafficLoadMbps = platform.trafficLoadMbps;
    g_environmentSummary.numInterferenceNodes = interf.numInterferenceNodes;
    g_environmentSummary.observationEnabled = obs.observationEnabled;
    g_environmentSummary.observationWindowDurationSec = obs.windowDurationSec;
    g_environmentSummary.observationSubslotCount = obs.subslotCount;
    g_environmentSummary.observationSubslotDurationSec = obs.subslotDurationSec;
    g_environmentSummary.trackCreateWindowCount = obs.trackCreateWindowCount;
    g_environmentSummary.trackDeleteWindowCount = obs.trackDeleteWindowCount;
    g_environmentSummary.observationRangeM = obs.observationRangeM;
    g_environmentSummary.observationRandomDropRate = obs.randomDropRate;
    g_environmentSummary.observationPositionNoiseStdDevM = obs.positionNoiseStdDevM;
    g_environmentSummary.observationPowerNoiseStdDevDb = obs.powerNoiseStdDevDb;
    g_environmentSummary.observationObserverCount =
        static_cast<uint32_t>(g_observationRuntime.observerIds.size());
    g_environmentSummary.observationTargetObjectCount =
        static_cast<uint32_t>(g_observationRuntime.targetObjectKeys.size());
    g_environmentSummary.communicationMode = CommunicationModeToString(coop.communicationMode);
    g_environmentSummary.leaderNodeId = coop.leaderNodeId;
    {
        std::ostringstream backupList;
        for (size_t i = 0; i < coop.backupLeaderList.size(); ++i)
        {
            if (i > 0)
            {
                backupList << ",";
            }
            backupList << coop.backupLeaderList[i];
        }
        g_environmentSummary.backupLeaderList = backupList.str();
    }
    g_environmentSummary.distributedHopLimit = coop.distributedHopLimit;
    g_environmentSummary.cooperativeFailureType =
        CooperativeFailureTypeToString(coop.failureType);
    g_environmentSummary.failureTargetId = coop.failureTargetId;
    g_environmentSummary.failureStartTime = coop.failureStartTime;
    g_environmentSummary.failureDuration = coop.failureDuration;
    g_environmentSummary.recoveryPolicy = RecoveryPolicyToString(coop.recoveryPolicy);
    g_environmentSummary.recoveryObjective =
        RecoveryObjectiveToString(coop.recoveryObjective);
    g_environmentSummary.recoveryCooldown = coop.recoveryCooldown;
    g_environmentSummary.allowChannelReallocation = coop.allowChannelReallocation;
    g_environmentSummary.allowPowerAdjustment = coop.allowPowerAdjustment;
    g_environmentSummary.allowRateAdjustment = coop.allowRateAdjustment;
    g_environmentSummary.allowRelayReselection = coop.allowRelayReselection;
    g_environmentSummary.allowSlotReallocation = coop.allowSlotReallocation;
    g_environmentSummary.allowRouteRebuild = coop.allowRouteRebuild;
    g_environmentSummary.nonCooperativeAttackEnabled =
        g_environmentConfig.nonCooperativeAttackConfig.enabled;
    g_environmentSummary.nonCooperativeAttackType =
        NonCooperativeAttackTypeToString(g_environmentConfig.nonCooperativeAttackConfig.attackType);
    g_environmentSummary.manualStrikeTarget =
        g_environmentConfig.nonCooperativeAttackConfig.manualStrikeTarget;
    g_environmentSummary.attackExecuteTime =
        g_environmentConfig.nonCooperativeAttackConfig.attackExecuteTime;
    g_environmentSummary.attackEvaluationDuration =
        g_environmentConfig.nonCooperativeAttackConfig.attackEvaluationDuration;
    g_environmentSummary.attackNeighborhoodHop =
        g_environmentConfig.nonCooperativeAttackConfig.attackNeighborhoodHop;
}

void SetupUavMobility(bool useFormation)
{
    g_uavNodes.Create(g_config.numUAVs);

    if (useFormation)
    {
        SetupFormationMobility(g_uavNodes);
        return;
    }

    std::cout << "使用随机游走移动模型" << std::endl;
    MobilityHelper mobility;
    mobility.SetPositionAllocator("ns3::RandomBoxPositionAllocator",
                                  "X",
                                  StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +
                                              std::to_string(g_config.areaSize) + "]"),
                                  "Y",
                                  StringValue("ns3::UniformRandomVariable[Min=0.0|Max=" +
                                              std::to_string(g_config.areaSize) + "]"),
                                  "Z",
                                  StringValue("ns3::UniformRandomVariable[Min=" +
                                              std::to_string(g_config.uavHeight - 10) +
                                              "|Max=" +
                                              std::to_string(g_config.uavHeight + 10) + "]"));

    mobility.SetMobilityModel("ns3::RandomWalk2dMobilityModel",
                              "Bounds",
                              RectangleValue(Rectangle(0, g_config.areaSize, 0, g_config.areaSize)),
                              "Speed",
                              StringValue("ns3::UniformRandomVariable[Min=5.0|Max=" +
                                          std::to_string(g_config.maxSpeed) + "]"),
                              "Distance",
                              DoubleValue(50.0));
    mobility.Install(g_uavNodes);
}

void InitializeResourceAssignments()
{
    g_state.adjacencyMatrix.resize(g_config.numUAVs,
                                   std::vector<bool>(g_config.numUAVs, false));

    for (uint32_t i = 0; i < g_config.numUAVs; ++i)
    {
        if (g_config.allocationStrategy == "static")
        {
            g_state.channelAssignment[i] = 0;
            g_state.powerAssignment[i] = 20.0;
            g_state.rateAssignment[i] = 6.0;
        }
        else
        {
            g_state.channelAssignment[i] = i % g_config.numChannels;
            g_state.powerAssignment[i] = 20.0;
            g_state.rateAssignment[i] = 6.0;
        }
    }

    ApplyResourceAssignments();
    UpdateTopology();
}

} // namespace

void SetupSimulationInfrastructure(bool useFormation,
                                   OperationMode operationMode,
                                   const std::string& sceneType,
                                   const std::string& difficulty,
                                   const std::string& formationName,
                                   const std::string& mapFile,
                                   const CooperativeControlConfig& cooperativeConfig,
                                   const NonCooperativeAttackConfig& nonCooperativeAttackConfig,
                                   const SceneRealismOverrides& sceneRealismOverrides,
                                   double customPathLossExp,
                                   double customRxSensitivity,
                                   double customTxPower)
{
    SetupUavMobility(useFormation);

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211a);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode",
                                 StringValue("OfdmRate54Mbps"),
                                 "ControlMode",
                                 StringValue("OfdmRate6Mbps"));

    WifiMacHelper wifiMac;
    wifiMac.SetType("ns3::AdhocWifiMac");

    BuildScenarioEnvironmentConfig(operationMode,
                                   sceneType,
                                   difficulty,
                                   formationName,
                                   mapFile,
                                   cooperativeConfig,
                                   nonCooperativeAttackConfig,
                                   sceneRealismOverrides,
                                   customPathLossExp,
                                   customRxSensitivity,
                                   customTxPower);
    ApplyScenarioEnvironmentToLegacyState();
    g_cooperativeRuntime = CooperativeRuntimeState();
    g_cooperativeRuntime.activeLeaderNodeId =
        g_environmentConfig.cooperativeControlConfig.leaderNodeId;
    g_cooperativeRuntime.activeBackupLeaderNodeId =
        g_environmentConfig.cooperativeControlConfig.backupLeaderList.empty()
            ? -1
            : static_cast<int32_t>(g_environmentConfig.cooperativeControlConfig.backupLeaderList.front());
    g_cooperativeRuntime.leaderAlive = true;

    std::cout << "信道参数: PathLossExp=" << g_environmentSummary.pathLossExponent
              << ", RxSens=" << g_environmentSummary.rxSensitivity << "dBm"
              << ", RangeFactor=" << g_environmentSummary.connectivityRangeFactor
              << std::endl;

    bool hasBuildings = g_environmentConfig.useBuildingGeometry;
    YansWifiPhyHelper wifiPhy;
    Ptr<YansWifiChannel> theChannel = CreateObject<YansWifiChannel>();
    theChannel->SetPropagationDelayModel(CreateObject<ConstantSpeedPropagationDelayModel>());

    Ptr<PropagationLossModel> headLoss;
    Ptr<PropagationLossModel> tailLoss;
    auto appendLossModel = [&headLoss, &tailLoss](Ptr<PropagationLossModel> model) {
        if (!headLoss)
        {
            headLoss = model;
            tailLoss = model;
            return;
        }
        tailLoss->SetNext(model);
        tailLoss = model;
    };

    if (hasBuildings)
    {
        std::cout << "🧱 初始化高级云边协同建筑射线追踪损耗模型 "
                     "(HybridBuildingsPropagationLossModel)..."
                  << std::endl;
        appendLossModel(CreateObject<HybridBuildingsPropagationLossModel>());
        if (g_environmentConfig.sceneType == "urban")
        {
            appendLossModel(CreateObject<UrbanAltitudeAdaptivePropagationLossModel>());
        }
    }
    else if (g_environmentConfig.sceneType == "lake")
    {
        std::cout << "🌊 初始化湖面反射传播模型 (TwoRayGroundPropagationLossModel)..."
                  << std::endl;
        appendLossModel(CreateObject<TwoRayGroundPropagationLossModel>());
        appendLossModel(CreateObject<WaterSurfaceOverlayPropagationLossModel>());
    }
    else
    {
        Ptr<LogDistancePropagationLossModel> logDistance =
            CreateObject<LogDistancePropagationLossModel>();
        logDistance->SetAttribute("Exponent",
                                  DoubleValue(g_environmentSummary.pathLossExponent));
        logDistance->SetAttribute("ReferenceDistance", DoubleValue(1.0));
        logDistance->SetAttribute("ReferenceLoss", DoubleValue(46.6777));
        appendLossModel(logDistance);
    }

    if (g_environmentConfig.sceneType == "forest")
    {
        appendLossModel(CreateObject<ForestOverlayPropagationLossModel>());
    }

    if (g_diffParams.nakagamiM > 0.0)
    {
        Ptr<NakagamiPropagationLossModel> nakagami =
            CreateObject<NakagamiPropagationLossModel>();
        nakagami->SetAttribute("m0", DoubleValue(g_diffParams.nakagamiM));
        nakagami->SetAttribute("m1", DoubleValue(g_diffParams.nakagamiM));
        nakagami->SetAttribute("m2", DoubleValue(g_diffParams.nakagamiM));
        appendLossModel(nakagami);
        std::cout << "Nakagami-m 衰落已启用: m=" << g_diffParams.nakagamiM
                  << (g_diffParams.nakagamiM >= 2.0 ? " (近Rician/强LOS)"
                      : g_diffParams.nakagamiM >= 0.8 ? " (近Rayleigh)"
                                                     : " (极度散射)")
                  << std::endl;
    }

    theChannel->SetPropagationLossModel(headLoss);
    wifiPhy.SetChannel(theChannel);
    wifiPhy.Set("TxPowerStart", DoubleValue(g_environmentSummary.txPower));
    wifiPhy.Set("TxPowerEnd", DoubleValue(g_environmentSummary.txPower));
    wifiPhy.Set("RxSensitivity", DoubleValue(g_environmentSummary.rxSensitivity));
    wifiPhy.Set("RxNoiseFigure", DoubleValue(g_diffParams.noiseFigure));

    NetDeviceContainer devices = wifi.Install(wifiPhy, wifiMac, g_uavNodes);

    for (uint32_t i = 0; i < devices.GetN(); ++i)
    {
        Ptr<WifiNetDevice> wifiDev = DynamicCast<WifiNetDevice>(devices.Get(i));
        if (!wifiDev)
        {
            continue;
        }
        Ptr<WifiRemoteStationManager> mgr = wifiDev->GetRemoteStationManager();
        if (mgr)
        {
            mgr->SetAttribute("MaxSsrc", UintegerValue(g_diffParams.macMaxRetries));
            mgr->SetAttribute("MaxSlrc", UintegerValue(g_diffParams.macMaxRetries));
        }
    }

    OlsrHelper olsr;
    Ipv4ListRoutingHelper routingList;
    routingList.Add(olsr, 10);

    InternetStackHelper internet;
    internet.SetRoutingHelper(routingList);
    internet.Install(g_uavNodes);

    Ipv4AddressHelper ipv4;
    ipv4.SetBase("10.1.1.0", "255.255.255.0");
    ipv4.Assign(devices);

    if (!mapFile.empty())
    {
        LoadSceneGeometryFromMap(mapFile);
    }

    CreateInterferenceNodes(theChannel);

    if (hasBuildings)
    {
        BuildingsHelper::Install(NodeContainer::GetGlobal());
    }

    InitializeObservationNamespaces();
    g_environmentSummary.observationObserverCount =
        static_cast<uint32_t>(g_observationRuntime.observerIds.size());
    g_environmentSummary.observationTargetObjectCount =
        static_cast<uint32_t>(g_observationRuntime.targetObjectKeys.size());

    g_tdma.enabled = true;
    SetupTDMATraffic();

    g_flowMonitor = g_flowHelper.Install(g_uavNodes);

    InitializeResourceAssignments();
    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i)
    {
        g_cooperativeRuntime.effectiveCooperativeNodes.insert(i);
    }
}
