#include "roo_scheduler.h"

#include <algorithm>

namespace roo_scheduler {

Scheduler::Scheduler() : queue_(), next_execution_id_(0), canceled_(0) {}

ExecutionID Scheduler::scheduleOn(Executable* task, roo_time::Uptime when,
                                  Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  nonempty_.notify_all();
  return push(task, when, priority);
}

ExecutionID Scheduler::scheduleAfter(Executable* task, roo_time::Interval delay,
                                     Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  nonempty_.notify_all();
  return push(task, roo_time::Uptime::Now() + delay, priority);
}

ExecutionID Scheduler::push(Executable* task, roo_time::Uptime when,
                            Priority priority) {
  ExecutionID id = next_execution_id_;
  ++next_execution_id_;
  // Reserve negative IDs for special use.
  next_execution_id_ &= 0x7FFFFFFF;
  queue_.emplace_back(id, task, when, priority);
  std::push_heap(queue_.begin(), queue_.end(), TimeComparator());
  return id;
}

// The queue must be non-empty.
void Scheduler::pop() {
  std::pop_heap(queue_.begin(), queue_.end(), TimeComparator());
  queue_.pop_back();
  // Fix the possibly broken invariant - get a non-cancelled task, if exists, at
  // the top of the queue.
  while (!canceled_.empty()) {
    if (queue_.empty()) {
      canceled_.clear();
      return;
    }
    ExecutionID id = queue_.front().id();
    if (!canceled_.erase(id)) return;
    std::pop_heap(queue_.begin(), queue_.end(), TimeComparator());
    queue_.pop_back();
  }
}

bool Scheduler::executeEligibleTasksUpTo(roo_time::Uptime deadline,
                                         Priority min_priority, int max_tasks) {
  while (max_tasks < 0 || max_tasks-- > 0) {
    if (!runOneEligibleExecution(deadline, min_priority)) return true;
  }
  return false;
}

bool Scheduler::executeEligibleTasks(Priority min_priority, int max_tasks) {
  while (max_tasks < 0 || max_tasks-- > 0) {
    if (!runOneEligibleExecution(roo_time::Uptime::Now(), min_priority))
      return true;
  }
  return false;
}

roo_time::Uptime Scheduler::getNearestExecutionTime() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return getNearestExecutionTimeWithLockHeld();
}

roo_time::Uptime Scheduler::getNearestExecutionTimeWithLockHeld() const {
#if !ROO_SCHEDULER_IGNORE_PRIORITY
  if (!ready_.empty()) {
    return roo_time::Uptime::Now();
  }
#endif
  if (!queue_.empty()) {
    return queue_.front().when();
  }
  return roo_time::Uptime::Max();
}

roo_time::Interval Scheduler::getNearestExecutionDelay() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return getNearestExecutionDelayWithLockHeld();
}

roo_time::Interval Scheduler::getNearestExecutionDelayWithLockHeld() const {
#if !ROO_SCHEDULER_IGNORE_PRIORITY
  if (!ready_.empty()) {
    return roo_time::Interval();
  }
#endif
  if (!queue_.empty()) {
    roo_time::Uptime next = queue_.front().when();
    roo_time::Uptime now = roo_time::Uptime::Now();
    return (next < now ? roo_time::Interval() : next - now);
  }
  return roo_time::Interval::Max();
}

#if !ROO_SCHEDULER_IGNORE_PRIORITY
bool Scheduler::runOneEligibleExecution(roo_time::Uptime deadline,
                                        Priority min_priority) {
  Executable* task = nullptr;
  ExecutionID id;
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    // Move all due tasks to the ready queue.
    roo_time::Uptime now = roo_time::Uptime::Now();
    if (deadline > now) deadline = now;
    while (!queue_.empty() && queue_.front().when() <= deadline) {
      ready_.push_back(queue_.front());
      std::push_heap(ready_.begin(), ready_.end(), PriorityComparator());
      pop();
    }
    while (!ready_.empty()) {
      const Entry& entry = ready_.front();
      task = entry.task();
      id = entry.id();
      bool canceled = canceled_.erase(id);
      if (!canceled && entry.priority() < min_priority) {
        // Next ready task is too low priority.
        return false;
      }
      std::pop_heap(ready_.begin(), ready_.end(), PriorityComparator());
      ready_.pop_back();
      if (!canceled) {
        // Found an eligible task (not canceled, with high enough priority).
        break;
      }
    }
  }
  if (task == nullptr) {
    // No ready tasks.
    return false;
  }
  task->execute(id);
  return true;
}
#else
bool Scheduler::runOneEligibleExecution(roo_time::Uptime deadline,
                                        Priority min_priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  // Move all due tasks to the ready queue.
  roo_time::Uptime now = roo_time::Uptime::Now();
  if (deadline > now) deadline = now;
  while (!queue_.empty() && queue_.front().when() <= deadline) {
    const Entry& entry = queue_.front();
    Executable* task = entry.task();
    ExecutionID id = entry.id();
    bool canceled = canceled_.erase(id);
    pop();
    if (!canceled) {
      // Found an eligible task (not canceled, with high enough priority).
      task->execute(id);
      return true;
    }
  }
  // No ready tasks.
  return false;
}
#endif

void Scheduler::cancel(ExecutionID id) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (queue_.empty()) {
    // There is nothing to cancel.
    return;
  }
  // Opportunistically check if the scheduled run is at the top of the queue and
  // can be immediately removed.
  if (queue_.front().id() == id) {
    // Found, indeed!
    pop();
    return;
  }
  // The task might be scheduled behind others; need to defer cancellation.
  canceled_.insert(id);
}

