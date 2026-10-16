#include "qe/engine.hpp"

#include <iomanip>
#include <iostream>

namespace qe {

void Backtester::run() {
    while (auto bar = feed_.next()) {
        queue_.push(MarketEvent{*bar});
        while (!queue_.empty()) {
            Event event = std::move(queue_.front());
            queue_.pop();
            dispatch(event);
        }
    }
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
    portfolio_.record(bar.date);
    ++bars_;
}

void Backtester::handle(const OrderEvent& e) {
    if (auto reason = portfolio_.check(e.order)) {
        ++rejected_;
        if (verbose_)
            std::cout << e.order.created_date << "  REJECTED " << to_string(e.order.side) << ' '
                      << e.order.quantity << ' ' << e.order.symbol << ": " << *reason << '\n';
        return;
    }
    broker_.submit(e.order);
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
