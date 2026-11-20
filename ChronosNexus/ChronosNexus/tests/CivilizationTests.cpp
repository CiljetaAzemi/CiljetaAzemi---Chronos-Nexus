#include "TestSupport.h"
#include "../algorithms/CivilizationPlanner.h"

namespace regression {
void testCivilization() {
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
