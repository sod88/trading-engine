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

// Relative Strength Index with Wilder's smoothing. Needs `period` price
// changes (so period + 1 prices) before it is ready. Ranges 0..100.
class Rsi {
public:
    explicit Rsi(std::size_t period) : period_(period) {
        if (period_ == 0) throw std::invalid_argument("Rsi period must be > 0");
    }

    void push(double price) {
        if (!has_prev_) {
            prev_ = price;
            has_prev_ = true;
            return;
        }
        const double change = price - prev_;
        prev_ = price;
        const double gain = change > 0 ? change : 0.0;
        const double loss = change < 0 ? -change : 0.0;
        const double n = static_cast<double>(period_);

        if (count_ < period_) {
            // Seed with a plain average of the first `period` changes.
            avg_gain_ += gain / n;
            avg_loss_ += loss / n;
            ++count_;
        } else {
            avg_gain_ = (avg_gain_ * (n - 1.0) + gain) / n;
            avg_loss_ = (avg_loss_ * (n - 1.0) + loss) / n;
        }
    }

    bool ready() const { return count_ == period_; }
    double value() const {
        if (avg_loss_ == 0.0) return avg_gain_ == 0.0 ? 50.0 : 100.0;
        return 100.0 - 100.0 / (1.0 + avg_gain_ / avg_loss_);
    }
    std::size_t period() const { return period_; }

private:
    std::size_t period_;
    std::size_t count_ = 0;
    bool has_prev_ = false;
    double prev_ = 0.0;
    double avg_gain_ = 0.0;
    double avg_loss_ = 0.0;
};

}  // namespace qe
