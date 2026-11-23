#include "RecoveryPlanner.h"

#include <algorithm>
#include <array>
#include <tuple>

namespace recovery {
namespace {
struct Slot {
    Spacecraft ship;
    double unreservedFuel;
    const Booking* next;
};

std::vector<Slot> availableSlots(const Mission& mission,
    const GalaxyGraph& graph, std::vector<ShipPlan>& ships, double now) {
    std::vector<Slot> slots;
    for (auto& plan : ships) {
        auto ship = plan.state;
        if (ship.maxCargoCapacity < mission.cargoAmount || ship.speed <= 0)
            continue;
        auto& pending = plan.pending;
        std::sort(pending.begin(), pending.end(), [](const Booking& a, const Booking& b) {
            return std::tie(a.decision->departureTime, a.decision->missionId) <
                std::tie(b.decision->departureTime, b.decision->missionId);
        });
        const size_t firstSlot = slots.size();
        bool validChain = true;
        for (const auto& booking : pending) {
            const auto& entry = *booking.decision;
            RouteMetrics metrics;
            double arrival = 0;
            if (entry.departureTime < now ||
                !Dijkstra::measure(graph, entry.routeIds, booking.mission->origin,
                    booking.mission->destination, booking.mission->cargoAmount, metrics) ||
                !MissionScheduler::feasible(*booking.mission, ship, entry.departureTime, metrics, arrival) ||
                arrival != entry.arrivalTime) {
                validChain = false;
                break;
            }
            if (ship.currentLocation == mission.origin && booking.mission->origin == mission.destination)
                slots.push_back({ship, 0, &booking});
            ship.currentFuel -= metrics.fuel;
            ship.currentLocation = booking.mission->destination;
            ship.availableAt = arrival;
        }
        if (!validChain) {
            slots.resize(firstSlot);
            continue;
        }
        if (ship.currentLocation == mission.origin)
            slots.push_back({ship, 0, nullptr});
        for (size_t i = firstSlot; i < slots.size(); ++i)
            slots[i].unreservedFuel = ship.currentFuel;
    }
    return slots;
}
}

std::optional<ScheduledMission> findAssignment(const Mission& mission,
    const ScheduledMission* previous, const GalaxyGraph& graph, std::vector<ShipPlan>& ships,
    InventoryReservations& inventory, double now, long long& nodesExplored) {
    const auto slots = availableSlots(mission, graph, ships, now);
    if (slots.empty())
        return std::nullopt;
    const RouteWeights weights = previous ? previous->weights : RouteWeights{0.02, 0.03, 0.5, 3.0, 2.0};
    auto rank = [&](const ScheduledMission& row) {
        const bool unchanged = previous && row.spacecraftId == previous->spacecraftId &&
            row.departureTime == previous->departureTime && row.routeIds == previous->routeIds;
        return std::tuple{!unchanged, previous && row.spacecraftId != previous->spacecraftId,
            row.arrivalTime, row.fuelUsed, row.spacecraftId};
    };
    std::optional<ScheduledMission> best;
    auto consider = [&](const PathResult& path) {
        for (const auto& slot : slots) {
            const auto& ship = slot.ship;
            if (path.metrics.fuel > slot.unreservedFuel)
                continue;
            const double duration = path.metrics.time / ship.speed;
            const double earliest = std::max(now, ship.availableAt);
            const double limit = slot.next ? std::min(mission.deadline, slot.next->decision->departureTime) : mission.deadline;
            const double latest = limit - duration;
            const auto times = inventory.departures(mission.origin, mission.destination, mission.cargoType,
                earliest - now, latest - now, duration, mission.cargoAmount);
            auto tryAt = [&](double departure) {
                double arrival = 0;
                if (departure < earliest || !MissionScheduler::feasible(mission, ship, departure, path.metrics, arrival) ||
                    arrival > limit || !inventory.reserve(mission.origin, mission.destination, mission.cargoType,
                        departure - now, arrival - now, mission.cargoAmount, false))
                    return false;
                ScheduledMission candidate{mission.id, ship.id, departure, arrival, path.metrics.fuel,
                    path.path, path.routeIds, weights};
                if (!best || rank(candidate) < rank(*best))
                    best = std::move(candidate);
                return true;
            };
            if (previous && previous->spacecraftId == ship.id && previous->routeIds == path.routeIds &&
                tryAt(previous->departureTime))
                continue;
            for (const double offset : times)
                if (tryAt(now + offset))
                    break;
        }
    };

    std::vector<std::vector<int>> searched;
    if (previous) {
        PathResult original;
        original.routeIds = previous->routeIds;
        original.path = previous->path;
        if (Dijkstra::measure(graph, original.routeIds, mission.origin, mission.destination,
            mission.cargoAmount, original.metrics)) {
            consider(original);
            if (best && !std::get<0>(rank(*best)))
                return best;
            searched.push_back(std::move(original.routeIds));
        }
    }
    for (const auto& strategy : std::array{weights, RouteWeights{0, 1, 0}, RouteWeights{0, 0, 1}}) {
        auto path = Dijkstra::findShortestPath(graph, mission.origin, mission.destination, strategy, mission.cargoAmount);
        nodesExplored += path.nodesExplored;
        if (path.found && std::find(searched.begin(), searched.end(), path.routeIds) == searched.end()) {
            consider(path);
            searched.push_back(std::move(path.routeIds));
        }
    }
    return best;
}
}
