#pragma once

#include "CivilizationObjective.h"
#include "PlanningBudget.h"

namespace benchmark { struct Scenario; }

struct CivilizationPlan {
    SchedulingResult schedule;
    InfrastructureOptimizationResult construction;
    double forecastScore = 0;
    CivilizationMetrics forecastMetrics{};
    int evaluated = 0;
    int feasible = 0;
};

struct CoordinatedAid {
    std::vector<Mission> missions;
    AllocationResult allocation;
    SimulationResult::AllocationWork work;
    bool retained = false;
    double coordinationSeconds = 0;
    double replaySeconds = 0;
};

struct IncumbentSample {
    double seconds, legacyScore, expandedScore, timelyService, criticalShortage;
    double shortage, unserved, transportation, fuel, waste, risk;
    int scheduled, failed, unscheduled;
    double essentialCoverage, criticalUnserved;
};

struct AnytimePlan {
    CivilizationPlan plan;
    std::vector<Mission> jobs;
    CoordinatedAid coordinated;
    std::optional<SimulationResult> forecast;
    std::optional<SimulationResult> simulation;
    std::vector<IncumbentSample> trace;
    std::optional<double> firstUseful;
    double validationSeconds = 0;
    int evaluated = 0, accepted = 0, interrupted = 0;
    bool planningComplete = false;
    InitialSchedulingProfile initialProfile;
};

struct LongSearchOptions {
    std::string identity, checkpoint, progress;
    unsigned seed = 1;
    bool adaptive = true, resume = false, replayOnly = false;
    bool neighborhoods = false, acceptWorse = true;
    bool exactHybrid = false;
    bool expandingHybrid = false;
    double reportSeconds = 30;
    // Experimental coefficients for quality, time, memory and operational burden.
    std::array<double, 4> coefficients{1, .05, .05, .1};
    double timeReference = 28800, memoryReferenceMiB = 1024;
};

class CivilizationPlanner {
public:
    using CandidateObserver = std::function<void(const SchedulingResult&,
        const InfrastructureOptimizationResult&, SimulationResult&&)>;

    static AnytimePlan anytime(const benchmark::Scenario& data, const CivilizationObjective& legacy,
        const CivilizationObjective& expanded, const CivilizationObjective& objective,
        const std::unordered_set<int>& colonies, int budget, double horizon, const RouteWeights& weights,
        const std::vector<Event>& knownProductionChanges, PlanningBudget& clock,
        bool coordinatedAllocator, bool indexedAid, bool reverseBounds,
        const std::function<std::vector<Mission>()>& proposeAid, bool proactiveSimulation = false,
        std::optional<int> profileInitial = std::nullopt, const LongSearchOptions* longSearch = nullptr);

    static CoordinatedAid coordinateAid(CivilizationPlan& plan, const std::vector<Mission>& obligations,
        const std::vector<ResourceDemand>& accounts, const std::vector<ResourceDemand>& network,
        const std::vector<Spacecraft>& ships, const GalaxyGraph& graph, const CivilizationObjective& objective,
        const std::vector<Event>& knownSupply, double horizon, int firstAidId, bool indexedAid = true,
        const AidRoutingOptions& aidRouting = {});
    // Construction events are generated from committed projects.
    static std::vector<Event> constructionEvents(const std::vector<Infrastructure>& projects,
        const InfrastructureOptimizationResult& construction);

    static CivilizationPlan choose(SchedulingResult initial, const std::vector<Mission>& obligations,
        const std::vector<Mission>& aid, const std::vector<ResourceDemand>& inventory,
        const std::vector<ResourceDemand>& aidNetwork, const std::vector<Spacecraft>& ships,
        const GalaxyGraph& graph, const std::vector<Infrastructure>& projects,
        const CivilizationObjective& objective, const std::unordered_set<int>& colonies,
        int budget, double horizon, const RouteWeights& weights,
        const std::vector<Event>& knownProductionChanges = {}, const CandidateObserver& observer = {},
        double* validationSeconds = nullptr, int* evaluatedCandidates = nullptr);
};
