#include "Benchmark.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <tuple>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_set>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

#include "algorithms/InfrastructureOptimizer.h"
#include "algorithms/InventoryReservations.h"
#include "algorithms/LogisticsPlanner.h"
#include "models/Colony.h"
#include "models/Station.h"
#include "simulation/SimulationEngine.h"

namespace {
using Clock = std::chrono::steady_clock;

struct Size {
    int planets;
    int routes;
    int missions;
};

Size dimensions(const std::string& name) {
    if (name == "small") return {100, 500, 50};
    if (name == "medium") return {1000, 10000, 500};
    if (name == "large") return {10000, 100000, 5000};
    if (name == "extreme") return {100000, 1000000, 50000};
    throw std::invalid_argument("Expected small, medium, large, or extreme.");
}

struct Scenario {
    GalaxyGraph graph;
    std::vector<Planet> planets;
    std::vector<Colony> colonies;
    std::vector<Station> stations;
    std::vector<ResourceDemand> inventory;
    std::vector<ResourceDemand> aidDemands;
    std::vector<Spacecraft> ships;
    std::vector<Mission> missions;
    std::vector<Infrastructure> projects;
    std::vector<Event> events;
    long long population = 0;
};

Scenario generate(Size size, unsigned seed) {
    std::mt19937 random(seed);
    auto draw = [&](unsigned limit) { return static_cast<int>(random() % limit); };
    Scenario data;
    data.planets.reserve(size.planets);
    data.inventory.reserve(size.planets * 2);
    for (int i = 0; i < size.planets; ++i) {
        Planet planet{};
        planet.id = i;
        planet.population = 1000 + draw(100000);
        planet.x = i % 1000;
        planet.y = i / 1000;
        planet.technologyLevel = 1 + draw(8);
        planet.defenseLevel = draw(10);
        planet.strategicImportance = 1 + draw(10);
        data.population += planet.population;
        const bool receiver = i % 25 == 0;
        const bool donor = i % 25 == 1;
        ResourceDemand food{i, receiver ? 12.0 : donor ? 220.0 : 80.0,
            45.0, static_cast<int>(planet.strategicImportance), ResourceType::Food,
            receiver ? 0.5 : 2.0, receiver ? 2.0 : 1.5, 250.0};
        ResourceDemand medicine{i, 20.0 + draw(50), 15.0, food.priority,
            ResourceType::Medicine, 0.3, 0.4, 100.0};
        for (const auto& resource : {food, medicine}) {
            planet.resources.emplace(resource.type, Resource{resource.type, resource.available,
                resource.production, resource.consumption, resource.storage, 1.0});
            data.inventory.push_back(resource);
        }
        if (receiver || donor)
            data.aidDemands.push_back(food);
        data.planets.push_back(std::move(planet));
        data.graph.addLocation(i);
    }

    const int colonyCount = std::max(1, size.planets / 100);
    const int stationCount = std::max(1, size.planets / 200);
    for (int i = 0; i < colonyCount; ++i) {
        Colony colony;
        static_cast<Planet&>(colony) = data.planets[i];
        colony.id = size.planets + i;
        colony.parentPlanetId = i;
        colony.isEstablished = true;
        colony.population /= 20;
        data.population += colony.population;
        data.inventory.push_back({colony.id, 30, 35, 6, ResourceType::Food, 0.5, 1.0, 100});
        colony.resources[ResourceType::Food] = {ResourceType::Food, 30, 0.5, 1.0, 100, 1};
        data.aidDemands.push_back(data.inventory.back());
        data.graph.addLocation(colony.id);
        data.colonies.push_back(std::move(colony));
    }
    for (int i = 0; i < stationCount; ++i) {
        Station station;
        station.id = size.planets + colonyCount + i;
        station.storageCapacity = 100;
        station.resources[ResourceType::Medicine] = 30;
        data.inventory.push_back({station.id, 30, 10, 4, ResourceType::Medicine, 0, 0, 100});
        data.graph.addLocation(station.id);
        data.stations.push_back(std::move(station));
    }

    auto connect = [&](int id, int from, int to, bool wormhole) {
        Route route{};
        route.id = id;
        route.from = from;
        route.to = to;
        route.distance = 5.0 + draw(wormhole ? 70 : 30);
        route.fuelConsumption = route.distance * (0.6 + draw(90) / 100.0);
        // Seeded edge durations form one deterministic scenario realization.
        route.travelTime = route.distance * (0.06 + draw(12) / 100.0);
        route.risk = (1 + draw(wormhole ? 30 : 12)) / 100.0;
        route.reliability = 1 - route.risk / 2;
        route.maxCapacity = 12 + draw(75);
        route.isAvailable = true;
        route.isWormhole = wormhole;
        data.graph.addRoute(route);
    };
    int edge = 0;
    for (int i = 0; i < size.planets; ++i)
        connect(edge++, i, (i + 1) % size.planets, false);
    for (const auto& colony : data.colonies) {
        connect(edge++, colony.parentPlanetId, colony.id, false);
        connect(edge++, colony.id, colony.parentPlanetId, false);
    }
    for (int i = 0; i < stationCount; ++i) {
        connect(edge++, i, data.stations[i].id, false);
        connect(edge++, data.stations[i].id, i, false);
    }
    while (edge < size.routes) {
        const int from = edge % size.planets;
        const bool wormhole = draw(100) < 5;
        const int offset = wormhole ? 1 + draw(size.planets - 1) : 1 + draw(40);
        const int id = edge++;
        connect(id, from, (from + offset) % size.planets, wormhole);
    }

    auto addShip = [&](int origin) {
        Spacecraft ship{};
        ship.id = static_cast<int>(data.ships.size());
        ship.currentLocation = origin;
        ship.maxCargoCapacity = 18 + draw(63);
        ship.maxFuelCapacity = 190 + draw(220);
        ship.currentFuel = ship.maxFuelCapacity * (0.55 + draw(46) / 100.0);
        ship.speed = 0.7 + draw(100) / 100.0;
        ship.availableAt = draw(5) / 2.0;
        ship.reliability = 0.84 + draw(17) / 100.0;
        ship.operatingCost = 0.5 + draw(100) / 100.0;
        data.ships.push_back(ship);
    };
    for (int i = 0; i < size.missions; ++i) {
        Mission mission;
        mission.id = i;
        mission.origin = draw(size.planets);
        mission.destination = (mission.origin + 1 + draw(i % 8 == 0 ? size.planets - 1 : 48)) % size.planets;
        mission.cargoType = i % 3 == 0 ? ResourceType::Medicine : ResourceType::Food;
        mission.cargoAmount = 6 + draw(55);
        mission.deadline = 4 + draw(25);
        mission.priority = 1 + draw(10);
        mission.requirements.minReliability = i % 5 == 0 ? 0.95 : 0.8;
        if (i % 7 == 0) {
            const int station = i % stationCount;
            mission.origin = station;
            mission.destination = data.stations[station].id;
            mission.cargoType = ResourceType::Medicine;
        }
        data.missions.push_back(mission);
        if (i % 4 != 0)
            addShip(mission.origin);
    }
    for (int i = 1; i < size.planets; i += 25)
        addShip(i);

    for (int i = 0; i < std::max(8, size.planets / 100); ++i) {
        Infrastructure p;
        p.id = i;
        p.locationId = (i * 25) % size.planets;
        p.constructionCost = 5 + draw(40);
        p.constructionTime = 1 + draw(12);
        p.strategicValue = 5 + draw(80);
        p.resourceImpact[ResourceType::Food] = 0.5 + draw(25) / 10.0;
        data.projects.push_back(std::move(p));
    }
    for (int i = 0; i < std::max(12, size.missions / 50); ++i) {
        Event e;
        e.id = i;
        e.eventTime = draw(9);
        if (i % 3 == 0) {
            e.eventType = EventType::RouteBlocked;
            e.affectedObjectId = draw(size.routes);
        } else if (i % 3 == 1) {
            e.eventType = EventType::SpacecraftDestroyed;
            e.affectedObjectType = AffectedObjectType::Spacecraft;
            e.affectedObjectId = draw(static_cast<unsigned>(data.ships.size()));
        } else {
            e.eventType = EventType::ResourceProductionDecrease;
            e.affectedObjectType = AffectedObjectType::Planet;
            e.affectedObjectId = 25 * draw(size.planets / 25);
            e.severity = 0.6;
        }
        data.events.push_back(e);
    }
    return data;
}

InfrastructureOptimizationResult greedyInfrastructure(std::vector<Infrastructure> projects, int budget) {
    std::sort(projects.begin(), projects.end(), [](const auto& a, const auto& b) {
        const auto av = a.strategicValue / a.constructionCost;
        const auto bv = b.strategicValue / b.constructionCost;
        return av == bv ? a.id < b.id : av > bv;
    });
    InfrastructureOptimizationResult result;
    for (const auto& p : projects) {
        if (result.totalCost + p.constructionCost <= budget) {
            result.totalCost += p.constructionCost;
            result.totalStrategicValue += p.strategicValue;
            result.selectedInfrastructureIds.push_back(p.id);
        }
    }
    return result;
}

void addStressClosures(const SchedulingResult& schedule, std::vector<Event>& events) {
    // Both stress variants use the same pre-disruption schedule and closures.
    std::unordered_set<int> routes;
    for (size_t i = 0; i < schedule.scheduled.size(); i += 8) {
        const auto& entry = schedule.scheduled[i];
        if (entry.routeIds.empty() || !routes.insert(entry.routeIds.front()).second)
            continue;
        Event event;
        event.eventTime = entry.departureTime;
        event.eventType = EventType::RouteBlocked;
        event.affectedObjectId = entry.routeIds.front();
        events.push_back(event);
    }
    std::cout << "Injected " << routes.size() << " exact-edge stress closures at scheduled departure times.\n";
}
}

