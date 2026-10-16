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
std::vector<Bar> load_csv(const std::string& path, const std::string& symbol);

}  // namespace qe
