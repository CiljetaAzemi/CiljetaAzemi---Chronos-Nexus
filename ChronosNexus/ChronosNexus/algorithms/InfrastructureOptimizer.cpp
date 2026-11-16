
#include "InfrastructureOptimizer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>
#include <vector>

InfrastructureOptimizationResult InfrastructureOptimizer::optimize(
    const std::vector<Infrastructure>& projects,
    int budget
) {
    if (budget < 0) {
        throw std::invalid_argument("Budget cannot be negative.");
    }

    // Only proposed projects can be selected.
    // Costs must be non-negative whole credits.
    std::vector<const Infrastructure*> candidates;
    std::unordered_set<int> seenIds;

    for (const auto& project : projects) {
        if (project.status != ConstructionStatus::Proposed) {
            continue;
        }

        if (project.id < 0 || !seenIds.insert(project.id).second) {
            throw std::invalid_argument("Invalid or duplicate project ID.");
        }

        if (!std::isfinite(project.constructionCost) ||
            project.constructionCost < 0.0 ||
            project.constructionCost > static_cast<double>(budget) ||
            std::floor(project.constructionCost) != project.constructionCost) {
            if (std::isfinite(project.constructionCost) &&
                project.constructionCost > budget) {
                continue; // Cannot afford this project.
            }

            throw std::invalid_argument("Invalid construction cost.");
        }

        if (!std::isfinite(project.strategicValue)) {
            throw std::invalid_argument("Invalid strategic value.");
        }

        if (project.strategicValue <= 0.0) {
            continue;
        }

        candidates.push_back(&project);
    }

    // dp[b] stores the maximum benefit for budget b.
    std::vector<double> dp(budget + 1, 0.0);

    // Persistent selection records preserve earlier decisions.
    struct SelectionNode {
        int projectId;
        int previousNode;
    };

    std::vector<SelectionNode> nodes;
    std::vector<int> bestNode(budget + 1, -1);

    for (const Infrastructure* project : candidates) {
        const int cost = static_cast<int>(project->constructionCost);
        const double value = project->strategicValue;

        // Descending order prevents selecting a project twice.
        for (int b = budget; b >= cost; --b) {
            const double newValue = dp[b - cost] + value;

            if (newValue > dp[b]) {
                dp[b] = newValue;

                nodes.push_back({
                    project->id,
                    bestNode[b - cost]
                    });

                bestNode[b] = static_cast<int>(nodes.size()) - 1;
            }
        }
    }

    InfrastructureOptimizationResult result;
    result.totalStrategicValue = dp[budget];

    // Reconstruct selected projects.
    int nodeIndex = bestNode[budget];

    while (nodeIndex != -1) {
        const SelectionNode& node = nodes[nodeIndex];
        result.selectedInfrastructureIds.push_back(node.projectId);
        nodeIndex = node.previousNode;
    }

    std::reverse(
        result.selectedInfrastructureIds.begin(),
        result.selectedInfrastructureIds.end()
    );

    // Calculate the actual construction cost.
    for (int id : result.selectedInfrastructureIds) {
        for (const auto& project : projects) {
            if (project.id == id) {
                result.totalCost += project.constructionCost;
                break;
            }
        }
    }

    return result;
}
