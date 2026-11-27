#include "Dijkstra.h"
#include "PlanningBudget.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>

namespace {
bool nonnegative(double value) {
    return std::isfinite(value) && value >= 0;
}
}

bool Dijkstra::usable(const Route& edge, double cargo) {
    return edge.isAvailable && nonnegative(edge.distance) &&
        nonnegative(edge.fuelConsumption) && nonnegative(edge.travelTime) &&
        nonnegative(edge.minimumTime()) && nonnegative(edge.maximumTime()) &&
        edge.minimumTime() <= edge.travelTime && edge.travelTime <= edge.maximumTime() &&
        nonnegative(edge.risk) && edge.risk <= 1 &&
        edge.reliability > 0 && edge.reliability <= 1 &&
        nonnegative(edge.maxCapacity) && cargo <= edge.maxCapacity;
}

double Dijkstra::edgeCost(const Route& edge, const RouteWeights& weights) {
    return weights.distance * edge.distance + weights.fuel * edge.fuelConsumption +
        weights.time * edge.maximumTime() + weights.risk * edge.risk +
        weights.reliability * (1 - edge.reliability);
}

void RouteMetrics::add(const Route& edge) {
    distance += edge.distance;
    fuel += edge.fuelConsumption;
    time += edge.maximumTime();
    risk += edge.risk;
    minimumTime += edge.minimumTime();
    expectedTime += edge.travelTime;
    reliability *= edge.reliability;
}

void RouteSearchWorkspace::prepare(const GalaxyGraph& source) {
    if (graph == &source && revision == source.revision())
        return;
    int highest = -1;
    bool compact = true;
    for (const auto& [id, edges] : source.adjacency()) {
        if (id < 0 || static_cast<size_t>(id) / 2 >= source.locationCount()) {
            compact = false;
            break;
        }
        highest = std::max(highest, id);
    }
    labels.assign(compact ? static_cast<size_t>(highest + 1LL) : 0, {});
    outgoing.assign(labels.size(), nullptr);
    if (!labels.empty())
        for (const auto& [id, edges] : source.adjacency())
            outgoing[id] = &edges;
    stamp = 0;
    graph = &source;
    revision = source.revision();
}

size_t RouteSearchWorkspace::bytes() const {
    return labels.capacity() * sizeof(Label) + outgoing.capacity() * sizeof(outgoing.front()) +
        queue.capacity() * sizeof(queue.front());
}

PathResult Dijkstra::findShortestPath(const GalaxyGraph& graph, int start,
    int destination, const RouteWeights& weights, double cargo, RouteSearchWorkspace* workspace) {
    if (!graph.hasLocation(destination))
        return {};
    return findNearest(graph, start, [=](int node) { return node == destination; }, weights, cargo, workspace);
}

PathResult Dijkstra::findNearest(const GalaxyGraph& graph, int start,
    const std::function<bool(int)>& isDestination, const RouteWeights& weights, double cargo,
    RouteSearchWorkspace* workspace) {
    PathResult result;
    if (!nonnegative(weights.distance) || !nonnegative(weights.fuel) ||
        !nonnegative(weights.time) || !nonnegative(weights.risk) ||
        !nonnegative(weights.reliability) || !nonnegative(cargo) ||
        !graph.hasLocation(start))
        return result;

    using Item = std::pair<double, int>;
    if (workspace)
        workspace->prepare(graph);
    const bool dense = workspace && !workspace->labels.empty();
    std::vector<Item> temporary;
    auto& queue = dense ? workspace->queue : temporary;
    queue.clear();
    std::unordered_map<int, double> distance;
    std::unordered_map<int, int> parentEdge;
    if (dense && ++workspace->stamp == 0) {
        for (auto& label : workspace->labels)
            label.stamp = 0;
        ++workspace->stamp;
    }
    const auto push = [&](double cost, int node) {
        queue.emplace_back(cost, node);
        std::push_heap(queue.begin(), queue.end(), std::greater<Item>{});
    };
    if (dense)
        workspace->labels[start] = {0, 0, workspace->stamp};
    else
        distance[start] = 0;
    push(0, start);
    int destination = start;
    while (!queue.empty()) {
        PlanningBudget::checkpoint();
        std::pop_heap(queue.begin(), queue.end(), std::greater<Item>{});
        const auto [cost, node] = queue.back();
        queue.pop_back();
        if (cost != (dense ? workspace->labels[node].cost : distance.at(node)))
            continue;
        ++result.nodesExplored;
        if (isDestination(node)) {
            destination = node;
            result.found = true;
            result.totalCost = cost;
            break;
        }
        for (const Route& edge : dense ? *workspace->outgoing[node] : graph.getRoutes(node)) {
            if ((result.edgesExplored & 127) == 0)
                PlanningBudget::checkpoint();
            ++result.edgesExplored;
            if (!usable(edge, cargo))
                continue;
            const double next = cost + edgeCost(edge, weights);
            if (dense) {
                auto& label = workspace->labels[edge.to];
                if (std::isfinite(next) && (label.stamp != workspace->stamp || next < label.cost)) {
                    label = {next, edge.id, workspace->stamp};
                    push(next, edge.to);
                }
                continue;
            }
            auto previous = distance.find(edge.to);
            if (std::isfinite(next) && (previous == distance.end() || next < previous->second)) {
                distance[edge.to] = next;
                parentEdge[edge.to] = edge.id;
                push(next, edge.to);
            }
        }
    }
    if (!result.found)
        return result;

    for (int node = destination; node != start;) {
        result.path.push_back(node);
        const int id = dense ? workspace->labels[node].parent : parentEdge.at(node);
        result.routeIds.push_back(id);
        node = graph.findRoute(id)->from;
    }
    result.path.push_back(start);
    std::reverse(result.path.begin(), result.path.end());
    std::reverse(result.routeIds.begin(), result.routeIds.end());
    result.found = measure(graph, result.routeIds, start, destination, cargo, result.metrics);
    return result;
}

TravelTimeBounds Dijkstra::reverseTimeBounds(const GalaxyGraph& reversed, int receiver) {
    TravelTimeBounds result;
    if (!reversed.hasLocation(receiver))
        return result;
    using Item = std::pair<double, int>;
    std::priority_queue<Item, std::vector<Item>, std::greater<Item>> queue;
    result.time.emplace(receiver, 0);
    queue.emplace(0, receiver);
    while (!queue.empty()) {
        PlanningBudget::checkpoint();
        const auto [distance, location] = queue.top();
        queue.pop();
        if (distance != result.time.at(location))
            continue;
        ++result.nodes;
        for (const auto& edge : reversed.getRoutes(location)) {
            if ((result.edges & 127) == 0)
                PlanningBudget::checkpoint();
            ++result.edges;
            if (!usable(edge, 0))
                continue;
            const double next = std::max(distance, std::nextafter(distance + edge.maximumTime(), 0.0));
            const auto known = result.time.find(edge.to);
            if (known == result.time.end() || next < known->second) {
                result.time[edge.to] = next;
                queue.emplace(next, edge.to);
            }
        }
    }
    // Weighted paths are simple and their forward sums can round down.
    const double margin = std::min(1.0, reversed.locationCount() * std::numeric_limits<double>::epsilon());
    for (auto& [location, time] : result.time)
        time = std::nextafter(time * (1 - margin), 0.0);
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
        metrics.add(*edge);
    }
    return current == destination && nonnegative(metrics.distance) &&
        nonnegative(metrics.fuel) && nonnegative(metrics.time) && nonnegative(metrics.risk);
}
