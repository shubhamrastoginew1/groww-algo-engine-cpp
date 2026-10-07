#include "ge/core/time.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <stdexcept>

namespace ge {
namespace {

// Howard Hinnant's civil-date algorithms (proleptic Gregorian calendar).
std::int64_t days_from_civil(int y, unsigned m, unsigned d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

int to_int(std::string_view s, size_t pos, size_t len) {
    if (pos + len > s.size()) throw std::invalid_argument("bad date/time: " + std::string(s));
    int v = 0;
    for (size_t i = pos; i < pos + len; ++i) {
        char c = s[i];
        if (c < '0' || c > '9') throw std::invalid_argument("bad date/time: " + std::string(s));
        v = v * 10 + (c - '0');
    }
    return v;
}

}  // namespace

Date Date::from_days(std::int64_t z) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = static_cast<std::int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    return Date{static_cast<int>(y + (m <= 2)), static_cast<int>(m), static_cast<int>(d)};
}

Date Date::parse(std::string_view s) {
    if (s.size() < 10 || s[4] != '-' || s[7] != '-') throw std::invalid_argument("bad date: " + std::string(s));
    return Date{to_int(s, 0, 4), to_int(s, 5, 2), to_int(s, 8, 2)};
}

std::int64_t Date::days() const {
    return days_from_civil(y, static_cast<unsigned>(m), static_cast<unsigned>(d));
}

int Date::weekday() const { return static_cast<int>(floor_mod(days() + 3, 7)); }  // 1970-01-01 = Thursday

std::string Date::iso() const {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", y, m, d);
    return buf;
}

Date ist_date(Timestamp ts) { return Date::from_days(floor_div(ts + kIstOffsetSec * kNsPerSec, kNsPerDay)); }

int ist_seconds(Timestamp ts) {
    return static_cast<int>(floor_mod(ts + kIstOffsetSec * kNsPerSec, kNsPerDay) / kNsPerSec);
}

Timestamp ist_time(Date d, int sec_of_day) {
    return (d.days() * 86'400 + sec_of_day - kIstOffsetSec) * kNsPerSec;
}

std::string iso_ist(Timestamp ts) {
    Date d = ist_date(ts);
    int s = ist_seconds(ts);
    int ms = static_cast<int>(floor_mod(ts, kNsPerSec) / 1'000'000);
    char buf[48];
    if (ms)
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d.%03d+05:30", d.y, d.m, d.d, s / 3600,
                      s / 60 % 60, s % 60, ms);
    else
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02d+05:30", d.y, d.m, d.d, s / 3600, s / 60 % 60,
                      s % 60);
    return buf;
}

std::string format_ist(Timestamp ts, const char* fmt) {
    Date d = ist_date(ts);
    int s = ist_seconds(ts);
    std::tm tm{};
    tm.tm_year = d.y - 1900;
    tm.tm_mon = d.m - 1;
    tm.tm_mday = d.d;
    tm.tm_hour = s / 3600;
    tm.tm_min = s / 60 % 60;
    tm.tm_sec = s % 60;
    tm.tm_wday = (d.weekday() + 1) % 7;  // tm: Sunday = 0
    char buf[128];
    size_t n = std::strftime(buf, sizeof buf, fmt, &tm);
    return std::string(buf, n);
}

int parse_hhmm(std::string_view s) {
    auto colon = s.find(':');
    if (colon == std::string_view::npos) throw std::invalid_argument("expected HH:MM, got " + std::string(s));
    int h = to_int(s, 0, colon);
    int m = to_int(s, colon + 1, 2);
    return hhmm(h, m);
}

std::string hhmm_str(int sec) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02d:%02d", sec / 3600, sec / 60 % 60);
    return buf;
}

Timestamp parse_ist(std::string_view s) {
    Date d = Date::parse(s);
    int h = 0, mi = 0, se = 0;
    Timestamp frac = 0;
    size_t p = 10;
    if (p < s.size() && (s[p] == ' ' || s[p] == 'T')) {
        h = to_int(s, p + 1, 2);
        mi = to_int(s, p + 4, 2);
        p += 6;
        if (p < s.size() && s[p] == ':') {
            se = to_int(s, p + 1, 2);
            p += 3;
        }
        if (p < s.size() && s[p] == '.') {
            ++p;
            Timestamp scale = kNsPerSec / 10;
            while (p < s.size() && s[p] >= '0' && s[p] <= '9') {
                frac += (s[p] - '0') * scale;
                scale /= 10;
                ++p;
            }
        }
    }
    int offset = kIstOffsetSec;  // naive timestamps are IST
    if (p < s.size()) {
        if (s[p] == 'Z') {
            offset = 0;
        } else if (s[p] == '+' || s[p] == '-') {
            int oh = to_int(s, p + 1, 2);
            int om = (p + 3 < s.size() && s[p + 3] == ':') ? to_int(s, p + 4, 2) : to_int(s, p + 3, 2);
            offset = (s[p] == '-' ? -1 : 1) * (oh * 3600 + om * 60);
        }
    }
    return (d.days() * 86'400 + h * 3600 + mi * 60 + se - offset) * kNsPerSec + frac;
}

Timestamp wall_now() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
}

// --------------------------------------------------------------------------
MarketCalendar::MarketCalendar(const std::vector<std::string>& holidays, int open, int close)
    : open_(open), close_(close) {
    for (const auto& h : holidays) holidays_.insert(Date::parse(h));
}

bool MarketCalendar::is_trading_day(Date d) const { return d.weekday() < 5 && !holidays_.count(d); }

bool MarketCalendar::is_open(Timestamp ts) const {
    int t = ist_seconds(ts);
    return is_trading_day(ist_date(ts)) && t >= open_ && t < close_;
}

Date MarketCalendar::next_trading_day(Date d) const {
    Date n = d.add_days(1);
    while (!is_trading_day(n)) n = n.add_days(1);
    return n;
}

Date MarketCalendar::prev_trading_day(Date d) const {
    Date p = d.add_days(-1);
    while (!is_trading_day(p)) p = p.add_days(-1);
    return p;
}

Timestamp MarketCalendar::next_open(Timestamp ts) const {
    Date d = ist_date(ts);
    if (is_trading_day(d) && ist_seconds(ts) < open_) return session_open(d);
    return session_open(next_trading_day(d));
}

std::string MarketCalendar::status(Timestamp ts) const {
    if (!is_trading_day(ist_date(ts))) return "HOLIDAY";
    int t = ist_seconds(ts);
    if (t < kPreOpen) return "CLOSED";
    if (t < open_) return "PRE_OPEN";
    if (t < close_) return "OPEN";
    return "CLOSED";
}

}  // namespace ge
