# Groww Algo Engine: C++ core

A small C++20 port of the trading core of `../shubham_HFT_groww`. The C++ engine
does the trading work. Python only displays the results.

```
SimulatedFeed ──ticks──▶ [lock-free SPSC ring] ──▶ EventBus (1 engine thread)
                                                    │
                     CandleBuilder ◀────────────────┤ ticks
                          │ candles                 │
                          ▼                         ▼
                     Strategies ──orders──▶ RiskManager ──▶ PaperBroker
                                                                │ fills
                     PositionManager ◀──────────────────────────┘
                          │
                          └──▶ run/state.json ──▶ python/dashboard.py
```

## What is in C++

| File | What it does |
|---|---|
| `core/time` | int64 ns timestamps, IST helpers, `SimClock` / `RealClock`, NSE calendar |
| `core/models` | `Instrument`, `Tick`, `Candle`, `Order`, `Fill`, `Trade` (symbols interned as `SymbolId`) |
| `core/spsc_ring.hpp` | lock-free single-producer/single-consumer ring for ticks |
| `core/event_bus` | single-threaded dispatch; `emit()` defers events until the current handler returns |
| `core/engine` | wires feed → strategies → risk → broker → positions, auto square-off, state file |
| `data/` | instrument store + resolver (`NIFTY:FUT:0`), candle builder (09:15 aligned), market simulator |
| `orders/paper_broker` | market / limit fills, slippage, charges |
| `risk/risk_manager` | lot size, max lots / qty / value / open positions, market hours, entry cut-off, kill switch, daily loss limit |
| `portfolio/position_manager` | average-cost positions, realised / unrealised P&L, closed trades, equity curve |
| `strategies/` | `Strategy` base class + `EmaCrossover` (stop-loss / target) |
| `backtest/backtester` | replays candles through the **same** engine; fills at the next bar's open |

Python (`python/`, standard library only): `dashboard.py` (live view of `run/state.json`)
and `report.py` (backtest HTML report).

## Build and run

```bash
cmake -S . -B build && cmake --build build -j8     # fetches nlohmann/json, yaml-cpp, googletest
./build/ge_tests                                   # 14 unit + integration tests

./build/groww_engine backtest --days 20            # all enabled strategies; -s ema_nifty_fut to pick
python3 python/report.py reports/backtest.json     # -> reports/backtest.html

./build/groww_engine demo --speed 300              # terminal 1 (simulated market, paper fills)
python3 python/dashboard.py                        # terminal 2 -> http://localhost:8050
```

Run the commands from the project root. Strategies and limits are set in `config/settings.yaml`.

## Adding a strategy

1. Subclass `Strategy` (see `strategies/ema_crossover.*`) and override `on_init`, `on_candle`, `on_tick` or `on_fill`.
2. Add one line to `create_strategy()` in `src/strategies/strategy.cpp`.
3. Reference the new type in `config/settings.yaml`.

## Design points (interview notes)

- **One engine thread.** Strategy, risk and position code is single-threaded, so it needs no locks.
  Other threads only hand data to that thread.
- **Lock-free tick path.** The feed thread pushes into an SPSC ring: one `release` store per push,
  one `acquire` load per pop, head and tail on separate cache lines (128 bytes on Apple Silicon),
  and cached copies of the other side's index.
- **Interned symbols.** `Tick` is a trivially copyable 24-byte struct (`SymbolId`, price, time), with no
  strings on the hot path. Last traded prices live in a flat `vector<double>` indexed by `SymbolId`.
- **Deferred events.** A fill raised while a strategy is handling a tick is queued and delivered
  after that handler returns, which avoids re-entrant callbacks.
- **Exits always allowed.** Risk blocks new exposure (kill switch, cut-off, limits), never a reducing order.
- **Same code for backtest and demo.** The backtester drives the engine synchronously with a `SimClock`.
  It fills at the next bar's open, so there is no look-ahead.
- **Integer time.** Timestamps are int64 nanoseconds, and IST is a fixed +05:30 offset (no tz database).

## Not ported (still in the Python project)

Live Groww trading (login, websocket feed, REST orders), calendar spread / butterfly strategies
with multi-leg execution, the SQLite journal and Telegram alerts. A live broker would implement
the 3-method `Broker` interface (`orders/broker.hpp`).
