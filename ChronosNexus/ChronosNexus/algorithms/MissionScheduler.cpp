
#include "MissionScheduler.h"

#include <algorithm>
#include <cmath>
#include <limits>

SchedulingResult MissionScheduler::schedule(
    std::vector<Mission> missions,
    std::vector<Spacecraft> spacecraft,
    const GalaxyGraph& galaxy
) {
    SchedulingResult result;

    // Highest priority first; earliest deadline breaks ties.
    std::sort(missions.begin(), missions.end(),
        [](const Mission& a, const Mission& b) {
            if (a.priority != b.priority)
                return a.priority > b.priority;

            if (a.deadline != b.deadline)
                return a.deadline < b.deadline;

            return a.id < b.id;
        });

    for (const Mission& mission : missions) {

        // Reject invalid mission data.
        if (mission.origin < 0 ||
            mission.destination < 0 ||
            !std::isfinite(mission.cargoAmount) ||
            mission.cargoAmount <= 0.0 ||
            !std::isfinite(mission.deadline) ||
            mission.deadline < 0.0) {

            result.rejectedMissionIds.push_back(mission.id);
            continue;
        }

        // Calculate the route ONCE per mission.
        PathResult path = Dijkstra::findShortestPath(
            galaxy,
            mission.origin,
            mission.destination
        );

        if (!path.found ||
            !std::isfinite(path.totalCost) ||
            path.totalCost < 0.0) {

            result.rejectedMissionIds.push_back(mission.id);
            continue;
        }


        int bestShip = -1;
        double bestArrival = std::numeric_limits<double>::infinity();
        double bestScore = std::numeric_limits<double>::infinity();


        for (int i = 0;
            i < static_cast<int>(spacecraft.size());
            ++i) {

            const Spacecraft& ship = spacecraft[i];

            if (ship.currentLocation != mission.origin)
                continue;

            if (!std::isfinite(ship.maxCargoCapacity) ||
                !std::isfinite(ship.maxFuelCapacity) ||
                !std::isfinite(ship.currentFuel) ||
                !std::isfinite(ship.speed) ||
                !std::isfinite(ship.availableAt) ||
                !std::isfinite(ship.reliability))
                continue;

            if (ship.maxCargoCapacity < mission.cargoAmount ||
                ship.maxCargoCapacity <
                mission.requirements.minCargoCapacity)
                continue;

            if (ship.maxFuelCapacity <
                mission.requirements.minFuelCapacity)
                continue;

            if (ship.speed <= 0.0 ||
                ship.speed < mission.requirements.minSpeed)
                continue;

            if (ship.reliability <
                mission.requirements.minReliability)
                continue;

            // Baseline assumption: fuel = route distance.
            double fuelNeeded = path.totalCost;

            if (fuelNeeded > ship.currentFuel ||
                fuelNeeded > ship.maxFuelCapacity)
                continue;

            double arrival =
                ship.availableAt + path.totalCost / ship.speed;

            if (arrival > mission.deadline)
                continue;


            // Custom heuristic: balance arrival time, unused cargo
            // capacity, and fuel consumption.

            double timeScore = arrival / (1.0 + mission.deadline);

            double cargoWaste =
                (ship.maxCargoCapacity - mission.cargoAmount)
                / ship.maxCargoCapacity;

            double fuelScore =
                fuelNeeded / ship.maxFuelCapacity;

            // Lower score is better.
            double score =
                0.50 * timeScore +
                0.35 * cargoWaste +
                0.15 * fuelScore;

            if (score < bestScore ||
                (score == bestScore && arrival < bestArrival)) {
                bestScore = score;
                bestArrival = arrival;
                bestShip = i;
            }

        }

        if (bestShip == -1) {
            result.rejectedMissionIds.push_back(mission.id);
            continue;
        }

        Spacecraft& ship = spacecraft[bestShip];

        ScheduledMission scheduled{};
        scheduled.missionId = mission.id;
        scheduled.spacecraftId = ship.id;
        scheduled.departureTime = ship.availableAt;
        scheduled.arrivalTime = bestArrival;
        scheduled.fuelUsed = path.totalCost;
        scheduled.path = path.path;

        result.scheduled.push_back(scheduled);

        // Update the local ship state for future assignments.
        ship.currentLocation = mission.destination;
        ship.availableAt = bestArrival;
        ship.currentFuel -= scheduled.fuelUsed;
    }

    result.missionsScheduled =
        static_cast<int>(result.scheduled.size());

    return result;
}
