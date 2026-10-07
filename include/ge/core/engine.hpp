#pragma once
// Engine: wires market data -> strategies -> risk -> broker -> positions.
//
//   feed ──ticks──▶ EventBus ──▶ CandleBuilder ──candles──▶ Strategies
//                      │                                      │ orders
//   PaperBroker ◀──────┴────────── RiskManager ◀─────────────┘
//        │ fills
//        └──▶ EventBus ──▶ PositionManager ──▶ state.json (Python dashboard)
//
// realtime = true : one engine thread + one timer thread (demo)
// realtime = false: no threads, driven synchronously by the Backtester / tests
#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "ge/core/config.hpp"
#include "ge/core/event_bus.hpp"
#include "ge/data/candle_builder.hpp"
#include "ge/data/instruments.hpp"
#include "ge/orders/broker.hpp"
#include "ge/portfolio/position_manager.hpp"
#include "ge/risk/risk_manager.hpp"
#include "ge/strategies/strategy.hpp"

namespace ge {

class Engine {
public:
    Engine(Settings settings, Broker& broker, InstrumentStore& store, Clock& clock, std::string mode, bool realtime);
    ~Engine();

    // ---- setup
    void load_strategies(const std::vector<std::string>& only = {});  // empty: every enabled one
    Strategy* add_strategy(const StrategyConfig& cfg);
    void subscribe(Strategy& s, const Instrument& inst);

    // ---- lifecycle (realtime mode)
    void start();
    void stop();

    // ---- inputs
    void on_market_tick(const Tick& t);  // feed sink: ring buffer when realtime, direct dispatch otherwise
    Order* submit_order(Order o);        // risk check -> broker
    void cancel_order(Order& o);
    void square_off(const std::string& strategy_id, const std::string& reason);

    // ---- queries
    std::optional<double> ltp(SymbolId s) const;
    const std::deque<Order>& orders() const { return orders_; }
    const std::vector<std::unique_ptr<Strategy>>& strategies() const { return strategies_; }
    const std::vector<SymbolId>& subscribed() const { return subscribed_; }
    PositionManager& positions() { return positions_; }
    RiskManager& risk() { return risk_; }
    EventBus& bus() { return bus_; }
    InstrumentStore& store() { return store_; }
    Clock& clock() { return clock_; }
    const Settings& settings() const { return settings_; }

    nlohmann::json snapshot() const;  // dashboard state
    void write_state_file() const;

private:
    void handle(const Event& ev);
    void on_tick(const Tick& t);
    void on_candle(const Candle& c);
    void on_fill(const Fill& f);
    void on_timer();
    void on_broker_fill(Order& o, double price, double charges);

    Settings settings_;
    Broker& broker_;
    InstrumentStore& store_;
    Clock& clock_;
    std::string mode_;
    bool realtime_;
    Logger log_{"engine"};

    MarketCalendar calendar_;
    EventBus bus_;
    PositionManager positions_;
    RiskManager risk_;
    CandleBuilder candles_;
    int square_off_time_;
    std::optional<Date> squared_off_on_;

    std::vector<std::unique_ptr<Strategy>> strategies_;
    std::unordered_map<SymbolId, std::vector<Strategy*>> subs_;  // symbol -> subscribed strategies
    std::vector<SymbolId> subscribed_;
    std::deque<Order> orders_;  // engine owns every order (deque: stable addresses)
    std::vector<double> ltp_;   // by SymbolId, NaN = no price yet
    std::uint64_t ticks_ = 0;

    std::atomic<bool> stop_{false};
    std::thread engine_thread_, timer_thread_;
};

}  // namespace ge
