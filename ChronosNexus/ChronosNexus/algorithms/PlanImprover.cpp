#include "PlanImprover.h"
#include "InventoryReservations.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>

namespace {
constexpr int moveLimit = 192;
constexpr int insertionLimit = 144;
constexpr int targetLimit = 64;
constexpr size_t nearbyLimit = 3;
constexpr int expansionLimit = 256;
constexpr size_t arcLimit = 4096;
constexpr size_t labelsPerVertex = 8;
constexpr size_t pathLimit = 3;
constexpr double tolerance = 1e-8;

struct Track {
    Spacecraft tail;
    std::vector<int> missions;
    std::vector<double> fuelBefore;
};

struct Label {
    int vertex;
    RouteMetrics metrics;
    double cost;
    size_t parent;
    int edge;
    bool active = true;
};

struct Leg {
    size_t track;
    int mission;
};

bool dominates(const Label& a, const Label& b) {
    return a.metrics.fuel <= b.metrics.fuel && a.metrics.time <= b.metrics.time && a.cost <= b.cost;
}

// Bounded multi-label search. A label records both constrained resources.
std::vector<PathResult> constrainedRoutes(const GalaxyGraph& graph, const Mission& mission,
    const Spacecraft& ship, const RouteWeights& weights, SchedulingResult& stats) {
    std::vector<PathResult> result;
    if (!graph.hasLocation(mission.origin) || !graph.hasLocation(mission.destination) ||
        ship.currentLocation != mission.origin || ship.speed <= 0 ||
        ship.maxCargoCapacity < std::max(mission.cargoAmount, mission.requirements.minCargoCapacity) ||
        ship.maxFuelCapacity < mission.requirements.minFuelCapacity ||
        ship.speed < mission.requirements.minSpeed || ship.reliability < mission.requirements.minReliability ||
        ship.availableAt > mission.deadline)
        return result;
    const double timeLimit = (mission.deadline - ship.availableAt) * ship.speed;
    std::vector<Label> labels{{mission.origin, {}, 0, 0, 0}};
    std::unordered_map<int, std::vector<size_t>> frontier;
    frontier[mission.origin].push_back(0);
    using QueueItem = std::pair<double, size_t>;
    std::priority_queue<QueueItem, std::vector<QueueItem>, std::greater<QueueItem>> queue;
    queue.push({0, 0});
    int expanded = 0;
    size_t arcs = 0;
    while (!queue.empty() && expanded < expansionLimit && arcs < arcLimit && result.size() < pathLimit) {
        const auto index = queue.top().second;
        queue.pop();
        const auto current = labels[index];
        if (!current.active)
            continue;
        ++expanded;
        ++stats.nodesExplored;
        if (current.vertex == mission.destination) {
            PathResult path;
            path.found = true;
            path.metrics = current.metrics;
            path.totalCost = current.cost;
            for (auto i = index; i != 0; i = labels[i].parent) {
                path.path.push_back(labels[i].vertex);
                path.routeIds.push_back(labels[i].edge);
            }
            path.path.push_back(mission.origin);
            std::reverse(path.path.begin(), path.path.end());
            std::reverse(path.routeIds.begin(), path.routeIds.end());
            result.push_back(std::move(path));
            continue;
        }
        for (const auto& edge : graph.getRoutes(current.vertex)) {
            if (arcs == arcLimit)
                break;
            ++arcs;
            ++stats.searchArcs;
            if (!Dijkstra::usable(edge, mission.cargoAmount)) {
                ++stats.prunedArcs;
                continue;
            }
            Label next{edge.to, current.metrics, current.cost, index, edge.id};
            next.metrics.add(edge);
            next.cost += Dijkstra::edgeCost(edge, weights);
            if (!std::isfinite(next.cost) || !std::isfinite(next.metrics.distance) ||
                next.metrics.fuel > ship.currentFuel || next.metrics.time > timeLimit) {
                ++stats.prunedArcs;
                continue;
            }
            auto& at = frontier[edge.to];
            if (std::any_of(at.begin(), at.end(), [&](size_t i) { return dominates(labels[i], next); })) {
                ++stats.prunedArcs;
                continue;
            }
            const auto survivors = std::count_if(at.begin(), at.end(),
                [&](size_t i) { return !dominates(next, labels[i]); });
            if (static_cast<size_t>(survivors) >= labelsPerVertex) {
                ++stats.prunedArcs;
                continue;
            }
            std::erase_if(at, [&](size_t i) {
                if (!dominates(next, labels[i]))
                    return false;
                labels[i].active = false;
                return true;
            });
            at.push_back(labels.size());
            queue.push({next.cost, labels.size()});
            labels.push_back(next);
        }
    }
    return result;
}

double value(const Mission& mission, const Spacecraft& ship, const RouteMetrics& route) {
    return std::max(0, mission.priority) * mission.cargoAmount -
        0.01 * ship.operatingCost * route.distance - route.risk;
}
}

