#include "PlanningBudget.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace {
std::pair<std::size_t, std::size_t> workingSet() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb = sizeof(memory);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
        return {memory.WorkingSetSize, memory.PeakWorkingSetSize};
#endif
    return {};
}
}

bool BudgetLimits::parse(std::string_view option) {
    const auto equal = option.find('=');
    const auto name = option.substr(0, equal);
    if (name != "--time-budget-ms" && name != "--memory-budget-mib" && name != "--reserve-ms")
        return false;
    const auto value = equal == option.npos ? std::string{} : std::string(option.substr(equal + 1));
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument(std::string(name) + " requires a positive integer.");
    const auto number = std::stoull(value);
    if (number == 0 || number > std::numeric_limits<unsigned int>::max() ||
        (name == "--memory-budget-mib" && number > std::numeric_limits<std::size_t>::max() / 1048576))
        throw std::invalid_argument(std::string(name) + " is out of range.");
    if (name == "--time-budget-ms")
        seconds = static_cast<double>(number) / 1000;
    else if (name == "--reserve-ms")
        reserveSeconds = static_cast<double>(number) / 1000;
    else
        memoryMiB = static_cast<std::size_t>(number);
    return true;
}

PlanningBudget::PlanningBudget(BudgetLimits settings) : limits(settings) {
    if (limits.reserveSeconds > 0 && (limits.seconds <= 0 || limits.reserveSeconds >= limits.seconds))
        throw std::invalid_argument("Output reserve requires a time budget and must be smaller than it.");
    if (limits.memoryMiB && workingSet().first == 0)
        throw std::runtime_error("Working-set monitoring is unavailable.");
    if (limits.enabled())
        current = this;
    if (limits.seconds > 0)
        reserved = limits.reserveSeconds > 0 ? limits.reserveSeconds :
            std::clamp(limits.seconds * 0.15, 0.001, 3.0);
    reserved = std::min(reserved, limits.seconds * 0.9);
}

double PlanningBudget::elapsed() const {
    return std::chrono::duration<double>(Clock::now() - started).count();
}

double PlanningBudget::remaining() const {
    return limits.seconds > 0 ? std::max(0.0, limits.seconds - elapsed()) :
        std::numeric_limits<double>::infinity();
}

std::size_t PlanningBudget::peakBytes() const { return workingSet().second; }

bool PlanningBudget::memoryAvailable() const {
    return !limits.memoryMiB || workingSet().first < limits.memoryMiB * 1048576;
}

void PlanningBudget::check() {
    const auto now = Clock::now();
    const char* cause = nullptr;
    if (limits.seconds > 0 &&
        std::chrono::duration<double>(now - started).count() >= limits.seconds - reserved * (finalizing ? 0.25 : 1))
        cause = "time";
    if (limits.memoryMiB && now - lastMemorySample >= std::chrono::milliseconds(20)) {
        lastMemorySample = now;
        if (!memoryAvailable())
            cause = "memory";
    }
    if (stopRequested.load(std::memory_order_relaxed))
        cause = "cancelled";
    if (cause) {
        if (!*exhaustedReason) {
            exhaustedReason = cause;
            exhaustedStage = stageName;
        }
        throw Interrupted{};
    }
}
