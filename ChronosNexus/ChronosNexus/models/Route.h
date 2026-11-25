#pragma once
#include <optional>

class Route {
public:
    // Identification
    int id;
    int from;
    int to;

    // Travel information
    double distance;
    double travelTime;
    double fuelConsumption;

    // Risk and reliability
    double risk;
    double reliability;

    // Transportation capacity
    double maxCapacity;

    // Current availability
    bool isAvailable;

    // Route type
    bool isWormhole;

    // Bounds at reference speed. Legacy routes have a fixed travel time.
    std::optional<double> minimumTravelTime;
    std::optional<double> maximumTravelTime;
    double minimumTime() const { return minimumTravelTime.value_or(travelTime); }
    double maximumTime() const { return maximumTravelTime.value_or(travelTime); }
};

