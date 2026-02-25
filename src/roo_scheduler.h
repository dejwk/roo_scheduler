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
enum Priority {
  PRIORITY_MINIMUM = 0,
  PRIORITY_BACKGROUND = 1,
  PRIORITY_REDUCED = 2,
  PRIORITY_NORMAL = 3,
  PRIORITY_ELEVATED = 4,
  PRIORITY_SENSITIVE = 5,
  PRIORITY_CRITICAL = 6,
  PRIORITY_MAXIMUM = 7
};

/// Abstract interface for executable tasks in the scheduler queue.
class Executable {
 public:
  virtual ~Executable() = default;
  virtual void execute(ExecutionID id) = 0;
};

/// Schedules and dispatches delayed task executions.
///
/// Scheduler does not execute eligible work automatically; caller must invoke
/// one of `executeEligibleTasks*()` methods.
class Scheduler {
 public:
  /// Creates an empty scheduler.
  Scheduler();

  /// Schedules execution no earlier than `when`.
  ///
  /// Caller retains ownership and must keep `task` alive until execution or
  /// cancellation.
  ExecutionID scheduleOn(roo_time::Uptime when, Executable& task,
                         Priority priority = PRIORITY_NORMAL);

  /// Schedules execution no earlier than `when`.
  ///
  /// Scheduler takes ownership of `task` and destroys it after execution or
  /// cancellation.
  ExecutionID scheduleOn(roo_time::Uptime when,
                         std::unique_ptr<Executable> task,
                         Priority priority = PRIORITY_NORMAL);

  /// Schedules callable execution no earlier than `when`.
  ExecutionID scheduleOn(roo_time::Uptime when, std::function<void()> task,
                         Priority priority = PRIORITY_NORMAL);

#ifndef ROO_SCHEDULER_NO_DEPRECATED
  /// @deprecated Use `scheduleOn(when, task, priority)`.
  ExecutionID scheduleOn(Executable* task, roo_time::Uptime when,
                         Priority priority = PRIORITY_NORMAL) {
    return scheduleOn(when, *task, priority);
  }
#endif

  /// Schedules execution after `delay` elapses.
  ///
  /// Caller retains ownership and must keep `task` alive until execution or
  /// cancellation.
  ExecutionID scheduleAfter(roo_time::Duration delay, Executable& task,
                            Priority priority = PRIORITY_NORMAL);

  /// Schedules execution after `delay` elapses.
  ///
  /// Scheduler takes ownership of `task` and destroys it after execution or
  /// cancellation.
  ExecutionID scheduleAfter(roo_time::Duration delay,
                            std::unique_ptr<Executable> task,
                            Priority priority = PRIORITY_NORMAL);

  /// Schedules callable execution after `delay` elapses.
  ExecutionID scheduleAfter(roo_time::Duration delay,
                            std::function<void()> task,
                            Priority priority = PRIORITY_NORMAL);

#ifndef ROO_SCHEDULER_NO_DEPRECATED
  /// @deprecated Use `scheduleAfter(delay, task, priority)`.
  ExecutionID scheduleAfter(Executable* task, roo_time::Duration delay,
                            Priority priority = PRIORITY_NORMAL) {
    return scheduleAfter(delay, *task, priority);
  }
#endif

  /// Schedules execution as soon as possible.
  ///
  /// Caller retains ownership and must keep `task` alive until execution or
  /// cancellation.
  ExecutionID scheduleNow(Executable& task,
                          Priority priority = PRIORITY_NORMAL) {
    return scheduleOn(roo_time::Uptime::Now(), task, priority);
  }

  /// Schedules execution as soon as possible.
  ///
  /// Scheduler takes ownership of `task` and destroys it after execution or
  /// cancellation.
  ExecutionID scheduleNow(std::unique_ptr<Executable> task,
                          Priority priority = PRIORITY_NORMAL) {
    return scheduleOn(roo_time::Uptime::Now(), std::move(task), priority);
  }

  /// Schedules callable execution as soon as possible.
  ExecutionID scheduleNow(std::function<void()> task,
                          Priority priority = PRIORITY_NORMAL) {
    return scheduleOn(roo_time::Uptime::Now(), std::move(task), priority);
  }

