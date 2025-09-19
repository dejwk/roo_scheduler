#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "roo_collections.h"
#include "roo_collections/flat_small_hash_set.h"
#include "roo_threads.h"
#include "roo_threads/condition_variable.h"
#include "roo_threads/mutex.h"
#include "roo_time.h"

// A typical Arduino use case may look like the following:
//
// void foo();
//
// using namespace roo_time;
// using namespace roo_scheduler;
//
// Scheduler scheduler;
// RepetitiveTask foo_task(scheduler, foo, Seconds(5));
//
// void setup() {
//   // Instruct the scheduler to begin scheduling the foo_task.
//   foo_task.start();
// }
//
// void loop() {
//   // Actually execute the foo_task when due.
//   scheduler.executeEligibleTasks();
//   // ... other work
// }
//
// Also, see examples.

#ifndef ROO_SCHEDULER_IGNORE_PRIORITY
#define ROO_SCHEDULER_IGNORE_PRIORITY 0
#endif

namespace roo_scheduler {

// Represents a unique task execution identifier.
using ExecutionID = int32_t;

// Deprecated; prefer ExecutionID.
using EventID = ExecutionID;

// Priority dictates the order in which eligible tasks are executed. Higher
// priority tasks are executed first. Tasks with equal priority are executed in
// the FIFO order.
//
// The task becomes eligible for execution when its scheduled time arrives. If
// tasks complete quickly enough, and are spread in time sufficiently enough,
// they are executed in the order of their scheduled time, regardless of
// priority. In other words, priority only matters in case of contention, when
// multiple tasks are eligible for execution at the same time.
//
// The priority is assigned when the task is scheduled, and it remains unchanged
// for the lifetime of the scheduled execution. If a task is rescheduled, it may
// be assigned a different priority.
//
// The priority has no effect on the scheduling time of the task; it only
// affects the order in which eligible tasks are executed.
//
// The priority also affects the behavior of delay() and delayUntil(): those
// methods guarantee that all tasks with priority equal or higher than the
// specified priority will be executed before the method returns. Lower priority
// tasks might not be executed.
//
// Use higher priorities for short-running tasks that are latency-sensitive,
// and lower priorities for long-running tasks that are not latency-sensitive.
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

// Abstract interface for executable tasks in the scheduler queue.
class Executable {
 public:
  virtual ~Executable() = default;
  virtual void execute(ExecutionID id) = 0;
};

// Allows executables to be scheduled at specified delays. Usually you
// will only have once static instance of this class in your program.
// At any point, a number of scheduled tasks may be in the state
// 'eligible for execution'. The scheduler does not execute those tasks
// on its own; it is the client's responsibility to do so explicitly,
// by calling 'executeEligibleTasks'.
class Scheduler {
 public:
  // Creates a scheduler.
  Scheduler();

  // Schedules the specified task to be executed no earlier than at the
  // specified absolute time. The caller must ensure that the task object
  // remains valid until the scheduled execution occurs (or is canceled).
  ExecutionID scheduleOn(roo_time::Uptime when, Executable& task,
                         Priority priority = PRIORITY_NORMAL);

  // Schedules the specified task to be executed no earlier than at the
  // specified absolute time. The scheduler takes ownership of the task object
  // and will delete it after the scheduled execution occurs (or is canceled).
  ExecutionID scheduleOn(roo_time::Uptime when,
                         std::unique_ptr<Executable> task,
                         Priority priority = PRIORITY_NORMAL);

  // Schedules the specified callable to be executed no earlier than at the
  // specified absolute time.
  template <typename Callable,
            typename = std::enable_if_t<std::is_invocable_v<Callable>>>
  ExecutionID scheduleOn(roo_time::Uptime when, Callable task,
                         Priority priority = PRIORITY_NORMAL);

  // DEPRECATED. Use scheduleOn(when, task, priority) instead.
  ExecutionID scheduleOn(Executable* task, roo_time::Uptime when,
                         Priority priority = PRIORITY_NORMAL) {
    return scheduleOn(when, *task, priority);
  }

  // Schedules the specified task to be executed no earlier than after the
  // specified delay. The caller must ensure that the task object
  // remains valid until the scheduled execution occurs (or is canceled).
  ExecutionID scheduleAfter(roo_time::Interval delay, Executable& task,
                            Priority priority = PRIORITY_NORMAL);

  // Schedules the specified task to be executed no earlier than after the
  // specified delay. The scheduler takes ownership of the task object
  // and will delete it after the scheduled execution occurs (or is canceled).
  ExecutionID scheduleAfter(roo_time::Interval delay,
                            std::unique_ptr<Executable> task,
                            Priority priority = PRIORITY_NORMAL);

  // Schedules the specified callable to be executed no earlier than after the
  // specified delay.
  template <typename Callable,
            typename = std::enable_if_t<std::is_invocable_v<Callable>>>
  ExecutionID scheduleAfter(roo_time::Interval delay, Callable task,
                            Priority priority = PRIORITY_NORMAL);

