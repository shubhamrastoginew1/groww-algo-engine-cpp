#pragma once
// Instrument master: symbol interning + a tiny resolver.
//
//   RELIANCE        exact trading symbol
//   NIFTY:FUT:0     nearest futures expiry (1 = next month, 2 = far month)
//
// Instruments live in a std::deque so references stay valid as the store
// grows; the SymbolId is the index (O(1) lookup).
#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "ge/core/models.hpp"

namespace ge {

class InstrumentStore {
public:
    SymbolId add(Instrument inst);
    std::size_t size() const { return insts_.size(); }
    const Instrument& get(SymbolId id) const { return insts_.at(id); }
    const std::string& name(SymbolId id) const { return insts_.at(id).symbol; }
    const Instrument* find(std::string_view symbol) const;
    const Instrument& at(std::string_view symbol) const;  // throws if unknown

    const Instrument& future(std::string_view underlying, int n, Date on_or_after) const;
    const Instrument& resolve(std::string_view spec, Date today) const;

    // Offline universe: an index + 3 monthly futures for NIFTY / BANKNIFTY,
    // cash equities for everything else. No network needed.
    void load_synthetic_universe(const std::map<std::string, double>& base_prices, Date today,
                                 const std::set<Date>& holidays = {});

private:
    std::deque<Instrument> insts_;
    std::unordered_map<std::string, SymbolId> by_symbol_;
};

// NSE monthly expiry: last Tuesday of the month, moved earlier over holidays.
std::vector<Date> monthly_expiries(Date today, int n, const std::set<Date>& holidays = {});

}  // namespace ge
