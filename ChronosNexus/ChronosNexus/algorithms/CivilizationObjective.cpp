#include "CivilizationObjective.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace {
double penalty(double value, double reference) {
    return value / (value + std::max(1.0, reference));
}

bool essential(ResourceType type) {
    return type == ResourceType::Food || type == ResourceType::Medicine;
}
double boundedLoss(double value, double reference) {
    if (std::isnan(value) || value == std::numeric_limits<double>::infinity())
        return 1;
    if (!(value > 0))
        return 0;
    const double baseline = std::max(1.0, reference);
    return value > baseline ? 1 / (1 + baseline / value) : (value / baseline) / (1 + value / baseline);
}
double fraction(double value, double reference) {
    if (!(value >= 0) || (std::isinf(value) && std::isinf(reference)))
        return 1;
    return std::clamp(value / reference, 0.0, 1.0);
}
}

struct CivilizationObjective::ExpandedModel {
    struct Account {
        int location;
        ResourceType resource;
        double target;
        double demand;
        double priority;
        double populationWeight;
    };
    std::vector<Account> accounts;
    std::unordered_map<int, Infrastructure> projects;
    std::array<double, 3> reference{};
    std::array<double, 3> scale{};
    std::array<double, 10> weights{};
    double horizon = 0;
};

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
    largestCargo = std::max(largestCargo, mission.cargoAmount);
    serviceRef += mission.cargoAmount * std::max(0, mission.priority);
}

bool CivilizationObjective::isCriticalResource(int location, ResourceType resource) const {
    return colonies.contains(location) && essential(resource);
}

CivilizationObjective CivilizationObjective::expanded(const std::string& profile,
    const std::vector<ResourceDemand>& inventory, const std::vector<Infrastructure>& projects,
    const std::unordered_map<int, int>& population, double horizon) const {
    auto result = *this;
    auto built = std::make_shared<ExpandedModel>();
    if (profile == "balanced")
        built->weights = {20, 25, 25, 4, 4, 4, 5, 5, 4, 4};
    else if (profile == "security")
        built->weights = {25, 35, 10, 4, 4, 4, 5, 5, 4, 4};
    else if (profile == "service")
        built->weights = {15, 15, 40, 4, 4, 4, 5, 5, 4, 4};
    else
        throw std::invalid_argument("Expanded objective profiles are balanced, security, and service.");
    built->horizon = horizon;
    std::unordered_map<int, int> essentials;
    for (const auto& account : inventory)
        if (essential(account.type) && account.consumption > 0 && horizon > 0)
            ++essentials[account.planetId];
    for (const auto& account : inventory) {
        double populationWeight = 0;
        const auto people = population.find(account.planetId);
        if (essential(account.type) && account.consumption > 0 && horizon > 0 &&
            people != population.end() && people->second > 0)
            populationWeight = static_cast<double>(people->second) / essentials.at(account.planetId);
        built->accounts.push_back({account.planetId, account.type, account.required,
            horizon * account.consumption, 1 + std::log1p(std::max(0, account.priority)), populationWeight});
    }
    for (const auto& project : projects) {
        if (project.status != ConstructionStatus::Proposed || project.constructionTime > horizon ||
            project.constructionCost > budget)
            continue;
        built->projects.emplace(project.id, project);
        for (size_t i = 0; i < built->scale.size(); ++i)
            built->scale[i] = std::max(built->scale[i], project.development[i]);
    }
    for (const auto& project : projects)
        if (built->projects.contains(project.id))
            for (size_t i = 0; i < built->reference.size(); ++i)
                if (built->scale[i] > 0)
                    built->reference[i] += project.development[i] / built->scale[i];
    result.model = std::move(built);
    return result;
}