  // DEPRECATED. Use scheduleAfter(delay, task, priority) instead.
  ExecutionID scheduleAfter(Executable* task, roo_time::Interval delay,
                            Priority priority = PRIORITY_NORMAL) {
    return scheduleAfter(delay, *task, priority);
  }

  // Schedules the specified task to be executed ASAP. The caller must ensure
  // that the task object remains valid until the scheduled execution occurs (or
  // is canceled).
  ExecutionID scheduleNow(Executable& task,
                          Priority priority = PRIORITY_NORMAL) {
    return scheduleOn(roo_time::Uptime::Now(), task, priority);
  }

  // Schedules the specified task to be executed ASAP. The scheduler takes
  // ownership of the task object and will delete it after the scheduled
  // execution occurs (or is canceled).
  ExecutionID scheduleNow(std::unique_ptr<Executable> task,
                          Priority priority = PRIORITY_NORMAL) {
    return scheduleOn(roo_time::Uptime::Now(), std::move(task), priority);
  }

  // Schedules the specified callable to be executed ASAP.
  template <typename Callable,
            typename = std::enable_if_t<std::is_invocable_v<Callable>>>
  ExecutionID scheduleNow(Callable task, Priority priority = PRIORITY_NORMAL) {
    return scheduleOn(roo_time::Uptime::Now(), std::forward<Callable>(task),
                      priority);
  }

  // Execute up to max_count of eligible task executions, whose scheduled time
  // is not greater than the time of invocation. Returns true if the queue has
  // been cleared; false if some eligible executions have remained in the queue.
  bool executeEligibleTasksUpToNow(Priority min_priority, int max_count = -1) {
    return executeEligibleTasksUpTo(roo_time::Uptime::Now(), min_priority,
                                    max_count);
  }

  // Execute up to max_count of eligible task executions, whose scheduled time
  // is not greater than the specified deadline. Returns true if the queue has
  // been cleared; false if some eligible executions have remained in the queue.
  bool executeEligibleTasksUpTo(roo_time::Uptime deadline,
                                Priority min_priority, int max_count = -1);

  // Execute up to max_count of eligible task executions, of at least the
  // specified priority. Returns true if the queue has been cleared; false if
  // some eligible executions have remained in the queue.
  bool executeEligibleTasks(Priority min_priority, int max_count = -1);

  // Execute up to max_count of eligible task executions. Returns true if the
  // queue has been cleared; false if some eligible executions have remained in
  // the queue.
  bool executeEligibleTasks(int max_count = -1) {
    return executeEligibleTasks(PRIORITY_MINIMUM, max_count);
  }

  // Returns the scheduled time of the nearest upcoming task execution.
  roo_time::Uptime getNearestExecutionTime() const;

  // Returns the time interval until the nearest upcoming task execution.
  roo_time::Interval getNearestExecutionDelay() const;

  // Indicates that the specified execution should be canceled.
  // The execution (and the task) may not be immediately removed from the queue,
  // but it will not run when due.
  //
  // See also pruneCanceled().
  void cancel(ExecutionID);

  // Clears all canceled executions from the queue. This method has linear
  // complexity (~3N, when N is the queue size) and should be used sparingly (if
  // at all).
  void pruneCanceled();

  // Returns false if the scheduler queue contains any (non-canceled)
  // task executions, true otherwise.
  bool empty() const { return queue_.empty(); }

  // Blocks for at least the delay (similarly to roo_time::Delay(), or
  // Arduino delay()), except that it keeps executing scheduled work.
  //
  // Tasks with scheduled execution time less or equal to now + delay, and
  // priority equal or larger than min_priority, are guaranteed to execute
  // before this method returns. Lower priority overdue tasks might not be
  // executed.
  //
  // Caution: since the scheduled tasks are executing with call stack that
  // begins at the call site of this method, stack overflow is more likely
  // than in the standard scenario of calling scheduleEligibleTasks() directly
  // e.g. from loop().
  void delay(roo_time::Interval delay, Priority min_priority = PRIORITY_NORMAL);

  // Similar to delay() above, but blocks until the specified deadline passes.
  //
  // Tasks with scheduled execution time less or equal to the deadline, and
  // priority equal or larger than min_priority, are guaranteed to execute
  // before this method returns. Lower priority overdue tasks might not be
  // executed.
  void delayUntil(roo_time::Uptime deadline,
                  Priority min_priority = PRIORITY_NORMAL);

  // Enters the 'event loop' mode, executing scheduled tasks. This method
  // never returns. It acts as an infinite delay(). It can be used to implement
  // purely event-driven apps, where the scheduled tasks are the only thing that
  // executes (besides interrupt handlers).
  void run();

 private:
  class Entry {
   public:
#if !ROO_SCHEDULER_IGNORE_PRIORITY
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

  roo_time::Interval getNearestExecutionDelayWithLockHeld() const;

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

// A convenience adapter that allows to schedule a one-time execution of
// an arbitrary C++ callable.
class Task : public Executable {
 public:
  Task(std::function<void()> task) : task_(task) {}
  void execute(ExecutionID id) override { task_(); }

