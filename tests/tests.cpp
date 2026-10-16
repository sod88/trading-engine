// Dependency-free unit tests. Run with `ctest` or the qe_tests executable.
// The most important one is the look-ahead test: an order decided on bar N
// must fill at bar N+1's open, never at bar N's close.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

#include "qe/broker.hpp"
#include "qe/data_feed.hpp"
#include "qe/engine.hpp"
#include "qe/indicators.hpp"
#include "qe/metrics.hpp"
#include "qe/portfolio.hpp"
#include "qe/strategies/rsi_mean_reversion.hpp"
#include "qe/strategies/sma_crossover.hpp"

namespace {

int failures = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (!(cond)) {                                                                    \
            ++failures;                                                                   \
            std::cerr << "  FAILED: " #cond " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        }                                                                                 \
    } while (0)

bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }

qe::Bar bar(const std::string& date, double open, double close, const std::string& symbol = "TEST") {
    return {symbol, date, open, std::max(open, close), std::min(open, close), close, 1000};
}

const qe::CostModel kFree{0.0, 0.0, 0.0};

// Buys a fixed quantity on one chosen bar; records what it saw.
class BuyOnce : public qe::Strategy {
public:
    explicit BuyOnce(std::string date, long qty) : date_(std::move(date)), qty_(qty) {}
    std::string name() const override { return "BuyOnce"; }
    void on_bar(const qe::Bar& b, qe::StrategyContext& ctx) override {
        if (b.date == date_) ctx.submit_order({b.symbol, qe::Side::Buy, qty_, b.date});
    }
    void on_fill(const qe::Fill& f) override { fills.push_back(f); }
    std::vector<qe::Fill> fills;

private:
    std::string date_;
    long qty_;
};

void test_rolling_mean() {
    qe::RollingMean m(3);
    m.push(1);
    m.push(2);
    CHECK(!m.ready());
    m.push(3);
    CHECK(m.ready());
    CHECK(near(m.value(), 2.0));
    m.push(10);  // window is now 2,3,10
    CHECK(near(m.value(), 5.0));
}

void test_rsi() {
    qe::Rsi r(2);
    r.push(10);
    r.push(12);  // +2
    CHECK(!r.ready());
    r.push(11);  // -1: seed avg gain 1, avg loss 0.5
    CHECK(r.ready());
    CHECK(near(r.value(), 100.0 - 100.0 / 3.0));
    r.push(9);   // -2: Wilder -> gain 0.5, loss 1.25
    CHECK(near(r.value(), 100.0 - 100.0 / (1.0 + 0.4)));

    qe::Rsi up(3);
    for (double p : {1.0, 2.0, 3.0, 4.0}) up.push(p);
    CHECK(near(up.value(), 100.0));  // only gains
    qe::Rsi flat(3);
    for (int i = 0; i < 4; ++i) flat.push(5.0);
    CHECK(near(flat.value(), 50.0));  // no movement at all
}

