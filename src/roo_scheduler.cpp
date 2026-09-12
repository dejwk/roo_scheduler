#include "roo_scheduler.h"

#include <algorithm>

namespace roo_scheduler {

Scheduler::Scheduler() : queue_(), next_execution_id_(0), canceled_(0) {}

ExecutionID Scheduler::scheduleOn(roo_time::Uptime when, Executable &task,
                                  Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  nonempty_.notify_all();
  return push(when, &task, false, priority);
}

ExecutionID Scheduler::scheduleOn(roo_time::Uptime when,
                                  std::unique_ptr<Executable> task,
                                  Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  nonempty_.notify_all();
  return push(when, task.release(), true, priority);
}

ExecutionID Scheduler::scheduleOn(roo_time::Uptime when,
                                  std::function<void()> task,
                                  Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  nonempty_.notify_all();
  return push(when, new Task(std::move(task)), true, priority);
}

ExecutionID Scheduler::scheduleAfter(roo_time::Duration delay, Executable &task,
                                     Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  nonempty_.notify_all();
  return push(roo_time::Uptime::Now() + delay, &task, false, priority);
}

ExecutionID Scheduler::scheduleAfter(roo_time::Duration delay,
                                     std::unique_ptr<Executable> task,
                                     Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  nonempty_.notify_all();
  return push(roo_time::Uptime::Now() + delay, task.release(), true, priority);
}

ExecutionID Scheduler::scheduleAfter(roo_time::Duration delay,
                                     std::function<void()> task,
                                     Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  nonempty_.notify_all();
  return push(roo_time::Uptime::Now() + delay, new Task(std::move(task)), true,
              priority);
}

ExecutionID Scheduler::push(roo_time::Uptime when, Executable *task,
                            bool owns_task, Priority priority) {
  ExecutionID id = next_execution_id_;
  // Reserve negative IDs without overflowing signed arithmetic.
  next_execution_id_ = (static_cast<uint32_t>(id) + 1) & 0x7FFFFFFF;
  queue_.emplace_back(id, task, owns_task, when, priority);
  std::push_heap(queue_.begin(), queue_.end(), TimeComparator());
  return id;
}

// The queue must be non-empty.
void Scheduler::pop(std::vector<Entry> &retired) {
  std::pop_heap(queue_.begin(), queue_.end(), TimeComparator());
  retired.push_back(std::move(queue_.back()));
  queue_.pop_back();
  while (!queue_.empty() && canceled_.erase(queue_.front().id())) {
    std::pop_heap(queue_.begin(), queue_.end(), TimeComparator());
    retired.push_back(std::move(queue_.back()));
    queue_.pop_back();
  }
  // Cancellation records may still refer to ready_ entries.
}

bool Scheduler::empty() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  for (const auto &entry : queue_) {
    if (!canceled_.contains(entry.id())) return false;
  }
#if !ROO_SCHEDULER_IGNORE_PRIORITY
  for (const auto &entry : ready_) {
    if (!canceled_.contains(entry.id())) return false;
  }
#endif
  return true;
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

roo_time::Duration Scheduler::getNearestExecutionDelay() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return getNearestExecutionDelayWithLockHeld();
}

roo_time::Duration Scheduler::getNearestExecutionDelayWithLockHeld() const {
#if !ROO_SCHEDULER_IGNORE_PRIORITY
  if (!ready_.empty()) {
    return roo_time::Duration();
  }
#endif
  if (!queue_.empty()) {
    roo_time::Uptime next = queue_.front().when();
    roo_time::Uptime now = roo_time::Uptime::Now();
    return (next < now ? roo_time::Duration() : next - now);
  }
  return roo_time::Duration::Max();
}

#if !ROO_SCHEDULER_IGNORE_PRIORITY
bool Scheduler::runOneEligibleExecution(roo_time::Uptime deadline,
                                        Priority min_priority) {
  roo_time::Uptime now = roo_time::Uptime::Now();
  if (deadline > now) deadline = now;
  std::vector<Entry> retired;
  Entry to_execute;
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    // Move all due tasks to the ready queue.
    while (!queue_.empty() && queue_.front().when() <= deadline) {
      ready_.push_back(std::move(queue_.front()));
      std::push_heap(ready_.begin(), ready_.end(), PriorityComparator());
      pop(retired);
    }
    // The cutoff admits queued work. Previously admitted work stays eligible,
    // including after nested dispatch or a later call with an earlier cutoff.
    while (!ready_.empty()) {
      if (canceled_.erase(ready_.front().id())) {
        std::pop_heap(ready_.begin(), ready_.end(), PriorityComparator());
        retired.push_back(std::move(ready_.back()));
        ready_.pop_back();
        continue;
      }
      if (ready_.front().priority() < min_priority) break;
      to_execute = std::move(ready_.front());
      std::pop_heap(ready_.begin(), ready_.end(), PriorityComparator());
      ready_.pop_back();
      break;
    }
  }
  if (to_execute.task() == nullptr) {
    // No ready tasks.
    return false;
  }
  to_execute.task()->execute(to_execute.id());
  return true;
}
#else
bool Scheduler::runOneEligibleExecution(roo_time::Uptime deadline,
                                        Priority min_priority) {
  roo_time::Uptime now = roo_time::Uptime::Now();
  if (deadline > now) deadline = now;
  std::vector<Entry> retired;
  Entry to_execute;
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    // Process all due tasks.
    while (!queue_.empty() && queue_.front().when() <= deadline) {
      Entry &entry = queue_.front();
      // ExecutionID id = entry.id();
      bool canceled = canceled_.erase(entry.id());
      if (!canceled) {
        if (entry.priority() < min_priority) {
          // Next ready task is too low priority.
          return false;
        }
        to_execute = std::move(entry);
      }
      pop(retired);
      if (to_execute.task() != nullptr) {
        // Found an eligible task (not canceled, with high enough priority).
        break;
      }
    }
  }
  if (to_execute.task() == nullptr) {
    // No ready tasks.
    return false;
  }
  to_execute.task()->execute(to_execute.id());
  return true;
}
#endif

