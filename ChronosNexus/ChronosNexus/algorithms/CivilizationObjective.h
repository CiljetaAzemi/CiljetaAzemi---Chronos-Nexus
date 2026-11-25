#pragma once

#include <array>
#include <string>
#include <unordered_set>
#include "InfrastructureOptimizer.h"
#include "../simulation/SimulationEngine.h"

struct CivilizationMetrics {
    double score;
    double security;
    double criticalSecurity;
    double service;
    double infrastructure;
    double operatingPenalty;
    double wastePenalty;
    double riskPenalty;
    double criticalEndingShortage;
    double criticalUnserved;
};

class CivilizationObjective {
public:
    using Weights = std::array<double, 7>;
    static Weights parseWeights(const std::string& profile);
    CivilizationObjective(const std::vector<ResourceDemand>& inventory,
        const std::vector<Mission>& fixedInputs, const std::vector<Infrastructure>& projects,
        std::unordered_set<int> colonyIds, double horizon, double constructionBudget, Weights weights);
    CivilizationMetrics evaluate(const SimulationResult& actual,
        const InfrastructureOptimizationResult& construction) const;
    void addExternalObligation(const Mission& mission);
    size_t obligationCount() const { return obligations.size(); }

private:
    std::vector<Mission> obligations;
    std::unordered_set<int> colonies;
    Weights weights;
    double endingRef = 0, consumptionRef = 0, criticalEndingRef = 0, criticalConsumptionRef = 0;
    double cargoRef = 0, serviceRef = 0, strategicRef = 0, budget = 0;
};
