#include "SimulationEngine.h"
#include "RecoveryPlanner.h"
#include "../algorithms/CivilizationObjective.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_set>

namespace {
enum class ActionType { Event, Forecast, Arrival, Departure };

struct Action {
    double time;
    ActionType type;
    size_t index;
};

struct Later {
    bool operator()(const Action& a, const Action& b) const {
        if (a.time != b.time)
            return a.time > b.time;
        if (a.type != b.type)
            return a.type > b.type;
        return a.index > b.index;
    }
};

struct Balance {
    double stock = 0;
    double target = 0;
    double production = 0;
    double consumption = 0;
    double storage = 1e30;
    double updatedAt = 0;
    double unserved = 0;

    void advance(double now, SimulationResult& result) {
        const double next = stock + (production - consumption) * (now - updatedAt);
        result.waste += std::max(0.0, next - storage);
        const double missed = std::max(0.0, -next);
        result.stockoutAmount += missed;
        unserved += missed;
        stock = std::clamp(next, 0.0, storage);
        updatedAt = now;
    }
};
}

SimulationResult SimulationEngine::run(const GalaxyGraph& galaxy,
    const std::vector<ResourceDemand>& planets, const std::vector<Mission>& missions,
    const std::vector<Spacecraft>& spacecraft, const SchedulingResult& schedule,
    const std::vector<Event>& events, const std::function<void(size_t)>& progress,
    bool recheckAllDepartures, double minimumHorizon, const std::vector<Event>* knownSupplyEvents,
    const SimulationObjective* scoring, const std::vector<ResourceDemand>* allocationNetwork, bool indexedAid,
    const AidRoutingOptions& aidRouting) {
    SimulationResult result;
    result.scheduled = static_cast<int>(schedule.scheduled.size());
    GalaxyGraph active = galaxy;
    std::unordered_map<int, std::unordered_map<ResourceType, Balance>> balances;
    for (const auto& p : planets) {
        if (!std::isfinite(p.available) || !std::isfinite(p.required) ||
            !std::isfinite(p.production) || !std::isfinite(p.consumption) ||
            !std::isfinite(p.storage) || p.available < 0 || p.required < 0 ||
            p.production < 0 || p.consumption < 0 || p.storage < p.available ||
            !balances[p.planetId].emplace(p.type, Balance{p.available, p.required,
                p.production, p.consumption, p.storage}).second)
            throw std::invalid_argument("Invalid or duplicate inventory.");
    }
    std::unordered_map<int, const Mission*> missionById;
    std::unordered_map<int, Spacecraft> ships;
    for (const auto& mission : missions) {
        if (!missionById.emplace(mission.id, &mission).second)
            throw std::invalid_argument("Duplicate mission ID.");
        result.missionStatuses.emplace(mission.id, MissionStatus::Pending);
    }
    for (const auto& ship : spacecraft)
        if (!ships.emplace(ship.id, ship).second)
            throw std::invalid_argument("Duplicate spacecraft ID.");

    std::unordered_set<int> disabled;
    struct Flight { size_t entry; double arrival; FlightEstimate estimate; };
    std::unordered_map<int, Flight> inFlight;
    auto entries = schedule.scheduled;
    std::set<size_t> pending;
    std::priority_queue<Action, std::vector<Action>, Later> queue;
    for (size_t i = 0; i < events.size(); ++i)
        if (std::isfinite(events[i].eventTime) && events[i].eventTime >= 0)
            queue.push({events[i].eventTime, ActionType::Event, i});
    for (size_t i = 0; i < schedule.scheduled.size(); ++i) {
        const auto& entry = schedule.scheduled[i];
        if (!missionById.contains(entry.missionId) ||
            !std::isfinite(entry.departureTime) || entry.departureTime < 0)
            throw std::invalid_argument("Invalid scheduled departure.");
        auto& status = result.missionStatuses.at(entry.missionId);
        if (status != MissionStatus::Pending)
            throw std::invalid_argument("Duplicate scheduled mission.");
        status = MissionStatus::Scheduled;
        pending.insert(i);
        queue.push({entry.departureTime, ActionType::Departure, i});
    }

    double now = 0;
    using Decision = SimulationResult::ReplanningDecision;
    using Key = InventoryReservations::Key;
    auto accountAtNow = [&](const Key& key) {
        const auto& stock = balances.at(key.first).at(key.second);
        const double available = std::clamp(stock.stock +
            (stock.production - stock.consumption) * (now - stock.updatedAt), 0.0, stock.storage);
        return ResourceDemand{key.first, available, stock.target, 0, key.second,
            stock.production, stock.consumption, stock.storage};
    };
    std::map<Key, std::vector<const Event*>> futureSupply;
    if (knownSupplyEvents)
        for (const auto& event : *knownSupplyEvents)
            if (std::isfinite(event.eventTime) && std::isfinite(event.severity) &&
                (event.eventType == EventType::ResourceProductionDecrease ||
                    event.eventType == EventType::ConstructionCompleted))
                futureSupply[{event.affectedObjectId, event.resourceType}].push_back(&event);
    for (auto& [key, changes] : futureSupply)
        std::stable_sort(changes.begin(), changes.end(), [](const Event* a, const Event* b) {
            return a->eventTime < b->eventTime;
        });
    bool beforeFirstEvent = true;
    auto inventoryFor = [&](const std::set<Key>& keys, const std::set<size_t>& held = {},
        const Mission* replacing = nullptr) {
        std::vector<ResourceDemand> initial;
        for (const auto& key : keys)
            initial.push_back(accountAtNow(key));
        InventoryReservations inventory(initial);
        for (const auto& account : initial) {
            const auto it = futureSupply.find({account.planetId, account.type});
            if (it == futureSupply.end())
                continue;
            double production = account.production;
            for (const auto* event : it->second) {
                if (event->eventTime < now || (!beforeFirstEvent && event->eventTime == now))
                    continue;
                const double next = event->eventType == EventType::ConstructionCompleted
                    ? production + std::max(0.0, event->severity)
                    : production * (1 - std::clamp(event->severity, 0.0, 1.0));
                inventory.addRateChange(account.planetId, account.type, event->eventTime - now, next - production);
                production = next;
            }
        }
        for (const auto& [id, flight] : inFlight) {
            if (disabled.contains(id))
                continue;
            const auto& cargo = *missionById.at(entries[flight.entry].missionId);
            inventory.addConfirmedArrival(cargo.destination, cargo.cargoType, flight.arrival - now, cargo.cargoAmount);
        }
        auto addBooking = [&](size_t index, bool includeArrival) {
            const auto& row = entries[index];
            const auto& job = *missionById.at(row.missionId);
            if (includeArrival || !replacing || job.origin != replacing->destination)
                inventory.addPlannedDeparture(job.origin, job.cargoType,
                    std::max(0.0, row.departureTime - now), job.cargoAmount);
            if (includeArrival)
                inventory.addConfirmedArrival(job.destination, job.cargoType,
                    std::max(0.0, row.arrivalTime - now), job.cargoAmount);
        };
        for (auto index : pending)
            addBooking(index, true);
        // A held successor's delivery is uncertain until its carrier's arrival is checked.
        for (auto index : held)
            addBooking(index, false);
        return inventory;
    };
    // Planning-only index. The graph and clock do not change during this transaction.
    std::unordered_map<int, std::map<size_t, Spacecraft>> tailsByLocation;
    std::unordered_map<int, size_t> shipOrdinal;
    auto collectPlans = [&] {
        std::vector<recovery::ShipPlan> plans;
        std::unordered_map<int, size_t> byShip;
        for (const auto& [id, current] : ships) {
            if (disabled.contains(id))
                continue;
            auto state = current;
            if (const auto flight = inFlight.find(id); flight != inFlight.end())
                state.currentLocation = missionById.at(entries[flight->second.entry].missionId)->destination;
            byShip.emplace(id, plans.size());
            plans.push_back({std::move(state), {}});
        }
        for (const auto index : pending) {
            const auto& entry = entries[index];
            if (const auto ship = byShip.find(entry.spacecraftId); ship != byShip.end())
                plans[ship->second].pending.push_back({missionById.at(entry.missionId), &entry});
        }
        return plans;
    };
    auto findReplacement = [&](const Mission& mission, const ScheduledMission* previous,
        const std::set<size_t>& held = {}, recovery::SupplySearch* supply = nullptr,
        const InventoryReservations* forecastInventory = nullptr, bool reconsiderRisk = false) {
        const std::set<Key> keys{{mission.origin, mission.cargoType}, {mission.destination, mission.cargoType}};
        for (const auto& [location, type] : keys) {
            const auto row = balances.find(location);
            if (row == balances.end() || !row->second.contains(type))
                return std::optional<ScheduledMission>{};
        }
        auto inventory = forecastInventory
            ? forecastInventory->subset({{mission.origin, mission.cargoType}, {mission.destination, mission.cargoType}})
            : inventoryFor(keys, held, &mission);
        const auto started = std::chrono::steady_clock::now();
        std::vector<recovery::ShipPlan> plans;
        if (allocationNetwork && indexedAid) {
            // A non-loop aid leg cannot precede a fixed booking: that booking
            // starts at the current location, but the aid leg ends elsewhere.
            if (const auto location = tailsByLocation.find(mission.origin); location != tailsByLocation.end())
                for (const auto& [ordinal, tail] : location->second)
                    plans.push_back({tail, {}});
        } else {
            plans = collectPlans();
            if (allocationNetwork)
                for (const auto& plan : plans)
                    if (plan.state.maxCargoCapacity >= supply->minimumCargo && plan.state.speed > 0)
                        ++result.allocationWork.itineraryBuilds;
        }
        const auto ready = std::chrono::steady_clock::now();
        auto replacement = recovery::findAssignment(mission, previous, active, plans, inventory, now,
            result.nodesExplored, supply, reconsiderRisk);
        if (allocationNetwork) {
            result.allocationWork.donorShipCandidates += plans.size();
            result.allocationWork.itinerarySeconds += std::chrono::duration<double>(ready - started).count();
            result.allocationWork.recoverySeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - ready).count();
            result.allocationWork.routeSeconds += supply->routeSeconds;
        }
        return replacement;
    };


    auto applyRepair = [&](const Event& event, Decision::Reason reason, const Mission& mission,
        std::optional<ScheduledMission> previous, std::optional<ScheduledMission> replacement) {
        ++result.replans;
        result.replanningDecisions.push_back({event.id, event.affectedObjectId, now,
            mission.id, reason, previous, replacement});
        auto& status = result.missionStatuses.at(mission.id);
        if (!replacement) {
            status = MissionStatus::Failed;
            ++result.failed;
            return;
        }
        if (!previous)
            ++result.scheduled;
        else if (previous->routeIds != replacement->routeIds)
            ++result.rerouted;
        status = MissionStatus::Scheduled;
        pending.insert(entries.size());
        queue.push({replacement->departureTime, ActionType::Departure, entries.size()});
        entries.push_back(std::move(*replacement));
    };

    std::set<Key> accounts, refresh;
    std::deque<Mission> preventiveMissions;
    std::unordered_set<int> urgentIds;
    long long nextPreventiveId = 0;
    if (knownSupplyEvents || scoring || allocationNetwork) {
        for (const auto& [location, row] : balances)
            for (const auto& [type, stock] : row)
                accounts.emplace(location, type);
        for (const auto& event : events)
            if (event.urgentMission)
                urgentIds.insert(event.urgentMission->id);
    }
    std::map<Key, int> allocationPriorities;
    std::optional<GalaxyGraph> reverseAidGraph;
    if (allocationNetwork)
        for (const auto& demand : *allocationNetwork)
            allocationPriorities.emplace(Key{demand.planetId, demand.type}, demand.priority);
    auto predictAndPrevent = [&](const std::set<Key>& affected, const Event& trigger) {
        if (affected.empty() || now > minimumHorizon)
            return;
        // Include all same-resource accounts so donor security can be checked too.
        std::set<ResourceType> types;
        for (const auto& key : affected)
            types.insert(key.second);
        std::set<Key> relevant;
        for (const auto& key : accounts)
            if (types.contains(key.second) && (!allocationNetwork || allocationPriorities.contains(key)))
                relevant.insert(key);
        auto inventory = inventoryFor(relevant);
        std::unordered_set<int> shipLocations;
        for (const auto& [id, ship] : ships) {
            if (disabled.contains(id))
                continue;
            if (const auto flight = inFlight.find(id); flight != inFlight.end())
                shipLocations.insert(missionById.at(entries[flight->second.entry].missionId)->destination);
            else
                shipLocations.insert(ship.currentLocation);
        }
        for (const auto index : pending)
            shipLocations.insert(missionById.at(entries[index].missionId)->destination);
        const double remaining = minimumHorizon - now;
        auto forecast = [&](const Key& key) {
            const auto row = inventory.project(key.first, key.second, remaining);
            return SimulationResult::ShortageForecast{key.first, key.second, now,
                row.stock, row.endingShortage, row.unserved,
                row.depletion ? std::optional<double>(now + *row.depletion) : std::nullopt};
        };
        const auto needsSupply = [&](const SimulationResult::ShortageForecast& row) {
            if (allocationNetwork)
                return row.endingShortage > 0 || row.unserved > 0;
            return row.depletionTime && (*row.depletionTime < minimumHorizon ||
                row.unserved > 0 || row.endingShortage > 0);
        };
        std::vector<SimulationResult::ShortageForecast> risk;
        for (const auto& key : affected) {
            auto row = forecast(key);
            result.shortageForecasts.push_back(row);
            if (needsSupply(row))
                risk.push_back(row);
        }
        std::sort(risk.begin(), risk.end(), [&](const auto& a, const auto& b) {
            if (allocationNetwork) {
                const auto rank = [&](const auto& row) {
                    const bool essential = row.resource == ResourceType::Food || row.resource == ResourceType::Medicine;
                    const bool critical = scoring && scoring->initial.isCriticalResource(row.location, row.resource);
                    return std::tuple{!critical, !essential, row.depletionTime.value_or(minimumHorizon),
                        -static_cast<long long>(allocationPriorities.at({row.location, row.resource})),
                        -row.endingShortage, row.location, row.resource};
                };
                return rank(a) < rank(b);
            }
            return std::tie(a.depletionTime, a.location, a.resource) <
                std::tie(b.depletionTime, b.location, b.resource);
        });
        for (const auto& riskRow : risk) {
            const Key key{riskRow.location, riskRow.resource};
            const auto before = forecast(key);
            if (!needsSupply(before))
                continue;
            std::optional<TravelTimeBounds> timeBounds;
            std::set<std::tuple<int, double, int>> observedQueries;
            if (allocationNetwork)
                ++result.allocationWork.receivers;
            auto residual = before;
            bool accepted = false;
            // Every shipment in this batch must beat the original depletion time.
            const double deadline = before.depletionTime && *before.depletionTime > now
                ? std::nextafter(*before.depletionTime, now)
                : allocationNetwork ? minimumHorizon : now;
            if (deadline > now) {
                auto lowerPriority = [](const auto& a, const auto& b) {
                    return a.first != b.first ? a.first < b.first : a.second > b.second;
                };
                using Donor = std::pair<double, int>;
                std::priority_queue<Donor, std::vector<Donor>, decltype(lowerPriority)> donors(lowerPriority);
                auto addDonor = [&](int location) {
                    if (!shipLocations.contains(location))
                        return;
                    const auto supply = inventory.project(location, key.second, remaining);
                    const double surplus = supply.stock - balances.at(location).at(key.second).target;
                    if (surplus > 0 && !supply.depletion)
                        donors.emplace(surplus, location);
                };
                for (const auto& source : relevant)
                    if (source.first != key.first && source.second == key.second)
                        addDonor(source.first);
                const auto started = std::chrono::steady_clock::now();
                const double storage = balances.at(key.first).at(key.second).storage;
                while (!donors.empty() && (needsSupply(residual) || residual.endingShortage > 0)) {
                    const auto [surplus, donor] = donors.top();
                    donors.pop();
                    while (nextPreventiveId < std::numeric_limits<int>::max() &&
                        (missionById.contains(static_cast<int>(nextPreventiveId)) ||
                            urgentIds.contains(static_cast<int>(nextPreventiveId))))
                        ++nextPreventiveId;
                    if (nextPreventiveId == std::numeric_limits<int>::max())
                        break;
                    Mission job;
                    job.id = static_cast<int>(nextPreventiveId);
                    job.origin = donor;
                    job.destination = key.first;
                    job.cargoType = key.second;
                    job.deadline = deadline;
                    if (allocationNetwork)
                        job.priority = allocationPriorities.at(key);
                    const double need = residual.unserved + residual.endingShortage;
                    const double desired = need > 0 ? need : 1.0;
                    job.cargoAmount = std::min({desired, surplus, storage});
                    // Ignore amounts too small to change the account balances reliably.
                    const double scale = std::max({1.0, desired, accountAtNow(key).available,
                        accountAtNow({donor, key.second}).available});
                    recovery::SupplySearch search{remaining, 8 * std::numeric_limits<double>::epsilon() * scale};
                    if (allocationNetwork) {
                        search.requirePrevention = before.depletionTime && *before.depletionTime > now;
                        search.considerOperatingCost = true;
                        if (aidRouting.progress)
                            search.observeQuery = [&](double cargo, int strategy) {
                                if (!observedQueries.emplace(donor, cargo, strategy).second)
                                    ++result.allocationWork.repeatedQueries;
                            };
                        if (indexedAid && aidRouting.reverseBounds && tailsByLocation.contains(donor) &&
                            !tailsByLocation.at(donor).empty()) {
                            if (!timeBounds) {
                                const auto start = std::chrono::steady_clock::now();
                                if (!reverseAidGraph)
                                    reverseAidGraph.emplace(active.reversed());
                                timeBounds.emplace(Dijkstra::reverseTimeBounds(*reverseAidGraph, key.first));
                                ++result.allocationWork.boundTrees;
                                result.allocationWork.boundNodes += timeBounds->nodes;
                                result.allocationWork.boundEdges += timeBounds->edges;
                                result.allocationWork.boundSeconds += std::chrono::duration<double>(
                                    std::chrono::steady_clock::now() - start).count();
                            }
                            const auto bound = timeBounds->time.find(donor);
                            search.optimisticTime = bound == timeBounds->time.end()
                                ? std::numeric_limits<double>::infinity() : bound->second;
                        }
                    }
                    if (job.cargoAmount < search.minimumCargo)
                        continue;
                    if (allocationNetwork) {
                        ++result.allocationWork.attempted;
                        result.allocationWork.proposedCargo += job.cargoAmount;
                    }
                    auto replacement = findReplacement(job, nullptr, {}, &search, &inventory);
                    if (allocationNetwork) {
                        result.allocationWork.routeQueries += search.routeQueries;
                        result.allocationWork.feasibilityChecks += search.feasibilityChecks;
                        result.allocationWork.routeNodes += search.routeNodes;
                        result.allocationWork.routeEdges += search.routeEdges;
                        result.allocationWork.deadlinePruned += search.deadlinePruned;
                        if (aidRouting.progress)
                            aidRouting.progress(result.allocationWork);
                    }
                    // Booking more inbound cargo cannot free a donor slot or create
                    // storage at this recipient. Retire suppliers the search exhausts.
                    if (!replacement) {
                        if (allocationNetwork)
                            ++result.allocationWork.rejected;
                        continue;
                    }
                    if (allocationNetwork && search.assignedCargo < job.cargoAmount)
                        ++result.allocationWork.partial;
                    job.cargoAmount = search.assignedCargo;
                    if (allocationNetwork)
                        result.allocationWork.committedCargo += job.cargoAmount;
                    ++nextPreventiveId;
                    preventiveMissions.push_back(job);
                    missionById.emplace(job.id, &preventiveMissions.back());
                    result.missionStatuses.emplace(job.id, MissionStatus::Pending);
                    inventory.adjust(job.origin, job.destination, job.cargoType,
                        replacement->departureTime - now, replacement->arrivalTime - now, job.cargoAmount);
                    const auto after = forecast(key);
                    if (allocationNetwork && indexedAid) {
                        const auto ordinal = shipOrdinal.at(replacement->spacecraftId);
                        auto& atDonor = tailsByLocation.at(job.origin);
                        recovery::ShipPlan update{atDonor.at(ordinal), {{&job, &*replacement}}};
                        const auto started = std::chrono::steady_clock::now();
                        const auto tail = recovery::projectTail(update, active, now);
                        if (!tail)
                            throw std::logic_error("Accepted aid could not extend its indexed spacecraft itinerary.");
                        atDonor.erase(ordinal);
                        tailsByLocation[tail->currentLocation].emplace(ordinal, *tail);
                        ++result.allocationWork.itineraryBuilds;
                        result.allocationWork.itinerarySeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                    }
                    result.preventiveDecisions.push_back({residual, job, replacement, after});
                    residual = after;
                    accepted = true;
                    Event cause = trigger;
                    cause.affectedObjectId = key.first;
                    applyRepair(cause, Decision::Reason::PreventiveSupply, job, {}, std::move(replacement));
                    shipLocations.insert(key.first);
                    // Only this supplier's surplus changed. It may have another ship
                    // or a later slot, so rank it again against all remaining suppliers.
                    addDonor(donor);
                }
                if (accepted)
                    result.replanSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            }
            if (!accepted)
                result.preventiveDecisions.push_back({before, {}, {}, before});
        }
    };

    auto repairPending = [&](const Event& event, bool route) {
        std::vector<size_t> affected;
        for (const auto index : pending) {
            const auto& entry = entries[index];
            if (route ? std::count(entry.routeIds.begin(), entry.routeIds.end(), event.affectedObjectId) != 0
                      : entry.spacecraftId == event.affectedObjectId)
                affected.push_back(index);
        }
        const auto earlier = [&](size_t a, size_t b) {
            return std::tie(entries[a].departureTime, entries[a].missionId) <
                std::tie(entries[b].departureTime, entries[b].missionId);
        };
        std::sort(affected.begin(), affected.end(), earlier);
        for (const auto index : affected)
            pending.erase(index);
        for (const auto index : affected) {
            const auto previous = entries[index];
            const auto& mission = *missionById.at(previous.missionId);
            const auto start = std::chrono::steady_clock::now();
            std::set<size_t> held;
            if (route) {
                // A detour can change the ship's arrival time or leave it at the old
                // origin. Hold later bookings on that ship while repairing its prefix.
                for (const auto next : pending)
                    if (entries[next].spacecraftId == previous.spacecraftId && earlier(index, next))
                        held.insert(next);
                for (const auto next : held)
                    pending.erase(next);
            }
            auto replacement = findReplacement(mission, &previous, held);
            applyRepair(event, route ? Decision::Reason::BlockedRoute : Decision::Reason::UnavailableSpacecraft,
                mission, previous, std::move(replacement));

            std::vector<size_t> successors(held.begin(), held.end());
            std::sort(successors.begin(), successors.end(), earlier);
            for (const auto next : successors) {
                held.erase(next);
                const auto original = entries[next];
                const auto& followUp = *missionById.at(original.missionId);
                auto repaired = findReplacement(followUp, &original, held);
                if (repaired && repaired->spacecraftId == original.spacecraftId &&
                    repaired->departureTime == original.departureTime && repaired->arrivalTime == original.arrivalTime &&
                    repaired->routeIds == original.routeIds) {
                    pending.insert(next);
                } else {
                    applyRepair(event, Decision::Reason::DependentAssignment, followUp, original, std::move(repaired));
                }
            }
            result.replanSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        }
    };

    auto displaceForUrgent = [&](const Event& event, const Mission& urgent) -> std::optional<ScheduledMission> {

        if (!scoring || now > minimumHorizon)
            return std::nullopt;
        auto objective = scoring->initial;
        for (const auto& job : result.activatedUrgentMissions)
            objective.addExternalObligation(job);
        const double remaining = minimumHorizon - now;
        auto addProjection = [&](SimulationResult& projection, const InventoryReservations& ledger,
            const std::set<Key>& keys, double sign) {
            for (const auto& [location, type] : keys) {
                const auto value = ledger.project(location, type, remaining);
                projection.remainingShortage += sign * value.endingShortage;
                projection.stockoutAmount += sign * value.unserved;
                projection.waste += sign * value.waste;
                auto& row = projection.resourceOutcomes[location][type];
                row.endingShortage += sign * value.endingShortage;
                row.unserved += sign * value.unserved;
            }
        };
        auto addLeg = [&](SimulationResult& projection, const Mission& job, const ScheduledMission& leg, double sign) {
            RouteMetrics metrics;
            if (!Dijkstra::measure(active, leg.routeIds, job.origin, job.destination, job.cargoAmount, metrics))
                return false;
            projection.transportationCost += sign * metrics.distance * ships.at(leg.spacecraftId).operatingCost;
            projection.fuel += sign * metrics.fuel;
            projection.risk += sign * metrics.risk;
            if (sign > 0)
                projection.deliveries.emplace(job.id, SimulationResult::Delivery{job.cargoAmount, leg.arrivalTime, leg.estimate});
            else
                projection.deliveries.erase(job.id);
            return true;
        };
        std::optional<SimulationResult> common;
        auto buildCommon = [&]() -> bool {
            auto ledger = inventoryFor(accounts);
            if (!ledger.feasible())
                return false;
            common.emplace();
            auto& forecast = *common;
            forecast.deliveries = result.deliveries;
            forecast.transportationCost = result.transportationCost;
            forecast.fuel = result.fuel;
            forecast.risk = result.risk;
            forecast.waste = result.waste;
            forecast.stockoutAmount = result.stockoutAmount;
            for (const auto& [location, type] : accounts) {
                const auto& stock = balances.at(location).at(type);
                const double raw = stock.stock + (stock.production - stock.consumption) * (now - stock.updatedAt);
                const double unserved = std::max(0.0, -raw);
                forecast.resourceOutcomes[location][type] = {0, stock.unserved + unserved};
                forecast.stockoutAmount += unserved;
                forecast.waste += std::max(0.0, raw - stock.storage);
            }
            addProjection(forecast, ledger, accounts, 1);
            for (const auto index : pending) {
                const auto& leg = entries[index];
                if (!addLeg(forecast, *missionById.at(leg.missionId), leg, 1))
                    return false;
            }
            for (const auto& [shipId, flight] : inFlight) {
                const auto& job = *missionById.at(entries[flight.entry].missionId);
                if (disabled.contains(shipId))
                    forecast.waste += job.cargoAmount;
                else
                    forecast.deliveries.emplace(job.id, SimulationResult::Delivery{job.cargoAmount, flight.arrival, flight.estimate});
            }
            return true;
        };
        // Only terminal pending legs can be removed without changing other ship bookings.
        std::map<int, size_t> terminal;
        for (auto index : pending) {
            const auto& row = entries[index];
            auto [slot, added] = terminal.emplace(row.spacecraftId, index);
            if (!added && entries[slot->second].departureTime <= row.departureTime)
                slot->second = index;
        }
        for (const auto& [shipId, index] : terminal) {
            const auto previous = entries[index];
            const auto& incumbent = *missionById.at(previous.missionId);
            if (incumbent.origin != urgent.origin)
                continue;
            const std::set<Key> keys{{urgent.origin, urgent.cargoType}, {urgent.destination, urgent.cargoType},
                {incumbent.origin, incumbent.cargoType}, {incumbent.destination, incumbent.cargoType}};
            const bool accountsExist = std::all_of(keys.begin(), keys.end(), [&](const auto& key) {
                return balances.contains(key.first) && balances.at(key.first).contains(key.second);
            });
            if (!accountsExist)
                continue;
            const auto before = inventoryFor(keys);
            pending.erase(index);
            auto replacement = findReplacement(urgent, nullptr);
            auto after = inventoryFor(keys);
            if (replacement && replacement->spacecraftId == shipId &&
                after.adjust(urgent.origin, urgent.destination, urgent.cargoType,

                    replacement->departureTime - now, replacement->arrivalTime - now, urgent.cargoAmount) &&
                after.feasible()) {
                if (!common) {
                    pending.insert(index);
                    if (!buildCommon())
                        return std::nullopt;
                    pending.erase(index);
                }
                auto candidate = *common;
                addProjection(candidate, before, keys, -1);
                addProjection(candidate, after, keys, 1);
                if (addLeg(candidate, incumbent, previous, -1) && addLeg(candidate, urgent, *replacement, 1) &&
                    objective.evaluate(candidate, scoring->construction).score >
                        objective.evaluate(*common, scoring->construction).score + 1e-9) {
                    applyRepair(event, Decision::Reason::DisplacedBooking, incumbent, previous, {});
                    return replacement;
                }
            }
            pending.insert(index);
        }
        return std::nullopt;
    };

    if (allocationNetwork) {
        if (indexedAid) {
            const auto started = std::chrono::steady_clock::now();
            auto plans = collectPlans();
            for (size_t i = 0; i < plans.size(); ++i) {
                auto& plan = plans[i];
                shipOrdinal.emplace(plan.state.id, i);
                if (const auto tail = recovery::projectTail(plan, active, now))
                    tailsByLocation[tail->currentLocation].emplace(i, *tail);
                ++result.allocationWork.itineraryBuilds;
            }
            result.allocationWork.itinerarySeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        }
        std::set<Key> participants;
        for (const auto& key : accounts)
            if (allocationPriorities.contains(key))
                participants.insert(key);
        predictAndPrevent(participants, Event{});
        return result;
    }
    if (knownSupplyEvents)
        predictAndPrevent(accounts, Event{});
    beforeFirstEvent = false;
    while (!queue.empty()) {
        const auto action = queue.top();
        queue.pop();
        if (action.type == ActionType::Departure && !pending.contains(action.index))
            continue;
        now = action.time;
        if (progress)
            progress(static_cast<size_t>(result.completed + result.failed));
        if (action.type == ActionType::Forecast) {
            predictAndPrevent(refresh, events[action.index]);
            refresh.clear();
            continue;
        }
        if (action.type == ActionType::Event) {
            const auto& event = events[action.index];
            const auto id = event.affectedObjectId;
            bool applied = false;
            switch (event.eventType) {
            case EventType::RouteBlocked:
            case EventType::WormholeCollapse:
            case EventType::PirateBlockade:
                if (const auto* edge = active.findRoute(id); edge && edge->isAvailable) {
                    applied = active.blockRoute(id);
                    repairPending(event, true);
                } else {
                    applied = active.blockRoute(id);
                }
                break;
            case EventType::RouteRiskIncrease: {
                const auto* edge = active.findRoute(id);
                const double oldRisk = edge ? edge->risk : 0;
                applied = active.updateRouteRisk(id, event.severity);
                if (!applied || event.severity <= oldRisk)
                    break;
                const auto started = std::chrono::steady_clock::now();
                std::vector<size_t> affected;
                for (const auto index : pending) {
                    const auto& ids = entries[index].routeIds;
                    if (std::find(ids.begin(), ids.end(), id) != ids.end())
                        affected.push_back(index);
                }
                for (const auto index : affected) {
                    const auto previous = entries[index];
                    const auto& mission = *missionById.at(previous.missionId);
                    pending.erase(index);
                    auto replacement = findReplacement(mission, &previous, {}, nullptr, nullptr, true);
                    if (replacement && replacement->routeIds != previous.routeIds) {
                        auto cost = [&](const ScheduledMission& row) {
                            double total = 0;
                            for (int edgeId : row.routeIds)
                                total += Dijkstra::edgeCost(*active.findRoute(edgeId), replacement->weights);
                            return total;
                        };
                        if (cost(*replacement) < cost(previous)) {
                            applyRepair(event, Decision::Reason::IncreasedRouteRisk, mission, previous, std::move(replacement));
                            continue;
                        }
                    }
                    pending.insert(index);
                }
                result.replanSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
                break;
            }
            case EventType::SpacecraftDestroyed:
            case EventType::SpacecraftMechanicalFailure:
                applied = ships.contains(id) && disabled.insert(id).second;
                if (applied)
                    repairPending(event, false);
                break;
            case EventType::EmergencyMedicalRequest:
            case EventType::HighPriorityMission:
                if (event.urgentMission && missionById.emplace(event.urgentMission->id, &*event.urgentMission).second) {
                    const auto& mission = *event.urgentMission;
                    if (now <= minimumHorizon)
                        result.activatedUrgentMissions.push_back(mission);
                    result.missionStatuses.emplace(mission.id, MissionStatus::Pending);
                    const auto start = std::chrono::steady_clock::now();
                    auto replacement = findReplacement(mission, nullptr);
                    if (!replacement)
                        replacement = displaceForUrgent(event, mission);
                    applyRepair(event, Decision::Reason::UrgentMission, mission, std::nullopt, std::move(replacement));
                    result.replanSeconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
                    applied = true;
                }
                break;
            case EventType::ResourceProductionDecrease:
            case EventType::ConstructionCompleted: {
                const auto location = balances.find(id);
                if (location == balances.end())
                    break;
                const auto resource = location->second.find(event.resourceType);
                if (resource == location->second.end() || !std::isfinite(event.severity))
                    break;
                auto& b = resource->second;
                b.advance(now, result);
                if (event.eventType == EventType::ConstructionCompleted)
                    b.production += std::max(0.0, event.severity);
                else
                    b.production *= 1 - std::clamp(event.severity, 0.0, 1.0);
                if (knownSupplyEvents && now <= minimumHorizon) {
                    if (refresh.empty())
                        queue.push({now, ActionType::Forecast, action.index});
                    refresh.emplace(id, event.resourceType);
                }
                applied = true;
                break;
            }
            default:
                break;
            }
            if (applied)
                ++result.eventsProcessed;
            else
                ++result.unsupportedEvents;
            continue;
        }

        const auto& entry = entries[action.index];
        const auto& mission = *missionById.at(entry.missionId);
        auto& status = result.missionStatuses.at(mission.id);
        auto fail = [&] {
            pending.erase(action.index);
            status = MissionStatus::Failed;
            ++result.failed;
        };
        auto shipIt = ships.find(entry.spacecraftId);
        if (action.type == ActionType::Arrival) {
            if (status != MissionStatus::InProgress)
                continue;
            const auto estimate = inFlight.at(entry.spacecraftId).estimate;
            inFlight.erase(entry.spacecraftId);
            if (disabled.contains(entry.spacecraftId)) {
                result.waste += mission.cargoAmount;
                fail();
                continue;
            }
            auto& ship = shipIt->second;
            ship.currentLocation = mission.destination;
            ship.availableAt = now;
            auto& destination = balances[mission.destination][mission.cargoType];
            destination.advance(now, result);
            const double accepted = std::min(mission.cargoAmount, destination.storage - destination.stock);
            destination.stock += accepted;
            result.deliveredAmount += accepted;
            result.deliveries.emplace(mission.id, SimulationResult::Delivery{accepted, now, estimate});
            result.waste += mission.cargoAmount - accepted;
            status = MissionStatus::Completed;
            ++result.completed;
            continue;
        }

        if (status != MissionStatus::Scheduled)
            continue;
        const auto origin = balances.find(mission.origin);
        if (shipIt == ships.end() || disabled.contains(entry.spacecraftId)) {
            fail();
            continue;
        }
        if (origin == balances.end() || !origin->second.contains(mission.cargoType)) {
            ++result.inventoryFailures;
            fail();
            continue;
        }
        auto& ship = shipIt->second;
        if (inFlight.contains(ship.id) || ship.availableAt > now) {
            if (ship.availableAt > now && ship.availableAt <= mission.deadline)
                queue.push({ship.availableAt, ActionType::Departure, action.index});
            else
                fail();
            continue;
        }
        if (ship.currentLocation != mission.origin) {
            fail();
            continue;
        }
        auto& stock = origin->second.at(mission.cargoType);
        stock.advance(now, result);
        if (stock.stock < mission.cargoAmount) {
            ++result.inventoryFailures;
            fail();
            continue;
        }

        RouteMetrics metrics;
        double arrival = 0;
        const bool originalFeasible = Dijkstra::measure(active, entry.routeIds,
            mission.origin, mission.destination, mission.cargoAmount, metrics) &&
            MissionScheduler::feasible(mission, ship, now, metrics, arrival);
        if (recheckAllDepartures || !originalFeasible) {
            ++result.replans;
            const auto start = std::chrono::steady_clock::now();
            bool found = false;
            for (const auto& weights : {entry.weights, RouteWeights{0, 1, 0}, RouteWeights{0, 0, 1}}) {
                const auto alternative = Dijkstra::findShortestPath(active, mission.origin,
                    mission.destination, weights, mission.cargoAmount);
                result.nodesExplored += alternative.nodesExplored;
                if (alternative.found &&
                    MissionScheduler::feasible(mission, ship, now, alternative.metrics, arrival)) {
                    metrics = alternative.metrics;
                    found = true;
                    if (alternative.routeIds != entry.routeIds)
                        ++result.rerouted;
                    break;
                }
            }
            result.replanSeconds += std::chrono::duration<double>(
                std::chrono::steady_clock::now() - start).count();
            if (!found) {
                fail();
                continue;
            }
        }
        stock.stock -= mission.cargoAmount;
        ship.currentFuel -= metrics.fuel;
        ship.availableAt = arrival;
        pending.erase(action.index);
        inFlight.emplace(ship.id, Flight{action.index, arrival, FlightEstimate::from(metrics, now, ship.speed)});
        status = MissionStatus::InProgress;
        // Account for the full route at departure. Closures affect future departures only.
        result.distance += metrics.distance;
        result.fuel += metrics.fuel;
        result.risk += metrics.risk;
        result.transportationCost += metrics.distance * ship.operatingCost;
        queue.push({arrival, ActionType::Arrival, action.index});
    }

    now = std::max(now, minimumHorizon);
    for (auto& [location, resources] : balances) {
        for (auto& [type, stock] : resources) {
            stock.advance(now, result);
            result.finalResources[location] += stock.stock;
            const double shortage = std::max(0.0, stock.target - stock.stock);
            result.remainingShortage += shortage;
            result.resourceOutcomes[location][type] = {shortage, stock.unserved};
        }
    }
    if (progress)
        progress(static_cast<size_t>(result.completed + result.failed));
    return result;
}
