#pragma once
// Classic moving-average crossover, long-only, run independently per symbol.
//   fast SMA > slow SMA and flat  -> buy `allocation / symbols` of equity
//   fast SMA < slow SMA and long  -> sell everything
// With several symbols each one gets an equal slice of the allocation.
// It is a deliberately simple strategy for exercising the engine end to end,
// not a strategy anyone should expect to make money with.

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "qe/indicators.hpp"
#include "qe/strategy.hpp"

namespace qe {

class SmaCrossover : public Strategy {
public:
    SmaCrossover(std::size_t fast, std::size_t slow, double allocation = 0.95, std::size_t symbols = 1)
        : fast_window_(fast), slow_window_(slow), allocation_(allocation),
          slice_(allocation / static_cast<double>(symbols)) {
        if (fast == 0) throw std::invalid_argument("fast window must be > 0");
        if (fast >= slow) throw std::invalid_argument("fast window must be shorter than slow window");
        if (allocation <= 0.0 || allocation > 1.0)
            throw std::invalid_argument("allocation must be in (0, 1]");
        if (symbols == 0) throw std::invalid_argument("symbols must be > 0");
    }

    std::string name() const override {
        return "SMA crossover(" + std::to_string(fast_window_) + "," + std::to_string(slow_window_) + ")";
    }

    void on_bar(const Bar& bar, StrategyContext& ctx) override {
        auto it = state_.find(bar.symbol);
        if (it == state_.end())
            it = state_.emplace(bar.symbol, State{RollingMean(fast_window_), RollingMean(slow_window_)}).first;
        State& st = it->second;
        st.fast.push(bar.close);
        st.slow.push(bar.close);
        if (!st.slow.ready()) return;  // warm-up period

        const double fast = st.fast.value();
        const double slow = st.slow.value();
        const long pos = ctx.position(bar.symbol);

        if (fast > slow && pos == 0) {
            // Size off today's close; the order fills at tomorrow's open. The
            // allocation below 100% leaves room for an overnight gap up. If
            // other symbols' winners have grown past their slice, there may be
            // less cash than a full slice: buy what the cash allows.
            const double budget = std::min(ctx.equity() * slice_, ctx.cash() * allocation_);
            const long qty = static_cast<long>(std::floor(budget / bar.close));
            if (qty > 0) ctx.submit_order({bar.symbol, Side::Buy, qty, bar.date});
        } else if (fast < slow && pos > 0) {
            ctx.submit_order({bar.symbol, Side::Sell, pos, bar.date});
        }
    }

private:
    struct State {
        RollingMean fast;
        RollingMean slow;
    };
    std::size_t fast_window_;
    std::size_t slow_window_;
    double allocation_;
    double slice_;
    std::unordered_map<std::string, State> state_;
};

}  // namespace qe
