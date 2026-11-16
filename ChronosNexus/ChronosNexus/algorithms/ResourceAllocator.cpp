#include "ResourceAllocator.h"
#include <limits>

#include <algorithm>
#include <cmath>

AllocationResult ResourceAllocator::allocate(
    std::vector<ResourceDemand> planets
) {
    AllocationResult result;

    // Prioritize planets with greater urgency
    std::sort(planets.begin(), planets.end(),
        [](const ResourceDemand& a, const ResourceDemand& b) {
            return a.priority > b.priority;
        });

    for (auto& receiver : planets) {

        double shortage = std::max(
            0.0, receiver.required - receiver.available
        );

        if (shortage <= 0.0)
            continue;

        for (auto& supplier : planets) {

            if (supplier.planetId == receiver.planetId)
                continue;

            double surplus = std::max(
                0.0, supplier.available - supplier.required
            );

            if (surplus <= 0.0)
                continue;

            double amount = std::min(shortage, surplus);

            supplier.available -= amount;
            receiver.available += amount;
            shortage -= amount;

            result.transfers.push_back({
                supplier.planetId,
                receiver.planetId,
                amount
                });

            result.totalDelivered += amount;

            if (shortage <= 0.0)
                break;
        }
    }

    // Calculate remaining shortages
    for (const auto& planet : planets) {
        result.remainingShortage += std::max(
            0.0, planet.required - planet.available
        );
    }

    return result;
}



AllocationResult ResourceAllocator::allocateWithRoutes(
    std::vector<ResourceDemand> planets,
    const GalaxyGraph& galaxy,
    const RouteWeights& weights
) {
    AllocationResult result;

    // Process highest-priority planets first
    std::sort(planets.begin(), planets.end(),
        [](const ResourceDemand& a, const ResourceDemand& b) {
            return a.priority > b.priority;
        });

    for (auto& receiver : planets) {

        double shortage = std::max(
            0.0, receiver.required - receiver.available
        );

        while (shortage > 0.0) {

            int bestSupplier = -1;
            double bestCost = std::numeric_limits<double>::infinity();
            PathResult bestPath;

            // Find cheapest reachable supplier
            for (int i = 0; i < static_cast<int>(planets.size()); i++) {

                auto& supplier = planets[i];

                if (supplier.planetId == receiver.planetId)
                    continue;

                double surplus = std::max(
                    0.0, supplier.available - supplier.required
                );

                if (surplus <= 0.0)
                    continue;

                PathResult path = Dijkstra::findShortestPath(
                    galaxy,
                    supplier.planetId,
                    receiver.planetId,
                    weights
                );

                if (path.found && path.totalCost < bestCost) {
                    bestCost = path.totalCost;
                    bestSupplier = i;
                    bestPath = path;
                }
            }

            if (bestSupplier == -1)
                break;

            auto& supplier = planets[bestSupplier];

            double surplus = std::max(
                0.0, supplier.available - supplier.required
            );

            double amount = std::min(shortage, surplus);

            supplier.available -= amount;
            receiver.available += amount;
            shortage -= amount;

            ResourceTransfer transfer;
            transfer.fromPlanet = supplier.planetId;
            transfer.toPlanet = receiver.planetId;
            transfer.amount = amount;
            transfer.transportationCost = bestCost * amount;
            transfer.path = bestPath.path;

            result.transfers.push_back(transfer);
            result.totalDelivered += amount;
            result.totalTransportationCost += transfer.transportationCost;
        }
    }

    for (const auto& planet : planets) {
        result.remainingShortage += std::max(
            0.0, planet.required - planet.available
        );
    }

    return result;
}
