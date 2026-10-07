#pragma once
// EMA crossover (trend following) on one instrument (future or equity).
//   fast EMA crosses above slow -> go long (reverse a short)
//   fast EMA crosses below slow -> go short (or just exit if allow_short = false)
// Every tick: fixed stop-loss % and target % exits.
#include <optional>

#include "ge/strategies/indicators.hpp"
#include "ge/strategies/strategy.hpp"

namespace ge {

class EmaCrossover : public Strategy {
public:
    using Strategy::Strategy;

    void on_init() override;
    void on_candle(const Candle& c) override;
    void on_tick(const Tick& t) override;
    void on_fill(const Fill& f) override;

    SymbolId symbol() const { return sym_; }
    std::optional<double> entry_price() const { return entry_price_; }

private:
    SymbolId sym_ = kNoSymbol;
    int size_ = 1;  // units per entry
    EMA fast_, slow_;
    std::optional<double> prev_diff_;
    std::optional<double> entry_price_;
};

}  // namespace ge
