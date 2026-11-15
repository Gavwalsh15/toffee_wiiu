#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace diagnostics {
template <class Event, std::size_t Capacity> class EventMailbox {
    static_assert(Capacity > 0);
    static_assert(std::is_trivially_copyable_v<Event>);
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    enum : unsigned { Free, Writing, Ready, Reading };
    struct Slot {
        std::atomic<unsigned> state{Free};
        Event event{};
    };
    std::array<Slot, Capacity> slots_{};
    std::atomic<unsigned> dropped_{0};

  public:
    bool tryPush(const Event& event) noexcept
    {
        for (auto& slot : slots_)
        {
            unsigned expected = Free;
            if (!slot.state.compare_exchange_strong(expected, Writing, std::memory_order_acquire,
                                                    std::memory_order_relaxed))
                continue;
            slot.event = event;
            slot.state.store(Ready, std::memory_order_release);
            return true;
        }
        dropped_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    bool tryPop(Event& event) noexcept
    {
        for (auto& slot : slots_)
        {
            unsigned expected = Ready;
            if (!slot.state.compare_exchange_strong(expected, Reading, std::memory_order_acquire,
                                                    std::memory_order_relaxed))
                continue;
            event = slot.event;
            slot.state.store(Free, std::memory_order_release);
            return true;
        }
        return false;
    }
    unsigned dropped() const noexcept { return dropped_.load(std::memory_order_relaxed); }
};
} // namespace diagnostics
