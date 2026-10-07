#pragma once
// Streaming indicators: O(1) per update, safe for live trading.
#include <optional>
#include <stdexcept>

namespace ge {

// Exponential moving average seeded with the SMA of the first `period` values.
class EMA {
public:
    explicit EMA(int period = 1) {
        if (period < 1) throw std::invalid_argument("EMA period must be >= 1");
        period_ = period;
        alpha_ = 2.0 / (period + 1);
    }
    std::optional<double> update(double x) {
        if (++count_ < period_) {
            seed_sum_ += x;
            return std::nullopt;
        }
        value_ = count_ == period_ ? (seed_sum_ + x) / period_ : alpha_ * x + (1 - alpha_) * *value_;
        return value_;
    }
    std::optional<double> value() const { return value_; }

private:
    int period_ = 1;
    double alpha_ = 1.0;
    int count_ = 0;
    double seed_sum_ = 0.0;
    std::optional<double> value_;
};

}  // namespace ge
