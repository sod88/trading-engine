#include "qe/broker.hpp"

#include <algorithm>

namespace qe {

std::vector<Fill> SimulatedBroker::on_bar(const Bar& bar) {
    std::vector<Fill> fills;
    std::vector<Order> still_pending;

    for (const Order& order : pending_) {
        if (order.symbol != bar.symbol) {
            still_pending.push_back(order);
            continue;
        }
        const double slip = bar.open * costs_.slippage_bps / 10'000.0;
        Fill fill;
        fill.symbol = order.symbol;
        fill.date = bar.date;
        fill.side = order.side;
        fill.quantity = order.quantity;
        fill.price = order.side == Side::Buy ? bar.open + slip : bar.open - slip;
        fill.commission = std::max(costs_.min_commission,
                                   costs_.commission_per_share * static_cast<double>(order.quantity));
        fills.push_back(fill);
    }
    pending_ = std::move(still_pending);
    return fills;
}

}  // namespace qe
