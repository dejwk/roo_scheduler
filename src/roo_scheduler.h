#pragma once

#include <functional>
#include <memory>
#include <queue>

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

typedef int32_t EventID;

// Abstract interface for executable tasks in the scheduler queue.
class Executable {
 public:
  virtual ~Executable() = default;
  virtual void execute(EventID id) = 0;
};

// Allows executables to be scheduled at specified delays. Usually you
// will only have once static instance of this class in your program.
// At any point, a number of scheduled tasks may be in the state
// 'eligible for execution'. The scheduler does not execute those tasks
// on its own; it is the client's responsibility to do so explicitly,
// by calling 'executeEligibleTasks'.
class Scheduler {
 public:
  Scheduler() : next_event_id_(0) {}

  // Schedules the specified task to be executed no earlier than at the
  // specified absolute time.
  EventID scheduleOn(Executable* task, roo_time::Uptime when) {
    EventID id = next_event_id_++;
    queue_.emplace(id, task, when);
    return id;
  }

  // Schedules the specified task to be executed no earlier than after the
  // specified delay.
  EventID scheduleAfter(Executable* task, roo_time::Interval delay) {
    EventID id = next_event_id_;
    ++next_event_id_;
    // Reserve negative IDs for special use.
    next_event_id_ &= 0x07FFFFFFF;
    queue_.emplace(id, task, roo_time::Uptime::Now() + delay);
    return id;
  }

  // Schedules the specified task to be executed ASAP.
  EventID scheduleNow(Executable* task) {
    return scheduleOn(task, roo_time::Uptime::Now());
  }

  // Execute up to max_tasks of eligible tasks. Returns true if all eligible
  // tasks have been executed; false if some eligible tasks have remained in the
  // queue.
  bool executeEligibleTasks(int max_tasks = -1);

  // Returns the time when the next scheduler task will be executed.
  roo_time::Uptime GetNextTaskTime() const;

  // Returns the time interval after which the next scheduler task will be
  // executed.
  roo_time::Interval GetNextTaskDelay() const;

 private:
  class Entry {
   public:
    Entry(EventID id, Executable* task, roo_time::Uptime when)
        : id_(id), task_(task), when_(when) {}

    roo_time::Uptime when() const { return when_; }
    Executable* task() const { return task_; }
    EventID id() const { return id_; }

   private:
    EventID id_;
    Executable* task_;
    roo_time::Uptime when_;
  };

  // Returns true if the task has been executed; false if there was
  // no eligible task.
  bool executeOneEligibleTask();

  friend bool operator<(const Entry& a, const Entry& b);

  // Internally, the queue uses std::vector, so when the number of tasks
  // is bounded, there will be no dynamic allocation once the underlying
  // vector reaches sufficient capacity. On the other hand, if tasks are
  // dynamically allocated, the queue can accommodate arbitrary number
  // of them as long as there is sufficient memory.
  std::priority_queue<Entry> queue_;

  EventID next_event_id_;
};

inline bool operator<(const Scheduler::Entry& a, const Scheduler::Entry& b) {
  return a.when() > b.when();
}

// A convenience adapter that allows to schedule a one-time execution of
// an arbitrary C++ callable.
class Task : public Executable {
 public:
  Task(std::function<void()> task) : task_(task) {}
  void execute(EventID id) override { task_(); }

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
                 roo_time::Interval delay)
      : scheduler_(scheduler),
        task_(task),
        id_(-1),
        active_(false),
        delay_(delay) {}

  bool is_active() const { return active_; }

  // Starts the task, scheduling the next execution after its regular configured
  // delay.
  void start() { start(delay_); }

  // Starts the task, scheduling the next execution immediately.
  void startInstantly() { start(roo_time::Millis(0)); }

  // Starts the task, scheduling the next execution after the specified delay.
  void start(roo_time::Interval initial_delay) {
    active_ = true;
    id_ = scheduler_.scheduleAfter(this, initial_delay);
  }

  void stop() {
    active_ = false;
    id_ = -1;
  }

  void execute(EventID id) override {
    if (id != id_ || !active_) return;
    task_();
    if (!active_) return;
    id_ = scheduler_.scheduleAfter(this, delay_);
  }

 private:
  Scheduler& scheduler_;
  std::function<void()> task_;
  EventID id_;
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
               roo_time::Interval period)
      : scheduler_(scheduler),
        task_(task),
        id_(-1),
        active_(false),
        period_(period) {}

  bool is_active() const { return active_; }

  void start(roo_time::Uptime when = roo_time::Uptime::Now()) {
    active_ = true;
    next_ = when;
    id_ = scheduler_.scheduleOn(this, next_);
  }

  void stop() {
    active_ = false;
    id_ = -1;
  }

  void execute(EventID id) override {
    if (id != id_ || !active_) return;
    task_();
    next_ += period_;
    if (!active_) return;
    id_ = scheduler_.scheduleOn(this, next_);
  }

 private:
  Scheduler& scheduler_;
  std::function<void()> task_;
  EventID id_;
  bool active_;
  roo_time::Interval period_;
  roo_time::Uptime next_;
};