double CivilizationObjective::developmentPreference(const Infrastructure& project) const {
    if (!model)
        return project.strategicValue;
    if (!model->projects.contains(project.id))
        return 0;
    double value = 0;
    for (size_t i = 0; i < model->reference.size(); ++i)
        if (model->reference[i] > 0)
            value += model->weights[i + 3] * ((project.development[i] / model->scale[i]) / model->reference[i]);
    return value;
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
    if (model)
        return evaluateExpanded(actual, construction, m);
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

CivilizationMetrics CivilizationObjective::evaluateExpanded(const SimulationResult& actual,
    const InfrastructureOptimizationResult& construction, CivilizationMetrics m) const {
    double resourceLoss = 0, resourceWeight = 0, populationLoss = 0, populationWeight = 0;
    for (const auto& account : model->accounts) {
        SimulationResult::ResourceOutcome outcome{account.target, account.demand};
        if (const auto row = actual.resourceOutcomes.find(account.location); row != actual.resourceOutcomes.end())
            if (const auto stock = row->second.find(account.resource); stock != row->second.end())
                outcome = stock->second;
        if (account.target > 0) {
            resourceLoss += account.priority * fraction(outcome.endingShortage, account.target);
            resourceWeight += account.priority;
        }
        if (account.demand > 0) {
            const double missed = fraction(outcome.unserved, account.demand);
            if (account.populationWeight > 0) {
                populationLoss += account.populationWeight * missed;
                populationWeight += account.populationWeight;
            } else {
                resourceLoss += account.priority * missed;
                resourceWeight += account.priority;
            }
        }
    }
    m.populationApplicable = populationWeight > 0;
    m.populationCoverage = populationWeight > 0 ? 1 - populationLoss / populationWeight : 0;
    m.security = resourceWeight > 0 ? 1 - resourceLoss / resourceWeight : 0;
    double timely = 0, required = 0;
    for (const auto& mission : obligations) {
        const double importance = std::max(1, mission.priority);
        if (!(largestCargo > 0) || !(mission.cargoAmount > 0))
            continue;
        required += importance * (mission.cargoAmount / largestCargo);
        if (const auto delivered = actual.deliveries.find(mission.id);
            delivered != actual.deliveries.end() && delivered->second.arrival <= mission.deadline)
            if (std::isfinite(delivered->second.amount) && delivered->second.amount > 0)
                timely += importance * (std::min(delivered->second.amount, mission.cargoAmount) / largestCargo);
    }
    m.service = required > 0 ? timely / required : 0;
    m.missionLoss = required > 0 ? 1 - m.service : 0;
    std::unordered_set<int> selected;
    double projectCost = 0;
    for (int id : construction.selectedInfrastructureIds) {
        const auto project = model->projects.find(id);
        if (project == model->projects.end() || !selected.insert(id).second) {
            m.score = 0;
            return m;
        }
        projectCost += project->second.constructionCost;
        for (size_t i = 0; i < m.development.size(); ++i)
            if (model->scale[i] > 0)
                m.development[i] += project->second.development[i] / model->scale[i];
    }
    if (projectCost > budget) {
        m.score = 0;
        return m;
    }
    for (size_t i = 0; i < m.development.size(); ++i) {
        m.developmentApplicable[i] = model->reference[i] > 0;
        m.development[i] = m.developmentApplicable[i] ? m.development[i] / model->reference[i] : 0;
    }
    m.transportPenalty = boundedLoss(actual.transportationCost, cargoRef);
    m.fuelPenalty = boundedLoss(actual.fuel, cargoRef);
    m.constructionPenalty = boundedLoss(projectCost, budget);
    m.wastePenalty = boundedLoss(actual.waste, cargoRef + consumptionRef);
    m.riskPenalty = boundedLoss(actual.risk, static_cast<double>(obligations.size()));
    const std::array values{m.populationCoverage, m.security, m.service, m.development[0], m.development[1],
        m.development[2], 1 - m.transportPenalty, 1 - m.fuelPenalty,
        1 - (m.wastePenalty + m.constructionPenalty) / 2, 1 - m.riskPenalty};
    const std::array active{m.populationApplicable, resourceWeight > 0, required > 0, m.developmentApplicable[0],
        m.developmentApplicable[1], m.developmentApplicable[2], true, true, true, true};
    double weighted = 0;
    for (size_t i = 0; i < active.size(); ++i)
        if (active[i]) {
            m.applicableWeight += model->weights[i];
            weighted += model->weights[i] * std::clamp(values[i], 0.0, 1.0);
        }
    m.score = 100 * weighted / m.applicableWeight;
    return m;
}
