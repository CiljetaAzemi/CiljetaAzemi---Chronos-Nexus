#pragma once

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
};