// A convenience adapter that allows to schedule 'refresh'-type tasks, that can
// be canceled or rescheduled. If the task is scheduled the second time while
// its another execution is already pending, that other execution is effectively
// 'canceled' - i.e. the task will not be triggered on its due time. (The entry
// is not actually removed from the queue, so the task object cannot be deleted
// until all its entries are past due and processed).
class SingletonTask : public Executable {
 public:
  SingletonTask(Scheduler& scheduler, std::function<void()> task)
      : scheduler_(scheduler), task_(task), id_(-1) {}

  bool is_scheduled() const { return id_ >= 0; }

  // (Re)schedules the execution of the task at the specific absolute time.
  //
  // If the task is already scheduled (is_scheduled() returning true), the new
  // entry 'overrides' the previous instance - i.e. the task will only trigger
  // on `when`.
  //
  // Note: the previous instance is not deleted from the queue. The task must
  // stay alive until all previously scheduled instances are past due and
  // processed.
  void scheduleOn(roo_time::Uptime when = roo_time::Uptime::Now()) {
    id_ = scheduler_.scheduleOn(this, when);
  }

  // (Re)schedules the execution of the task at the specified delay.
  //
  // If the task is already scheduled (is_scheduled() returning true), the new
  // entry 'overrides' the previous instance - i.e. the task will only trigger
  // on `when`.
  //
  // Note: the previous instance is not deleted from the queue. The task must
  // stay alive until all previously scheduled instances are past due and
  // processed.
  void scheduleAfter(roo_time::Interval delay) {
    id_ = scheduler_.scheduleAfter(this, delay);
  }

  void cancel() { id_ = -1; }

  bool isScheduled() const { return id_ >= 0; }

  void execute(EventID id) override {
    if (id != id_) return;
    task_();
    // Note: task may have re-scheduled itself. In this case, just return.
    // Otherwise, mark as inactive, as we have done our job and aren't
    // rescheduled.
    if (id != id_) return;
    id_ = -1;
  }

 private:
  Scheduler& scheduler_;
  std::function<void()> task_;
  EventID id_;
};

class IteratingTask : public Executable {
 public:
  class Iterator {
   public:
    virtual ~Iterator() = default;
    virtual int64_t next() = 0;
  };

  IteratingTask(Scheduler& scheduler, Iterator& iterator,
                std::function<void()> done_cb = std::function<void()>())
      : scheduler_(scheduler),
        itr_(iterator),
        active_(false),
        done_cb_(done_cb) {}

  void start(roo_time::Uptime when = roo_time::Uptime::Now()) {
    active_ = true;
    scheduler_.scheduleOn(this, when);
  }

  void execute(EventID id) override {
    int64_t next_delay_us = itr_.next();
    if (next_delay_us >= 0) {
      scheduler_.scheduleAfter(this, roo_time::Micros(next_delay_us));
    } else {
      active_ = false;
      // This is the last thing we do, so that if the callback invokes our
      // destructor, that's OK. (That said, the callback should also do so at
      // the very end, because the callback is also destructing itself this
      // way).
      done_cb_();
    }
  }

  bool is_active() const { return active_; }

 private:
  Scheduler& scheduler_;
  Iterator& itr_;
  bool active_;

  // Called when the iterator finishes. Allowed to delete the iterating task.
  std::function<void()> done_cb_;
};

}  // namespace roo_scheduler
