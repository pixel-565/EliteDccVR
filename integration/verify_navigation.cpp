#define NOMINMAX
#include "../exobi_settings.h"
#include "../exobiology_catalog.h"
#include "../navigation_data.h"
#include <cassert>
#include <iostream>
#include <windows.h>
using namespace NavigationData;
Json parse(const char *s)
{
    return Json::parse(s);
}
void event(State &s, const char *j)
{
    s.event(parse(j));
}
int main(int argc, char **argv)
{
    {
        Exobiology::Settings appearance;
        assert(appearance.incompleteColor == 0xb9dcf1 && appearance.completedColor == 0x83c78c &&
               appearance.rowGap == 2);
        assert(appearance.distanceBelowMinimumColor == 0x65bfff && appearance.distanceReadyColor == 0x80da8c);
        assert(appearance.load("ExobiSetting.json"));
        auto configPath = std::filesystem::temp_directory_path() /
                          ("vrkeyboard-exobi-colors-" + std::to_string(GetCurrentProcessId()) + ".json");
        {
            std::ofstream out(configPath);
            out << R"({"IncompleteColor":"#000000","CompletedColor":"#12aBcD","RowGapPx":4,"DistanceBelowMinimumColor":"#234567","DistanceReadyColor":"#ABCDEF"})";
        }
        assert(appearance.load(configPath));
        assert(appearance.distanceBelowMinimumColor == 0x234567 && appearance.distanceReadyColor == 0xabcdef);
        assert(appearance.incompleteColor == 0 && appearance.completedColor == 0x12abcd &&
               appearance.rowGap == 4);
        {
            std::ofstream out(configPath);
            out << R"({"IncompleteColor":"bad","CompletedColor":"#GGGGGG","RowGapPx":99})";
        }
        assert(appearance.load(configPath));
        assert(appearance.incompleteColor == 0 && appearance.completedColor == 0x12abcd &&
               appearance.rowGap == 4);
        {
            std::ofstream out(configPath);
            out << "{";
        }
        assert(!appearance.load(configPath) && appearance.rowGap == 4);
        assert(Exobiology::Settings::rowText(0xffffff) == 0x132530 &&
               Exobiology::Settings::rowText(0) == 0xeaf1ff);
        std::filesystem::remove(configPath);
        std::cout << "PASS exobiology appearance: defaults, reload, hex colors, row gap, contrast and "
                     "malformed data\n";
    }
    assert(samplePollingInterval(Json{{"StatusReadIntervalMs", 100}}) == 100);
    assert(samplePollingInterval(Json{{"StatusReadIntervalMs", 20}}) == 20);
    assert(samplePollingInterval(Json{{"StatusReadIntervalMs", 2000}}) == 2000);
    assert(samplePollingInterval(Json{{"StatusReadIntervalMs", 0}}, 125) == 125);
    assert(samplePollingInterval(Json{{"StatusReadIntervalMs", "bad"}}, 125) == 125);
    Exobiology::Catalog catalog;
    assert(catalog.load("exobiology-catalog.json"));
    auto reference = catalog.lookup("$Codex_Ent_Tussocks_09_A_Name;", "Tussock Propagito - Yellow");
    assert(reference.price == 1000000 && reference.distance == 200);
    reference = catalog.lookup("$Codex_Ent_Tubus_05_A_Name;", "Tubus Compagibus - Grey");
    assert(reference.price == 7774700 && reference.distance == 800);
    reference = catalog.lookup("$Codex_Ent_Bacterial_Genus_Name;", "Bacterium");
    assert(reference.price == -1 && reference.distance == 500);
    reference = catalog.lookup("", "Stratum Tectonicas - Green");
    assert(reference.price == 19010800 && reference.distance == 500);
    reference = catalog.lookup("$Codex_Ent_Stratum_07_A_Name;", "Localized organism");
    assert(reference.price == 19010800 && reference.distance == 500);
    reference = catalog.lookup("$Codex_Ent_Stratum_80_A_Name;", "Unknown");
    assert(reference.price == -1 && reference.distance == 500);
    reference = catalog.lookup("", "Mystery");
    assert(reference.price == -1 && reference.distance == -1);
    assert(Exobiology::priceText(19010800) == L"19,010,800 Cr");
    assert(!catalog.load("missing-exobiology-catalog.json"));
    assert(catalog.lookup("", "Stratum Tectonicas").price == -1);
    auto badCatalog = std::filesystem::temp_directory_path() / "vrkeyboard-invalid-catalog.json";
    {
        std::ofstream out(badCatalog);
        out << R"({"schema":1,"genuses":[{"symbol":"X","name":"X","distance_m":-1}],"species":[]})";
    }
    assert(!catalog.load(badCatalog));
    std::filesystem::remove(badCatalog);
    assert(catalog.load("exobiology-catalog.json"));
    std::cout << "PASS offline exobiology catalog: species, variants, genus-only, unknown and invalid data\n";
    SurfacePosition origin{0, 0, true}, east{0, 1, true};
    assert(std::abs(surfaceDistance(origin, east, 1000) - std::numbers::pi * 1000 / 180) < 1.e-9);
    assert(std::abs(surfaceDistance({0, 179, true}, {0, -179, true}, 1000) - std::numbers::pi * 1000 / 90) <
           1.e-9);
    assert(surfaceDistance({}, east, 1000) == -1 && surfaceDistance(origin, east, -1) == -1);
    State tracking;
    tracking.view.systemAddress = 99;
    tracking.view.bodyId = 19;
    tracking.view.body = "Test planet";
    tracking.planets[{99, 19}].radius = 1000;
    auto position = [&](double lat, double lon) {
        tracking.updatePosition(Json{{"timestamp", "2026-10-03T10:00:00Z"},
                                     {"BodyName", "Test planet"},
                                     {"Flags", uint64_t(1) << 21},
                                     {"Latitude", lat},
                                     {"Longitude", lon},
                                     {"PlanetRadius", 2000}});
    };
    auto sample = [&](const char *kind, const char *stamp, bool live = true) {
        tracking.event(Json{{"event", "ScanOrganic"},
                            {"SystemAddress", uint64_t(99)},
                            {"Body", 19},
                            {"Variant", "$Codex_Ent_Tussocks_09_A_Name;"},
                            {"Variant_Localised", "Tussock Propagito - Yellow"},
                            {"ScanType", kind},
                            {"timestamp", stamp}},
                       live);
    };
    position(0, 0);
    sample("Log", "2026-10-03T10:00:01Z", false);
    assert(!tracking.samplePositions[0].valid && tracking.snapshotView().organicSamples == 1);
    position(0, 1);
    sample("Sample", "2026-10-03T10:00:02Z");
    auto sampleView = tracking.snapshotView();
    assert(sampleView.organicSamples == 2 && sampleView.sampleDistances[0] == -1 &&
           sampleView.sampleDistances[1] == 0);
    position(0, 2);
    sampleView = tracking.snapshotView();
    assert(std::abs(sampleView.sampleDistances[1] - std::numbers::pi * 1000 / 180) < 1.e-9);
    sample("Sample", "2026-10-03T10:00:03Z");
    assert(tracking.snapshotView().organicSamples == 0 && !tracking.samplePositions[1].valid);
    tracking.planets[{99, 19}].bioSamples["tussock"] = 0;
    position(0, 0);
    sample("Log", "2026-10-03T10:00:04Z");
    position(0, 1);
    sample("Sample", "2026-10-03T10:00:05Z");
    position(0, 2);
    sampleView = tracking.snapshotView();
    assert(std::abs(sampleView.sampleDistances[0] - std::numbers::pi * 1000 / 90) < 1.e-9);
    assert(std::abs(sampleView.sampleDistances[1] - std::numbers::pi * 1000 / 180) < 1.e-9);
    tracking.updatePosition(Json{{"timestamp", "2026-10-03T10:00:06Z"}, {"Flags", uint64_t(0)}});
    assert(tracking.snapshotView().sampleDistances[0] == -1);
    position(0, 2);
    tracking.positionBody = "Different planet";
    assert(tracking.snapshotView().sampleDistances[0] == -1);
    position(91, 0);
    assert(!tracking.currentPosition.valid);
    std::cout << "PASS live sample positions: surface distance, date line, history exclusion, second-only "
                 "capture, completion and body isolation\n";
    tracking.view.commander = "Persistence Test";
    tracking.fid = "FID-test";
    auto savedSamples = tracking.sampleRecord();
    assert(savedSamples.is_object());
    State resumed = tracking;
    resumed.samplePositions = {};
    assert(resumed.restoreSamples(savedSamples) && resumed.samplePositions[0].valid &&
           resumed.samplePositions[1].valid);
    auto mismatched = savedSamples;
    mismatched["FID"] = "different";
    assert(!resumed.restoreSamples(mismatched));
    mismatched = savedSamples;
    mismatched["BodyID"] = 20;
    assert(!resumed.restoreSamples(mismatched));
    mismatched = savedSamples;
    mismatched["SamplingSet"] = "another scan";
    assert(!resumed.restoreSamples(mismatched));
    mismatched = savedSamples;
    mismatched["Positions"][1]["Latitude"] = 91;
    assert(!resumed.restoreSamples(mismatched));
    assert(!resumed.restoreSamples(Json::parse("{", nullptr, false)));
    resumed.planets[{99, 19}].bioSamples["tussock"] = 3;
    assert(!resumed.restoreSamples(savedSamples) && resumed.sampleRecord().is_null());
    std::cout << "PASS sample persistence: restore, commander/body/set isolation, invalid coordinates and "
                 "completed set rejection\n";
    State organic;
    auto scan = [&](const char *type, const char *stamp, const char *name = "Tussock Yellow") {
        organic.event(Json{{"event", "ScanOrganic"},
                           {"SystemAddress", uint64_t(99)},
                           {"Body", 19},
                           {"Variant", "$Codex_Ent_Tussock_Test;"},
                           {"Variant_Localised", name},
                           {"ScanType", type},
                           {"timestamp", stamp}});
    };
    auto &organicPlanet = organic.planets[{99, 19}];
    scan("Log", "a");
    assert(organicPlanet.bioSamples["tussock"] == 1);
    scan("Sample", "b");
    assert(organicPlanet.bioSamples["tussock"] == 2);
    scan("Sample", "b");
    assert(organicPlanet.bioSamples["tussock"] == 2);
    scan("Sample", "c");
    assert(organicPlanet.bioSamples["tussock"] == 3);
    scan("Analyse", "d");
    assert(organicPlanet.bioSamples["tussock"] == 3);
    scan("Log", "e", "Bacterium Blue");
    scan("Log", "f", "Tubus Red");
    assert(organicPlanet.bioSamples["bacterium"] == 0 && organicPlanet.bioSamples["tubus"] == 1);
    assert(organicPlanet.bioSamples["tussock"] == 3);
    scan("Sample", "g", "Stratum Green");
    assert(organicPlanet.bioSamples["stratum"] == -1);
    scan("Analyse", "h", "Stratum Green");
    assert(organicPlanet.bioSamples["stratum"] == 3);
    State s;
    event(s, R"({"event":"LoadGame","FID":"one","Commander":"Test"})");
    event(
        s,
        R"({"event":"Location","timestamp":"2026-10-01T10:00:00Z","SystemAddress":100,"StarSystem":"Alpha","BodyType":"Star","BodyID":3})");
    event(s, R"({"event":"Scan","SystemAddress":100,"BodyID":3,"StarType":"K"})");
    event(s, R"({"event":"Scan","SystemAddress":100,"BodyID":9,"StarType":"M","DistanceFromArrivalLS":900})");
    assert(s.view.star == "K" && scoopable(s.view.star) && !scoopable("DA") && !scoopable("N"));
    assert(s.view.bodies == -1);
    event(s, R"({"event":"FSSDiscoveryScan","SystemAddress":100,"BodyCount":18})");
    assert(s.view.bodies == 18);
    auto route = parse(
        R"({"timestamp":"2026-10-01T10:01:00Z","Route":[{"SystemAddress":100,"StarSystem":"Alpha","StarClass":"K"},{"SystemAddress":200,"StarSystem":"Beta","StarClass":"N"},{"SystemAddress":300,"StarSystem":"Gamma","StarClass":"M"}]})");
    s.setRoute(route);
    assert(s.view.destinationStar == "M" && s.view.following == "Gamma" && s.view.followingStar == "M");
    assert(s.view.next == "Beta" && s.view.nextStar == "N" && s.view.destination == "Gamma" &&
           s.view.jumps == 2);
    event(
        s,
        R"({"event":"Touchdown","timestamp":"2026-10-01T10:02:00Z","Body":"Alpha 1","PlayerControlled":true})");
    assert(s.view.landed && s.view.body == "Alpha 1");
    s.status(parse(R"({"timestamp":"2026-10-01T10:01:00Z","Flags":0})"));
    assert(s.view.landed);
    event(
        s,
        R"({"event":"Liftoff","timestamp":"2026-10-01T10:03:00Z","Body":"Alpha 1","PlayerControlled":true})");
    assert(!s.view.landed);
    event(
        s,
        R"({"event":"FSDJump","timestamp":"2026-10-01T10:04:00Z","SystemAddress":200,"StarSystem":"Beta"})");
    assert(s.view.jumps == 1 && s.view.star == "N" && s.view.bodies == -1 && s.view.body.empty());
    event(
        s,
        R"({"event":"FSDJump","timestamp":"2026-10-01T10:05:00Z","SystemAddress":300,"StarSystem":"Gamma"})");
    assert(s.view.jumps == 0 && s.view.next.empty());
    event(s, R"({"event":"NavRouteClear","timestamp":"2026-10-01T10:06:00Z"})");
    s.setRoute(route);
    assert(s.view.jumps == -1 && s.route.empty() && s.view.following.empty() &&
           s.view.destinationStar.empty());
    event(s, R"({"event":"LoadGame","FID":"two","Commander":"Other"})");
    assert(s.view.system.empty() && s.systems.empty());
    State surface;
    event(surface, R"({"event":"LoadGame","FID":"surface-test"})");
    event(
        surface,
        R"({"event":"Location","SystemAddress":500,"StarSystem":"Surface system","Body":"Surface system 1","BodyID":8,"BodyType":"Planet"})");
    event(
        surface,
        R"({"event":"Touchdown","SystemAddress":500,"Body":"Surface system 1","BodyID":8,"PlayerControlled":true})");
    event(surface, R"({"event":"Shutdown"})");
    event(surface, R"({"event":"Fileheader"})");
    event(surface, R"({"event":"LoadGame","FID":"surface-test"})");
    event(
        surface,
        R"({"event":"Location","timestamp":"2026-10-02T10:00:00Z","SystemAddress":500,"StarSystem":"Surface system","Body":"Surface system 1","BodyID":8,"BodyType":"Planet"})");
    surface.status(parse(R"({"timestamp":"2026-10-02T10:01:00Z","Flags":0})"));
    assert(surface.view.landed && surface.view.bodyId == 8 && surface.view.body == "Surface system 1");
    event(
        surface,
        R"({"event":"Disembark","SystemAddress":500,"Body":"Surface system 1","BodyID":8,"OnPlanet":true})");
    assert(surface.view.surfaceMode == "On foot");
    event(surface,
          R"({"event":"Liftoff","SystemAddress":500,"Body":"Surface system 1","PlayerControlled":false})");
    assert(surface.view.landed);
    surface.status(
        parse(R"({"timestamp":"2026-10-02T10:02:00Z","Flags":0,"Flags2":16,"BodyName":"Surface system 1"})"));
    assert(surface.view.landed && surface.view.surfaceMode == "On foot");
    surface.status(
        parse(R"({"timestamp":"2026-10-02T10:03:00Z","Flags":67108864,"BodyName":"Surface system 1"})"));
    assert(surface.view.landed && surface.view.surfaceMode == "SRV");
    surface.status(
        parse(R"({"timestamp":"2026-10-02T10:04:00Z","Flags":16777216,"BodyName":"Surface system 1"})"));
    assert(!surface.view.landed);
    event(surface, R"({"event":"LoadGame","FID":"surface-test","StartLanded":true})");
    event(
        surface,
        R"({"event":"Location","SystemAddress":600,"StarSystem":"New system","BodyType":"Planet","Body":"New system 2","BodyID":2})");
    assert(surface.view.landed && surface.view.body == "New system 2");
    event(
        surface,
        R"({"event":"Location","SystemAddress":600,"StarSystem":"New system","BodyType":"Planet","Body":"New system 3","BodyID":3})");
    assert(!surface.view.landed && !surface.view.surfaceKnown);
    event(
        surface,
        R"({"event":"Location","SystemAddress":600,"StarSystem":"New system","BodyType":"Planet","Body":"New system 3","BodyID":3,"Latitude":12,"Longitude":24})");
    assert(surface.view.landed);
    event(surface, R"({"event":"SupercruiseEntry"})");
    assert(!surface.view.landed && surface.view.body.empty() && surface.view.surfaceMode == "Supercruise");
    event(surface, R"({"event":"SupercruiseExit"})");
    assert(surface.view.surfaceMode == "Normal flight");
    event(surface, R"({"event":"Docked"})");
    assert(surface.view.surfaceMode == "Docked");
    event(surface, R"({"event":"Undocked"})");
    assert(surface.view.surfaceMode == "Normal flight");
    std::cout << "PASS contextual surface recovery across restart, menu status, on-foot/SRV, ship dismissal "
                 "and real departure\n";
    State details;
    event(
        details,
        R"({"event":"Scan","SystemAddress":100,"BodyID":2,"BodyName":"Alpha 2","PlanetClass":"Rocky body","SurfaceTemperature":250,"SurfaceGravity":9.80665,"Radius":1200000,"Volcanism":"Water geysers"})");
    event(
        details,
        R"({"event":"SAASignalsFound","SystemAddress":100,"BodyID":2,"Signals":[{"Type":"$SAA_SignalType_Biological;","Count":3},{"Type":"$SAA_SignalType_Geological;","Count":2}],"Genuses":[{"Genus":"bacterium","Genus_Localised":"Bacterium"}]})");
    event(
        details,
        R"({"event":"ScanOrganic","SystemAddress":100,"Body":2,"Variant":"variant1","Variant_Localised":"Bacterium test"})");
    event(
        details,
        R"({"event":"ScanOrganic","SystemAddress":100,"Body":2,"Variant":"variant1","Variant_Localised":"Bacterium test"})");
    event(
        details,
        R"({"event":"CodexEntry","SystemAddress":100,"BodyID":2,"SubCategory":"$Codex_SubCategory_Geology_And_Anomalies;","Name":"geyser","Name_Localised":"Water geyser"})");
    event(
        details,
        R"({"event":"SAASignalsFound","SystemAddress":100,"BodyID":2,"Genuses":[{"Genus":"bacterium","Genus_Localised":"Bacterium"}]})");
    const auto &planet = details.planets.at({100, 2});
    assert(planet.radius == 1200000 && planet.temperature == 250 && planet.hasBio() && planet.hasGeo());
    assert(planet.bioCount == 3 && planet.geoCount == 2 && planet.bio.size() == 1 && planet.geo.size() == 1);
    event(
        details,
        R"({"event":"Scan","SystemAddress":200,"BodyID":2,"BodyName":"Beta 2","PlanetClass":"Icy body","Radius":500000})");
    assert(details.planets.at({200, 2}).radius == 500000 && !details.planets.at({200, 2}).hasBio());
    assert(details.planets.at({100, 2}).radius == 1200000);
    std::cout
        << "PASS per-system planet data, units, signal/genus/organic/codex merge and duplicate suppression\n";
    Planet progressive;
    progressive.mergeBio("$Codex_Ent_Tussocks_Genus_Name;", "Tussock", 1);
    progressive.mergeBio("$Codex_Ent_Tussocks_09_Name;", "Tussock Propagito", 2);
    progressive.mergeBio("$Codex_Ent_Tussocks_09_F_Name;", "Tussock Propagito - Yellow", 3);
    progressive.mergeBio("$Codex_Ent_Tussocks_Genus_Name;", "Tussock", 1);
    progressive.mergeBio("$Codex_Ent_Tussocks_09_F_Name;", "Tussock Propagito - Yellow", 3);
    progressive.mergeBio("$Codex_Ent_Tubus_Genus_Name;", "Tubus", 1);
    assert(progressive.bio.size() == 2 && progressive.bio.at("tussock") == "Tussock Propagito - Yellow");
    std::cout << "PASS genus/species/variant upgrade, repeated discoveries and no downgrade from later genus "
                 "list\n";
    State distances;
    event(distances, R"({"event":"Location","StarSystem":"A","SystemAddress":1})");
    distances.setRoute(parse(
        R"({"timestamp":"2026-10-02T00:00:00Z","Route":[{"StarSystem":"A","SystemAddress":1,"StarPos":[0,0,0]},{"StarSystem":"B","SystemAddress":2,"StarPos":[3,4,0]},{"StarSystem":"C","SystemAddress":3,"StarPos":[3,4,12]}]})"));
    assert(distances.view.nextDistance == 5 && distances.view.followingDistance == 12 &&
           distances.view.remainingDistance == 17);
    event(distances, R"({"event":"FSDJump","StarSystem":"B","SystemAddress":2})");
    assert(distances.view.nextDistance == 12 && distances.view.remainingDistance == 12);
    event(distances, R"({"event":"FSDJump","StarSystem":"C","SystemAddress":3})");
    assert(distances.view.remainingDistance == 0 && distances.view.nextDistance == -1);
    distances.setRoute(parse(
        R"({"timestamp":"2026-10-02T00:01:00Z","Route":[{"StarSystem":"C","SystemAddress":3,"StarPos":[3,4,12]},{"StarSystem":"D","SystemAddress":4}]})"));
    assert(distances.view.remainingDistance == -1 && distances.view.nextDistance == -1);
    std::cout << "PASS 3D jump distance, route sum, progression, arrival and missing coordinates\n";
    std::cout << "PASS route progression, stale route rejection, star selection, scoop classes, unknown "
                 "bodies, landing and commander isolation\n";

    auto folder = std::filesystem::temp_directory_path() /
                  (L"VRKeyboard-navigation-test-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(folder);
    auto config = folder / "config.json";
    {
        std::ofstream out(config);
        out << Json{{"HistoryDays", 7}, {"JournalFolder", folder.string()}};
    }
    auto old = folder / "Journal.2000-01-01T000000.01.log";
    {
        std::ofstream out(old);
        out << R"({"event":"Location","timestamp":"2000-01-01T00:00:00Z","StarSystem":"Excluded","SystemAddress":1})"
            << '\n';
    }
    std::filesystem::last_write_time(old, std::filesystem::file_time_type::clock::now() -
                                              std::chrono::hours(24 * 20));
    auto legacy = folder / "Journal.250513184820.01.log";
    {
        std::ofstream out(legacy);
        out << R"({"event":"Fileheader","timestamp":"1999-05-13T16:48:20Z"})" << '\n'
            << R"({"event":"Location","timestamp":"1999-05-13T16:49:00Z","StarSystem":"Wrong legacy location","SystemAddress":999})"
            << '\n';
    }
    std::filesystem::last_write_time(legacy, std::filesystem::file_time_type::clock::now() -
                                                 std::chrono::hours(24 * 400));
    auto active = folder / "Journal.2000-01-02T000000.01.log";
    {
        std::ofstream out(active);
        out << R"({"event":"Location","timestamp":"2000-01-02T00:00:00Z","StarSystem":"Long-running session","SystemAddress":2})"
            << '\n';
    }
    std::filesystem::last_write_time(active, std::filesystem::file_time_type::clock::now() -
                                                 std::chrono::hours(24 * 19));
    {
        Reader reader;
        reader.start(config);
        auto waitFor = [&](auto predicate) {
            for (int i = 0; i < 60; ++i)
            {
                if (predicate(reader.snapshot()))
                    return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            return false;
        };
        assert(waitFor([](auto v) { return v.system == "Long-running session"; }));
        {
            std::ofstream out(active, std::ios::app);
            out << R"({"event":"FSSDiscoveryScan","SystemAddress":2,"BodyCount":)";
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        assert(reader.snapshot().bodies == -1);
        {
            std::ofstream out(active, std::ios::app);
            out << "12}\n";
        }
        assert(waitFor([](auto v) { return v.bodies == 12; }));
        {
            std::ofstream out(active, std::ios::app);
            out << "malformed line\n"
                << R"({"event":"Touchdown","Body":"Long-running session 1","PlayerControlled":true})" << '\n';
        }
        assert(waitFor([](auto v) { return v.landed; }));
    }
    auto priorSession = folder / "Journal.SurfaceHistory.01.log";
    auto newSession = folder / "Journal.SurfaceRestart.01.log";
    auto iso = [](std::time_t stamp) {
        std::tm utc{};
        gmtime_s(&utc, &stamp);
        std::ostringstream out;
        out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
        return out.str();
    };
    auto now = std::time(nullptr);
    {
        std::ofstream out(priorSession);
        out << Json{{"event", "Fileheader"}, {"timestamp", iso(now - 9 * 86400)}} << '\n';
        out << Json{{"event", "Location"},
                    {"timestamp", iso(now - 9 * 86400 + 1)},
                    {"StarSystem", "Recovered planet"},
                    {"SystemAddress", uint64_t(700)},
                    {"Body", "Recovered planet 1"},
                    {"BodyID", 4},
                    {"BodyType", "Planet"}}
            << '\n';
        out << Json{{"event", "Touchdown"},
                    {"timestamp", iso(now - 9 * 86400 + 2)},
                    {"Body", "Recovered planet 1"},
                    {"BodyID", 4},
                    {"PlayerControlled", true}}
            << '\n';
    }
    std::filesystem::last_write_time(priorSession, std::filesystem::file_time_type::clock::now() -
                                                       std::chrono::hours(24 * 9));
    {
        std::ofstream out(newSession);
        out << Json{{"event", "Fileheader"}, {"timestamp", iso(now - 100)}} << '\n';
        out << Json{{"event", "Location"},
                    {"timestamp", iso(now - 99)},
                    {"StarSystem", "Recovered planet"},
                    {"SystemAddress", uint64_t(700)},
                    {"Body", "Recovered planet 1"},
                    {"BodyID", 4},
                    {"BodyType", "Planet"}}
            << '\n';
        std::ofstream settings(config);
        settings << Json{{"JournalFolder", folder.string()}};
    }
    {
        Reader reader;
        reader.start(config);
        bool recovered = false;
        for (int i = 0; i < 60; ++i)
        {
            auto v = reader.snapshot();
            if (v.system == "Recovered planet" && v.landed && v.bodyId == 4 &&
                v.status.find("20 days") != std::string::npos)
            {
                recovered = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        assert(recovered);
    }
    std::filesystem::remove(priorSession);
    std::filesystem::remove(newSession);
    std::cout << "PASS default 20-day history restores landing from previous session 9 days earlier\n";
    std::filesystem::remove(legacy);
    std::filesystem::remove(old);
    std::filesystem::remove(active);
    std::filesystem::remove(config);
    std::filesystem::remove(folder);
    std::cout << "PASS mixed legacy/modern journal chronology, background reading, long-running session, "
                 "partial append, malformed line recovery and "
                 "shutdown\n";
    {
        auto sampleFolder = std::filesystem::temp_directory_path() /
                            ("vrkeyboard-live-samples-" + std::to_string(GetCurrentProcessId()));
        std::filesystem::create_directories(sampleFolder);
        auto sampleConfig = sampleFolder / "config.json";
        auto sampleJournal = sampleFolder / "Journal.live.log";
        auto sampleStatus = sampleFolder / "Status.json";
        auto sampleTestTime = std::time(nullptr) + 5;
        std::tm utc{};
        gmtime_s(&utc, &sampleTestTime);
        std::ostringstream stamp;
        stamp << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
        auto timestampText = stamp.str();
        {
            std::ofstream out(sampleConfig);
            out << Json{{"JournalFolder", sampleFolder.string()}};
        }
        auto writePosition = [&](double longitude) {
            std::ofstream out(sampleStatus);
            out << Json{{"timestamp", timestampText}, {"Flags", (uint64_t(1) << 21) | 2},
                        {"BodyName", "Live planet"},  {"Latitude", 0},
                        {"Longitude", longitude},     {"PlanetRadius", 1000}};
        };
        Json scanRecord{{"event", "ScanOrganic"},
                        {"timestamp", timestampText},
                        {"SystemAddress", uint64_t(99)},
                        {"Body", 19},
                        {"Variant", "$Codex_Ent_Tussocks_09_A_Name;"},
                        {"Variant_Localised", "Tussock Propagito - Yellow"},
                        {"ScanType", "Log"}};
        {
            std::ofstream out(sampleJournal);
            out << Json{{"event", "Fileheader"}, {"timestamp", timestampText}} << '\n';
            out << Json{{"event", "Commander"}, {"FID", "reader-test"}, {"Name", "Reader Test"}} << '\n';
            out << Json{{"event", "Location"},
                        {"timestamp", timestampText},
                        {"SystemAddress", uint64_t(99)},
                        {"StarSystem", "Live system"},
                        {"BodyID", 19},
                        {"BodyType", "Planet"},
                        {"Body", "Live planet"},
                        {"Latitude", 0},
                        {"Longitude", 0}}
                << '\n';
            out << scanRecord << '\n';
        }
        writePosition(0);
        {
            auto reader = std::make_unique<Reader>();
            reader->start(sampleConfig);
            auto awaitSample = [&](auto predicate) {
                for (int i = 0; i < 100; ++i)
                {
                    if (predicate(reader->snapshot()))
                        return true;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
                return false;
            };
            assert(awaitSample([](auto v) { return v.organicSamples == 1; }));
            assert(reader->snapshot().sampleDistances[0] == -1);
            writePosition(1);
            scanRecord["ScanType"] = "Sample";
            {
                std::ofstream out(sampleJournal, std::ios::app);
                out << scanRecord << '\n';
            }
            assert(awaitSample([](auto v) { return v.organicSamples == 2 && v.sampleDistances[1] == 0; }));
            auto cacheFile = sampleFolder / "exobi-samples.json";
            assert(std::filesystem::exists(cacheFile));
            reader.reset();
            reader = std::make_unique<Reader>();
            reader->start(sampleConfig);
            assert(awaitSample([](auto v) { return v.organicSamples == 2 && v.sampleDistances[1] == 0; }));
            auto movementStart = std::chrono::steady_clock::now();
            writePosition(2);
            assert(
                awaitSample([](auto v) { return v.sampleDistances[1] > 17 && v.sampleDistances[1] < 18; }));
            auto movementMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::steady_clock::now() - movementStart)
                                  .count();
            assert(movementMs < 400);
            std::cout << "PASS fast Status distance update: " << movementMs << " ms\n";
            auto pollingConfig = sampleFolder / "ExobiSetting.json";
            {
                std::ofstream out(pollingConfig);
                out << Json{{"StatusReadIntervalMs", 75}};
            }
            assert(awaitSample([&](auto) { return reader->statusReadInterval() == 75; }));
            {
                std::ofstream out(pollingConfig);
                out << Json{{"StatusReadIntervalMs", 0}};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1100));
            assert(reader->statusReadInterval() == 75);
            std::filesystem::remove(pollingConfig);
            scanRecord["ScanType"] = "Analyse";
            {
                std::ofstream out(sampleJournal, std::ios::app);
                out << scanRecord << '\n';
            }
            assert(awaitSample([](auto v) { return v.organicSamples == 0; }));
            assert(!std::filesystem::exists(cacheFile));
        }
        for (const auto &file : {sampleConfig, sampleJournal, sampleStatus})
            std::filesystem::remove(file);
        std::filesystem::remove(sampleFolder);
        std::cout << "PASS background reader: current Status coordinates captured on new sample, movement "
                     "updates, restart restoration and completion file cleanup\n";
    }
    if (argc > 1)
    {
        Reader reader;
        reader.start(std::filesystem::path(argv[1]));
        auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < 200; ++i)
        {
            auto v = reader.snapshot();
            if (v.status != "Loading journal history...")
            {
                std::cout << "Live history: " << v.status << "; system=" << v.system << "; star=" << v.star
                          << "; bodies=" << v.bodies << "; jumps=" << v.jumps << "; landed=" << v.landed
                          << "; body=" << v.body << '\n';
                auto currentPlanet = reader.planet(v.systemAddress, v.bodyId, v.body);
                for (const auto &[key, name] : currentPlanet.bio)
                {
                    auto found = currentPlanet.bioSamples.find(key);
                    std::cout << "Exobio: " << name
                              << "; samples=" << (found == currentPlanet.bioSamples.end() ? 0 : found->second)
                              << "/3";
                    auto symbol = currentPlanet.bioSymbols.find(key);
                    auto ref =
                        catalog.lookup(symbol == currentPlanet.bioSymbols.end() ? "" : symbol->second, name);
                    std::cout << "; price=" << ref.price << "; distance=" << ref.distance << " m\n";
                }
                std::cout << "Initial read ms: "
                          << std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - start)
                                 .count()
                          << '\n';
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
}
