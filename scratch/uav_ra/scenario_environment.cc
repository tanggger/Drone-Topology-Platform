#include "context.h"

#include <cctype>
#include <regex>
#pragma GCC optimize("no-tree-vrp")

NS_LOG_COMPONENT_DEFINE("UAVResourceAllocationScenario");

namespace
{

struct ParsedSceneFeature
{
    std::string featureType;
    std::string sceneType;
    std::string geometryType;
    std::string densityClass;
    std::string waterType;
    std::string surfaceType;
    std::string name;
    bool enabled = true;
    double heightM = 0.0;
    double canopyHeightM = 0.0;
    std::vector<Vector> points;
};

struct BuildingFootprint
{
    double minX = 0.0;
    double maxX = 0.0;
    double minY = 0.0;
    double maxY = 0.0;
    double heightM = 0.0;
};

double DensityClassToWeight(const std::string& densityClass)
{
    if (densityClass == "high")
    {
        return 1.35;
    }
    if (densityClass == "low")
    {
        return 0.70;
    }
    return 1.0;
}

double WaterTypeToWeight(const std::string& waterType)
{
    if (waterType == "river")
    {
        return 0.75;
    }
    if (waterType == "pond")
    {
        return 0.85;
    }
    return 1.0;
}

void ResetSceneOverlayRegions()
{
    g_forestRegions.clear();
    g_waterRegions.clear();
    g_openFieldRegions.clear();
}

void StoreOverlayRegion(const ParsedSceneFeature& feature)
{
    SceneOverlayRegion region;
    region.featureType = feature.featureType;
    region.sceneType = feature.sceneType;
    region.name = feature.name;
    region.densityClass = feature.densityClass;
    region.waterType = feature.waterType;
    region.surfaceType = feature.surfaceType;
    region.points = feature.points;
    region.heightM = feature.featureType == "forest" ? feature.canopyHeightM : feature.heightM;
    region.weight = 1.0;

    if (feature.featureType == "forest")
    {
        region.weight = DensityClassToWeight(feature.densityClass);
        g_forestRegions.push_back(region);
        return;
    }

    if (feature.featureType == "water")
    {
        region.weight = WaterTypeToWeight(feature.waterType);
        g_waterRegions.push_back(region);
        return;
    }

    if (feature.featureType == "open_field")
    {
        g_openFieldRegions.push_back(region);
    }
}

bool EndsWithCaseInsensitive(const std::string& value, const std::string& suffix)
{
    if (value.size() < suffix.size())
    {
        return false;
    }

    for (size_t i = 0; i < suffix.size(); ++i)
    {
        char lhs = static_cast<char>(std::tolower(value[value.size() - suffix.size() + i]));
        char rhs = static_cast<char>(std::tolower(suffix[i]));
        if (lhs != rhs)
        {
            return false;
        }
    }
    return true;
}

std::string ReadFileToString(const std::string& filePath)
{
    std::ifstream file(filePath);
    if (!file.is_open())
    {
        return "";
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

size_t FindMatching(const std::string& text, size_t start, char openChar, char closeChar)
{
    if (start >= text.size() || text[start] != openChar)
    {
        return std::string::npos;
    }

    int depth = 0;
    bool inString = false;
    bool escape = false;
    for (size_t i = start; i < text.size(); ++i)
    {
        char c = text[i];
        if (inString)
        {
            if (escape)
            {
                escape = false;
            }
            else if (c == '\\')
            {
                escape = true;
            }
            else if (c == '"')
            {
                inString = false;
            }
            continue;
        }

        if (c == '"')
        {
            inString = true;
            continue;
        }
        if (c == openChar)
        {
            ++depth;
        }
        else if (c == closeChar)
        {
            --depth;
            if (depth == 0)
            {
                return i;
            }
        }
    }
    return std::string::npos;
}

std::string ExtractObjectByKey(const std::string& text, const std::string& key)
{
    size_t keyPos = text.find("\"" + key + "\"");
    if (keyPos == std::string::npos)
    {
        return "";
    }

    size_t openPos = text.find('{', keyPos);
    if (openPos == std::string::npos)
    {
        return "";
    }

    size_t closePos = FindMatching(text, openPos, '{', '}');
    if (closePos == std::string::npos)
    {
        return "";
    }

    return text.substr(openPos, closePos - openPos + 1);
}

std::string ExtractArrayByKey(const std::string& text, const std::string& key)
{
    size_t keyPos = text.find("\"" + key + "\"");
    if (keyPos == std::string::npos)
    {
        return "";
    }

    size_t openPos = text.find('[', keyPos);
    if (openPos == std::string::npos)
    {
        return "";
    }

    size_t closePos = FindMatching(text, openPos, '[', ']');
    if (closePos == std::string::npos)
    {
        return "";
    }

    return text.substr(openPos, closePos - openPos + 1);
}

std::string ExtractJsonStringField(const std::string& text, const std::string& key)
{
    std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (std::regex_search(text, match, pattern))
    {
        return match[1].str();
    }
    return "";
}

bool ExtractJsonBoolField(const std::string& text, const std::string& key, bool defaultValue)
{
    std::regex pattern("\"" + key + "\"\\s*:\\s*(true|false)");
    std::smatch match;
    if (std::regex_search(text, match, pattern))
    {
        return match[1].str() == "true";
    }
    return defaultValue;
}

double ExtractJsonDoubleField(const std::string& text,
                              const std::string& key,
                              double defaultValue)
{
    std::regex pattern("\"" + key + "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?)");
    std::smatch match;
    if (std::regex_search(text, match, pattern))
    {
        return std::stod(match[1].str());
    }
    return defaultValue;
}

std::vector<Vector> ExtractCoordinatePairs(const std::string& coordinatesJson)
{
    std::vector<Vector> points;
    std::regex numberPattern("-?[0-9]+(?:\\.[0-9]+)?");
    std::sregex_iterator iter(coordinatesJson.begin(), coordinatesJson.end(), numberPattern);
    std::sregex_iterator end;

    std::vector<double> values;
    for (; iter != end; ++iter)
    {
        values.push_back(std::stod(iter->str()));
    }

    for (size_t i = 0; i + 1 < values.size(); i += 2)
    {
        points.emplace_back(values[i], values[i + 1], 0.0);
    }
    return points;
}

void UpdateSceneBounds(double x, double y, bool& firstPoint)
{
    if (firstPoint)
    {
        g_config.minX = x;
        g_config.maxX = x;
        g_config.minY = y;
        g_config.maxY = y;
        firstPoint = false;
        return;
    }

    g_config.minX = std::min(g_config.minX, x);
    g_config.maxX = std::max(g_config.maxX, x);
    g_config.minY = std::min(g_config.minY, y);
    g_config.maxY = std::max(g_config.maxY, y);
}

void FinalizeBoundsWithMargin(bool hadAnyPoint)
{
    if (!hadAnyPoint)
    {
        return;
    }

    const double margin = 5.0;
    g_config.minX -= margin;
    g_config.maxX += margin;
    g_config.minY -= margin;
    g_config.maxY += margin;
}

void UpdateGeometrySummary(const ParsedSceneFeature& feature)
{
    if (feature.featureType == "building")
    {
        ++g_environmentSummary.buildingFeatureCount;
        g_environmentSummary.hasBuildings = true;
        g_environmentSummary.maxGeometryHeightM =
            std::max(g_environmentSummary.maxGeometryHeightM, feature.heightM);
        return;
    }

    if (feature.featureType == "forest")
    {
        ++g_environmentSummary.forestFeatureCount;
        g_environmentSummary.hasVegetation = true;
        if (g_environmentSummary.primaryForestDensityClass.empty())
        {
            g_environmentSummary.primaryForestDensityClass = feature.densityClass;
        }
        g_environmentSummary.maxGeometryHeightM =
            std::max(g_environmentSummary.maxGeometryHeightM, feature.canopyHeightM);
        return;
    }

    if (feature.featureType == "water")
    {
        ++g_environmentSummary.waterFeatureCount;
        g_environmentSummary.hasWaterSurface = true;
        if (g_environmentSummary.primaryWaterType.empty())
        {
            g_environmentSummary.primaryWaterType = feature.waterType;
        }
        return;
    }

    if (feature.featureType == "open_field")
    {
        ++g_environmentSummary.openFieldFeatureCount;
        if (g_environmentSummary.primaryOpenFieldSurfaceType.empty())
        {
            g_environmentSummary.primaryOpenFieldSurfaceType = feature.surfaceType;
        }
    }
}

double ComputeFootprintArea(const BuildingFootprint& building)
{
    return std::max(0.0, building.maxX - building.minX) *
           std::max(0.0, building.maxY - building.minY);
}

double ComputeAxisGap(double aMin, double aMax, double bMin, double bMax)
{
    if (aMax < bMin)
    {
        return bMin - aMax;
    }
    if (bMax < aMin)
    {
        return aMin - bMax;
    }
    return 0.0;
}

double ComputeApproxStreetWidth(const std::vector<BuildingFootprint>& buildings)
{
    if (buildings.size() < 2)
    {
        return 0.0;
    }

    double gapSum = 0.0;
    uint32_t gapCount = 0;
    for (size_t i = 0; i < buildings.size(); ++i)
    {
        double bestGap = std::numeric_limits<double>::infinity();
        for (size_t j = 0; j < buildings.size(); ++j)
        {
            if (i == j)
            {
                continue;
            }

            double xGap = ComputeAxisGap(buildings[i].minX,
                                         buildings[i].maxX,
                                         buildings[j].minX,
                                         buildings[j].maxX);
            double yGap = ComputeAxisGap(buildings[i].minY,
                                         buildings[i].maxY,
                                         buildings[j].minY,
                                         buildings[j].maxY);

            double candidateGap = std::numeric_limits<double>::infinity();
            bool yOverlap = yGap == 0.0;
            bool xOverlap = xGap == 0.0;
            if (yOverlap && xGap > 0.0)
            {
                candidateGap = xGap;
            }
            else if (xOverlap && yGap > 0.0)
            {
                candidateGap = yGap;
            }

            if (candidateGap < bestGap)
            {
                bestGap = candidateGap;
            }
        }

        if (std::isfinite(bestGap))
        {
            gapSum += bestGap;
            ++gapCount;
        }
    }

    if (gapCount == 0)
    {
        return 0.0;
    }
    return gapSum / gapCount;
}

// Helper to avoid GCC ICE in complex loops by isolating logic
static bool IsPointInAnyBuilding(const Vector& p)
{
    for (BuildingList::Iterator bit = BuildingList::Begin(); bit != BuildingList::End(); ++bit)
    {
        Box box = (*bit)->GetBoundaries();
        if (p.x >= box.xMin - 2.0 && p.x <= box.xMax + 2.0 &&
            p.y >= box.yMin - 2.0 && p.y <= box.yMax + 2.0 &&
            p.z <= box.zMax + 2.0)
        {
            return true;
        }
    }
    return false;
}

static bool IsSegmentBlocked(const Vector& p1, const Vector& p2)
{
    const double dx = p2.x - p1.x;
    const double dy = p2.y - p1.y;
    const double dz = p2.z - p1.z;
    const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (dist < 1e-3) return false;

    int steps = std::max(2, static_cast<int>(dist / 5.0));
    for (int s = 1; s <= steps; ++s)
    {
        double alpha = static_cast<double>(s) / steps;
        Vector check(p1.x + alpha * dx, p1.y + alpha * dy, p1.z + alpha * dz);
        if (IsPointInAnyBuilding(check))
        {
            return true;
        }
    }
    return false;
}

uint32_t CountBuildingCrossingsForSegment(const Vector& src,
                                          const Vector& dst,
                                          const std::vector<BuildingFootprint>& buildings)
{
    uint32_t crossings = 0;
    const double dx = dst.x - src.x;
    const double dy = dst.y - src.y;
    const double dz = dst.z - src.z;
    const int samples = std::max(
        12,
        static_cast<int>(std::sqrt(dx * dx + dy * dy + dz * dz) / 8.0));

    for (const auto& building : buildings)
    {
        bool intersects = false;
        for (int i = 0; i <= samples; ++i)
        {
            double alpha = static_cast<double>(i) / samples;
            double x = src.x + alpha * dx;
            double y = src.y + alpha * dy;
            double z = src.z + alpha * dz;

            if (x >= building.minX && x <= building.maxX &&
                y >= building.minY && y <= building.maxY &&
                z >= 0.0 && z <= building.heightM)
            {
                intersects = true;
                break;
            }
        }

        if (intersects)
        {
            ++crossings;
        }
    }

    return crossings;
}

void FinalizeUrbanGeometrySummary(const std::vector<BuildingFootprint>& buildings)
{
    if (buildings.empty())
    {
        return;
    }

    double totalHeight = 0.0;
    double totalFootprintArea = 0.0;
    double minX = buildings.front().minX;
    double maxX = buildings.front().maxX;
    double minY = buildings.front().minY;
    double maxY = buildings.front().maxY;

    for (const auto& building : buildings)
    {
        totalHeight += building.heightM;
        totalFootprintArea += ComputeFootprintArea(building);
        minX = std::min(minX, building.minX);
        maxX = std::max(maxX, building.maxX);
        minY = std::min(minY, building.minY);
        maxY = std::max(maxY, building.maxY);
    }

    double mapWidth = std::max(1.0, maxX - minX);
    double mapHeight = std::max(1.0, maxY - minY);
    double mapArea = mapWidth * mapHeight;

    g_environmentSummary.avgBuildingHeightM = totalHeight / buildings.size();
    g_environmentSummary.buildingDensityPerKm2 =
        static_cast<double>(buildings.size()) / mapArea * 1e6;
    g_environmentSummary.buildingCoverageRatio =
        std::min(1.0, totalFootprintArea / mapArea);
    g_environmentSummary.avgStreetWidthM = ComputeApproxStreetWidth(buildings);
}

void RefreshUrbanLosSummaryFromBuildings(const std::vector<BuildingFootprint>& buildings)
{
    if (buildings.empty())
    {
        g_environmentSummary.losDecisionMode = "fallback-losBaseProb";
        g_environmentSummary.losBlockedPairRatio = 0.0;
        g_environmentSummary.avgBuildingCrossingsPerPair = 0.0;
        return;
    }

    uint32_t pairCount = 0;
    uint32_t blockedPairCount = 0;
    uint32_t totalCrossings = 0;

    for (uint32_t i = 0; i < g_uavNodes.GetN(); ++i)
    {
        Ptr<MobilityModel> mobI = g_uavNodes.Get(i)->GetObject<MobilityModel>();
        if (!mobI)
        {
            continue;
        }

        for (uint32_t j = i + 1; j < g_uavNodes.GetN(); ++j)
        {
            Ptr<MobilityModel> mobJ = g_uavNodes.Get(j)->GetObject<MobilityModel>();
            if (!mobJ)
            {
                continue;
            }

            ++pairCount;
            uint32_t crossings = CountBuildingCrossingsForSegment(
                mobI->GetPosition(),
                mobJ->GetPosition(),
                buildings);
            totalCrossings += crossings;
            if (crossings > 0)
            {
                ++blockedPairCount;
            }
        }
    }

    g_environmentSummary.losDecisionMode = "geometry-building-priority";
    if (pairCount == 0)
    {
        g_environmentSummary.losBlockedPairRatio = 0.0;
        g_environmentSummary.avgBuildingCrossingsPerPair = 0.0;
        return;
    }

    g_environmentSummary.losBlockedPairRatio =
        static_cast<double>(blockedPairCount) / pairCount;
    g_environmentSummary.avgBuildingCrossingsPerPair =
        static_cast<double>(totalCrossings) / pairCount;
}

bool ParseGeoJsonFeatures(const std::string& jsonText,
                          std::vector<ParsedSceneFeature>& parsedFeatures)
{
    std::string featuresArray = ExtractArrayByKey(jsonText, "features");
    if (featuresArray.empty())
    {
        return false;
    }

    size_t i = 0;
    while (i < featuresArray.size())
    {
        if (featuresArray[i] != '{')
        {
            ++i;
            continue;
        }

        size_t closePos = FindMatching(featuresArray, i, '{', '}');
        if (closePos == std::string::npos)
        {
            break;
        }

        std::string featureJson = featuresArray.substr(i, closePos - i + 1);
        std::string propertiesJson = ExtractObjectByKey(featureJson, "properties");
        std::string geometryJson = ExtractObjectByKey(featureJson, "geometry");
        std::string coordinatesJson = ExtractArrayByKey(geometryJson, "coordinates");

        ParsedSceneFeature feature;
        feature.featureType = ExtractJsonStringField(propertiesJson, "feature_type");
        feature.sceneType = ExtractJsonStringField(propertiesJson, "scene_type");
        feature.name = ExtractJsonStringField(propertiesJson, "name");
        feature.enabled = ExtractJsonBoolField(propertiesJson, "enabled", true);
        feature.geometryType = ExtractJsonStringField(geometryJson, "type");
        feature.heightM = ExtractJsonDoubleField(propertiesJson, "height_m", 15.0);
        feature.canopyHeightM =
            ExtractJsonDoubleField(propertiesJson, "canopy_height_m", 12.0);
        feature.densityClass = ExtractJsonStringField(propertiesJson, "density_class");
        feature.waterType = ExtractJsonStringField(propertiesJson, "water_type");
        feature.surfaceType = ExtractJsonStringField(propertiesJson, "surface_type");
        feature.points = ExtractCoordinatePairs(coordinatesJson);

        if (!feature.featureType.empty() || (feature.heightM > 0.0 && feature.sceneType == "urban"))
        {
            if (feature.featureType.empty()) feature.featureType = "building";
            parsedFeatures.push_back(feature);
        }

        i = closePos + 1;
    }

    return !parsedFeatures.empty();
}

bool LoadLegacyBuildingBoxesFromText(const std::string& mapFile)
{
    std::cout << "🚧 正在从 " << mapFile << " 加载三维物理实体建筑..." << std::endl;
    std::ifstream bFile(mapFile);
    if (!bFile.is_open())
    {
        return false;
    }

    ResetSceneOverlayRegions();
    bool firstPoint = true;
    std::vector<BuildingFootprint> buildings;
    std::string line;
    while (std::getline(bFile, line))
    {
        if (line.empty() || line[0] == '#')
        {
            continue;
        }

        std::istringstream iss(line);
        double x1;
        double x2;
        double y1;
        double y2;
        double z1;
        double z2;
        if (!(iss >> x1 >> x2 >> y1 >> y2 >> z1 >> z2))
        {
            continue;
        }

        Ptr<Building> building = CreateObject<Building>();
        building->SetBoundaries(Box(x1, x2, y1, y2, z1, z2));
        building->SetExtWallsType(Building::ConcreteWithWindows);
        building->SetNFloors(std::max(1, static_cast<int>(z2 / 3.0)));

        ParsedSceneFeature feature;
        feature.featureType = "building";
        feature.heightM = z2;
        UpdateGeometrySummary(feature);
        buildings.push_back({x1, x2, y1, y2, z2});

        UpdateSceneBounds(x1, y1, firstPoint);
        UpdateSceneBounds(x2, y2, firstPoint);
    }

    FinalizeBoundsWithMargin(!firstPoint);
    FinalizeUrbanGeometrySummary(buildings);
    RefreshUrbanLosSummaryFromBuildings(buildings);
    return true;
}

bool LoadSceneGeometryFromGeoJson(const std::string& mapFile)
{
    std::string jsonText = ReadFileToString(mapFile);
    if (jsonText.empty())
    {
        return false;
    }

    std::vector<ParsedSceneFeature> features;
    if (!ParseGeoJsonFeatures(jsonText, features))
    {
        return false;
    }

    ResetSceneOverlayRegions();
    bool firstPoint = true;
    bool loadedAnyFeature = false;
    std::vector<BuildingFootprint> buildings;
    for (const auto& feature : features)
    {
        if (!feature.enabled || feature.points.empty())
        {
            continue;
        }

        UpdateGeometrySummary(feature);
        for (const auto& point : feature.points)
        {
            UpdateSceneBounds(point.x, point.y, firstPoint);
        }

        if (feature.featureType == "building")
        {
            double minX = feature.points.front().x;
            double maxX = feature.points.front().x;
            double minY = feature.points.front().y;
            double maxY = feature.points.front().y;
            for (const auto& point : feature.points)
            {
                minX = std::min(minX, point.x);
                maxX = std::max(maxX, point.x);
                minY = std::min(minY, point.y);
                maxY = std::max(maxY, point.y);
            }

            Ptr<Building> building = CreateObject<Building>();
            building->SetBoundaries(Box(minX, maxX, minY, maxY, 0.0, feature.heightM));
            building->SetExtWallsType(Building::ConcreteWithWindows);
            building->SetNFloors(std::max(1, static_cast<int>(feature.heightM / 3.0)));
            buildings.push_back({minX, maxX, minY, maxY, feature.heightM});
        }
        else
        {
            StoreOverlayRegion(feature);
        }

        loadedAnyFeature = true;
    }

    FinalizeBoundsWithMargin(!firstPoint);
    FinalizeUrbanGeometrySummary(buildings);
    RefreshUrbanLosSummaryFromBuildings(buildings);
    return loadedAnyFeature;
}

} // namespace

void RefreshUrbanLosSummary()
{
    if (!g_environmentSummary.hasBuildings)
    {
        g_environmentSummary.losDecisionMode = "fallback-losBaseProb";
        g_environmentSummary.losBlockedPairRatio = 0.0;
        g_environmentSummary.avgBuildingCrossingsPerPair = 0.0;
        return;
    }

    std::vector<BuildingFootprint> buildings;
    for (BuildingList::Iterator bit = BuildingList::Begin(); bit != BuildingList::End(); ++bit)
    {
        Box box = (*bit)->GetBoundaries();
        buildings.push_back({box.xMin, box.xMax, box.yMin, box.yMax, box.zMax});
    }

    RefreshUrbanLosSummaryFromBuildings(buildings);
}

bool IsObservationPathOccluded(const Vector& observerPos, const Vector& targetPos)
{
    std::vector<BuildingFootprint> buildings;
    for (BuildingList::Iterator bit = BuildingList::Begin(); bit != BuildingList::End(); ++bit)
    {
        Box box = (*bit)->GetBoundaries();
        buildings.push_back({box.xMin, box.xMax, box.yMin, box.yMax, box.zMax});
    }

    if (!buildings.empty() &&
        CountBuildingCrossingsForSegment(observerPos, targetPos, buildings) > 0)
    {
        return true;
    }

    for (const auto& region : g_forestRegions)
    {
        double depth = 0.0;
        if (region.points.size() >= 3)
        {
            const double dx = targetPos.x - observerPos.x;
            const double dy = targetPos.y - observerPos.y;
            const double dz = targetPos.z - observerPos.z;
            const double dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist > 1e-6)
            {
                const int samples = std::max(12, static_cast<int>(dist / 10.0));
                int insideSamples = 0;
                for (int i = 0; i <= samples; ++i)
                {
                    double alpha = static_cast<double>(i) / samples;
                    Vector point(observerPos.x + alpha * dx,
                                 observerPos.y + alpha * dy,
                                 observerPos.z + alpha * dz);

                    bool inside = false;
                    for (size_t p = 0, q = region.points.size() - 1; p < region.points.size();
                         q = p++)
                    {
                        const Vector& pi = region.points[p];
                        const Vector& pj = region.points[q];
                        bool intersect =
                            ((pi.y > point.y) != (pj.y > point.y)) &&
                            (point.x < (pj.x - pi.x) * (point.y - pi.y) /
                                               std::max(1e-9, (pj.y - pi.y)) +
                                           pi.x);
                        if (intersect)
                        {
                            inside = !inside;
                        }
                    }
                    if (inside)
                    {
                        ++insideSamples;
                    }
                }
                depth = dist * static_cast<double>(insideSamples) / (samples + 1);
            }
        }

        // Forest canopy should degrade observation quality, but not behave like
        // a solid building for every moderate crossing. Use a much deeper path
        // penetration threshold before declaring the path fully occluded.
        const double hardOcclusionDepthM =
            g_environmentConfig.sceneType == "forest" ? 55.0 : 35.0;
        if (depth >= hardOcclusionDepthM)
        {
            return true;
        }
    }

    return false;
}

Vector ApplyRTKNoise(const Vector& originalPos, double time)
{
    Vector noisyPos = originalPos;
    if (g_diffParams.rtkNoiseStdDev == 0.0 && g_diffParams.rtkDriftInterval == 0.0)
    {
        return noisyPos;
    }

    Ptr<NormalRandomVariable> normalRand = CreateObject<NormalRandomVariable>();
    normalRand->SetAttribute("Mean", DoubleValue(0.0));
    normalRand->SetAttribute(
        "Variance",
        DoubleValue(g_diffParams.rtkNoiseStdDev * g_diffParams.rtkNoiseStdDev));

    noisyPos.x += normalRand->GetValue();
    noisyPos.y += normalRand->GetValue();
    noisyPos.z += normalRand->GetValue() * 0.5;

    if (g_diffParams.rtkDriftInterval > 0)
    {
        double cycleTime = fmod(time, g_diffParams.rtkDriftInterval);
        if (cycleTime < g_diffParams.rtkDriftDuration)
        {
            double driftFactor =
                1.0 - exp(-3.0 * cycleTime / g_diffParams.rtkDriftDuration);
            if (!g_randVar)
            {
                g_randVar = CreateObject<UniformRandomVariable>();
            }
            noisyPos.x += g_diffParams.rtkDriftMagnitude * driftFactor *
                          (g_randVar->GetValue() - 0.5) * 2.0;
            noisyPos.y += g_diffParams.rtkDriftMagnitude * driftFactor *
                          (g_randVar->GetValue() - 0.5) * 2.0;
        }
    }

    if ((g_environmentSummary.hasBuildings || g_environmentConfig.useBuildingGeometry) &&
        noisyPos.z < 0.5)
    {
        noisyPos.z = 0.5;
    }

    return noisyPos;
}

bool LoadFormationTrajectory(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "无法打开轨迹文件: " << filename << std::endl;
        return false;
    }

