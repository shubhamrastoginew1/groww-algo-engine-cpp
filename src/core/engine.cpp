#include "ge/core/engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace ge {

Engine::Engine(Settings settings, Broker& broker, InstrumentStore& store, Clock& clock, std::string mode,
               bool realtime)
    : settings_(std::move(settings)),
      broker_(broker),
      store_(store),
      clock_(clock),
      mode_(std::move(mode)),
      realtime_(realtime),
      calendar_(settings_.holidays),
      risk_(settings_.risk, positions_, calendar_),
      candles_(settings_.engine.candle_interval, [this](const Candle& c) { bus_.emit(Event{c}); }),
      square_off_time_(parse_hhmm(settings_.engine.square_off_time)) {
    ltp_.assign(store_.size(), kNaN);
    bus_.set_handler([this](const Event& e) { handle(e); });
    broker_.set_fill_sink([this](Order& o, double px, double charges) { on_broker_fill(o, px, charges); });
}

Engine::~Engine() { stop(); }

// ------------------------------------------------------------------ setup
void Engine::load_strategies(const std::vector<std::string>& only) {
    for (const auto& cfg : settings_.strategies) {
        const bool wanted = only.empty() ? cfg.enabled : std::find(only.begin(), only.end(), cfg.id) != only.end();
        if (wanted) add_strategy(cfg);
    }
}

Strategy* Engine::add_strategy(const StrategyConfig& cfg) {
    strategies_.push_back(create_strategy(cfg, *this));
    Strategy* s = strategies_.back().get();
    try {
        s->on_init();
    } catch (const std::exception& e) {  // a broken strategy never takes the engine down
        s->status = StrategyStatus::Error;
        s->error = e.what();
        log_.error("strategy " + cfg.id + " failed to initialise: " + e.what());
    }
    return s;
}

void Engine::subscribe(Strategy& s, const Instrument& inst) {
    subs_[inst.id].push_back(&s);
    if (std::find(subscribed_.begin(), subscribed_.end(), inst.id) == subscribed_.end()) subscribed_.push_back(inst.id);
}

// -------------------------------------------------------------- lifecycle
void Engine::start() {
    stop_ = false;
    engine_thread_ = std::thread([this] { bus_.run(stop_); });
    timer_thread_ = std::thread([this] {
        const auto period = std::chrono::duration<double>(settings_.engine.timer_interval_sec);
        while (!stop_) {
            std::this_thread::sleep_for(period);
            bus_.post(Event{TimerEvent{}});
        }
    });
    log_.info(cat("engine started (", mode_, ", ", strategies_.size(), " strategies)"));
}

void Engine::stop() {
    stop_ = true;
    if (timer_thread_.joinable()) timer_thread_.join();
    if (engine_thread_.joinable()) engine_thread_.join();
}

// ----------------------------------------------------------------- inputs
void Engine::on_market_tick(const Tick& t) {
    if (realtime_)
        bus_.post_tick(t);  // lock-free hand-off to the engine thread
    else
        bus_.dispatch(Event{t});
}

Order* Engine::submit_order(Order o) {
    const Instrument& inst = store_.get(o.symbol);
    o.order_id = new_order_id(o.strategy_id);
    o.created_at = clock_.now();
    if (o.price > 0) o.price = round_to_tick(o.price, inst.tick_size);
    orders_.push_back(std::move(o));
    Order& ord = orders_.back();

    const double ref_px = ord.price > 0 ? ord.price : ltp(ord.symbol).value_or(0.0);
    if (const RiskVerdict v = risk_.check(ord, inst, ref_px, clock_.now()); !v.ok) {
        ord.status = OrderStatus::Rejected;
        ord.message = "RISK: " + v.reason;
        log_.warn(cat("[", ord.strategy_id, "] ", to_string(ord.side), " ", ord.qty, " ", inst.symbol, " rejected: ",
                      v.reason));
        return &ord;
    }
    log_.info(cat("[", ord.strategy_id, "] -> ", to_string(ord.side), " ", ord.qty, " ", inst.symbol, " ",
                  to_string(ord.type), " (", ord.tag, ")"));
    broker_.place(ord);
    return &ord;
}

void Engine::cancel_order(Order& o) {
    if (o.is_open()) broker_.cancel(o);
}

void Engine::square_off(const std::string& sid, const std::string& reason) {
    for (Order& o : orders_)
        if (o.is_open() && (sid.empty() || o.strategy_id == sid)) cancel_order(o);
    for (const Position* p : positions_.open_positions(sid)) {
        log_.warn(cat("[", p->strategy_id, "] squaring off ", p->qty, " ", store_.name(p->symbol), ": ", reason));
        Order o;
        o.symbol = p->symbol;
        o.side = p->qty > 0 ? Side::Sell : Side::Buy;
        o.qty = std::abs(p->qty);
        o.strategy_id = p->strategy_id;
        o.tag = "square_off";
        submit_order(std::move(o));
    }
}

// -------------------------------------------------------------- dispatch
void Engine::handle(const Event& ev) {
    if (auto* t = std::get_if<Tick>(&ev)) on_tick(*t);
    else if (auto* c = std::get_if<Candle>(&ev)) on_candle(*c);
    else if (auto* f = std::get_if<Fill>(&ev)) on_fill(*f);
    else on_timer();
}

void Engine::on_tick(const Tick& t) {
    if (t.symbol >= ltp_.size()) ltp_.resize(t.symbol + 1, kNaN);
    ltp_[t.symbol] = t.ltp;
    ++ticks_;
    positions_.mark(t.symbol, t.ltp);
    broker_.on_tick(t);
    if (realtime_) candles_.on_tick(t);  // backtests feed candles directly
    for (Strategy* s : subs_[t.symbol])
        if (s->status == StrategyStatus::Running) s->on_tick(t);
}

