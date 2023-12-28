
#include "roo_scheduler.h"

namespace roo_scheduler {

EventID Scheduler::scheduleOn(Executable* task, roo_time::Uptime when) {
  EventID id = next_event_id_++;
  queue_.emplace(id, task, when);
  return id;
}

EventID Scheduler::scheduleAfter(Executable* task, roo_time::Interval delay) {
  EventID id = next_event_id_;
  ++next_event_id_;
  // Reserve negative IDs for special use.
  next_event_id_ &= 0x07FFFFFFF;
  queue_.emplace(id, task, roo_time::Uptime::Now() + delay);
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
    return queue_.top().when();
  }
}

roo_time::Interval Scheduler::GetNextTaskDelay() const {
  if (queue_.empty()) {
    return roo_time::Interval::Max();
  } else {
    roo_time::Uptime next = queue_.top().when();
    roo_time::Uptime now = roo_time::Uptime::Now();
    return (next < now ? roo_time::Interval() : next - now);
  }
}

bool Scheduler::executeOneEligibleTask() {
  if (queue_.empty() || queue_.top().when() > roo_time::Uptime::Now()) {
    return false;
  }
  const Entry& entry = queue_.top();
  Executable* task = entry.task();
  EventID id = entry.id();
  queue_.pop();
  task->execute(id);
  return true;
}

}  // namespace roo_scheduler
