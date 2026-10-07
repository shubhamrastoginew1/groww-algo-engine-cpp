#pragma once
// PaperBroker: simulated execution.
//   MARKET  fills at LTP +/- slippage (demo) or at the next bar's open (backtest)
//   LIMIT   fills when the price trades through the limit
//   charges flat brokerage per order + bps of turnover
#include <unordered_map>
#include <vector>

#include "ge/core/config.hpp"
#include "ge/orders/broker.hpp"

namespace ge {

class InstrumentStore;

class PaperBroker : public Broker {
public:
    PaperBroker(PaperConfig cfg, const InstrumentStore& store, bool immediate = true)
        : cfg_(cfg), store_(store), immediate_(immediate) {}

    void place(Order& o) override;
    void cancel(Order& o) override;
    void on_tick(const Tick& t) override;
    void on_bar(const Candle& c) override;  // backtests: fills orders placed on the previous bar

    std::size_t working() const { return open_.size(); }

private:
    bool try_fill(Order& o, double px);  // true if filled
    void fill(Order& o, double px);

    PaperConfig cfg_;
    const InstrumentStore& store_;
    bool immediate_;
    std::unordered_map<SymbolId, double> ltp_;
    std::vector<Order*> open_;  // time priority
};

}  // namespace ge
