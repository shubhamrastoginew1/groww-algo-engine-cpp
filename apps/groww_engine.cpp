// Groww Algo Engine (C++ core).
//
//   groww_engine demo [--speed 60] [--duration SEC]   simulated market + paper broker, live state for the dashboard
//   groww_engine backtest [-s id,id] [--days 20]       replay strategies on synthetic data
//
// Python side: python/dashboard.py (live view), python/report.py (backtest HTML).
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <thread>

#include "ge/backtest/backtester.hpp"
#include "ge/core/engine.hpp"
#include "ge/data/simulator.hpp"
#include "ge/orders/paper_broker.hpp"

using namespace ge;
namespace fs = std::filesystem;

namespace {

std::atomic<bool> g_stop{false};
void on_signal(int) { g_stop = true; }

// --key value / --flag  ->  map  (-s is short for --strategies)
std::map<std::string, std::string> parse_args(int argc, char** argv) {
    std::map<std::string, std::string> a;
    for (int i = 2; i < argc; ++i) {
        std::string k = argv[i];
        if (k == "-s") k = "--strategies";
        if (k.rfind("--", 0) != 0) continue;
        a[k.substr(2)] = (i + 1 < argc && std::string(argv[i + 1]).rfind("-", 0) != 0) ? argv[++i] : "true";
    }
    return a;
}

std::vector<std::string> split_list(const std::string& s) {
    std::vector<std::string> out;
    for (auto& p : split(s, ','))
        if (!trim(p).empty()) out.push_back(trim(p));
    return out;
}

int run_demo(const Settings& s, const std::map<std::string, std::string>& a) {
    const MarketCalendar cal(s.holidays);
    const Timestamp now = wall_now();
    SimClock clock(cal.is_open(now) ? now : cal.next_open(now));  // the demo always runs in market hours
    InstrumentStore store;
    store.load_synthetic_universe(s.demo.base_prices, clock.today(), cal.holidays());
    PaperBroker broker(s.paper, store);
    Engine engine(s, broker, store, clock, "demo", /*realtime=*/true);
    engine.load_strategies(a.count("strategies") ? split_list(a.at("strategies")) : std::vector<std::string>{});

    const double speed = a.count("speed") ? std::stod(a.at("speed")) : s.demo.speed;
    SimulatedFeed feed(clock, cal, s.demo.base_prices, speed, s.demo.tick_interval_sec, s.demo.annual_vol, s.demo.seed);
    for (SymbolId id : engine.subscribed()) feed.subscribe(store.get(id));

    std::signal(SIGINT, on_signal);
    engine.start();
    feed.start([&engine](const Tick& t) { engine.on_market_tick(t); });  // feed thread -> lock-free ring
    std::cout << "\n  Demo running at " << speed << "x. State file: " << s.engine.state_file
              << "\n  Dashboard: python3 python/dashboard.py   ->  http://localhost:8050\n  Stop with Ctrl+C\n\n";

    const double duration = a.count("duration") ? std::stod(a.at("duration")) : 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    while (!g_stop &&
           (duration <= 0 || std::chrono::steady_clock::now() - t0 < std::chrono::duration<double>(duration)))
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

    feed.stop();
    engine.stop();
    engine.write_state_file();
    const PnL p = engine.positions().pnl();
    std::cout << "\n  Stopped. P&L " << fmt_inr(p.total) << " (realised " << fmt_inr(p.realized) << ", open "
              << fmt_inr(p.unrealized) << ", charges " << fmt_inr(p.charges) << ")\n";
    return 0;
}

int run_backtest_cmd(const Settings& s, const std::map<std::string, std::string>& a) {
    const MarketCalendar cal(s.holidays);
    BacktestOptions o;
    for (const auto& c : s.strategies)
        if (c.enabled) o.strategy_ids.push_back(c.id);
    if (a.count("strategies")) o.strategy_ids = split_list(a.at("strategies"));
    o.interval = a.count("interval") ? std::stoi(a.at("interval")) : 5;
    o.seed = a.count("seed") ? static_cast<unsigned>(std::stoul(a.at("seed"))) : 7u;
    const Date last = cal.prev_trading_day(ist_date(wall_now()));  // last complete session
    o.end = cal.session_close(last);
    o.start = cal.session_open(last.add_days(-(a.count("days") ? std::stoi(a.at("days")) : 20)));

    set_log_level(a.count("verbose") ? LogLevel::Info : LogLevel::Error);
    const auto res = run_backtest(s, o);
    std::printf("\n  Backtest %s -> %s, %dm bars: %d bars in %.3fs\n\n", format_ist(o.start, "%d %b %Y").c_str(),
                format_ist(o.end, "%d %b %Y").c_str(), o.interval, res["bars"].get<int>(),
                res["elapsed_sec"].get<double>());
    for (const auto& [k, v] : res["metrics"].items()) std::printf("  %-14s %14s\n", k.c_str(), v.dump().c_str());
    std::printf("\n");
    for (const auto& [id, v] : res["per_strategy"].items())
        std::printf("  %-18s net %14s | trades %3d | win %5.1f%%\n", id.c_str(),
                    fmt_inr(v["net_pnl"].get<double>(), 0).c_str(), v["trades"].get<int>(),
                    v["win_rate_pct"].get<double>());

    fs::create_directories("reports");
    const std::string out = a.count("out") ? a.at("out") : "reports/backtest.json";
    std::ofstream(out) << res.dump(1);
    std::printf("\n  Results: %s   (HTML: python3 python/report.py %s)\n\n", out.c_str(), out.c_str());
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string cmd = argc > 1 ? argv[1] : "";
    const auto args = parse_args(argc, argv);
    try {
        if (cmd == "demo" || cmd == "backtest") {
            const Settings s = Settings::load(args.count("config") ? args.at("config") : "config/settings.yaml");
            return cmd == "demo" ? run_demo(s, args) : run_backtest_cmd(s, args);
        }
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
    std::cout << "usage: groww_engine demo [--speed X] [--duration SEC] [-s id,id]\n"
                 "       groww_engine backtest [-s id,id] [--days N] [--interval MIN] [--seed N] [--out FILE]\n"
                 "       (run from the project root; --config PATH for another settings file)\n";
    return cmd.empty() ? 0 : 2;
}
