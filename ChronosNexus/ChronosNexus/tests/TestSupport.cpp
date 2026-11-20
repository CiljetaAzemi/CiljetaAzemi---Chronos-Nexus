#include "TestSupport.h"
#include <tuple>

namespace regression {
void addTestRoute(GalaxyGraph& graph, int id, int from, int to, double time, double fuel, double capacity) {
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
}

FlightFixture makeFlightFixture() {
    GalaxyGraph graph;
    addTestRoute(graph, 1, 1, 2, 1, 2, 20);
    addTestRoute(graph, 2, 1, 2, 4, 3, 20);
    addTestRoute(graph, 3, 2, 3, 1, 5, 20);
    addTestRoute(graph, 4, 1, 3, 0.5, 6, 4);
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
    return {std::move(graph), first, second, ship};
}

void requireSamePlan(const SchedulingResult& a, const SchedulingResult& b) {
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
}
}
