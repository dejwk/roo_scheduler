#include "roo_scheduler.h"

#include <algorithm>
#include <cassert>
#include <exception>

namespace roo_scheduler {
namespace {
// Execution IDs are nonnegative; encode idle and permanent shutdown in the
// same field rather than adding per-adapter lifecycle flags.
constexpr ExecutionID kInactive = -1;
constexpr ExecutionID kShutdown = -2;
}  // namespace

Scheduler::Scheduler() : queue_(), next_execution_id_(0), canceled_(0) {}

Scheduler::~Scheduler() {
  // Owned destructors may call back into the scheduler. Retire all tasks while
  // the mutex and queues are still alive, including work submitted by those
  // destructors. External producers and dispatch must already have stopped.
  while (true) {
    RetiredTasks retired;
    roo::lock_guard<roo::mutex> lock(mutex_);
    assert(in_flight_ == nullptr);
    if (queue_.empty()
#if !ROO_SCHEDULER_IGNORE_PRIORITY
        && ready_.empty()
#endif
    )
      return;
    for (auto &entry : queue_) retired.add(std::move(entry));
    queue_.clear();
#if !ROO_SCHEDULER_IGNORE_PRIORITY
    for (auto &entry : ready_) retired.add(std::move(entry));
    ready_.clear();
#endif
    canceled_.clear();
  }
}

ExecutionID Scheduler::scheduleOn(roo_time::Uptime when, Executable &task,
                                  Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  changed_.notify_all();
  return push(when, &task, false, priority);
}

ExecutionID Scheduler::scheduleOn(roo_time::Uptime when,
                                  std::unique_ptr<Executable> task,
                                  Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  changed_.notify_all();
  return push(when, task.release(), true, priority);
}

ExecutionID Scheduler::scheduleOn(roo_time::Uptime when,
                                  std::function<void()> task,
                                  Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  changed_.notify_all();
  return push(when, new Task(std::move(task)), true, priority);
}

ExecutionID Scheduler::scheduleAfter(roo_time::Duration delay, Executable &task,
                                     Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  changed_.notify_all();
  return push(roo_time::Uptime::Now() + delay, &task, false, priority);
}

ExecutionID Scheduler::scheduleAfter(roo_time::Duration delay,
                                     std::unique_ptr<Executable> task,
                                     Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  changed_.notify_all();
  return push(roo_time::Uptime::Now() + delay, task.release(), true, priority);
}

ExecutionID Scheduler::scheduleAfter(roo_time::Duration delay,
                                     std::function<void()> task,
                                     Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  changed_.notify_all();
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

Scheduler::RetiredTasks::~RetiredTasks() {
  while (head_ != nullptr) {
    Executable *task = head_;
    head_ = task->retired_next_;
    delete task;
  }
}

void Scheduler::RetiredTasks::add(Entry &&entry) {
  Executable *task = entry.releaseOwnedTask();
  if (task == nullptr) return;
  task->retired_next_ = head_;
  head_ = task;
}

// The queue must be non-empty.
void Scheduler::pop(RetiredTasks &retired) {
  std::pop_heap(queue_.begin(), queue_.end(), TimeComparator());
  if (queue_.back().owns_task()) retired.add(std::move(queue_.back()));
  queue_.pop_back();
  while (!queue_.empty() && canceled_.erase(queue_.front().id())) {
    std::pop_heap(queue_.begin(), queue_.end(), TimeComparator());
    if (queue_.back().owns_task()) retired.add(std::move(queue_.back()));
    queue_.pop_back();
  }
  // Cancellation records may still refer to ready_ entries.
}

bool Scheduler::empty() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (canceled_.empty()) {
    return queue_.empty()
#if !ROO_SCHEDULER_IGNORE_PRIORITY
           && ready_.empty()
#endif
        ;
  }
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
  RetiredTasks retired;
  Entry to_execute;
  InFlight flight(*this);
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
        retired.add(std::move(ready_.back()));
        ready_.pop_back();
        continue;
      }
      if (ready_.front().priority() < min_priority) break;
      to_execute = std::move(ready_.front());
      std::pop_heap(ready_.begin(), ready_.end(), PriorityComparator());
      ready_.pop_back();
      break;
    }
    if (queue_.empty()
#if !ROO_SCHEDULER_IGNORE_PRIORITY
        && ready_.empty()
#endif
    )
      canceled_.clear();
    if (to_execute.task() != nullptr) {
#ifndef ROO_THREADS_SINGLETHREADED
      assert(in_flight_ == nullptr ||
             in_flight_->thread == roo::this_thread::get_id());
      flight.thread = roo::this_thread::get_id();
#endif
      flight.task = to_execute.task();
      flight.previous = in_flight_;
      in_flight_ = &flight;
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
  RetiredTasks retired;
  Entry to_execute;
  InFlight flight(*this);
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
    if (queue_.empty()
#if !ROO_SCHEDULER_IGNORE_PRIORITY
        && ready_.empty()
#endif
    )
      canceled_.clear();
    if (to_execute.task() != nullptr) {
#ifndef ROO_THREADS_SINGLETHREADED
      assert(in_flight_ == nullptr ||
             in_flight_->thread == roo::this_thread::get_id());
      flight.thread = roo::this_thread::get_id();
#endif
      flight.task = to_execute.task();
      flight.previous = in_flight_;
      in_flight_ = &flight;
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
  RetiredTasks retired;
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (queue_.empty()
#if !ROO_SCHEDULER_IGNORE_PRIORITY
      && ready_.empty()
#endif
  ) {
    canceled_.clear();
    return;
  }
  if (!queue_.empty() && queue_.front().id() == id) {
    pop(retired);
    return;
  }
  canceled_.insert(id);
}

ExecutionID Scheduler::replace(ExecutionID previous, roo_time::Uptime when,
                               Executable &task, Priority priority,
                               RetiredTasks &retired) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (previous >= 0) {
    if (!queue_.empty() && queue_.front().id() == previous) {
      // Reuse the queue slot before inserting its replacement.
      pop(retired);
    } else if (!queue_.empty()
#if !ROO_SCHEDULER_IGNORE_PRIORITY
               || !ready_.empty()
#endif
    ) {
      canceled_.insert(previous);
    }
  }
  ExecutionID id = push(when, &task, false, priority);
  changed_.notify_all();
  return id;
}

