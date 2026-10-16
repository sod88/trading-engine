#pragma once
// Performance statistics computed from the portfolio's equity curve.

#include <cstddef>
#include <optional>
#include <ostream>
#include <string>

#include "qe/portfolio.hpp"

namespace qe {

struct PerformanceReport {
    std::string start_date, end_date;
    std::size_t bars = 0;
    double initial_equity = 0.0;
    double final_equity = 0.0;
    double total_return = 0.0;      // fraction, 0.10 = +10%
    double cagr = 0.0;              // compound annual growth rate
    double annual_volatility = 0.0;
    double sharpe = 0.0;            // annualized, risk-free rate = 0
    double max_drawdown = 0.0;      // fraction, positive number
    std::string drawdown_peak_date, drawdown_trough_date;
    double exposure = 0.0;          // fraction of bars with money in the market
    std::size_t fills = 0;
    std::size_t round_trips = 0;
    double win_rate = 0.0;          // fraction of round trips with positive net P&L
    double total_commission = 0.0;
    std::optional<double> benchmark_return;  // e.g. buy-and-hold over the same period
};

PerformanceReport compute_report(const Portfolio& portfolio, double periods_per_year = 252.0);
void print_report(std::ostream& out, const std::string& title, const PerformanceReport& r);

}  // namespace qe