void Engine::on_candle(const Candle& c) {
    for (Strategy* s : subs_[c.symbol])
        if (s->status == StrategyStatus::Running) s->on_candle(c);
}

void Engine::on_broker_fill(Order& o, double price, double charges) {
    o.status = OrderStatus::Filled;
    o.filled_qty = o.qty;
    o.avg_price = price;
    // position update + strategy callbacks run after the current handler returns
    bus_.emit(Event{Fill{o.order_id, o.symbol, o.side, o.qty, price, clock_.now(), o.strategy_id, charges}});
}

void Engine::on_fill(const Fill& f) {
    const auto trade = positions_.apply_fill(f, store_.name(f.symbol));
    log_.info(cat("FILL [", f.strategy_id, "] ", to_string(f.side), " ", f.qty, " ", store_.name(f.symbol), " @ ",
                  fixed(f.price, 2), trade ? " | P&L " + fmt_inr(trade->pnl) : ""));
    for (auto& s : strategies_)
        if (s->id() == f.strategy_id) s->on_fill(f);
}

void Engine::on_timer() {
    const Timestamp now = clock_.now();
    if (realtime_) candles_.flush(now - from_seconds(2));  // close candles of quiet symbols
    if (risk_.daily_loss_breached(now)) square_off({}, risk_.kill_reason());
    const Date today = ist_date(now);
    if (calendar_.is_open(now) && ist_seconds(now) >= square_off_time_ && squared_off_on_ != today) {
        squared_off_on_ = today;  // intraday: flatten once per day
        square_off({}, "auto square-off at " + settings_.engine.square_off_time);
    }
    positions_.snapshot_equity(now);
    if (realtime_) write_state_file();
}

// ---------------------------------------------------------------- queries
std::optional<double> Engine::ltp(SymbolId s) const {
    if (s >= ltp_.size() || std::isnan(ltp_[s])) return std::nullopt;
    return ltp_[s];
}

nlohmann::json Engine::snapshot() const {
    using nlohmann::json;
    const Timestamp now = clock_.now();
    auto pnl_json = [](const PnL& p) {
        return json{{"realized", round_to(p.realized, 2)}, {"unrealized", round_to(p.unrealized, 2)},
                    {"charges", round_to(p.charges, 2)}, {"total", p.total}};
    };
    json strategies = json::array(), positions = json::array(), orders = json::array(), trades = json::array(),
         equity = json::array(), ltp = json::object();
    for (const auto& s : strategies_)
        strategies.push_back({{"id", s->id()}, {"type", s->type}, {"status", to_string(s->status)},
                              {"error", s->error}, {"last_signal", s->last_signal}, {"state", s->state},
                              {"pnl", pnl_json(positions_.pnl(s->id()))}});
    for (const Position* p : positions_.all())
        positions.push_back({{"strategy_id", p->strategy_id}, {"symbol", store_.name(p->symbol)}, {"qty", p->qty},
                             {"avg_price", round_to(p->avg_price, 2)}, {"last_price", p->last_price},
                             {"unrealized", round_to(p->unrealized(), 2)}, {"realized", round_to(p->realized, 2)},
                             {"total", round_to(p->total(), 2)}});
    for (auto it = orders_.rbegin(); it != orders_.rend() && orders.size() < 50; ++it)
        orders.push_back({{"time", format_ist(it->created_at, "%H:%M:%S")}, {"strategy_id", it->strategy_id},
                          {"symbol", store_.name(it->symbol)}, {"side", to_string(it->side)}, {"qty", it->qty},
                          {"type", to_string(it->type)}, {"status", to_string(it->status)},
                          {"avg_price", it->avg_price}, {"tag", it->tag}, {"message", it->message}});
    const auto& tr = positions_.trades();
    for (auto it = tr.rbegin(); it != tr.rend() && trades.size() < 50; ++it)
        trades.push_back({{"time", format_ist(it->exit_time, "%H:%M:%S")}, {"strategy_id", it->strategy_id},
                          {"symbol", it->symbol}, {"direction", it->direction}, {"qty", it->qty},
                          {"entry", round_to(it->entry_price, 2)}, {"exit", it->exit_price}, {"pnl", it->pnl}});
    const auto& eq = positions_.equity_curve();
    for (size_t i = eq.size() > 300 ? eq.size() - 300 : 0; i < eq.size(); ++i) equity.push_back(eq[i].second);
    for (SymbolId s : subscribed_) ltp[store_.name(s)] = this->ltp(s) ? json(*this->ltp(s)) : json();
    return {{"mode", mode_},          {"clock", format_ist(now, "%a %d %b %Y %H:%M:%S")},
            {"market", calendar_.status(now)},
            {"ticks", ticks_},        {"pnl", pnl_json(positions_.pnl())},
            {"day_pnl", risk_.day_pnl()},
            {"kill_switch", risk_.kill_switch()},
            {"kill_reason", risk_.kill_reason()},
            {"strategies", strategies}, {"positions", positions}, {"orders", orders},
            {"trades", trades},       {"equity", equity},          {"ltp", ltp}};
}

void Engine::write_state_file() const {
    namespace fs = std::filesystem;
    const fs::path path = settings_.engine.state_file;
    std::error_code ec;
    if (path.has_parent_path()) fs::create_directories(path.parent_path(), ec);
    const fs::path tmp = path.string() + ".tmp";
    { std::ofstream(tmp) << snapshot().dump(); }
    fs::rename(tmp, path, ec);  // atomic replace: readers never see a half-written file
}

}  // namespace ge
