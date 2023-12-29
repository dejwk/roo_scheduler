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

}  // namespace roo_scheduler
