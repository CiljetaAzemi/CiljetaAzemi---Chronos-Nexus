#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <vector>
#include "interfaces/highs_c_api.h"

int main() {
    // Fixed compatible departures A->X, A->Y, B->X, B->Y.
    // Choose fractional quantities without changing spacecraft or times.
    const double costs[]{1, 2, 2, 10};
    const double lower[]{0, 0, 0, 0};
    const double upper[]{4.5, 4.5, 3.25, 3.25};
    const double rowLower[]{0, 0, 4.5, 3.25};
    const double rowUpper[]{4.5, 3.25, 4.5, 3.25};
    const HighsInt starts[]{0, 2, 4, 6};
    const HighsInt columns[]{0, 1, 2, 3, 0, 2, 1, 3};
    const double coefficients[]{1, 1, 1, 1, 1, 1, 1, 1};
    std::array<double, 4> quantity{}, dual{}, stocks{}, shadow{};
    std::array<HighsInt, 4> columnsBasis{}, rowsBasis{};
    HighsInt status = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto result = Highs_lpCall(4, 4, 8, kHighsMatrixFormatRowwise, kHighsObjSenseMinimize, 0,
        costs, lower, upper, rowLower, rowUpper, starts, columns, coefficients, quantity.data(),
        dual.data(), stocks.data(), shadow.data(), columnsBasis.data(), rowsBasis.data(), &status);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    double cost = 0;
    for (size_t i = 0; i < quantity.size(); ++i)
        cost += costs[i] * quantity[i];
    // With A->X = x, all other quantities follow from conservation.
    // Cost is 6.5*x? Direct expansion gives 8? Check with the exact endpoint.
    // x is in [1.25, 4.5], and the cost coefficient is 1-2-2+10 = 7.
    constexpr double expectedCost = 1.25 + 2 * 3.25 + 2 * 3.25;
    constexpr double recipientFirstCost = 4.5 + 10 * 3.25;
    if (result != kHighsStatusOk || status != kHighsModelStatusOptimal ||
        std::abs(cost - expectedCost) > 1e-8 || std::abs(quantity[0] - 1.25) > 1e-8)
        return 1;
    std::cout << std::setprecision(12)
        << "quantity_ax,quantity_ay,quantity_bx,quantity_by,lp_cost,recipient_first_cost,solve_seconds\n"
        << quantity[0] << ',' << quantity[1] << ',' << quantity[2] << ',' << quantity[3] << ','
        << cost << ',' << recipientFirstCost << ',' << elapsed << '\n';
}