 private:
  std::function<void()> task_;
};

// A convenience adapter that allows to schedule repetitive execution of
// an arbitrary C++ callable. Subsequent executions are scheduled with
// a constant delay in-between executiions. For example, if the task
// needs ~1 second to execute and the delay is 5s, the task will run
// approximately every 6 seconds.
class RepetitiveTask : public Executable {
 public:
  RepetitiveTask(Scheduler& scheduler, roo_time::Interval delay,
                 std::function<void()> task,
                 Priority priority = PRIORITY_NORMAL);

  // DEPRECATED. Use RepetitiveTask(scheduler, delay, task, priority) instead.
  RepetitiveTask(Scheduler& scheduler, std::function<void()> task,
                 roo_time::Interval delay, Priority priority = PRIORITY_NORMAL)
      : RepetitiveTask(scheduler, delay, std::move(task), priority) {}

  bool is_active() const { return active_; }

  Priority priority() const { return priority_; }

  // Starts the task, scheduling the next execution after its regular configured
  // delay. Returns true on success, false if the task had already been started.
  bool start() { return start(delay_); }

  // Starts the task, scheduling the next execution immediately. Returns true on
  // success, false if the task had already been started.
  bool startInstantly() { return start(roo_time::Millis(0)); }

  // Starts the task, scheduling the next execution after the specified delay.
  // Returns true on success, false if the task had already been started.
  bool start(roo_time::Interval initial_delay);

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
  roo_time::Interval delay_;
};

// A convenience adapter that allows to schedule periodic execution of
// an arbitrary C++ callable. Subsequent executions are scheduled at
// predefined time, so that the average execution frequency is constant,
// regardless of scheduling delays and task execution time.
//
// Use PeriodicTask in favor of RepetitiveTask when the unskewed frequency
// of execution is important; e.g. when using it to update some kind of a clock.
// Make sure that the execution time is shorter than the period, or else
// a backlog of late executions will build up.
class PeriodicTask : public Executable {
 public:
  PeriodicTask(Scheduler& scheduler, roo_time::Interval period,
               std::function<void()> task, Priority priority = PRIORITY_NORMAL);

  // DEPRECATED. Use PeriodicTask(scheduler, period, task, priority) instead.
  PeriodicTask(Scheduler& scheduler, std::function<void()> task,
               roo_time::Interval period, Priority priority = PRIORITY_NORMAL)
      : PeriodicTask(scheduler, period, std::move(task), priority) {}

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
  roo_time::Interval period_;
  roo_time::Uptime next_;
};

// A convenience adapter that allows to schedule 'refresh'-type tasks, that can
// be canceled or rescheduled. If the task is scheduled for the second time
// while its another execution is already pending, that other execution is
// canceled.
class SingletonTask : public Executable {
 public:
  SingletonTask(Scheduler& scheduler, std::function<void()> task);

  bool is_scheduled() const { return scheduled_; }

  // (Re)schedules the execution of the task at the specific absolute time.
  //
  // If the task is already scheduled (is_scheduled() returning true), the new
  // entry 'overrides' the previous instance - i.e. the task will only trigger
  // on `when`.
  void scheduleOn(roo_time::Uptime when, Priority priority = PRIORITY_NORMAL);

  // (Re)schedules the execution of the task at the specified delay.
  //
  // If the task is already scheduled (is_scheduled() returning true), the new
  // entry 'overrides' the previous instance - i.e. the task will only trigger
  // on `when`.
  void scheduleAfter(roo_time::Interval delay,
                     Priority priority = PRIORITY_NORMAL);

  // (Re)schedules the execution of the task to run ASAP.
  //
  // If the task is already scheduled (is_scheduled() returning true), the new
  // entry 'overrides' the previous instance - i.e. the task will only trigger
  // on `when`.
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

// A simple wrapper for one-off tasks that self-destruct upon execution.

template <typename Callable,
          typename = std::enable_if_t<std::is_invocable_v<Callable>>>
class OneOffTask : public Executable {
 public:
  OneOffTask(Callable task) : task_(std::forward<Callable>(task)) {}

  void execute(ExecutionID id) override {
    task_();
    delete this;
  }

 private:
  Callable task_;
};

template <typename Callable, typename>
ExecutionID Scheduler::scheduleOn(roo_time::Uptime when, Callable task,
                                  Priority priority) {
  return scheduleOn(when,
                    std::unique_ptr<Executable>(
                        new OneOffTask<Callable>(std::forward<Callable>(task))),
                    priority);
}

template <typename Callable, typename>
ExecutionID Scheduler::scheduleAfter(roo_time::Interval delay, Callable task,
                                     Priority priority) {
  return scheduleAfter(delay,
                       std::unique_ptr<Executable>(new OneOffTask<Callable>(
                           std::forward<Callable>(task))),
                       priority);
}

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

  // Called when the iterator finishes. Allowed to delete the iterating task.
  std::function<void()> done_cb_;
};

}  // namespace roo_scheduler
