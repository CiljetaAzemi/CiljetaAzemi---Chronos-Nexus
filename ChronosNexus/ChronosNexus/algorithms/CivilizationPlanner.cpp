#include "CivilizationPlanner.h"
#include "PlanImprover.h"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
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
    if (objective.usesExpanded()) {
        auto appraised = projects;
        for (auto& project : appraised)
            project.strategicValue = objective.developmentPreference(project);
        auto development = InfrastructureOptimizer::optimize(appraised, budget);
        const std::unordered_set<int> selected(development.selectedInfrastructureIds.begin(),
            development.selectedInfrastructureIds.end());
        development.totalStrategicValue = 0;
        for (const auto& project : projects)
            if (selected.contains(project.id))
                development.totalStrategicValue += project.strategicValue;
        if (!selected.empty() && development.selectedInfrastructureIds != standard.selectedInfrastructureIds)
            consider(initial, development);
    }
    if (std::any_of(obligations.begin(), obligations.end(), [](const Mission& mission) { return mission.importance > 0; })) {
        const InventoryPlanningInput priorityInventory{inventory, aidNetwork, aid};
        auto priorityPlan = MissionScheduler::schedule(obligations, ships, graph, weights, false, {}, &priorityInventory);
        consider(priorityPlan, standard);
    }
    const auto local = PlanImprover::improve(initial, obligations, ships, graph, inventory, weights, horizon, true);
    consider(local, standard);

    std::map<std::pair<int, ResourceType>, const ResourceDemand*> balances;
    for (const auto& b : inventory)
        balances.emplace(std::make_pair(b.planetId, b.type), &b);

    // Revalue production where the unassisted forecast shows a deficit.
    auto targeted = projects;
    for (auto& project : targeted) {
        project.strategicValue = objective.developmentPreference(project);
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
        auto schedule = MissionScheduler::schedule(jobs, ships, graph, weights, preferEarliest, {}, &planning, true);
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
CoordinatedAid CivilizationPlanner::coordinateAid(CivilizationPlan& plan, const std::vector<Mission>& obligations,
    const std::vector<ResourceDemand>& accounts, const std::vector<ResourceDemand>& network,
    const std::vector<Spacecraft>& ships, const GalaxyGraph& graph, const CivilizationObjective& objective,
    const std::vector<Event>& knownSupply, double horizon, int firstAidId, bool indexedAid,
    const AidRoutingOptions& aidRouting) {
    CoordinatedAid aid;
    if (network.empty())
        return aid;
    const SimulationObjective scoring{objective, plan.construction};
    auto started = std::chrono::steady_clock::now();
    const auto trial = SimulationEngine::run(graph, accounts, obligations, ships, plan.schedule,
        {}, {}, false, horizon, &knownSupply, &scoring, &network, indexedAid, aidRouting);
    aid.coordinationSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    aid.work = trial.allocationWork;
    aid.allocation.nodesExplored = trial.nodesExplored;
    aid.allocation.candidates = trial.allocationWork.routeQueries;
    auto candidate = plan.schedule;
    auto missions = obligations;
    for (const auto& proposal : trial.preventiveDecisions) {
        if (!proposal.assignment || !proposal.mission)
            continue;
        if (firstAidId == std::numeric_limits<int>::max())
            throw std::invalid_argument("Not enough mission IDs for coordinated aid.");
        auto assignment = *proposal.assignment;
        auto mission = *proposal.mission;
        assignment.missionId = mission.id = firstAidId++;
        missions.push_back(mission);
        aid.missions.push_back(mission);
        candidate.scheduled.push_back(assignment);
        ++candidate.missionsScheduled;
        double routeCost = 0;
        for (int edge : assignment.routeIds)
            routeCost += Dijkstra::edgeCost(*graph.findRoute(edge), assignment.weights);
        aid.allocation.transfers.push_back({mission.origin, mission.destination, mission.cargoAmount,
            routeCost * mission.cargoAmount, assignment.path, assignment.routeIds});
        aid.allocation.totalDelivered += mission.cargoAmount;
    }
    if (aid.missions.empty())
        return aid;
    ++plan.evaluated;
    started = std::chrono::steady_clock::now();
    const auto forecast = SimulationEngine::run(graph, accounts, missions, ships, candidate,
        knownSupply, {}, false, horizon);
    aid.replaySeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::unordered_map<int, const Mission*> byId;
    for (const auto& job : missions)
        byId.emplace(job.id, &job);
    const bool fullDeliveries = std::all_of(candidate.scheduled.begin(), candidate.scheduled.end(), [&](const auto& row) {
        const auto delivery = forecast.deliveries.find(row.missionId);
        if (delivery == forecast.deliveries.end())
            return false;
        const auto& job = *byId.at(row.missionId);
        return delivery->second.arrival <= job.deadline && delivery->second.amount + 1e-8 >= job.cargoAmount;
    });
    if (fullDeliveries) {
        ++plan.feasible;
        const double score = objective.evaluate(forecast, plan.construction).score;
        if (score > plan.forecastScore + 1e-9) {
            plan.schedule = std::move(candidate);
            plan.forecastScore = score;
            aid.retained = true;
            return aid;
        }
    }
    aid.missions.clear();
    aid.allocation.transfers.clear();
    aid.allocation.totalDelivered = 0;
    return aid;
}
