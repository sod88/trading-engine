#pragma once
// The backtester: an event loop that wires the data feed, strategy,
// portfolio and broker together.
//
// Per bar, in this order:
//   1. the broker fills orders from the previous bar at this bar's open
//   2. the portfolio marks positions to this bar's close
//   3. the strategy sees the closed bar and may submit orders
//   4. orders pass the portfolio's risk check and go to the broker
//   5. the equity curve records the day, once every symbol's bar for that
//      date has been processed
// With several symbols the feed hands out one bar per symbol per date, in
// date order. Because orders created in step 3 can only fill in step 1 of the next bar,
// a strategy can never trade on a price it hasn't seen yet.

#include <cstddef>
#include <queue>
#include <string>

#include "qe/broker.hpp"
#include "qe/data_feed.hpp"
#include "qe/event.hpp"
#include "qe/portfolio.hpp"
#include "qe/strategy.hpp"

namespace qe {

class Backtester : private StrategyContext {
public:
    Backtester(DataFeed& feed, Strategy& strategy, Portfolio& portfolio, SimulatedBroker& broker)
        : feed_(feed), strategy_(strategy), portfolio_(portfolio), broker_(broker) {}

    void run();

    void set_verbose(bool v) { verbose_ = v; }
    std::size_t bars_processed() const { return bars_; }
    std::size_t rejected_orders() const { return rejected_; }

private:
    void dispatch(const Event& event);
    void handle(const MarketEvent& e);
    void handle(const OrderEvent& e);
    void handle(const FillEvent& e);
    double reserved_cash() const;

    // StrategyContext
    long position(const std::string& symbol) const override { return portfolio_.position(symbol); }
    double cash() const override { return portfolio_.cash(); }
    double equity() const override { return portfolio_.equity(); }
    void submit_order(const Order& order) override { queue_.push(OrderEvent{order}); }

    DataFeed& feed_;
    Strategy& strategy_;
    Portfolio& portfolio_;
    SimulatedBroker& broker_;
    std::queue<Event> queue_;
    bool verbose_ = false;
    std::size_t bars_ = 0;
    std::size_t rejected_ = 0;
    std::string current_date_;
};

}  // namespace qe