SchedulingResult PlanImprover::improve(SchedulingResult plan, const std::vector<Mission>& missions,
    const std::vector<Spacecraft>& ships, const GalaxyGraph& graph,
    const std::vector<ResourceDemand>& inventory, const RouteWeights& weights, double horizon, bool benefitAware) {
    const auto legValue = benefitAware ? MissionScheduler::bookingValue : value;
    for (double weight : {weights.distance, weights.fuel, weights.time, weights.risk, weights.reliability})
        if (!std::isfinite(weight) || weight < 0)
            return plan;
    if (!std::isfinite(horizon) || horizon < 0)
        return plan;
    InventoryReservations ledger(inventory);
    std::map<int, Mission> jobs;
    std::map<int, ScheduledMission> assignments;
    std::map<int, size_t> shipIndex;
    std::vector<Track> tracks;
    for (const auto& ship : ships) {
        if (!shipIndex.emplace(ship.id, tracks.size()).second)
            return plan;
        tracks.push_back({ship, {}, {}});
    }
    for (auto mission : missions) {
        if (const auto revised = plan.revisedOrigins.find(mission.id); revised != plan.revisedOrigins.end())
            mission.origin = revised->second;
        if (!jobs.emplace(mission.id, mission).second)
            return plan;
    }
    for (const auto& leg : plan.scheduled) {
        const auto& mission = jobs.at(leg.missionId);
        auto& track = tracks.at(shipIndex.at(leg.spacecraftId));
        track.missions.push_back(leg.missionId);
        track.fuelBefore.push_back(track.tail.currentFuel);
        track.tail.availableAt = leg.arrivalTime;
        track.tail.currentLocation = mission.destination;
        track.tail.currentFuel -= leg.fuelUsed;
        assignments.emplace(leg.missionId, leg);
        if (!ledger.adjust(mission.origin, mission.destination, mission.cargoType,
                leg.departureTime, leg.arrivalTime, mission.cargoAmount))
            return plan;
    }
    if (!ledger.feasible())
        return plan;

    std::set<int> originalRejected(plan.rejectedMissionIds.begin(), plan.rejectedMissionIds.end());
    std::set<int> pending = originalRejected;
    std::map<int, std::vector<int>> incoming, outgoing;
    std::vector<int> targets(pending.begin(), pending.end());
    std::erase_if(targets, [&](int id) {
        const auto& m = jobs.at(id);
        return !std::isfinite(m.deadline) || m.deadline < 0 ||
            !std::isfinite(m.cargoAmount) || m.cargoAmount <= 0 ||
            !graph.hasLocation(m.origin) || !graph.hasLocation(m.destination);
    });
    std::sort(targets.begin(), targets.end(), [&](int a, int b) {
        const auto& x = jobs.at(a);
        const auto& y = jobs.at(b);
        if (benefitAware && MissionScheduler::serviceValue(x) != MissionScheduler::serviceValue(y))
            return MissionScheduler::serviceValue(x) > MissionScheduler::serviceValue(y);
        if (x.priority != y.priority)
            return x.priority > y.priority;
        return std::tie(x.deadline, x.id) < std::tie(y.deadline, y.id);
    });
    for (int id : targets) {
        incoming[jobs.at(id).destination].push_back(id);
        if (benefitAware)
            outgoing[jobs.at(id).origin].push_back(id);
    }
    if (targets.size() > targetLimit)
        targets.resize(targetLimit);

    std::map<int, std::set<size_t>> tailLocations, terminalOrigins;
    auto indexTrack = [&](size_t i, bool insert) {
        const auto& track = tracks[i];
        auto update = [&](auto& index, int location) {
            if (insert)
                index[location].insert(i);
            else
                index.at(location).erase(i);
        };
        update(tailLocations, track.tail.currentLocation);
        if (!track.missions.empty())
            update(terminalOrigins, jobs.at(track.missions.back()).origin);
    };
    for (size_t i = 0; i < tracks.size(); ++i)
        indexTrack(i, true);

    // A move replaces only named terminal legs. Account and ship state is committed atomically.
    int activeLimit = insertionLimit;
    auto trial = [&](const std::vector<size_t>& remove, const std::vector<Leg>& add) {
        if (plan.searchTrials >= activeLimit)
            return false;
        ++plan.searchTrials;
        std::map<size_t, Spacecraft> affected;
        std::vector<InventoryReservations::Key> accounts;
        std::vector<int> removed;
        auto touch = [&](const Mission& m) {
            accounts.emplace_back(m.origin, m.cargoType);
            accounts.emplace_back(m.destination, m.cargoType);
        };
        for (size_t i : remove) {
            const auto& track = tracks[i];
            if (track.missions.empty())
                return false;
            const int id = track.missions.back();
            removed.push_back(id);
            const auto& mission = jobs.at(id);
            const auto& leg = assignments.at(id);
            auto state = track.tail;
            state.currentLocation = mission.origin;
            state.currentFuel = track.fuelBefore.back();
            state.availableAt = track.missions.size() == 1 ? ships[i].availableAt :
                assignments.at(track.missions[track.missions.size() - 2]).arrivalTime;
            affected.emplace(i, state);
            touch(mission);
        }
        for (const auto& leg : add) {
            affected.try_emplace(leg.track, tracks[leg.track].tail);
            touch(jobs.at(leg.mission));
        }
        auto candidate = ledger.subset(accounts);
        const auto before = candidate.shortages(horizon);
        double delta = 0;
        for (int id : removed) {
            const auto& mission = jobs.at(id);
            const auto& leg = assignments.at(id);
            RouteMetrics metric;
            if (!Dijkstra::measure(graph, leg.routeIds, mission.origin, mission.destination,
                mission.cargoAmount, metric) || !candidate.adjust(mission.origin, mission.destination,
                mission.cargoType, leg.departureTime, leg.arrivalTime, -mission.cargoAmount))
                return false;
            delta -= legValue(mission, ships[shipIndex.at(leg.spacecraftId)], metric);
        }

        std::vector<ScheduledMission> newLegs;
        std::vector<double> fuelBefore;
        for (const auto& leg : add) {
            auto& state = affected.at(leg.track);
            const auto& mission = jobs.at(leg.mission);
            auto routes = constrainedRoutes(graph, mission, state, weights, plan);
            bool fitted = false;
            for (auto& route : routes) {
                const double duration = route.metrics.time / state.speed;
                for (double departure : candidate.departures(mission.origin, mission.destination,
                    mission.cargoType, state.availableAt, mission.deadline - duration, duration, mission.cargoAmount)) {
                    double arrival;
                    ++plan.reservationChecks;
                    if (!MissionScheduler::feasible(mission, state, departure, route.metrics, arrival) ||
                        !candidate.reserve(mission.origin, mission.destination, mission.cargoType,
                            departure, arrival, mission.cargoAmount, true))
                        continue;
                    newLegs.push_back({mission.id, state.id, departure, arrival, route.metrics.fuel,
                        std::move(route.path), std::move(route.routeIds), weights,
                        FlightEstimate::from(route.metrics, departure, state.speed)});
                    fuelBefore.push_back(state.currentFuel);
                    state.currentLocation = mission.destination;
                    state.availableAt = arrival;
                    state.currentFuel -= route.metrics.fuel;
                    delta += legValue(mission, state, route.metrics);
                    fitted = true;
                    break;
                }
                if (fitted)
                    break;
            }
            if (!fitted)
                return false;
        }
        if (!candidate.feasible())
            return false;
        const auto after = candidate.shortages(horizon);
        const double endingChange = after.ending - before.ending;
        const double unservedChange = after.unserved - before.unserved;
        if (endingChange > tolerance || unservedChange > tolerance ||
            delta - 2 * endingChange - 2 * unservedChange <= tolerance)
            return false;

        ledger.replaceAccounts(std::move(candidate));
        for (const auto& [i, tail] : affected)
            indexTrack(i, false);
        for (size_t i : remove) {
            tracks[i].missions.pop_back();
            tracks[i].fuelBefore.pop_back();
        }
        for (int id : removed) {
            assignments.erase(id);
            pending.insert(id);
        }
        for (size_t i = 0; i < add.size(); ++i) {
            tracks[add[i].track].missions.push_back(add[i].mission);
            tracks[add[i].track].fuelBefore.push_back(fuelBefore[i]);
            pending.erase(add[i].mission);
            assignments.emplace(add[i].mission, std::move(newLegs[i]));
        }
        for (const auto& [i, tail] : affected) {
            tracks[i].tail = tail;
            indexTrack(i, true);
        }
        ++plan.acceptedMoves;
        return true;
    };

    auto atOrigin = [&](int origin, bool beforeTail, size_t exclude = static_cast<size_t>(-1)) {
        std::vector<size_t> options;
        const auto& index = beforeTail ? terminalOrigins : tailLocations;
        if (const auto at = index.find(origin); at != index.end()) {
            for (size_t i : at->second) {
                if (i != exclude)
                    options.push_back(i);
                if (options.size() == nearbyLimit)
                    break;
            }
        }
        return options;
    };

    for (int id : targets) {
        if (plan.searchTrials >= activeLimit)
            break;
        if (!pending.contains(id))
            continue;
        const auto& job = jobs.at(id);
        bool accepted = false;
        for (size_t i : atOrigin(job.origin, false))
            if ((accepted = trial({}, {{i, id}})))
                break;
        if (accepted)
            continue;

        // A rejected delivery terminating at the target's origin can enable both ship and stock.
        if (const auto source = incoming.find(job.origin); source != incoming.end()) {
            size_t inspected = 0;
            for (int parent : source->second) {
                if (parent == id || !pending.contains(parent))
                    continue;
                if (inspected++ == nearbyLimit)
                    break;
                for (size_t i : atOrigin(jobs.at(parent).origin, false))
                    if ((accepted = trial({}, {{i, parent}, {i, id}})))
                        break;
                if (accepted)
                    break;
            }
        }
        if (accepted)
            continue;

        for (size_t i : atOrigin(job.origin, true)) {
            const int incumbent = tracks[i].missions.back();
            if (const auto next = outgoing.find(job.destination); next != outgoing.end()) {
                for (int successor : next->second) {
                    if (plan.searchTrials >= activeLimit)
                        break;
                    if (successor != id && pending.contains(successor) &&
                        (accepted = trial({i}, {{i, id}, {i, successor}})))
                        break;
                }
            }
            if (accepted)
                break;
            // A compatible return leg can place a two-leg chain before the incumbent.
            if (const auto returns = incoming.find(job.origin); returns != incoming.end()) {
                size_t inspected = 0;
                for (int bridge : returns->second) {
                    if (bridge == id || !pending.contains(bridge) ||
                        jobs.at(bridge).origin != job.destination)
                        continue;
                    if (inspected++ == nearbyLimit)
                        break;
                    if ((accepted = trial({i}, {{i, id}, {i, bridge}, {i, incumbent}})))
                        break;
                }
            }
            if (accepted)
                break;
            // Try a different spacecraft for the displaced incumbent.
            for (size_t other : atOrigin(jobs.at(incumbent).origin, false, i))
                if ((accepted = trial({i}, {{i, id}, {other, incumbent}})))
                    break;
            if (accepted || trial({i}, {{i, id}}))
                break;
        }
    }

    // Inspect a fixed number of terminal legs for relocation and pairwise exchange.
    activeLimit = moveLimit;
    for (size_t i = 0, inspected = 0; i < tracks.size() && plan.searchTrials < moveLimit &&
        inspected < 24; ++i) {
        if (tracks[i].missions.empty())
            continue;
        ++inspected;
        const int id = tracks[i].missions.back();
        const auto origin = jobs.at(id).origin;
        bool accepted = false;
        for (size_t other : atOrigin(origin, false, i))
            if ((accepted = trial({i}, {{other, id}})))
                break;
        if (accepted)
            continue;
        for (size_t other : atOrigin(origin, true, i)) {
            const int second = tracks[other].missions.back();
            if (trial({i, other}, {{i, second}, {other, id}}))
                break;
        }
    }

    if (plan.acceptedMoves == 0)
        return plan;
    auto oldOrder = std::move(plan.scheduled);
    plan.scheduled.clear();
    auto collect = [&](int id) {
        if (auto entry = assignments.find(id); entry != assignments.end()) {
            plan.scheduled.push_back(std::move(entry->second));
            assignments.erase(entry);
        }
    };
    for (const auto& old : oldOrder)
        collect(old.missionId);
    for (const auto& track : tracks)
        for (int id : track.missions)
            collect(id);
    std::stable_sort(plan.scheduled.begin(), plan.scheduled.end(), [](const auto& a, const auto& b) {
        return a.departureTime < b.departureTime;
    });
    plan.rejectedMissionIds.assign(pending.begin(), pending.end());
    std::erase_if(plan.recoveredMissionIds, [&](int id) { return pending.contains(id); });
    for (int id : originalRejected)
        if (!pending.contains(id)) {
            ++plan.locallyRecovered;
            plan.recoveredMissionIds.push_back(id);
        }
    std::erase_if(plan.revisedOrigins, [&](const auto& item) { return pending.contains(item.first); });
    plan.missionsScheduled = static_cast<int>(plan.scheduled.size());
    return plan;
}
