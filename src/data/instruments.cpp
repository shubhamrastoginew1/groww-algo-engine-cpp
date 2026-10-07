#include "ge/data/instruments.hpp"

#include <stdexcept>

namespace ge {
namespace {

const char* kMonths[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

int days_in_month(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : d[m - 1];
}

}  // namespace

SymbolId InstrumentStore::add(Instrument inst) {
    const auto id = static_cast<SymbolId>(insts_.size());
    inst.id = id;
    by_symbol_[inst.symbol] = id;
    insts_.push_back(std::move(inst));
    return id;
}

const Instrument* InstrumentStore::find(std::string_view symbol) const {
    auto it = by_symbol_.find(std::string(symbol));
    return it == by_symbol_.end() ? nullptr : &insts_[it->second];
}

const Instrument& InstrumentStore::at(std::string_view symbol) const {
    if (const Instrument* i = find(symbol)) return *i;
    throw std::out_of_range("instrument '" + std::string(symbol) + "' not found");
}

const Instrument& InstrumentStore::future(std::string_view und, int n, Date on_or_after) const {
    std::vector<const Instrument*> futs;  // insertion order == expiry order
    for (const auto& i : insts_)
        if (i.is_future() && i.underlying == und && i.expiry && *i.expiry >= on_or_after) futs.push_back(&i);
    if (n < 0 || static_cast<size_t>(n) >= futs.size())
        throw std::out_of_range(cat(und, ": no futures expiry #", n));
    return *futs[static_cast<size_t>(n)];
}

const Instrument& InstrumentStore::resolve(std::string_view spec, Date today) const {
    auto parts = split(upper(trim(spec)), ':');
    if (parts.size() == 1) return at(parts[0]);
    if (parts.size() == 3 && parts[1] == "FUT") return future(parts[0], std::stoi(parts[2]), today);
    throw std::invalid_argument("cannot resolve instrument spec '" + std::string(spec) + "'");
}

std::vector<Date> monthly_expiries(Date today, int n, const std::set<Date>& holidays) {
    std::vector<Date> out;
    for (int y = today.y, m = today.m; static_cast<int>(out.size()) < n;) {
        Date d{y, m, days_in_month(y, m)};
        d = d.add_days(-floor_mod(d.weekday() - 1, 7));                  // last Tuesday
        while (d.weekday() >= 5 || holidays.count(d)) d = d.add_days(-1);  // holiday -> earlier
        if (d >= today) out.push_back(d);
        if (++m > 12) m = 1, ++y;
    }
    return out;
}

void InstrumentStore::load_synthetic_universe(const std::map<std::string, double>& base_prices, Date today,
                                              const std::set<Date>& holidays) {
    insts_.clear();
    by_symbol_.clear();
    const std::map<std::string, std::pair<int, double>> fno = {{"NIFTY", {65, 0.1}}, {"BANKNIFTY", {30, 0.2}}};
    for (const auto& [raw, px] : base_prices) {
        const std::string und = upper(raw);
        auto spec = fno.find(und);
        if (spec == fno.end()) {
            add(Instrument{kNoSymbol, und, InstrumentType::EQ, "", 1, 0.05, std::nullopt});
            continue;
        }
        add(Instrument{kNoSymbol, und, InstrumentType::IDX, "", 1, 0.05, std::nullopt});
        for (Date exp : monthly_expiries(today, 3, holidays)) {
            const std::string yy = cat(exp.y % 100 < 10 ? "0" : "", exp.y % 100);
            add(Instrument{kNoSymbol, und + yy + kMonths[exp.m - 1] + "FUT", InstrumentType::FUT, und,
                           spec->second.first, spec->second.second, exp});
        }
    }
}

}  // namespace ge
