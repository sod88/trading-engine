#pragma once
// RSI mean reversion, long-only, run independently per symbol.
//   RSI < entry and flat  -> buy `allocation / symbols` of equity (oversold)
//   RSI > exit  and long  -> sell everything (price has reverted)
// The opposite bet to SmaCrossover: it buys weakness instead of strength, so
// the two tend to do well in different market regimes.

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_map>

#include "qe/indicators.hpp"
#include "qe/strategy.hpp"

namespace qe {

class RsiMeanReversion : public Strategy {
public:
    RsiMeanReversion(std::size_t period, double entry = 30.0, double exit = 50.0,
                     double allocation = 0.95, std::size_t symbols = 1)
        : period_(period), entry_(entry), exit_(exit), allocation_(allocation),
          slice_(allocation / static_cast<double>(symbols)) {
        if (period == 0) throw std::invalid_argument("RSI period must be > 0");
        if (!(0.0 < entry && entry < exit && exit < 100.0))
            throw std::invalid_argument("RSI levels must satisfy 0 < entry < exit < 100");
        if (allocation <= 0.0 || allocation > 1.0)
            throw std::invalid_argument("allocation must be in (0, 1]");
        if (symbols == 0) throw std::invalid_argument("symbols must be > 0");
    }

    std::string name() const override {
        return "RSI mean reversion(" + std::to_string(period_) + "," + fmt(entry_) + "," + fmt(exit_) + ")";
    }

    void on_bar(const Bar& bar, StrategyContext& ctx) override {
        auto it = rsi_.find(bar.symbol);
        if (it == rsi_.end()) it = rsi_.emplace(bar.symbol, Rsi(period_)).first;
        Rsi& rsi = it->second;
        rsi.push(bar.close);
        if (!rsi.ready()) return;  // warm-up period

        const double value = rsi.value();
        const long pos = ctx.position(bar.symbol);

        if (value < entry_ && pos == 0) {
            // Same sizing rule as SmaCrossover: size off the close, keep a
            // reserve for the gap to tomorrow's open, never more than the cash.
            const double budget = std::min(ctx.equity() * slice_, ctx.cash() * allocation_);
            const long qty = static_cast<long>(std::floor(budget / bar.close));
            if (qty > 0) ctx.submit_order({bar.symbol, Side::Buy, qty, bar.date});
        } else if (value > exit_ && pos > 0) {
            ctx.submit_order({bar.symbol, Side::Sell, pos, bar.date});
        }
    }

private:
    static std::string fmt(double v) {
        std::string s = std::to_string(v);
        s.erase(s.find_last_not_of('0') + 1);
        if (s.back() == '.') s.pop_back();
        return s;
    }

    std::size_t period_;
    double entry_;
    double exit_;
    double allocation_;
    double slice_;
    std::unordered_map<std::string, Rsi> rsi_;
};

}  // namespace qe
