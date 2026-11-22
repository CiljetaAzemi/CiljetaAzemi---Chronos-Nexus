#include "benchmarks/Benchmark.h"
#include "examples/Demonstration.h"
#include "scenario/ScenarioJson.h"
#include "tests/RegressionTests.h"
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {
    if (argc > 1) {
        try {
            if (argc == 2 && std::string(argv[1]) == "--self-test")
                return runSelfTests();
            if (std::string(argv[1]) == "--scenario-file")
                return scenario::runFile(argc, argv);
            if (std::string(argv[1]) != "--benchmark")
                throw std::invalid_argument("Expected --self-test, --benchmark SIZE SEED [MODE], or --scenario-file INPUT.json --output RESULT.json.");
            return runBenchmark(argc, argv);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    return runDemonstration();
}
