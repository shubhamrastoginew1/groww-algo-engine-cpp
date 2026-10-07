#pragma once
// Event-driven backtester: replays synthetic candles through the SAME engine,
// strategies, risk manager and PaperBroker used in the demo. Orders decided
// on a bar's close fill at the next bar's open (no look-ahead).
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ge/core/config.hpp"
#include "ge/core/time.hpp"

namespace ge {

struct BacktestOptions {
    std::vector<std::string> strategy_ids;
    Timestamp start = 0;
    Timestamp end = 0;
    int interval = 5;  // candle minutes
    unsigned seed = 7;
};

// Result as JSON: {metrics, per_strategy, equity_curve, trades, ...}; python/report.py renders it.
nlohmann::json run_backtest(const Settings& settings, const BacktestOptions& opt);

}  // namespace ge
