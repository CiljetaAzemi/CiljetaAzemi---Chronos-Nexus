
#include "SimulationEngine.h"

#include <queue>
#include <unordered_set>
#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

    enum class ActionType {
        ExternalEvent = 0,
        Arrival = 1,
        Departure = 2
    };

    struct Action {
        double time;
        ActionType type;
        int index;
        int sequence;
    };

    struct ActionCompare {
        bool operator()(const Action& a, const Action& b) const {
            if (a.time != b.time)
                return a.time > b.time;

            if (a.type != b.type)
                return static_cast<int>(a.type) >
                static_cast<int>(b.type);

            return a.sequence > b.sequence;
        }
    };

    bool pathAvailable(
        const GalaxyGraph& galaxy,
        const std::vector<int>& path
    ) {
        if (path.empty())
            return false;

        for (size_t i = 1; i < path.size(); ++i) {
            int from = path[i - 1];
            int to = path[i];

            if (!galaxy.hasLocation(from))
                return false;

            bool found = false;

            for (const Route& route : galaxy.getRoutes(from)) {
                if (route.to == to && route.isAvailable) {
                    found = true;
                    break;
                }
            }

            if (!found)
                return false;
        }

        return true;
    }

} // namespace

SimulationResult SimulationEngine::run(
    const GalaxyGraph& galaxy,
    const std::vector<ResourceDemand>& planets,
    const std::vector<Mission>& missions,
    const std::vector<Spacecraft>& spacecraft,
    const SchedulingResult& schedule,
    const std::vector<Event>& events
) {
    SimulationResult result;
    result.scheduled =
        static_cast<int>(schedule.scheduled.size());

    GalaxyGraph activeGalaxy = galaxy;

    std::unordered_map<int, double> required;
    std::unordered_map<int, const Mission*> missionById;
    std::unordered_map<int, Spacecraft> shipById;
    std::unordered_set<int> disabledShips;
    std::unordered_set<int> cargoInTransit;

    for (const auto& planet : planets) {
        result.finalResources[planet.planetId] = planet.available;
        required[planet.planetId] = planet.required;
    }

    for (const auto& mission : missions) {
        missionById[mission.id] = &mission;
        result.missionStatuses[mission.id] =
            MissionStatus::Pending;
    }

    for (const auto& ship : spacecraft)
        shipById[ship.id] = ship;

    std::priority_queue<
        Action,
        std::vector<Action>,
        ActionCompare
    > queue;

    int sequence = 0;

    for (int i = 0; i < static_cast<int>(events.size()); ++i) {
        if (std::isfinite(events[i].eventTime) &&
            events[i].eventTime >= 0.0) {

            queue.push({
                events[i].eventTime,
                ActionType::ExternalEvent,
                i,
                sequence++
                });
        }
    }

    for (int i = 0;
        i < static_cast<int>(schedule.scheduled.size());
        ++i) {

        const auto& scheduled = schedule.scheduled[i];

        if (!missionById.count(scheduled.missionId) ||
            !std::isfinite(scheduled.departureTime) ||
            !std::isfinite(scheduled.arrivalTime) ||
            scheduled.departureTime < 0.0 ||
            scheduled.arrivalTime < scheduled.departureTime)
            continue;

        result.missionStatuses[scheduled.missionId] =
            MissionStatus::Scheduled;

        // Only departure is queued initially.
        queue.push({
            scheduled.departureTime,
            ActionType::Departure,
            i,
            sequence++
            });
    }

    while (!queue.empty()) {
        Action action = queue.top();
        queue.pop();

        if (action.type == ActionType::ExternalEvent) {
            const Event& event = events[action.index];
            ++result.eventsProcessed;

            switch (event.eventType) {

            case EventType::RouteBlocked:
            case EventType::WormholeCollapse:
            case EventType::PirateBlockade:
                if (event.affectedObjectType ==
                    AffectedObjectType::Route) {
                    activeGalaxy.blockRoute(
                        event.affectedObjectId
                    );
                }
                break;

            case EventType::SpacecraftDestroyed:
            case EventType::SpacecraftMechanicalFailure:
                if (event.affectedObjectType ==
                    AffectedObjectType::Spacecraft) {
                    disabledShips.insert(
                        event.affectedObjectId
                    );
                }
                break;

            default:
                ++result.unsupportedEvents;
                break;
            }

            continue;
        }

        const ScheduledMission& scheduled =
            schedule.scheduled[action.index];

        auto missionIt =
            missionById.find(scheduled.missionId);

        if (missionIt == missionById.end())
            continue;

        const Mission& mission = *missionIt->second;
        MissionStatus& status =
            result.missionStatuses[mission.id];

        if (action.type == ActionType::Departure) {

            if (status != MissionStatus::Scheduled)
                continue;

            auto shipIt =
                shipById.find(scheduled.spacecraftId);

            auto originIt =
                result.finalResources.find(mission.origin);

            bool canDepart =
                shipIt != shipById.end() &&
                !disabledShips.count(scheduled.spacecraftId) &&
                originIt != result.finalResources.end() &&
                std::isfinite(mission.cargoAmount) &&
                mission.cargoAmount > 0.0;

            if (!canDepart) {
                status = MissionStatus::Failed;
                ++result.failed;
                continue;
            }

            Spacecraft& ship = shipIt->second;

            if (originIt->second < mission.cargoAmount ||
                ship.speed <= 0.0 ||
                ship.maxCargoCapacity < mission.cargoAmount ||
                !std::isfinite(ship.currentFuel)) {

                status = MissionStatus::Failed;
                ++result.failed;
                continue;
            }

            double fuelNeeded = scheduled.fuelUsed;
            double arrivalTime = scheduled.arrivalTime;

            // Replan only if the original path is blocked.
            if (!pathAvailable(activeGalaxy, scheduled.path)) {

                PathResult alternative =
                    Dijkstra::findShortestPath(
                        activeGalaxy,
                        mission.origin,
                        mission.destination
                    );

                if (!alternative.found ||
                    !std::isfinite(alternative.totalCost)) {

                    status = MissionStatus::Failed;
                    ++result.failed;
                    continue;
                }

                fuelNeeded = alternative.totalCost;

                arrivalTime =
                    action.time + alternative.totalCost / ship.speed;

                // Alternative must still satisfy constraints.
                if (fuelNeeded > ship.currentFuel ||
                    fuelNeeded > ship.maxFuelCapacity ||
                    arrivalTime > mission.deadline) {

                    status = MissionStatus::Failed;
                    ++result.failed;
                    continue;
                }

                ++result.rerouted;
            }

            if (!std::isfinite(fuelNeeded) ||
                fuelNeeded < 0.0 ||
                fuelNeeded > ship.currentFuel ||
                !std::isfinite(arrivalTime) ||
                arrivalTime < action.time ||
                arrivalTime > mission.deadline) {

                status = MissionStatus::Failed;
                ++result.failed;
                continue;
            }

            originIt->second -= mission.cargoAmount;
            ship.currentFuel -= fuelNeeded;

            cargoInTransit.insert(mission.id);
            status = MissionStatus::InProgress;

            // Queue the ACTUAL arrival time.
            queue.push({
                arrivalTime,
                ActionType::Arrival,
                action.index,
                sequence++
                });
        }
        else if (action.type == ActionType::Arrival) {

            if (status != MissionStatus::InProgress)
                continue;

            if (disabledShips.count(scheduled.spacecraftId)) {
                status = MissionStatus::Failed;
                cargoInTransit.erase(mission.id);
                ++result.failed;
                continue;
            }

            if (!cargoInTransit.erase(mission.id))
                continue;

            result.finalResources[mission.destination] +=
                mission.cargoAmount;

            result.deliveredAmount += mission.cargoAmount;

            status = MissionStatus::Completed;
            ++result.completed;
        }
    }

    for (const auto& [planetId, demand] : required) {
        result.remainingShortage +=
            std::max(
                0.0,
                demand - result.finalResources[planetId]
            );
    }

    return result;
}