    std::string line;
    std::getline(file, line); // 跳过首行(注释或表头)

    g_trajectoryData.clear();
    g_nodeTrajectories.clear();

    double maxTime = 0.0;
    uint32_t maxNodeId = 0;
    
    // 初始化边界，以便在读取轨迹时更新
    // 如果没有点数据，使用默认 0~500
    // 读取到一个点后，立即更新 min/max
    bool firstPoint = true;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string token;
        std::vector<std::string> tokens;

        while (std::getline(iss, token, ',')) {
            tokens.push_back(token);
        }

        if (tokens.size() >= 5) {
            TrajectoryPoint point;
            point.time   = std::stod(tokens[0]);
            point.nodeId = std::stoul(tokens[1]);
            point.x      = std::stod(tokens[2]);
            point.y      = std::stod(tokens[3]);
            point.z      = std::max(0.5, std::stod(tokens[4]));

            g_trajectoryData.push_back(point);
            g_nodeTrajectories[point.nodeId].push_back(point);

            maxTime   = std::max(maxTime, point.time);
            maxNodeId = std::max(maxNodeId, point.nodeId);
            
            // 更新场景边界
            if (firstPoint) {
                g_config.minX = point.x;
                g_config.maxX = point.x;
                g_config.minY = point.y;
                g_config.maxY = point.y;
                firstPoint = false;
            } else {
                if (point.x < g_config.minX) g_config.minX = point.x;
                if (point.x > g_config.maxX) g_config.maxX = point.x;
                if (point.y < g_config.minY) g_config.minY = point.y;
                if (point.y > g_config.maxY) g_config.maxY = point.y;
            }
        }
    }
    file.close();

    // 适当扩充边界，给黑飞留点周围空间
    // Fix: 不能无限扩充，必须限制在用户指定的地图边界内（如果有）
    // 或者仅仅扩充一个很小的值，避免飞出去太远
    double margin = 10.0;
    // g_config.minX -= margin; // 移除向负方向的盲目扩充
    // g_config.maxX += margin;
    // g_config.minY -= margin;
    // g_config.maxY += margin;
    // 改为更保守的扩充，且尽量保持在 0 以上 (如果原始轨迹就在 0 以上)
    if (g_config.minX > 0) g_config.minX = std::max(0.0, g_config.minX - margin);
    else g_config.minX -= margin; // 如果本来就是负的，那说明确实需要飞到负区域
    
    g_config.maxX += margin;
    
    if (g_config.minY > 0) g_config.minY = std::max(0.0, g_config.minY - margin);
    else g_config.minY -= margin;

    g_config.maxY += margin;
    
    std::cout << "场景边界已更新: X[" << g_config.minX << ", " << g_config.maxX 
              << "] Y[" << g_config.minY << ", " << g_config.maxY << "]" << std::endl;

    // 按时间排序并去除非严格递增的时间点
    for (auto& entry : g_nodeTrajectories) {
        auto& trajectory = entry.second;
        std::sort(trajectory.begin(), trajectory.end(),
                  [](const TrajectoryPoint& a, const TrajectoryPoint& b) {
                      return a.time < b.time;
                  });
        std::vector<TrajectoryPoint> cleaned;
        cleaned.reserve(trajectory.size());
        double lastTime = -std::numeric_limits<double>::infinity();
        for (const auto& pt : trajectory) {
            if (pt.time > lastTime) {
                cleaned.push_back(pt);
                lastTime = pt.time;
            }
        }
        trajectory.swap(cleaned);
        if (!trajectory.empty()) {
            maxTime = std::max(maxTime, trajectory.back().time);
        }
    }

    g_trajectoryEndTime = maxTime;

    std::cout << "成功加载编队轨迹数据:" << std::endl;
    std::cout << "  节点数量: " << (maxNodeId + 1) << std::endl;
    std::cout << "  轨迹时长: " << g_trajectoryEndTime << " 秒" << std::endl;
    std::cout << "  数据点数: " << g_trajectoryData.size() << std::endl;

    return true;
}

