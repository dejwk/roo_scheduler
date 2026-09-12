#pragma once

/// Umbrella header for the roo_scheduler module.
///
/// Provides task scheduling primitives and adapters.

#include <functional>
#include <memory>
#include <vector>

#include "roo_collections.h"
#include "roo_collections/flat_small_hash_set.h"
#include "roo_threads.h"
#include "roo_threads/condition_variable.h"
#include "roo_threads/mutex.h"
#include "roo_time.h"

/// A typical Arduino use case may look like the following:
///
/// @code
/// void foo();
///
/// using namespace roo_time;
/// using namespace roo_scheduler;
///
/// Scheduler scheduler;
/// RepetitiveTask foo_task(scheduler, foo, Seconds(5));
///
/// void setup() {
///   foo_task.start();
/// }
///
/// void loop() {
///   scheduler.executeEligibleTasks();
///   // ... other work
/// }
/// @endcode

#ifndef ROO_SCHEDULER_IGNORE_PRIORITY
#define ROO_SCHEDULER_IGNORE_PRIORITY 0
#endif

namespace roo_scheduler {

/// Represents a unique task execution identifier.
using ExecutionID = int32_t;

/// Deprecated alias; prefer `ExecutionID`.
using EventID = ExecutionID;

/// Priority controls dispatch order among eligible tasks.
///
/// Higher-priority tasks execute first. Tasks with equal priority execute in
/// FIFO order.
///
/// Priority does not affect scheduling time; it only affects dispatch order
/// once tasks are already eligible.
///
/// If tasks are sufficiently spread out in time and complete quickly, they are
/// effectively dispatched by due time regardless of priority.
///
/// Priority is captured when execution is scheduled and remains fixed for that
/// scheduled execution. Re-scheduling may assign a different priority.
///
/// `delay()` and `delayUntil()` guarantee execution of tasks whose priority is
/// at least the requested minimum. Lower-priority overdue tasks may remain
/// pending.
enum class Priority {
  kMinimum = 0,
  kBackground = 1,
  kReduced = 2,
  kNormal = 3,
  kElevated = 4,
  kSensitive = 5,
  kCritical = 6,
  kMaximum = 7,
};

/// @deprecated Use `Priority::kMinimum`.
constexpr Priority PRIORITY_MINIMUM = Priority::kMinimum;
/// @deprecated Use `Priority::kBackground`.
constexpr Priority PRIORITY_BACKGROUND = Priority::kBackground;
/// @deprecated Use `Priority::kReduced`.
constexpr Priority PRIORITY_REDUCED = Priority::kReduced;
/// @deprecated Use `Priority::kNormal`.
constexpr Priority PRIORITY_NORMAL = Priority::kNormal;
/// @deprecated Use `Priority::kElevated`.
constexpr Priority PRIORITY_ELEVATED = Priority::kElevated;
/// @deprecated Use `Priority::kSensitive`.
constexpr Priority PRIORITY_SENSITIVE = Priority::kSensitive;
/// @deprecated Use `Priority::kCritical`.
constexpr Priority PRIORITY_CRITICAL = Priority::kCritical;
/// @deprecated Use `Priority::kMaximum`.
constexpr Priority PRIORITY_MAXIMUM = Priority::kMaximum;

/// Abstract interface for executable tasks in the scheduler queue.
class Executable {
 public:
  virtual ~Executable() = default;
  virtual void execute(ExecutionID id) = 0;

 private:
  friend class Scheduler;
  // Used only after the scheduler removes an owned task from its queues.
  Executable *retired_next_ = nullptr;
};

/// Schedules and dispatches delayed task executions.
///
/// Scheduler does not execute eligible work automatically; caller must invoke
/// one of `executeEligibleTasks*()` methods.
///
/// Scheduling, cancellation and queries may be called from any thread. Dispatch
/// must be serialized on one thread; nested dispatch on that thread is allowed.
/// Callbacks and canceled task destructors run without the scheduler mutex.
/// Stop dispatch and all producers before destroying the scheduler. The
/// scheduler must outlive every adapter that refers to it.
class Scheduler {
 public:
  /// Creates an empty scheduler.
  Scheduler();
  ~Scheduler();

