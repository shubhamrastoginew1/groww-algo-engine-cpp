#include "ge/core/event_bus.hpp"

#include <chrono>
#include <thread>

#include "ge/core/logger.hpp"

namespace ge {

void EventBus::post_tick(const Tick& t) {
    while (!ticks_.try_push(t)) std::this_thread::yield();  // back-pressure: never drop market data
}

void EventBus::post(Event e) {
    std::lock_guard lk(mu_);
    queue_.push_back(std::move(e));
}

void EventBus::emit(Event e) {
    local_.push_back(std::move(e));
    if (!dispatching_) drain();
}

void EventBus::drain() {
    dispatching_ = true;
    while (!local_.empty()) {
        Event ev = std::move(local_.front());
        local_.pop_front();
        try {
            if (handler_) handler_(ev);
        } catch (const std::exception& ex) {  // keep the engine alive
            Logger("event_bus").error(std::string("handler failed: ") + ex.what());
        }
        ++dispatched_;
    }
    dispatching_ = false;
}

void EventBus::run(const std::atomic<bool>& stop) {
    std::deque<Event> batch;
    Tick t;
    while (!stop.load(std::memory_order_acquire)) {
        bool idle = true;
        for (int n = 0; n < 512 && ticks_.try_pop(t); ++n) {  // market data first, bounded batch
            emit(Event{t});
            idle = false;
        }
        {
            std::lock_guard lk(mu_);
            batch.swap(queue_);
        }
        for (auto& e : batch) {
            emit(std::move(e));
            idle = false;
        }
        batch.clear();
        if (idle) std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
}

}  // namespace ge
