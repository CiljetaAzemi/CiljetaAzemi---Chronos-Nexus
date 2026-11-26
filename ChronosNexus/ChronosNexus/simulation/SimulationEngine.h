#pragma once

#include <vector>
#include <unordered_map>
#include <optional>

#include "../models/Event.h"
#include "../models/Mission.h"
#include "../models/Spacecraft.h"
#include "../graph/GalaxyGraph.h"
#include "../algorithms/ResourceAllocator.h"
#include "../algorithms/MissionScheduler.h"

struct SimulationResult {
    int completed = 0;
    int failed = 0;
    int scheduled = 0;
    int eventsProcessed = 0;
    int unsupportedEvents = 0;
    int rerouted = 0;
    int inventoryFailures = 0;
    int replans = 0;
    double replanSeconds = 0;
    long long nodesExplored = 0;
    double distance = 0;
    double fuel = 0;
    double transportationCost = 0;
    double risk = 0;
    double waste = 0;
    double stockoutAmount = 0;

    double deliveredAmount = 0.0;
    double remainingShortage = 0.0;

    std::unordered_map<int, double> finalResources;
    struct ResourceOutcome { double endingShortage; double unserved; };
    std::unordered_map<int, std::unordered_map<ResourceType, ResourceOutcome>> resourceOutcomes;
    std::unordered_map<int, MissionStatus> missionStatuses;
    struct Delivery { double amount; double arrival; FlightEstimate estimate; };
    std::unordered_map<int, Delivery> deliveries;

    struct ReplanningDecision {
        enum class Reason { BlockedRoute, UnavailableSpacecraft, UrgentMission, DependentAssignment,
            PreventiveSupply, IncreasedRouteRisk, DisplacedBooking };
        int eventId;
        int targetId;
        double eventTime;
        int missionId;
        Reason reason;
        std::optional<ScheduledMission> previous;
        std::optional<ScheduledMission> replacement;
    };
    std::vector<ReplanningDecision> replanningDecisions;

    struct ShortageForecast {
        int location;
        ResourceType resource;
        double calculatedAt;
        double endingStock;
        double endingShortage;
        double unserved;
        std::optional<double> depletionTime;
    };
    struct Prevention {
        ShortageForecast before;
        std::optional<Mission> mission;
        std::optional<ScheduledMission> assignment;
        ShortageForecast after;
    };
    std::vector<ShortageForecast> shortageForecasts;
    std::vector<Prevention> preventiveDecisions;
    std::vector<Mission> activatedUrgentMissions;
    struct AllocationWork {
        long long attempted = 0;
        long long rejected = 0;
        long long routeQueries = 0;
        long long feasibilityChecks = 0;
        long long partial = 0;
        long long itineraryBuilds = 0;
        long long donorShipCandidates = 0;
        long long receivers = 0;
        long long deadlinePruned = 0;
        long long routeNodes = 0;
        long long routeEdges = 0;
        long long repeatedQueries = 0;
        long long boundTrees = 0;
        long long boundNodes = 0;
        long long boundEdges = 0;
        double boundSeconds = 0;
        double itinerarySeconds = 0;
        double recoverySeconds = 0;
        double routeSeconds = 0;
        double proposedCargo = 0;
        double committedCargo = 0;
    } allocationWork;
};

struct AidRoutingOptions {
    bool reverseBounds = false;
    std::function<void(const SimulationResult::AllocationWork&)> progress;
};

class CivilizationObjective;
struct InfrastructureOptimizationResult;
struct SimulationObjective {
    const CivilizationObjective& initial;
    const InfrastructureOptimizationResult& construction;
};

class SimulationEngine {
public:
    static SimulationResult run(
        const GalaxyGraph& galaxy,
        const std::vector<ResourceDemand>& planets,
        const std::vector<Mission>& missions,
        const std::vector<Spacecraft>& spacecraft,
        const SchedulingResult& schedule,
        const std::vector<Event>& events,
        const std::function<void(size_t)>& progress = {},
        bool recheckAllDepartures = false,
        double minimumHorizon = 0,
        const std::vector<Event>* knownSupplyEvents = nullptr,
        const SimulationObjective* scoring = nullptr,
        const std::vector<ResourceDemand>* allocationNetwork = nullptr,
        bool indexedAid = true,
        const AidRoutingOptions& aidRouting = {}
    );
};

