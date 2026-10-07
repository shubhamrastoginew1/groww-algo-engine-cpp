#pragma once
// Strategy SDK. Subclass Strategy, override the hooks you need, and add your
// type to create_strategy() in strategy.cpp. Hooks run on the engine thread,
// one event at a time, so strategy code needs no locks. The same class runs
// in the live demo and in backtests.
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "ge/core/config.hpp"
#include "ge/core/logger.hpp"
#include "ge/core/models.hpp"

namespace ge {

class Engine;

enum class StrategyStatus { Running, Stopped, Error };
const char* to_string(StrategyStatus s);

class Strategy {
public:
    Strategy(std::string id, nlohmann::json params, Engine& engine);
    virtual ~Strategy() = default;

    // ---- hooks
    virtual void on_init() {}  // resolve instruments + subscribe(); runs once
    virtual void on_tick(const Tick&) {}
    virtual void on_candle(const Candle&) {}
    virtual void on_fill(const Fill&) {}

    const std::string& id() const { return id_; }
    std::string type;
    StrategyStatus status = StrategyStatus::Running;
    std::string error;
    std::string last_signal;
    nlohmann::json state = nlohmann::json::object();  // shown on the dashboard

protected:
    // ---- parameters (from settings.yaml), with defaults
    double num(const char* key, double dflt) const;
    std::optional<double> opt_num(const char* key) const;  // null / missing -> nullopt
    std::string str(const char* key, const std::string& dflt) const;
    bool flag(const char* key, bool dflt) const;

    // ---- market data / portfolio
    const Instrument& resolve(const std::string& spec);  // "RELIANCE", "NIFTY:FUT:0"
    void subscribe(const Instrument& inst);
    std::optional<double> ltp(SymbolId s) const;
    int position(SymbolId s) const;
    double avg_price(SymbolId s) const;
    bool has_open_orders(SymbolId s) const;

    // ---- orders (qty in units; risk-checked by the engine)
    Order* buy(SymbolId s, int qty, const std::string& tag = "");
    Order* sell(SymbolId s, int qty, const std::string& tag = "");
    Order* close(SymbolId s, const std::string& tag = "exit");
    void signal(const std::string& text);

    Engine& engine_;
    Logger log_;
    nlohmann::json params_;

private:
    std::string id_;
};

// Strategy factory: add new strategy types here.
std::unique_ptr<Strategy> create_strategy(const StrategyConfig& cfg, Engine& engine);

}  // namespace ge
