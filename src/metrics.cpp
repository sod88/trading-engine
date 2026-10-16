#include "qe/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <vector>

namespace qe {

PerformanceReport compute_report(const Portfolio& portfolio, double periods_per_year) {
    PerformanceReport r;
    const auto& curve = portfolio.equity_curve();
    r.initial_equity = portfolio.initial_cash();
    r.fills = portfolio.fills().size();
    r.total_commission = portfolio.total_commission();

    const auto& trips = portfolio.round_trip_pnls();
    r.round_trips = trips.size();
    if (!trips.empty()) {
        auto wins = std::count_if(trips.begin(), trips.end(), [](double p) { return p > 0; });
        r.win_rate = static_cast<double>(wins) / static_cast<double>(trips.size());
    }

    if (curve.empty()) {
        r.final_equity = r.initial_equity;
        return r;
    }

    r.bars = curve.size();
    r.start_date = curve.front().date;
    r.end_date = curve.back().date;
    r.final_equity = curve.back().equity;
    r.total_return = r.final_equity / r.initial_equity - 1.0;

    const double years = static_cast<double>(curve.size()) / periods_per_year;
    if (years > 0 && r.final_equity > 0)
        r.cagr = std::pow(r.final_equity / r.initial_equity, 1.0 / years) - 1.0;

    // Per-bar returns, starting from the initial cash.
    std::vector<double> returns;
    returns.reserve(curve.size());
    double prev = r.initial_equity;
    std::size_t invested_bars = 0;
    for (const auto& p : curve) {
        returns.push_back(p.equity / prev - 1.0);
        prev = p.equity;
        if (std::abs(p.equity - p.cash) > 1e-9) ++invested_bars;
    }
    r.exposure = static_cast<double>(invested_bars) / static_cast<double>(curve.size());

    if (returns.size() > 1) {
        double mean = 0.0;
        for (double x : returns) mean += x;
        mean /= static_cast<double>(returns.size());
        double var = 0.0;
        for (double x : returns) var += (x - mean) * (x - mean);
        var /= static_cast<double>(returns.size() - 1);
        const double sd = std::sqrt(var);
        r.annual_volatility = sd * std::sqrt(periods_per_year);
        if (sd > 0) r.sharpe = mean / sd * std::sqrt(periods_per_year);
    }

    double peak = r.initial_equity;
    std::string peak_date = curve.front().date;
    for (const auto& p : curve) {
        if (p.equity > peak) {
            peak = p.equity;
            peak_date = p.date;
        }
        const double dd = 1.0 - p.equity / peak;
        if (dd > r.max_drawdown) {
            r.max_drawdown = dd;
            r.drawdown_peak_date = peak_date;
            r.drawdown_trough_date = p.date;
        }
    }
    return r;
}

void print_report(std::ostream& out, const std::string& title, const PerformanceReport& r) {
    auto pct = [](double x) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(2) << x * 100.0 << '%';
        return s.str();
    };
    auto money = [](double x) {
        std::ostringstream s;
        s << std::fixed << std::setprecision(2) << x;
        return s.str();
    };
    auto row = [&](const std::string& label, const std::string& value) {
        out << "  " << std::left << std::setw(22) << label << std::right << std::setw(16) << value
            << '\n';
    };

    out << "\n" << title << "\n" << std::string(title.size(), '=') << "\n";
    row("Period", r.start_date + " .. " + r.end_date);
    row("Bars", std::to_string(r.bars));
    row("Initial equity", money(r.initial_equity));
    row("Final equity", money(r.final_equity));
    row("Total return", pct(r.total_return));
    if (r.benchmark_return) row("Buy & hold return", pct(*r.benchmark_return));
    row("CAGR", pct(r.cagr));
    row("Annual volatility", pct(r.annual_volatility));
    {
        std::ostringstream s;
        s << std::fixed << std::setprecision(2) << r.sharpe;
        row("Sharpe (rf = 0)", s.str());
    }
    row("Max drawdown", pct(r.max_drawdown));
    if (r.max_drawdown > 0) row("  peak -> trough", r.drawdown_peak_date + " -> " + r.drawdown_trough_date);
    row("Time in market", pct(r.exposure));
    row("Fills", std::to_string(r.fills));
    row("Round trips", std::to_string(r.round_trips));
    row("Win rate", r.round_trips ? pct(r.win_rate) : "n/a");
    row("Commissions paid", money(r.total_commission));
}

}  // namespace qe
