// Unit + integration tests for the trading core.
#include <thread>

#include <gtest/gtest.h>

#include "ge/backtest/backtester.hpp"
#include "ge/core/engine.hpp"
#include "ge/core/spsc_ring.hpp"
#include "ge/orders/paper_broker.hpp"
#include "ge/strategies/ema_crossover.hpp"

using namespace ge;

namespace {

const Date kDay{2026, 10, 7};  // a Wednesday
Timestamp at(int h, int m) { return ist_time(kDay, hhmm(h, m)); }

Settings test_settings() {
    Settings s = Settings::load(GE_SOURCE_DIR "/config/settings.yaml");
    s.strategies.clear();
    return s;
}

// Synchronous engine (no threads) on a fixed trading day.
struct Harness {
    Settings settings = test_settings();
    InstrumentStore store;
    SimClock clock{at(10, 0)};
    std::unique_ptr<PaperBroker> broker;
    std::unique_ptr<Engine> engine;

    explicit Harness(Timestamp start = at(10, 0)) {
        set_log_level(LogLevel::Error);
        clock.set(start);
        store.load_synthetic_universe(settings.demo.base_prices, kDay);
        broker = std::make_unique<PaperBroker>(settings.paper, store);
        engine = std::make_unique<Engine>(settings, *broker, store, clock, "test", false);
    }
    SymbolId nifty() const { return store.future("NIFTY", 0, kDay).id; }
    SymbolId reliance() const { return store.at("RELIANCE").id; }
    void tick(SymbolId s, double px) { engine->on_market_tick(Tick{s, px, clock.now()}); }
    Order* order(SymbolId s, Side side, int qty, const std::string& sid = "t") {
        Order o;
        o.symbol = s;
        o.side = side;
        o.qty = qty;
        o.strategy_id = sid;
        return engine->submit_order(std::move(o));
    }
    int net(const std::string& sid, SymbolId s) const { return engine->positions().net_qty(sid, s); }
};

}  // namespace

// ------------------------------------------------------------------- core
TEST(Core, CalendarAndTime) {
    MarketCalendar cal({"2026-10-20"});
    EXPECT_TRUE(cal.is_open(at(9, 15)));
    EXPECT_FALSE(cal.is_open(at(15, 30)));
    EXPECT_FALSE(cal.is_trading_day(Date{2026, 10, 20}));  // holiday
    EXPECT_EQ(ist_date(cal.next_open(ist_time(Date{2026, 10, 9}, hhmm(16, 0)))).iso(), "2026-10-12");  // Fri -> Mon
    EXPECT_EQ(iso_ist(parse_ist("2026-10-07 09:15")), "2026-10-07T09:15:00+05:30");
}

TEST(Core, InstrumentResolver) {
    Harness h;
    const Instrument& near = h.store.resolve("NIFTY:FUT:0", kDay);
    const Instrument& next = h.store.resolve("NIFTY:FUT:1", kDay);
    EXPECT_EQ(near.symbol, "NIFTY26OCTFUT");  // expiry: last Tuesday = 27 Oct 2026
    EXPECT_EQ(near.lot_size, 65);
    EXPECT_GT(*next.expiry, *near.expiry);
    EXPECT_EQ(h.store.resolve("RELIANCE", kDay).type, InstrumentType::EQ);
    EXPECT_THROW(h.store.resolve("NOPE", kDay), std::out_of_range);
}

TEST(Core, CandleBuilderAlignsTo0915) {
    std::vector<Candle> out;
    CandleBuilder cb(5, [&](const Candle& c) { out.push_back(c); });
    const double px[] = {100, 103, 99, 101};
    for (int i = 0; i < 4; ++i) cb.on_tick(Tick{1, px[i], at(9, 15) + from_seconds(10 + 60 * i)});
    cb.on_tick(Tick{1, 105, at(9, 20) + from_seconds(10)});  // next bucket closes the first candle
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].ts, at(9, 15));
    EXPECT_EQ(std::vector<double>({out[0].open, out[0].high, out[0].low, out[0].close}),
              std::vector<double>({100, 103, 99, 101}));
}

TEST(Core, PositionAverageCostAndFlip) {
    PositionManager pm;
    pm.apply_fill(Fill{"1", 7, Side::Buy, 100, 10.0, 0, "s"}, "X");
    pm.apply_fill(Fill{"2", 7, Side::Buy, 100, 12.0, 0, "s"}, "X");
    EXPECT_DOUBLE_EQ(pm.find("s", 7)->avg_price, 11.0);
    auto t = pm.apply_fill(Fill{"3", 7, Side::Sell, 250, 10.0, 0, "s", 20}, "X");  // close 200, flip short 50
    ASSERT_TRUE(t);
    EXPECT_DOUBLE_EQ(t->pnl, -200.0);
    EXPECT_EQ(pm.net_qty("s", 7), -50);
    pm.mark(7, 9.0);
    EXPECT_DOUBLE_EQ(pm.pnl().total, -200.0 + 50.0 - 20.0);
}

TEST(Core, SpscRingKeepsOrderAcrossThreads) {
    SpscRing<Tick> ring(1024);
    constexpr int kN = 1'000'000;
    std::thread producer([&] {
        for (int i = 0; i < kN; ++i)
            while (!ring.try_push(Tick{0, 0.0, i})) std::this_thread::yield();
    });
    Tick t;
    for (int expected = 0; expected < kN;)
        if (ring.try_pop(t)) ASSERT_EQ(t.ts, expected++);
    producer.join();
}

