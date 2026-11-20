#include "TestSupport.h"
#include "../algorithms/InfrastructureOptimizer.h"
#include <algorithm>

namespace regression {
void testInfrastructure() {
    std::vector<Infrastructure> projects(4);
    const int costs[] = {40, 50, 60, 30};
    const int values[] = {60, 80, 90, 45};
    for (int i = 0; i < 4; ++i) {
        projects[i].id = i;
        projects[i].constructionCost = costs[i];
        projects[i].strategicValue = values[i];
    }
    int exact = 0;
    for (int mask = 0; mask < 16; ++mask) {
        int cost = 0;
        int value = 0;
        for (int i = 0; i < 4; ++i)
            if (mask & (1 << i)) {
                cost += costs[i];
                value += values[i];
            }
        if (cost <= 100)
            exact = std::max(exact, value);
    }
    const auto dp = InfrastructureOptimizer::optimize(projects, 100);
    require(dp.totalStrategicValue == exact && dp.totalCost <= 100,
        "Knapsack must equal exhaustive subset enumeration.");
}
}
