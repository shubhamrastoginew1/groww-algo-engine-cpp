#pragma once
// Broker interface. The engine only talks to this; PaperBroker simulates
// fills. A live Groww broker would implement the same three calls.
#include <functional>

#include "ge/core/models.hpp"

namespace ge {

class Broker {
public:
    // (order, fill price, charges): called on the engine thread when an order executes.
    using FillSink = std::function<void(Order&, double, double)>;

    virtual ~Broker() = default;
    virtual void place(Order& o) = 0;   // may fill synchronously
    virtual void cancel(Order& o) = 0;
    virtual void on_tick(const Tick&) {}
    virtual void on_bar(const Candle&) {}

    void set_fill_sink(FillSink s) { fill_ = std::move(s); }

protected:
    FillSink fill_;
};

}  // namespace ge
