#pragma once
// Settings loaded from config/settings.yaml.
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ge {

struct EngineConfig {
    int candle_interval = 5;            // minutes, aligned to 09:15
    double timer_interval_sec = 1.0;
    std::string square_off_time = "15:15";  // intraday positions are flattened here
    std::string state_file = "run/state.json";  // live snapshot read by python/dashboard.py
};

struct PaperConfig {
    double starting_capital = 1'000'000.0;
    double slippage_bps = 1.0;
    double brokerage_per_order = 20.0;
    double charges_bps = 1.5;           // statutory charges as bps of turnover
};

struct RiskConfig {
    double max_daily_loss = 25'000.0;   // kill switch + flatten below this
    int max_lots_per_order = 20;
    int max_qty_per_order = 5'000;      // cash segment shares
    double max_order_value = 5'000'000.0;
    int max_open_positions = 10;
    bool enforce_market_hours = true;
    std::string no_new_entries_after = "15:00";
};

struct DemoConfig {
    double speed = 30.0;                // simulated seconds per real second
    double tick_interval_sec = 0.25;    // real seconds between simulated ticks
    unsigned seed = 7;
    double annual_vol = 0.16;
    std::map<std::string, double> base_prices = {{"NIFTY", 25000.0}, {"RELIANCE", 1400.0}};
};

struct StrategyConfig {
    std::string id;
    std::string type;
    bool enabled = true;
    nlohmann::json params = nlohmann::json::object();
};

struct Settings {
    EngineConfig engine;
    PaperConfig paper;
    RiskConfig risk;
    DemoConfig demo;
    std::vector<std::string> holidays;
    std::vector<StrategyConfig> strategies;

    static Settings load(const std::string& path);
    const StrategyConfig& strategy(const std::string& id) const;
};

}  // namespace ge
