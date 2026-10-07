#pragma once
// Positions and P&L per (strategy, symbol), average-cost method.
// Quantities are units (shares / contracts), so P&L = qty * price move.
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ge/core/models.hpp"

namespace ge {

struct Position {
    std::string strategy_id;
    SymbolId symbol = kNoSymbol;
    int qty = 0;
    double avg_price = 0.0;
    double realized = 0.0;
    double charges = 0.0;
    double last_price = 0.0;
    Timestamp opened_at = 0;

    double unrealized() const { return qty ? (last_price - avg_price) * qty : 0.0; }
    double total() const { return realized + unrealized() - charges; }
};

struct PnL {
    double realized = 0, unrealized = 0, charges = 0, total = 0;
};

class PositionManager {
public:
    // Returns the closed trade when the fill reduces / closes / flips a position.
    std::optional<Trade> apply_fill(const Fill& f, const std::string& symbol_name);
    void mark(SymbolId symbol, double price);  // mark-to-market on every tick

    int net_qty(const std::string& strategy_id, SymbolId symbol) const;
    const Position* find(const std::string& strategy_id, SymbolId symbol) const;
    std::vector<const Position*> open_positions(const std::string& strategy_id = {}) const;
    std::vector<const Position*> all() const;
    PnL pnl(const std::string& strategy_id = {}) const;  // empty = all strategies

    const std::vector<Trade>& trades() const { return trades_; }
    const std::vector<std::pair<Timestamp, double>>& equity_curve() const { return equity_; }
    void snapshot_equity(Timestamp ts) { equity_.emplace_back(ts, pnl().total); }

private:
    std::map<std::pair<std::string, SymbolId>, Position> positions_;
    std::vector<Trade> trades_;
    std::vector<std::pair<Timestamp, double>> equity_;
};

}  // namespace ge
