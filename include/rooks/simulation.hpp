#pragma once

#include "rooks/board.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>

namespace rooks
{

    using Clock        = std::chrono::steady_clock;
    using Milliseconds = std::chrono::milliseconds;

    // The executable always uses these defaults. Tests can shorten the delays.
    struct Timing
    {
        Milliseconds min_pause{200};
        Milliseconds max_pause{300};
        Milliseconds blocked_timeout{5000};
    };

    enum class EventKind : std::uint8_t
    {
        moved,
        waiting,
        timeout,
        stuck
    };

    struct Event
    {
        EventKind       kind;
        std::size_t     sequence;
        std::size_t     rook;
        Square          from;
        Square          target;
        int             moves;
        Milliseconds    pause;
        Clock::duration elapsed;
    };

    class Simulation
    {
    public:
        using EventHandler = std::function<void(const Event&)>;

        Simulation(std::vector<Rook> initial, std::uint32_t seed, Timing timing = {});

        // One-shot, blocking call. The handler runs on the calling thread, never
        // under the board lock. The returned snapshot is taken after joining workers.
        std::vector<Rook> run(const EventHandler& on_event = {});

    private:
        void worker(std::size_t rook) noexcept;
        void move_rook(std::size_t rook);
        void report_stuck();
        void post(EventKind kind, std::size_t rook, Square from, Square target, Milliseconds pause = {}, Clock::time_point when = Clock::now());

        const std::uint32_t seed_;
        const Timing        timing_;

        // All mutable state below is protected by mutex_ while workers are running.
        std::mutex                  mutex_;
        std::condition_variable     changed_;
        Board                       board_;
        std::deque<Event>           events_;
        std::array<bool, max_rooks> stuck_reported_{};
        std::exception_ptr          failure_;
        Clock::time_point           started_at_;
        std::size_t                 ready_      = 0;
        std::size_t                 sequence_   = 0;
        bool                        run_called_ = false;
        bool                        started_    = false;
        bool                        stopping_   = false;
    };

} // namespace rooks