void Scheduler::pruneCanceled() {
  RetiredTasks retired;
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (canceled_.empty()) return;
  auto prune = [&](std::vector<Entry> &entries, auto comparator) {
    bool modified = false;
    size_t i = 0;
    while (i < entries.size()) {
      if (canceled_.erase(entries[i].id())) {
        retired.add(std::move(entries[i]));
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

Scheduler::InFlight::~InFlight() {
  if (task == nullptr) return;
  roo::lock_guard<roo::mutex> lock(scheduler.mutex_);
  assert(scheduler.in_flight_ == this);
  scheduler.in_flight_ = previous;
  scheduler.changed_.notify_all();
}

bool Scheduler::cancelAndWait(Executable &task) {
  RetiredTasks retired;
  roo::unique_lock<roo::mutex> lock(mutex_);
  auto remove = [&](std::vector<Entry> &entries, auto comparator) {
    bool modified = false;
    for (size_t i = 0; i < entries.size();) {
      if (entries[i].task() == &task) {
        canceled_.erase(entries[i].id());
        retired.add(std::move(entries[i]));
        entries[i] = std::move(entries.back());
        entries.pop_back();
        modified = true;
      } else {
        ++i;
      }
    }
    if (modified) std::make_heap(entries.begin(), entries.end(), comparator);
  };
  remove(queue_, TimeComparator());
#if !ROO_SCHEDULER_IGNORE_PRIORITY
  remove(ready_, PriorityComparator());
#endif
  while (true) {
    InFlight *active = in_flight_;
    while (active != nullptr && active->task != &task)
      active = active->previous;
    if (active == nullptr) return true;
#ifdef ROO_THREADS_SINGLETHREADED
    return false;
#else
    if (active->thread == roo::this_thread::get_id()) return false;
#endif
    changed_.wait(lock);
  }
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
        changed_.wait_until(lock, next);
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
          changed_.wait(lock);
        } else {
          changed_.wait_for(lock, delay);
        }
      }
    }
  }
}

RepetitiveTask::RepetitiveTask(Scheduler &scheduler, roo_time::Duration delay,
                               std::function<void()> task, Priority priority)
    : scheduler_(scheduler),
      task_(std::move(task)),
      id_(kInactive),
      priority_(priority),
      delay_(delay) {}

bool RepetitiveTask::start(roo_time::Duration initial_delay) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (id_ != kInactive) return false;
  id_ = scheduler_.scheduleAfter(initial_delay, *this, priority_);
  return true;
}

bool RepetitiveTask::stop() {
  ExecutionID canceled;
  bool was_active;
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    canceled = id_;
    was_active = id_ >= 0;
    if (was_active) id_ = kInactive;
  }
  if (canceled >= 0) scheduler_.cancel(canceled);
  return was_active;
}

void RepetitiveTask::execute(ExecutionID id) {
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    if (id != id_) return;
  }
  task_();
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (id != id_) return;
  id_ = scheduler_.scheduleAfter(delay_, *this, priority_);
}

PeriodicTask::PeriodicTask(Scheduler &scheduler, roo_time::Duration period,
                           std::function<void()> task, Priority priority)
    : scheduler_(scheduler),
      task_(std::move(task)),
      id_(kInactive),
      priority_(priority),
      period_(period) {}