int runBenchmark(int argc, char* argv[]) {
    if (argc < 4 || argc > 6 || (argc == 6 && std::string(argv[5]) != "--trace-plan"))
        throw std::invalid_argument(
            "Usage: --benchmark SIZE SEED [MODE [--trace-plan]] (inventory or inventory-uncached for Phase 1)");
    const std::string name = argv[2];
    const auto size = dimensions(name);
    size_t consumed = 0;
    const auto seed = std::stoul(argv[3], &consumed);
    if (consumed != std::string(argv[3]).size() || seed > 0xffffffffUL || argv[3][0] == '-')
        throw std::invalid_argument("Seed must be an unsigned 32-bit integer.");
    const std::string mode = argc >= 5 ? argv[4] : "custom";
    const bool inventoryMode = mode == "inventory" || mode == "inventory-uncached";
    if (mode != "custom" && mode != "earliest" && mode != "basic" &&
        mode != "all-routes" && mode != "greedy-build" &&
        mode != "stress" && mode != "stress-all-routes" && !inventoryMode)
        throw std::invalid_argument("Unknown benchmark mode.");

    const auto started = Clock::now();
    auto lastReport = started;
    auto elapsed = [&] { return std::chrono::duration<double>(Clock::now() - started).count(); };
    auto report = [&](const char* stage, size_t rows, size_t missions, bool force = false) {
        auto now = Clock::now();
        if (force || now - lastReport >= std::chrono::seconds(2)) {
            std::cout << "PROGRESS," << stage << ',' << elapsed() << ',' << rows << ',' << missions << std::endl;
            lastReport = now;
        }
    };
    std::cout << "Dataset " << name << ", seed " << seed << ", mode " << mode << ": "
              << size.planets << " planets, " << size.routes << " routes, " << size.missions
              << " input missions\n";
    report("generation", 0, 0, true);
    auto data = generate(size, seed);
    std::cout << "Population " << data.population << ", colonies " << data.colonies.size()
              << ", stations " << data.stations.size() << ", spacecraft " << data.ships.size()
              << ", aid locations " << data.aidDemands.size() << ", external events "
              << data.events.size() << '\n';
    if (!data.events.empty()) {
        const auto& sample = data.events.front();
        std::cout << "Sample disruption: route " << sample.affectedObjectId
                  << " closes at day " << sample.eventTime << '\n';
    }

    const RouteWeights weights{0.02, 0.03, 0.5, 3.0, 2.0};
    report("allocation", 0, 0, true);
    const auto allocation = mode == "basic" ? ResourceAllocator::allocate(data.aidDemands) :
        ResourceAllocator::allocateWithRoutes(data.aidDemands, data.graph, weights,
            [&](size_t count) { report("allocation", count, 0); });
    auto aid = LogisticsPlanner::createMissions(allocation, data.aidDemands,
        ResourceType::Food, 24.0, size.missions);
    if (!aid.empty())
        std::cout << "First aid proposal " << aid.front().origin << " -> " << aid.front().destination
                  << ", cargo " << aid.front().cargoAmount << ", deadline " << aid.front().deadline << '\n';
    data.missions.insert(data.missions.end(), aid.begin(), aid.end());
    std::cout << "Scheduling " << data.missions.size() << " missions, including "
              << aid.size() << " aid proposals.\n";

    report("scheduling", 0, 0, true);
    const InventoryPlanningInput planning{data.inventory, data.aidDemands, aid, mode == "inventory"};
    const auto schedule = MissionScheduler::schedule(data.missions, data.ships, data.graph,
        weights, mode == "earliest", [&](size_t count) { report("scheduling", 0, count); },
        inventoryMode ? &planning : nullptr);
    // Alternative suppliers change the source, while obligation ID, quantity and deadline stay fixed.
    for (auto& mission : data.missions)
        if (const auto source = schedule.revisedOrigins.find(mission.id);
            source != schedule.revisedOrigins.end())
            mission.origin = source->second;
    if (!schedule.scheduled.empty()) {
        const auto& first = schedule.scheduled.front();
        std::cout << "First assignment: mission " << first.missionId << ", ship " << first.spacecraftId
                  << ", depart " << first.departureTime << ", expected arrival " << first.arrivalTime
                  << ", fuel " << first.fuelUsed << '\n';
    }

    report("infrastructure", 0, 0, true);
    const int budget = std::max(50, size.planets / 8);
    const auto buildStarted = Clock::now();
    const auto construction = mode == "greedy-build" ? greedyInfrastructure(data.projects, budget) :
        InfrastructureOptimizer::optimize(data.projects, budget);
    const double infrastructureSeconds = std::chrono::duration<double>(Clock::now() - buildStarted).count();
    for (int id : construction.selectedInfrastructureIds) {
        const auto& project = data.projects.at(id);
        Event event;
        event.eventType = EventType::ConstructionCompleted;
        event.affectedObjectType = AffectedObjectType::Planet;
        event.affectedObjectId = project.locationId;
        event.eventTime = project.constructionTime;
        event.severity = project.resourceImpact.at(ResourceType::Food);
        data.events.push_back(event);
    }
    if (mode == "stress" || mode == "stress-all-routes")
        addStressClosures(schedule, data.events);
    report("simulation", 0, 0, true);
    const auto actual = SimulationEngine::run(data.graph, data.inventory, data.missions, data.ships,
        schedule, data.events, [&](size_t count) { report("simulation", 0, count); },
        mode == "all-routes" || mode == "stress-all-routes", 30);
    report("complete", 0, actual.completed + actual.failed, true);
    // Classroom baseline score. Population survival has no calibrated model.
    const double score = 10.0 * actual.completed - 10.0 * actual.failed -
        5.0 * schedule.rejectedMissionIds.size() + actual.deliveredAmount -
        actual.remainingShortage - actual.stockoutAmount - actual.waste -
        0.01 * actual.transportationCost - actual.risk + construction.totalStrategicValue;
    double weightedDemand = 0;
    double weightedTimely = 0;
    double weightedAidDemand = 0;
    double weightedTimelyAid = 0;
    for (const auto& mission : data.missions) {
        const bool isAid = mission.id >= size.missions;
        const double demand = mission.priority * mission.cargoAmount;
        weightedDemand += demand;
        if (isAid)
            weightedAidDemand += demand;
        const auto delivery = actual.deliveries.find(mission.id);
        if (delivery != actual.deliveries.end() && delivery->second.arrival <= mission.deadline) {
            const double fulfilled = mission.priority * delivery->second.amount;
            weightedTimely += fulfilled;
            if (isAid)
                weightedTimelyAid += fulfilled;
        }
    }
    const auto recoveredCompleted = std::count_if(schedule.recoveredMissionIds.begin(),
        schedule.recoveredMissionIds.end(), [&](int id) {
            return actual.missionStatuses.at(id) == MissionStatus::Completed;
        });
    std::cout << "Plan: " << aid.size() << " aid proposals, " << schedule.missionsScheduled
              << " assignments, " << schedule.rejectedMissionIds.size() << " rejected, "
              << construction.selectedInfrastructureIds.size() << " construction decisions\n"
              << "Actual: " << actual.completed << " completed, " << actual.failed << " failed, "
              << actual.deliveredAmount << " delivered, " << actual.remainingShortage
              << " ending shortage, " << actual.eventsProcessed << " events applied, "
              << actual.rerouted << " routes changed, score " << score << '\n'
              << "Operations: distance " << actual.distance << ", fuel " << actual.fuel
              << ", transport credits " << actual.transportationCost << ", risk " << actual.risk
              << ", waste " << actual.waste << ", unserved demand " << actual.stockoutAmount << '\n'
              << "Performance: " << elapsed() << " seconds, "
              << allocation.nodesExplored + schedule.nodesExplored + actual.nodesExplored
              << " graph nodes expanded, " << schedule.candidates << " ship-route checks, "
              << actual.replans << " route recomputations in " << actual.replanSeconds << " seconds\n";
    std::cout << "QUALITY," << schedule.recoveredMissionIds.size() << ',' << recoveredCompleted << ','
              << schedule.revisedOrigins.size() << ',' << actual.inventoryFailures << ','
              << weightedTimely << ',' << weightedDemand << ',' << weightedTimelyAid << ','
              << weightedAidDemand << ',' << schedule.reservationChecks << std::endl;
    std::cout << "RESULT," << allocation.totalDelivered << ',' << schedule.missionsScheduled
              << ',' << schedule.rejectedMissionIds.size() << ',' << actual.completed << ','
              << actual.failed << ',' << actual.deliveredAmount << ',' << actual.remainingShortage
              << ',' << actual.stockoutAmount << ',' << actual.waste << ',' << actual.distance << ','
              << actual.fuel << ',' << actual.transportationCost << ',' << actual.risk << ','
              << allocation.nodesExplored + schedule.nodesExplored + actual.nodesExplored << ','
              << allocation.candidates << ',' << schedule.candidates << ',' << actual.eventsProcessed
              << ',' << actual.unsupportedEvents << ',' << actual.replans << ',' << actual.rerouted
              << ',' << actual.replanSeconds << ',' << construction.totalCost << ','
              << construction.totalStrategicValue << ',' << infrastructureSeconds << ',' << score
              << ',' << elapsed() << ',' << (actual.replans ? std::to_string(actual.replanSeconds / actual.replans) : "NA")
              << std::endl;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory))) {
        std::cout << "Peak working set: " << memory.PeakWorkingSetSize << " bytes\n";
        std::cout << "MEMORY," << memory.PeakWorkingSetSize << std::endl;
    }
