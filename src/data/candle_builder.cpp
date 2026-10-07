#include "ge/data/candle_builder.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace ge {

CandleBuilder::CandleBuilder(int interval_min, Sink sink) : interval_ns_(from_minutes(interval_min)), sink_(std::move(sink)) {
    if (interval_min <= 0) throw std::invalid_argument("interval_min must be positive");
}

Timestamp CandleBuilder::bucket_start(Timestamp ts) const {
    const Timestamp anchor = ist_time(ist_date(ts), kMarketOpen);
    return anchor + floor_div(ts - anchor, interval_ns_) * interval_ns_;
}

void CandleBuilder::on_tick(const Tick& t) {
    const Timestamp start = bucket_start(t.ts);
    auto it = open_.find(t.symbol);
    if (it != open_.end() && start > it->second.ts) {  // new bucket: close the old candle
        sink_(it->second);
        open_.erase(it);
        it = open_.end();
    }
    if (it == open_.end()) {
        open_.emplace(t.symbol, Candle{t.symbol, start, t.ltp, t.ltp, t.ltp, t.ltp});
    } else if (start == it->second.ts) {
        Candle& c = it->second;
        c.high = std::max(c.high, t.ltp);
        c.low = std::min(c.low, t.ltp);
        c.close = t.ltp;
    }  // out-of-order ticks (older bucket) are ignored
}

void CandleBuilder::flush(Timestamp now) {
    std::vector<SymbolId> done;
    for (const auto& [s, c] : open_)
        if (c.ts + interval_ns_ <= now) done.push_back(s);
    std::sort(done.begin(), done.end());  // deterministic order
    for (SymbolId s : done) {
        sink_(open_.at(s));
        open_.erase(s);
    }
}

}  // namespace ge
