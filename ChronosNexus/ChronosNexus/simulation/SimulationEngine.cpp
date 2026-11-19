#include "SimulationEngine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <queue>
#include <stdexcept>
#include <unordered_set>

namespace {
enum class ActionType { Event, Arrival, Departure };

struct Action {
    double time;
    ActionType type;
    size_t index;
};

struct Later {
    bool operator()(const Action& a, const Action& b) const {
        if (a.time != b.time)
            return a.time > b.time;
        if (a.type != b.type)
            return a.type > b.type;
        return a.index > b.index;
    }
};

struct Balance {
    double stock = 0;
    double target = 0;
    double production = 0;
    double consumption = 0;
    double storage = 1e30;
    double updatedAt = 0;

    void advance(double now, SimulationResult& result) {
        const double next = stock + (production - consumption) * (now - updatedAt);
        result.waste += std::max(0.0, next - storage);
        result.stockoutAmount += std::max(0.0, -next);
        stock = std::clamp(next, 0.0, storage);
        updatedAt = now;
    }
};
}

SimulationResult SimulationEngine::run(const GalaxyGraph& galaxy,
    const std::vector<ResourceDemand>& planets, const std::vector<Mission>& missions,
    const std::vector<Spacecraft>& spacecraft, const SchedulingResult& schedule,
    const std::vector<Event>& events, const std::function<void(size_t)>& progress,
    bool recheckAllDepartures, double minimumHorizon) {
    SimulationResult result;
    result.scheduled = static_cast<int>(schedule.scheduled.size());
    GalaxyGraph active = galaxy;
    std::unordered_map<int, std::unordered_map<ResourceType, Balance>> balances;
    for (const auto& p : planets) {
        if (!std::isfinite(p.available) || !std::isfinite(p.required) ||
            !std::isfinite(p.production) || !std::isfinite(p.consumption) ||
            !std::isfinite(p.storage) || p.available < 0 || p.required < 0 ||
            p.production < 0 || p.consumption < 0 || p.storage < p.available ||
            !balances[p.planetId].emplace(p.type, Balance{p.available, p.required,
                p.production, p.consumption, p.storage}).second)
            throw std::invalid_argument("Invalid or duplicate inventory.");
    }
    std::unordered_map<int, const Mission*> missionById;
    std::unordered_map<int, Spacecraft> ships;
    for (const auto& mission : missions) {
        if (!missionById.emplace(mission.id, &mission).second)
            throw std::invalid_argument("Duplicate mission ID.");
        result.missionStatuses.emplace(mission.id, MissionStatus::Pending);
    }
    for (const auto& ship : spacecraft)
        if (!ships.emplace(ship.id, ship).second)
            throw std::invalid_argument("Duplicate spacecraft ID.");

    std::unordered_set<int> disabled;
    std::unordered_set<int> inFlight;
    std::priority_queue<Action, std::vector<Action>, Later> queue;
    for (size_t i = 0; i < events.size(); ++i)
        if (std::isfinite(events[i].eventTime) && events[i].eventTime >= 0)
            queue.push({events[i].eventTime, ActionType::Event, i});
    for (size_t i = 0; i < schedule.scheduled.size(); ++i) {
        const auto& entry = schedule.scheduled[i];
        if (!missionById.contains(entry.missionId) ||
            !std::isfinite(entry.departureTime) || entry.departureTime < 0)
            throw std::invalid_argument("Invalid scheduled departure.");
        auto& status = result.missionStatuses.at(entry.missionId);
        if (status != MissionStatus::Pending)
            throw std::invalid_argument("Duplicate scheduled mission.");
        status = MissionStatus::Scheduled;
        queue.push({entry.departureTime, ActionType::Departure, i});
    }

    double now = 0;
    while (!queue.empty()) {
        const auto action = queue.top();
        queue.pop();
        now = action.time;
        if (progress)
            progress(static_cast<size_t>(result.completed + result.failed));
        if (action.type == ActionType::Event) {
            const auto& event = events[action.index];
            const auto id = event.affectedObjectId;
            bool applied = false;
            switch (event.eventType) {
            case EventType::RouteBlocked:
            case EventType::WormholeCollapse:
            case EventType::PirateBlockade:
                applied = active.blockRoute(id);
                break;
            case EventType::RouteRiskIncrease:
                applied = active.updateRouteRisk(id, event.severity);
                break;
            case EventType::SpacecraftDestroyed:
            case EventType::SpacecraftMechanicalFailure:
                applied = ships.contains(id) && disabled.insert(id).second;
                break;
            case EventType::ResourceProductionDecrease:
            case EventType::ConstructionCompleted: {
                const auto location = balances.find(id);
                if (location == balances.end())
                    break;
                const auto resource = location->second.find(event.resourceType);
                if (resource == location->second.end() || !std::isfinite(event.severity))
                    break;
                auto& b = resource->second;
                b.advance(now, result);
                if (event.eventType == EventType::ConstructionCompleted)
                    b.production += std::max(0.0, event.severity);
                else
                    b.production *= 1 - std::clamp(event.severity, 0.0, 1.0);
                applied = true;
                break;
            }
            default:
                break;
            }
            if (applied)
                ++result.eventsProcessed;
            else
                ++result.unsupportedEvents;
            continue;
        }

        const auto& entry = schedule.scheduled[action.index];
        const auto& mission = *missionById.at(entry.missionId);
        auto& status = result.missionStatuses.at(mission.id);
        auto fail = [&] {
            status = MissionStatus::Failed;
            ++result.failed;
        };
        auto shipIt = ships.find(entry.spacecraftId);
        if (action.type == ActionType::Arrival) {
            if (status != MissionStatus::InProgress)
                continue;
            inFlight.erase(entry.spacecraftId);
            if (disabled.contains(entry.spacecraftId)) {
                result.waste += mission.cargoAmount;
                fail();
                continue;
            }
            auto& ship = shipIt->second;
            ship.currentLocation = mission.destination;
            ship.availableAt = now;
            auto& destination = balances[mission.destination][mission.cargoType];
            destination.advance(now, result);
            const double accepted = std::min(mission.cargoAmount, destination.storage - destination.stock);
            destination.stock += accepted;
            result.deliveredAmount += accepted;
            result.deliveries.emplace(mission.id, SimulationResult::Delivery{accepted, now});
            result.waste += mission.cargoAmount - accepted;
            status = MissionStatus::Completed;
            ++result.completed;
            continue;
        }

        if (status != MissionStatus::Scheduled)
            continue;
        const auto origin = balances.find(mission.origin);
        if (shipIt == ships.end() || disabled.contains(entry.spacecraftId)) {
            fail();
            continue;
        }
        if (origin == balances.end() || !origin->second.contains(mission.cargoType)) {
            ++result.inventoryFailures;
            fail();
            continue;
        }
        auto& ship = shipIt->second;
        if (inFlight.contains(ship.id) || ship.availableAt > now) {
            if (ship.availableAt > now && ship.availableAt <= mission.deadline)
                queue.push({ship.availableAt, ActionType::Departure, action.index});
            else
                fail();
            continue;
        }
        if (ship.currentLocation != mission.origin) {
            fail();
            continue;
        }
        auto& stock = origin->second.at(mission.cargoType);
        stock.advance(now, result);
        if (stock.stock < mission.cargoAmount) {
            ++result.inventoryFailures;
            fail();
            continue;
        }

        RouteMetrics metrics;
        double arrival = 0;
        const bool originalFeasible = Dijkstra::measure(active, entry.routeIds,
            mission.origin, mission.destination, mission.cargoAmount, metrics) &&
            MissionScheduler::feasible(mission, ship, now, metrics, arrival);
        if (recheckAllDepartures || !originalFeasible) {
            ++result.replans;
            const auto start = std::chrono::steady_clock::now();
            bool found = false;
            for (const auto& weights : {entry.weights, RouteWeights{0, 1, 0}, RouteWeights{0, 0, 1}}) {
                const auto alternative = Dijkstra::findShortestPath(active, mission.origin,
                    mission.destination, weights, mission.cargoAmount);
                result.nodesExplored += alternative.nodesExplored;
                if (alternative.found &&
                    MissionScheduler::feasible(mission, ship, now, alternative.metrics, arrival)) {
                    metrics = alternative.metrics;
                    found = true;
                    if (alternative.routeIds != entry.routeIds)
                        ++result.rerouted;
                    break;
                }
            }
            result.replanSeconds += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start).count();
            if (!found) {
                fail();
                continue;
            }
        }
        stock.stock -= mission.cargoAmount;
        ship.currentFuel -= metrics.fuel;
        ship.availableAt = arrival;
        inFlight.insert(ship.id);
        status = MissionStatus::InProgress;
        // Account for the full route at departure. Closures affect future departures only.
        result.distance += metrics.distance;
        result.fuel += metrics.fuel;
        result.risk += metrics.risk;
        result.transportationCost += metrics.distance * ship.operatingCost;
        queue.push({arrival, ActionType::Arrival, action.index});
    }

    now = std::max(now, minimumHorizon);
    for (auto& [location, resources] : balances) {
        for (auto& [type, stock] : resources) {
            stock.advance(now, result);
            result.finalResources[location] += stock.stock;
            result.remainingShortage += std::max(0.0, stock.target - stock.stock);
        }
    }
    if (progress)
        progress(static_cast<size_t>(result.completed + result.failed));
    return result;
}
