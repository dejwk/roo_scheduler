#pragma once

/// Umbrella header for the roo_scheduler module.
///
/// Provides task scheduling primitives and adapters.

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

#include "roo_collections.h"
#include "roo_collections/flat_small_hash_set.h"
#include "roo_threads.h"
#include "roo_threads/condition_variable.h"
#include "roo_threads/isr_critical_section.h"
#include "roo_threads/isr_event.h"
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
/// SchedulingService scheduler;
/// RepetitiveTask foo_task(scheduler, Seconds(5), foo);
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
  Executable* retired_next_ = nullptr;
};

/// Provides scheduling and cancellation without access to dispatch operations.
///
/// Borrow this interface from a SchedulingService. The service must outlive
/// every client reference and task adapter. With a thread-safe mutex,
/// ordinary scheduling and cancellation can run on any task. On FreeRTOS
/// with ROO_THREADS_SINGLETHREADED, one task must make all ordinary calls
/// for a service. See SchedulingService for the bare-metal calling rules.
///
/// Ordinary scheduling and cancellation run in application or task context.
/// Cancellation may destroy owned tasks outside scheduler locks. Adapters
/// retain their documented shutdown and lifetime requirements.
class Scheduler {
 public:
  /// Schedules execution no earlier than `when`.
  ///
  /// Caller retains ownership and must keep `task` alive until execution or
  /// cancellation followed by quiescence (see
  /// SchedulingService::cancelAndWait()).
  ExecutionID scheduleOn(roo_time::Uptime when, Executable& task,
                         Priority priority = Priority::kNormal);

  /// Schedules execution no earlier than `when`.
  ///
  /// The service takes ownership of `task` and destroys it after execution or
  /// cancellation followed by quiescence (see
  /// SchedulingService::cancelAndWait()).
  ExecutionID scheduleOn(roo_time::Uptime when,
                         std::unique_ptr<Executable> task,
                         Priority priority = Priority::kNormal);

  /// Schedules callable execution no earlier than `when`.
  ExecutionID scheduleOn(roo_time::Uptime when, std::function<void()> task,
                         Priority priority = Priority::kNormal);

#ifndef ROO_SCHEDULER_NO_DEPRECATED
  /// @deprecated Use `scheduleOn(when, task, priority)`.
  ExecutionID scheduleOn(Executable* task, roo_time::Uptime when,
                         Priority priority = Priority::kNormal) {
    return scheduleOn(when, *task, priority);
  }
#endif

  /// Schedules execution after `delay` elapses.
  ///
  /// Caller retains ownership and must keep `task` alive until execution or
  /// cancellation followed by quiescence (see
  /// SchedulingService::cancelAndWait()).
  ExecutionID scheduleAfter(roo_time::Duration delay, Executable& task,
                            Priority priority = Priority::kNormal);

  /// Schedules execution after `delay` elapses.
  ///
  /// The service takes ownership of `task` and destroys it after execution or
  /// cancellation followed by quiescence (see
  /// SchedulingService::cancelAndWait()).
  ExecutionID scheduleAfter(roo_time::Duration delay,
                            std::unique_ptr<Executable> task,
                            Priority priority = Priority::kNormal);

  /// Schedules callable execution after `delay` elapses.
  ExecutionID scheduleAfter(roo_time::Duration delay,
                            std::function<void()> task,
                            Priority priority = Priority::kNormal);

#ifndef ROO_SCHEDULER_NO_DEPRECATED
  /// @deprecated Use `scheduleAfter(delay, task, priority)`.
  ExecutionID scheduleAfter(Executable* task, roo_time::Duration delay,
                            Priority priority = Priority::kNormal) {
    return scheduleAfter(delay, *task, priority);
  }
#endif

  /// Schedules execution as soon as possible.
  ///
  /// Caller retains ownership and must keep `task` alive until execution or
  /// cancellation followed by quiescence (see
  /// SchedulingService::cancelAndWait()).
  ExecutionID scheduleNow(Executable& task,
                          Priority priority = Priority::kNormal) {
    return scheduleOn(roo_time::Uptime::Now(), task, priority);
  }

