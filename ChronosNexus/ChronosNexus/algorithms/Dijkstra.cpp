
#include "Dijkstra.h"

#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <algorithm>
#include <functional>
#include <utility>


PathResult Dijkstra::findShortestPath(
    const GalaxyGraph& graph,
    int start,
    int destination,
    const RouteWeights& weights
)
{
    PathResult result;


    if (weights.distance < 0 || weights.fuel < 0 ||
        weights.time < 0 || weights.risk < 0) {
        return result;
    }


    if (!graph.hasLocation(start) ||
        !graph.hasLocation(destination)) {
        return result;
    }

    using QueueItem = std::pair<double, int>;

    std::priority_queue<
        QueueItem,
        std::vector<QueueItem>,
        std::greater<QueueItem>
    > pq;

    std::unordered_map<int, double> dist;
    std::unordered_map<int, int> parent;
    std::unordered_set<int> visited;

    dist[start] = 0.0;
    pq.push({ 0.0, start });

    while (!pq.empty()) {
        auto [currentDist, current] = pq.top();
        pq.pop();

        if (!visited.insert(current).second) {
            continue;
        }

        result.nodesExplored++;

        if (current == destination) {
            break;
        }

        for (const Route& route : graph.getRoutes(current)) {

            if (!route.isAvailable ||
                route.distance < 0.0) {
                continue;
            }


            double edgeCost =
                weights.distance * route.distance +
                weights.fuel * route.fuelConsumption +
                weights.time * route.travelTime +
                weights.risk * route.risk;

            double newDist = currentDist + edgeCost;

            auto it = dist.find(route.to);

            if (it == dist.end() || newDist < it->second) {
                dist[route.to] = newDist;
                parent[route.to] = current;
                pq.push({ newDist, route.to });
            }
        }
    }

    if (!visited.count(destination)) {
        return result;
    }

    result.found = true;
    result.totalCost = dist[destination];

    int node = destination;

    while (node != start) {
        result.path.push_back(node);
        node = parent.at(node);
    }

    result.path.push_back(start);

    std::reverse(result.path.begin(), result.path.end());

    return result;
}
