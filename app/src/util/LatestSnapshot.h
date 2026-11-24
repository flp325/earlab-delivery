#pragma once

#include <array>
#include <atomic>
#include <type_traits>

namespace earlab
{
/** Single producer/single consumer latest-value mailbox.
    Producer, consumer and exchange slot each own a distinct page. Publication
    never overwrites the consumer's page, regardless of producer speed. Neither
    side allocates, locks or retries. consume()'s reference remains valid until
    the next consume() on the same consumer; other readers must use their own copy.
    prepare/reset must not run concurrently with the consumer. */
template <typename T>
class LatestSnapshot
{
public:
    static_assert (std::is_trivially_copyable_v<T>);
    static_assert (std::atomic<unsigned>::is_always_lock_free);

    void publish (const T& value) noexcept
    {
        pages[writer] = value;
        writer = exchange.exchange (writer | dirty, std::memory_order_acq_rel) & mask;
    }

    [[nodiscard]] const T& consume() noexcept
    {
        if ((exchange.load (std::memory_order_acquire) & dirty) != 0)
            reader = exchange.exchange (reader, std::memory_order_acq_rel) & mask;
        return pages[reader];
    }

private:
    static constexpr unsigned dirty = 4, mask = 3;
    std::array<T, 3> pages {};
    unsigned reader = 0;
    unsigned writer = 2;
    std::atomic<unsigned> exchange { 1 };
};
} // namespace earlab