void test_no_look_ahead() {
    qe::BarSeriesFeed feed({bar("2024-01-01", 100, 101), bar("2024-01-02", 105, 106),
                            bar("2024-01-03", 107, 108)});
    BuyOnce strat("2024-01-01", 10);
    qe::Portfolio pf(10'000);
    qe::SimulatedBroker broker(kFree);
    qe::Backtester bt(feed, strat, pf, broker);
    bt.run();

    CHECK(strat.fills.size() == 1);
    if (!strat.fills.empty()) {
        CHECK(strat.fills[0].date == "2024-01-02");    // next bar...
        CHECK(near(strat.fills[0].price, 105.0));      // ...at its open, not 101
    }
    CHECK(pf.position("TEST") == 10);
    CHECK(near(pf.cash(), 10'000 - 10 * 105.0));
    CHECK(near(pf.equity(), pf.cash() + 10 * 108.0));  // marked at last close
    CHECK(pf.equity_curve().size() == 3);
}

void test_costs_and_slippage() {
    qe::SimulatedBroker broker({0.01, 1.0, 10.0});  // 1c/share, $1 min, 10 bps
    broker.submit({"TEST", qe::Side::Buy, 50, "d0"});
    broker.submit({"TEST", qe::Side::Sell, 500, "d0"});
    auto fills = broker.on_bar(bar("d1", 100, 100));
    CHECK(fills.size() == 2);
    CHECK(near(fills[0].price, 100.10));    // buys pay up
    CHECK(near(fills[0].commission, 1.0));  // minimum commission applies
    CHECK(near(fills[1].price, 99.90));     // sells receive less
    CHECK(near(fills[1].commission, 5.0));
    CHECK(broker.pending() == 0);
}

void test_portfolio_pnl() {
    qe::Portfolio pf(10'000);
    pf.on_fill({"TEST", "d1", qe::Side::Buy, 10, 100.0, 1.0});
    pf.on_fill({"TEST", "d2", qe::Side::Buy, 10, 110.0, 1.0});
    CHECK(pf.position("TEST") == 20);
    pf.on_fill({"TEST", "d3", qe::Side::Sell, 20, 120.0, 1.0});
    CHECK(pf.position("TEST") == 0);
    CHECK(near(pf.realized_pnl(), 300.0));  // avg cost 105, sold at 120
    CHECK(near(pf.total_commission(), 3.0));
    CHECK(near(pf.cash(), 10'000 + 300 - 3));
    CHECK(pf.round_trip_pnls().size() == 1);
    CHECK(near(pf.round_trip_pnls()[0], 297.0));
}

void test_risk_checks() {
    qe::Portfolio pf(1'000);
    CHECK(pf.check({"TEST", qe::Side::Buy, 1, "d"}).has_value());  // no price yet
    pf.update_price("TEST", 100.0);
    CHECK(!pf.check({"TEST", qe::Side::Buy, 10, "d"}).has_value());
    CHECK(pf.check({"TEST", qe::Side::Buy, 11, "d"}).has_value());   // not enough cash
    CHECK(pf.check({"TEST", qe::Side::Sell, 1, "d"}).has_value());   // no shorting
    CHECK(pf.check({"TEST", qe::Side::Buy, 0, "d"}).has_value());
}

void test_sma_crossover_trades() {
    // Down, then up, then down again: should produce one buy and one sell.
    std::vector<qe::Bar> bars;
    std::vector<double> closes;
    for (int i = 0; i < 10; ++i) closes.push_back(100 - i);
    for (int i = 0; i < 15; ++i) closes.push_back(91 + 2 * i);
    for (int i = 0; i < 15; ++i) closes.push_back(119 - 3 * i);
    for (std::size_t i = 0; i < closes.size(); ++i) {
        char date[32];
        std::snprintf(date, sizeof date, "2024-01-%02zu", i + 1);
        bars.push_back(bar(date, closes[i], closes[i]));
    }
    qe::BarSeriesFeed feed(bars);
    qe::SmaCrossover strat(3, 6);
    qe::Portfolio pf(10'000);
    qe::SimulatedBroker broker(kFree);
    qe::Backtester bt(feed, strat, pf, broker);
    bt.run();

    const auto& fills = pf.fills();
    CHECK(fills.size() == 2);
    if (fills.size() == 2) {
        CHECK(fills[0].side == qe::Side::Buy);
        CHECK(fills[1].side == qe::Side::Sell);
    }
    CHECK(pf.position("TEST") == 0);
    CHECK(bt.rejected_orders() == 0);
}

void test_rsi_mean_reversion_trades() {
    // Flat-ish, a sharp sell-off (oversold -> buy), then a rebound (-> sell).
    std::vector<double> closes;
    for (int i = 0; i < 10; ++i) closes.push_back(100 + (i % 2));
    for (int i = 0; i < 6; ++i) closes.push_back(98 - 3 * i);
    for (int i = 0; i < 10; ++i) closes.push_back(85 + 3 * i);
    std::vector<qe::Bar> bars;
    for (std::size_t i = 0; i < closes.size(); ++i) {
        char date[32];
        std::snprintf(date, sizeof date, "2024-01-%02zu", i + 1);
        bars.push_back(bar(date, closes[i], closes[i]));
    }
    qe::BarSeriesFeed feed(bars);
    qe::RsiMeanReversion strat(5, 30, 50);
    qe::Portfolio pf(10'000);
    qe::SimulatedBroker broker(kFree);
    qe::Backtester bt(feed, strat, pf, broker);
    bt.run();

    const auto& fills = pf.fills();
    CHECK(fills.size() == 2);
    if (fills.size() == 2) {
        CHECK(fills[0].side == qe::Side::Buy);
        CHECK(fills[1].side == qe::Side::Sell);
        CHECK(fills[1].price > fills[0].price);  // bought the dip, sold the bounce
    }
    CHECK(pf.position("TEST") == 0);
    CHECK(bt.rejected_orders() == 0);

    bool threw = false;
    try { qe::RsiMeanReversion bad(14, 60, 40); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

void test_metrics() {
    // Hold one share bought with all the cash, then move its price around.
    qe::Portfolio pf(100);
    pf.on_fill({"X", "d0", qe::Side::Buy, 1, 100.0, 0.0});  // cash 0, 1 share
    pf.update_price("X", 110.0); pf.record("d1");
    pf.update_price("X", 99.0);  pf.record("d2");
    pf.update_price("X", 121.0); pf.record("d3");
    auto r = qe::compute_report(pf);
    CHECK(near(r.total_return, 0.21));
    CHECK(near(r.max_drawdown, 0.10));  // 110 -> 99
    CHECK(r.drawdown_peak_date == "d1");
    CHECK(r.drawdown_trough_date == "d2");
    CHECK(near(r.exposure, 1.0));
}

void test_csv_loading() {
    const std::string path = "qe_test_data.csv";
    {
        std::ofstream f(path);
        f << "Date,Open,High,Low,Close,Adj Close,Volume\n"
             "2024-01-03,11,12,10,11.5,11.5,100\n"
             "2024-01-02,10,11,9,10.5,10.5,100\n"
             "2024-01-04,null,null,null,null,null,null\n";
    }
    auto bars = qe::load_csv(path, "ABC");
    CHECK(bars.size() == 2);  // null row skipped
    CHECK(bars[0].date == "2024-01-02");  // sorted
    CHECK(near(bars[1].close, 11.5));
    CHECK(bars[0].symbol == "ABC");
    std::remove(path.c_str());
}

void test_adjusted_close() {
    // A 2:1 split-style gap in Close that Adj Close already smooths out.
    const std::string path = "qe_test_adj.csv";
    {
        std::ofstream f(path);
        f << "Date,Open,High,Low,Close,Adj Close,Volume\n"
             "2024-01-02,200,210,190,200,100,10\n"
             "2024-01-03,102,104,100,102,102,10\n";
    }
    auto bars = qe::load_csv(path, "ABC");
    CHECK(bars.size() == 2);
    if (bars.size() == 2) {
        CHECK(near(bars[0].close, 100.0));
        CHECK(near(bars[0].open, 100.0));
        CHECK(near(bars[0].high, 105.0));
        CHECK(near(bars[0].low, 95.0));
        CHECK(near(bars[1].close, 102.0));  // factor 1 when they match
    }
    std::remove(path.c_str());
}

void test_symbol_from_path() {
    CHECK(qe::symbol_from_path("data/spy.csv") == "SPY");
    CHECK(qe::symbol_from_path("/tmp/BRK-B.csv") == "BRK-B");
    CHECK(qe::symbol_from_path("sample.csv") == "SAMPLE");
}

void test_merge_series() {
    auto merged = qe::merge_series({
        {bar("2024-01-01", 1, 1, "BBB"), bar("2024-01-03", 1, 1, "BBB")},
        {bar("2024-01-01", 1, 1, "AAA"), bar("2024-01-02", 1, 1, "AAA")},
    });
    CHECK(merged.size() == 4);
    if (merged.size() == 4) {
        CHECK(merged[0].symbol == "AAA" && merged[0].date == "2024-01-01");
        CHECK(merged[1].symbol == "BBB" && merged[1].date == "2024-01-01");
        CHECK(merged[2].date == "2024-01-02");
        CHECK(merged[3].date == "2024-01-03");
    }
    bool threw = false;
    try {
        qe::merge_series({{bar("2024-01-01", 1, 1, "AAA")}, {bar("2024-01-01", 1, 1, "AAA")}});
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

// Buys `qty` of every symbol it sees on the first date.
class BuyEverything : public qe::Strategy {
public:
    explicit BuyEverything(long qty) : qty_(qty) {}
    std::string name() const override { return "BuyEverything"; }
    void on_bar(const qe::Bar& b, qe::StrategyContext& ctx) override {
        if (b.date == "2024-01-01") ctx.submit_order({b.symbol, qe::Side::Buy, qty_, b.date});
    }

private:
    long qty_;
};

void test_multi_symbol_run() {
    qe::BarSeriesFeed feed(qe::merge_series({
        {bar("2024-01-01", 10, 10, "AAA"), bar("2024-01-02", 11, 12, "AAA"), bar("2024-01-03", 12, 13, "AAA")},
        {bar("2024-01-01", 20, 20, "BBB"), bar("2024-01-02", 21, 22, "BBB"), bar("2024-01-03", 22, 24, "BBB")},
    }));
    BuyEverything strat(10);
    qe::Portfolio pf(1'000);
    qe::SimulatedBroker broker(kFree);
    qe::Backtester bt(feed, strat, pf, broker);
    bt.run();

    CHECK(bt.bars_processed() == 6);
    CHECK(pf.equity_curve().size() == 3);  // one point per date, not per bar
    CHECK(pf.position("AAA") == 10);
    CHECK(pf.position("BBB") == 10);
    CHECK(near(pf.cash(), 1'000 - 10 * 11.0 - 10 * 21.0));
    CHECK(near(pf.equity(), pf.cash() + 10 * 13.0 + 10 * 24.0));
    if (pf.equity_curve().size() == 3) {
        // Day 2 is recorded after both symbols have filled and closed.
        CHECK(pf.equity_curve()[1].date == "2024-01-02");
        CHECK(near(pf.equity_curve()[1].equity, pf.cash() + 10 * 12.0 + 10 * 22.0));
    }
}

void test_same_day_buys_cannot_overspend() {
    // Each order alone fits in the cash, both together do not.
    qe::BarSeriesFeed feed(qe::merge_series({
        {bar("2024-01-01", 10, 10, "AAA"), bar("2024-01-02", 10, 10, "AAA")},
        {bar("2024-01-01", 10, 10, "BBB"), bar("2024-01-02", 10, 10, "BBB")},
    }));
    BuyEverything strat(60);
    qe::Portfolio pf(1'000);
    qe::SimulatedBroker broker(kFree);
    qe::Backtester bt(feed, strat, pf, broker);
    bt.run();

    CHECK(bt.rejected_orders() == 1);
    CHECK(pf.position("AAA") == 60);
    CHECK(pf.position("BBB") == 0);
    CHECK(pf.cash() >= 0);
}

void test_sma_crossover_per_symbol() {
    // AAA trends up, BBB trends down: only AAA should be bought, with half
    // of the allocation.
    std::vector<qe::Bar> up, down;
    for (int i = 0; i < 20; ++i) {
        char date[32];
        std::snprintf(date, sizeof date, "2024-01-%02d", i + 1);
        up.push_back(bar(date, 100 + i, 100 + i, "AAA"));
        down.push_back(bar(date, 100 - i, 100 - i, "BBB"));
    }
    qe::BarSeriesFeed feed(qe::merge_series({up, down}));
    qe::SmaCrossover strat(2, 4, 1.0, 2);
    qe::Portfolio pf(10'000);
    qe::SimulatedBroker broker(kFree);
    qe::Backtester bt(feed, strat, pf, broker);
    bt.run();

    CHECK(pf.position("BBB") == 0);
    CHECK(pf.position("AAA") > 0);
    if (!pf.fills().empty()) {
        const auto& f = pf.fills()[0];
        CHECK(f.symbol == "AAA");
        CHECK(f.quantity * f.price <= 5'000 + 1e-9 + f.price);  // ~half the equity
        CHECK(f.quantity * f.price > 4'500);
    }
}

void test_rsi_mean_reversion_per_symbol() {
    // AAA keeps falling (oversold), BBB keeps rising: only AAA should be
    // bought, with half of the allocation.
    std::vector<qe::Bar> falling, rising;
    for (int i = 0; i < 20; ++i) {
        char date[32];
        std::snprintf(date, sizeof date, "2024-01-%02d", i + 1);
        falling.push_back(bar(date, 100 - i, 100 - i, "AAA"));
        rising.push_back(bar(date, 100 + i, 100 + i, "BBB"));
    }
    qe::BarSeriesFeed feed(qe::merge_series({falling, rising}));
    qe::RsiMeanReversion strat(3, 30, 50, 1.0, 2);
    qe::Portfolio pf(10'000);
    qe::SimulatedBroker broker(kFree);
    qe::Backtester bt(feed, strat, pf, broker);
    bt.run();

    CHECK(pf.position("BBB") == 0);
    CHECK(pf.position("AAA") > 0);
    if (!pf.fills().empty()) {
        const auto& f = pf.fills()[0];
        CHECK(f.symbol == "AAA");
        CHECK(f.quantity * f.price <= 5'000 + 1e-9 + f.price);  // ~half the equity
        CHECK(f.quantity * f.price > 4'500);
    }
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"rolling mean", test_rolling_mean},
        {"RSI", test_rsi},
        {"no look-ahead", test_no_look_ahead},
        {"costs and slippage", test_costs_and_slippage},
        {"portfolio P&L", test_portfolio_pnl},
        {"risk checks", test_risk_checks},
        {"SMA crossover trades", test_sma_crossover_trades},
        {"RSI mean reversion trades", test_rsi_mean_reversion_trades},
        {"metrics", test_metrics},
        {"CSV loading", test_csv_loading},
        {"adjusted close", test_adjusted_close},
        {"symbol from file name", test_symbol_from_path},
        {"merge series", test_merge_series},
        {"multi-symbol run", test_multi_symbol_run},
        {"same-day buys can't overspend", test_same_day_buys_cannot_overspend},
        {"SMA crossover per symbol", test_sma_crossover_per_symbol},
        {"RSI mean reversion per symbol", test_rsi_mean_reversion_per_symbol},
    };
    for (const auto& [name, fn] : tests) {
        const int before = failures;
        fn();
        std::cout << (failures == before ? "[ OK ] " : "[FAIL] ") << name << '\n';
    }
    std::cout << (failures ? "\nSome tests FAILED\n" : "\nAll tests passed\n");
    return failures ? 1 : 0;
}
