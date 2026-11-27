#include "MissionScheduler.h"
#include "PlanningBudget.h"
#include "InventoryReservations.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace {
bool validMission(const Mission& mission) {
    const auto& r = mission.requirements;
    return mission.id >= 0 && mission.origin >= 0 && mission.destination >= 0 &&
        std::isfinite(mission.cargoAmount) && mission.cargoAmount > 0 &&
        std::isfinite(mission.deadline) && mission.deadline >= 0 &&
        std::isfinite(r.minCargoCapacity) && std::isfinite(r.minFuelCapacity) &&
        std::isfinite(r.minSpeed) && std::isfinite(r.minReliability);
}
}

bool MissionScheduler::feasible(const Mission& mission, const Spacecraft& ship,
    double departure, const RouteMetrics& route, double& arrival) {
    const auto& r = mission.requirements;
    if (!validMission(mission) || ship.currentLocation != mission.origin ||
        !std::isfinite(departure) || departure < 0 ||
        !std::isfinite(ship.availableAt) || ship.availableAt < 0 ||
        departure < ship.availableAt || !std::isfinite(ship.maxCargoCapacity) ||
        ship.maxCargoCapacity < std::max(mission.cargoAmount, r.minCargoCapacity) ||
        !std::isfinite(ship.maxFuelCapacity) || ship.maxFuelCapacity <= 0 ||
        ship.maxFuelCapacity < r.minFuelCapacity || !std::isfinite(ship.currentFuel) ||
        ship.currentFuel < route.fuel || ship.currentFuel > ship.maxFuelCapacity ||
        !std::isfinite(ship.speed) || ship.speed <= 0 || ship.speed < r.minSpeed ||
        !std::isfinite(ship.reliability) || ship.reliability < std::max(0.0, r.minReliability) ||
        ship.reliability > 1)
        return false;
    // Edge time is measured at reference speed 1. Ship speed is a multiplier.
    arrival = departure + route.time / ship.speed;
    return std::isfinite(arrival) && arrival <= mission.deadline;
}

double MissionScheduler::serviceValue(const Mission& mission) {
    return mission.cargoAmount * (std::max(0, mission.priority) + std::max(0.0, mission.importance));
}

double MissionScheduler::bookingValue(const Mission& mission, const Spacecraft& ship, const RouteMetrics& route) {
    return serviceValue(mission) - 0.01 * ship.operatingCost * route.distance - route.risk -
        (1 - route.reliability) - (1 - ship.reliability);
}

