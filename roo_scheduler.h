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
// RepetitiveTask foo_task(&scheduler, &foo, Seconds(5));
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

// Abstract interface for executable tasks in the scheduler queue.
class Executable {
 public:
  virtual void execute() = 0;
};

// Allows executables to be scheduled at specified delays. Usually you
// will only have once static instance of this class in your program.
// At any point, a number of scheduled tasks may be in the state
// 'eligible for execution'. The scheduler does not execute those tasks
// on its own; it is the client's responsibility to do so explicitly,
// by calling 'executeEligibleTasks'.
class Scheduler {
 public:
  // Schedules the specified task to be executed no earlier than at the
  // specified absolute time.
  void scheduleOn(Executable* task, roo_time::Uptime when) {
    queue_.emplace(task, when);
  }

  // Schedules the specified task to be executed no earlier than after the
  // specified delay.
  void scheduleAfter(Executable* task, roo_time::Interval delay) {
    queue_.emplace(task, roo_time::Uptime::Now() + delay);
  }

  // Schedules the specified task to be executed ASAP.
  void scheduleNow(Executable* task) {
    scheduleOn(task, roo_time::Uptime::Now());
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
    Entry(Executable* task, roo_time::Uptime when) : task_(task), when_(when) {}

    roo_time::Uptime when() const { return when_; }
    Executable* task() const { return task_; }

   private:
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
};

inline bool operator<(const Scheduler::Entry& a, const Scheduler::Entry& b) {
  return a.when() > b.when();
}

// A convenience adapter that allows to schedule a one-time execution of
// an arbitrary C++ callable.
class Task : public Executable {
 public:
  Task(std::function<void()> task) : task_(task) {}
  void execute() override { task_(); }

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
  RepetitiveTask(Scheduler* scheduler, std::function<void()> task,
                 roo_time::Interval delay)
      : scheduler_(scheduler), task_(task), active_(false), delay_(delay) {}

  bool is_active() const { return active_; }

  void start() { start(delay_); }

  void start(roo_time::Interval initial_delay) {
    active_ = true;
    scheduler_->scheduleAfter(this, initial_delay);
  }

  void execute() {
    task_();
    scheduler_->scheduleAfter(this, delay_);
  }

 private:
  Scheduler* scheduler_;
  std::function<void()> task_;
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
  PeriodicTask(Scheduler* scheduler, std::function<void()> task,
               roo_time::Interval period)
      : scheduler_(scheduler), task_(task), active_(false), period_(period) {}

  bool is_active() const { return active_; }

  void start(roo_time::Uptime when = roo_time::Uptime::Now()) {
    active_ = true;
    next_ = when;
    scheduler_->scheduleOn(this, next_);
  }

  void execute() {
    task_();
    next_ += period_;
    scheduler_->scheduleOn(this, next_);
  }

 private:
  Scheduler* scheduler_;
  std::function<void()> task_;
  bool active_;
  roo_time::Interval period_;
  roo_time::Uptime next_;
};

}  // namespace roo_scheduler
