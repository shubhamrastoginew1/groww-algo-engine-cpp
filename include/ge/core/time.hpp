#pragma once
// Time, clocks and the NSE trading calendar.
//
// Every timestamp is an int64 count of nanoseconds since the Unix epoch (UTC).
// India has no daylight saving, so IST wall-clock time is a fixed +05:30 shift;
// that keeps all calendar maths branch-free integer arithmetic (no tz database).
//
// The engine never reads the system clock directly: it asks a Clock. The same
// strategy code therefore runs live (RealClock), in the offline demo (SimClock
// advanced faster than real time) and in backtests (SimClock set per bar).
#include <atomic>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace ge {

using Timestamp = std::int64_t;  // ns since 1970-01-01T00:00:00Z

inline constexpr Timestamp kNsPerSec = 1'000'000'000LL;
inline constexpr Timestamp kNsPerMin = 60 * kNsPerSec;
inline constexpr Timestamp kNsPerDay = 86'400 * kNsPerSec;
inline constexpr int kIstOffsetSec = 5 * 3600 + 30 * 60;

constexpr Timestamp from_seconds(double s) { return static_cast<Timestamp>(s * 1e9); }
constexpr Timestamp from_minutes(std::int64_t m) { return m * kNsPerMin; }
constexpr double to_seconds(Timestamp ns) { return static_cast<double>(ns) / 1e9; }
constexpr int hhmm(int h, int m) { return h * 3600 + m * 60; }  // seconds since midnight

// Python-style floor division / modulo (correct for negative numerators).
constexpr std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}
constexpr std::int64_t floor_mod(std::int64_t a, std::int64_t b) { return a - floor_div(a, b) * b; }

struct Date {
    int y = 1970;
    int m = 1;
    int d = 1;

    static Date from_days(std::int64_t days_since_epoch);
    static Date parse(std::string_view iso);  // "YYYY-MM-DD"
    std::int64_t days() const;                 // days since 1970-01-01
    int weekday() const;                       // Monday = 0 ... Sunday = 6 (like Python)
    std::string iso() const;
    Date add_days(std::int64_t n) const { return from_days(days() + n); }
    auto operator<=>(const Date&) const = default;
};

Date ist_date(Timestamp ts);
int ist_seconds(Timestamp ts);                        // seconds since IST midnight
Timestamp ist_time(Date d, int sec_of_day = 0);        // IST wall clock -> Timestamp
std::string iso_ist(Timestamp ts);                    // 2026-10-07T10:00:00+05:30
std::string format_ist(Timestamp ts, const char* strftime_fmt);
int parse_hhmm(std::string_view s);                   // "15:15" -> seconds since midnight
std::string hhmm_str(int sec_of_day);                 // 55800 -> "15:30"
Timestamp parse_ist(std::string_view s);              // "2026-10-07 09:15[:00][.123][+05:30|Z]"
Timestamp wall_now();                                 // system clock

// --------------------------------------------------------------------------
class Clock {
public:
    virtual ~Clock() = default;
    virtual Timestamp now() const = 0;
    Date today() const { return ist_date(now()); }
};

class RealClock final : public Clock {
public:
    Timestamp now() const override { return wall_now(); }
};

// Manually driven clock (backtests, demo). Lock-free: the simulator thread
// advances it while the engine and dashboard threads read it.
class SimClock final : public Clock {
public:
    explicit SimClock(Timestamp start) : now_(start) {}
    Timestamp now() const override { return now_.load(std::memory_order_acquire); }
    void set(Timestamp t) { now_.store(t, std::memory_order_release); }
    Timestamp advance(Timestamp dt) { return now_.fetch_add(dt, std::memory_order_acq_rel) + dt; }

private:
    std::atomic<Timestamp> now_;
};

// --------------------------------------------------------------------------
inline constexpr int kPreOpen = hhmm(9, 0);
inline constexpr int kMarketOpen = hhmm(9, 15);
inline constexpr int kMarketClose = hhmm(15, 30);

// NSE cash / F&O session calendar: weekends + configured holidays.
class MarketCalendar {
public:
    explicit MarketCalendar(const std::vector<std::string>& holidays = {}, int open = kMarketOpen,
                            int close = kMarketClose);

    bool is_trading_day(Date d) const;
    bool is_open(Timestamp ts) const;
    Timestamp session_open(Date d) const { return ist_time(d, open_); }
    Timestamp session_close(Date d) const { return ist_time(d, close_); }
    Date next_trading_day(Date d) const;
    Date prev_trading_day(Date d) const;
    Timestamp next_open(Timestamp ts) const;
    std::string status(Timestamp ts) const;  // OPEN | PRE_OPEN | CLOSED | HOLIDAY

    int open_time() const { return open_; }
    int close_time() const { return close_; }
    const std::set<Date>& holidays() const { return holidays_; }

private:
    std::set<Date> holidays_;
    int open_;
    int close_;
};

}  // namespace ge
