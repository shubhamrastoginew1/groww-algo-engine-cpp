#pragma once
// Pre-trade checks + a daily-loss kill switch.
//
//   every order : market hours, lot multiple, max lots / qty, max order value,
//                 max open positions, kill switch, no new entries after cut-off
//   exits       : an order that only reduces a position is always allowed, so
//                 the system can flatten itself even with the kill switch on
//   on timer    : day P&L below -max_daily_loss -> kill switch (engine flattens)
#include <optional>
#include <string>

#include "ge/core/config.hpp"
#include "ge/core/models.hpp"

namespace ge {

class PositionManager;

struct RiskVerdict {
    bool ok = true;
    std::string reason;
};

class RiskManager {
public:
    RiskManager(RiskConfig config, const PositionManager& positions, const MarketCalendar& calendar)
        : cfg(std::move(config)), positions_(positions), calendar_(calendar) {}

    RiskVerdict check(const Order& o, const Instrument& inst, double ref_price, Timestamp now) const;
    bool daily_loss_breached(Timestamp now);  // true once, when the kill switch trips

    void activate_kill_switch(const std::string& reason);
    void release_kill_switch() {
        kill_switch_ = false;
        kill_reason_.clear();
    }
    bool kill_switch() const { return kill_switch_; }
    const std::string& kill_reason() const { return kill_reason_; }
    double day_pnl() const;

    RiskConfig cfg;  // public so limits can be tuned at runtime / in tests

private:
    const PositionManager& positions_;
    const MarketCalendar& calendar_;
    bool kill_switch_ = false;
    std::string kill_reason_;
    std::optional<Date> day_;
    double day_start_pnl_ = 0.0;
};

}  // namespace ge
