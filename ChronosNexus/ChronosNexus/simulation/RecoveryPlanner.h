#pragma once

#include <optional>
#include "../algorithms/InventoryReservations.h"
#include "../algorithms/MissionScheduler.h"

namespace recovery {
struct Booking {
    const Mission* mission;
    const ScheduledMission* decision;
};

struct ShipPlan {
    Spacecraft state; // State after any confirmed in-flight delivery.
    std::vector<Booking> pending;
};
std::optional<Spacecraft> projectTail(ShipPlan& plan, const GalaxyGraph& graph, double now);

struct SupplySearch {
    double horizon;
    double minimumCargo;
    double assignedCargo = 0;
    bool requirePrevention = true;
    bool considerOperatingCost = false;
    long long routeQueries = 0;
    long long feasibilityChecks = 0;
    double routeSeconds = 0;
    long long routeNodes = 0;
    long long routeEdges = 0;
    bool deadlinePruned = false;
    std::optional<double> optimisticTime;
    std::function<void(double, int)> observeQuery;
};

std::optional<ScheduledMission> findAssignment(const Mission& mission,
    const ScheduledMission* previous, const GalaxyGraph& graph, std::vector<ShipPlan>& ships,
    InventoryReservations& inventory, double now, long long& nodesExplored,
    SupplySearch* supply = nullptr, bool reconsiderRisk = false);
}