  /// Schedules execution as soon as possible.
  ///
  /// The service takes ownership of `task` and destroys it after execution or
  /// cancellation followed by quiescence (see
  /// SchedulingService::cancelAndWait()).
  ExecutionID scheduleNow(std::unique_ptr<Executable> task,
                          Priority priority = Priority::kNormal) {
    return scheduleOn(roo_time::Uptime::Now(), std::move(task), priority);
  }

  /// Schedules callable execution as soon as possible.
  ExecutionID scheduleNow(std::function<void()> task,
                          Priority priority = Priority::kNormal) {
    return scheduleOn(roo_time::Uptime::Now(), std::move(task), priority);
  }

#if ROO_THREADS_HAS_ISR_SUPPORT
  /// Requests one borrowed execution from an ISR. Returns false when the
  /// eight pending ISR slots are full. The callback receives its execution ID.
  /// On FreeRTOS, the interrupt priority must permit kernel API calls.
  /// On ESP32 with cache disabled, all called code and accessed data must
  /// reside in internal memory. See SchedulingService::cancelAndWait() for
  /// borrowed-task teardown.
  bool tryScheduleNowFromIsr(Executable& task,
                             Priority priority = Priority::kNormal) noexcept;
#endif

  /// Marks execution identified by `id` as canceled.
  ///
  /// Canceled entries may remain in queue until pruned, but will not run unless
  /// dispatch already claimed them. This operation does not wait for callbacks
  /// and does not by itself permit destroying a borrowed task on another
  /// thread.
  void cancel(ExecutionID);

 protected:
  Scheduler();
  ~Scheduler();

 private:
  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;
  Scheduler(Scheduler&&) = delete;
  Scheduler& operator=(Scheduler&&) = delete;

  friend class SchedulingService;
  friend class RepetitiveTask;
  friend class PeriodicTask;
  friend class IteratingTask;

  bool cancelAndWait(Executable& task);

  struct InFlight {
    explicit InFlight(Scheduler& scheduler) : scheduler(scheduler) {}
    ~InFlight();
    Scheduler& scheduler;
    Executable* task = nullptr;
#ifndef ROO_THREADS_SINGLETHREADED
    roo::thread::id thread;
#endif
    InFlight* previous = nullptr;
  };
  InFlight* in_flight_ = nullptr;

  class Entry {
   public:
#if !ROO_SCHEDULER_IGNORE_PRIORITY
    Entry()
        : id_(0),
          task_(nullptr),
          when_(roo_time::Uptime::Max()),
          priority_(Priority::kNormal),
          owns_task_(false) {}

    Entry(ExecutionID id, Executable* task, bool owns_task,
          roo_time::Uptime when, Priority priority)
        : id_(id),
          task_(task),
          when_(when),
          priority_(priority),
          owns_task_(owns_task) {}

    Entry(Entry&& other)
        : id_(other.id_),
          task_(other.task_),
          when_(other.when_),
          priority_(other.priority_),
          owns_task_(other.owns_task_) {
      other.task_ = nullptr;
      other.owns_task_ = false;
    }

