#pragma once
// Small string / number helpers shared by every layer.
#include <cmath>
#include <cstdio>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace ge {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// cat("a", 1, 2.5) -> "a12.5" (std::format is not available on every toolchain)
template <class... Args>
std::string cat(Args&&... args) {
    std::ostringstream os;
    (os << ... << std::forward<Args>(args));
    return os.str();
}

inline std::string fixed(double v, int precision = 2) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.*f", precision, v);
    return buf;
}

inline std::string signed_fixed(double v, int precision = 2) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%+.*f", precision, v);
    return buf;
}

// Round half-to-even like Python's round(); nearbyint honours the default FE_TONEAREST mode.
inline double round_to(double v, int decimals) {
    double scale = std::pow(10.0, decimals);
    return std::nearbyint(v * scale) / scale;
}

// Round a price to the instrument tick size (avoids exchange rejections).
inline double round_to_tick(double price, double tick = 0.05) {
    if (!(tick > 0)) return round_to(price, 2);
    return round_to(std::nearbyint(price / tick) * tick, 4);
}

// -12345.5 -> "-₹12,345.50" (western grouping, like the Python f"{v:,.2f}")
inline std::string fmt_inr(double value, int precision = 2) {
    std::string digits = fixed(std::fabs(value), precision);
    auto dot = digits.find('.');
    std::string int_part = digits.substr(0, dot), out;
    int count = 0;
    for (auto it = int_part.rbegin(); it != int_part.rend(); ++it) {
        if (count && count % 3 == 0) out.insert(out.begin(), ',');
        out.insert(out.begin(), *it);
        ++count;
    }
    if (dot != std::string::npos) out += digits.substr(dot);
    return (value < 0 ? "-" : "") + std::string("₹") + out;
}

inline std::string upper(std::string_view s) {
    std::string o(s);
    for (auto& c : o) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return o;
}

inline std::string trim(std::string_view s) {
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string_view::npos ? std::string() : std::string(s.substr(b, e - b + 1));
}

inline std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(sep, start);
        out.emplace_back(s.substr(start, pos == std::string_view::npos ? std::string_view::npos : pos - start));
        if (pos == std::string_view::npos) break;
        start = pos + 1;
    }
    return out;
}

inline bool starts_with(std::string_view s, std::string_view p) { return s.substr(0, p.size()) == p; }
inline bool ends_with(std::string_view s, std::string_view p) {
    return s.size() >= p.size() && s.substr(s.size() - p.size()) == p;
}

}  // namespace ge