  /// Schedules execution no earlier than `when`.
  ///
  /// Caller retains ownership and must keep `task` alive until execution or
  /// cancellation followed by quiescence (see `cancelAndWait()`).
  ExecutionID scheduleOn(roo_time::Uptime when, Executable &task,
                         Priority priority = Priority::kNormal);

  /// Schedules execution no earlier than `when`.
  ///
  /// Scheduler takes ownership of `task` and destroys it after execution or
  /// cancellation followed by quiescence (see `cancelAndWait()`).
  ExecutionID scheduleOn(roo_time::Uptime when,
                         std::unique_ptr<Executable> task,
                         Priority priority = Priority::kNormal);

  /// Schedules callable execution no earlier than `when`.
  ExecutionID scheduleOn(roo_time::Uptime when, std::function<void()> task,
                         Priority priority = Priority::kNormal);

#ifndef ROO_SCHEDULER_NO_DEPRECATED
  /// @deprecated Use `scheduleOn(when, task, priority)`.
  ExecutionID scheduleOn(Executable *task, roo_time::Uptime when,
                         Priority priority = Priority::kNormal) {
    return scheduleOn(when, *task, priority);
  }
#endif

  /// Schedules execution after `delay` elapses.
  ///
  /// Caller retains ownership and must keep `task` alive until execution or
  /// cancellation followed by quiescence (see `cancelAndWait()`).
  ExecutionID scheduleAfter(roo_time::Duration delay, Executable &task,
                            Priority priority = Priority::kNormal);

  /// Schedules execution after `delay` elapses.
  ///
  /// Scheduler takes ownership of `task` and destroys it after execution or
  /// cancellation followed by quiescence (see `cancelAndWait()`).
  ExecutionID scheduleAfter(roo_time::Duration delay,
                            std::unique_ptr<Executable> task,
                            Priority priority = Priority::kNormal);

  /// Schedules callable execution after `delay` elapses.
  ExecutionID scheduleAfter(roo_time::Duration delay,
                            std::function<void()> task,
                            Priority priority = Priority::kNormal);

#ifndef ROO_SCHEDULER_NO_DEPRECATED
  /// @deprecated Use `scheduleAfter(delay, task, priority)`.
  ExecutionID scheduleAfter(Executable *task, roo_time::Duration delay,
                            Priority priority = Priority::kNormal) {
    return scheduleAfter(delay, *task, priority);
  }
#endif

  /// Schedules execution as soon as possible.
  ///
  /// Caller retains ownership and must keep `task` alive until execution or
  /// cancellation followed by quiescence (see `cancelAndWait()`).
  ExecutionID scheduleNow(Executable &task,
                          Priority priority = Priority::kNormal) {
    return scheduleOn(roo_time::Uptime::Now(), task, priority);
  }

  /// Schedules execution as soon as possible.
  ///
  /// Scheduler takes ownership of `task` and destroys it after execution or
  /// cancellation followed by quiescence (see `cancelAndWait()`).
  ExecutionID scheduleNow(std::unique_ptr<Executable> task,
                          Priority priority = Priority::kNormal) {
    return scheduleOn(roo_time::Uptime::Now(), std::move(task), priority);
  }

  /// Schedules callable execution as soon as possible.
  ExecutionID scheduleNow(std::function<void()> task,
                          Priority priority = Priority::kNormal) {
    return scheduleOn(roo_time::Uptime::Now(), std::move(task), priority);
  }

  /// Admits queued tasks due no later than now, then dispatches eligible work.
  /// See executeEligibleTasksUpTo() for admission semantics.
  ///
  /// Tasks below `min_priority` are ignored (not executed).
  ///
  /// @return true if no eligible executions remain in queue; false otherwise.
  bool executeEligibleTasksUpToNow(Priority min_priority = Priority::kMinimum,
                                   int max_count = -1) {
    return executeEligibleTasksUpTo(roo_time::Uptime::Now(), min_priority,
                                    max_count);
  }

