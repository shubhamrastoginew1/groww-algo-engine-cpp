#include "ge/strategies/ema_crossover.hpp"

#include <cmath>
#include <stdexcept>

namespace ge {

void EmaCrossover::on_init() {
    const int fast = static_cast<int>(num("fast", 9)), slow = static_cast<int>(num("slow", 21));
    if (fast >= slow) throw std::invalid_argument("fast period must be smaller than slow period");
    fast_ = EMA(fast);
    slow_ = EMA(slow);
    const Instrument& inst = resolve(str("instrument", "NIFTY:FUT:0"));
    sym_ = inst.id;
    // derivatives trade in lots, equities in shares
    size_ = inst.is_future() ? static_cast<int>(num("lots", 1)) * inst.lot_size : static_cast<int>(num("qty", 1));
    subscribe(inst);
    log_.info(cat("Trading ", inst.symbol, " size=", size_, " fast=", fast, " slow=", slow));
}

void EmaCrossover::on_candle(const Candle& c) {
    if (c.symbol != sym_) return;
    const auto f = fast_.update(c.close), s = slow_.update(c.close);
    if (!f || !s) return;
    const double diff = *f - *s;
    const auto prev = prev_diff_;
    prev_diff_ = diff;
    state["fast_ema"] = round_to(*f, 2);
    state["slow_ema"] = round_to(*s, 2);
    if (!prev) return;

    const bool bullish = *prev <= 0 && diff > 0;
    const bool bearish = *prev >= 0 && diff < 0;
    if (!(bullish || bearish) || has_open_orders(sym_)) return;
    const int pos = position(sym_);
    const int target = bullish ? size_ : (flag("allow_short", true) ? -size_ : 0);
    if (target == pos) return;

    signal(cat(bullish ? "Bullish" : "Bearish", " crossover @ ", fixed(c.close, 2)));
    // exit first, then enter: an exit is never blocked by entry-only risk rules
    if (pos != 0) close(sym_, "exit");
    if (target > 0) buy(sym_, target, "entry");
    if (target < 0) sell(sym_, -target, "entry");
}

void EmaCrossover::on_tick(const Tick& t) {
    if (t.symbol != sym_ || !entry_price_ || has_open_orders(sym_)) return;
    const int pos = position(sym_);
    if (pos == 0) return;
    const double move_pct = (t.ltp - *entry_price_) / *entry_price_ * 100 * (pos > 0 ? 1 : -1);
    state["open_move_pct"] = round_to(move_pct, 3);
    const auto sl = opt_num("stop_loss_pct"), tgt = opt_num("target_pct");
    const char* reason = nullptr;
    if (sl && move_pct <= -std::fabs(*sl)) reason = "stop_loss";
    else if (tgt && move_pct >= std::fabs(*tgt)) reason = "target";
    if (!reason) return;
    signal(cat("Exit ", reason, " @ ", fixed(t.ltp, 2), " (", signed_fixed(move_pct, 2), "%)"));
    close(sym_, reason);
}

void EmaCrossover::on_fill(const Fill& f) {
    if (f.symbol != sym_) return;
    if (position(sym_) == 0) {
        entry_price_.reset();
        state.erase("open_move_pct");
    } else {
        entry_price_ = avg_price(sym_);
    }
}

}  // namespace ge
