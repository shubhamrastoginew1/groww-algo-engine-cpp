#include "ge/data/simulator.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

#include "ge/data/candle_builder.hpp"

namespace ge {
namespace {
constexpr double kRate = 0.065;
constexpr double kSecondsPerTradingYear = 252.0 * 375 * 60;
}  // namespace

MarketSimulator::MarketSimulator(const std::map<std::string, double>& base_prices, double annual_vol, unsigned seed)
    : rng_(seed), vol_(annual_vol) {
    for (const auto& [k, v] : base_prices) spot_[upper(k)] = v;
}

void MarketSimulator::advance(double dt) {
    const double sigma = vol_ / std::sqrt(kSecondsPerTradingYear);
    for (auto& [und, s] : spot_) s *= std::exp(-0.5 * sigma * sigma * dt + sigma * std::sqrt(dt) * normal_(rng_));
}

double MarketSimulator::price(const Instrument& inst, Timestamp now) const {
    const std::string& und = inst.is_future() ? inst.underlying : inst.symbol;
    double px = spot_.at(und);
    if (inst.is_future() && inst.expiry) {
        const double years = std::max(to_seconds(ist_time(*inst.expiry, kMarketClose) - now), 0.0) / (365.0 * 86400);
        px *= std::exp(kRate * years);
    }
    return round_to_tick(px, inst.tick_size);
}

std::vector<Timestamp> trading_minutes(const MarketCalendar& cal, Timestamp start, Timestamp end) {
    std::vector<Timestamp> out;
    for (Date d = ist_date(start); cal.session_open(d) < end; d = d.add_days(1)) {
        if (!cal.is_trading_day(d)) continue;
        Timestamp t = std::max(cal.session_open(d), start);
        t -= floor_mod(t, kNsPerMin);
        for (const Timestamp close = std::min(cal.session_close(d), end); t < close; t += kNsPerMin) out.push_back(t);
    }
    return out;
}

std::unordered_map<SymbolId, std::vector<Candle>> generate_history(
    const std::vector<const Instrument*>& insts, Timestamp start, Timestamp end, int interval_min,
    const std::map<std::string, double>& base_prices, const MarketCalendar& cal, double annual_vol, unsigned seed) {
    MarketSimulator sim(base_prices, annual_vol, seed);
    std::unordered_map<SymbolId, std::vector<Candle>> out;
    CandleBuilder builder(interval_min, [&](const Candle& c) { out[c.symbol].push_back(c); });
    Date prev_day{};
    for (Timestamp ts : trading_minutes(cal, start, end)) {
        if (ist_date(ts) != prev_day) sim.advance(3600);  // overnight gap
        prev_day = ist_date(ts);
        sim.advance(60);
        for (const auto* inst : insts) builder.on_tick(Tick{inst->id, sim.price(*inst, ts), ts});
    }
    builder.flush(end + kNsPerDay);
    return out;
}

// --------------------------------------------------------------------------
SimulatedFeed::SimulatedFeed(SimClock& clock, const MarketCalendar& cal,
                             const std::map<std::string, double>& base_prices, double speed, double tick_interval_sec,
                             double annual_vol, unsigned seed)
    : clock_(clock), cal_(cal), sim_(base_prices, annual_vol, seed), speed_(speed), tick_interval_(tick_interval_sec) {}

void SimulatedFeed::start(TickSink sink) {
    if (!cal_.is_open(clock_.now())) clock_.set(cal_.next_open(clock_.now()));
    stop_ = false;
    thread_ = std::thread([this, sink = std::move(sink)] {
        const double dt = tick_interval_ * speed_;
        while (!stop_) {
            std::this_thread::sleep_for(std::chrono::duration<double>(tick_interval_));
            Timestamp now = clock_.advance(from_seconds(dt));
            if (!cal_.is_open(now)) {  // session over: jump to the next open
                clock_.set(cal_.next_open(now));
                sim_.advance(1800);
                continue;
            }
            sim_.advance(dt);
            for (const auto* inst : insts_) sink(Tick{inst->id, sim_.price(*inst, now), now});
        }
    });
}

void SimulatedFeed::stop() {
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}

}  // namespace ge
