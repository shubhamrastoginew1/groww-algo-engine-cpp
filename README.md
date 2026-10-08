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
