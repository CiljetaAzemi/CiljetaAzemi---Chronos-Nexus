#pragma once

#include "ResourceAllocator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_map>

// Each account is replayed in simulator order: production and consumption,
// arrivals, then departures. A trial checks the entire affected account.
class InventoryReservations {
public:
    explicit InventoryReservations(const std::vector<ResourceDemand>& balances) {
        for (const auto& b : balances) {
            if (!std::isfinite(b.available) || !std::isfinite(b.storage) ||
                !std::isfinite(b.production) || !std::isfinite(b.consumption) ||
                b.available < 0 || b.available > b.storage ||
                b.production < 0 || b.consumption < 0 ||
                !accounts[b.planetId].emplace(b.type, Account{b.available,
                    b.production - b.consumption, b.storage, {}}).second)
                throw std::invalid_argument("Invalid or duplicate planning inventory.");
        }
    }

    std::vector<double> departures(int origin, int destination, ResourceType type,
        double earliest, double latest, double duration, double amount) const {
        const auto* source = find(origin, type);
        const auto* sink = find(destination, type);
        if (!source || !sink || latest < earliest || duration <= 0)
            return {};
        std::vector<double> times{earliest};
        opportunities(*source, earliest, latest, amount, true, 0, times);
        opportunities(*sink, earliest + duration, latest + duration, amount, false, duration, times);
        std::sort(times.begin(), times.end());
        times.erase(std::unique(times.begin(), times.end()), times.end());
        std::erase_if(times, [&](double time) { return time < earliest || time > latest; });
        if (times.size() > trialLimit)
            times.resize(trialLimit);
        return times;
    }

    bool reserve(int origin, int destination, ResourceType type,
        double departure, double arrival, double amount, bool commit) {
        auto* source = find(origin, type);
        auto* sink = find(destination, type);
        if (!source || !sink || !(departure < arrival) || !(amount > 0))
            return false;
        auto& outgoing = source->changes[departure];
        auto& incoming = sink->changes[arrival];
        const double oldOutgoing = outgoing.out;
        const double oldIncoming = incoming.in;
        outgoing.out += amount;
        incoming.in += amount;
        const bool feasible = valid(*source) && (source == sink || valid(*sink));
        if (!feasible || !commit) {
            outgoing.out = oldOutgoing;
            incoming.in = oldIncoming;
            if (outgoing.in == 0 && outgoing.out == 0)
                source->changes.erase(departure);
            if (incoming.in == 0 && incoming.out == 0)
                sink->changes.erase(arrival);
        }
        return feasible;
    }

private:
    static constexpr size_t trialLimit = 12;
    struct Change { double in = 0; double out = 0; };
    struct Account {
        double initial;
        double rate;
        double capacity;
        std::map<double, Change> changes;
    };
    std::unordered_map<int, std::unordered_map<ResourceType, Account>> accounts;

    const Account* find(int location, ResourceType type) const {
        const auto row = accounts.find(location);
        if (row == accounts.end())
            return nullptr;
        const auto account = row->second.find(type);
        return account == row->second.end() ? nullptr : &account->second;
    }

    Account* find(int location, ResourceType type) {
        return const_cast<Account*>(static_cast<const InventoryReservations&>(*this).find(location, type));
    }

    static double advance(const Account& account, double stock, double days) {
        return std::clamp(stock + account.rate * days, 0.0, account.capacity);
    }

    static bool valid(const Account& account) {
        double stock = account.initial;
        double previous = 0;
        for (const auto& [time, change] : account.changes) {
            stock = advance(account, stock, time - previous);
            if (change.in > account.capacity - stock)
                return false;
            stock += change.in;
            if (change.out > stock)
                return false;
            stock -= change.out;
            previous = time;
        }
        return true;
    }

    static void opportunities(const Account& account, double earliest, double latest,
        double amount, bool withdrawal, double offset, std::vector<double>& times) {
        const size_t limit = times.size() + trialLimit;
        double stock = account.initial;
        double previous = 0;
        auto segment = [&](double end) {
            const double start = std::max(earliest, previous);
            if (start >= end || times.size() >= limit)
                return;
            const double available = advance(account, stock, start - previous);
            const double slack = withdrawal ? available : account.capacity - available;
            const double improvement = withdrawal ? account.rate : -account.rate;
            if (slack >= amount)
                times.push_back(start - offset);
            else if (improvement > 0) {
                const double ready = start + (amount - slack) / improvement;
                if (ready < end) {
                    times.push_back(ready - offset);
                    if (times.size() < limit)
                        times.push_back(std::nextafter(ready - offset,
                            std::numeric_limits<double>::infinity()));
                }
            }
        };
        for (const auto& [time, change] : account.changes) {
            if (time > latest)
                break;
            segment(time);
            stock = advance(account, stock, time - previous) + change.in - change.out;
            previous = time;
            if (time >= earliest && times.size() < limit) {
                const double candidate = time - offset;
                times.push_back(withdrawal ? candidate : std::nextafter(candidate,
                    std::numeric_limits<double>::infinity()));
            }
            if (times.size() >= limit)
                return;
        }
        segment(std::nextafter(latest, std::numeric_limits<double>::infinity()));
    }
};
