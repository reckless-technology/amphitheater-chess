#pragma once
/// @file ThreadSafeQueue.h A queue one thread fills and another drains, which can be closed.
///
/// The one way data crosses threads in the host: an engine's reader thread pushes the lines it reads, a tournament's
/// worker threads push their progress events, and the owner - the GUI's frame callback, the command line's main loop
/// - drains them with TryPop (never blocking) or Pop (blocking, with a deadline). Closing wakes every waiter; what was
/// queued before the close can still be drained.

#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace amphitheater
{

/**
 * A closable, unbounded, multi-producer multi-consumer queue.
 * @tparam T The element type, movable.
 */
template <typename T> class ThreadSafeQueue
{
  public:
    /**
     * Add an element; dropped once the queue is closed.
     * @param value The element.
     * @return Whether it was queued.
     */
    bool Push(T value)
    {
        {
            std::lock_guard lock(mutex_);
            if (is_closed_)
            {
                return false;
            }
            items_.push_back(std::move(value));
        }
        ready_.notify_one();
        return true;
    }

    /**
     * Take the oldest element without waiting.
     * @return The element, or nothing when the queue is empty.
     */
    std::optional<T> TryPop()
    {
        std::lock_guard lock(mutex_);
        if (items_.empty())
        {
            return std::nullopt;
        }
        T value = std::move(items_.front());
        items_.pop_front();
        return value;
    }

    /**
     * Take the oldest element, waiting for one until a deadline.
     * @param deadline When to stop waiting.
     * @return The element, or nothing when the deadline passed, or the queue was closed and is empty.
     */
    std::optional<T> PopUntil(std::chrono::steady_clock::time_point deadline)
    {
        std::unique_lock lock(mutex_);
        ready_.wait_until(lock, deadline, [this] { return !items_.empty() || is_closed_; });
        if (items_.empty())
        {
            return std::nullopt;
        }
        T value = std::move(items_.front());
        items_.pop_front();
        return value;
    }

    /**
     * Take the oldest element, waiting for one for a while.
     * @param timeout How long to wait.
     * @return The element, or nothing when the time ran out, or the queue was closed and is empty.
     */
    std::optional<T> PopFor(std::chrono::milliseconds timeout)
    {
        return PopUntil(std::chrono::steady_clock::now() + timeout);
    }

    /** Refuse further elements and wake every waiter; what is queued stays to be drained. */
    void Close()
    {
        {
            std::lock_guard lock(mutex_);
            is_closed_ = true;
        }
        ready_.notify_all();
    }

    /** @return Whether the queue has been closed. */
    bool IsClosed() const
    {
        std::lock_guard lock(mutex_);
        return is_closed_;
    }

    /** @return Whether the queue is closed and empty: nothing more will ever come out of it. */
    bool IsDrained() const
    {
        std::lock_guard lock(mutex_);
        return is_closed_ && items_.empty();
    }

  private:
    mutable std::mutex      mutex_;             ///< Guards everything below.
    std::condition_variable ready_;             ///< Signalled on a push and on the close.
    std::deque<T>           items_;             ///< The queued elements.
    bool                    is_closed_ = false; ///< Whether the queue refuses further elements.
};

} // namespace amphitheater
