#pragma once

#include <array>
#include <memory>
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
    double essentialUnserved = 0;
    double populationCoverage = 0;
    double missionLoss = 0;
    double transportPenalty = 0;
    double fuelPenalty = 0;
    double constructionPenalty = 0;
    double applicableWeight = 0;
    bool populationApplicable = false;
    std::array<double, 3> development{};
    std::array<bool, 3> developmentApplicable{};
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
    bool isCriticalResource(int location, ResourceType resource) const;
    CivilizationObjective expanded(const std::string& profile, const std::vector<ResourceDemand>& inventory,
        const std::vector<Infrastructure>& projects, const std::unordered_map<int, int>& population,
        double horizon) const;
    bool usesExpanded() const { return static_cast<bool>(model); }
    bool prioritizesSecurity() const { return securityPriority; }
    CivilizationObjective withSecurityPriority() const;
    CivilizationObjective withBalancedPriority(int basisPoints = 100) const;
    std::array<double, 5> balancedKey(const CivilizationMetrics& metrics) const;
    bool better(const CivilizationMetrics& candidate, const CivilizationMetrics& incumbent) const;
    double developmentPreference(const Infrastructure& project) const;

private:
    std::vector<Mission> obligations;
    std::unordered_set<int> colonies;
    Weights weights;
    double endingRef = 0, consumptionRef = 0, criticalEndingRef = 0, criticalConsumptionRef = 0;
    double cargoRef = 0, serviceRef = 0, strategicRef = 0, budget = 0;
    double largestCargo = 0;
    struct ExpandedModel;
    std::shared_ptr<const ExpandedModel> model;
    bool securityPriority = false;
    int bandBasisPoints = 0;
    CivilizationMetrics evaluateExpanded(const SimulationResult& actual,
        const InfrastructureOptimizationResult& construction, CivilizationMetrics metrics) const;
};
