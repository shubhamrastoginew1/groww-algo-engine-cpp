#include "ge/portfolio/position_manager.hpp"

#include <algorithm>
#include <cstdlib>

namespace ge {

std::optional<Trade> PositionManager::apply_fill(const Fill& f, const std::string& symbol_name) {
    Position& p = positions_[{f.strategy_id, f.symbol}];
    p.strategy_id = f.strategy_id;
    p.symbol = f.symbol;
    p.charges += f.charges;
    if (!p.last_price) p.last_price = f.price;
    const int signed_qty = f.qty * sign(f.side);

    if (p.qty == 0 || (p.qty > 0) == (signed_qty > 0)) {  // open or add: average the cost
        const int new_qty = p.qty + signed_qty;
        p.avg_price = (p.avg_price * std::abs(p.qty) + f.price * std::abs(signed_qty)) / std::abs(new_qty);
        if (p.qty == 0) p.opened_at = f.ts;
        p.qty = new_qty;
        return std::nullopt;
    }
    // reduce, close or flip
    const int closing = std::min(std::abs(signed_qty), std::abs(p.qty));
    const int dir = p.qty > 0 ? 1 : -1;
    const double pnl = (f.price - p.avg_price) * closing * dir;
    p.realized += pnl;
    Trade t{f.strategy_id, symbol_name, dir > 0 ? "LONG" : "SHORT", closing, p.avg_price, f.price,
            p.opened_at,   f.ts,        round_to(pnl, 2)};
    trades_.push_back(t);
    p.qty -= dir * closing;
    if (const int rest = std::abs(signed_qty) - closing; rest > 0) {  // flipped to the other side
        p.qty = rest * (signed_qty > 0 ? 1 : -1);
        p.avg_price = f.price;
        p.opened_at = f.ts;
    } else if (p.qty == 0) {
        p.avg_price = 0.0;
    }
    return t;
}

void PositionManager::mark(SymbolId symbol, double price) {
    for (auto& [key, p] : positions_)
        if (key.second == symbol) p.last_price = price;
}

const Position* PositionManager::find(const std::string& sid, SymbolId symbol) const {
    auto it = positions_.find({sid, symbol});
    return it == positions_.end() ? nullptr : &it->second;
}

int PositionManager::net_qty(const std::string& sid, SymbolId symbol) const {
    const Position* p = find(sid, symbol);
    return p ? p->qty : 0;
}

std::vector<const Position*> PositionManager::open_positions(const std::string& sid) const {
    std::vector<const Position*> out;
    for (const auto& [k, p] : positions_)
        if (p.qty != 0 && (sid.empty() || p.strategy_id == sid)) out.push_back(&p);
    return out;
}

std::vector<const Position*> PositionManager::all() const {
    std::vector<const Position*> out;
    for (const auto& [k, p] : positions_) out.push_back(&p);
    return out;
}

PnL PositionManager::pnl(const std::string& sid) const {
    PnL r;
    for (const auto& [k, p] : positions_) {
        if (!sid.empty() && p.strategy_id != sid) continue;
        r.realized += p.realized;
        r.unrealized += p.unrealized();
        r.charges += p.charges;
    }
    r.total = round_to(r.realized + r.unrealized - r.charges, 2);
    return r;
}

}  // namespace ge
