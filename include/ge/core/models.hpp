#pragma once
// Domain models shared by every layer.
//
// Symbols are interned: each Instrument gets a dense SymbolId (uint32) from
// the InstrumentStore. Ticks and candles carry the id instead of a string, so
// they are trivially copyable (they travel through a lock-free ring buffer
// without allocating) and per-symbol state can live in flat vectors.
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "ge/core/time.hpp"
#include "ge/core/util.hpp"

namespace ge {

using SymbolId = std::uint32_t;
inline constexpr SymbolId kNoSymbol = 0xFFFF'FFFFu;

enum class Side : std::int8_t { Buy = 1, Sell = -1 };
constexpr int sign(Side s) { return static_cast<int>(s); }
constexpr Side opposite(Side s) { return s == Side::Buy ? Side::Sell : Side::Buy; }

enum class OrderType : std::uint8_t { Market, Limit };
enum class InstrumentType : std::uint8_t { EQ, IDX, FUT };
enum class OrderStatus : std::uint8_t { Pending, Filled, Cancelled, Rejected };

constexpr bool is_terminal(OrderStatus s) { return s != OrderStatus::Pending; }

const char* to_string(Side);
const char* to_string(OrderType);
const char* to_string(InstrumentType);
const char* to_string(OrderStatus);

struct Instrument {
    SymbolId id = kNoSymbol;
    std::string symbol;            // exchange trading symbol, e.g. NIFTY26OCTFUT
    InstrumentType type = InstrumentType::EQ;
    std::string underlying;        // futures: NIFTY
    int lot_size = 1;
    double tick_size = 0.05;
    std::optional<Date> expiry;

    bool is_future() const { return type == InstrumentType::FUT; }
};

struct Tick {
    SymbolId symbol = kNoSymbol;
    double ltp = 0.0;
    Timestamp ts = 0;
};

struct Candle {
    SymbolId symbol = kNoSymbol;
    Timestamp ts = 0;  // candle start time
    double open = 0, high = 0, low = 0, close = 0;
};

struct Order {
    std::string order_id;
    SymbolId symbol = kNoSymbol;
    Side side = Side::Buy;
    int qty = 0;                  // units (shares / contracts), not lots
    OrderType type = OrderType::Market;
    double price = 0.0;           // limit price (0 for market)
    std::string strategy_id = "manual";
    std::string tag;
    OrderStatus status = OrderStatus::Pending;
    int filled_qty = 0;
    double avg_price = 0.0;
    std::string message;
    Timestamp created_at = 0;

    bool is_open() const { return !is_terminal(status); }
    int signed_qty() const { return qty * sign(side); }
};

struct Fill {
    std::string order_id;
    SymbolId symbol = kNoSymbol;
    Side side = Side::Buy;
    int qty = 0;
    double price = 0.0;
    Timestamp ts = 0;
    std::string strategy_id;
    double charges = 0.0;
};

// A closed (or partially closed) round trip.
struct Trade {
    std::string strategy_id;
    std::string symbol;
    std::string direction;  // LONG | SHORT
    int qty = 0;
    double entry_price = 0.0;
    double exit_price = 0.0;
    Timestamp entry_time = 0;
    Timestamp exit_time = 0;
    double pnl = 0.0;
};

std::string new_order_id(std::string_view prefix = "AX");  // Groww-compatible order_reference_id

}  // namespace ge
