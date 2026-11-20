#include "TestSupport.h"
#include "../algorithms/LogisticsPlanner.h"

namespace regression {
void testAllocationAndForecast() {
    const auto graph = makeFlightFixture().graph;
    const auto routed = ResourceAllocator::allocateWithRoutes({{1, 20, 0, 1}, {2, 0, 6, 2}}, graph);
    require(routed.transfers.size() == 1 && routed.transfers[0].fromPlanet == 1 &&
        routed.transfers[0].routeIds == std::vector<int>{1} && routed.totalDelivered == 6,
        "Reverse search must find an inbound supplier and preserve the selected edge ID.");
    ResourceDemand receiver{2, 10000, 10001, 3, ResourceType::Food, 300, 700};
    const auto aid = LogisticsPlanner::createMissions(routed, {receiver}, ResourceType::Food, 30);
    require(aid.size() == 1 && aid[0].deadline == 25, "The known depletion example is 25 days.");
}
}