  /// Admits queued tasks due no later than min(deadline, now), then executes
  /// up to `max_count` eligible tasks in priority order.
  ///
  /// The deadline is an admission cutoff, not a limit on already-ready work.
  /// Previously admitted tasks remain eligible even if their due times exceed
  /// this cutoff. This includes work admitted by nested dispatch. Callers may
  /// use decreasing cutoffs; this does not withdraw previously admitted work.
  /// With ROO_SCHEDULER_IGNORE_PRIORITY there is no separate ready queue, so
  /// each execution is selected directly using the current cutoff.
  ///
  /// Tasks below `min_priority` are ignored (not executed).
  ///
  /// @return true if no eligible executions remain in queue; false otherwise.
  bool executeEligibleTasksUpTo(roo_time::Uptime deadline,
                                Priority min_priority = Priority::kMinimum,
                                int max_count = -1);

  /// Executes up to `max_count` eligible tasks with at least `min_priority`.
  ///
  /// @return true if no eligible executions remain in queue; false otherwise.
  bool executeEligibleTasks(Priority min_priority, int max_count = -1);

  /// Executes up to `max_count` eligible tasks.
  ///
  /// @return true if no eligible executions remain in queue; false otherwise.
  bool executeEligibleTasks(int max_count = -1) {
    return executeEligibleTasks(Priority::kMinimum, max_count);
  }

  /// Returns due time of the nearest upcoming execution.
  roo_time::Uptime getNearestExecutionTime() const;

  /// Returns delay to the nearest upcoming execution.
  roo_time::Duration getNearestExecutionDelay() const;

  /// Marks execution identified by `id` as canceled.
  ///
  /// Canceled entries may remain in queue until pruned, but will not run unless
  /// dispatch already claimed them. This operation does not wait for callbacks
  /// and does not by itself permit destroying a borrowed task on another
  /// thread.
  void cancel(ExecutionID);

  /// Cancels all pending executions of a borrowed task and waits for claimed
  /// executions to return. Prevent concurrent rescheduling before calling this.
  /// Do not hold a lock that the callback needs. Returns false instead of
  /// waiting if this thread is currently dispatching the task (including an
  /// outer callback during nested dispatch). In that case it is not quiescent.
  bool cancelAndWait(Executable &task);

  /// Removes canceled executions from the queue.
  ///
  /// This is linear-time and should be used sparingly.
  void pruneCanceled();

  /// Returns true iff no pending (non-canceled) executions exist.
  bool empty() const;

  /// Delays for at least `delay` while executing scheduled work.
  ///
  /// Tasks due by the requested return time (now + `delay`) with priority >=
  /// `min_priority` are guaranteed to execute before return. Lower-priority
  /// overdue tasks may remain pending.
  ///
  /// Note: because scheduled callbacks execute on the caller's stack, this mode
  /// can increase stack usage compared with explicit event-loop dispatch.
  void delay(roo_time::Duration delay,
             Priority min_priority = Priority::kNormal);

  /// Delays until `deadline` while executing scheduled work.
  ///
  /// Tasks due by `deadline` with priority >= `min_priority` are guaranteed to
  /// execute before return. Lower-priority overdue tasks may remain pending.
  void delayUntil(roo_time::Uptime deadline,
                  Priority min_priority = Priority::kNormal);

  /// Runs scheduler event loop forever.
  void run();

 private:
  struct InFlight {
    explicit InFlight(Scheduler &scheduler) : scheduler(scheduler) {}
    ~InFlight();
    Scheduler &scheduler;
    Executable *task = nullptr;
#ifndef ROO_THREADS_SINGLETHREADED
    roo::thread::id thread;
#endif
    InFlight *previous = nullptr;
  };
  InFlight *in_flight_ = nullptr;

  class Entry {
   public:
#if !ROO_SCHEDULER_IGNORE_PRIORITY
    Entry()
        : id_(0),
          task_(nullptr),
          when_(roo_time::Uptime::Max()),
          priority_(Priority::kNormal),
          owns_task_(false) {}

    Entry(ExecutionID id, Executable *task, bool owns_task,
          roo_time::Uptime when, Priority priority)
        : id_(id),
          task_(task),
          when_(when),
          priority_(priority),
          owns_task_(owns_task) {}

    Entry(Entry &&other)
        : id_(other.id_),
          task_(other.task_),
          when_(other.when_),
          priority_(other.priority_),
          owns_task_(other.owns_task_) {
      other.task_ = nullptr;
      other.owns_task_ = false;
    }

