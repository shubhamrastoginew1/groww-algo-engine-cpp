#pragma once
// Offline market: geometric Brownian motion per underlying; futures priced at
// cost of carry F = S * exp(r * T). Drives the demo and synthetic backtests.
#include <atomic>
#include <functional>
#include <map>
#include <random>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ge/core/time.hpp"
#include "ge/data/instruments.hpp"

namespace ge {

class MarketSimulator {
public:
    MarketSimulator(const std::map<std::string, double>& base_prices, double annual_vol, unsigned seed);
    void advance(double dt_sec);  // move every underlying forward in time
    double price(const Instrument& inst, Timestamp now) const;

private:
    std::mt19937_64 rng_;
    std::normal_distribution<double> normal_{0.0, 1.0};
    std::map<std::string, double> spot_;
    double vol_;
};

// Ascending 1-minute grid of in-session timestamps in [start, end).
std::vector<Timestamp> trading_minutes(const MarketCalendar& cal, Timestamp start, Timestamp end);

// Synthetic candles for a backtest window: {symbol id -> candles}.
std::unordered_map<SymbolId, std::vector<Candle>> generate_history(
    const std::vector<const Instrument*>& insts, Timestamp start, Timestamp end, int interval_min,
    const std::map<std::string, double>& base_prices, const MarketCalendar& cal, double annual_vol, unsigned seed);

// Real-time-ish feed for demo mode: a thread advancing a SimClock and emitting ticks.
class SimulatedFeed {
public:
    using TickSink = std::function<void(const Tick&)>;

    SimulatedFeed(SimClock& clock, const MarketCalendar& cal, const std::map<std::string, double>& base_prices,
                  double speed, double tick_interval_sec, double annual_vol, unsigned seed);
    ~SimulatedFeed() { stop(); }

    void subscribe(const Instrument& inst) { insts_.push_back(&inst); }
    void start(TickSink sink);
    void stop();

private:
    SimClock& clock_;
    const MarketCalendar& cal_;
    MarketSimulator sim_;
    double speed_, tick_interval_;
    std::vector<const Instrument*> insts_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

}  // namespace ge