// 将轨迹数据设置为 WaypointMobilityModel
void SetupFormationMobility(NodeContainer& nodes)
{
    std::cout << "设置编队移动模型 (WaypointMobilityModel)..." << std::endl;

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::WaypointMobilityModel");
    mobility.Install(nodes);

    for (uint32_t nodeId = 0; nodeId < nodes.GetN(); ++nodeId) {
        Ptr<WaypointMobilityModel> waypoint =
            nodes.Get(nodeId)->GetObject<WaypointMobilityModel>();

        if (g_nodeTrajectories.find(nodeId) != g_nodeTrajectories.end()) {
            auto& trajectory = g_nodeTrajectories[nodeId];
            std::cout << "  节点 " << nodeId << ": " << trajectory.size() << " 个waypoint" << std::endl;
            for (const auto& point : trajectory) {
                Vector originalPos(point.x, point.y, point.z);
                Vector noisyPos = ApplyRTKNoise(originalPos, point.time);
                if ((g_environmentSummary.hasBuildings || g_environmentConfig.useBuildingGeometry) &&
                    noisyPos.z < 0.5)
                {
                    noisyPos.z = 0.5;
                }
                waypoint->AddWaypoint(Waypoint(Seconds(point.time), noisyPos));
            }
        } else {
            std::cerr << "警告: 节点 " << nodeId << " 没有轨迹数据，使用静止位置" << std::endl;
            waypoint->AddWaypoint(Waypoint(Seconds(0.0), Vector(0, 0, 50)));
            
            double endTime = std::max(g_trajectoryEndTime, 1.0); // 至少持续 1 秒
            waypoint->AddWaypoint(Waypoint(Seconds(endTime), Vector(0, 0, 50)));
        }
    }
}

