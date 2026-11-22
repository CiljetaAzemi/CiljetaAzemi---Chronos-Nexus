#pragma once
#include <iosfwd>
#include "../benchmarks/DatasetGenerator.h"

namespace scenario {
struct Input {
    benchmark::Scenario data;
    int budget = 0;
    double horizon = 30;
    std::vector<Event> knownProductionChanges;
};
Input loadJson(std::istream& source);
void writeResult(Input input, std::ostream& destination);
int runFile(int argc, char* argv[]);
}
