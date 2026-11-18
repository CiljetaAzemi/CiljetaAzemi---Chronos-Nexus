#include <unordered_map>
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
            return a.priority != b.priority ? a.priority > b.priority : a.planetId < b.planetId;
        });

    for (auto& receiver : planets) {

        double shortage = std::max(
            0.0, receiver.required - receiver.available
        );

        if (shortage <= 0.0)
            continue;

        for (auto& supplier : planets) {

            if (supplier.planetId == receiver.planetId || supplier.type != receiver.type)
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
    std::vector<ResourceDemand> planets, const GalaxyGraph& galaxy,
    const RouteWeights& weights, const std::function<void(size_t)>& progress) {
    AllocationResult result;
    std::sort(planets.begin(), planets.end(), [](const ResourceDemand& a, const ResourceDemand& b) {
        return a.priority != b.priority ? a.priority > b.priority : a.planetId < b.planetId;
    });
    // Search backwards from a receiver. The first settled node with surplus
    // is its cheapest available supplier, even when the original graph is directed.
    auto reverse = galaxy.reversed();
    std::unordered_map<ResourceType, std::unordered_map<int, size_t>> donors;
    for (size_t i = 0; i < planets.size(); ++i)
        if (planets[i].available > planets[i].required)
            donors[planets[i].type].emplace(planets[i].planetId, i);

    size_t processed = 0;
    for (auto& receiver : planets) {
        if (progress)
            progress(processed);
        ++processed;
        auto& suppliers = donors[receiver.type];
        double shortage = receiver.required - receiver.available;
        while (shortage > 0 && !suppliers.empty()) {
            ++result.candidates;
            auto path = Dijkstra::findNearest(reverse, receiver.planetId,
                [&](int id) { return id != receiver.planetId && suppliers.contains(id); }, weights);
            result.nodesExplored += path.nodesExplored;
            if (!path.found)
                break;
            const int supplierId = path.path.back();
            auto& supplier = planets[suppliers.at(supplierId)];
            const double surplus = supplier.available - supplier.required;
            const double amount = std::min(shortage, surplus);
            if (surplus <= shortage) {
                supplier.available = supplier.required;
                suppliers.erase(supplierId);
            } else {
                supplier.available -= amount;
            }
            receiver.available += amount;
            shortage -= amount;
            std::reverse(path.path.begin(), path.path.end());
            std::reverse(path.routeIds.begin(), path.routeIds.end());
            result.transfers.push_back({supplierId, receiver.planetId, amount,
                path.totalCost * amount, std::move(path.path), std::move(path.routeIds)});
            result.totalDelivered += amount;
            result.totalTransportationCost += path.totalCost * amount;
        }
        result.remainingShortage += std::max(0.0, receiver.required - receiver.available);
    }
    return result;
}
