#include "ge/backtest/backtester.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>

#include "ge/core/engine.hpp"
#include "ge/data/simulator.hpp"
#include "ge/orders/paper_broker.hpp"

namespace ge {
namespace {

nlohmann::json metrics(const PositionManager& pm, double capital) {
    const auto& eq = pm.equity_curve();
    double peak = capital, mdd = 0.0;
    std::map<Date, double> daily;  // last equity value of each day
    for (const auto& [ts, pnl] : eq) {
        peak = std::max(peak, capital + pnl);
        mdd = std::max(mdd, peak - (capital + pnl));
        daily[ist_date(ts)] = capital + pnl;
    }
    std::vector<double> rets;
    double prev = capital;
    for (const auto& [d, v] : daily) {
        rets.push_back((v - prev) / prev);
        prev = v;
    }
    double sharpe = 0.0;
    if (rets.size() >= 2) {
        double mean = 0, var = 0;
        for (double r : rets) mean += r / rets.size();
        for (double r : rets) var += (r - mean) * (r - mean) / (rets.size() - 1);
        if (var > 0) sharpe = mean / std::sqrt(var) * std::sqrt(252.0);
    }
    int wins = 0;
    for (const auto& t : pm.trades()) wins += t.pnl > 0;
    const PnL p = pm.pnl();
    const auto n = pm.trades().size();
    return {{"net_pnl", p.total},
            {"return_pct", round_to(p.total / capital * 100, 3)},
            {"max_drawdown", round_to(mdd, 2)},
            {"sharpe", round_to(sharpe, 2)},
            {"trades", n},
            {"win_rate_pct", n ? round_to(100.0 * wins / n, 1) : 0.0},
            {"charges", round_to(p.charges, 2)},
            {"trading_days", daily.size()}};
}

}  // namespace

nlohmann::json run_backtest(const Settings& settings, const BacktestOptions& opt) {
    const auto t0 = std::chrono::steady_clock::now();
    Settings s = settings;
    s.engine.candle_interval = opt.interval;
    const MarketCalendar cal(s.holidays);

    // contracts listed as of the END date stay alive for the whole window
    InstrumentStore store;
    store.load_synthetic_universe(s.demo.base_prices, ist_date(opt.end), cal.holidays());
    SimClock clock(opt.start);
    PaperBroker broker(s.paper, store, /*immediate=*/false);
    Engine engine(s, broker, store, clock, "backtest", /*realtime=*/false);
    engine.load_strategies(opt.strategy_ids);
    for (const auto& st : engine.strategies())
        if (st->status == StrategyStatus::Error) throw std::runtime_error(st->id() + ": " + st->error);

    std::vector<const Instrument*> insts;
    for (SymbolId id : engine.subscribed()) insts.push_back(&store.get(id));
    std::map<Timestamp, std::vector<Candle>> bars;  // bar start -> candles of every symbol
    for (auto& [id, candles] : generate_history(insts, opt.start, opt.end, opt.interval, s.demo.base_prices, cal,
                                                s.demo.annual_vol, opt.seed))
        for (const auto& c : candles) bars[c.ts].push_back(c);
    if (bars.empty()) throw std::runtime_error("no candles in the backtest window");

    const Timestamp bar_ns = from_minutes(opt.interval);
    for (const auto& [ts, candles] : bars) {
        clock.set(ts);
        for (const auto& c : candles) broker.on_bar(c);  // fill orders from the previous bar at this open
        clock.set(ts + bar_ns);                          // the bar has closed
        for (const auto& c : candles) engine.on_market_tick(Tick{c.symbol, c.close, ts + bar_ns});
        for (const auto& c : candles) engine.bus().dispatch(Event{c});
        engine.bus().dispatch(Event{TimerEvent{}});
    }
    // flatten anything still open at the last close so all P&L is realised
    engine.risk().cfg.enforce_market_hours = false;
    engine.risk().cfg.no_new_entries_after = "23:59";
    engine.square_off({}, "end of backtest");
    for (const auto& c : bars.rbegin()->second) broker.on_bar(Candle{c.symbol, c.ts, c.close, c.close, c.close, c.close});
    engine.positions().snapshot_equity(clock.now());

    nlohmann::json per = nlohmann::json::object(), equity = nlohmann::json::array(), trades = nlohmann::json::array();
    for (const auto& st : engine.strategies()) {
        int n = 0, w = 0;
        for (const auto& t : engine.positions().trades())
            if (t.strategy_id == st->id()) n++, w += t.pnl > 0;
        per[st->id()] = {{"net_pnl", engine.positions().pnl(st->id()).total}, {"trades", n},
                         {"win_rate_pct", n ? round_to(100.0 * w / n, 1) : 0.0}};
    }
    for (const auto& [ts, v] : engine.positions().equity_curve()) equity.push_back({iso_ist(ts), v});
    for (const auto& t : engine.positions().trades())
        trades.push_back({{"strategy_id", t.strategy_id}, {"symbol", t.symbol}, {"direction", t.direction},
                          {"qty", t.qty}, {"entry_time", iso_ist(t.entry_time)}, {"entry_price", round_to(t.entry_price, 2)},
                          {"exit_time", iso_ist(t.exit_time)}, {"exit_price", t.exit_price}, {"pnl", t.pnl}});
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    return {{"strategy_ids", opt.strategy_ids},
            {"start", iso_ist(opt.start)},
            {"end", iso_ist(opt.end)},
            {"interval", opt.interval},
            {"bars", bars.size()},
            {"elapsed_sec", round_to(secs, 4)},
            {"metrics", metrics(engine.positions(), s.paper.starting_capital)},
            {"per_strategy", per},
            {"equity_curve", equity},
            {"trades", trades}};
}

}  // namespace ge