void Scheduler::pruneCanceled() {
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (canceled_.empty()) return;
  bool modified = false;
  size_t i = 0;
  while (i < queue_.size()) {
    if (canceled_.erase(queue_[i].id())) {
      modified = true;
      queue_[i] = queue_.back();
      queue_.pop_back();
    } else {
      ++i;
    }
    if (canceled_.empty()) break;
  }
  // Clear the canceled set, on the off chance that it contained any IDs that
  // were not actually found in the queue at all.
  canceled_.clear();
  if (modified) {
    std::make_heap(queue_.begin(), queue_.end(), TimeComparator());
  }
}

void Scheduler::delay(roo_time::Interval delay, Priority min_priority) {
  delayUntil(roo_time::Uptime::Now() + delay, min_priority);
}

void Scheduler::delayUntil(roo_time::Uptime deadline, Priority min_priority) {
  while (roo_time::Uptime::Now() < deadline) {
    if (executeEligibleTasks(1)) {
      roo::unique_lock<roo::mutex> lock(mutex_);
      roo_time::Uptime next = getNearestExecutionTimeWithLockHeld();
      if (next > deadline) next = deadline;
      auto now = roo_time::Uptime::Now();
      if (next > now) {
        nonempty_.wait_until(lock, next);
      }
    }
  }
  executeEligibleTasksUpTo(deadline, min_priority);
}

void Scheduler::run() {
  while (true) {
    executeEligibleTasks();
    roo::unique_lock<roo::mutex> lock(mutex_);
    roo_time::Interval delay = getNearestExecutionDelayWithLockHeld();
    nonempty_.wait_for(lock, delay);
  }
}

RepetitiveTask::RepetitiveTask(Scheduler& scheduler, std::function<void()> task,
                               roo_time::Interval delay, Priority priority)
    : scheduler_(scheduler),
      task_(task),
      id_(-1),
      active_(false),
      priority_(priority),
      delay_(delay) {}

// Starts the task, scheduling the next execution after the specified delay.
bool RepetitiveTask::start(roo_time::Interval initial_delay) {
  if (active_) return false;
  if (id_ >= 0) scheduler_.cancel(id_);
  active_ = true;
  id_ = scheduler_.scheduleAfter(this, initial_delay, priority_);
  return true;
}

bool RepetitiveTask::stop() {
  if (!active_) return false;
  active_ = false;
  return true;
}

void RepetitiveTask::execute(ExecutionID id) {
  if (id != id_ || !active_) return;
  task_();
  if (!active_) return;
  id_ = scheduler_.scheduleAfter(this, delay_, priority_);
}

RepetitiveTask::~RepetitiveTask() {
  if (id_ >= 0) scheduler_.cancel(id_);
}

PeriodicTask::PeriodicTask(Scheduler& scheduler, std::function<void()> task,
                           roo_time::Interval period, Priority priority)
    : scheduler_(scheduler),
      task_(task),
      id_(-1),
      active_(false),
      priority_(priority),
      period_(period) {}

bool PeriodicTask::start(roo_time::Uptime when) {
  if (active_) return false;
  if (id_ >= 0) scheduler_.cancel(id_);
  active_ = true;
  next_ = when;
  id_ = scheduler_.scheduleOn(this, next_, priority_);
  return true;
}

bool PeriodicTask::stop() {
  if (!active_) return false;
  active_ = false;
  return true;
}

void PeriodicTask::execute(ExecutionID id) {
  if (id != id_ || !active_) return;
  task_();
  next_ += period_;
  if (!active_) return;
  id_ = scheduler_.scheduleOn(this, next_, priority_);
}

PeriodicTask::~PeriodicTask() {
  if (id_ >= 0) scheduler_.cancel(id_);
}

SingletonTask::SingletonTask(Scheduler& scheduler, std::function<void()> task)
    : scheduler_(scheduler), task_(task), id_(-1), scheduled_(false) {}

void SingletonTask::scheduleOn(roo_time::Uptime when, Priority priority) {
  if (scheduled_) scheduler_.cancel(id_);
  id_ = scheduler_.scheduleOn(this, when, priority);
  scheduled_ = true;
}

void SingletonTask::scheduleAfter(roo_time::Interval delay, Priority priority) {
  if (scheduled_) scheduler_.cancel(id_);
  id_ = scheduler_.scheduleAfter(this, delay, priority);
  scheduled_ = true;
}

void SingletonTask::scheduleNow(Priority priority) {
  if (scheduled_) scheduler_.cancel(id_);
  id_ = scheduler_.scheduleNow(this, priority);
  scheduled_ = true;
}

void SingletonTask::execute(ExecutionID id) {
  if (!scheduled_ || id != id_) return;
  scheduled_ = false;
  id_ = -1;
  task_();
}

SingletonTask::~SingletonTask() {
  if (id_ >= 0) scheduler_.cancel(id_);
}

IteratingTask::IteratingTask(Scheduler& scheduler, Iterator& iterator,
                             std::function<void()> done_cb)
    : scheduler_(scheduler), itr_(iterator), id_(-1), done_cb_(done_cb) {}

bool IteratingTask::start(roo_time::Uptime when) {
  if (is_active()) return false;
  id_ = scheduler_.scheduleOn(this, when);
  return true;
}

void IteratingTask::execute(ExecutionID id) {
  int64_t next_delay_us = itr_.next();
  if (next_delay_us >= 0) {
    id_ = scheduler_.scheduleAfter(this, roo_time::Micros(next_delay_us));
  } else {
    id_ = -1;
    // This is the last thing we do, so that if the callback invokes our
    // destructor, that's OK. (That said, the callback should also do so at
    // the very end, because the callback is also destructing itself this
    // way).
    done_cb_();
  }
}

IteratingTask::~IteratingTask() {
  if (id_ >= 0) scheduler_.cancel(id_);
}

}  // namespace roo_scheduler
