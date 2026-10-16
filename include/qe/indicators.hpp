#pragma once
// Incremental indicators. Each one is updated with a single new value per
// bar, so it can only ever use past data.

#include <cstddef>
#include <deque>
#include <stdexcept>

namespace qe {

class RollingMean {
public:
    explicit RollingMean(std::size_t window) : window_(window) {
        if (window_ == 0) throw std::invalid_argument("RollingMean window must be > 0");
    }

    void push(double value) {
        values_.push_back(value);
        sum_ += value;
        if (values_.size() > window_) {
            sum_ -= values_.front();
            values_.pop_front();
        }
    }

    bool ready() const { return values_.size() == window_; }
    double value() const { return values_.empty() ? 0.0 : sum_ / static_cast<double>(values_.size()); }
    std::size_t window() const { return window_; }

private:
    std::size_t window_;
    std::deque<double> values_;
    double sum_ = 0.0;
};

}  // namespace qe