bool PeriodicTask::start(roo_time::Uptime when) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (id_ != kInactive) return false;
  next_ = when;
  id_ = scheduler_.scheduleOn(next_, *this, priority_);
  return true;
}

bool PeriodicTask::stop() {
  ExecutionID canceled;
  bool was_active;
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    canceled = id_;
    was_active = id_ >= 0;
    if (was_active) id_ = kInactive;
  }
  if (canceled >= 0) scheduler_.cancel(canceled);
  return was_active;
}

void PeriodicTask::execute(ExecutionID id) {
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    if (id != id_) return;
  }
  task_();
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (id != id_) return;
  next_ += period_;
  id_ = scheduler_.scheduleOn(next_, *this, priority_);
}

SingletonTask::SingletonTask(Scheduler &scheduler, std::function<void()> task)
    : scheduler_(scheduler),
      task_(std::make_shared<std::function<void()>>(std::move(task))),
      id_(kInactive) {}

bool SingletonTask::is_scheduled() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return id_ >= 0;
}

void SingletonTask::cancel() {
  ExecutionID canceled;
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    canceled = id_;
    if (id_ >= 0) id_ = kInactive;
  }
  if (canceled >= 0) scheduler_.cancel(canceled);
}

void SingletonTask::scheduleOn(roo_time::Uptime when, Priority priority) {
  Scheduler::RetiredTasks retired;
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (id_ == kShutdown) return;
  id_ = scheduler_.replace(id_, when, *this, priority, retired);
}

void SingletonTask::scheduleAfter(roo_time::Duration delay, Priority priority) {
  scheduleOn(roo_time::Uptime::Now() + delay, priority);
}

void SingletonTask::scheduleNow(Priority priority) {
  scheduleOn(roo_time::Uptime::Now(), priority);
}

void SingletonTask::execute(ExecutionID id) {
  std::shared_ptr<std::function<void()>> task;
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    if (id != id_) return;
    id_ = kInactive;
    // Keep the callable alive even if it destroys the adapter.
    task = task_;
  }
  (*task)();
}

IteratingTask::IteratingTask(Scheduler &scheduler, Iterator &iterator,
                             std::function<void()> done_cb)
    : scheduler_(scheduler),
      itr_(iterator),
      id_(kInactive),
      done_cb_(done_cb
                   ? std::make_shared<std::function<void()>>(std::move(done_cb))
                   : nullptr) {}

bool IteratingTask::start(roo_time::Uptime when) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (id_ != kInactive) return false;
  id_ = scheduler_.scheduleOn(when, *this);
  return true;
}

bool IteratingTask::is_active() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return id_ >= 0;
}

void IteratingTask::execute(ExecutionID id) {
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    if (id != id_) return;
  }
  int64_t next_delay_us = itr_.next();
  std::shared_ptr<std::function<void()>> done;
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    if (id != id_) return;
    if (next_delay_us >= 0) {
      id_ = scheduler_.scheduleAfter(roo_time::Micros(next_delay_us), *this);
    } else {
      id_ = kInactive;
      done = done_cb_;
    }
  }
  if (done && *done)
    (*done)();  // May destroy this adapter; no subsequent member access.
}

bool RepetitiveTask::is_active() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return id_ >= 0;
}

Priority RepetitiveTask::priority() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return priority_;
}

void RepetitiveTask::setPriority(Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  priority_ = priority;
}

bool PeriodicTask::is_active() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return id_ >= 0;
}

Priority PeriodicTask::priority() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return priority_;
}

void PeriodicTask::setPriority(Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  priority_ = priority;
}

bool RepetitiveTask::shutdown() {
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    id_ = kShutdown;
  }
  return scheduler_.cancelAndWait(*this);
}

bool RepetitiveTask::is_shutdown() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return id_ == kShutdown;
}

RepetitiveTask::~RepetitiveTask() {
  if (!shutdown()) std::terminate();
}

bool PeriodicTask::shutdown() {
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    id_ = kShutdown;
  }
  return scheduler_.cancelAndWait(*this);
}

bool PeriodicTask::is_shutdown() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return id_ == kShutdown;
}

PeriodicTask::~PeriodicTask() {
  if (!shutdown()) std::terminate();
}

bool SingletonTask::shutdown() {
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    id_ = kShutdown;
  }
  return scheduler_.cancelAndWait(*this);
}

bool SingletonTask::is_shutdown() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return id_ == kShutdown;
}

SingletonTask::~SingletonTask() { shutdown(); }

bool IteratingTask::shutdown() {
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
    id_ = kShutdown;
  }
  return scheduler_.cancelAndWait(*this);
}

bool IteratingTask::is_shutdown() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return id_ == kShutdown;
}

IteratingTask::~IteratingTask() { shutdown(); }

}  // namespace roo_scheduler
