#pragma once
// Bounded lock-free single-producer / single-consumer ring buffer.
//
// Used for the hottest path in the system: market-data ticks travelling from
// the feed thread to the engine thread. No locks, no allocation after
// construction, one release-store per push/pop.
//
//  * head_ is written only by the producer, tail_ only by the consumer. Each
//    sits on its own cache line (no false sharing).
//  * Each side keeps a *cached* copy of the other side's index and only reloads
//    the shared atomic when the cache says the ring looks full / empty, which
//    keeps cross-core cache-line traffic to a minimum.
//  * Indices grow monotonically; slot = index & mask (capacity is a power of 2).
#include <atomic>
#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>

namespace ge {

// Apple Silicon prefetches cache lines in 128-byte pairs, so 64-byte padding
// still lets head_ and tail_ false-share; x86 is fine with 64.
#if defined(__APPLE__) && defined(__aarch64__)
inline constexpr std::size_t kCacheLine = 128;
#else
inline constexpr std::size_t kCacheLine = 64;
#endif

template <class T>
class SpscRing {
    static_assert(std::is_trivially_copyable_v<T>, "SpscRing holds trivially copyable payloads only");

public:
    explicit SpscRing(std::size_t capacity_pow2)
        : capacity_(capacity_pow2), mask_(capacity_pow2 - 1), buf_(std::make_unique<T[]>(capacity_pow2)) {
        if (capacity_pow2 < 2 || (capacity_pow2 & mask_) != 0)
            throw std::invalid_argument("SpscRing capacity must be a power of two >= 2");
    }

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    // Producer thread only.
    bool try_push(const T& v) noexcept {
        const std::size_t h = head_.load(std::memory_order_relaxed);
        if (h - cached_tail_ == capacity_) {
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (h - cached_tail_ == capacity_) return false;  // full
        }
        buf_[h & mask_] = v;
        head_.store(h + 1, std::memory_order_release);  // publish the slot
        return true;
    }

    // Consumer thread only.
    bool try_pop(T& out) noexcept {
        const std::size_t t = tail_.load(std::memory_order_relaxed);
        if (t == cached_head_) {
            cached_head_ = head_.load(std::memory_order_acquire);
            if (t == cached_head_) return false;  // empty
        }
        out = buf_[t & mask_];
        tail_.store(t + 1, std::memory_order_release);  // free the slot
        return true;
    }

    // Any thread; a snapshot that may be stale by the time it is used.
    std::size_t size_approx() const noexcept {
        const std::size_t h = head_.load(std::memory_order_acquire);
        const std::size_t t = tail_.load(std::memory_order_acquire);
        return h - t;
    }
    bool empty_approx() const noexcept { return size_approx() == 0; }
    std::size_t capacity() const noexcept { return capacity_; }

private:
    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<T[]> buf_;

    alignas(kCacheLine) std::atomic<std::size_t> head_{0};  // producer writes
    alignas(kCacheLine) std::size_t cached_tail_ = 0;       // producer-private
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};  // consumer writes
    alignas(kCacheLine) std::size_t cached_head_ = 0;       // consumer-private
};

}  // namespace ge