    Entry &operator=(Entry &&other) {
      if (this == &other) return *this;
      if (owns_task_) {
        delete task_;
      }
      id_ = other.id_;
      task_ = other.task_;
      when_ = other.when_;
      priority_ = other.priority_;
      owns_task_ = other.owns_task_;
      other.task_ = nullptr;
      other.owns_task_ = false;
      return *this;
    }

#else
    Entry()
        : id_(0),
          task_(nullptr),
          when_(roo_time::Uptime::Max()),
          owns_task_(false) {}

    Entry(ExecutionID id, Executable *task, bool owns_task,
          roo_time::Uptime when, Priority priority)
        : id_(id), task_(task), when_(when), owns_task_(owns_task) {}

    Entry(Entry &&other)
        : id_(other.id_),
          task_(other.task_),
          when_(other.when_),
          owns_task_(other.owns_task_) {
      other.owns_task_ = false;
    }

    Entry &operator=(Entry &&other) {
      if (this == &other) return *this;
      if (owns_task_) {
        delete task_;
      }
      id_ = other.id_;
      task_ = other.task_;
      when_ = other.when_;
      owns_task_ = other.owns_task_;
      other.task_ = nullptr;
      other.owns_task_ = false;
      return *this;
    }
#endif

    Entry(const Entry &other) = delete;
    Entry &operator=(const Entry &other) = delete;

    ~Entry() {
      if (owns_task_) {
        delete task_;
      }
    }

    roo_time::Uptime when() const { return when_; }
    Executable *task() const { return task_; }
    ExecutionID id() const { return id_; }

    Priority priority() const {
#if !ROO_SCHEDULER_IGNORE_PRIORITY
      return priority_;
#else
      return Priority::kNormal;
#endif
    }

    bool owns_task() const { return owns_task_; }

    Executable *releaseOwnedTask() {
      if (!owns_task_) return nullptr;
      Executable *task = task_;
      task_ = nullptr;
      owns_task_ = false;
      return task;
    }

   private:
    friend struct TimeComparator;

    ExecutionID id_;
    Executable *task_;
    roo_time::Uptime when_;

#if !ROO_SCHEDULER_IGNORE_PRIORITY
    Priority priority_;
#endif
    bool owns_task_;
  };

  // Orders scheduled tasks in the queue by their nearest execution time.
  struct TimeComparator {
    bool operator()(const Entry &a, const Entry &b) {
      return a.when() > b.when() ||
             (a.when() == b.when() && a.id() - b.id() > 0);
    }
  };

  // Used for tasks that are already due, ordering them by priority.
  struct PriorityComparator {
    bool operator()(const Entry &a, const Entry &b) {
      return a.priority() < b.priority() ||
             (a.priority() == b.priority() &&
              (a.when() > b.when() ||
               (a.when() == b.when() && a.id() - b.id() > 0)));
    }
  };

  // Intrusive retirement avoids allocating during cancellation or dispatch.
  // Declare before lock guards so user destructors run after unlocking.
  class RetiredTasks {
   public:
    ~RetiredTasks();
    void add(Entry &&entry);

   private:
    Executable *head_ = nullptr;
  };

  friend class SingletonTask;
  // Retired owned tasks must outlive both scheduler and adapter lock guards.
  ExecutionID replace(ExecutionID previous, roo_time::Uptime when,
                      Executable &task, Priority priority,
                      RetiredTasks &retired);

  roo_time::Uptime getNearestExecutionTimeWithLockHeld() const;

  roo_time::Duration getNearestExecutionDelayWithLockHeld() const;

  ExecutionID push(roo_time::Uptime when, Executable *task, bool owns_task,
                   Priority priority);
  void pop(RetiredTasks &retired);

  // Returns true if has been executed; false if there was no eligible
  // execution.
  bool runOneEligibleExecution(roo_time::Uptime deadline,
                               Priority min_priority);

