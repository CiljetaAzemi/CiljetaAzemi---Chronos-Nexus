#pragma once

#include <stdexcept>
#include "../algorithms/MissionScheduler.h"

namespace regression {
inline void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

struct FlightFixture {
    GalaxyGraph graph;
    Mission first;
    Mission second;
    Spacecraft ship;
};

FlightFixture makeFlightFixture();
void addTestRoute(GalaxyGraph& graph, int id, int from, int to, double time, double fuel, double capacity);
void requireSamePlan(const SchedulingResult& a, const SchedulingResult& b);

void testRoutingAndSimulation();
void testAllocationAndForecast();
void testInventoryAndCache();
void testPlanImprovement();
void testStressScenarios();
void testInfrastructure();
}
