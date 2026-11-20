#include "CivilizationPlanner.h"
#include "PlanImprover.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

std::vector<Event> CivilizationPlanner::constructionEvents(const std::vector<Infrastructure>& projects,
    const InfrastructureOptimizationResult& construction) {
    std::vector<Event> result;
    for (int id : construction.selectedInfrastructureIds) {
        const auto project = std::find_if(projects.begin(), projects.end(),
            [=](const Infrastructure& p) { return p.id == id; });
        if (project == projects.end())
            continue;
        // The simulator currently interprets construction as a food production change.
        const auto impact = project->resourceImpact.find(ResourceType::Food);
        if (impact == project->resourceImpact.end())
            continue;
        Event e;
        e.eventType = EventType::ConstructionCompleted;
        e.affectedObjectType = AffectedObjectType::Planet;
        e.affectedObjectId = project->locationId;
        e.eventTime = project->constructionTime;
        e.severity = impact->second;
        result.push_back(e);
    }
    return result;
}

CivilizationPlan CivilizationPlanner::choose(SchedulingResult initial, const std::vector<Mission>& obligations,
    const std::vector<Mission>& aid, const std::vector<ResourceDemand>& inventory,
    const std::vector<ResourceDemand>& aidNetwork, const std::vector<Spacecraft>& ships,
    const GalaxyGraph& graph, const std::vector<Infrastructure>& projects,
    const CivilizationObjective& objective, const std::unordered_set<int>& colonies,
    int budget, double horizon, const RouteWeights& weights,
    const std::vector<Event>& knownProductionChanges) {
    CivilizationPlan best;
    int evaluated = 0, feasible = 0;
    auto nominalEvents = [&](const InfrastructureOptimizationResult& build) {
        auto construction = constructionEvents(projects, build);
        if (knownProductionChanges.empty())
            return construction;
        std::vector<Event> events;
        // Callers supply only changes already announced at planning time.
        for (const auto& change : knownProductionChanges)
            if (change.eventType == EventType::ResourceProductionDecrease)
                events.push_back(change);
        events.insert(events.end(), construction.begin(), construction.end());
        return events;
    };
    auto consider = [&](const SchedulingResult& schedule, const InfrastructureOptimizationResult& construction) {
        ++evaluated;
        auto missions = obligations;
        for (auto& m : missions)
            if (const auto src = schedule.revisedOrigins.find(m.id); src != schedule.revisedOrigins.end())
                m.origin = src->second;
        const auto forecast = SimulationEngine::run(graph, inventory, missions, ships, schedule,
            nominalEvents(construction), {}, false, horizon);
        if (forecast.completed != schedule.missionsScheduled ||
            std::any_of(missions.begin(), missions.end(), [&](const Mission& m) {
                const auto d = forecast.deliveries.find(m.id);
                return d != forecast.deliveries.end() &&
                    (d->second.arrival > m.deadline || d->second.amount + 1e-8 < m.cargoAmount);
            }))
            return;
        ++feasible;
        const double score = objective.evaluate(forecast, construction).score;
        if (feasible == 1 || score > best.forecastScore + 1e-9) {
            best.schedule = schedule;
            best.construction = construction;
            best.forecastScore = score;
        }
    };

    const auto standard = InfrastructureOptimizer::optimize(projects, budget);
    consider(initial, standard);
    consider(initial, {});
    const auto local = PlanImprover::improve(initial, obligations, ships, graph, inventory, weights, horizon);
    consider(local, standard);

    std::map<std::pair<int, ResourceType>, const ResourceDemand*> balances;
    for (const auto& b : inventory)
        balances.emplace(std::make_pair(b.planetId, b.type), &b);

    // Revalue production where the unassisted forecast shows a deficit.
    auto targeted = projects;
    for (auto& project : targeted) {
        const auto b = balances.find({project.locationId, ResourceType::Food});
        const auto impact = project.resourceImpact.find(ResourceType::Food);
        if (b == balances.end() || impact == project.resourceImpact.end())
            continue;
        const auto& stock = *b->second;
        const double deficit = std::max(0.0, stock.required - stock.available +
            (stock.consumption - stock.production) * horizon);
        const double benefit = std::min(deficit, std::max(0.0, horizon - project.constructionTime) *
            std::max(0.0, impact->second));
        project.strategicValue += benefit * (colonies.contains(project.locationId) ? 2 : 1);
    }
    auto securityBuild = InfrastructureOptimizer::optimize(targeted, budget);
    securityBuild.totalStrategicValue = 0;
    for (const auto& p : projects)
        if (std::find(securityBuild.selectedInfrastructureIds.begin(),
                securityBuild.selectedInfrastructureIds.end(), p.id) != securityBuild.selectedInfrastructureIds.end())
            securityBuild.totalStrategicValue += p.strategicValue;

    auto securedSchedule = [&](const InfrastructureOptimizationResult& build, bool preferEarliest) {
        std::map<std::pair<int, ResourceType>, double> production;
        for (const auto& e : constructionEvents(projects, build))
            production[{e.affectedObjectId, e.resourceType}] +=
                std::max(0.0, horizon - e.eventTime) * std::max(0.0, e.severity);
        auto jobs = obligations;
        std::unordered_set<int> aidIds;
        for (const auto& m : aid)
            aidIds.insert(m.id);
        for (auto& m : jobs) {
            const auto key = std::make_pair(m.destination, m.cargoType);
            if (const auto b = balances.find(key); b != balances.end()) {
                const auto& stock = *b->second;
                const double forecast = stock.available + horizon * (stock.production - stock.consumption) -
                    stock.required + production[key];
                if (forecast < 0)
                    m.priority += colonies.contains(m.destination) ? 8 : 3;
                else if (aidIds.contains(m.id))
                    m.priority -= 4; // Keep the obligation, but defer noncritical aid in this candidate.
            }
        }

        // Compare a bounded number of alternate donors with locally available ships.
        std::unordered_map<int, std::vector<const Spacecraft*>> localShips;
        for (const auto& ship : ships)
            localShips[ship.currentLocation].push_back(&ship);
        std::set<std::pair<int, ResourceType>> donors;
        for (const auto& stock : aidNetwork)
            if (stock.available > stock.required)
                donors.emplace(stock.planetId, stock.type);
        std::vector<Mission*> proposals;
        for (auto& m : jobs)
            if (aidIds.contains(m.id))
                proposals.push_back(&m);
        std::sort(proposals.begin(), proposals.end(), [](const Mission* a, const Mission* b) {
            return std::tuple{-a->priority, a->deadline, a->id} < std::tuple{-b->priority, b->deadline, b->id};
        });
        if (proposals.size() > 24)
            proposals.resize(24);
        std::unordered_map<int, int> revised;
        if (!proposals.empty()) {
            const auto reverse = graph.reversed();
            for (auto* m : proposals) {
                const auto path = Dijkstra::findNearest(reverse, m->destination, [&](int id) {
                    if (id == m->destination || !donors.contains({id, m->cargoType}) ||
                        !localShips.contains(id))
                        return false;
                    const auto& stock = *balances.at({id, m->cargoType});
                    return stock.available - stock.required >= m->cargoAmount &&
                        std::any_of(localShips.at(id).begin(), localShips.at(id).end(),
                            [&](const Spacecraft* ship) { return ship->maxCargoCapacity >= m->cargoAmount; });
                }, weights, m->cargoAmount);
                if (path.found && path.path.back() != m->origin) {
                    m->origin = path.path.back();
                    revised.emplace(m->id, m->origin);
                }
            }
        }
        const InventoryPlanningInput planning{inventory, aidNetwork, aid};
        auto schedule = MissionScheduler::schedule(jobs, ships, graph, weights, preferEarliest, {}, &planning);
        for (const auto& entry : schedule.scheduled)
            if (const auto src = revised.find(entry.missionId); src != revised.end())
                schedule.revisedOrigins.try_emplace(entry.missionId, src->second);
        return schedule;
    };
    consider(securedSchedule(standard, false), standard);
    consider(securedSchedule(securityBuild, true), securityBuild);

    if (feasible == 0) {
        SchedulingResult empty;
        for (const auto& m : obligations)
            empty.rejectedMissionIds.push_back(m.id);
        consider(empty, {});
    }
    best.evaluated = evaluated;
    best.feasible = feasible;
    return best;
}
