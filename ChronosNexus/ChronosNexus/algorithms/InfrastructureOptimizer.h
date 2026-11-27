
#pragma once

#include <vector>
#include <unordered_map>
#include "../models/Infrastructure.h"

struct InfrastructureOptimizationResult {
    std::vector<int> selectedInfrastructureIds;
    double totalCost = 0.0;
    double totalStrategicValue = 0.0;
    std::unordered_map<int, double> startDays;
};

class InfrastructureOptimizer {
public:
    static InfrastructureOptimizationResult optimize(
        const std::vector<Infrastructure>& projects,
        int budget
    );
};
