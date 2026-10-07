#pragma once
// Event bus with a single dispatch thread.
//
//  * The feed thread pushes ticks into a lock-free SPSC ring (hot path).
//  * Other threads (timer) post events into a small mutex-protected queue.
//  * ONE engine thread dispatches everything, so strategy / risk / portfolio
//    code is single-threaded and needs no locks.
//  * Code running inside a handler calls emit(): the event is queued and
//    dispatched right after the current handler returns (no re-entrancy).
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <variant>

#include "ge/core/models.hpp"
#include "ge/core/spsc_ring.hpp"

namespace ge {

struct TimerEvent {};

using Event = std::variant<Tick, Candle, Fill, TimerEvent>;

class EventBus {
public:
    using Handler = std::function<void(const Event&)>;

    explicit EventBus(std::size_t tick_capacity = 1u << 16) : ticks_(tick_capacity) {}

    void set_handler(Handler h) { handler_ = std::move(h); }
    void post_tick(const Tick& t);  // feed thread only (single producer), lock-free
    void post(Event e);             // any thread
    void emit(Event e);             // from inside a handler: runs after it returns
    void dispatch(Event e) { emit(std::move(e)); }  // synchronous (backtests, tests)
    void run(const std::atomic<bool>& stop);       // engine thread loop

    std::uint64_t dispatched() const { return dispatched_; }

private:
    void drain();

    Handler handler_;
    SpscRing<Tick> ticks_;
    std::mutex mu_;
    std::deque<Event> queue_;  // cross-thread events
    std::deque<Event> local_;  // engine-thread only
    bool dispatching_ = false;
    std::uint64_t dispatched_ = 0;
};

}  // namespace ge