// Use noinline to keep function separate and avoid giant loop analysis blocks that trigger ICE
__attribute__((noinline))
bool LoadSceneGeometryFromMap(const std::string& mapFile)
{
    if (mapFile.empty())
    {
        return false;
    }

    bool loaded = false;
    if (EndsWithCaseInsensitive(mapFile, ".geojson") ||
        EndsWithCaseInsensitive(mapFile, ".json"))
    {
        std::cout << "🗺️ 正在从 GeoJSON 场景文件加载几何: " << mapFile << std::endl;
        loaded = LoadSceneGeometryFromGeoJson(mapFile);
    }
    else
    {
        loaded = LoadLegacyBuildingBoxesFromText(mapFile);
    }

    if (!loaded)
    {
        std::cerr << "场景几何加载失败: " << mapFile << std::endl;
        return false;
    }

    std::ostringstream contribution;
    contribution << g_environmentSummary.sceneType << " scene, source="
                 << g_environmentConfig.environmentSource
                 << ", features(building=" << g_environmentSummary.buildingFeatureCount
                 << ", forest=" << g_environmentSummary.forestFeatureCount
                 << ", water=" << g_environmentSummary.waterFeatureCount
                 << ", open_field=" << g_environmentSummary.openFieldFeatureCount << ")";
    g_environmentSummary.environmentContributionSummary = contribution.str();

    if (g_environmentSummary.buildingFeatureCount > 0)
    {
        g_environmentSummary.effectiveModelSummary += " + geometry-backed buildings";
    }
    else if (g_environmentSummary.forestFeatureCount > 0 ||
             g_environmentSummary.waterFeatureCount > 0 ||
             g_environmentSummary.openFieldFeatureCount > 0)
    {
        g_environmentSummary.effectiveModelSummary += " + GeoJSON scene overlays";
    }

    return true;
}

