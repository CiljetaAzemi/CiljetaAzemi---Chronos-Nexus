#include "Dijkstra.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <unordered_map>

namespace {
bool nonnegative(double value) {
    return std::isfinite(value) && value >= 0;
}

bool usable(const Route& edge, double cargo) {
    return edge.isAvailable && nonnegative(edge.distance) &&
        nonnegative(edge.fuelConsumption) && nonnegative(edge.travelTime) &&
        nonnegative(edge.risk) && edge.risk <= 1 &&
        nonnegative(edge.reliability) && edge.reliability <= 1 &&
        nonnegative(edge.maxCapacity) && cargo <= edge.maxCapacity;
}
}

PathResult Dijkstra::findShortestPath(const GalaxyGraph& graph, int start,
    int destination, const RouteWeights& weights, double cargo) {
    if (!graph.hasLocation(destination))
        return {};
    return findNearest(graph, start, [=](int node) { return node == destination; }, weights, cargo);
}

PathResult Dijkstra::findNearest(const GalaxyGraph& graph, int start,
    const std::function<bool(int)>& isDestination, const RouteWeights& weights, double cargo) {
    PathResult result;
    if (!nonnegative(weights.distance) || !nonnegative(weights.fuel) ||
        !nonnegative(weights.time) || !nonnegative(weights.risk) ||
        !nonnegative(weights.reliability) || !nonnegative(cargo) ||
        !graph.hasLocation(start))
        return result;

    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    std::unordered_map<int, double> distance;
    std::unordered_map<int, int> parentEdge;
    distance[start] = 0;
    queue.push({0, start});
    int destination = start;
    while (!queue.empty()) {
        auto [cost, node] = queue.top();
        queue.pop();
        if (cost != distance.at(node))
            continue;
        ++result.nodesExplored;
        if (isDestination(node)) {
            destination = node;
            result.found = true;
            result.totalCost = cost;
            break;
        }
        for (const Route& edge : graph.getRoutes(node)) {
            if (!usable(edge, cargo))
                continue;
            const double next = cost + weights.distance * edge.distance +
                weights.fuel * edge.fuelConsumption + weights.time * edge.travelTime +
                weights.risk * edge.risk + weights.reliability * (1 - edge.reliability);
            auto previous = distance.find(edge.to);
            if (std::isfinite(next) && (previous == distance.end() || next < previous->second)) {
                distance[edge.to] = next;
                parentEdge[edge.to] = edge.id;
                queue.push({next, edge.to});
            }
        }
    }
    if (!result.found)
        return result;

    for (int node = destination; node != start;) {
        result.path.push_back(node);
        const int id = parentEdge.at(node);
        result.routeIds.push_back(id);
        node = graph.findRoute(id)->from;
    }
    result.path.push_back(start);
    std::reverse(result.path.begin(), result.path.end());
    std::reverse(result.routeIds.begin(), result.routeIds.end());
    result.found = measure(graph, result.routeIds, start, destination, cargo, result.metrics);
    return result;
}

bool Dijkstra::measure(const GalaxyGraph& graph, const std::vector<int>& routeIds,
    int origin, int destination, double cargo, RouteMetrics& metrics) {
    metrics = {};
    int current = origin;
    for (int id : routeIds) {
        const auto* edge = graph.findRoute(id);
        if (!edge || edge->from != current || !usable(*edge, cargo))
            return false;
        current = edge->to;
        metrics.distance += edge->distance;
        metrics.fuel += edge->fuelConsumption;
        metrics.time += edge->travelTime;
        metrics.risk += edge->risk;
    }
    return current == destination && nonnegative(metrics.distance) &&
        nonnegative(metrics.fuel) && nonnegative(metrics.time) && nonnegative(metrics.risk);
}