    Entry& operator=(Entry&& other) {
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

    Entry(ExecutionID id, Executable* task, bool owns_task,
          roo_time::Uptime when, Priority priority)
        : id_(id), task_(task), when_(when), owns_task_(owns_task) {}

    Entry(Entry&& other)
        : id_(other.id_),
          task_(other.task_),
          when_(other.when_),
          owns_task_(other.owns_task_) {
      other.owns_task_ = false;
    }

    Entry& operator=(Entry&& other) {
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

    Entry(const Entry& other) = delete;
    Entry& operator=(const Entry& other) = delete;

    ~Entry() {
      if (owns_task_) {
        delete task_;
      }
    }

    roo_time::Uptime when() const { return when_; }
    Executable* task() const { return task_; }
    ExecutionID id() const { return id_; }

    Priority priority() const {
#if !ROO_SCHEDULER_IGNORE_PRIORITY
      return priority_;
#else
      return Priority::kNormal;
#endif
    }

    bool owns_task() const { return owns_task_; }

    Executable* releaseOwnedTask() {
      if (!owns_task_) return nullptr;
      Executable* task = task_;
      task_ = nullptr;
      owns_task_ = false;
      return task;
    }

   private:
    friend struct TimeComparator;
    friend class Scheduler;

    ExecutionID id_;
    Executable* task_;
    roo_time::Uptime when_;

#if !ROO_SCHEDULER_IGNORE_PRIORITY
    Priority priority_;
#endif
    bool owns_task_;
  };

  // Orders scheduled tasks in the queue by their nearest execution time.
  struct TimeComparator {
    bool operator()(const Entry& a, const Entry& b) const {
      return a.when() > b.when() ||
             (a.when() == b.when() && a.id() - b.id() > 0);
    }
  };

  // Used for tasks that are already due, ordering them by priority.
  struct PriorityComparator {
    bool operator()(const Entry& a, const Entry& b) const {
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
    void add(Entry&& entry);

   private:
    Executable* head_ = nullptr;
  };

  friend class SingletonTask;
  // Retired owned tasks must outlive both scheduler and adapter lock guards.
  ExecutionID replace(ExecutionID previous, roo_time::Uptime when,
                      Executable& task, Priority priority,
                      RetiredTasks& retired);

  roo_time::Uptime getNearestExecutionTimeWithLockHeld() const;

  roo_time::Duration getNearestExecutionDelayWithLockHeld() const;

  ExecutionID nextExecutionId();

  ExecutionID push(roo_time::Uptime when, Executable* task, bool owns_task,
                   Priority priority);
  void pop(RetiredTasks& retired);

  enum class DispatchMode { kFixed, kCaptureCutoff, kDelay };

  struct DispatchSelection {
    roo_time::Uptime cutoff;
    Priority min_priority;
    DispatchMode mode;
  };

  bool executeEligibleTasks(DispatchSelection selection, int max_tasks);
  bool runOneEligibleExecution(DispatchSelection& selection);

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
  // Completion notifications also wake dispatch on builds without a work event.
  roo::condition_variable changed_;
#if ROO_THREADS_HAS_ISR_SUPPORT
  static constexpr unsigned kIsrCapacity = 8;
  static constexpr unsigned kAllIsrSlots = (1u << kIsrCapacity) - 1;

  bool hasIsrWork() const;
  roo_time::Uptime importIsrSlots();
  Entry* findIsrCandidate(roo_time::Uptime cutoff);
  void releaseIsrSlot(Entry& slot);
  void removeIsrSlots(Executable& task);

  roo::isr_event work_event_;
  mutable roo::isr_critical_section isr_lock_;

  // ISR metadata under isr_lock_. Capture the posted list while leaving its
  // slots occupied. After capture, only the dispatcher or cancellation can
  // touch a slot until it is returned to the free mask.
  Entry isr_slots_[kIsrCapacity];
  uint8_t posted_isr_slots_[kIsrCapacity];
  uint8_t posted_isr_count_ = 0;
  uint8_t free_isr_slots_ = kAllIsrSlots;

  // Admission state is protected by the ordinary scheduler mutex.
  uint8_t imported_isr_slots_ = 0;
#if !ROO_SCHEDULER_IGNORE_PRIORITY
  uint8_t ready_isr_slots_ = 0;
#endif
#endif
};

/// Schedules and dispatches delayed task executions.
///
/// SchedulingService does not execute eligible work automatically; caller must
/// invoke one of `executeEligibleTasks*()` methods.
///
/// With a thread-safe mutex, scheduling, cancellation and queries may be
/// called from any thread. Dispatch must be serialized on one thread, where
/// nested dispatch is allowed. With ROO_THREADS_SINGLETHREADED on FreeRTOS,
/// one task must make all ordinary calls for this service. On bare-metal
/// builds with a no-op mutex, use one core or serialize ordinary calls
/// externally. Supported interrupt producers can use tryScheduleNowFromIsr().
/// Callbacks and canceled task destructors run without the scheduler mutex.
/// Stop dispatch and all producers before destroying the scheduler. The
/// scheduler must outlive every adapter that refers to it.
class SchedulingService : public Scheduler {
 public:
  /// Creates an empty scheduling service.
  SchedulingService() = default;

  /// Destroys queued work after dispatch and all producers have stopped.
  ~SchedulingService() = default;

  /// Admits queued tasks due no later than now, then dispatches eligible work.
  /// See executeEligibleTasksUpTo() for admission semantics.
  ///
  /// Tasks below `min_priority` are ignored (not executed).
  ///
  /// @return true if no eligible executions remain in queue; false otherwise.
  bool executeEligibleTasksUpToNow(Priority min_priority = Priority::kMinimum,
                                   int max_count = -1);

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

  /// Cancels all pending executions of a borrowed task and waits for claimed
  /// executions to return. Prevent concurrent rescheduling before calling this.
  /// Disable interrupt producers for this task and synchronize with handlers
  /// already in progress before calling. Do not hold a lock that the callback
  /// needs. Returns false instead of
  /// waiting if this thread is currently dispatching the task (including an
  /// outer callback during nested dispatch). In that case it is not quiescent.
  bool cancelAndWait(Executable& task) {
    return Scheduler::cancelAndWait(task);
  }

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

  /// Lets an application loop sleep until a scheduled task is due or a wakeup
  /// occurs. Returns immediately if work is already due.
  ///
  /// A backend that supports raw processor events can also wake this wait.
  /// Single-threaded builds without an ISR work event poll an idle queue at
  /// 1 ms intervals to let the platform's cooperative event pump run.
  ///
  /// Submissions can wake a wait for an empty queue or a later deadline.
  /// Call from the dispatch context and recheck application signals and work
  /// after each return. This method does not execute tasks.
  void waitForWork();

  /// Runs the scheduler event loop forever, dispatching work and waiting for
  /// the next task. Use waitForWork() in a custom loop to handle Pico events.
  void run();

 private:
  void waitUntilNextWork(roo_time::Uptime deadline);
};

/// Compatibility alias for the restricted scheduling interface.
using SchedulerClient
    [[deprecated("Use Scheduler for scheduling and cancellation")]] = Scheduler;

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
  RepetitiveTask(Scheduler& scheduler, roo_time::Duration delay,
                 std::function<void()> task,
                 Priority priority = Priority::kNormal);

#ifndef ROO_SCHEDULER_NO_DEPRECATED
  /// @deprecated Use `RepetitiveTask(scheduler, delay, task, priority)`.
  RepetitiveTask(Scheduler& scheduler, std::function<void()> task,
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
  Scheduler& scheduler_;
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
  PeriodicTask(Scheduler& scheduler, roo_time::Duration period,
               std::function<void()> task,
               Priority priority = Priority::kNormal);

#ifndef ROO_SCHEDULER_NO_DEPRECATED
  /// @deprecated Use `PeriodicTask(scheduler, period, task, priority)`.
  PeriodicTask(Scheduler& scheduler, std::function<void()> task,
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
  Scheduler& scheduler_;
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
  SingletonTask(Scheduler& scheduler, std::function<void()> task);

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
  Scheduler& scheduler_;
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

  IteratingTask(Scheduler& scheduler, Iterator& iterator,
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
  Scheduler& scheduler_;
  Iterator& itr_;
  mutable roo::mutex mutex_;
  // Nonnegative: active execution; -1: inactive; -2: permanently shut down.
  ExecutionID id_;

  /// Called when iterator finishes; callback may delete the iterating task.
  std::shared_ptr<std::function<void()>> done_cb_;
};

}  // namespace roo_scheduler
