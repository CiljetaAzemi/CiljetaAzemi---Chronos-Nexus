#pragma once

#include "ResourceAllocator.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
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
                    b.production - b.consumption, b.storage, {}, b.required}).second)
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
            if (outgoing.empty())
                source->changes.erase(departure);
            if (incoming.empty())
                sink->changes.erase(arrival);
        }
        return feasible;
    }

    using Key = std::pair<int, ResourceType>;
    struct Shortages { double ending = 0; double unserved = 0; };
    struct Projection {
        double stock = 0;
        double endingShortage = 0;
        double unserved = 0;
        double waste = 0;
        std::optional<double> depletion;
    };

    void addRateChange(int location, ResourceType type, double time, double delta) {
        if (auto* account = find(location, type))
            account->changes[time].rateDelta += delta;
    }

    Projection project(int location, ResourceType type, double horizon) const {
        const auto* account = find(location, type);
        if (!account)
            return {};
        Projection result;
        result.stock = account->initial;
        double rate = account->rate;
        double last = 0;
        auto advanceTo = [&](double time, bool final) {
            const double raw = result.stock + rate * (time - last);
            if (!result.depletion && rate < 0 && (raw < 0 || (final && raw == 0)))
                result.depletion = last + result.stock / -rate;
            result.unserved += std::max(0.0, -raw);
            result.waste += std::max(0.0, raw - account->capacity);
            result.stock = std::clamp(raw, 0.0, account->capacity);
            last = time;
        };
        for (const auto& [time, change] : account->changes) {
            if (time > horizon)
                break;
            advanceTo(time, false);
            result.waste += std::max(0.0, result.stock + change.in - account->capacity);
            const double after = std::min(account->capacity, result.stock + change.in) - change.out;
            if (after < 0 && !result.depletion)
                result.depletion = time;
            result.unserved += std::max(0.0, -after);
            result.stock = std::max(0.0, after);
            rate += change.rateDelta;
        }
        advanceTo(horizon, true);
        result.endingShortage = std::max(0.0, account->target - result.stock);
        return result;
    }

    void addConfirmedArrival(int location, ResourceType type, double time, double amount) {
        if (auto* account = find(location, type))
            account->changes[time].in += amount;
    }

    void addPlannedDeparture(int location, ResourceType type, double time, double amount) {
        if (auto* account = find(location, type))
            account->changes[time].out += amount;
    }

    InventoryReservations subset(const std::vector<Key>& keys) const {
        InventoryReservations copy({});
        for (const auto& [location, type] : keys)
            if (const auto* account = find(location, type))
                copy.accounts[location].emplace(type, *account);
        return copy;
    }

    void replaceAccounts(InventoryReservations&& replacement) {
        for (auto& [location, row] : replacement.accounts)
            for (auto& [type, account] : row)
                accounts.at(location).at(type) = std::move(account);
    }

    bool adjust(int origin, int destination, ResourceType type,
        double departure, double arrival, double signedAmount) {
        auto* source = find(origin, type);
        auto* sink = find(destination, type);
        if (!source || !sink || !(departure < arrival))
            return false;
        auto& outgoing = source->changes[departure];
        outgoing.out += signedAmount;
        if (outgoing.empty())
            source->changes.erase(departure);
        auto& incoming = sink->changes[arrival];
        incoming.in += signedAmount;
        if (incoming.empty())
            sink->changes.erase(arrival);
        return true;
    }

    bool feasible() const {
        for (const auto& [location, row] : accounts)
            for (const auto& [type, account] : row)
                if (!valid(account))
                    return false;
        return true;
    }

    Shortages shortages(double horizon) const {
        Shortages result;
        for (const auto& [location, row] : accounts) {
            for (const auto& [type, account] : row) {
                double stock = account.initial;
                double last = 0;
                auto advanceTo = [&](double time) {
                    const double raw = stock + account.rate * (time - last);
                    result.unserved += std::max(0.0, -raw);
                    stock = std::clamp(raw, 0.0, account.capacity);
                    last = time;
                };
                for (const auto& [time, change] : account.changes) {
                    if (time > horizon)
                        break;
                    advanceTo(time);
                    stock += change.in - change.out;
                }
                advanceTo(horizon);
                result.ending += std::max(0.0, account.target - stock);
            }
        }
        return result;
    }

private:
    static constexpr size_t trialLimit = 12;
    struct Change {
        double in = 0;
        double out = 0;
        double rateDelta = 0;
        bool empty() const { return in == 0 && out == 0 && rateDelta == 0; }
    };
    struct Account {
        double initial;
        double rate;
        double capacity;
        std::map<double, Change> changes;
        double target = 0;
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

    static double advance(const Account& account, double stock, double rate, double days) {
        return std::clamp(stock + rate * days, 0.0, account.capacity);
    }

    static bool valid(const Account& account) {
        double stock = account.initial;
        double previous = 0;
        double rate = account.rate;
        for (const auto& [time, change] : account.changes) {
            stock = advance(account, stock, rate, time - previous);
            if (change.in < 0 || change.out < 0 || change.in > account.capacity - stock)
                return false;
            stock += change.in;
            if (change.out > stock)
                return false;
            stock -= change.out;
            rate += change.rateDelta;
            previous = time;
        }
        return true;
    }

    static void opportunities(const Account& account, double earliest, double latest,
        double amount, bool withdrawal, double offset, std::vector<double>& times) {
        const size_t limit = times.size() + trialLimit;
        double stock = account.initial;
        double previous = 0;
        double rate = account.rate;
        auto segment = [&](double end) {
            const double start = std::max(earliest, previous);
            if (start >= end || times.size() >= limit)
                return;
            const double available = advance(account, stock, rate, start - previous);
            const double slack = withdrawal ? available : account.capacity - available;
            const double improvement = withdrawal ? rate : -rate;
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
            stock = advance(account, stock, rate, time - previous) + change.in - change.out;
            rate += change.rateDelta;
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
