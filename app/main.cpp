// Command-line backtest runner.
//
//   backtest <data.csv> [more.csv ...] [--symbol SPY] [--strategy sma|rsi]
//            [--cash 100000] [--commission 0.005] [--slippage-bps 1]
//            [--out results] [--verbose]
//   SMA crossover:      [--fast 20] [--slow 50]
//   RSI mean reversion: [--rsi-period 14] [--rsi-entry 30] [--rsi-exit 50]
//
// Each file holds one symbol, named after the file (SPY.csv -> SPY) unless
// --symbol is given for a single file. With several files the strategy
// splits its allocation equally between them.

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "qe/broker.hpp"
#include "qe/data_feed.hpp"
#include "qe/engine.hpp"
#include "qe/metrics.hpp"
#include "qe/portfolio.hpp"
#include "qe/strategies/rsi_mean_reversion.hpp"
#include "qe/strategies/sma_crossover.hpp"

namespace {

struct Options {
    std::vector<std::string> data_paths;
    std::string symbol;  // only valid with a single file
    std::string strategy = "sma";
    std::size_t fast = 20;
    std::size_t slow = 50;
    std::size_t rsi_period = 14;
    double rsi_entry = 30.0;
    double rsi_exit = 50.0;
    double cash = 100'000.0;
    qe::CostModel costs;
    std::string out_dir = "results";
    bool verbose = false;
};

[[noreturn]] void usage(const char* argv0) {
    std::cerr << "usage: " << argv0
              << " <data.csv> [more.csv ...] [--symbol NAME] [--strategy sma|rsi] [--cash AMOUNT]\n"
                 "                 [--commission PER_SHARE] [--slippage-bps BPS] [--out DIR] [--verbose]\n"
                 "  sma: [--fast N] [--slow N]\n"
                 "  rsi: [--rsi-period N] [--rsi-entry LEVEL] [--rsi-exit LEVEL]\n";
    std::exit(2);
}

Options parse_args(int argc, char** argv) {
    Options opt;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto value = [&]() -> std::string {
            if (i + 1 >= argc) usage(argv[0]);
            return argv[++i];
        };
        if (arg == "--symbol") opt.symbol = value();
        else if (arg == "--strategy") opt.strategy = value();
        else if (arg == "--fast") opt.fast = std::stoul(value());
        else if (arg == "--slow") opt.slow = std::stoul(value());
        else if (arg == "--rsi-period") opt.rsi_period = std::stoul(value());
        else if (arg == "--rsi-entry") opt.rsi_entry = std::stod(value());
        else if (arg == "--rsi-exit") opt.rsi_exit = std::stod(value());
        else if (arg == "--cash") opt.cash = std::stod(value());
        else if (arg == "--commission") opt.costs.commission_per_share = std::stod(value());
        else if (arg == "--slippage-bps") opt.costs.slippage_bps = std::stod(value());
        else if (arg == "--out") opt.out_dir = value();
        else if (arg == "--verbose" || arg == "-v") opt.verbose = true;
        else if (arg == "--help" || arg == "-h") usage(argv[0]);
        else if (!arg.empty() && arg[0] == '-') usage(argv[0]);
        else opt.data_paths.push_back(arg);
    }
    if (opt.data_paths.empty()) usage(argv[0]);
    if (opt.strategy != "sma" && opt.strategy != "rsi") usage(argv[0]);
    if (!opt.symbol.empty() && opt.data_paths.size() > 1) {
        std::cerr << "--symbol only works with a single data file; symbols come from file names\n";
        std::exit(2);
    }
    return opt;
}

std::unique_ptr<qe::Strategy> make_strategy(const Options& opt, std::size_t symbols) {
    if (opt.strategy == "rsi")
        return std::make_unique<qe::RsiMeanReversion>(opt.rsi_period, opt.rsi_entry, opt.rsi_exit, 0.95,
                                                      symbols);
    return std::make_unique<qe::SmaCrossover>(opt.fast, opt.slow, 0.95, symbols);
}

void write_outputs(const std::string& dir, const qe::Portfolio& pf) {
    std::filesystem::create_directories(dir);

    std::ofstream eq(dir + "/equity.csv");
    eq << "date,equity,cash\n" << std::fixed << std::setprecision(2);
    for (const auto& p : pf.equity_curve()) eq << p.date << ',' << p.equity << ',' << p.cash << '\n';

    std::ofstream tr(dir + "/trades.csv");
    tr << "date,symbol,side,quantity,price,commission\n" << std::fixed << std::setprecision(4);
    for (const auto& f : pf.fills())
        tr << f.date << ',' << f.symbol << ',' << qe::to_string(f.side) << ',' << f.quantity << ','
           << f.price << ',' << f.commission << '\n';
}

}  // namespace

int main(int argc, char** argv) {
    const Options opt = parse_args(argc, argv);
    try {
        std::vector<std::vector<qe::Bar>> series;
        std::vector<std::string> symbols;
        double buy_and_hold = 0.0;  // equal-weight, each symbol over its own history
        for (const auto& path : opt.data_paths) {
            const std::string symbol = opt.symbol.empty() ? qe::symbol_from_path(path) : opt.symbol;
            auto bars = qe::load_csv(path, symbol);
            buy_and_hold += (bars.back().close / bars.front().close - 1.0) / static_cast<double>(opt.data_paths.size());
            std::cout << "Loaded " << bars.size() << " bars of " << symbol << " (" << bars.front().date
                      << " .. " << bars.back().date << ")\n";
            symbols.push_back(symbol);
            series.push_back(std::move(bars));
        }

        qe::BarSeriesFeed feed(qe::merge_series(std::move(series)));
        auto strategy = make_strategy(opt, symbols.size());
        qe::Portfolio portfolio(opt.cash);
        qe::SimulatedBroker broker(opt.costs);

        qe::Backtester bt(feed, *strategy, portfolio, broker);
        bt.set_verbose(opt.verbose);
        bt.run();

        auto report = qe::compute_report(portfolio);
        report.benchmark_return = buy_and_hold;
        std::string universe;
        for (const auto& sym : symbols) universe += (universe.empty() ? "" : ", ") + sym;
        qe::print_report(std::cout, strategy->name() + " on " + universe, report);
        if (bt.rejected_orders() > 0)
            std::cout << "  (" << bt.rejected_orders() << " orders rejected by risk checks)\n";
        if (broker.pending() > 0)
            std::cout << "  (" << broker.pending() << " order(s) submitted on the last bar never filled)\n";

        write_outputs(opt.out_dir, portfolio);
        std::cout << "\nWrote " << opt.out_dir << "/equity.csv and " << opt.out_dir << "/trades.csv\n";
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
