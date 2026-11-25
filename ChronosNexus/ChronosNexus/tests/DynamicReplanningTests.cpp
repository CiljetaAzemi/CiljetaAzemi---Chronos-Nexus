#include "TestSupport.h"
#include "../algorithms/InventoryReservations.h"
#include "../simulation/SimulationEngine.h"

namespace regression {
void testShortagePrediction() {
    GalaxyGraph graph;
    addTestRoute(graph, 1, 1, 2, 1, 1, 4000);
    const ResourceDemand colony{2, 10000, 1000, 1, ResourceType::Food, 300, 700, 15000};
    const ResourceDemand depot{1, 6000, 2000, 1, ResourceType::Food, 0, 0, 8000};
    const std::vector<Event> noEvents;
    auto ship = makeFlightFixture().ship;
    ship.maxCargoCapacity = 4000;
    auto run = [&](const std::vector<ResourceDemand>& stock, const std::vector<Spacecraft>& fleet,
        const std::vector<Mission>& jobs = {}, const SchedulingResult& schedule = {},
        const std::vector<Event>& events = {}, const std::vector<Event>& known = {}) {
        return SimulationEngine::run(graph, stock, jobs, fleet, schedule, events, {}, false, 30, &known);
    };

    const auto unsupplied = run({colony}, {});
    require(unsupplied.shortageForecasts[0].depletionTime == 25 &&
        unsupplied.shortageForecasts[0].unserved == 2000 &&
        unsupplied.preventiveDecisions.size() == 1 && !unsupplied.preventiveDecisions[0].assignment,
        "Professor's example must deplete on day 25 even when stock starts above target.");

    const auto supplied = run({depot, colony}, {ship});
    const auto& decision = supplied.preventiveDecisions.at(0);
    require(supplied.preventiveDecisions.size() == 1 && decision.mission && decision.assignment &&
        decision.assignment->arrivalTime < 25 && !decision.after.depletionTime &&
        supplied.completed == 1 && supplied.stockoutAmount == 0 && supplied.finalResources.at(2) == 1000,
        "A successful preventive delivery must arrive before depletion and meet the projected need.");

    auto scarce = depot;
    scarce.available = 2500;
    const auto partial = run({scarce, colony}, {ship});
    require(partial.preventiveDecisions.size() == 1 && partial.preventiveDecisions[0].assignment &&
        partial.finalResources.at(1) == scarce.required && partial.stockoutAmount == 1500,
        "Partial supply must delay the shortage without drawing the donor below its target.");

    std::vector<ResourceDemand> sharedStock{colony};
    std::vector<Spacecraft> supplierFleet;
    for (const int origin : {3, 4, 5}) {
        addTestRoute(graph, origin, origin, 2, origin - 2, 1, 1000);
        auto supplier = depot;
        supplier.planetId = origin;
        supplier.available = supplier.required + 1000;
        sharedStock.push_back(supplier);
        auto carrier = ship;
        carrier.id = origin;
        carrier.currentLocation = origin;
        carrier.maxCargoCapacity = 1000;
        supplierFleet.push_back(carrier);
    }
    const auto coordinated = run(sharedStock, supplierFleet);
    require(coordinated.preventiveDecisions.size() == 3 && coordinated.completed == 3 &&
        coordinated.deliveredAmount == 3000 && coordinated.stockoutAmount == 0 &&
        coordinated.finalResources.at(2) == 1000,
        "Three suppliers must cover the 3000-unit need without extra deliveries.");
    const std::vector<double> remainingNeed{3000, 2000, 1000, 0};
    for (size_t i = 0; i < coordinated.preventiveDecisions.size(); ++i) {
        const auto& step = coordinated.preventiveDecisions[i];
        require(step.mission && step.assignment && step.mission->origin == supplierFleet[i].currentLocation &&
            step.mission->cargoAmount == 1000 && step.assignment->arrivalTime < 25 &&
            step.before.unserved + step.before.endingShortage == remainingNeed[i] &&
            step.after.unserved + step.after.endingShortage == remainingNeed[i + 1] &&
            coordinated.finalResources.at(step.mission->origin) == depot.required,
            "Each delivery must beat the initial depletion and reduce the projected need by 1000.");
    }
    auto limitedStock = sharedStock;
    limitedStock.back().available = depot.required - 1;
    limitedStock[2].available -= 500;
    const auto insufficient = run(limitedStock, supplierFleet);
    require(insufficient.completed == 2 && insufficient.preventiveDecisions.size() == 2 &&
        insufficient.deliveredAmount == 1500 && insufficient.stockoutAmount == 500 &&
        insufficient.preventiveDecisions.back().after.depletionTime == 28.75 &&
        insufficient.finalResources.at(5) == depot.required - 1,
        "Insufficient combined supply must leave a measured residual and preserve donor stock.");

    auto noEndingTarget = sharedStock;
    noEndingTarget.front().required = 0;
    const auto demandCovered = run(noEndingTarget, supplierFleet);
    require(demandCovered.completed == 2 && demandCovered.preventiveDecisions.size() == 2 &&
        demandCovered.stockoutAmount == 0,
        "The last supplier is unnecessary when existing deliveries cover all demand through the horizon.");

    auto unavailableFleet = supplierFleet;
    for (auto& carrier : unavailableFleet)
        carrier.availableAt = 25;
    const auto unavailable = run(sharedStock, unavailableFleet);
    require(unavailable.completed == 0 && unavailable.scheduled == 0 &&
        unavailable.preventiveDecisions.size() == 1 && !unavailable.preventiveDecisions[0].assignment &&
        unavailable.stockoutAmount == 2000,
        "Supply cannot be dispatched when every spacecraft becomes available too late.");

    auto manyColony = colony;
    manyColony.required = 1200;
    std::vector<ResourceDemand> manySuppliers{manyColony};
    std::vector<Spacecraft> manyCarriers;
    for (int origin = 100; origin < 132; ++origin) {
        addTestRoute(graph, origin, origin, 2, 1, 1, 100);
        auto supplier = depot;
        supplier.planetId = origin;
        supplier.available = supplier.required + 100;
        manySuppliers.push_back(supplier);
        auto carrier = ship;
        carrier.id = origin;
        carrier.currentLocation = origin;
        carrier.maxCargoCapacity = 100;
        manyCarriers.push_back(carrier);
    }
    const auto many = run(manySuppliers, manyCarriers);
    require(many.completed == 32 && many.preventiveDecisions.size() == 32 &&
        many.deliveredAmount == 3200 && many.stockoutAmount == 0 &&
        many.finalResources.at(2) == 1200 && many.nodesExplored <= 32 * 6,
        "All 32 suppliers must contribute, with one suite of route searches per supplier.");

    GalaxyGraph singleSupplierGraph;
    addTestRoute(singleSupplierGraph, 200, 1, 2, 1, 1, 1000);
    std::vector<Spacecraft> depotFleet{ship, ship, ship};
    for (size_t i = 0; i < depotFleet.size(); ++i)
        depotFleet[i].id = static_cast<int>(i + 20);
    const auto repeated = SimulationEngine::run(singleSupplierGraph, {depot, colony}, {}, depotFleet,
        {}, {}, {}, false, 30, &noEvents);
    require(repeated.completed == 3 && repeated.preventiveDecisions.size() == 3 &&
        repeated.deliveredAmount == 3000 && repeated.stockoutAmount == 0 &&
        repeated.finalResources.at(1) == 3000,
        "The same supplier must be reconsidered while distinct carriers can deliver more.");
    for (size_t i = 0; i < repeated.preventiveDecisions.size(); ++i) {
        const auto& step = repeated.preventiveDecisions[i];
        require(step.mission && step.assignment && step.mission->origin == 1 &&
            step.mission->cargoAmount == 1000 && step.assignment->spacecraftId == depotFleet[i].id &&
            step.assignment->arrivalTime < 25,
            "Repeat shipments must use available spacecraft and respect the route's 1000-unit capacity.");
    }

    Mission shipment;
    shipment.id = 7;
    shipment.origin = 1;
    shipment.destination = 2;
    shipment.cargoAmount = 3000;
    shipment.deadline = 10;
    SchedulingResult fixed;
    fixed.scheduled.push_back({7, ship.id, 3, 4, 1, {1, 2}, {1}, {}});
    const auto covered = run({depot, colony}, {ship}, {shipment}, fixed);
    require(covered.preventiveDecisions.empty() && covered.completed == 1 && covered.stockoutAmount == 0,
        "A confirmed scheduled delivery must suppress a duplicate preventive request.");

    InventoryReservations ledger({colony});
    ledger.addPlannedDeparture(2, ResourceType::Food, 5, 2000);
    require(ledger.project(2, ResourceType::Food, 30).depletion == 20,
        "A planned withdrawal must advance the depletion forecast.");

    Event project;
    project.eventTime = 20;
    project.eventType = EventType::ConstructionCompleted;
    project.affectedObjectId = 2;
    project.severity = 400;
    const auto protectedByBuild = run({depot, colony}, {ship}, {}, {}, {project}, {project});
    require(protectedByBuild.preventiveDecisions.empty() && !protectedByBuild.shortageForecasts[1].depletionTime &&
        protectedByBuild.shortageForecasts[1].endingStock == 2000,
        "A known construction effect must contribute from its completion time.");

    auto stable = colony;
    stable.available = 3000;
    stable.production = stable.consumption = 600;
    Event shock = project;
    shock.id = 42;
    shock.eventType = EventType::ResourceProductionDecrease;
    shock.eventTime = 2;
    shock.severity = 0.5;
    const auto disrupted = run({depot, stable}, {ship}, {}, {}, {shock}, noEvents);
    require(disrupted.shortageForecasts.size() == 3 &&
        !disrupted.shortageForecasts[1].depletionTime && disrupted.shortageForecasts[2].depletionTime == 12 &&
        disrupted.preventiveDecisions.size() == 1 && disrupted.preventiveDecisions[0].assignment &&
        disrupted.replanningDecisions[0].eventId == 42 && disrupted.replanningDecisions[0].eventTime == 2,
        "A surprise production cut must refresh the affected account and trigger targeted replanning.");
}

void testDynamicReplanning() {
    using Reason = SimulationResult::ReplanningDecision::Reason;
    auto [graph, first, second, ship] = makeFlightFixture();
    auto backup = ship;
    backup.id = 2;
    auto other = ship;
    other.id = 3;
    other.currentLocation = 2;
    const std::vector<ResourceDemand> stock{{1, 12, 0, 1}, {2, 12, 0, 1}, {3, 0, 0, 1}};
    SchedulingResult plan;
    plan.scheduled = {{first.id, ship.id, 2, 3, 2, {1, 2}, {1}, {}},
        {second.id, other.id, 1, 2, 5, {2, 3}, {3}, {}}};

    Event failure;
    failure.id = 40;
    failure.eventTime = 1.5;
    failure.eventType = EventType::SpacecraftMechanicalFailure;
    failure.affectedObjectId = ship.id;
    const auto reassigned = SimulationEngine::run(graph, stock, {first, second}, {ship, backup, other}, plan, {failure});
    require(reassigned.completed == 2 && reassigned.replanningDecisions.size() == 1 &&
        reassigned.deliveries.at(second.id).arrival == 2 &&
        reassigned.replanningDecisions[0].previous->spacecraftId == ship.id &&
        reassigned.replanningDecisions[0].replacement->spacecraftId == backup.id,
        "Repair must replace the disabled ship and preserve the unrelated in-flight mission.");

    backup.currentFuel = 1;
    const auto stranded = SimulationEngine::run(graph, stock, {first, second}, {ship, backup, other}, plan, {failure});
    require(stranded.completed == 1 && stranded.failed == 1 && stranded.replanningDecisions.size() == 1 &&
        stranded.replanningDecisions[0].reason == Reason::UnavailableSpacecraft &&
        !stranded.replanningDecisions[0].replacement, "A replacement must have sufficient fuel.");

    Event closure = failure;
    closure.id = 41;
    closure.eventType = EventType::RouteBlocked;
    closure.affectedObjectId = 1;
    const auto rerouted = SimulationEngine::run(graph, stock, {first, second}, {ship, other}, plan, {closure});
    require(rerouted.completed == 2 && rerouted.replanningDecisions.size() == 1 &&
        rerouted.replanningDecisions[0].previous->routeIds == std::vector<int>{1} &&
        rerouted.replanningDecisions[0].replacement->routeIds == std::vector<int>{2},
        "A blocked edge must invalidate and replace the exact pending route.");
    first.deadline = 4;
    const auto unreachable = SimulationEngine::run(graph, stock, {first, second}, {ship, other}, plan, {closure});
    require(unreachable.completed == 1 && unreachable.failed == 1 && unreachable.replanningDecisions.size() == 1 &&
        !unreachable.replanningDecisions[0].replacement,
        "A detour that misses the deadline cannot recover a closure.");

    auto chainFirst = first;
    chainFirst.deadline = 5;
    auto chainSecond = second;
    chainSecond.deadline = 6;
    const auto chainPlan = MissionScheduler::schedule({chainFirst, chainSecond}, {ship}, graph);
    auto dependentStock = stock;
    dependentStock[1].available = 0;
    closure.eventTime = 0;
    const auto delayed = SimulationEngine::run(graph, dependentStock, {chainFirst, chainSecond}, {ship}, chainPlan, {closure});
    require(delayed.completed == 2 && delayed.replanningDecisions.size() == 2 &&
        delayed.replanningDecisions[1].reason == Reason::DependentAssignment &&
        delayed.replanningDecisions[1].replacement->departureTime == 4 && delayed.inventoryFailures == 0,
        "Rerouting must repair a successor that depends on the delayed ship and delivery.");

    plan.scheduled.resize(1);
    backup.currentFuel = 8;
    Mission urgent = first;
    urgent.id = 7;
    urgent.deadline = 5;
    Event request;
    request.id = 42;
    request.eventTime = 1;
    request.eventType = EventType::HighPriorityMission;
    request.affectedObjectId = urgent.id;
    request.urgentMission = urgent;
    const auto accepted = SimulationEngine::run(graph, stock, {first}, {ship, backup}, plan, {request});
    require(accepted.completed == 2 && accepted.deliveries.at(first.id).arrival == 3 &&
        accepted.replanningDecisions.size() == 1 &&
        !accepted.replanningDecisions[0].previous &&
        accepted.replanningDecisions[0].replacement->spacecraftId == backup.id,
        "An urgent arrival must use spare capacity without moving the fixed booking.");

    auto scarce = stock;
    scarce[0].available = 6;
    const auto declined = SimulationEngine::run(graph, scarce, {first}, {ship, backup}, plan, {request});
    require(declined.completed == 1 && declined.failed == 1 && declined.inventoryFailures == 0 &&
        declined.deliveries.at(first.id).arrival == 3 && !declined.replanningDecisions[0].replacement,
        "An urgent mission cannot spend stock reserved for a pending departure.");

    plan.scheduled[0].departureTime = 0;
    plan.scheduled[0].arrivalTime = 1;
    urgent = second;
    urgent.id = 8;
    request.affectedObjectId = urgent.id;
    request.eventTime = 0.5;
    request.urgentMission = urgent;
    scarce[1].available = 0;
    const auto chained = SimulationEngine::run(graph, scarce, {first}, {ship}, plan, {request});
    require(chained.completed == 2 && chained.replanningDecisions[0].replacement &&
        chained.replanningDecisions[0].replacement->departureTime == 1,
        "An urgent repair can reserve a confirmed in-flight delivery and its arriving ship.");

    closure.eventTime = 1.5;
    const auto completed = SimulationEngine::run(graph, stock, {first}, {ship}, plan, {closure});
    require(completed.completed == 1 && completed.replanningDecisions.empty() &&
        completed.deliveries.at(first.id).arrival == 1, "A completed route must not be replanned.");
}
}