#endif
    if (inventoryMode)
        std::cout << "CACHE," << schedule.routeCacheHits << ',' << schedule.routeCachePeakEntries
                  << ',' << schedule.routeCachePeakPayloadBytes << ',' << schedule.routeCacheInvalidations << '\n';
    // Trace after timing and memory measurements. Hex floats preserve exact decision values.
    if (argc == 6) {
        std::cout << std::hexfloat;
        for (const auto& entry : schedule.scheduled) {
            const auto& w = entry.weights;
            std::cout << "PLAN,assigned," << entry.missionId << ',' << entry.spacecraftId << ','
                << entry.departureTime << ',' << entry.arrivalTime << ',' << entry.fuelUsed << ','
                << w.distance << ',' << w.fuel << ',' << w.time << ',' << w.risk << ',' << w.reliability;
            if (const auto source = schedule.revisedOrigins.find(entry.missionId);
                source != schedule.revisedOrigins.end())
                std::cout << ",supplier=" << source->second;
            std::cout << ",vertices";
            for (int vertex : entry.path)
                std::cout << ',' << vertex;
            std::cout << ",edges";
            for (int edge : entry.routeIds)
                std::cout << ',' << edge;
            std::cout << '\n';
        }
        for (int id : schedule.rejectedMissionIds)
            std::cout << "PLAN,rejected," << id << '\n';
        for (int id : schedule.recoveredMissionIds)
            std::cout << "PLAN,recovered," << id << '\n';
        std::cout << std::defaultfloat;
    }
    return 0;
}

