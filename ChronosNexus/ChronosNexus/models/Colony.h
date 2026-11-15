#pragma once

#include "Planet.h"

class Colony : public Planet {
public:
    // Colony identification
    int parentPlanetId = -1;

    // Colony development
    bool isEstablished = false;
    double developmentLevel = 0.0;
};
