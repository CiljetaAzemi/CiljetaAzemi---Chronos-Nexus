#pragma once
#include <iosfwd>
#include "../benchmarks/DatasetGenerator.h"

namespace scenario {
struct Input {
    benchmark::Scenario data;
    int budget = 0;
    double horizon = 30;
    bool expandedObjective = false;
    std::string objectiveProfile = "balanced";
    bool experimentalAllocator = false;
    bool feasibilityAwareAllocator = false;
    std::vector<Event> knownProductionChanges;
};
Input loadJson(std::istream& source);
void writeResult(Input input, std::ostream& destination);
int runFile(int argc, char* argv[]);
}