int runSelfTests() {
    auto require = [](bool condition, const char* message) {
        if (!condition)
            throw std::runtime_error(message);
    };
    GalaxyGraph graph;
    auto edge = [&](int id, int from, int to, double time, double fuel, double capacity) {
        Route route{};
        route.id = id;
        route.from = from;
        route.to = to;
        route.travelTime = time;
        route.fuelConsumption = fuel;
        route.distance = time;
        route.maxCapacity = capacity;
        route.isAvailable = true;
        route.reliability = 1;
        graph.addRoute(route);
    };
    edge(1, 1, 2, 1, 2, 20);
    edge(2, 1, 2, 4, 3, 20);
    edge(3, 2, 3, 1, 5, 20);
    edge(4, 1, 3, 0.5, 6, 4);
    Mission first;
    first.id = 1;
    first.origin = 1;
    first.destination = 2;
    first.cargoAmount = 6;
    first.deadline = 8;
    first.priority = 2;
    Mission second = first;
    second.id = 2;
    second.origin = 2;
    second.destination = 3;
    second.deadline = 3;
    second.priority = 1;
    Spacecraft ship{};
    ship.id = 1;
    ship.currentLocation = 1;
    ship.availableAt = 0;
    ship.maxCargoCapacity = 20;
    ship.maxFuelCapacity = 8;
    ship.currentFuel = 8;
    ship.speed = 1;
    ship.reliability = 1;
    const auto schedule = MissionScheduler::schedule({first, second}, {ship}, graph);
    require(schedule.scheduled.size() == 2 && schedule.scheduled[0].routeIds == std::vector<int>{1},
        "The original two missions should be chained on the intended exact routes.");
    require(schedule.scheduled[0].fuelUsed == 2 && schedule.scheduled[1].arrivalTime == 2,
        "Planning must use edge fuel and travel time.");
    const auto capacity = Dijkstra::findShortestPath(graph, 1, 3, {}, 6);
    require(capacity.routeIds == std::vector<int>({1, 3}), "An undersized direct edge must be skipped.");
    require(!Dijkstra::findShortestPath(graph, 1, 3, {-1}).found,
        "Dijkstra must reject negative weights.");
    Event closure;
    closure.eventType = EventType::RouteBlocked;
    closure.affectedObjectId = 1;
    const std::vector<ResourceDemand> inventory{{1, 20, 0, 1}, {2, 20, 0, 1}, {3, 0, 0, 1}};
    const auto disrupted = SimulationEngine::run(graph, inventory, {first, second}, {ship},
        schedule, {closure});
    require(disrupted.completed == 1 && disrupted.failed == 1 && disrupted.rerouted == 1 &&
        disrupted.fuel == 3 && disrupted.distance == 4,
        "Closing a parallel edge must use the detour and prevent overlapping the next trip.");
    second.deadline = 6;
    const auto relaxed = SimulationEngine::run(graph, inventory, {first, second}, {ship},
        schedule, {closure});
    require(relaxed.completed == 2 && relaxed.failed == 0 && relaxed.fuel == 8,
        "A delayed second trip should depart on arrival when still feasible.");
    ship.currentFuel = 1;
    require(MissionScheduler::schedule({first}, {ship}, graph).scheduled.empty(),
        "The scheduler must reject insufficient fuel.");

    const auto routed = ResourceAllocator::allocateWithRoutes({{1, 20, 0, 1}, {2, 0, 6, 2}}, graph);
    require(routed.transfers.size() == 1 && routed.transfers[0].fromPlanet == 1 &&
        routed.transfers[0].routeIds == std::vector<int>{1} && routed.totalDelivered == 6,
        "Reverse search must find an inbound supplier and preserve the selected edge ID.");
    ResourceDemand receiver{2, 10000, 10001, 3, ResourceType::Food, 300, 700};
    const auto aid = LogisticsPlanner::createMissions(routed, {receiver}, ResourceType::Food, 30);
    require(aid.size() == 1 && aid[0].deadline == 25, "The known depletion example is 25 days.");

    Event shock;
    shock.eventType = EventType::ResourceProductionDecrease;
    shock.affectedObjectType = AffectedObjectType::Planet;
    shock.affectedObjectId = 1;
    shock.eventTime = 2;
    shock.severity = 0.5;
    Event construction = shock;
    construction.eventType = EventType::ConstructionCompleted;
    construction.eventTime = 3;
    construction.severity = 2;
    const auto changedStock = SimulationEngine::run(graph,
        {{1, 10, 0, 1, ResourceType::Food, 2, 1, 100}},
        {}, {}, {}, {shock, construction}, {}, false, 5);
    require(changedStock.eventsProcessed == 2 && changedStock.finalResources.at(1) == 16,
        "Production changes must apply only after their event times.");

    first.cargoType = ResourceType::Medicine;
    ship.currentFuel = 8;
    const auto medicineSchedule = MissionScheduler::schedule({first}, {ship}, graph);
    const auto missingMedicine = SimulationEngine::run(graph, inventory, {first}, {ship},
        medicineSchedule, {});
    require(missingMedicine.failed == 1 && missingMedicine.deliveredAmount == 0,
        "Food stock cannot satisfy a medicine mission.");

    first.cargoType = ResourceType::Food;
    second.priority = first.priority + 1;
    const std::vector<ResourceDemand> chainBalances{{1, 6, 0, 1}, {2, 0, 0, 1}, {3, 0, 0, 1}};
    const std::vector<Mission> noAid;
    const InventoryPlanningInput chainPlanning{chainBalances, chainBalances, noAid};
    const auto chainPlan = MissionScheduler::schedule({first, second}, {ship}, graph,
        {}, false, {}, &chainPlanning);
    const auto chainRun = SimulationEngine::run(graph, chainBalances, {first, second},
        {ship}, chainPlan, {});
    // Two obligations is the upper bound. The only feasible order is 1 then 2.
    require(chainRun.completed == 2 && chainRun.inventoryFailures == 0 &&
        chainPlan.recoveredMissionIds == std::vector<int>{second.id},
        "The second pass must recover the chain using the first delivery as inventory.");

    auto competing = first;
    competing.id = 9;
    auto secondShip = ship;
    secondShip.id = 2;
    const auto competingPlan = MissionScheduler::schedule({first, competing}, {ship, secondShip},
        graph, {}, false, {}, &chainPlanning);
    const auto competingRun = SimulationEngine::run(graph, chainBalances, {first, competing},
        {ship, secondShip}, competingPlan, {});
    // A common source has six units and each obligation requires six, so the exact optimum is one.
    require(competingPlan.rejectedMissionIds.size() == 1 && competingRun.completed == 1 &&
        competingRun.inventoryFailures == 0, "Concurrent withdrawals cannot reserve the same cargo.");

    const std::vector<ResourceDemand> productionBalances{
        {1, 0, 0, 1, ResourceType::Food, 5, 2, 20}, {2, 0, 0, 1}, {3, 0, 0, 1}};
    const InventoryPlanningInput productionPlanning{productionBalances, productionBalances, noAid};
    first.deadline = 3;
    const auto delayed = MissionScheduler::schedule({first}, {ship}, graph,
        {}, false, {}, &productionPlanning);
    require(delayed.scheduled.size() == 1 && delayed.scheduled[0].departureTime == 2 &&
        SimulationEngine::run(graph, productionBalances, {first}, {ship}, delayed, {}).completed == 1,
        "Net production must enable a departure exactly at the latest feasible time.");

    InventoryReservations book(chainBalances);
    require(book.reserve(1, 2, ResourceType::Food, 2, 3, 6, true) &&
        !book.reserve(1, 3, ResourceType::Food, 0, 1, 6, true) &&
        book.reserve(2, 3, ResourceType::Food, 3, 4, 6, false),
        "Rejecting an inserted withdrawal must preserve existing reservations and incoming stock.");
    InventoryReservations boundedSink({{1, 12, 0, 1}, {2, 0, 0, 1, ResourceType::Food, 0, 0, 6}});
    require(boundedSink.reserve(1, 2, ResourceType::Food, 3, 4, 6, true) &&
        !boundedSink.reserve(1, 2, ResourceType::Food, 1, 2, 6, true),
        "An earlier insertion cannot consume storage needed by a reserved arrival.");

    auto alternativeAid = first;
    alternativeAid.origin = 2;
    const std::vector<Mission> eligibleAid{alternativeAid};
    const InventoryPlanningInput supplierPlanning{chainBalances, chainBalances, eligibleAid};
    const auto supplierPlan = MissionScheduler::schedule(eligibleAid, {ship}, graph,
        {}, false, {}, &supplierPlanning);
    require(supplierPlan.revisedOrigins.at(alternativeAid.id) == 1 &&
        supplierPlan.recoveredMissionIds.size() == 1 && supplierPlan.missionsScheduled == 1,
        "A rejected aid obligation can retain its identity while changing supplier.");

    auto changedGraph = graph;
    std::vector<Mission> repeated(4, first);
    std::vector<Spacecraft> fleet(4, ship);
    for (size_t i = 0; i < repeated.size(); ++i) {
        repeated[i].id = static_cast<int>(i + 1);
        repeated[i].deadline = 8;
        fleet[i].id = static_cast<int>(i + 1);
    }
    const std::vector<ResourceDemand> stocked{{1, 60, 0, 1}, {2, 0, 0, 1}};
    const InventoryPlanningInput stockedPlan{stocked, stocked, noAid};
    const auto afterUpdates = MissionScheduler::schedule(repeated, fleet, changedGraph,
        {1, 0, 0, 10}, false, [&](size_t i) {
            if (i == 1) {
                changedGraph.blockRoute(1);
                changedGraph.blockRoute(2);
            } else if (i == 2) {
                changedGraph.unblockRoute(1);
                changedGraph.unblockRoute(2);
            } else if (i == 3) {
                changedGraph.updateRouteRisk(1, 1);
            } else if (i == 4) {
                Route shortcut = *changedGraph.findRoute(2);
                shortcut.id = 77;
                shortcut.distance = shortcut.travelTime = 0.5;
                changedGraph.addRoute(shortcut);
            }
        }, &stockedPlan);
    require(afterUpdates.routeCacheInvalidations == 4 &&
        afterUpdates.scheduled.size() == 4 &&
        afterUpdates.scheduled[0].routeIds == std::vector<int>{1} &&
        afterUpdates.scheduled[1].missionId == 3 &&
        afterUpdates.scheduled[1].routeIds == std::vector<int>{1} &&
        afterUpdates.scheduled[2].routeIds == std::vector<int>{2} &&
        afterUpdates.scheduled[3].routeIds == std::vector<int>{77} &&
        afterUpdates.recoveredMissionIds == std::vector<int>{2},
        "Closure, reopening, risk changes, and new edges must invalidate cached searches.");

    auto samePlan = [&](const SchedulingResult& a, const SchedulingResult& b) {
        require(a.rejectedMissionIds == b.rejectedMissionIds &&
            a.recoveredMissionIds == b.recoveredMissionIds && a.revisedOrigins == b.revisedOrigins &&
            a.scheduled.size() == b.scheduled.size(), "Cache changed mission decisions.");
        auto fields = [](const ScheduledMission& m) {
            const auto& w = m.weights;
            return std::tie(m.missionId, m.spacecraftId, m.departureTime, m.arrivalTime,
                m.fuelUsed, m.path, m.routeIds, w.distance, w.fuel, w.time, w.risk, w.reliability);
        };
        for (size_t i = 0; i < a.scheduled.size(); ++i)
            require(fields(a.scheduled[i]) == fields(b.scheduled[i]),
                "Cache changed an exact assignment field.");
        require(b.routeCachePeakEntries <= 12288 && b.routeCachePeakPayloadBytes <= 2 * 1024 * 1024,
            "Route cache exceeded its bound.");
    };
    const InventoryPlanningInput uncachedChain{chainBalances, chainBalances, noAid, false};
    first.deadline = 8;
    samePlan(MissionScheduler::schedule({first, second}, {ship}, graph, {}, false, {}, &uncachedChain),
        chainPlan);
    samePlan(MissionScheduler::schedule({first, competing}, {ship, secondShip}, graph,
        {}, false, {}, &uncachedChain), competingPlan);

    edge(5, 3, 4, 1, 1, 20);
    auto third = second;
    third.id = 3;
    third.origin = 3;
    third.destination = 4;
    third.priority = second.priority + 1;
    auto longChainBalances = chainBalances;
    longChainBalances.push_back({4, 0, 0, 1});
    const InventoryPlanningInput longChainCached{longChainBalances, longChainBalances, noAid};
    const InventoryPlanningInput longChainUncached{longChainBalances, longChainBalances, noAid, false};
    const auto longChain = MissionScheduler::schedule({first, second, third}, {ship}, graph,
        {}, false, {}, &longChainCached);
    samePlan(MissionScheduler::schedule({first, second, third}, {ship}, graph,
        {}, false, {}, &longChainUncached), longChain);
    require(longChain.missionsScheduled == 2, "Cache changed the bounded three-leg retry outcome.");

    const InventoryPlanningInput uncachedProduction{productionBalances, productionBalances, noAid, false};
    first.deadline = 3;
    samePlan(MissionScheduler::schedule({first}, {ship}, graph, {}, false, {}, &uncachedProduction),
        delayed);
    auto surpriseCut = shock;
    surpriseCut.eventTime = 1;
    surpriseCut.severity = 1;
    require(SimulationEngine::run(graph, productionBalances, {first}, {ship}, delayed,
        {surpriseCut}).inventoryFailures == 1,
        "A production shock can still invalidate a nominal reservation.");

    for (const std::string scenario : {"scarce_sparse", "donor_production_cut"}) {
        auto data = generate(dimensions("medium"), 42);
        data.events.clear();
        if (scenario == "scarce_sparse") {
            for (auto& balance : data.inventory)
                balance.available = std::floor(0.4 * balance.available);
            for (auto& demand : data.aidDemands)
                demand.available = std::floor(0.4 * demand.available);
            std::erase_if(data.ships, [](const auto& candidate) { return candidate.id % 3 != 0; });
        } else {
            for (const auto& demand : data.aidDemands) {
                if (demand.available <= demand.required)
                    continue;
                Event cut;
                cut.eventType = EventType::ResourceProductionDecrease;
                cut.affectedObjectId = demand.planetId;
                cut.eventTime = 1;
                cut.severity = 1;
                data.events.push_back(cut);
            }
        }
        const RouteWeights stressWeights{0.02, 0.03, 0.5, 3.0, 2.0};
        const auto allocation = ResourceAllocator::allocateWithRoutes(data.aidDemands, data.graph, stressWeights);
        const auto obligations = LogisticsPlanner::createMissions(allocation, data.aidDemands,
            ResourceType::Food, 24, 500);
        data.missions.insert(data.missions.end(), obligations.begin(), obligations.end());
        const InventoryPlanningInput uncached{data.inventory, data.aidDemands, obligations, false};
        const InventoryPlanningInput cached{data.inventory, data.aidDemands, obligations};
        const auto before = MissionScheduler::schedule(data.missions, data.ships,
            data.graph, stressWeights, false, {}, &uncached);
        const auto after = MissionScheduler::schedule(data.missions, data.ships,
            data.graph, stressWeights, false, {}, &cached);
        samePlan(before, after);
        for (auto& mission : data.missions)
            if (const auto source = after.revisedOrigins.find(mission.id); source != after.revisedOrigins.end())
                mission.origin = source->second;
        const auto executed = SimulationEngine::run(data.graph, data.inventory, data.missions,
            data.ships, after, data.events, {}, false, 30);
        require(executed.completed == (scenario == "scarce_sparse" ? 87 : 255),
            "Stress execution diverged from the verified inventory-plan outcome.");
        std::cout << "Cache stress " << scenario << ": identical plan, " << executed.completed
                  << " completed, " << executed.inventoryFailures << " stock failures.\n";
    }

    std::vector<Infrastructure> projects(4);
    const int costs[] = {40, 50, 60, 30};
    const int values[] = {60, 80, 90, 45};
    for (int i = 0; i < 4; ++i) {
        projects[i].id = i;
        projects[i].constructionCost = costs[i];
        projects[i].strategicValue = values[i];
    }
    int exact = 0;
    for (int mask = 0; mask < 16; ++mask) {
        int cost = 0;
        int value = 0;
        for (int i = 0; i < 4; ++i)
            if (mask & (1 << i)) {
                cost += costs[i];
                value += values[i];
            }
        if (cost <= 100)
            exact = std::max(exact, value);
    }
    const auto dp = InfrastructureOptimizer::optimize(projects, 100);
    require(dp.totalStrategicValue == exact && dp.totalCost <= 100,
        "Knapsack must equal exhaustive subset enumeration.");
    std::cout << "Self-tests passed: route identity, capacity, fuel, overlap, reverse allocation, "
                 "depletion, inventory, production events, and exact Knapsack.\n";
    return 0;
}
