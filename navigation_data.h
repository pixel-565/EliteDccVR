#pragma once
#include "vendor/json/json.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <condition_variable>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <numbers>
#include <set>
#include <shlobj.h>
#include <sstream>
#include <thread>

namespace NavigationData
{
using Json = nlohmann::json;
inline std::string text(const Json &j, const char *key)
{
    auto i = j.find(key);
    return i != j.end() && i->is_string() ? i->get<std::string>() : "";
}
inline uint64_t number(const Json &j, const char *key)
{
    auto i = j.find(key);
    return i != j.end() && i->is_number_unsigned() ? i->get<uint64_t>() : 0;
}
inline int count(const Json &j, const char *key)
{
    auto i = j.find(key);
    if (i == j.end() || !i->is_number_integer())
        return -1;
    auto n = i->get<int64_t>();
    return n >= 0 && n <= 1000000 ? static_cast<int>(n) : -1;
}
inline int samplePollingInterval(const Json &j, int previous = 100)
{
    int value = count(j, "StatusReadIntervalMs");
    return value >= 20 && value <= 2000 ? value : previous;
}
inline std::time_t timestamp(const Json &j)
{
    std::tm t{};
    std::istringstream input(text(j, "timestamp"));
    input >> std::get_time(&t, "%Y-%m-%dT%H:%M:%S");
    return input.fail() ? 0 : _mkgmtime(&t);
}
inline bool scoopable(const std::string &star)
{
    return star.size() == 1 && std::string("OBAFGKM").find(star[0]) != std::string::npos;
}
struct Stop
{
    uint64_t address = 0;
    std::string name, star;
    std::array<double, 3> position{};
    bool hasPosition = false;
};
inline double jumpDistance(const Stop &a, const Stop &b)
{
    if (!a.hasPosition || !b.hasPosition)
        return -1;
    return std::hypot(a.position[0] - b.position[0], a.position[1] - b.position[1],
                      a.position[2] - b.position[2]);
}
struct Planet
{
    std::string name, planetClass, atmosphere, volcanism;
    double temperature = -1, gravity = -1, radius = -1;
    int bioCount = -1, geoCount = -1;
    std::map<std::string, std::string> bio, geo, bioSymbols;
    std::map<std::string, int> bioRanks;
    std::map<std::string, int> bioSamples;
    std::set<std::string> organicEvents;
    static std::string bioIdentity(const std::string &symbol, const std::string &label)
    {
        std::string identity = label;
        if (identity.starts_with("$"))
        {
            identity = symbol;
            auto prefix = identity.find("Codex_Ent_");
            if (prefix != std::string::npos)
                identity = identity.substr(prefix + 10);
            identity = identity.substr(0, identity.find('_'));
        }
        else
            identity = identity.substr(0, identity.find(' '));
        std::transform(identity.begin(), identity.end(), identity.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return identity;
    }
    void mergeBio(const std::string &symbol, const std::string &label, int rank)
    {
        if (symbol.empty() || label.empty())
            return;
        auto identity = bioIdentity(symbol, label);
        if (rank >= bioRanks[identity])
        {
            bio[identity] = label;
            bioSymbols[identity] = symbol;
            bioRanks[identity] = rank;
        }
    }
    bool hasBio() const
    {
        return bioCount > 0 || !bio.empty();
    }
    bool hasGeo() const
    {
        return geoCount > 0 || !geo.empty();
    }
};
inline std::string localName(const Json &j, const char *key, const char *localized)
{
    auto value = text(j, localized);
    return value.empty() ? text(j, key) : value;
}
struct SurfacePosition
{
    double latitude = 0, longitude = 0;
    bool valid = false;
};
inline double surfaceDistance(const SurfacePosition &a, const SurfacePosition &b, double radius)
{
    if (!a.valid || !b.valid || !std::isfinite(radius) || radius <= 0)
        return -1;
    constexpr double radians = std::numbers::pi / 180.;
    double latitude = (b.latitude - a.latitude) * radians;
    double longitude = (b.longitude - a.longitude) * radians;
    double h = std::pow(std::sin(latitude * .5), 2) + std::cos(a.latitude * radians) *
                                                          std::cos(b.latitude * radians) *
                                                          std::pow(std::sin(longitude * .5), 2);
    return 2 * radius * std::asin(std::sqrt(std::clamp(h, 0., 1.)));
}
struct Snapshot
{
    std::string commander, system, star, next, nextStar, destination, body;
    std::string status = "Loading journal history...";
    std::string destinationStar, following, followingStar;
    double nextDistance = -1, followingDistance = -1, remainingDistance = -1;
    int bodies = -1, jumps = -1;
    bool landed = false, surfaceKnown = false;
    int bodyId = -1;
    std::string surfaceMode = "Unknown";
    uint64_t systemAddress = 0;
    int organicSamples = 0;
    std::string organicIdentity;
    std::array<double, 2> sampleDistances{-1, -1};
};
// This reducer knows nothing about VR, file watching or rendering.
struct State
{
    struct System
    {
        std::string star;
        int bodies = -1, arrivalBody = -1;
        std::map<int, std::string> stars;
    };
    Snapshot view;
    std::map<std::pair<uint64_t, int>, Planet> planets;
    std::map<uint64_t, System> systems;
    std::vector<Stop> route;
    uint64_t address = 0;
    std::string fid;
    std::pair<uint64_t, int> activeOrganicPlanet{};
    std::string activeOrganic, activeOrganicStart;
    // Only the unfinished sampling set is persisted; completed sets have no stored positions.
    std::array<SurfacePosition, 2> samplePositions{};
    SurfacePosition currentPosition;
    std::string positionBody;
    uint64_t positionSystem = 0;
    double positionRadius = -1;
    void updatePosition(const Json &j)
    {
        currentPosition = {};
        positionBody = text(j, "BodyName");
        positionSystem = view.systemAddress;
        positionRadius = -1;
        if (!(number(j, "Flags") & (uint64_t(1) << 21)) || positionBody.empty() || !j.contains("Latitude") ||
            !j["Latitude"].is_number() || !j.contains("Longitude") || !j["Longitude"].is_number() ||
            !timestamp(j) || timestamp(j) < locationStamp)
            return;
        double lat = j["Latitude"].get<double>(), lon = j["Longitude"].get<double>();
        if (!std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat) > 90 || std::abs(lon) > 180)
            return;
        currentPosition = {lat, lon, true};
        if (j.contains("PlanetRadius") && j["PlanetRadius"].is_number())
        {
            double radius = j["PlanetRadius"].get<double>();
            if (std::isfinite(radius) && radius > 0)
                positionRadius = radius;
        }
    }
    Snapshot snapshotView() const
    {
        auto result = view;
        auto planet = planets.find(activeOrganicPlanet);
        if (activeOrganic.empty() || planet == planets.end() ||
            activeOrganicPlanet.first != view.systemAddress ||
            (activeOrganicPlanet.second != view.bodyId &&
             (view.body.empty() || planet->second.name != view.body)))
            return result;
        auto found = planet->second.bioSamples.find(activeOrganic);
        if (found == planet->second.bioSamples.end() || (found->second != 1 && found->second != 2))
            return result;
        result.organicSamples = found->second;
        result.organicIdentity = activeOrganic;
        bool sameBody = positionSystem == activeOrganicPlanet.first && positionBody == view.body;
        if (sameBody)
        {
            double radius = planet->second.radius > 0 ? planet->second.radius : positionRadius;
            for (int i = 0; i < result.organicSamples; ++i)
                result.sampleDistances[i] = surfaceDistance(samplePositions[i], currentPosition, radius);
        }
        return result;
    }
    Json sampleRecord() const
    {
        auto planet = planets.find(activeOrganicPlanet);
        if (activeOrganic.empty() || activeOrganicStart.empty() || view.commander.empty() ||
            planet == planets.end())
            return nullptr;
        auto samples = planet->second.bioSamples.find(activeOrganic);
        if (samples == planet->second.bioSamples.end() || samples->second < 1 || samples->second > 2 ||
            (!samplePositions[0].valid && !samplePositions[1].valid))
            return nullptr;
        Json positions = Json::array();
        for (const auto &position : samplePositions)
            positions.push_back(position.valid
                                    ? Json{{"Latitude", position.latitude}, {"Longitude", position.longitude}}
                                    : Json(nullptr));
        return Json{{"Schema", 1},
                    {"FID", fid},
                    {"Commander", view.commander},
                    {"SystemAddress", activeOrganicPlanet.first},
                    {"BodyID", activeOrganicPlanet.second},
                    {"Organism", activeOrganic},
                    {"SamplingSet", activeOrganicStart},
                    {"Samples", samples->second},
                    {"Positions", positions}};
    }
    bool restoreSamples(const Json &record)
    {
        try
        {
            if (!record.is_object() || record.value("Schema", 0) != 1)
                return false;
            auto savedFid = text(record, "FID");
            bool sameCommander = !savedFid.empty() && !fid.empty()
                                     ? savedFid == fid
                                     : !view.commander.empty() && text(record, "Commander") == view.commander;
            if (!sameCommander || activeOrganic.empty() || activeOrganicStart.empty() ||
                number(record, "SystemAddress") != activeOrganicPlanet.first ||
                count(record, "BodyID") != activeOrganicPlanet.second ||
                text(record, "Organism") != activeOrganic ||
                text(record, "SamplingSet") != activeOrganicStart)
                return false;
            auto planet = planets.find(activeOrganicPlanet);
            if (planet == planets.end())
                return false;
            auto samples = planet->second.bioSamples.find(activeOrganic);
            int savedSamples = count(record, "Samples");
            if (samples == planet->second.bioSamples.end() || samples->second < savedSamples ||
                samples->second > 2 || savedSamples < 1 || savedSamples > 2)
                return false;
            auto positions = record.find("Positions");
            if (positions == record.end() || !positions->is_array() || positions->size() != 2)
                return false;
            std::array<SurfacePosition, 2> restored{};
            for (int i = 0; i < savedSamples; ++i)
            {
                const auto &position = (*positions)[i];
                if (position.is_null())
                    continue;
                double lat = position.at("Latitude").get<double>(),
                       lon = position.at("Longitude").get<double>();
                if (!std::isfinite(lat) || !std::isfinite(lon) || std::abs(lat) > 90 || std::abs(lon) > 180)
                    return false;
                restored[i] = {lat, lon, true};
            }
            samplePositions = restored;
            return true;
        }
        catch (const Json::exception &)
        {
            return false;
        }
    }
    std::time_t routeStamp = 0, clearStamp = 0, locationStamp = 0;
    bool startLanded = false;
    bool matchesSystem(const Json &j) const
    {
        auto id = number(j, "SystemAddress");
        if (id && address)
            return id == address;
        auto system = text(j, "StarSystem");
        return system.empty() || system == view.system;
    }
    void clearSurface(bool known)
    {
        view.body.clear();
        view.bodyId = -1;
        view.landed = false;
        view.surfaceKnown = known;
        view.surfaceMode = known ? "Normal flight" : "Unknown";
    }
    void setSurfaceBody(const Json &j)
    {
        auto body = text(j, "Body");
        if (!body.empty())
        {
            if (body != view.body)
                view.bodyId = -1;
            view.body = body;
        }
        int id = count(j, "BodyID");
        if (id >= 0)
            view.bodyId = id;
    }
    void routeView()
    {
        view.next.clear();
        view.nextStar.clear();
        view.destination.clear();
        view.destinationStar.clear();
        view.following.clear();
        view.followingStar.clear();
        view.jumps = -1;
        view.nextDistance = view.followingDistance = view.remainingDistance = -1;
        for (size_t i = 0; i < route.size(); ++i)
        {
            if (address ? route[i].address != address : route[i].name != view.system)
                continue;
            double total = 0;
            for (size_t k = i; k + 1 < route.size(); ++k)
            {
                double distance = jumpDistance(route[k], route[k + 1]);
                if (distance < 0 || !std::isfinite(distance))
                {
                    total = -1;
                    break;
                }
                total += distance;
            }
            view.remainingDistance = total;
            if (i + 1 < route.size())
                view.nextDistance = jumpDistance(route[i], route[i + 1]);
            if (i + 2 < route.size())
                view.followingDistance = jumpDistance(route[i + 1], route[i + 2]);
            view.destination = route.back().name;
            view.destinationStar = route.back().star;
            if (i + 2 < route.size())
            {
                view.following = route[i + 2].name;
                view.followingStar = route[i + 2].star;
            }
            view.jumps = static_cast<int>(route.size() - i - 1);
            if (view.star.empty())
                view.star = route[i].star;
            if (i + 1 < route.size())
            {
                view.next = route[i + 1].name;
                view.nextStar = route[i + 1].star;
            }
            break;
        }
    }
    void setRoute(const Json &j)
    {
        auto time = timestamp(j);
        if (!time || time <= clearStamp || time < routeStamp || !j.contains("Route") ||
            !j["Route"].is_array())
            return;
        std::vector<Stop> next;
        for (const auto &s : j["Route"])
        {
            if (!s.is_object() || text(s, "StarSystem").empty())
                return;
            Stop stop{number(s, "SystemAddress"), text(s, "StarSystem"), text(s, "StarClass")};
            auto pos = s.find("StarPos");
            if (pos != s.end() && pos->is_array() && pos->size() == 3)
            {
                stop.hasPosition = true;
                for (size_t axis = 0; axis < 3; ++axis)
                {
                    if (!(*pos)[axis].is_number())
                    {
                        stop.hasPosition = false;
                        break;
                    }
                    stop.position[axis] = (*pos)[axis].get<double>();
                    if (!std::isfinite(stop.position[axis]))
                        stop.hasPosition = false;
                }
            }
            next.push_back(std::move(stop));
        }
        route = std::move(next);
        routeStamp = time;
        routeView();
    }
    void event(const Json &j, bool live = false)
    {
        auto kind = text(j, "event");
        if (kind == "Scan" || kind == "SAASignalsFound" || kind == "FSSBodySignals" ||
            kind == "ScanOrganic" || kind == "CodexEntry")
        {
            auto system = number(j, "SystemAddress");
            int body = count(j, kind == "ScanOrganic" ? "Body" : "BodyID");
            if (system && body >= 0)
            {
                auto &planet = planets[{system, body}];
                auto name = text(j, "BodyName");
                if (!name.empty())
                    planet.name = name;
                if (kind == "Scan" && !text(j, "PlanetClass").empty())
                {
                    planet.planetClass = text(j, "PlanetClass");
                    planet.atmosphere = text(j, "Atmosphere");
                    planet.volcanism = text(j, "Volcanism");
                    for (auto field : {"SurfaceTemperature", "SurfaceGravity", "Radius"})
                        if (j.contains(field) && j[field].is_number())
                        {
                            double value = j[field].get<double>();
                            if (!std::isfinite(value) || value < 0)
                                continue;
                            if (std::string(field) == "SurfaceTemperature")
                                planet.temperature = value;
                            else if (std::string(field) == "SurfaceGravity")
                                planet.gravity = value;
                            else
                                planet.radius = value;
                        }
                }
                if (j.contains("Signals") && j["Signals"].is_array())
                    for (const auto &signal : j["Signals"])
                    {
                        auto type = text(signal, "Type");
                        if (type == "$SAA_SignalType_Biological;")
                            planet.bioCount = count(signal, "Count");
                        if (type == "$SAA_SignalType_Geological;")
                            planet.geoCount = count(signal, "Count");
                    }
                if (j.contains("Genuses") && j["Genuses"].is_array())
                    for (const auto &genus : j["Genuses"])
                    {
                        auto key = text(genus, "Genus");
                        if (!key.empty())
                            planet.mergeBio(key, localName(genus, "Genus", "Genus_Localised"), 1);
                    }
                if (kind == "ScanOrganic")
                {
                    auto key = text(j, "Variant");
                    if (key.empty())
                        key = text(j, "Species");
                    auto organicName = localName(j, "Variant", "Variant_Localised");
                    if (organicName.empty())
                        organicName = localName(j, "Species", "Species_Localised");
                    if (!key.empty())
                    {
                        planet.mergeBio(key, organicName, text(j, "Variant").empty() ? 2 : 3);
                        auto identity = Planet::bioIdentity(key, organicName);
                        auto scan = text(j, "ScanType");
                        auto stamp = text(j, "timestamp");
                        // A repeated journal record must not count as another sample.
                        if (!stamp.empty() && !planet.organicEvents.insert(stamp + scan + key).second)
                            return;
                        auto &samples = planet.bioSamples[identity];
                        if (scan == "Log")
                        {
                            samplePositions = {};
                            if (!activeOrganic.empty())
                            {
                                auto &previous = planets[activeOrganicPlanet].bioSamples[activeOrganic];
                                if (previous < 3)
                                    previous = 0;
                            }
                            activeOrganicPlanet = {system, body};
                            activeOrganic = identity;
                            activeOrganicStart = stamp.empty() ? "" : stamp + "|" + key;
                            if (samples < 3)
                                samples = 1;
                        }
                        else if (scan == "Sample")
                        {
                            // Without the initial Log, partial history cannot identify the sample number.
                            if (samples > 0)
                                samples = std::min(3, samples + 1);
                            else
                                samples = -1;
                        }
                        else if (scan == "Analyse")
                        {
                            samples = 3;
                            if (activeOrganicPlanet == std::pair<uint64_t, int>{system, body} &&
                                activeOrganic == identity)
                                activeOrganic.clear();
                        }
                        if (live && (samples == 1 || samples == 2) && (scan == "Log" || scan == "Sample") &&
                            currentPosition.valid && positionSystem == system && !view.body.empty() &&
                            positionBody == view.body && (view.bodyId == body || planet.name == view.body))
                            samplePositions[samples - 1] = currentPosition;
                        if (samples == 3)
                        {
                            samplePositions = {};
                            activeOrganic.clear();
                        }
                    }
                }
                if (kind == "CodexEntry")
                {
                    auto sub = text(j, "SubCategory"), key = text(j, "Name");
                    if (!key.empty())
                    {
                        if (sub.find("Organic") != std::string::npos)
                            planet.mergeBio(key, localName(j, "Name", "Name_Localised"), 3);
                        else if (sub.find("Geolog") != std::string::npos)
                            planet.geo[key] = localName(j, "Name", "Name_Localised");
                    }
                }
            }
        }
        auto event = text(j, "event");
        if (event == "LoadGame" || event == "Commander")
        {
            auto nextFid = text(j, "FID");
            auto name = text(j, "Commander");
            if (name.empty())
                name = text(j, "Name");
            if ((!fid.empty() && !nextFid.empty() && fid != nextFid) ||
                (nextFid.empty() && !view.commander.empty() && !name.empty() && name != view.commander))
            {
                *this = State{};
                clearStamp = timestamp(j);
            }
            if (!nextFid.empty())
                fid = nextFid;
            if (!name.empty())
                view.commander = name;
            if (event == "LoadGame")
            {
                startLanded = j.contains("StartLanded") && j["StartLanded"] == true;
                if (startLanded)
                {
                    view.landed = true;
                    view.surfaceKnown = true;
                    view.surfaceMode = "Landed";
                }
                locationStamp = std::max(locationStamp, timestamp(j));
            }
        }
        if (event == "Location" || event == "FSDJump" || event == "CarrierJump")
        {
            auto name = text(j, "StarSystem");
            if (name.empty())
                return;
            auto newAddress = number(j, "SystemAddress");
            bool sameSystem = address && newAddress ? address == newAddress : view.system == name;
            auto body = text(j, "Body");
            int bodyId = count(j, "BodyID");
            bool sameBody = (bodyId >= 0 && view.bodyId >= 0) ? bodyId == view.bodyId
                                                              : !body.empty() && body == view.body;
            if (event != "Location" || !sameSystem || (!body.empty() && !sameBody))
                clearSurface(event != "Location");
            address = newAddress;
            view.systemAddress = address;
            view.system = name;
            if (address && text(j, "BodyType") == "Star")
            {
                auto &system = systems[address];
                system.arrivalBody = count(j, "BodyID");
                if (system.stars.contains(system.arrivalBody))
                    system.star = system.stars[system.arrivalBody];
            }
            view.star = address ? systems[address].star : "";
            view.bodies = address ? systems[address].bodies : -1;
            if (event == "Location")
            {
                bool planet = text(j, "BodyType") == "Planet";
                if (planet)
                    setSurfaceBody(j);
                bool coordinates = j.contains("Latitude") && j["Latitude"].is_number() &&
                                   j.contains("Longitude") && j["Longitude"].is_number();
                if (planet && (startLanded || coordinates || (j.contains("OnFoot") && j["OnFoot"] == true)))
                {
                    setSurfaceBody(j);
                    view.landed = true;
                    view.surfaceKnown = true;
                    view.surfaceMode = j.contains("OnFoot") && j["OnFoot"] == true ? "On foot" : "Landed";
                }
                else if (!planet && !body.empty())
                    clearSurface(true);
                startLanded = false;
            }
            locationStamp = timestamp(j);
            routeView();
        }
        if (event == "FSSDiscoveryScan")
        {
            auto id = number(j, "SystemAddress");
            if (!id)
                id = address;
            if (id)
                systems[id].bodies = count(j, "BodyCount");
            if (id && id == address)
                view.bodies = systems[id].bodies;
        }
        if (event == "StartJump" && text(j, "JumpType") == "Hyperspace")
        {
            auto id = number(j, "SystemAddress");
            if (id && !text(j, "StarClass").empty())
                systems[id].star = text(j, "StarClass");
        }
        if (event == "Scan" && !text(j, "StarType").empty())
        {
            auto id = number(j, "SystemAddress");
            if (!id)
                id = address;
            if (id)
            {
                auto &system = systems[id];
                int body = count(j, "BodyID");
                if (body >= 0)
                    system.stars[body] = text(j, "StarType");
                bool zeroDistance = j.contains("DistanceFromArrivalLS") &&
                                    j["DistanceFromArrivalLS"].is_number() &&
                                    j["DistanceFromArrivalLS"].get<double>() == 0;
                if ((body >= 0 && body == system.arrivalBody) || zeroDistance ||
                    (body == 0 && system.star.empty()))
                    system.star = text(j, "StarType");
                if (id == address)
                    view.star = system.star;
            }
        }
        if (event == "NavRouteClear")
        {
            clearStamp = timestamp(j);
            route.clear();
            routeView();
        }
        if (event == "NavRoute" && j.contains("Route"))
            setRoute(j);
        if (event == "Touchdown" || event == "Liftoff")
        {
            if (j.contains("PlayerControlled") && j["PlayerControlled"] == false)
                return;
            if (!matchesSystem(j))
                return;
            setSurfaceBody(j);
            view.surfaceMode = event == "Touchdown" ? "Landed" : "Normal flight";
            view.landed = event == "Touchdown";
            view.surfaceKnown = true;
            locationStamp = timestamp(j);
        }
        if (event == "Disembark" || event == "Embark")
        {
            if (!matchesSystem(j))
                return;
            if (j.contains("OnPlanet") && j["OnPlanet"] == true)
            {
                setSurfaceBody(j);
                view.landed = true;
                view.surfaceKnown = true;
                view.surfaceMode = event == "Disembark"                    ? "On foot"
                                   : j.contains("SRV") && j["SRV"] == true ? "SRV"
                                                                           : "Landed";
                locationStamp = timestamp(j);
            }
            else if (j.contains("OnStation") && j["OnStation"] == true)
            {
                clearSurface(true);
                locationStamp = timestamp(j);
            }
        }
        if (event == "LaunchSRV" && view.landed)
        {
            view.surfaceMode = "SRV";
            locationStamp = timestamp(j);
        }
        if (event == "DockSRV" && view.landed)
        {
            view.surfaceMode = "Landed";
            locationStamp = timestamp(j);
        }
        if (event == "Docked" || event == "Undocked" || event == "Died")
        {
            clearSurface(true);
            locationStamp = timestamp(j);
        }
        if (event == "SupercruiseEntry" || event == "SupercruiseExit")
        {
            clearSurface(true);
            view.surfaceMode = event == "SupercruiseEntry" ? "Supercruise" : "Normal flight";
            locationStamp = timestamp(j);
        }
        if (event == "Docked")
            view.surfaceMode = "Docked";
        if (event == "Location" && j.contains("Docked") && j["Docked"] == true)
        {
            clearSurface(true);
            view.surfaceMode = "Docked";
        }
    }
    void status(const Json &j)
    {
        updatePosition(j);
        if (view.system.empty() || !timestamp(j) || timestamp(j) < locationStamp)
            return;
        auto flags = number(j, "Flags"), flags2 = number(j, "Flags2");
        // Main menu and shutdown emit zero flags: no location evidence is available there.
        if (!flags && !flags2)
            return;
        bool onFootPlanet = (flags2 & (uint64_t(1) << 4)) != 0;
        bool srv = (flags & (uint64_t(1) << 26)) != 0;
        bool landed = (flags & 2) != 0;
        bool ship = (flags & (uint64_t(1) << 24)) != 0;
        bool supercruise = (flags & 16) != 0;
        bool docked = (flags & 1) != 0;
        if (!onFootPlanet && !srv && !landed && !ship && !supercruise && !docked)
            return;
        auto body = text(j, "BodyName");
        if (onFootPlanet || landed || (srv && (!body.empty() || (view.landed && !view.body.empty()))))
        {
            if (!body.empty())
            {
                if (body != view.body)
                    view.bodyId = -1;
                view.body = body;
            }
            view.landed = true;
            view.surfaceKnown = true;
            view.surfaceMode = onFootPlanet ? "On foot" : srv ? "SRV" : "Landed";
        }
        else if (ship || supercruise || docked)
        {
            clearSurface(true);
            view.surfaceMode = docked ? "Docked" : supercruise ? "Supercruise" : "Normal flight";
            if (!body.empty() && !docked && !supercruise)
                view.body = body;
        }
        locationStamp = timestamp(j);
    }
};

class Reader
{
    std::atomic<int> sampleInterval{100};
    mutable std::mutex lock;
    std::map<std::pair<uint64_t, int>, Planet> knownPlanets;
    std::condition_variable wake;
    bool stopping = false;
    Snapshot published;
    std::thread worker;
    void publish(const State &s, const std::string &status)
    {
        std::lock_guard guard(lock);
        published = s.snapshotView();
        knownPlanets = s.planets;
        published.status = status;
    }
    void publishPosition(const State &s)
    {
        std::lock_guard guard(lock);
        auto status = published.status;
        published = s.snapshotView();
        published.status = std::move(status);
    }
    void run(std::filesystem::path folder, int days, std::filesystem::path sampleFile)
    {
        try
        {
            State state;
            struct Tail
            {
                uint64_t offset = 0;
                std::string pending;
            };
            std::map<std::filesystem::path, Tail> tails;
            auto cutoff = std::time(nullptr) - static_cast<std::time_t>(days) * 86400;
            std::filesystem::path latest;
            std::map<std::filesystem::path, std::time_t> starts;
            bool historyLoaded = false;
            bool sampleCacheRestored = false;
            std::string savedSampleRecord;
            auto liveSince = std::time(nullptr);
            auto startTime = [&](const std::filesystem::path &path) {
                auto found = starts.find(path);
                if (found != starts.end())
                    return found->second;
                std::ifstream in(path);
                std::string line;
                std::getline(in, line);
                auto header = Json::parse(line, nullptr, false);
                auto time = header.is_object() ? timestamp(header) : 0;
                // Empty/new files may not have their header yet; retry those on the next poll.
                if (time)
                    starts[path] = time;
                return time;
            };
            auto chronological = [&](const auto &a, const auto &b) {
                auto ta = startTime(a), tb = startTime(b);
                return ta != tb ? ta < tb : a < b;
            };
            using Clock = std::chrono::steady_clock;
            auto nextJournalRead = Clock::now();
            auto nextSettingsRead = Clock::now();
            auto readStatus = [&] {
                std::ifstream in(folder / L"Status.json");
                auto status = Json::parse(in, nullptr, false);
                if (status.is_object())
                {
                    try
                    {
                        state.status(status);
                    }
                    catch (const Json::exception &)
                    {
                    }
                }
            };
            auto waitNext = [&] {
                int samples = state.snapshotView().organicSamples;
                int interval = samples == 1 || samples == 2 ? sampleInterval.load() : 500;
                auto remaining =
                    std::chrono::duration_cast<std::chrono::milliseconds>(nextJournalRead - Clock::now())
                        .count();
                auto delay =
                    std::chrono::milliseconds(std::max<int64_t>(1, std::min<int64_t>(interval, remaining)));
                std::unique_lock guard(lock);
                return wake.wait_for(guard, delay, [&] { return stopping; });
            };
            for (;;)
            {
                {
                    std::lock_guard guard(lock);
                    if (stopping)
                        return;
                }
                if (Clock::now() >= nextSettingsRead)
                {
                    std::ifstream in(sampleFile.parent_path() / L"ExobiSetting.json");
                    auto settings = Json::parse(in, nullptr, false);
                    if (settings.is_object())
                        sampleInterval = samplePollingInterval(settings, sampleInterval.load());
                    nextSettingsRead = Clock::now() + std::chrono::seconds(1);
                }
                if (historyLoaded && Clock::now() < nextJournalRead)
                {
                    // Fast position updates avoid directory scans, journal replay and planet-map copies.
                    readStatus();
                    publishPosition(state);
                    if (waitNext())
                        return;
                    continue;
                }
                std::vector<std::filesystem::path> files;
                latest.clear();
                std::error_code ec;
                auto fileCutoff =
                    std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * days);
                for (auto &entry : std::filesystem::directory_iterator(folder, ec))
                {
                    auto name = entry.path().filename().wstring();
                    if (!entry.is_regular_file(ec) || !name.starts_with(L"Journal.") ||
                        entry.path().extension() != L".log")
                        continue;
                    if (startTime(entry.path()) && (latest.empty() || chronological(latest, entry.path())))
                        latest = entry.path();
                    if (entry.last_write_time(ec) >= fileCutoff)
                        files.push_back(entry.path());
                }
                if (!latest.empty() && std::find(files.begin(), files.end(), latest) == files.end())
                    files.push_back(latest);
                std::sort(files.begin(), files.end(), chronological);
                // Read the latest coordinates before processing new ScanOrganic records.
                if (historyLoaded)
                {
                    std::ifstream statusInput(folder / L"Status.json");
                    auto status = Json::parse(statusInput, nullptr, false);
                    if (status.is_object())
                        state.updatePosition(status);
                }
                for (const auto &file : files)
                {
                    {
                        std::lock_guard guard(lock);
                        if (stopping)
                            return;
                    }
                    auto &tail = tails[file];
                    auto size = std::filesystem::file_size(file, ec);
                    if (ec || size == tail.offset)
                        continue;
                    if (size < tail.offset)
                    {
                        tail = {};
                    }
                    std::ifstream input(file, std::ios::binary);
                    if (!input)
                        continue;
                    input.seekg(tail.offset);
                    char block[65536];
                    while (input)
                    {
                        {
                            std::lock_guard guard(lock);
                            if (stopping)
                                return;
                        }
                        input.read(block, sizeof(block));
                        auto bytes = input.gcount();
                        tail.offset += bytes;
                        tail.pending.append(block, static_cast<size_t>(bytes));
                        size_t start = 0, end;
                        while ((end = tail.pending.find('\n', start)) != std::string::npos)
                        {
                            auto j = Json::parse(tail.pending.begin() + start, tail.pending.begin() + end,
                                                 nullptr, false);
                            if (j.is_object() && (timestamp(j) >= cutoff || file == latest))
                            {
                                try
                                {
                                    state.event(j, historyLoaded && timestamp(j) >= liveSince);
                                }
                                catch (const Json::exception &)
                                {
                                }
                            }
                            start = end + 1;
                        }
                        tail.pending.erase(0, start);
                        if (tail.pending.size() > 4 * 1024 * 1024)
                            tail.pending.clear();
                    }
                }
                // Snapshot files may be temporarily incomplete during a game write. Keep the last good state.
                for (auto name : {L"NavRoute.json", L"Status.json"})
                {
                    std::ifstream in(folder / name);
                    if (!in)
                        continue;
                    auto j = Json::parse(in, nullptr, false);
                    if (!j.is_object())
                        continue;
                    try
                    {
                        if (name == std::wstring(L"NavRoute.json"))
                            state.setRoute(j);
                        else
                            state.status(j);
                    }
                    catch (const Json::exception &)
                    {
                    }
                }
                if (!sampleCacheRestored && !files.empty() && !state.view.commander.empty())
                {
                    std::ifstream in(sampleFile);
                    auto record = Json::parse(in, nullptr, false);
                    state.restoreSamples(record);
                    sampleCacheRestored = true;
                }
                if (sampleCacheRestored)
                {
                    auto record = state.sampleRecord();
                    auto serialized = record.dump(2);
                    if (serialized != savedSampleRecord)
                    {
                        if (record.is_null())
                        {
                            std::error_code removalError;
                            std::filesystem::remove(sampleFile, removalError);
                            if (!removalError)
                                savedSampleRecord = serialized;
                        }
                        else
                        {
                            auto temporary = sampleFile;
                            temporary += L".tmp";
                            std::ofstream out(temporary);
                            out << serialized << '\n';
                            out.close();
                            if (out && MoveFileExW(temporary.c_str(), sampleFile.c_str(),
                                                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                                savedSampleRecord = serialized;
                        }
                    }
                }
                historyLoaded = true;
                publish(state, files.empty()
                                   ? "Waiting for journal files"
                                   : "Journal connected | History: " + std::to_string(days) + " days");
                nextJournalRead = Clock::now() + std::chrono::milliseconds(500);
                if (waitNext())
                    return;
            }
        }
        catch (const std::exception &)
        {
            std::lock_guard guard(lock);
            published.status = "Journal read error. Check the journal folder and restart.";
        }
    }

  public:
    int statusReadInterval() const
    {
        return sampleInterval.load();
    }
    void start(const std::filesystem::path &config)
    {
        int days = 20;
        std::string folderOverride;
        {
            std::ifstream in(config);
            auto j = Json::parse(in, nullptr, false);
            if (j.is_object())
            {
                int n = count(j, "HistoryDays");
                if (n >= 1 && n <= 3650)
                    days = n;
                folderOverride = text(j, "JournalFolder");
            }
        }
        std::filesystem::path folder;
        if (!folderOverride.empty())
            folder = std::filesystem::path(std::u8string(folderOverride.begin(), folderOverride.end()));
        else
        {
            PWSTR saved = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_SavedGames, 0, nullptr, &saved)))
            {
                folder = std::filesystem::path(saved) / L"Frontier Developments" / L"Elite Dangerous";
                CoTaskMemFree(saved);
            }
        }
        auto sampleFile = config.parent_path() / L"exobi-samples.json";
        worker = std::thread([this, folder, days, sampleFile] { run(folder, days, sampleFile); });
    }
    Planet planet(uint64_t system, int body, const std::string &name) const
    {
        std::lock_guard guard(lock);
        auto found = knownPlanets.find({system, body});
        if (found != knownPlanets.end())
        {
            auto result = found->second;
            if (result.name.empty())
                result.name = name;
            return result;
        }
        for (const auto &[key, value] : knownPlanets)
            if (key.first == system && !name.empty() && value.name == name)
                return value;
        Planet unknown;
        unknown.name = name;
        return unknown;
    }
    Snapshot snapshot()
    {
        std::lock_guard guard(lock);
        return published;
    }
    ~Reader()
    {
        {
            std::lock_guard guard(lock);
            stopping = true;
        }
        wake.notify_all();
        if (worker.joinable())
            worker.join();
    }
};
} // namespace NavigationData
