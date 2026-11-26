#include "TestSupport.h"
#include "../algorithms/CivilizationPlanner.h"
#include "../benchmarks/DatasetGenerator.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace regression {
void testCivilization() {
    {
        const CivilizationObjective legacy({}, {}, {}, {}, 30, 0,
            CivilizationObjective::parseWeights("balanced"));
        const auto expanded = legacy.expanded("balanced", {}, {}, {}, 30);
        const auto strict = expanded.withSecurityPriority();
        const auto balanced = expanded.withBalancedPriority();
        CivilizationMetrics cheap{}, tiny{}, substantial{}, dominated{};
        cheap.populationApplicable = tiny.populationApplicable =
            substantial.populationApplicable = dominated.populationApplicable = true;
        cheap.populationCoverage = dominated.populationCoverage = .981;
        tiny.populationCoverage = .982;
        substantial.populationCoverage = 1;
        cheap.service = tiny.service = dominated.service = .50;
        substantial.service = .30;
        cheap.score = 75;
        tiny.score = 60;
        substantial.score = 61;
        dominated.score = 70;
        require(strict.better(tiny, cheap) && balanced.better(cheap, tiny) &&
            balanced.better(substantial, cheap) && balanced.better(cheap, dominated),
            "Fixed bands must admit small trade-offs and respect a distinct coverage band.");
        const std::array outcomes{cheap, tiny, substantial, dominated};
        for (const auto& a : outcomes)
            for (const auto& b : outcomes)
                for (const auto& c : outcomes) {
                    require(!balanced.better(a, a), "Balanced ties must preserve the incumbent.");
                    if (balanced.better(a, b) && balanced.better(b, c))
                        require(balanced.better(a, c), "Balanced comparison must be transitive.");
                }
        for (const auto& outcome : outcomes)
            require(!balanced.better(outcome, substantial), "Enumeration must select the exact banded optimum.");
    }
    {
        const auto fixture = makeFlightFixture();
        benchmark::Scenario data;
        data.graph = fixture.graph;
        data.ships = {fixture.ship};
        data.missions = {fixture.first, fixture.second};
        data.inventory = {
            {fixture.first.origin, 100, 0, 1, fixture.first.cargoType, 0, 0, 100},
            {fixture.first.destination, 0, 0, 1, fixture.first.cargoType, 0, 0, 100},
            {fixture.second.destination, 0, 0, 1, fixture.second.cargoType, 0, 0, 100}};
        const CivilizationObjective legacy(data.inventory, data.missions, {}, {}, 30, 0,
            CivilizationObjective::parseWeights("balanced"));
        const auto expanded = legacy.expanded("balanced", data.inventory, {}, {}, 30);
        const std::vector<Mission> none;
        auto noAid = [&] { return none; };
        {
            const auto policy = expanded.withSecurityPriority();
            const InventoryPlanningInput input{data.inventory, {}, none};
            CivilizationMetrics exact{};
            bool first = true;
            // The sole spacecraft starts at the first source. The only feasible
            // mission sequences are empty, first, and first followed by second.
            for (const auto& sequence : {none, std::vector{fixture.first}, data.missions}) {
                const auto schedule = MissionScheduler::schedule(sequence, data.ships, data.graph, {},
                    false, {}, &input, true);
                const auto simulated = SimulationEngine::run(data.graph, data.inventory, data.missions, data.ships,
                    schedule, {}, {}, false, 30);
                require(static_cast<size_t>(simulated.completed) == sequence.size(), "The enumerated sequence must be feasible.");
                const auto metrics = expanded.evaluate(simulated, {});
                if (first || policy.better(metrics, exact))
                    exact = metrics;
                first = false;
            }
            PlanningBudget clock({20});
            const auto result = CivilizationPlanner::anytime(data, legacy, expanded, policy, {}, 0, 30,
                {}, {}, clock, false, true, false, noAid);
            require(result.forecast && result.plan.schedule.missionsScheduled == 2 && exact.service == 1 &&
                !policy.better(exact, result.plan.forecastMetrics) && !policy.better(result.plan.forecastMetrics, exact),
                "Anytime planning must reach the exact security-priority key on the two-mission chain.");
        }
        {
            PlanningBudget clock({20});
            const auto result = CivilizationPlanner::anytime(data, legacy, expanded, legacy, {}, 0, 30,
                {}, {}, clock, false, true, false, noAid);
            require(result.forecast && result.planningComplete && result.simulation &&
                result.trace.front().scheduled == 0 && result.firstUseful &&
                result.plan.schedule.scheduled.size() + result.plan.schedule.rejectedMissionIds.size() == data.missions.size(),
                "Anytime planning must return a validated incumbent, keep every obligation and separate simulation status.");
            for (size_t i = 1; i < result.trace.size(); ++i)
                require(result.trace[i].legacyScore > result.trace[i - 1].legacyScore &&
                    result.trace[i].seconds >= result.trace[i - 1].seconds,
                    "Accepted incumbent scores must increase under the fixed active objective.");
        }
        {
            PlanningBudget clock({20});
            const InventoryPlanningInput input{data.inventory, {}, none};
            const auto prefix = MissionScheduler::schedule(data.missions, data.ships, data.graph, {}, false, {},
                &input, true, [&](const SchedulingResult&) { clock.requestStop(); });
            require(prefix.interrupted && prefix.scheduled.size() == 1 &&
                prefix.missionsScheduled == 1 && std::string_view(clock.reason()) == "cancelled",
                "Interrupting after a booking must preserve the committed scheduler prefix.");
        }
        {
            PlanningBudget clock({20});
            const auto balanced = expanded.withBalancedPriority();
            const auto result = CivilizationPlanner::anytime(data, legacy, expanded, balanced, {}, 0, 30,
                {}, {}, clock, false, true, false, [&] { clock.requestStop(); PlanningBudget::checkpoint(); return none; });
            require(result.forecast && !result.planningComplete && !result.simulation && result.interrupted == 1 &&
                std::string_view(clock.exhaustedAt()) == "allocation",
                "An interrupted candidate must preserve the incumbent and must not publish a simulation.");
        }
    }
    {
        const std::vector<ResourceDemand> accounts{
            {1, 0, 0, 1, ResourceType::Food, 0, 1},
            {2, 0, 0, 1, ResourceType::Food, 0, 1},
            {3, 0, 4, 1, ResourceType::Water, 0, 1}};
        Mission request;
        request.id = 8;
        request.priority = 0;
        request.cargoAmount = 4;
        request.deadline = 3;
        std::vector<Infrastructure> projects(3);
        for (size_t i = 0; i < projects.size(); ++i) {
            projects[i].id = static_cast<int>(i);
            projects[i].locationId = 1;
            projects[i].constructionCost = 1;
            projects[i].constructionTime = 1;
            projects[i].strategicValue = i == 0 ? 10 : 0;
        }
        projects[1].development = {2, 3, 0};
        projects[2].development = {0, 0, 1};
        const CivilizationObjective legacy(accounts, {request}, projects, {1, 2}, 4, 1,
            CivilizationObjective::parseWeights("balanced"));
        const auto expanded = legacy.expanded("balanced", accounts, projects, {{1, 300}, {2, 100}}, 4);
        SimulationResult caseA;
        caseA.resourceOutcomes[1][ResourceType::Food] = {0, 0};
        caseA.resourceOutcomes[2][ResourceType::Food] = {0, 4};
        caseA.resourceOutcomes[3][ResourceType::Water] = {0, 0};
        caseA.deliveries.emplace(8, SimulationResult::Delivery{2, 3});
        const auto half = expanded.evaluate(caseA, {});
        require(half.populationCoverage == .75 && half.security == 1 && half.service == .5 && half.missionLoss == .5,
            "Population coverage must weight residents, without duplicating consumption in resource security.");
        auto caseB = caseA;
        caseB.resourceOutcomes[1][ResourceType::Food] = {0, 4};
        caseB.resourceOutcomes[2][ResourceType::Food] = {0, 0};
        require(expanded.evaluate(caseB, {}).populationCoverage == .25,
            "An equal stockout at a higher-population location must reduce the coverage proxy.");
        caseB.deliveries.at(8).amount = 4;
        const auto securityProfile = legacy.expanded("security", accounts, projects, {{1, 300}, {2, 100}}, 4);
        const auto serviceProfile = legacy.expanded("service", accounts, projects, {{1, 300}, {2, 100}}, 4);
        require(securityProfile.evaluate(caseA, {}).score > securityProfile.evaluate(caseB, {}).score &&
            serviceProfile.evaluate(caseB, {}).score > serviceProfile.evaluate(caseA, {}).score,
            "Coverage and timely fulfillment must trade off differently under the two profiles.");
        const auto policy = expanded.withSecurityPriority();
        auto emergency = caseA;
        emergency.resourceOutcomes[2][ResourceType::Food].unserved = 0;
        emergency.fuel = emergency.transportationCost = 10000;
        auto expensiveTie = emergency;
        expensiveTie.fuel *= 2;
        const std::array candidates{expanded.evaluate(caseA, {}), expanded.evaluate(caseB, {}),
            expanded.evaluate(expensiveTie, {}), expanded.evaluate(emergency, {})};
        size_t winner = 0;
        for (size_t i = 1; i < candidates.size(); ++i)
            if (policy.better(candidates[i], candidates[winner]))
                winner = i;
        require(winner == 3 && !policy.better(candidates[2], candidates[3]),
            "The exact four-outcome comparison must favor essential coverage and eliminate the fuel-dominated tie.");
        auto empty = caseA;
        empty.deliveries.clear();
        auto expensiveService = caseA;
        expensiveService.fuel = expensiveService.transportationCost = 10000;
        require(policy.better(expanded.evaluate(expensiveService, {}), expanded.evaluate(empty, {})),
            "With equal essential coverage and critical security, timely service must beat the empty plan.");
        require(!policy.better(expanded.evaluate(empty, {}), expanded.evaluate(empty, {})),
            "Unavoidable shortages must allow a feasible incumbent and exact ties must keep it.");
        caseB = caseA;
        caseB.deliveries.at(8).arrival = 4;
        caseB.deliveries.emplace(300, SimulationResult::Delivery{4, 1});
        require(expanded.evaluate(caseB, {}).missionLoss == 1,
            "Late service and unrelated aid cannot offset an unfulfilled external obligation.");
        auto dynamic = expanded;
        auto urgent = request;
        urgent.id = 9;
        dynamic.addExternalObligation(urgent);
        require(dynamic.evaluate(caseA, {}).service == .25, "An unserved external urgent request stays in the denominator.");
        for (auto member : {&SimulationResult::transportationCost, &SimulationResult::fuel,
            &SimulationResult::risk, &SimulationResult::waste}) {
            caseB = caseA;
            caseB.*member = 10;
            require(expanded.evaluate(caseB, {}).score < half.score, "Operating harms must each lower the expanded score.");
        }
        require(expanded.evaluate(caseA, {{0, 1}, 2, 10}).score == 0, "An over-budget portfolio cannot earn a score.");
        const auto science = expanded.evaluate(caseA, {{1}, 1, 0});
        const auto colony = expanded.evaluate(caseA, {{2}, 1, 0});
        require(science.development[0] == 1 && science.development[1] == 1 && colony.development[2] == 1 &&
            expanded.evaluate(caseA, {{0}, 1, 10}).score < half.score,
            "Typed incremental appraisals replace generic strategic value in the expanded score.");

        GalaxyGraph graph;
        const auto plan = CivilizationPlanner::choose({}, {request}, {}, accounts, {}, {}, graph,
            projects, expanded, {1, 2}, 1, 4, {});
        const auto forecast = SimulationEngine::run(graph, accounts, {request}, {}, {}, {}, {}, false, 4);
        double exact = expanded.evaluate(forecast, {}).score;
        for (const auto& project : projects)
            exact = std::max(exact, expanded.evaluate(forecast, {{project.id}, 1, project.strategicValue}).score);
        require(plan.construction.selectedInfrastructureIds == std::vector<int>{1} &&
            std::abs(exact - plan.forecastScore) < 1e-10, "Expanded planning must find the exact one-project optimum.");
        std::cout << "EXPANDED_EXACT," << exact << ',' << exact - plan.forecastScore << '\n';
    }
    {
        GalaxyGraph network;
        addTestRoute(network, 10, 1, 2, 6, 0, 5);
        addTestRoute(network, 11, 1, 3, 1, 0, 2);
        addTestRoute(network, 12, 4, 3, 1, 0, 2);
        addTestRoute(network, 13, 5, 4, 1, 0, 5);
        const std::vector<ResourceDemand> stock{
            {1, 5, 2, 1, ResourceType::Food, 0, 0, 5},
            {2, 0, 4, 9, ResourceType::Food, 0, 0, 4},
            {3, 0, 4, 3, ResourceType::Food, 0, 0, 4},
            {4, 4, 2, 1, ResourceType::Food, 0, 0, 5},
            {5, 2, 0, 1, ResourceType::Water, 0, 0, 5},
            {4, 0, 0, 1, ResourceType::Water, 0, 0, 5}};
        auto first = makeFlightFixture().ship;
        auto second = first;
        second.id = 2;
        second.currentLocation = 5;
        Mission reposition;
        reposition.id = 10;
        reposition.origin = 5;
        reposition.destination = 4;
        reposition.cargoType = ResourceType::Water;
        reposition.cargoAmount = 1;
        reposition.deadline = 1;
        const std::vector<Mission> input{reposition};
        const std::vector<Spacecraft> fleet{first, second};
        const CivilizationObjective objective(stock, input, {}, {2, 3}, 3, 0,
            CivilizationObjective::parseWeights("balanced"));
        CivilizationPlan original;
        original.schedule = MissionScheduler::schedule(input, fleet, network);
        original.forecastScore = objective.evaluate(SimulationEngine::run(network, stock, input, fleet,
            original.schedule, {}, {}, false, 3), {}).score;
        auto plan = original;
        const auto aid = CivilizationPlanner::coordinateAid(plan, input, stock, stock, fleet, network,
            objective, {}, 3, 11);
        auto legacy = original;
        const auto oldAid = CivilizationPlanner::coordinateAid(legacy, input, stock, stock, fleet, network,
            objective, {}, 3, 11, false);
        requireSamePlan(plan.schedule, legacy.schedule);
        auto bounded = original;
        const auto boundedAid = CivilizationPlanner::coordinateAid(bounded, input, stock, stock, fleet, network,
            objective, {}, 3, 11, true, {true, {}});
        requireSamePlan(plan.schedule, bounded.schedule);
        require(boundedAid.work.deadlinePruned > 0 && boundedAid.work.routeQueries < aid.work.routeQueries,
            "The directed time bound must skip impossible donor routes without changing assignments.");
        require(aid.retained == oldAid.retained && aid.allocation.totalDelivered == oldAid.allocation.totalDelivered &&
            plan.forecastScore == legacy.forecastScore,
            "Indexed aid must reproduce legacy assignments, including a future spacecraft.");
        require(aid.retained && aid.work.rejected > 0 && aid.work.partial > 0 && aid.missions.size() == 2 &&
            aid.allocation.totalDelivered == 4 && plan.schedule.scheduled.size() == 3 &&
            std::all_of(aid.missions.begin(), aid.missions.end(), [](const Mission& m) {
                return m.destination == 3 && m.cargoAmount == 2;
            }), "A rejected donor must remain available, and current and future carriers must split aid.");
        auto missions = input;
        missions.insert(missions.end(), aid.missions.begin(), aid.missions.end());
        Event closure;
        closure.eventType = EventType::RouteBlocked;
        closure.affectedObjectId = 12;
        const auto event = SimulationEngine::run(network, stock, missions, fleet, plan.schedule, {closure}, {}, false, 3);
        require(event.completed == 2 && event.failed == 1 && event.finalResources.at(4) == 5,
            "A closure must invalidate the affected aid without spending its reserved cargo.");
        network.blockRoute(11);
        network.blockRoute(12);
        const auto impossible = CivilizationPlanner::coordinateAid(original, input, stock, stock, fleet,
            network, objective, {}, 3, 11);
        require(!impossible.retained && impossible.missions.empty() && original.schedule.scheduled.size() == 1,
            "An infeasible allocation cannot modify the original plan.");
    }
    {
        GalaxyGraph graph;
        addTestRoute(graph, 21, 1, 2, 1, 0, 4);
        addTestRoute(graph, 22, 1, 3, 2, 0, 4);
        const std::vector<ResourceDemand> stock{
            {1, 4, 0, 1, ResourceType::Food, 0, 0, 4},
            {2, 0, 4, 5, ResourceType::Food, 0, 0, 4},
            {3, 0, 4, 5, ResourceType::Food, 0, 0, 4}};
        auto expensive = makeFlightFixture().ship;
        expensive.operatingCost = 2;
        auto cheap = expensive;
        cheap.id = 2;
        cheap.operatingCost = 1;
        const auto weights = CivilizationObjective::parseWeights("balanced");
        const CivilizationObjective objective(stock, {}, {}, {3}, 4, 0, weights);
        CivilizationPlan plan;
        plan.forecastScore = objective.evaluate(SimulationEngine::run(graph, stock, {}, {}, {}, {}, {}, false, 4), {}).score;
        const auto aid = CivilizationPlanner::coordinateAid(plan, {}, stock, stock, {expensive, cheap},
            graph, objective, {}, 4, 1);
        require(aid.retained && aid.missions.size() == 1 && aid.missions[0].destination == 3 &&
            plan.schedule.scheduled[0].spacecraftId == cheap.id,
            "Saving critical stock may justify the longer route, while otherwise equivalent ships favor lower cost.");
    }
    {
        auto [graph, mission, other, expensive] = makeFlightFixture();
        expensive.operatingCost = 10;
        auto affordable = expensive;
        affordable.id += 1;
        affordable.operatingCost = 1;
        const auto baseline = MissionScheduler::schedule({mission}, {expensive, affordable}, graph);
        const auto improved = MissionScheduler::schedule({mission}, {expensive, affordable}, graph,
            {}, false, {}, nullptr, true);
        require(baseline.scheduled.at(0).spacecraftId == expensive.id &&
            improved.scheduled.at(0).spacecraftId == affordable.id,
            "Benefit-aware greedy assignment must account for operating cost before local repair.");
    }
    GalaxyGraph graph;
    addTestRoute(graph, 1, 1, 3, 1, 1, 10);
    addTestRoute(graph, 2, 2, 3, 1, 1, 10);
    const std::vector<ResourceDemand> inventory{
        {1, 4, 4, 1, ResourceType::Food, 0, 0, 20},
        {2, 8, 0, 1, ResourceType::Food, 0, 0, 20},
        {3, 0, 4, 2, ResourceType::Food, 0, 1, 20}};
    const std::unordered_set<int> colonies{3};
    Infrastructure project;
    project.id = 15;
    project.locationId = 3;
    project.constructionCost = 1;
    project.constructionTime = 1;
    project.resourceImpact[ResourceType::Food] = 2;
    const auto weights = CivilizationObjective::parseWeights("balanced");
    const CivilizationObjective constructionObjective(inventory, {}, {project}, colonies, 5, 1, weights);
    const auto plan = CivilizationPlanner::choose({}, {}, {}, inventory, {}, {}, graph,
        {project}, constructionObjective, colonies, 1, 5, {});
    require(plan.construction.selectedInfrastructureIds == std::vector<int>{15},
        "Select the better of the two exact construction choices.");
    const auto expected = SimulationEngine::run(graph, inventory, {}, {}, {},
        CivilizationPlanner::constructionEvents({project}, plan.construction), {}, false, 5);
    const auto m = constructionObjective.evaluate(expected, plan.construction);
    require(m.criticalEndingShortage == 0 && m.criticalUnserved == 1,
        "Construction starts producing at completion; day-one unmet consumption is retained.");
    Event plannedCut;
    plannedCut.eventType = EventType::ResourceProductionDecrease;
    plannedCut.affectedObjectType = AffectedObjectType::Planet;
    plannedCut.affectedObjectId = 3;
    plannedCut.eventTime = 3;
    plannedCut.severity = 1;
    const auto withCut = CivilizationPlanner::choose({}, {}, {}, inventory, {}, {}, graph,
        {project}, constructionObjective, colonies, 1, 5, {}, {plannedCut});
    auto declaredEvents = std::vector<Event>{plannedCut};
    const auto constructionEvents = CivilizationPlanner::constructionEvents({project}, withCut.construction);
    declaredEvents.insert(declaredEvents.end(), constructionEvents.begin(), constructionEvents.end());
    const auto declaredForecast = SimulationEngine::run(graph, inventory, {}, {}, {},
        declaredEvents, {}, false, 5);
    require(withCut.forecastScore == constructionObjective.evaluate(declaredForecast, withCut.construction).score &&
        withCut.forecastScore < plan.forecastScore,
        "Announced production changes affect the nominal evaluation at their scheduled time.");

    Mission obligation;
    obligation.id = 9;
    obligation.origin = 1;
    obligation.destination = 3;
    obligation.cargoAmount = 4;
    obligation.priority = 2;
    obligation.deadline = 3;
    Spacecraft ship{};
    ship.id = 1;
    ship.currentLocation = 1;
    ship.maxCargoCapacity = 8;
    ship.maxFuelCapacity = ship.currentFuel = 8;
    ship.speed = ship.reliability = 1;
    auto alternative = ship;
    alternative.id = 2;
    alternative.currentLocation = 2;
    const std::vector<Mission> jobs{obligation};
    const std::vector<ResourceDemand> donors{inventory[0], inventory[1]};
    const InventoryPlanningInput planning{inventory, donors, jobs};
    const auto incumbent = MissionScheduler::schedule(jobs, {ship, alternative},
        graph, {}, false, {}, &planning);
    const CivilizationObjective stockObjective(inventory, {}, {}, colonies, 5, 0, weights);
    const auto supply = CivilizationPlanner::choose(incumbent, jobs, jobs, inventory, donors,
        {ship, alternative}, graph, {}, stockObjective, colonies, 0, 5, {});
    require(supply.schedule.missionsScheduled == 1 && supply.schedule.revisedOrigins.at(9) == 2,
        "The same aid obligation can be served without emptying the protected supplier.");

    const CivilizationObjective serviceObjective(inventory, jobs, {}, colonies, 5, 0, weights);
    SimulationResult delivery;
    require(serviceObjective.evaluate(delivery, {}).service == 0, "Omitting a mission must not remove its denominator.");
    delivery.deliveries.emplace(9, SimulationResult::Delivery{4, 4});
    require(serviceObjective.evaluate(delivery, {}).service == 0, "Late delivery is not timely fulfillment.");
    delivery.deliveries.at(9).arrival = 3;
    require(serviceObjective.evaluate(delivery, {}).service == 1, "On-deadline accepted cargo earns full service.");

    addTestRoute(graph, 3, 1, 4, 1, 1, 10);
    auto urgent = obligation;
    urgent.origin = 2;
    urgent.priority = 9;
    auto routine = obligation;
    routine.id = 10;
    routine.destination = 4;
    routine.priority = 1;
    const std::vector<Mission> competing{urgent, routine};
    const std::vector<ResourceDemand> vulnerable{
        {1, 8, 8, 9, ResourceType::Food, 0, 0, 20},
        {2, 8, 0, 1, ResourceType::Food, 0, 0, 20},
        {3, 0, 4, 9, ResourceType::Food, 0, 0, 20},
        {4, 0, 0, 1, ResourceType::Food, 0, 0, 20}};
    const CivilizationObjective competingObjective(vulnerable, competing, {}, {1, 3, 4}, 5, 0, weights);
    const std::vector<Spacecraft> fleet{ship, alternative};
    const auto both = MissionScheduler::schedule(competing, fleet, graph);
    const auto onlyUrgent = MissionScheduler::schedule({urgent}, fleet, graph);
    const auto all = SimulationEngine::run(graph, vulnerable, competing, fleet, both, {}, {}, false, 5);
    const auto protectedStock = SimulationEngine::run(graph, vulnerable, competing, fleet, onlyUrgent, {}, {}, false, 5);
    const auto allScore = competingObjective.evaluate(all, {});
    const auto protectedScore = competingObjective.evaluate(protectedStock, {});
    require(all.completed == 2 && protectedStock.completed == 1 && protectedScore.service == 0.9 &&
        protectedScore.criticalEndingShortage == 0 && allScore.criticalEndingShortage == 4 &&
        protectedScore.score > allScore.score,
        "Critical stock protection can outweigh a second physically feasible low-priority shipment.");
}
}
