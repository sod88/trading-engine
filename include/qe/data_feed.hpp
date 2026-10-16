#pragma once
// Market data sources. A DataFeed hands out bars one at a time, in time
// order, so a strategy can never peek at future data.

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "qe/event.hpp"

namespace qe {

class DataFeed {
public:
    virtual ~DataFeed() = default;
    // Next bar in time order, or std::nullopt when the data is exhausted.
    virtual std::optional<Bar> next() = 0;
};

// Replays an in-memory series of bars. Used for backtests and tests.
class BarSeriesFeed : public DataFeed {
public:
    explicit BarSeriesFeed(std::vector<Bar> bars) : bars_(std::move(bars)) {}

    std::optional<Bar> next() override {
        if (index_ >= bars_.size()) return std::nullopt;
        return bars_[index_++];
    }

    std::size_t size() const { return bars_.size(); }
    const std::vector<Bar>& bars() const { return bars_; }

private:
    std::vector<Bar> bars_;
    std::size_t index_ = 0;
};

// Loads daily bars from a CSV file with a header row. Columns are matched by
// name (case-insensitive), so both this project's sample files and a Yahoo
// Finance download (Date,Open,High,Low,Close,Adj Close,Volume) work.
// Required: date, open, high, low, close. Optional: volume.
// Rows with missing values ("null", empty) are skipped. Bars are sorted by
// date; duplicate dates throw.
// If an "Adj Close" column is present, open/high/low/close are scaled by
// adj_close / close so that dividends and splits don't show up as price
// jumps. Prices are then comparable across the whole history.
std::vector<Bar> load_csv(const std::string& path, const std::string& symbol);

// Default symbol for a data file: its name without directory or extension,
// upper-cased ("data/spy.csv" -> "SPY").
std::string symbol_from_path(const std::string& path);

// Combines several symbols' histories into one stream ordered by date, then
// symbol, ready for BarSeriesFeed. Symbols may trade on different days.
std::vector<Bar> merge_series(std::vector<std::vector<Bar>> series);

}  // namespace qe
