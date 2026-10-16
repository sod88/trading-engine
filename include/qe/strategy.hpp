#pragma once
// The strategy interface. Strategies only see the current bar and what the
// context exposes; they never touch the broker or the data feed directly.

#include <string>

#include "qe/event.hpp"

namespace qe {

// What a strategy is allowed to see and do. The backtester implements this;
// a live engine would implement the same interface.
class StrategyContext {
public:
    virtual ~StrategyContext() = default;
    virtual long position(const std::string& symbol) const = 0;
    virtual double cash() const = 0;
    virtual double equity() const = 0;
    virtual void submit_order(const Order& order) = 0;
};

class Strategy {
public:
    virtual ~Strategy() = default;
    virtual std::string name() const = 0;
    // Called once per bar, after the bar has closed. Orders submitted here
    // are executed on the NEXT bar's open (no look-ahead).
    virtual void on_bar(const Bar& bar, StrategyContext& ctx) = 0;
    virtual void on_fill(const Fill& /*fill*/) {}
};

}  // namespace qe
