#include "rooks/simulation.hpp"

#include <random>
#include <stdexcept>
#include <thread>
#include <utility>

namespace rooks
{

    Simulation::Simulation(std::vector<Rook> initial, std::uint32_t seed, Timing timing)
        : seed_(seed),
          timing_(timing),
          board_(std::move(initial))
    {
        if (timing_.min_pause < Milliseconds::zero() || timing_.max_pause < timing_.min_pause || timing_.blocked_timeout <= Milliseconds::zero())
        {
            throw std::invalid_argument("Invalid timing settings.");
        }
    }

    std::vector<Rook> Simulation::run(const EventHandler& on_event)
    {
        {
            std::lock_guard lock(mutex_);
            if (run_called_)
            {
                throw std::logic_error("A Simulation can only be run once.");
            }
            run_called_ = true;
        }

        const auto                count = board_.rooks().size();
        std::vector<std::jthread> workers;
        workers.reserve(count);
        try
        {
            for (std::size_t i = 0; i < count; ++i)
            {
                workers.emplace_back(
                    [this, i]
                    {
                        worker(i);
                    });
            }

            std::unique_lock lock(mutex_);
            coordinator_changed_.wait(lock,
                                      [this, count]
                                      {
                                          return ready_ == count || failure_;
                                      });
            if (!failure_)
            {
                started_at_ = Clock::now();
                report_stuck();
                started_  = true;
                stopping_ = board_.done();
            }
            wake_all();

            for (;;)
            {
                coordinator_changed_.wait(lock,
                                          [this]
                                          {
                                              return stopping_ || !events_.empty();
                                          });
                std::deque<Event> pending;
                pending.swap(events_);
                lock.unlock();
                for (const auto& event : pending)
                {
                    if (on_event)
                    {
                        on_event(event);
                    }
                }
                lock.lock();
                if (stopping_ && events_.empty())
                {
                    break;
                }
            }
        }
        catch (...)
        {
            // Also covers partial thread creation and a throwing output handler.
            // Release the start gate / timed waits before jthread destructors join.
            {
                std::lock_guard lock(mutex_);
                stopping_ = true;
            }
            wake_all();
            throw;
        }

        // Join normally, rather than asking individual jthreads to stop early.
        // The simulation uses one shared stop flag so every waiter sees the same stop.
        for (auto& thread : workers)
        {
            thread.join();
        }
        if (failure_)
        {
            std::rethrow_exception(failure_);
        }
        return board_.rooks();
    }

    void Simulation::worker(std::size_t rook) noexcept
    {
        try
        {
            move_rook(rook);
        }
        catch (...)
        {
            {
                std::lock_guard lock(mutex_);
                if (!failure_)
                {
                    failure_ = std::current_exception();
                }
                stopping_ = true;
            }
            wake_all();
        }
    }

    void Simulation::move_rook(std::size_t rook)
    {
        std::seed_seq                                    seed{seed_, static_cast<std::uint32_t>(rook)};
        std::mt19937                                     random(seed); // Each thread owns its generator; no RNG lock.
        std::uniform_int_distribution<Milliseconds::rep> pause_distribution(timing_.min_pause.count(), timing_.max_pause.count());
        std::optional<Square>                            previous_target;

        std::unique_lock lock(mutex_);
        // Only the last arrival wakes the coordinator, never the other workers.
        if (++ready_ == board_.rooks().size())
        {
            coordinator_changed_.notify_one();
        }
        rook_changed_[rook].wait(lock,
                                 [this]
                                 {
                                     return started_ || stopping_;
                                 });

        const auto& me = board_.rooks()[rook];
        while (!stopping_ && !me.finished())
        {
            const Square target = random_target(me.square, random, previous_target);
            previous_target.reset();

            if (!board_.path_clear(rook, target))
            {
                const auto wait_started = Clock::now();
                const auto deadline     = wait_started + timing_.blocked_timeout;
                post(EventKind::waiting, rook, me.square, target, {}, wait_started);

                // Register under the board lock, before wait_until releases it:
                // a move that clears this path cannot slip past our registration.
                waiting_targets_[rook] = target;
                // Keep the predicate: another rook can block the path again before
                // we reacquire the lock. The absolute deadline never restarts.
                const bool available = rook_changed_[rook].wait_until(lock,
                                                                      deadline,
                                                                      [&]
                                                                      {
                                                                          return stopping_ || board_.path_clear(rook, target);
                                                                      });
                waiting_targets_[rook].reset();
                if (stopping_)
                {
                    break;
                }
                if (!available)
                {
                    post(EventKind::timeout, rook, me.square, target);
                    previous_target = target; // "Another position" excludes this one.
                    continue;
                }
            }

            const Square from = me.square;
            // The path check and position update share one uninterrupted lock hold.
            if (!board_.try_move(rook, target))
            {
                throw std::logic_error("A checked move unexpectedly became invalid.");
            }
            const auto         moved_at = Clock::now();
            const Milliseconds pause    = me.finished() ? Milliseconds::zero() : Milliseconds(pause_distribution(random));
            post(EventKind::moved, rook, from, target, pause, moved_at);
            report_stuck();
            stopping_ = board_.done();
            if (stopping_)
            {
                wake_all();
            }
            else
            {
                wake_unblocked();
            }
            if (stopping_ || me.finished())
            {
                break;
            }

            // No registered target means moves cannot notify this worker during
            // cooldown. Shutdown still wakes it immediately; no uninterruptible sleep.
            rook_changed_[rook].wait_until(lock,
                                           moved_at + pause,
                                           [this]
                                           {
                                               return stopping_;
                                           });
        }
        // Deliberately no "if (me.stuck) return": stuck rooks keep retrying until
        // the entire game ends, as required. Their five-second waits do not spin.
    }
    void Simulation::wake_all()
    {
        // Used only to open the start gate or stop the game (including failures).
        coordinator_changed_.notify_one();
        for (auto& changed : rook_changed_)
        {
            changed.notify_one(); // Exactly one worker can wait on each CV.
        }
    }
    void Simulation::wake_unblocked()
    {
        // Caller holds mutex_. At most six targets: a scan is simpler than keeping
        // dependency lists, and avoids waking workers whose paths are still blocked.
        for (std::size_t i = 0; i < board_.rooks().size(); ++i)
        {
            const auto target = waiting_targets_[i];
            if (target && board_.path_clear(i, *target))
            {
                rook_changed_[i].notify_one();
            }
        }
    }

    void Simulation::report_stuck()
    {
        for (std::size_t i = 0; i < board_.rooks().size(); ++i)
        {
            const auto& rook = board_.rooks()[i];
            if (rook.stuck && !stuck_reported_[i])
            {
                stuck_reported_[i] = true;
                post(EventKind::stuck, i, rook.square, rook.square);
            }
        }
    }

    void Simulation::post(EventKind kind, std::size_t rook, Square from, Square target, Milliseconds pause, Clock::time_point when)
    {
        // Caller holds mutex_. Queue small records here; do all formatting/I/O in run().
        events_.push_back({kind, ++sequence_, rook, from, target, board_.rooks()[rook].moves, pause, when - started_at_});
        coordinator_changed_.notify_one();
    }

} // namespace rooks
