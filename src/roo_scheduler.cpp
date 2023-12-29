#include "roo_scheduler.h"

#include <algorithm>

namespace roo_scheduler {

EventID Scheduler::scheduleOn(Executable* task, roo_time::Uptime when) {
  EventID id = next_event_id_++;
  queue_.emplace_back(id, task, when);
  std::push_heap(queue_.begin(), queue_.end());
  return id;
}

EventID Scheduler::scheduleAfter(Executable* task, roo_time::Interval delay) {
  EventID id = next_event_id_;
  ++next_event_id_;
  // Reserve negative IDs for special use.
  next_event_id_ &= 0x07FFFFFFF;
  queue_.emplace_back(id, task, roo_time::Uptime::Now() + delay);
  std::push_heap(queue_.begin(), queue_.end());
  return id;
}

bool Scheduler::executeEligibleTasks(int max_tasks) {
  while (max_tasks < 0 || max_tasks-- > 0) {
    if (!executeOneEligibleTask()) return true;
  }
  return false;
}

roo_time::Uptime Scheduler::GetNextTaskTime() const {
  if (queue_.empty()) {
    return roo_time::Uptime::Max();
  } else {
    return queue_.front().when();
  }
}

roo_time::Interval Scheduler::GetNextTaskDelay() const {
  if (queue_.empty()) {
    return roo_time::Interval::Max();
  } else {
    roo_time::Uptime next = queue_.front().when();
    roo_time::Uptime now = roo_time::Uptime::Now();
    return (next < now ? roo_time::Interval() : next - now);
  }
}

bool Scheduler::executeOneEligibleTask() {
  if (queue_.empty() || queue_.front().when() > roo_time::Uptime::Now()) {
    return false;
  }
  const Entry& entry = queue_.front();
  Executable* task = entry.task();
  EventID id = entry.id();
  std::pop_heap(queue_.begin(), queue_.end());
  queue_.pop_back();
  task->execute(id);
  return true;
}

void Scheduler::cancel(EventID id) {
  if (queue_.empty()) {
    // There is nothing to cancel.
    return;
  }
  // Opportunistically check if the scheduled run is at the top of the queue and
  // can be immediately removed.
  if (queue_.front().id() == id) {
    // Found, indeed!
    std::pop_heap(queue_.begin(), queue_.end());
    queue_.pop_back();
    return;
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

void RepetitiveTask::execute(EventID id) {
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

void PeriodicTask::execute(EventID id) {
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
    : scheduler_(scheduler), task_(task), id_(-1) {}

void SingletonTask::scheduleOn(roo_time::Uptime when) {
  if (scheduled_) scheduler_.cancel(id_);
  id_ = scheduler_.scheduleOn(this, when);
}

void SingletonTask::scheduleAfter(roo_time::Interval delay) {
  if (scheduled_) scheduler_.cancel(id_);
  id_ = scheduler_.scheduleAfter(this, delay);
}

void SingletonTask::scheduleNow() {
  if (scheduled_) scheduler_.cancel(id_);
  id_ = scheduler_.scheduleNow(this);
}

void SingletonTask::execute(EventID id) {
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

void IteratingTask::execute(EventID id) {
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