  // Entries in the queue_ are stored as a heap. (We're not directly using
  // std::priority_queue in order to support cancellation; see prune()). Since
  // the entries are stored in a vector, when the number of scheduled executions
  // is bounded, there will be no dynamic allocation once the vector reaches
  // sufficient capacity. At the same time, even if executions are dynamically
  // created, the queue can accommodate them, as long as there is sufficient
  // amount of memory.
  //
  // We maintain the invariant that the top (front) of the queue is a
  // non-canceled execution.
  std::vector<Entry> queue_;

#if !ROO_SCHEDULER_IGNORE_PRIORITY
  // Tasks that are due. Heap, ordered by priority.
  std::vector<Entry> ready_;
#endif

  ExecutionID next_execution_id_;

  // Deferred cancellation set, containing IDs of scheduled executions that have
  // been canceled. These records cover pending entries, not claimed callbacks.
  //
  // Calling pruneCanceled() removes all canceled executions from the queue, and
  // clears this set.
  roo_collections::FlatSmallHashSet<ExecutionID> canceled_;

  mutable roo::mutex mutex_;
  // Queue changes and execution completion share the same mutex/predicate loop.
  roo::condition_variable changed_;
};

/// Convenience adapter for one-time execution of an arbitrary callable.
class Task : public Executable {
 public:
  Task(std::function<void()> task) : task_(task) {}
  void execute(ExecutionID id) override { task_(); }

 private:
  std::function<void()> task_;
};

/// Convenience adapter for repetitive callable execution.
///
/// Subsequent executions are scheduled with constant delay between runs.
/// Control methods are thread-safe; stop() does not wait for a running
/// callback. Callbacks must not destroy this adapter. Use shutdown() before
/// destroying state referenced by a callback, and keep the scheduler alive.
class RepetitiveTask : public Executable {
 public:
  RepetitiveTask(Scheduler &scheduler, roo_time::Duration delay,
                 std::function<void()> task,
                 Priority priority = Priority::kNormal);

#ifndef ROO_SCHEDULER_NO_DEPRECATED
  /// @deprecated Use `RepetitiveTask(scheduler, delay, task, priority)`.
  RepetitiveTask(Scheduler &scheduler, std::function<void()> task,
                 roo_time::Duration delay,
                 Priority priority = Priority::kNormal)
      : RepetitiveTask(scheduler, delay, std::move(task), priority) {}
#endif

  bool is_active() const;

  Priority priority() const;

  /// Starts task using configured periodic delay.
  ///
  /// @return false if already active or permanently shut down.
  bool start() { return start(delay_); }

  /// Starts task immediately.
  ///
  /// @return false if already active or permanently shut down.
  bool startInstantly() { return start(roo_time::Millis(0)); }

  /// Starts task with custom initial delay.
  ///
  /// @return false if already active or permanently shut down.
  bool start(roo_time::Duration initial_delay);

  bool stop();

  void execute(ExecutionID id) override;

  void setPriority(Priority priority);

  /// Permanently disables scheduling, cancels pending work and waits for a
  /// claimed callback. Returns false on the dispatching callback's own thread;
  /// no waiting occurs there. Never hold a callback-needed lock while waiting.
  /// Concurrent callers must keep the object alive until their calls return.
  bool shutdown();

  /// True once shutdown begins; does not imply a claimed callback has finished.
  bool is_shutdown() const;

  ~RepetitiveTask();

 private:
  Scheduler &scheduler_;
  std::function<void()> task_;
  mutable roo::mutex mutex_;
  // Nonnegative: active execution; -1: inactive; -2: permanently shut down.
  ExecutionID id_;
  Priority priority_;
  roo_time::Duration delay_;
};

/// Convenience adapter for periodic callable execution.
///
/// Uses fixed target schedule to keep average execution frequency stable.
/// Shares RepetitiveTask's threading and lifetime contract.
class PeriodicTask : public Executable {
 public:
  PeriodicTask(Scheduler &scheduler, roo_time::Duration period,
               std::function<void()> task,
               Priority priority = Priority::kNormal);

#ifndef ROO_SCHEDULER_NO_DEPRECATED
  /// @deprecated Use `PeriodicTask(scheduler, period, task, priority)`.
  PeriodicTask(Scheduler &scheduler, std::function<void()> task,
               roo_time::Duration period, Priority priority = Priority::kNormal)
      : PeriodicTask(scheduler, period, std::move(task), priority) {}
#endif

  bool is_active() const;

  Priority priority() const;

