#include "RecoveryPlanner.h"

#include <algorithm>
#include <array>
#include <numeric>
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
    InventoryReservations& inventory, double now, long long& nodesExplored,
    SupplySearch* supply, bool reconsiderRisk) {
    auto smallest = mission;
    if (supply)
        smallest.cargoAmount = supply->minimumCargo;
    const auto slots = availableSlots(smallest, graph, ships, now);
    if (slots.empty())
        return std::nullopt;
    const RouteWeights standard{0.02, 0.03, 0.5, 3.0, 2.0};
    auto weights = previous ? previous->weights : standard;
    if (reconsiderRisk && weights.risk == 0)
        weights.risk = standard.risk;
    auto rank = [&](const ScheduledMission& row) {
        const bool unchanged = previous && row.spacecraftId == previous->spacecraftId &&
            row.departureTime == previous->departureTime && row.routeIds == previous->routeIds;
        double routeCost = 0;
        if (reconsiderRisk)
            for (int edge : row.routeIds)
                routeCost += Dijkstra::edgeCost(*graph.findRoute(edge), weights);
        return std::tuple{routeCost, !unchanged, previous && row.spacecraftId != previous->spacecraftId,
            row.arrivalTime, row.fuelUsed, row.spacecraftId};
    };
    std::optional<ScheduledMission> best;
    const auto donorBefore = supply
        ? inventory.project(mission.origin, mission.cargoType, supply->horizon) : InventoryReservations::Projection{};
    const auto recipientBefore = supply
        ? inventory.project(mission.destination, mission.cargoType, supply->horizon) : InventoryReservations::Projection{};
    auto consider = [&](const PathResult& path) {
        double routeCargo = mission.cargoAmount;
        if (supply)
            for (int edge : path.routeIds)
                routeCargo = std::min(routeCargo, graph.findRoute(edge)->maxCapacity);
        for (const auto& slot : slots) {
            const auto& ship = slot.ship;
            if (path.metrics.fuel > slot.unreservedFuel)
                continue;
            const double duration = path.metrics.time / ship.speed;
            const double earliest = std::max(now, ship.availableAt);
            const double limit = slot.next ? std::min(mission.deadline, slot.next->decision->departureTime) : mission.deadline;
            const double latest = limit - duration;
            const double upper = supply ? std::min(routeCargo, ship.maxCargoCapacity) : mission.cargoAmount;
            if (upper < smallest.cargoAmount || latest < earliest || (supply && upper < supply->assignedCargo))
                continue;
            auto times = inventory.departures(mission.origin, mission.destination, mission.cargoType,
                earliest - now, latest - now, duration, upper);
            if (supply && upper > smallest.cargoAmount) {
                const auto smallTimes = inventory.departures(mission.origin, mission.destination, mission.cargoType,
                    earliest - now, latest - now, duration, smallest.cargoAmount);
                times.insert(times.end(), smallTimes.begin(), smallTimes.end());
                times.push_back(latest - now);
                std::sort(times.begin(), times.end());
                times.erase(std::unique(times.begin(), times.end()), times.end());
            }
            auto feasibleAt = [&](double departure, double cargo, double& arrival) {
                auto job = mission;
                job.cargoAmount = cargo;
                if (departure < earliest || !MissionScheduler::feasible(job, ship, departure, path.metrics, arrival) ||
                    arrival > limit || !inventory.reserve(mission.origin, mission.destination, mission.cargoType,
                        departure - now, arrival - now, cargo, false))
                    return false;
                if (supply) {
                    auto trial = inventory.subset({{mission.origin, mission.cargoType}, {mission.destination, mission.cargoType}});
                    trial.adjust(mission.origin, mission.destination, mission.cargoType,
                        departure - now, arrival - now, cargo);
                    const auto donorAfter = trial.project(mission.origin, mission.cargoType, supply->horizon);
                    const auto recipient = trial.project(mission.destination, mission.cargoType, supply->horizon);
                    const bool improvesRecipient = recipient.unserved < recipientBefore.unserved ||
                        recipient.endingShortage < recipientBefore.endingShortage ||
                        (recipientBefore.depletion &&
                            (!recipient.depletion || *recipient.depletion > *recipientBefore.depletion));
                    if (donorAfter.unserved > donorBefore.unserved ||
                        donorAfter.endingShortage > donorBefore.endingShortage ||
                        (donorAfter.depletion && (!donorBefore.depletion || *donorAfter.depletion < *donorBefore.depletion)) ||
                        (recipient.depletion && now + *recipient.depletion <= mission.deadline) ||
                        !improvesRecipient)
                        return false;
                }
                return true;
            };
            auto tryAt = [&](double departure) {
                double arrival = 0;
                double cargo = upper;
                if (!feasibleAt(departure, cargo, arrival)) {
                    if (!supply || upper <= std::max(supply->assignedCargo, smallest.cargoAmount))
                        return false;
                    cargo = std::max(supply->assignedCargo, smallest.cargoAmount);
                    if (!feasibleAt(departure, cargo, arrival))
                        return false;
                    double infeasible = upper;
                    while (infeasible - cargo > smallest.cargoAmount) {
                        const double middle = std::midpoint(cargo, infeasible);
                        if (feasibleAt(departure, middle, arrival))
                            cargo = middle;
                        else
                            infeasible = middle;
                    }
                }
                ScheduledMission candidate{mission.id, ship.id, departure, arrival, path.metrics.fuel,
                    path.path, path.routeIds, weights, FlightEstimate::from(path.metrics, departure, ship.speed)};
                const double bestCargo = supply ? supply->assignedCargo : cargo;
                if (!best || cargo > bestCargo || (cargo == bestCargo && rank(candidate) < rank(*best))) {
                    best = std::move(candidate);
                    if (supply)
                        supply->assignedCargo = cargo;
                }
                return cargo == upper;
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
            if (best && !reconsiderRisk && best->spacecraftId == previous->spacecraftId &&
                best->departureTime == previous->departureTime && best->routeIds == previous->routeIds)
                return best;
            searched.push_back(std::move(original.routeIds));
        }
    }
    auto search = [&](double cargo) {
        for (const auto& strategy : std::array{weights, RouteWeights{0, 1, 0}, RouteWeights{0, 0, 1}}) {
            auto path = Dijkstra::findShortestPath(graph, mission.origin, mission.destination, strategy, cargo);
            nodesExplored += path.nodesExplored;
            if (path.found && std::find(searched.begin(), searched.end(), path.routeIds) == searched.end()) {
                consider(path);
                searched.push_back(std::move(path.routeIds));
            }
        }
    };
    double searchCargo = mission.cargoAmount;
    if (supply) {
        double capacity = 0;
        for (const auto& slot : slots)
            capacity = std::max(capacity, slot.ship.maxCargoCapacity);
        searchCargo = std::min(searchCargo, capacity);
    }
    search(searchCargo);
    if (supply && supply->assignedCargo < searchCargo && smallest.cargoAmount < searchCargo)
        search(smallest.cargoAmount);
    return best;
}
}
