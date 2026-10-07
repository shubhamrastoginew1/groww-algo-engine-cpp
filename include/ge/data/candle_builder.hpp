#pragma once
// Tick -> N-minute OHLC candles aligned to the 09:15 session open
// (5-minute bars start at 09:15, 09:20, ... like exchange charts).
#include <functional>
#include <unordered_map>

#include "ge/core/models.hpp"

namespace ge {

class CandleBuilder {
public:
    using Sink = std::function<void(const Candle&)>;

    CandleBuilder(int interval_min, Sink sink);
    void on_tick(const Tick& t);   // emits the previous candle when a new bucket starts
    void flush(Timestamp now);     // close candles whose window has fully elapsed
    Timestamp bucket_start(Timestamp ts) const;

private:
    Timestamp interval_ns_;
    Sink sink_;
    std::unordered_map<SymbolId, Candle> open_;
};

}  // namespace ge
