#pragma once

#include <chrono>
#include "../algorithms/InfrastructureOptimizer.h"
#include "../simulation/SimulationEngine.h"

namespace benchmark {
using Clock = std::chrono::steady_clock;

class Measurements {
public:
    double elapsed() const;
    void report(const char* stage, size_t rows, size_t missions, bool force = false);

private:
    Clock::time_point started = Clock::now();
    Clock::time_point lastReport = started;
};

void reportResults(const std::vector<Mission>& missions, int inputMissionCount, size_t aidCount,
    const AllocationResult& allocation, const SchedulingResult& schedule,
    const InfrastructureOptimizationResult& construction, const SimulationResult& actual,
    double infrastructureSeconds, const Measurements& measurements, bool inventoryMode, bool improve,
    bool tracePlan);
}
