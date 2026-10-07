#include "ge/core/models.hpp"

#include <atomic>
#include <cctype>

namespace ge {

const char* to_string(Side s) { return s == Side::Buy ? "BUY" : "SELL"; }
const char* to_string(OrderType t) { return t == OrderType::Market ? "MARKET" : "LIMIT"; }

const char* to_string(InstrumentType t) {
    switch (t) {
        case InstrumentType::EQ: return "EQ";
        case InstrumentType::IDX: return "IDX";
        case InstrumentType::FUT: return "FUT";
    }
    return "?";
}

const char* to_string(OrderStatus s) {
    switch (s) {
        case OrderStatus::Pending: return "PENDING";
        case OrderStatus::Filled: return "FILLED";
        case OrderStatus::Cancelled: return "CANCELLED";
        case OrderStatus::Rejected: return "REJECTED";
    }
    return "?";
}

namespace {
std::string base36(std::uint64_t n, int width) {
    static constexpr char kAlnum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    std::string out(static_cast<size_t>(width), '0');
    for (int i = width - 1; i >= 0; --i, n /= 36) out[static_cast<size_t>(i)] = kAlnum[n % 36];
    return out;
}
std::atomic<std::uint64_t> g_seq{0};
}  // namespace

// PREF-TTTTTTTT-SSSS: 8-20 alphanumerics, at most two hyphens (Groww's rule).
std::string new_order_id(std::string_view prefix) {
    std::string p;
    for (char c : prefix)
        if (std::isalnum(static_cast<unsigned char>(c)) && p.size() < 4)
            p += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (p.empty()) p = "AX";
    const auto ms = static_cast<std::uint64_t>(wall_now() / 1'000'000);
    const std::uint64_t seq = ++g_seq;
    return p + "-" + base36(ms % 2821109907456ULL, 8) + "-" + base36(seq % 1679616ULL, 4);
}

}  // namespace ge
