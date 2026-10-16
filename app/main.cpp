// Command-line backtest runner.
//
//   backtest <data.csv> [--symbol SPY] [--fast 20] [--slow 50] [--cash 100000]
//            [--commission 0.005] [--slippage-bps 1] [--out results] [--verbose]

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include "qe/broker.hpp"
#include "qe/data_feed.hpp"
#include "qe/engine.hpp"
#include "qe/metrics.hpp"
#include "qe/portfolio.hpp"
#include "qe/strategies/sma_crossover.hpp"

namespace {

struct Options {
    std::string data_path;
    std::string symbol = "SAMPLE";
    std::size_t fast = 20;
    std::size_t slow = 50;
    double cash = 100'000.0;
    qe::CostModel costs;
    std::string out_dir = "results";
    bool verbose = false;
};

[[noreturn]] void usage(const char* argv0) {
    std::cerr << "usage: " << argv0
              << " <data.csv> [--symbol NAME] [--fast N] [--slow N] [--cash AMOUNT]\n"
                 "                 [--commission PER_SHARE] [--slippage-bps BPS] [--out DIR] [--verbose]\n";
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
        else if (arg == "--fast") opt.fast = std::stoul(value());
        else if (arg == "--slow") opt.slow = std::stoul(value());
        else if (arg == "--cash") opt.cash = std::stod(value());
        else if (arg == "--commission") opt.costs.commission_per_share = std::stod(value());
        else if (arg == "--slippage-bps") opt.costs.slippage_bps = std::stod(value());
        else if (arg == "--out") opt.out_dir = value();
        else if (arg == "--verbose" || arg == "-v") opt.verbose = true;
        else if (arg == "--help" || arg == "-h") usage(argv[0]);
        else if (!arg.empty() && arg[0] == '-') usage(argv[0]);
        else opt.data_path = arg;
    }
    if (opt.data_path.empty()) usage(argv[0]);
    return opt;
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
        auto bars = qe::load_csv(opt.data_path, opt.symbol);
        const double buy_and_hold = bars.back().close / bars.front().close - 1.0;

        qe::BarSeriesFeed feed(std::move(bars));
        qe::SmaCrossover strategy(opt.fast, opt.slow);
        qe::Portfolio portfolio(opt.cash);
        qe::SimulatedBroker broker(opt.costs);

        qe::Backtester bt(feed, strategy, portfolio, broker);
        bt.set_verbose(opt.verbose);
        bt.run();

        auto report = qe::compute_report(portfolio);
        report.benchmark_return = buy_and_hold;
        qe::print_report(std::cout, strategy.name() + " on " + opt.symbol, report);
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