void Scheduler::cancel(ExecutionID id) {
  std::vector<Entry> retired;
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (queue_.empty()
#if !ROO_SCHEDULER_IGNORE_PRIORITY
      && ready_.empty()
#endif
  )
    return;
  if (!queue_.empty() && queue_.front().id() == id) {
    pop(retired);
    return;
  }
  canceled_.insert(id);
}

void Scheduler::pruneCanceled() {
  std::vector<Entry> retired;
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (canceled_.empty()) return;
  auto prune = [&](std::vector<Entry> &entries, auto comparator) {
    bool modified = false;
    size_t i = 0;
    while (i < entries.size()) {
      if (canceled_.erase(entries[i].id())) {
        retired.push_back(std::move(entries[i]));
        entries[i] = std::move(entries.back());
        entries.pop_back();
        modified = true;
      } else {
        ++i;
      }
    }
    if (modified) std::make_heap(entries.begin(), entries.end(), comparator);
  };
  prune(queue_, TimeComparator());
#if !ROO_SCHEDULER_IGNORE_PRIORITY
  prune(ready_, PriorityComparator());
#endif
  canceled_.clear();
}

void Scheduler::delay(roo_time::Duration delay, Priority min_priority) {
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
    {
      roo::unique_lock<roo::mutex> lock(mutex_);
      roo_time::Duration delay = getNearestExecutionDelayWithLockHeld();
      if (delay > roo_time::Duration()) {
        if (delay == roo_time::Duration::Max()) {
          nonempty_.wait(lock);
        } else {
          nonempty_.wait_for(lock, delay);
        }
      }
    }
  }
}

RepetitiveTask::RepetitiveTask(Scheduler &scheduler, roo_time::Duration delay,
                               std::function<void()> task, Priority priority)
    : scheduler_(scheduler),
      task_(task),
      id_(-1),
      active_(false),
      priority_(priority),
      delay_(delay) {}

// Starts the task, scheduling the next execution after the specified delay.
bool RepetitiveTask::start(roo_time::Duration initial_delay) {
  if (active_) return false;
  if (id_ >= 0) scheduler_.cancel(id_);
  active_ = true;
  id_ = scheduler_.scheduleAfter(initial_delay, *this, priority_);
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
  id_ = scheduler_.scheduleAfter(delay_, *this, priority_);
}

RepetitiveTask::~RepetitiveTask() {
  if (id_ >= 0) scheduler_.cancel(id_);
}

PeriodicTask::PeriodicTask(Scheduler &scheduler, roo_time::Duration period,
                           std::function<void()> task, Priority priority)
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
  id_ = scheduler_.scheduleOn(next_, *this, priority_);
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
  id_ = scheduler_.scheduleOn(next_, *this, priority_);
}

PeriodicTask::~PeriodicTask() {
  if (id_ >= 0) scheduler_.cancel(id_);
}

SingletonTask::SingletonTask(Scheduler &scheduler, std::function<void()> task)
    : scheduler_(scheduler), task_(task), id_(-1), scheduled_(false) {}

void SingletonTask::scheduleOn(roo_time::Uptime when, Priority priority) {
  if (id_ >= 0) scheduler_.cancel(id_);
  id_ = scheduler_.scheduleOn(when, *this, priority);
  scheduled_ = true;
}

void SingletonTask::scheduleAfter(roo_time::Duration delay, Priority priority) {
  if (id_ >= 0) scheduler_.cancel(id_);
  id_ = scheduler_.scheduleAfter(delay, *this, priority);
  scheduled_ = true;
}

void SingletonTask::scheduleNow(Priority priority) {
  if (id_ >= 0) scheduler_.cancel(id_);
  id_ = scheduler_.scheduleNow(*this, priority);
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

IteratingTask::IteratingTask(Scheduler &scheduler, Iterator &iterator,
                             std::function<void()> done_cb)
    : scheduler_(scheduler), itr_(iterator), id_(-1), done_cb_(done_cb) {}

bool IteratingTask::start(roo_time::Uptime when) {
  if (is_active()) return false;
  id_ = scheduler_.scheduleOn(when, *this);
  return true;
}

void IteratingTask::execute(ExecutionID id) {
  int64_t next_delay_us = itr_.next();
  if (next_delay_us >= 0) {
    id_ = scheduler_.scheduleAfter(roo_time::Micros(next_delay_us), *this);
  } else {
    id_ = -1;
    // This is the last thing we do, so that if the callback invokes our
    // destructor, that's OK. (That said, the callback should also do so at
    // the very end, because the callback is also destructing itself this
    // way).
    if (done_cb_) done_cb_();
  }
}

IteratingTask::~IteratingTask() {
  if (id_ >= 0) scheduler_.cancel(id_);
}

}  // namespace roo_scheduler
