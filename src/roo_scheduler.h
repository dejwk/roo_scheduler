#pragma once

#include <functional>
#include <memory>
#include <vector>

#include "roo_collections.h"
#include "roo_collections/flat_small_hash_set.h"
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
// }

namespace roo_scheduler {

// Represents a unique task execution identifier.
using ExecutionID = int32_t;

// Deprecated; prefer ExecutionID.
using EventID = ExecutionID;

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
  // specified absolute time.
  ExecutionID scheduleOn(Executable* task, roo_time::Uptime when);

  // Schedules the specified task to be executed no earlier than after the
  // specified delay.
  ExecutionID scheduleAfter(Executable* task, roo_time::Interval delay);

  // Schedules the specified task to be executed ASAP.
  ExecutionID scheduleNow(Executable* task) {
    return scheduleOn(task, roo_time::Uptime::Now());
  }

  // Execute up to max_count of eligible task executions. Returns true if the
  // queue has been cleared; false if some eligible executions have remained in
  // the queue.
  bool executeEligibleTasks(int max_count = -1);

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

  // Returns true if the scheduler queue contains any (non-canceled)
  // task executions.
  bool empty() const { return queue_.empty(); }

  // Blocks for at least the delay (similarly to roo_time::Delay(), or
  // Arduino delay()), except that it keeps executing scheduled work.
  //
  // Caution: since the scheduled tasks are executing with call stack that
  // begins at the call site of this method, stack overflow is more likely
  // than in the standard scenario of calling scheduleEligibleTasks() directly
  // e.g. from loop().
  void delay(roo_time::Interval delay);

 private:
  class Entry {
   public:
    Entry(ExecutionID id, Executable* task, roo_time::Uptime when)
        : id_(id), task_(task), when_(when) {}

    roo_time::Uptime when() const { return when_; }
    Executable* task() const { return task_; }
    ExecutionID id() const { return id_; }

    bool operator<(const Entry& other) {
      return when() > other.when() ||
             (when() == other.when() && id() - other.id() > 0);
    }

   private:
    ExecutionID id_;
    Executable* task_;
    roo_time::Uptime when_;
  };

  const Entry& top() const { return queue_.front(); }

  ExecutionID push(Executable* task, roo_time::Uptime when);
  void pop();

  // Returns true if has been executed; false if there was no eligible
  // execution.
  bool runOneEligibleExecution();

  void pruneUpcomingCanceledExecutions();

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

  ExecutionID next_execution_id_;

  // Deferred cancellation set, containing IDs of scheduled executions that have
  // been canceled. They will not run when due, and the tasks they refer to can
  // be safely destroyed.
  //
  // Calling pruneCanceled() removes all canceled executions from the queue, and
  // clears this set.
  roo_collections::FlatSmallHashSet<ExecutionID> canceled_;
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
  RepetitiveTask(Scheduler& scheduler, std::function<void()> task,
                 roo_time::Interval delay);

  bool is_active() const { return active_; }

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

  ~RepetitiveTask();

 private:
  Scheduler& scheduler_;
  std::function<void()> task_;
  ExecutionID id_;
  bool active_;
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
  PeriodicTask(Scheduler& scheduler, std::function<void()> task,
               roo_time::Interval period);

  bool is_active() const { return active_; }

  bool start(roo_time::Uptime when = roo_time::Uptime::Now());

  bool stop();

  void execute(ExecutionID id) override;

  ~PeriodicTask();

 private:
  Scheduler& scheduler_;
  std::function<void()> task_;
  ExecutionID id_;
  bool active_;
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
  void scheduleOn(roo_time::Uptime when);

  // (Re)schedules the execution of the task at the specified delay.
  //
  // If the task is already scheduled (is_scheduled() returning true), the new
  // entry 'overrides' the previous instance - i.e. the task will only trigger
  // on `when`.
  void scheduleAfter(roo_time::Interval delay);

  // (Re)schedules the execution of the task to run ASAP.
  //
  // If the task is already scheduled (is_scheduled() returning true), the new
  // entry 'overrides' the previous instance - i.e. the task will only trigger
  // on `when`.
  void scheduleNow();

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

  // Called when the iterator finishes. Allowed to delete the iterating task.
  std::function<void()> done_cb_;
};

}  // namespace roo_scheduler
