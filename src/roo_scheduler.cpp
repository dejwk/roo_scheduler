#include "roo_scheduler.h"

#include <algorithm>

namespace roo_scheduler {

ExecutionID Scheduler::scheduleOn(Executable* task, roo_time::Uptime when) {
  return push(task, when);
}

ExecutionID Scheduler::scheduleAfter(Executable* task,
                                     roo_time::Interval delay) {
  return push(task, roo_time::Uptime::Now() + delay);
}

ExecutionID Scheduler::push(Executable* task, roo_time::Uptime when) {
  ExecutionID id = next_execution_id_;
  ++next_execution_id_;
  // Reserve negative IDs for special use.
  next_execution_id_ &= 0x7FFFFFFF;
  queue_.emplace_back(id, task, when);
  std::push_heap(queue_.begin(), queue_.end());
  return id;
}

// The queue must be non-empty.
void Scheduler::pop() {
  std::pop_heap(queue_.begin(), queue_.end());
  queue_.pop_back();
  // Fix the possibly broken invariant - get a non-cancelled task, if exists, at
  // the top of the queue.
  pruneUpcomingCanceledExecutions();
}

bool Scheduler::executeEligibleTasks(int max_tasks) {
  while (max_tasks < 0 || max_tasks-- > 0) {
    if (!runOneEligibleExecution()) return true;
  }
  return false;
}

roo_time::Uptime Scheduler::GetNearestExecutionTime() const {
  if (queue_.empty()) {
    return roo_time::Uptime::Max();
  } else {
    return top().when();
  }
}

roo_time::Interval Scheduler::GetNearestExecutionDelay() const {
  if (queue_.empty()) {
    return roo_time::Interval::Max();
  } else {
    roo_time::Uptime next = top().when();
    roo_time::Uptime now = roo_time::Uptime::Now();
    return (next < now ? roo_time::Interval() : next - now);
  }
}

bool Scheduler::runOneEligibleExecution() {
  if (queue_.empty() || top().when() > roo_time::Uptime::Now()) {
    return false;
  }
  const Entry& entry = top();
  Executable* task = entry.task();
  ExecutionID id = entry.id();
  pop();
  task->execute(id);
  return true;
}

void Scheduler::pruneUpcomingCanceledExecutions() {
  while (!canceled_.empty()) {
    if (queue_.empty()) {
      canceled_.clear();
      return;
    }
    ExecutionID id = queue_.front().id();
    if (!canceled_.erase(id)) return;
    std::pop_heap(queue_.begin(), queue_.end());
    queue_.pop_back();
  }
}

void Scheduler::cancel(ExecutionID id) {
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
    std::make_heap(queue_.begin(), queue_.end());
  }
}

RepetitiveTask::RepetitiveTask(Scheduler& scheduler, std::function<void()> task,
                               roo_time::Interval delay)
    : scheduler_(scheduler),
      task_(task),
      id_(-1),
      active_(false),
      delay_(delay) {}

// Starts the task, scheduling the next execution after the specified delay.
bool RepetitiveTask::start(roo_time::Interval initial_delay) {
  if (active_) return false;
  if (id_ >= 0) scheduler_.cancel(id_);
  active_ = true;
  id_ = scheduler_.scheduleAfter(this, initial_delay);
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
  id_ = scheduler_.scheduleAfter(this, delay_);
}

RepetitiveTask::~RepetitiveTask() {
  if (id_ >= 0) scheduler_.cancel(id_);
}

PeriodicTask::PeriodicTask(Scheduler& scheduler, std::function<void()> task,
                           roo_time::Interval period)
    : scheduler_(scheduler),
      task_(task),
      id_(-1),
      active_(false),
      period_(period) {}

bool PeriodicTask::start(roo_time::Uptime when) {
  if (active_) return false;
  if (id_ >= 0) scheduler_.cancel(id_);
  active_ = true;
  next_ = when;
  id_ = scheduler_.scheduleOn(this, next_);
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
  id_ = scheduler_.scheduleOn(this, next_);
}

PeriodicTask::~PeriodicTask() {
  if (id_ >= 0) scheduler_.cancel(id_);
}

SingletonTask::SingletonTask(Scheduler& scheduler, std::function<void()> task)
    : scheduler_(scheduler), task_(task), id_(-1), scheduled_(false) {}

void SingletonTask::scheduleOn(roo_time::Uptime when) {
  if (scheduled_) scheduler_.cancel(id_);
  id_ = scheduler_.scheduleOn(this, when);
  scheduled_ = true;
}

void SingletonTask::scheduleAfter(roo_time::Interval delay) {
  if (scheduled_) scheduler_.cancel(id_);
  id_ = scheduler_.scheduleAfter(this, delay);
  scheduled_ = true;
}

void SingletonTask::scheduleNow() {
  if (scheduled_) scheduler_.cancel(id_);
  id_ = scheduler_.scheduleNow(this);
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
