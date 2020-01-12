
#include "roo_scheduler.h"

namespace roo_scheduler {

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
  Executable* task = queue_.top().task();
  queue_.pop();
  task->execute();
  return true;
}

}  // namespace roo_scheduler
