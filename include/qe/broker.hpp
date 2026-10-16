#pragma once
// Simulated broker. Orders wait until the next bar and fill at its open,
// adjusted for slippage, with a per-share commission.

#include <cstddef>
#include <vector>

#include "qe/event.hpp"

namespace qe {

struct CostModel {
    double commission_per_share = 0.005;  // USD per share
    double min_commission = 1.0;          // USD per order
    double slippage_bps = 1.0;            // 1 bp = 0.01%, always against you
};

class SimulatedBroker {
public:
    explicit SimulatedBroker(CostModel costs = {}) : costs_(costs) {}

    void submit(const Order& order) { pending_.push_back(order); }

    // Execute every pending order for bar.symbol at this bar's open.
    std::vector<Fill> on_bar(const Bar& bar);

    std::size_t pending() const { return pending_.size(); }
    const CostModel& costs() const { return costs_; }

private:
    CostModel costs_;
    std::vector<Order> pending_;
};

}  // namespace qe
