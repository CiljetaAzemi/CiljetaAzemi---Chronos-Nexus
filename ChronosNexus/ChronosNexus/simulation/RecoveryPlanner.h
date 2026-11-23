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

std::optional<ScheduledMission> findAssignment(const Mission& mission,
    const ScheduledMission* previous, const GalaxyGraph& graph, std::vector<ShipPlan>& ships,
    InventoryReservations& inventory, double now, long long& nodesExplored);
}
