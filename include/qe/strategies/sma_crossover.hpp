#pragma once
// Classic moving-average crossover, long-only.
//   fast SMA > slow SMA and flat  -> buy `allocation` of equity
//   fast SMA < slow SMA and long  -> sell everything
// It is a deliberately simple strategy for exercising the engine end to end,
// not a strategy anyone should expect to make money with.

#include <cmath>
#include <stdexcept>
#include <string>

#include "qe/indicators.hpp"
#include "qe/strategy.hpp"

namespace qe {

class SmaCrossover : public Strategy {
public:
    SmaCrossover(std::size_t fast, std::size_t slow, double allocation = 0.95)
        : fast_(fast), slow_(slow), allocation_(allocation) {
        if (fast >= slow) throw std::invalid_argument("fast window must be shorter than slow window");
        if (allocation <= 0.0 || allocation > 1.0)
            throw std::invalid_argument("allocation must be in (0, 1]");
    }

    std::string name() const override {
        return "SMA crossover(" + std::to_string(fast_.window()) + "," +
               std::to_string(slow_.window()) + ")";
    }

    void on_bar(const Bar& bar, StrategyContext& ctx) override {
        fast_.push(bar.close);
        slow_.push(bar.close);
        if (!slow_.ready()) return;  // warm-up period

        const double fast = fast_.value();
        const double slow = slow_.value();
        const long pos = ctx.position(bar.symbol);

        if (fast > slow && pos == 0) {
            // Size off today's close; the order fills at tomorrow's open. The
            // allocation below 100% leaves room for an overnight gap up.
            const long qty = static_cast<long>(std::floor(ctx.equity() * allocation_ / bar.close));
            if (qty > 0) ctx.submit_order({bar.symbol, Side::Buy, qty, bar.date});
        } else if (fast < slow && pos > 0) {
            ctx.submit_order({bar.symbol, Side::Sell, pos, bar.date});
        }
    }

private:
    RollingMean fast_;
    RollingMean slow_;
    double allocation_;
};

}  // namespace qe
