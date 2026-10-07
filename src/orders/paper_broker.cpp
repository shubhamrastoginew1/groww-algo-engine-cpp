#include "ge/orders/paper_broker.hpp"

#include <algorithm>

#include "ge/data/instruments.hpp"

namespace ge {

void PaperBroker::place(Order& o) {
    if (o.qty <= 0 || (o.type == OrderType::Limit && o.price <= 0)) {
        o.status = OrderStatus::Rejected;
        o.message = "invalid quantity / price";
        return;
    }
    if (immediate_) {
        auto it = ltp_.find(o.symbol);
        if (it != ltp_.end() && try_fill(o, it->second)) return;
    }
    open_.push_back(&o);  // rests until the market reaches it
}

void PaperBroker::cancel(Order& o) {
    auto it = std::find(open_.begin(), open_.end(), &o);
    if (it != open_.end()) open_.erase(it);
    if (o.is_open()) o.status = OrderStatus::Cancelled;
}

void PaperBroker::on_tick(const Tick& t) {
    ltp_[t.symbol] = t.ltp;
    if (!immediate_) return;
    std::vector<Order*> still_open;
    auto working = std::move(open_);
    open_.clear();
    for (Order* o : working) {
        if (!o->is_open()) continue;  // cancelled from inside a fill callback
        if (o->symbol != t.symbol || !try_fill(*o, t.ltp)) still_open.push_back(o);
    }
    open_.insert(open_.begin(), still_open.begin(), still_open.end());  // keep time priority
}

void PaperBroker::on_bar(const Candle& c) {
    std::vector<Order*> still_open;
    auto working = std::move(open_);
    open_.clear();
    for (Order* o : working) {
        if (!o->is_open()) continue;
        bool done = false;
        if (o->symbol == c.symbol) {
            if (o->type == OrderType::Market) {
                fill(*o, c.open);
                done = true;
            } else if (o->side == Side::Buy && c.low <= o->price) {
                fill(*o, std::min(c.open, o->price));
                done = true;
            } else if (o->side == Side::Sell && c.high >= o->price) {
                fill(*o, std::max(c.open, o->price));
                done = true;
            }
        }
        if (!done) still_open.push_back(o);
    }
    open_.insert(open_.begin(), still_open.begin(), still_open.end());
    ltp_[c.symbol] = c.close;
}

bool PaperBroker::try_fill(Order& o, double px) {
    if (o.type == OrderType::Limit) {
        if (o.side == Side::Buy && px > o.price) return false;
        if (o.side == Side::Sell && px < o.price) return false;
        fill(o, o.side == Side::Buy ? std::min(px, o.price) : std::max(px, o.price));
        return true;
    }
    fill(o, px);
    return true;
}

void PaperBroker::fill(Order& o, double px) {
    if (o.type == OrderType::Market)  // pay the spread
        px = round_to_tick(px * (1 + sign(o.side) * cfg_.slippage_bps / 10'000.0), store_.get(o.symbol).tick_size);
    const double charges = cfg_.brokerage_per_order + px * o.qty * cfg_.charges_bps / 10'000.0;
    if (fill_) fill_(o, px, round_to(charges, 2));
}

}  // namespace ge
