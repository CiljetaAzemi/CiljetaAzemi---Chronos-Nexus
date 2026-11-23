#include "RegressionTests.h"
#include "TestSupport.h"
#include <iostream>

int runSelfTests() {
    regression::testRoutingAndSimulation();
    regression::testAllocationAndForecast();
    regression::testInventoryAndCache();
    regression::testPlanImprovement();
    regression::testStressScenarios();
    regression::testInfrastructure();
    regression::testCivilization();
    regression::testScenarioJson();
    regression::testDynamicReplanning();
    std::cout << "Self-tests passed: route identity, capacity, fuel, overlap, reverse allocation, "
                 "depletion, inventory, production events, exact Knapsack, civilization planning, "
                 "scenario JSON, and dynamic replanning.\n";
    return 0;
}