SchedulingResult MissionScheduler::schedule(std::vector<Mission> missions,
    std::vector<Spacecraft> ships, const GalaxyGraph& galaxy,
    const RouteWeights& weights, bool earliestArrival,
    const std::function<void(size_t)>& progress, const InventoryPlanningInput* inventory, bool benefitAware,
    const std::function<void(const SchedulingResult&)>& publishPrefix, InitialSchedulingProfile* profile) {
    using Clock = PlanningBudget::Clock;
    const auto started = Clock::now();
    RouteSearchWorkspace workspace;
    SchedulingResult result;
    std::optional<InventoryReservations> reservations;
    if (inventory)
        reservations.emplace(inventory->balances);
    // These independent limits bound both map overhead and allocated path storage.
    constexpr size_t maxCachedRoutes = 12288;
    constexpr size_t maxPathBytes = 2 * 1024 * 1024;
    using RouteKey = std::tuple<int, int, double, double, double, double, double, double>;
    std::map<RouteKey, PathResult> routes;
    size_t pathBytes = 0;
    auto cacheRevision = galaxy.revision();
    auto findRoute = [&](const Mission& mission, const RouteWeights& w) {
        auto search = [&] {
            const auto began = profile ? Clock::now() : Clock::time_point{};
            if (profile)
                ++profile->routeCalls;
            PathResult path;
            try {
                path = Dijkstra::findShortestPath(galaxy, mission.origin,
                    mission.destination, w, mission.cargoAmount,
                    profile && profile->useWorkspace ? &workspace : nullptr);
            } catch (const PlanningBudget::Interrupted&) {
                if (profile)
                    profile->routeSeconds += std::chrono::duration<double>(Clock::now() - began).count();
                throw;
            }
            if (profile) {
                profile->routeSeconds += std::chrono::duration<double>(Clock::now() - began).count();
                profile->nodes += path.nodesExplored;
                profile->edges += path.edgesExplored;
                if (path.found) {
                    ++profile->foundQueries;
                    profile->foundNodes += path.nodesExplored;
                    profile->foundDistance += path.metrics.distance;
                } else {
                    ++profile->missingQueries;
                    profile->missingNodes += path.nodesExplored;
                }
            }
            result.nodesExplored += path.nodesExplored;
            return path;
        };
        if (!inventory || !inventory->cacheRoutes || !std::isfinite(w.distance) ||
            !std::isfinite(w.fuel) || !std::isfinite(w.time) ||
            !std::isfinite(w.risk) || !std::isfinite(w.reliability))
            return search();
        if (cacheRevision != galaxy.revision()) {
            routes.clear();
            pathBytes = 0;
            cacheRevision = galaxy.revision();
            ++result.routeCacheInvalidations;
        }
        const RouteKey key{mission.origin, mission.destination, mission.cargoAmount,
            w.distance, w.fuel, w.time, w.risk, w.reliability};
        if (const auto hit = routes.find(key); hit != routes.end()) {
            ++result.routeCacheHits;
            if (profile)
                ++profile->routeHits;
            return hit->second;
        }
        auto path = search();
        const size_t remainingIds = (maxPathBytes - pathBytes) / sizeof(int);
        if (routes.size() < maxCachedRoutes && path.path.capacity() <= remainingIds &&
            path.routeIds.capacity() <= remainingIds - path.path.capacity()) {
            const auto entry = routes.emplace(key, std::move(path)).first;
            pathBytes += sizeof(int) * (entry->second.path.capacity() + entry->second.routeIds.capacity());
            result.routeCachePeakEntries = std::max(result.routeCachePeakEntries, routes.size());
            result.routeCachePeakPayloadBytes = std::max(result.routeCachePeakPayloadBytes, pathBytes);
            return entry->second;
        }
        return path;
    };
    std::unordered_map<int, int> missionCounts;
    std::unordered_map<int, int> shipCounts;
    for (const auto& mission : missions)
        ++missionCounts[mission.id];
    for (const auto& ship : ships)
        ++shipCounts[ship.id];
    std::erase_if(missions, [&](const Mission& mission) {
        if (validMission(mission) && missionCounts.at(mission.id) == 1)
            return false;
        result.rejectedMissionIds.push_back(mission.id);
        return true;
    });
    std::sort(missions.begin(), missions.end(), [benefitAware](const Mission& a, const Mission& b) {
        if (benefitAware && serviceValue(a) != serviceValue(b))
            return serviceValue(a) > serviceValue(b);
        if (a.priority != b.priority)
            return a.priority > b.priority;
        if (a.deadline != b.deadline)
            return a.deadline < b.deadline;
        return a.id < b.id;
    });

    std::unordered_map<int, std::set<size_t>> atLocation;
    for (size_t i = 0; i < ships.size(); ++i)
        if (ships[i].id >= 0 && shipCounts.at(ships[i].id) == 1)
            atLocation[ships[i].currentLocation].insert(i);

    auto assign = [&](const Mission& mission) {
        const auto origin = atLocation.find(mission.origin);
        if (origin == atLocation.end() || origin->second.empty())
            return false;
        double availableFuel = 0;
        if (profile && profile->pruneRoutes) {
            bool eligible = false;
            for (size_t i : origin->second) {
                PlanningBudget::checkpoint();
                double arrival;
                if (feasible(mission, ships[i], ships[i].availableAt, {}, arrival)) {
                    eligible = true;
                    availableFuel = std::max(availableFuel, ships[i].currentFuel);
                }
            }
            if (!eligible) {
                ++profile->carrierPrunes;
                return false;
            }
        }
        auto path = findRoute(mission, weights);
        if (!path.found)
            return false;

        const auto noShip = ships.size();
        size_t winner = noShip;
        double bestArrival = 0;
        double bestDeparture = 0;
        double bestScore = std::numeric_limits<double>::infinity();
        PathResult bestPath;
        RouteWeights bestWeights = weights;
        auto consider = [&](const PathResult& candidate, const RouteWeights& candidateWeights) {
            if (!candidate.found)
                return;
            for (size_t i : origin->second) {
                PlanningBudget::checkpoint();
                ++result.candidates;
                if (profile)
                    ++profile->shipChecks;
                const auto& ship = ships[i];
                double arrival;
                if (!feasible(mission, ship, ship.availableAt, candidate.metrics, arrival))
                    continue;
                double departure = ship.availableAt;
                if (reservations) {
                    const auto began = profile ? Clock::now() : Clock::time_point{};
                    const double duration = candidate.metrics.time / ship.speed;
                    bool found = false;
                    for (double time : reservations->departures(mission.origin, mission.destination,
                        mission.cargoType, departure, mission.deadline - duration, duration, mission.cargoAmount)) {
                        ++result.reservationChecks;
                        if (profile)
                            ++profile->inventoryChecks;
                        if (feasible(mission, ship, time, candidate.metrics, arrival) &&
                            reservations->reserve(mission.origin, mission.destination, mission.cargoType,
                                time, arrival, mission.cargoAmount, false)) {
                            departure = time;
                            found = true;
                            break;
                        }
                    }
                    if (profile)
                        profile->inventorySeconds += std::chrono::duration<double>(Clock::now() - began).count();
                    if (!found)
                        continue;
                }
                double score = earliestArrival ? arrival :
                    0.50 * (arrival - ship.availableAt) / (1 + mission.deadline - ship.availableAt) +
                    0.35 * (1 - mission.cargoAmount / ship.maxCargoCapacity) +
                    0.15 * candidate.metrics.fuel / ship.maxFuelCapacity;
                if (benefitAware) {
                    const double cost = ship.operatingCost * candidate.metrics.distance;
                    score += cost / (1 + cost) + (1 - ship.reliability);
                }
                if (score < bestScore || (score == bestScore && arrival < bestArrival)) {
                    winner = i;
                    bestArrival = arrival;
                    bestDeparture = departure;
                    bestScore = score;
                    bestPath = candidate;
                    bestWeights = candidateWeights;
                }
            }
        };
        consider(path, weights);
        if (winner == noShip) {
            // A lightweight feasibility fallback. It can miss routes between these extremes.
            for (const RouteWeights alternative : {RouteWeights{0, 1, 0}, RouteWeights{0, 0, 1}}) {
                auto alternatePath = findRoute(mission, alternative);
                if (profile && profile->pruneRoutes && alternative.fuel == 1 &&
                    alternatePath.found && alternatePath.metrics.fuel > availableFuel) {
                    ++profile->fuelPrunes;
                    break;
                }
                consider(alternatePath, alternative);
            }
        }
        if (winner == noShip)
            return false;
        if (reservations && !reservations->reserve(mission.origin, mission.destination,
            mission.cargoType, bestDeparture, bestArrival, mission.cargoAmount, true))
            throw std::logic_error("Selected reservation became infeasible.");
        auto& ship = ships[winner];
        result.scheduled.push_back({mission.id, ship.id, bestDeparture, bestArrival,
            bestPath.metrics.fuel, std::move(bestPath.path), std::move(bestPath.routeIds), bestWeights,
            FlightEstimate::from(bestPath.metrics, bestDeparture, ship.speed)});
        origin->second.erase(winner);
        ship.currentLocation = mission.destination;
        ship.availableAt = bestArrival;
        ship.currentFuel -= bestPath.metrics.fuel;
        atLocation[ship.currentLocation].insert(winner);
        return true;
    };

    std::vector<const Mission*> retry;
    size_t processed = result.rejectedMissionIds.size();
    try {
        size_t nextPublication = 1;
        for (const auto& mission : missions) {
            PlanningBudget::checkpoint();
            if (profile)
                ++profile->missionsTested;
            if (progress)
                progress(processed);
            ++processed;
            if (!assign(mission)) {
                if (inventory)
                    retry.push_back(&mission);
                else
                    result.rejectedMissionIds.push_back(mission.id);
            }
            if (publishPrefix && result.scheduled.size() == nextPublication) {
                result.missionsScheduled = static_cast<int>(result.scheduled.size());
                publishPrefix(result);
                nextPublication *= 2;
            }
        }
        if (inventory && !retry.empty()) {
            std::unordered_set<int> aidIds;
            std::unordered_map<ResourceType, std::unordered_set<int>> donors;
            for (const auto& mission : inventory->aidMissions)
                aidIds.insert(mission.id);
            for (const auto& demand : inventory->aidNetwork)
                if (demand.available > demand.required)
                    donors[demand.type].insert(demand.planetId);
            std::optional<GalaxyGraph> reverse;
            std::uint64_t reverseRevision = 0;

            // Try one append-only insertion pass. Existing ship assignments and times stay fixed.
            for (const auto* mission : retry) {
                PlanningBudget::checkpoint();
                if (profile)
                    ++profile->retriesTested;
                if (progress)
                    progress(processed);
                ++processed;
                bool recovered = assign(*mission);
                if (!recovered && aidIds.contains(mission->id) && donors.contains(mission->cargoType)) {
                    if (!reverse || reverseRevision != galaxy.revision()) {
                        reverse.emplace(galaxy.reversed());
                        reverseRevision = galaxy.revision();
                    }
                    const auto& candidates = donors.at(mission->cargoType);
                    std::unordered_set<int> tried{mission->origin, mission->destination};
                    for (int attempt = 0; attempt < 4 && !recovered; ++attempt) {
                        const auto nearest = Dijkstra::findNearest(*reverse, mission->destination,
                            [&](int id) {
                                auto local = atLocation.find(id);
                                return candidates.contains(id) && !tried.contains(id) &&
                                    local != atLocation.end() && !local->second.empty();
                            }, weights, mission->cargoAmount);
                        result.nodesExplored += nearest.nodesExplored;
                        if (!nearest.found)
                            break;
                        Mission alternative = *mission;
                        alternative.origin = nearest.path.back();
                        tried.insert(alternative.origin);
                        recovered = assign(alternative);
                        if (recovered)
                            result.revisedOrigins.emplace(alternative.id, alternative.origin);
                    }
                }
                if (recovered)
                    result.recoveredMissionIds.push_back(mission->id);
                else
                    result.rejectedMissionIds.push_back(mission->id);
            }
        }
        if (progress)
            progress(processed);
    } catch (const PlanningBudget::Interrupted&) {
        if (!publishPrefix)
            throw;
        result.interrupted = true;
    }
    if (profile) {
        profile->workspaceBytes = workspace.bytes();
        profile->totalSeconds = std::chrono::duration<double>(Clock::now() - started).count();
    }
    result.missionsScheduled = static_cast<int>(result.scheduled.size());
    return result;
}