// -------------------------------------------------------------- execution
TEST(Execution, MarketOrderFillsWithSlippage) {
    Harness h;
    h.tick(h.nifty(), 25000.0);
    Order* o = h.order(h.nifty(), Side::Buy, 65);
    EXPECT_EQ(o->status, OrderStatus::Filled);
    EXPECT_DOUBLE_EQ(o->avg_price, 25002.5);  // 1 bp slippage, rounded to the 0.1 tick
    EXPECT_EQ(h.net("t", h.nifty()), 65);
    EXPECT_GT(h.engine->positions().pnl().charges, 0);
}

TEST(Execution, LimitOrderRestsUntilPriceTrades) {
    Harness h;
    h.tick(h.nifty(), 25000.0);
    Order o;
    o.symbol = h.nifty();
    o.qty = 65;
    o.type = OrderType::Limit;
    o.price = 24990.0;
    o.strategy_id = "t";
    Order* lim = h.engine->submit_order(o);
    EXPECT_EQ(lim->status, OrderStatus::Pending);
    h.tick(h.nifty(), 24985.0);
    EXPECT_EQ(lim->status, OrderStatus::Filled);
    EXPECT_DOUBLE_EQ(lim->avg_price, 24985.0);
}

// ------------------------------------------------------------------- risk
TEST(Risk, LotSizeAndLimits) {
    Harness h;
    h.tick(h.nifty(), 25000.0);
    EXPECT_NE(h.order(h.nifty(), Side::Buy, 60)->message.find("lot size"), std::string::npos);
    EXPECT_NE(h.order(h.nifty(), Side::Buy, 65 * 21)->message.find("max_lots"), std::string::npos);
    EXPECT_NE(h.order(h.reliance(), Side::Buy, 6000)->message.find("max_qty"), std::string::npos);
}

TEST(Risk, KillSwitchBlocksEntriesButAllowsExits) {
    Harness h;
    h.tick(h.nifty(), 25000.0);
    h.order(h.nifty(), Side::Buy, 65);
    h.engine->risk().activate_kill_switch("test");
    EXPECT_EQ(h.order(h.reliance(), Side::Buy, 10)->status, OrderStatus::Rejected);
    EXPECT_EQ(h.order(h.nifty(), Side::Sell, 65)->status, OrderStatus::Filled);  // exit
}

TEST(Risk, MarketHoursAndEntryCutoff) {
    Harness h(at(15, 5));
    h.tick(h.nifty(), 25000.0);
    EXPECT_NE(h.order(h.nifty(), Side::Buy, 65)->message.find("no new entries"), std::string::npos);
    h.clock.set(at(16, 0));
    EXPECT_NE(h.order(h.nifty(), Side::Buy, 65)->message.find("market closed"), std::string::npos);
}

TEST(Risk, DailyLossLimitFlattensEverything) {
    Harness h;
    h.engine->risk().cfg.max_daily_loss = 5000;
    h.tick(h.nifty(), 25000.0);
    h.engine->bus().dispatch(Event{TimerEvent{}});  // start of day baseline
    h.order(h.nifty(), Side::Buy, 65);
    h.tick(h.nifty(), 24800.0);  // about -13k MTM
    h.engine->bus().dispatch(Event{TimerEvent{}});
    EXPECT_TRUE(h.engine->risk().kill_switch());
    EXPECT_EQ(h.net("t", h.nifty()), 0);
}

// ------------------------------------------------------------- strategies
TEST(Strategy, EmaCrossoverGoesLongThenReverses) {
    Harness h;
    auto* s = static_cast<EmaCrossover*>(h.engine->add_strategy(
        {"ema", "ema_crossover", true, {{"instrument", "NIFTY:FUT:0"}, {"fast", 3}, {"slow", 6}}}));
    ASSERT_EQ(s->status, StrategyStatus::Running) << s->error;
    Timestamp ts = at(10, 0);
    auto feed = [&](double px) {
        h.tick(s->symbol(), px);
        h.engine->bus().dispatch(Event{Candle{s->symbol(), ts, px, px, px, px}});
        ts += from_minutes(5);
    };
    for (int i = 0; i < 12; ++i) feed(25000 - 10 * i);  // down trend
    for (int i = 0; i < 10; ++i) feed(24890 + 25 * i);  // up-cross
    EXPECT_EQ(h.net("ema", s->symbol()), 65);
    for (int i = 0; i < 10; ++i) feed(25100 - 40 * i);  // down-cross
    EXPECT_EQ(h.net("ema", s->symbol()), -65);
}

TEST(Strategy, BadConfigIsIsolated) {
    Harness h;
    Strategy* s = h.engine->add_strategy({"bad", "ema_crossover", true, {{"instrument", "DOES_NOT_EXIST"}}});
    EXPECT_EQ(s->status, StrategyStatus::Error);
    EXPECT_THROW(h.engine->add_strategy({"x", "no_such_type", true, {}}), std::invalid_argument);
}

// --------------------------------------------------------------- backtest
TEST(Backtest, SyntheticEndToEndIsDeterministic) {
    set_log_level(LogLevel::Error);
    Settings s = test_settings();
    s.strategies = {{"ema", "ema_crossover", true, {{"instrument", "NIFTY:FUT:0"}, {"fast", 9}, {"slow", 21}}}};
    BacktestOptions o{{"ema"}, parse_ist("2026-09-28 09:15"), parse_ist("2026-10-06 15:30"), 5, 3};
    const auto a = run_backtest(s, o);
    EXPECT_GT(a["metrics"]["trades"].get<int>(), 0);
    EXPECT_GT(a["equity_curve"].size(), 10u);
    EXPECT_EQ(run_backtest(s, o)["metrics"], a["metrics"]);  // same seed, same result
}
