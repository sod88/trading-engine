#pragma once
// Tracks cash, positions and P&L, records the equity curve, and runs the
// pre-trade risk checks every order must pass before reaching the broker.

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "qe/event.hpp"

namespace qe {

struct Position {
    long quantity = 0;
    double avg_price = 0.0;
    // Running totals for the current round trip (flat -> long -> flat).
    double trip_gross_pnl = 0.0;
    double trip_commission = 0.0;
};

struct EquityPoint {
    std::string date;
    double equity = 0.0;
    double cash = 0.0;
};

class Portfolio {
public:
    explicit Portfolio(double initial_cash);

    // Pre-trade risk check. Returns a rejection reason, or nullopt if allowed.
    // Phase 1 rules: positive quantity, a known price, no shorting, and no
    // buying with money you don't have (estimated at the last close).
    // `reserved_cash` is money already promised to buy orders that haven't
    // filled yet, so several buys on the same day can't overspend together.
    std::optional<std::string> check(const Order& order, double reserved_cash = 0.0) const;

    void on_fill(const Fill& fill);
    void update_price(const std::string& symbol, double price);
    void record(const std::string& date);  // append a point to the equity curve

    double initial_cash() const { return initial_cash_; }
    double cash() const { return cash_; }
    double equity() const;
    std::optional<double> last_price(const std::string& symbol) const;
    long position(const std::string& symbol) const;
    double realized_pnl() const { return realized_pnl_; }
    double total_commission() const { return total_commission_; }

    const std::vector<EquityPoint>& equity_curve() const { return equity_curve_; }
    const std::vector<Fill>& fills() const { return fills_; }
    // Net P&L (after commissions) of each completed round trip.
    const std::vector<double>& round_trip_pnls() const { return round_trips_; }

private:
    double initial_cash_;
    double cash_;
    double realized_pnl_ = 0.0;
    double total_commission_ = 0.0;
    std::unordered_map<std::string, Position> positions_;
    std::unordered_map<std::string, double> last_price_;
    std::vector<EquityPoint> equity_curve_;
    std::vector<Fill> fills_;
    std::vector<double> round_trips_;
};

}  // namespace qe
