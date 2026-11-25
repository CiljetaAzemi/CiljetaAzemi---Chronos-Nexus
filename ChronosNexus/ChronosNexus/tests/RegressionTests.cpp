#include "RegressionTests.h"
#include "TestSupport.h"
#include <iostream>

int runSelfTests() {
    regression::testRoutingAndSimulation();
    regression::testRouteUncertainty();
    regression::testAllocationAndForecast();
    regression::testInventoryAndCache();
    regression::testPlanImprovement();
    regression::testStressScenarios();
    regression::testInfrastructure();
    regression::testCivilization();
    regression::testScenarioJson();
    regression::testDynamicReplanning();
    regression::testShortagePrediction();
    std::cout << "Self-tests passed: route identity, capacity, fuel, overlap, reverse allocation, "
                 "depletion, inventory, production events, exact Knapsack, civilization planning, "
                 "scenario JSON, dynamic replanning, and shortage prediction.\n";
    return 0;
}