  /// Executes up to `max_count` eligible tasks due no later than now.
  ///
  /// Tasks below `min_priority` are ignored (not executed).
  ///
  /// @return true if no eligible executions remain in queue; false otherwise.
  bool executeEligibleTasksUpToNow(Priority min_priority = PRIORITY_MINIMUM,
                                   int max_count = -1) {
    return executeEligibleTasksUpTo(roo_time::Uptime::Now(), min_priority,
                                    max_count);
  }

  /// Executes up to `max_count` eligible tasks due no later than `deadline`.
  ///
  /// Tasks below `min_priority` are ignored (not executed).
  ///
  /// @return true if no eligible executions remain in queue; false otherwise.
  bool executeEligibleTasksUpTo(roo_time::Uptime deadline,
                                Priority min_priority = PRIORITY_MINIMUM,
                                int max_count = -1);

  /// Executes up to `max_count` eligible tasks with at least `min_priority`.
  ///
  /// @return true if no eligible executions remain in queue; false otherwise.
  bool executeEligibleTasks(Priority min_priority, int max_count = -1);

  /// Executes up to `max_count` eligible tasks.
  ///
  /// @return true if no eligible executions remain in queue; false otherwise.
  bool executeEligibleTasks(int max_count = -1) {
    return executeEligibleTasks(PRIORITY_MINIMUM, max_count);
  }

  /// Returns due time of the nearest upcoming execution.
  roo_time::Uptime getNearestExecutionTime() const;

  /// Returns delay to the nearest upcoming execution.
  roo_time::Duration getNearestExecutionDelay() const;

  /// Marks execution identified by `id` as canceled.
  ///
  /// Canceled entries may remain in queue until pruned, but will not run.
  void cancel(ExecutionID);

  /// Removes canceled executions from the queue.
  ///
  /// This is linear-time and should be used sparingly.
  void pruneCanceled();

  /// Returns true iff no pending (non-canceled) executions exist.
  bool empty() const { return queue_.empty(); }

  /// Delays for at least `delay` while executing scheduled work.
  ///
  /// Tasks due by the requested return time (now + `delay`) with priority >=
  /// `min_priority` are guaranteed to execute before return. Lower-priority
  /// overdue tasks may remain pending.
  ///
  /// Note: because scheduled callbacks execute on the caller's stack, this mode
  /// can increase stack usage compared with explicit event-loop dispatch.
  void delay(roo_time::Duration delay, Priority min_priority = PRIORITY_NORMAL);

  /// Delays until `deadline` while executing scheduled work.
  ///
  /// Tasks due by `deadline` with priority >= `min_priority` are guaranteed to
  /// execute before return. Lower-priority overdue tasks may remain pending.
  void delayUntil(roo_time::Uptime deadline,
                  Priority min_priority = PRIORITY_NORMAL);

  /// Runs scheduler event loop forever.
  void run();

 private:
  class Entry {
   public:
#if !ROO_SCHEDULER_IGNORE_PRIORITY
    Entry()
        : id_(0),
          task_(nullptr),
          when_(roo_time::Uptime::Max()),
          priority_(PRIORITY_NORMAL),
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
      return PRIORITY_NORMAL;
#endif
    }

    bool owns_task() const { return owns_task_; }

   private:
    friend struct TimeComparator;

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
    bool operator()(const Entry& a, const Entry& b) {
      return a.when() > b.when() ||
             (a.when() == b.when() && a.id() - b.id() > 0);
    }
  };

  // Used for tasks that are already due, ordering them by priority.
  struct PriorityComparator {
    bool operator()(const Entry& a, const Entry& b) {
      return a.priority() < b.priority() ||
             (a.priority() == b.priority() &&
              (a.when() > b.when() ||
               (a.when() == b.when() && a.id() - b.id() > 0)));
    }
  };

  roo_time::Uptime getNearestExecutionTimeWithLockHeld() const;

  roo_time::Duration getNearestExecutionDelayWithLockHeld() const;

  ExecutionID push(roo_time::Uptime when, Executable* task, bool owns_task,
                   Priority priority);
  void pop();

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
  // been canceled. They will not run when due, and the tasks they refer to can
  // be safely destroyed.
  //
  // Calling pruneCanceled() removes all canceled executions from the queue, and
  // clears this set.
  roo_collections::FlatSmallHashSet<ExecutionID> canceled_;

  mutable roo::mutex mutex_;
  roo::condition_variable nonempty_;
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
class RepetitiveTask : public Executable {
 public:
  RepetitiveTask(Scheduler& scheduler, roo_time::Duration delay,
                 std::function<void()> task,
                 Priority priority = PRIORITY_NORMAL);

#ifndef ROO_SCHEDULER_NO_DEPRECATED
  /// @deprecated Use `RepetitiveTask(scheduler, delay, task, priority)`.
  RepetitiveTask(Scheduler& scheduler, std::function<void()> task,
                 roo_time::Duration delay, Priority priority = PRIORITY_NORMAL)
      : RepetitiveTask(scheduler, delay, std::move(task), priority) {}
#endif

