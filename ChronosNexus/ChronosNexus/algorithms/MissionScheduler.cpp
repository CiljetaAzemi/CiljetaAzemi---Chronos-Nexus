#include "MissionScheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
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

SchedulingResult MissionScheduler::schedule(std::vector<Mission> missions,
    std::vector<Spacecraft> ships, const GalaxyGraph& galaxy,
    const RouteWeights& weights, bool earliestArrival,
    const std::function<void(size_t)>& progress) {
    SchedulingResult result;
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
    std::sort(missions.begin(), missions.end(), [](const Mission& a, const Mission& b) {
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

    size_t processed = result.rejectedMissionIds.size();
    for (const auto& mission : missions) {
        if (progress)
            progress(processed);
        ++processed;
        const auto origin = atLocation.find(mission.origin);
        if (origin == atLocation.end() || origin->second.empty()) {
            result.rejectedMissionIds.push_back(mission.id);
            continue;
        }
        auto path = Dijkstra::findShortestPath(galaxy, mission.origin, mission.destination,
            weights, mission.cargoAmount);
        result.nodesExplored += path.nodesExplored;
        if (!path.found) {
            result.rejectedMissionIds.push_back(mission.id);
            continue;
        }

        const auto noShip = ships.size();
        size_t winner = noShip;
        double bestArrival = 0;
        double bestScore = std::numeric_limits<double>::infinity();
        PathResult bestPath;
        RouteWeights bestWeights = weights;
        auto consider = [&](const PathResult& candidate, const RouteWeights& candidateWeights) {
            if (!candidate.found)
                return;
            for (size_t i : origin->second) {
                ++result.candidates;
                const auto& ship = ships[i];
                double arrival;
                if (!feasible(mission, ship, ship.availableAt, candidate.metrics, arrival))
                    continue;
                const double score = earliestArrival ? arrival :
                    0.50 * (arrival - ship.availableAt) / (1 + mission.deadline - ship.availableAt) +
                    0.35 * (1 - mission.cargoAmount / ship.maxCargoCapacity) +
                    0.15 * candidate.metrics.fuel / ship.maxFuelCapacity;
                if (score < bestScore || (score == bestScore && arrival < bestArrival)) {
                    winner = i;
                    bestArrival = arrival;
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
                auto alternatePath = Dijkstra::findShortestPath(galaxy, mission.origin,
                    mission.destination, alternative, mission.cargoAmount);
                result.nodesExplored += alternatePath.nodesExplored;
                consider(alternatePath, alternative);
            }
        }
        if (winner == noShip) {
            result.rejectedMissionIds.push_back(mission.id);
            continue;
        }
        auto& ship = ships[winner];
        result.scheduled.push_back({mission.id, ship.id, ship.availableAt, bestArrival,
            bestPath.metrics.fuel, std::move(bestPath.path), std::move(bestPath.routeIds), bestWeights});
        origin->second.erase(winner);
        ship.currentLocation = mission.destination;
        ship.availableAt = bestArrival;
        ship.currentFuel -= bestPath.metrics.fuel;
        atLocation[ship.currentLocation].insert(winner);
    }
    if (progress)
        progress(processed);
    result.missionsScheduled = static_cast<int>(result.scheduled.size());
    return result;
}
