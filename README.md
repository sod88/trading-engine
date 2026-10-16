# Quant Engine — Phase 1

An event-driven backtester in C++20. No external dependencies: a compiler
and CMake 3.20+ are enough on macOS, Linux or Windows.

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure     # or ./build/qe_tests

./build/backtest data/sample.csv
./build/backtest data/sample.csv --fast 10 --slow 30 --verbose
```

On Windows with Visual Studio the executables land in `build\Release\`.

Options: `--symbol NAME`, `--fast N`, `--slow N`, `--cash AMOUNT`,
`--commission PER_SHARE`, `--slippage-bps BPS`, `--out DIR`, `--verbose`.
Each run writes `equity.csv` and `trades.csv` to the output directory
(`results/` by default) so you can chart them in Python, Excel, anything.

## Data

`data/sample.csv` is **synthetic** (regime-switching random walk, made by
`tools/generate_sample_data.py`). It exists to test the plumbing, not to
judge strategies. For real data, download daily history for any ticker from
Yahoo Finance as CSV and run:

```bash
./build/backtest ~/Downloads/SPY.csv --symbol SPY
```

The loader matches columns by name, skips `null` rows and sorts by date.

## How it works

```
CSV -> DataFeed -> MarketEvent -> Backtester ----> Strategy.on_bar()
                                     ^                 |
                       FillEvent     |                 v OrderEvent
               SimulatedBroker <-----+---- Portfolio.check() (risk)
```

For every bar the engine:

1. fills orders from the previous bar at **this bar's open** (+ slippage, + commission)
2. marks positions to this bar's close
3. calls `strategy.on_bar()`, which may submit orders
4. runs each order through the portfolio's risk checks, then hands it to the broker
5. records equity

A decision made on a bar's close can only execute on the next bar, which is
what keeps the backtest free of look-ahead bias. `tests/tests.cpp` checks this
explicitly.

| File | Role |
|---|---|
| `include/qe/event.hpp` | `Bar`, `Order`, `Fill` and the event variant |
| `include/qe/data_feed.hpp`, `src/data_feed.cpp` | data feeds and the CSV loader |
| `include/qe/strategy.hpp` | `Strategy` and `StrategyContext` interfaces |
| `include/qe/strategies/sma_crossover.hpp` | the sample strategy |
| `include/qe/indicators.hpp` | incremental indicators (`RollingMean`) |
| `include/qe/portfolio.hpp`, `src/portfolio.cpp` | cash, positions, P&L, risk checks |
| `include/qe/broker.hpp`, `src/broker.cpp` | simulated execution and costs |
| `include/qe/engine.hpp`, `src/engine.cpp` | the event loop |
| `include/qe/metrics.hpp`, `src/metrics.cpp` | Sharpe, drawdown, CAGR, win rate... |
| `app/main.cpp` | command-line runner |

## Writing your own strategy

```cpp
class MyStrategy : public qe::Strategy {
public:
    std::string name() const override { return "Mine"; }
    void on_bar(const qe::Bar& bar, qe::StrategyContext& ctx) override {
        // look at bar, ctx.position(bar.symbol), ctx.equity() ...
        // ctx.submit_order({bar.symbol, qe::Side::Buy, 100, bar.date});
    }
};
```

Then swap it in for `SmaCrossover` in `app/main.cpp`.

## Phase 1 limitations (on purpose)

- Single symbol, daily bars, market orders only, long-only.
- Orders are sized from the close and filled at the next open, so a large
  overnight gap can push cash slightly negative. The strategy keeps 5% in
  reserve to make this rare.
- The cost model is simple: per-share commission with a minimum, plus fixed
  slippage in basis points. Phase 2 makes this realistic.
- Orders submitted on the final bar never fill (reported at the end).
