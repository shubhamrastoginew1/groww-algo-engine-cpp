#include "ge/core/config.hpp"

#include <stdexcept>

#include <yaml-cpp/yaml.h>

namespace ge {
namespace {

// YAML -> JSON so strategy params are a plain json object with typed access.
nlohmann::json to_json(const YAML::Node& n) {
    if (n.IsSequence()) {
        auto arr = nlohmann::json::array();
        for (const auto& v : n) arr.push_back(to_json(v));
        return arr;
    }
    if (n.IsMap()) {
        auto obj = nlohmann::json::object();
        for (const auto& kv : n) obj[kv.first.as<std::string>()] = to_json(kv.second);
        return obj;
    }
    if (!n.IsScalar()) return nullptr;
    const std::string& s = n.Scalar();
    if (n.Tag() == "!") return s;  // quoted string
    if (s == "null" || s == "~") return nullptr;
    if (s == "true") return true;
    if (s == "false") return false;
    try {
        size_t pos = 0;
        long long i = std::stoll(s, &pos);
        if (pos == s.size()) return i;
        double d = std::stod(s, &pos);
        if (pos == s.size()) return d;
    } catch (...) {
    }
    return s;
}

template <class T>
void get(const YAML::Node& n, const char* key, T& out) {
    if (n && n[key] && !n[key].IsNull()) out = n[key].as<T>();
}

}  // namespace

Settings Settings::load(const std::string& path) {
    Settings s;
    YAML::Node root = YAML::LoadFile(path);  // throws YAML::BadFile if missing
    if (auto e = root["engine"]) {
        get(e, "candle_interval", s.engine.candle_interval);
        get(e, "timer_interval_sec", s.engine.timer_interval_sec);
        get(e, "square_off_time", s.engine.square_off_time);
        get(e, "state_file", s.engine.state_file);
    }
    if (auto p = root["paper"]) {
        get(p, "starting_capital", s.paper.starting_capital);
        get(p, "slippage_bps", s.paper.slippage_bps);
        get(p, "brokerage_per_order", s.paper.brokerage_per_order);
        get(p, "charges_bps", s.paper.charges_bps);
    }
    if (auto r = root["risk"]) {
        get(r, "max_daily_loss", s.risk.max_daily_loss);
        get(r, "max_lots_per_order", s.risk.max_lots_per_order);
        get(r, "max_qty_per_order", s.risk.max_qty_per_order);
        get(r, "max_order_value", s.risk.max_order_value);
        get(r, "max_open_positions", s.risk.max_open_positions);
        get(r, "enforce_market_hours", s.risk.enforce_market_hours);
        get(r, "no_new_entries_after", s.risk.no_new_entries_after);
    }
    if (auto d = root["demo"]) {
        get(d, "speed", s.demo.speed);
        get(d, "tick_interval_sec", s.demo.tick_interval_sec);
        get(d, "seed", s.demo.seed);
        get(d, "annual_vol", s.demo.annual_vol);
        if (auto bp = d["base_prices"]) {
            s.demo.base_prices.clear();
            for (const auto& kv : bp) s.demo.base_prices[kv.first.as<std::string>()] = kv.second.as<double>();
        }
    }
    if (auto h = root["holidays"])
        for (const auto& v : h) s.holidays.push_back(v.as<std::string>());
    if (auto st = root["strategies"]) {
        for (const auto& v : st) {
            StrategyConfig c;
            c.id = v["id"].as<std::string>();
            c.type = v["type"].as<std::string>();
            get(v, "enabled", c.enabled);
            if (v["params"] && v["params"].IsMap()) c.params = to_json(v["params"]);
            s.strategies.push_back(std::move(c));
        }
    }
    return s;
}

const StrategyConfig& Settings::strategy(const std::string& id) const {
    for (const auto& s : strategies)
        if (s.id == id) return s;
    throw std::out_of_range("strategy '" + id + "' not found in settings");
}

}  // namespace ge
