#include "qe/portfolio.hpp"

#include <stdexcept>

namespace qe {

Portfolio::Portfolio(double initial_cash) : initial_cash_(initial_cash), cash_(initial_cash) {
    if (initial_cash <= 0) throw std::invalid_argument("initial cash must be positive");
}

std::optional<std::string> Portfolio::check(const Order& order, double reserved_cash) const {
    if (order.quantity <= 0) return "quantity must be positive";

    auto price_it = last_price_.find(order.symbol);
    if (price_it == last_price_.end()) return "no price known for " + order.symbol;

    if (order.side == Side::Buy) {
        const double estimated_cost = static_cast<double>(order.quantity) * price_it->second;
        if (estimated_cost > cash_ - reserved_cash) return "insufficient cash";
    } else {
        if (order.quantity > position(order.symbol)) return "short selling is not allowed";
    }
    return std::nullopt;
}

void Portfolio::on_fill(const Fill& fill) {
    Position& pos = positions_[fill.symbol];
    const double qty = static_cast<double>(fill.quantity);

    cash_ -= fill.commission;
    total_commission_ += fill.commission;
    pos.trip_commission += fill.commission;

    if (fill.side == Side::Buy) {
        cash_ -= qty * fill.price;
        const double new_qty = static_cast<double>(pos.quantity) + qty;
        pos.avg_price = (pos.avg_price * static_cast<double>(pos.quantity) + fill.price * qty) / new_qty;
        pos.quantity += fill.quantity;
    } else {
        if (fill.quantity > pos.quantity)
            throw std::logic_error("fill would create a short position in " + fill.symbol);
        cash_ += qty * fill.price;
        const double pnl = (fill.price - pos.avg_price) * qty;
        realized_pnl_ += pnl;
        pos.trip_gross_pnl += pnl;
        pos.quantity -= fill.quantity;
        if (pos.quantity == 0) {
            round_trips_.push_back(pos.trip_gross_pnl - pos.trip_commission);
            pos = Position{};
        }
    }
    fills_.push_back(fill);
}

void Portfolio::update_price(const std::string& symbol, double price) { last_price_[symbol] = price; }

void Portfolio::record(const std::string& date) { equity_curve_.push_back({date, equity(), cash_}); }

double Portfolio::equity() const {
    double value = cash_;
    for (const auto& [symbol, pos] : positions_) {
        if (pos.quantity == 0) continue;
        auto it = last_price_.find(symbol);
        const double price = it != last_price_.end() ? it->second : pos.avg_price;
        value += static_cast<double>(pos.quantity) * price;
    }
    return value;
}

std::optional<double> Portfolio::last_price(const std::string& symbol) const {
    auto it = last_price_.find(symbol);
    if (it == last_price_.end()) return std::nullopt;
    return it->second;
}

long Portfolio::position(const std::string& symbol) const {
    auto it = positions_.find(symbol);
    return it == positions_.end() ? 0 : it->second.quantity;
}

}  // namespace qe
