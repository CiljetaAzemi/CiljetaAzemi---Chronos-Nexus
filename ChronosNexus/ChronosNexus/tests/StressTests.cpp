#include "TestSupport.h"
#include "../benchmarks/DatasetGenerator.h"
#include "../algorithms/LogisticsPlanner.h"
#include "../algorithms/PlanImprover.h"
#include "../simulation/SimulationEngine.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace regression {
void testStressScenarios() {
    for (const std::string scenario : {"scarce_sparse", "donor_production_cut"}) {
        auto data = benchmark::generate(benchmark::dimensions("medium"), 42);
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
        requireSamePlan(before, after);
        for (auto& mission : data.missions)
            if (const auto source = after.revisedOrigins.find(mission.id); source != after.revisedOrigins.end())
                mission.origin = source->second;
        const auto executed = SimulationEngine::run(data.graph, data.inventory, data.missions,
            data.ships, after, data.events, {}, false, 30);
        require(executed.completed == (scenario == "scarce_sparse" ? 87 : 255),
            "Stress execution diverged from the verified inventory-plan outcome.");
        std::cout << "Cache stress " << scenario << ": identical plan, " << executed.completed
                  << " completed, " << executed.inventoryFailures << " stock failures.\n";
        // Restore input supplier identities before evaluating each mode independently.
        for (const auto& original : obligations)
            for (auto& mission : data.missions)
                if (mission.id == original.id)
                    mission.origin = original.origin;
        const auto baseline = MissionScheduler::schedule(data.missions, data.ships, data.graph, stressWeights);
        const auto local = PlanImprover::improve(after, data.missions, data.ships,
            data.graph, data.inventory, stressWeights, 30);
        for (const auto& [name, schedule] : std::vector<std::pair<std::string, const SchedulingResult*>>{
            {"custom", &baseline}, {"inventory", &after}, {"phase2", &local}}) {
            auto obligationsForRun = data.missions;
            for (auto& mission : obligationsForRun)
                if (const auto source = schedule->revisedOrigins.find(mission.id);
                    source != schedule->revisedOrigins.end())
                    mission.origin = source->second;
            const auto result = SimulationEngine::run(data.graph, data.inventory, obligationsForRun,
                data.ships, *schedule, data.events, {}, false, 30);
            double total = 0, timely = 0;
            for (const auto& mission : obligationsForRun) {
                total += mission.priority * mission.cargoAmount;
                if (const auto delivery = result.deliveries.find(mission.id);
                    delivery != result.deliveries.end() && delivery->second.arrival <= mission.deadline)
                    timely += mission.priority * delivery->second.amount;
            }
            if (name == "phase2") {
                const auto nominal = SimulationEngine::run(data.graph, data.inventory, obligationsForRun,
                    data.ships, *schedule, {}, {}, false, 30);
                require(nominal.completed == schedule->missionsScheduled && nominal.inventoryFailures == 0,
                    "Phase 2 stress plan must be feasible in a nominal simulator replay.");
            }
            std::cout << "PHASE2_STRESS," << scenario << ',' << name << ',' << result.completed << ','
                << result.failed << ',' << schedule->rejectedMissionIds.size() << ',' << timely / total * 100
                << ',' << result.remainingShortage << ',' << result.stockoutAmount << ',' << result.fuel
                << ',' << schedule->searchTrials << ',' << schedule->acceptedMoves << '\n';
        }
    }
}
}
