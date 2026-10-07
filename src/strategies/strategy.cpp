#include "ge/strategies/strategy.hpp"

#include <stdexcept>

#include "ge/core/engine.hpp"
#include "ge/strategies/ema_crossover.hpp"

namespace ge {

const char* to_string(StrategyStatus s) {
    switch (s) {
        case StrategyStatus::Running: return "RUNNING";
        case StrategyStatus::Stopped: return "STOPPED";
        case StrategyStatus::Error: return "ERROR";
    }
    return "?";
}

std::unique_ptr<Strategy> create_strategy(const StrategyConfig& cfg, Engine& engine) {
    std::unique_ptr<Strategy> s;
    if (cfg.type == "ema_crossover") s = std::make_unique<EmaCrossover>(cfg.id, cfg.params, engine);
    // else if (cfg.type == "my_strategy") s = std::make_unique<MyStrategy>(cfg.id, cfg.params, engine);
    if (!s) throw std::invalid_argument("unknown strategy type '" + cfg.type + "'");
    s->type = cfg.type;
    return s;
}

Strategy::Strategy(std::string id, nlohmann::json params, Engine& engine)
    : engine_(engine), log_("strat." + id), params_(std::move(params)), id_(std::move(id)) {}

double Strategy::num(const char* key, double dflt) const { return opt_num(key).value_or(dflt); }

std::optional<double> Strategy::opt_num(const char* key) const {
    if (!params_.contains(key) || !params_[key].is_number()) return std::nullopt;
    return params_[key].get<double>();
}

std::string Strategy::str(const char* key, const std::string& dflt) const {
    return params_.contains(key) && params_[key].is_string() ? params_[key].get<std::string>() : dflt;
}

bool Strategy::flag(const char* key, bool dflt) const {
    return params_.contains(key) && params_[key].is_boolean() ? params_[key].get<bool>() : dflt;
}

const Instrument& Strategy::resolve(const std::string& spec) {
    return engine_.store().resolve(spec, engine_.clock().today());
}

void Strategy::subscribe(const Instrument& inst) { engine_.subscribe(*this, inst); }
std::optional<double> Strategy::ltp(SymbolId s) const { return engine_.ltp(s); }
int Strategy::position(SymbolId s) const { return engine_.positions().net_qty(id_, s); }

double Strategy::avg_price(SymbolId s) const {
    const Position* p = engine_.positions().find(id_, s);
    return p ? p->avg_price : 0.0;
}

bool Strategy::has_open_orders(SymbolId s) const {
    for (const Order& o : engine_.orders())
        if (o.is_open() && o.strategy_id == id_ && o.symbol == s) return true;
    return false;
}

Order* Strategy::buy(SymbolId s, int qty, const std::string& tag) {
    Order o;
    o.symbol = s;
    o.side = Side::Buy;
    o.qty = qty;
    o.strategy_id = id_;
    o.tag = tag;
    return engine_.submit_order(std::move(o));
}

Order* Strategy::sell(SymbolId s, int qty, const std::string& tag) {
    Order o;
    o.symbol = s;
    o.side = Side::Sell;
    o.qty = qty;
    o.strategy_id = id_;
    o.tag = tag;
    return engine_.submit_order(std::move(o));
}

Order* Strategy::close(SymbolId s, const std::string& tag) {
    const int pos = position(s);
    if (pos == 0) return nullptr;
    return pos > 0 ? sell(s, pos, tag) : buy(s, -pos, tag);
}

void Strategy::signal(const std::string& text) {
    last_signal = text;
    log_.info("SIGNAL " + text);
}

}  // namespace ge
