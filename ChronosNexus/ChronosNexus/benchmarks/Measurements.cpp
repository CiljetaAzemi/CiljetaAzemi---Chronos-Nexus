#include "Measurements.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <psapi.h>
#endif

namespace benchmark {
double Measurements::elapsed() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
}

void Measurements::report(const char* stage, size_t rows, size_t missions, bool force) {
    const auto now = Clock::now();
    if (force || now - lastReport >= std::chrono::seconds(2)) {
        std::cout << "PROGRESS," << stage << ',' << elapsed() << ',' << rows << ',' << missions << std::endl;
        lastReport = now;
    }
}

void reportResults(const std::vector<Mission>& missions, int inputMissionCount, size_t aidCount,
    const AllocationResult& allocation, const SchedulingResult& schedule,
    const InfrastructureOptimizationResult& construction, const SimulationResult& actual,
    double infrastructureSeconds, const Measurements& measurements, bool inventoryMode, bool improve,
    bool tracePlan) {
    // Classroom baseline score. Population survival has no calibrated model.
    const double score = 10.0 * actual.completed - 10.0 * actual.failed -
        5.0 * schedule.rejectedMissionIds.size() + actual.deliveredAmount -
        actual.remainingShortage - actual.stockoutAmount - actual.waste -
        0.01 * actual.transportationCost - actual.risk + construction.totalStrategicValue;
    double weightedDemand = 0;
    double weightedTimely = 0;
    double weightedAidDemand = 0;
    double weightedTimelyAid = 0;
    for (const auto& mission : missions) {
        const bool isAid = mission.id >= inputMissionCount;
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
    std::cout << "Plan: " << aidCount << " aid proposals, " << schedule.missionsScheduled
              << " assignments, " << schedule.rejectedMissionIds.size() << " rejected, "
              << construction.selectedInfrastructureIds.size() << " construction decisions\n"
              << "Actual: " << actual.completed << " completed, " << actual.failed << " failed, "
              << actual.deliveredAmount << " delivered, " << actual.remainingShortage
              << " ending shortage, " << actual.eventsProcessed << " events applied, "
              << actual.rerouted << " routes changed, score " << score << '\n'
              << "Operations: distance " << actual.distance << ", fuel " << actual.fuel
              << ", transport credits " << actual.transportationCost << ", risk " << actual.risk
              << ", waste " << actual.waste << ", unserved demand " << actual.stockoutAmount << '\n'
              << "Performance: " << measurements.elapsed() << " seconds, "
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
              << ',' << measurements.elapsed() << ',' << (actual.replans ? std::to_string(actual.replanSeconds / actual.replans) : "NA")
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
    if (improve)
        std::cout << "LOCAL," << schedule.searchTrials << ',' << schedule.acceptedMoves << ','
            << schedule.locallyRecovered << ',' << schedule.searchArcs << ',' << schedule.prunedArcs << '\n';
    // Trace after timing and memory measurements. Hex floats preserve exact decision values.
    if (tracePlan) {
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
}
}