  bool start(roo_time::Uptime when = roo_time::Uptime::Now());

  bool stop();

  void execute(ExecutionID id) override;

  void setPriority(Priority priority);

  /// Permanently disables scheduling, cancels pending work and waits for a
  /// claimed callback. Returns false on the dispatching callback's own thread;
  /// no waiting occurs there. Never hold a callback-needed lock while waiting.
  /// Concurrent callers must keep the object alive until their calls return.
  bool shutdown();

  /// True once shutdown begins; does not imply a claimed callback has finished.
  bool is_shutdown() const;

  ~PeriodicTask();

 private:
  Scheduler &scheduler_;
  std::function<void()> task_;
  mutable roo::mutex mutex_;
  // Nonnegative: active execution; -1: inactive; -2: permanently shut down.
  ExecutionID id_;
  Priority priority_;
  roo_time::Duration period_;
  roo_time::Uptime next_;
};

/// Convenience adapter for cancelable and replaceable single pending work.
/// Control methods are thread-safe. cancel() does not wait for a running
/// callback. is_scheduled() describes pending work, not a running callback.
/// A callback may reschedule or destroy this adapter; captured state must stay
/// alive until the callback returns. Use shutdown() before owner teardown.
class SingletonTask : public Executable {
 public:
  SingletonTask(Scheduler &scheduler, std::function<void()> task);

  bool is_scheduled() const;

  /// Schedules or reschedules task at absolute time `when`.
  ///
  /// Any previously pending execution is canceled. Ignored after shutdown().
  void scheduleOn(roo_time::Uptime when, Priority priority = Priority::kNormal);

  /// Schedules or reschedules task after `delay`.
  ///
  /// Any previously pending execution is canceled. Ignored after shutdown().
  void scheduleAfter(roo_time::Duration delay,
                     Priority priority = Priority::kNormal);

  /// Schedules or reschedules task for immediate execution.
  ///
  /// Any previously pending execution is canceled. Ignored after shutdown().
  void scheduleNow(Priority priority = Priority::kNormal);

  void cancel();

  void execute(ExecutionID id) override;

  /// Permanently disables scheduling, cancels pending work and waits for a
  /// claimed callback. Returns false on the dispatching callback's own thread;
  /// no waiting occurs there. Never hold a callback-needed lock while waiting.
  /// Concurrent callers must keep the object alive until their calls return.
  bool shutdown();

  /// True once shutdown begins; does not imply a claimed callback has finished.
  bool is_shutdown() const;

  ~SingletonTask();

 private:
  Scheduler &scheduler_;
  std::shared_ptr<std::function<void()>> task_;
  mutable roo::mutex mutex_;
  // Nonnegative: active execution; -1: inactive; -2: permanently shut down.
  ExecutionID id_;
};

/// Thread-safe scheduling of an iterator. Only the dispatch thread calls
/// next(). The iterator must outlive shutdown(); it must not destroy this task
/// in next(). The completion callback may destroy this adapter.
class IteratingTask : public Executable {
 public:
  class Iterator {
   public:
    virtual ~Iterator() = default;
    virtual int64_t next() = 0;
  };

  IteratingTask(Scheduler &scheduler, Iterator &iterator,
                std::function<void()> done_cb = std::function<void()>());

  bool start(roo_time::Uptime when = roo_time::Uptime::Now());

  void execute(ExecutionID id) override;

  bool is_active() const;

  /// Permanently disables scheduling, cancels pending work and waits for a
  /// claimed callback. Returns false on the dispatching callback's own thread;
  /// no waiting occurs there. Never hold a callback-needed lock while waiting.
  /// Concurrent callers must keep the object alive until their calls return.
  bool shutdown();

  /// True once shutdown begins; does not imply a claimed callback has finished.
  bool is_shutdown() const;

  ~IteratingTask();

 private:
  Scheduler &scheduler_;
  Iterator &itr_;
  mutable roo::mutex mutex_;
  // Nonnegative: active execution; -1: inactive; -2: permanently shut down.
  ExecutionID id_;

  /// Called when iterator finishes; callback may delete the iterating task.
  std::shared_ptr<std::function<void()>> done_cb_;
};

}  // namespace roo_scheduler
