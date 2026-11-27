#pragma once

#include <chrono>
#include <atomic>
#include <cstddef>
#include <csignal>
#include <string>
#include <string_view>

struct BudgetLimits {
    double seconds = 0;
    std::size_t memoryMiB = 0;
    double reserveSeconds = 0;
    bool enabled() const { return seconds > 0 || memoryMiB > 0; }
    bool parse(std::string_view option);
};

class PlanningBudget {
public:
    struct Interrupted {};
    using Clock = std::chrono::steady_clock;

    explicit PlanningBudget(BudgetLimits limits);
    ~PlanningBudget() { current = previous; }
    PlanningBudget(const PlanningBudget&) = delete;
    PlanningBudget& operator=(const PlanningBudget&) = delete;

    static void checkpoint() {
        if (current)
            current->check();
    }

    void stage(const char* value) { stageName = value; }
    void finalWork() { finalizing = true; }
    void requestStop() { stopRequested.store(true, std::memory_order_relaxed); }
    static void consoleInterrupt(int) { consoleStop = 1; }
    double elapsed() const;
    double remaining() const;
    double reserve() const { return reserved; }
    std::size_t peakBytes() const;
    bool memoryAvailable() const;
    const char* reason() const { return exhaustedReason; }
    const char* exhaustedAt() const { return exhaustedStage; }

private:
    void check();
    static inline thread_local PlanningBudget* current = nullptr;
    static inline volatile std::sig_atomic_t consoleStop = 0;
    PlanningBudget* previous = current;
    BudgetLimits limits;
    Clock::time_point started = Clock::now();
    Clock::time_point lastMemorySample{};
    double reserved = 0;
    const char* stageName = "input";
    const char* exhaustedReason = "";
    const char* exhaustedStage = "";
    bool finalizing = false;
    std::atomic<bool> stopRequested{false};
};
