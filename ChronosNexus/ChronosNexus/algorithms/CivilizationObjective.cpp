#include "CivilizationObjective.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace {
double penalty(double value, double reference) {
    return value / (value + std::max(1.0, reference));
}

bool essential(ResourceType type) {
    return type == ResourceType::Food || type == ResourceType::Medicine;
}
}

CivilizationObjective::Weights CivilizationObjective::parseWeights(const std::string& profile) {
    if (profile == "balanced") return {30, 25, 20, 5, 10, 5, 5};
    if (profile == "security") return {35, 35, 10, 5, 5, 5, 5};
    if (profile == "service") return {20, 15, 40, 5, 10, 5, 5};
    Weights result{};
    std::istringstream input(profile);
    std::string item;
    for (double& weight : result) {
        if (!std::getline(input, item, ','))
            throw std::invalid_argument("Expected seven objective weights.");
        size_t consumed = 0;
        weight = std::stod(item, &consumed);
        if (consumed != item.size() || !std::isfinite(weight) || weight < 0)
            throw std::invalid_argument("Objective weights must be finite and nonnegative.");
    }
    if (profile.back() == ',' || std::getline(input, item, ','))
        throw std::invalid_argument("Expected seven objective weights.");
    const double total = std::accumulate(result.begin(), result.end(), 0.0);
    if (!std::isfinite(total) || total <= 0)
        throw std::invalid_argument("Objective weights must have a finite positive sum.");
    for (double& weight : result)
        weight = 100 * (weight / total);
    return result;
}

CivilizationObjective::CivilizationObjective(const std::vector<ResourceDemand>& inventory,
    const std::vector<Mission>& fixedInputs, const std::vector<Infrastructure>& projects,
    std::unordered_set<int> colonyIds, double horizon, double constructionBudget, Weights coefficients)
    : colonies(std::move(colonyIds)), weights(coefficients), budget(constructionBudget) {
    for (const auto& b : inventory) {
        endingRef += b.required;
        consumptionRef += horizon * b.consumption;
        if (colonies.contains(b.planetId) && essential(b.type)) {
            criticalEndingRef += b.required;
            criticalConsumptionRef += horizon * b.consumption;
        }
    }
    obligations.reserve(fixedInputs.size());
    for (const auto& mission : fixedInputs)
        addExternalObligation(mission);
    for (const auto& project : projects)
        strategicRef += std::max(0.0, project.strategicValue);
}

void CivilizationObjective::addExternalObligation(const Mission& mission) {
    obligations.push_back(mission);
    cargoRef += mission.cargoAmount;
    serviceRef += mission.cargoAmount * std::max(0, mission.priority);
}

bool CivilizationObjective::isCriticalResource(int location, ResourceType resource) const {
    return colonies.contains(location) && essential(resource);
}

CivilizationMetrics CivilizationObjective::evaluate(const SimulationResult& actual,
    const InfrastructureOptimizationResult& construction) const {
    CivilizationMetrics m{};
    for (int id : colonies) {
        const auto it = actual.resourceOutcomes.find(id);
        if (it == actual.resourceOutcomes.end())
            continue;
        for (const auto& [type, stock] : it->second)
            if (essential(type)) {
                m.criticalEndingShortage += stock.endingShortage;
                m.criticalUnserved += stock.unserved;
            }
    }
    m.security = 1 - (penalty(actual.remainingShortage, endingRef) +
        penalty(actual.stockoutAmount, consumptionRef)) / 2;
    m.criticalSecurity = colonies.empty() ? m.security :
        1 - (penalty(m.criticalEndingShortage, criticalEndingRef) +
            penalty(m.criticalUnserved, criticalConsumptionRef)) / 2;
    double served = 0;
    for (const auto& mission : obligations) {
        const auto delivery = actual.deliveries.find(mission.id);
        if (delivery != actual.deliveries.end() && delivery->second.arrival <= mission.deadline)
            served += std::min(mission.cargoAmount, delivery->second.amount) * std::max(0, mission.priority);
    }
    m.service = serviceRef > 0 ? served / serviceRef : 0;
    m.infrastructure = strategicRef > 0 ? construction.totalStrategicValue / strategicRef : 0;
    m.operatingPenalty = (penalty(actual.transportationCost, cargoRef) +
        penalty(actual.fuel, cargoRef) + penalty(construction.totalCost, budget)) / 3;
    m.wastePenalty = penalty(actual.waste, cargoRef + consumptionRef);
    m.riskPenalty = penalty(actual.risk, static_cast<double>(obligations.size()));
    m.score = weights[0] * m.security + weights[1] * m.criticalSecurity + weights[2] * m.service +
        weights[3] * m.infrastructure - weights[4] * m.operatingPenalty -
        weights[5] * m.wastePenalty - weights[6] * m.riskPenalty;
    return m;
}