  bool is_active() const { return active_; }

  Priority priority() const { return priority_; }

  /// Starts task using configured periodic delay.
  ///
  /// @return false if already active.
  bool start() { return start(delay_); }

  /// Starts task immediately.
  ///
  /// @return false if already active.
  bool startInstantly() { return start(roo_time::Millis(0)); }

  /// Starts task with custom initial delay.
  ///
  /// @return false if already active.
  bool start(roo_time::Duration initial_delay);

  bool stop();

  void execute(ExecutionID id) override;

  void setPriority(Priority priority) { priority_ = priority; }

  ~RepetitiveTask();

 private:
  Scheduler& scheduler_;
  std::function<void()> task_;
  ExecutionID id_;
  bool active_;
  Priority priority_;
  roo_time::Duration delay_;
};

/// Convenience adapter for periodic callable execution.
///
/// Uses fixed target schedule to keep average execution frequency stable.
class PeriodicTask : public Executable {
 public:
  PeriodicTask(Scheduler& scheduler, roo_time::Duration period,
               std::function<void()> task, Priority priority = PRIORITY_NORMAL);

#ifndef ROO_SCHEDULER_NO_DEPRECATED
  /// @deprecated Use `PeriodicTask(scheduler, period, task, priority)`.
  PeriodicTask(Scheduler& scheduler, std::function<void()> task,
               roo_time::Duration period, Priority priority = PRIORITY_NORMAL)
      : PeriodicTask(scheduler, period, std::move(task), priority) {}
#endif

  bool is_active() const { return active_; }

  Priority priority() const { return priority_; }

  bool start(roo_time::Uptime when = roo_time::Uptime::Now());

  bool stop();

  void execute(ExecutionID id) override;

  void setPriority(Priority priority) { priority_ = priority; }

  ~PeriodicTask();

 private:
  Scheduler& scheduler_;
  std::function<void()> task_;
  ExecutionID id_;
  bool active_;
  Priority priority_;
  roo_time::Duration period_;
  roo_time::Uptime next_;
};

/// Convenience adapter for cancelable and replaceable single pending work.
class SingletonTask : public Executable {
 public:
  SingletonTask(Scheduler& scheduler, std::function<void()> task);

  bool is_scheduled() const { return scheduled_; }

  /// Schedules or reschedules task at absolute time `when`.
  ///
  /// Any previously pending execution is canceled.
  void scheduleOn(roo_time::Uptime when, Priority priority = PRIORITY_NORMAL);

  /// Schedules or reschedules task after `delay`.
  ///
  /// Any previously pending execution is canceled.
  void scheduleAfter(roo_time::Duration delay,
                     Priority priority = PRIORITY_NORMAL);

  /// Schedules or reschedules task for immediate execution.
  ///
  /// Any previously pending execution is canceled.
  void scheduleNow(Priority priority = PRIORITY_NORMAL);

  void cancel() { scheduled_ = false; }

  void execute(ExecutionID id) override;

  ~SingletonTask();

 private:
  Scheduler& scheduler_;
  std::function<void()> task_;
  ExecutionID id_;
  bool scheduled_;
};

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

  bool is_active() const { return id_ >= 0; }

  ~IteratingTask();

 private:
  Scheduler& scheduler_;
  Iterator& itr_;
  ExecutionID id_;

  /// Called when iterator finishes; callback may delete the iterating task.
  std::function<void()> done_cb_;
};

}  // namespace roo_scheduler
