#include "qe/engine.hpp"

#include <iomanip>
#include <iostream>

namespace qe {

void Backtester::run() {
    while (auto bar = feed_.next()) {
        // A new date means every bar of the previous one is in: record it.
        if (!current_date_.empty() && bar->date != current_date_) portfolio_.record(current_date_);
        current_date_ = bar->date;
        queue_.push(MarketEvent{*bar});
        while (!queue_.empty()) {
            Event event = std::move(queue_.front());
            queue_.pop();
            dispatch(event);
        }
    }
    if (!current_date_.empty()) portfolio_.record(current_date_);
}

void Backtester::dispatch(const Event& event) {
    std::visit([this](const auto& e) { handle(e); }, event);
}

void Backtester::handle(const MarketEvent& e) {
    const Bar& bar = e.bar;
    // Fills are handled immediately (not queued) so the strategy sees its
    // updated position before it looks at this bar.
    for (const Fill& fill : broker_.on_bar(bar)) handle(FillEvent{fill});
    portfolio_.update_price(bar.symbol, bar.close);
    strategy_.on_bar(bar, *this);
    ++bars_;
}

void Backtester::handle(const OrderEvent& e) {
    if (auto reason = portfolio_.check(e.order, reserved_cash())) {
        ++rejected_;
        if (verbose_)
            std::cout << e.order.created_date << "  REJECTED " << to_string(e.order.side) << ' '
                      << e.order.quantity << ' ' << e.order.symbol << ": " << *reason << '\n';
        return;
    }
    broker_.submit(e.order);
}

// Estimated cost of buy orders still waiting at the broker.
double Backtester::reserved_cash() const {
    double reserved = 0.0;
    for (const Order& o : broker_.pending_orders()) {
        if (o.side != Side::Buy) continue;
        if (auto price = portfolio_.last_price(o.symbol)) reserved += static_cast<double>(o.quantity) * *price;
    }
    return reserved;
}

void Backtester::handle(const FillEvent& e) {
    portfolio_.on_fill(e.fill);
    strategy_.on_fill(e.fill);
    if (verbose_) {
        std::cout << e.fill.date << "  " << std::left << std::setw(4) << to_string(e.fill.side)
                  << std::right << std::setw(8) << e.fill.quantity << ' ' << e.fill.symbol << " @ "
                  << std::fixed << std::setprecision(2) << e.fill.price
                  << "  (commission " << e.fill.commission << ")\n";
    }
}

}  // namespace qe
