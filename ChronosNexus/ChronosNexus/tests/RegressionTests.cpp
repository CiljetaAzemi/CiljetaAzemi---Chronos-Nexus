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
    std::cout << "Self-tests passed: route identity, capacity, fuel, overlap, reverse allocation, "
                 "depletion, inventory, production events, and exact Knapsack.\n";
    return 0;
}
