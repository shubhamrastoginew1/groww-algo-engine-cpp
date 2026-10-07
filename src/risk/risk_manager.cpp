#include "ge/risk/risk_manager.hpp"

#include <cmath>
#include <cstdlib>

#include "ge/core/logger.hpp"
#include "ge/portfolio/position_manager.hpp"

namespace ge {

RiskVerdict RiskManager::check(const Order& o, const Instrument& inst, double ref_price, Timestamp now) const {
    const int pos = positions_.net_qty(o.strategy_id, o.symbol);
    const bool reduces = pos != 0 && (pos > 0) != (o.signed_qty() > 0) && std::abs(o.qty) <= std::abs(pos);

    if (cfg.enforce_market_hours && !calendar_.is_open(now))
        return {false, "market closed (" + format_ist(now, "%a %H:%M") + ")"};
    if (!reduces) {
        if (kill_switch_) return {false, "kill switch active: " + kill_reason_};
        if (ist_seconds(now) >= parse_hhmm(cfg.no_new_entries_after))
            return {false, "no new entries after " + cfg.no_new_entries_after};
        if (pos == 0 && static_cast<int>(positions_.open_positions().size()) >= cfg.max_open_positions)
            return {false, cat("max open positions (", cfg.max_open_positions, ") reached")};
    }
    if (inst.is_future()) {
        if (o.qty % inst.lot_size) return {false, cat("qty ", o.qty, " is not a multiple of lot size ", inst.lot_size)};
        if (o.qty / inst.lot_size > cfg.max_lots_per_order)
            return {false, cat(o.qty / inst.lot_size, " lots > max_lots_per_order ", cfg.max_lots_per_order)};
    } else if (o.qty > cfg.max_qty_per_order) {
        return {false, cat("qty ", o.qty, " > max_qty_per_order ", cfg.max_qty_per_order)};
    }
    if (!reduces && ref_price > 0 && o.qty * ref_price > cfg.max_order_value)
        return {false, "order value " + fmt_inr(o.qty * ref_price, 0) + " > max_order_value"};
    return {};
}

double RiskManager::day_pnl() const { return round_to(positions_.pnl().total - day_start_pnl_, 2); }

bool RiskManager::daily_loss_breached(Timestamp now) {
    const Date today = ist_date(now);
    if (day_ != today) {  // new trading day: reset the daily baseline
        day_ = today;
        day_start_pnl_ = positions_.pnl().total;
        if (kill_switch_ && kill_reason_.find("daily") != std::string::npos) release_kill_switch();
    }
    if (kill_switch_ || day_pnl() > -std::fabs(cfg.max_daily_loss)) return false;
    activate_kill_switch("daily loss limit hit (" + fmt_inr(day_pnl(), 0) + ")");
    return true;
}

void RiskManager::activate_kill_switch(const std::string& reason) {
    if (!kill_switch_) Logger("risk").error("KILL SWITCH ON: " + reason);
    kill_switch_ = true;
    kill_reason_ = reason;
}

}  // namespace ge