// ==================== 资源分配配置 ====================

void CreateInterferenceNodes(Ptr<YansWifiChannel> channel)
{
    if (!g_diffParams.enableInterference || g_diffParams.numInterferenceNodes == 0) return;

    std::cout << "创建 " << g_diffParams.numInterferenceNodes
              << " 个动态黑飞节点 (随机漂移飞行)..." << std::endl;

    g_interferenceNodes.Create(g_diffParams.numInterferenceNodes);

    MobilityHelper mobility;
    mobility.SetMobilityModel("ns3::WaypointMobilityModel");
    mobility.Install(g_interferenceNodes);

    Ptr<UniformRandomVariable> rng = CreateObject<UniformRandomVariable>();
    double margin           = 20.0;
    double baseZ            = g_config.uavHeight;
    double waypointInterval = 15.0;
    const double safeMinX = std::max(0.0, g_config.minX + margin);
    const double safeMinY = std::max(0.0, g_config.minY + margin);
    const double safeMaxX = std::max(safeMinX + 1.0, g_config.maxX - margin);
    const double safeMaxY = std::max(safeMinY + 1.0, g_config.maxY - margin);

    for (uint32_t i = 0; i < g_interferenceNodes.GetN(); ++i) {
        Ptr<WaypointMobilityModel> wpm =
            g_interferenceNodes.Get(i)->GetObject<WaypointMobilityModel>();
        
        double curX = 0, curY = 0, curZ = baseZ;
        int initRetries = 20;
        while (initRetries-- > 0) {
            curX = rng->GetValue(safeMinX, safeMaxX);
            curY = rng->GetValue(safeMinY, safeMaxY);
            curZ = rng->GetValue(baseZ - 10.0, baseZ + 10.0);
            if (curZ < 0.5) curZ = 0.5;
            
            if (!IsPointInAnyBuilding(Vector(curX, curY, curZ))) break;
            if (initRetries < 5) { 
                curZ += 30.0; // Fail-safe: lift up
                break;
            }
        }
        
        wpm->AddWaypoint(Waypoint(Seconds(0.0), Vector(curX, curY, curZ)));

        for (double t = waypointInterval; t <= g_config.duration; t += waypointInterval) {
            int retriesCount = 20;
            bool validMove = false;
            double nextX = curX, nextY = curY, nextZ = curZ;

            while (retriesCount-- > 0) {
                double candX = curX + rng->GetValue(-100.0, 100.0);
                double candY = curY + rng->GetValue(-100.0, 100.0);
                double candZ = curZ + rng->GetValue(-10.0, 10.0);

                if (candX < safeMinX) candX = safeMinX + 5;
                if (candX > safeMaxX) candX = safeMaxX - 5;
                if (candY < safeMinY) candY = safeMinY + 5;
                if (candY > safeMaxY) candY = safeMaxY - 5;
                candZ = std::max(baseZ - 15.0, std::min(baseZ + 30.0, candZ));

                if (!IsSegmentBlocked(Vector(curX, curY, curZ), Vector(candX, candY, candZ))) {
                    nextX = candX; nextY = candY; nextZ = candZ;
                    validMove = true;
                    break;
                }
            }

            if (!validMove) {
                nextZ = curZ + 2.0;
            }

            curX = nextX; curY = nextY; curZ = nextZ;
            wpm->AddWaypoint(Waypoint(Seconds(t), Vector(curX, curY, curZ)));
        }
    }

    WifiHelper wifi;
    wifi.SetStandard(WIFI_STANDARD_80211a);
    wifi.SetRemoteStationManager("ns3::ConstantRateWifiManager",
                                 "DataMode", StringValue("OfdmRate6Mbps"),
                                 "ControlMode", StringValue("OfdmRate6Mbps"));
    
    YansWifiPhyHelper phy;
    phy.SetChannel(channel);
    phy.Set("TxPowerStart", DoubleValue(30.0));
    phy.Set("TxPowerEnd", DoubleValue(30.0));
    
    WifiMacHelper mac;
    mac.SetType("ns3::AdhocWifiMac");
    NetDeviceContainer interferenceDevices = wifi.Install(phy, mac, g_interferenceNodes);
    
    InternetStackHelper stack;
    stack.Install(g_interferenceNodes);
    
    Ipv4AddressHelper interferenceIpv4;
    interferenceIpv4.SetBase("10.2.1.0", "255.255.255.0");
    Ipv4InterfaceContainer interferenceInterfaces = interferenceIpv4.Assign(interferenceDevices);
    
    uint16_t port = 8888;
    for (uint32_t i = 0; i < g_interferenceNodes.GetN(); ++i) {
        double rateMbps = g_diffParams.interferenceRateMbps;
        double onTime   = std::max(0.01, std::min(0.99, g_diffParams.interferenceDutyCycle));
        double offTime  = 1.0 - onTime;
        
        std::string dataRate = (rateMbps >= 1.0) ? 
            std::to_string((int)rateMbps) + "Mbps" : 
            std::to_string((int)(rateMbps * 1000)) + "kbps";
        
        uint32_t pktSize = 512;
        if (rateMbps >= 4.0) pktSize = 1300;
        if (rateMbps >= 6.0) pktSize = 1472;
        
        OnOffHelper onoff("ns3::UdpSocketFactory",
                         InetSocketAddress(Ipv4Address("255.255.255.255"), port));
        onoff.SetAttribute("PacketSize", UintegerValue(pktSize));
        onoff.SetAttribute("DataRate", StringValue(dataRate));
        onoff.SetAttribute("OnTime", StringValue("ns3::ConstantRandomVariable[Constant=" + std::to_string(onTime) + "]"));
        onoff.SetAttribute("OffTime", StringValue("ns3::ConstantRandomVariable[Constant=" + std::to_string(offTime) + "]"));
        
        ApplicationContainer app = onoff.Install(g_interferenceNodes.Get(i));
        app.Start(Seconds(1.0));
        app.Stop(Seconds(g_config.duration));
    }
}

// ==================== 主函数 ====================
