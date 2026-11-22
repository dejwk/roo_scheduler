#include "roo_scheduler.h"

#include <algorithm>
#include <cassert>
#include <exception>

// Emulation identifies the runtime independently of the chip and SDK.
#if !ROO_THREADS_HAS_ISR_SUPPORT && defined(ROO_THREADS_SINGLETHREADED) && \
    !defined(__FREERTOS) &&                                                \
    !(defined(LIB_FREERTOS_KERNEL) && LIB_FREERTOS_KERNEL)
#if defined(ROO_TESTING)
#if defined(ROO_TESTING_RUNTIME_BARE_METAL) &&                           \
    ROO_TESTING_RUNTIME_BARE_METAL && defined(ROO_TESTING_SOC_RP2040) && \
    ROO_TESTING_SOC_RP2040 && defined(PICO_BUILD) && PICO_BUILD
#define ROO_SCHEDULER_BARE_METAL_PICO
#endif
#elif defined(PICO_ON_DEVICE) && PICO_ON_DEVICE && \
    (defined(ARDUINO_ARCH_RP2040) ||               \
     (defined(ROO_THREADS_PICO_SDK_BARE_METAL) &&  \
      ROO_THREADS_PICO_SDK_BARE_METAL))
#define ROO_SCHEDULER_BARE_METAL_PICO
#endif
#endif

#ifdef ROO_SCHEDULER_BARE_METAL_PICO
#include <pico/time.h>
// SDK 2.0.0 could clear a pending event before parking in a timed wait.
#if defined(PICO_RP2040) && PICO_RP2040 &&                            \
    (PICO_SDK_VERSION_MAJOR > 2 ||                                    \
     (PICO_SDK_VERSION_MAJOR == 2 && PICO_SDK_VERSION_MINOR >= 1)) && \
    !PICO_TIME_DEFAULT_ALARM_POOL_DISABLED
#include <hardware/sync.h>
#define ROO_SCHEDULER_PICO_EVENT_WAIT
#endif
#endif

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
    for (auto& entry : queue_) retired.add(std::move(entry));
    queue_.clear();
#if !ROO_SCHEDULER_IGNORE_PRIORITY
    for (auto& entry : ready_) retired.add(std::move(entry));
    ready_.clear();
#endif
    canceled_.clear();
  }
}

ExecutionID Scheduler::scheduleOn(roo_time::Uptime when, Executable& task,
                                  Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return push(when, &task, false, priority);
}

ExecutionID Scheduler::scheduleOn(roo_time::Uptime when,
                                  std::unique_ptr<Executable> task,
                                  Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return push(when, task.release(), true, priority);
}

ExecutionID Scheduler::scheduleOn(roo_time::Uptime when,
                                  std::function<void()> task,
                                  Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return push(when, new Task(std::move(task)), true, priority);
}

ExecutionID Scheduler::scheduleAfter(roo_time::Duration delay, Executable& task,
                                     Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return push(roo_time::Uptime::Now() + delay, &task, false, priority);
}

ExecutionID Scheduler::scheduleAfter(roo_time::Duration delay,
                                     std::unique_ptr<Executable> task,
                                     Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return push(roo_time::Uptime::Now() + delay, task.release(), true, priority);
}

ExecutionID Scheduler::scheduleAfter(roo_time::Duration delay,
                                     std::function<void()> task,
                                     Priority priority) {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return push(roo_time::Uptime::Now() + delay, new Task(std::move(task)), true,
              priority);
}

ExecutionID Scheduler::nextExecutionId() {
  const ExecutionID id = next_execution_id_;
  // Reserve negative IDs without overflowing signed arithmetic.
  next_execution_id_ = (static_cast<uint32_t>(id) + 1) & 0x7FFFFFFF;
  return id;
}

ExecutionID Scheduler::push(roo_time::Uptime when, Executable* task,
                            bool owns_task, Priority priority) {
  const ExecutionID id = nextExecutionId();
  queue_.emplace_back(id, task, owns_task, when, priority);
  std::push_heap(queue_.begin(), queue_.end(), TimeComparator());
#if ROO_THREADS_HAS_ISR_SUPPORT
  work_event_.signal();
#else
  changed_.notify_all();
#ifdef ROO_SCHEDULER_PICO_EVENT_WAIT
  // Cooperative callbacks can post while the dispatcher is in a Pico wait.
  __sev();
#endif
#endif
  return id;
}

#if ROO_THREADS_HAS_ISR_SUPPORT
bool Scheduler::tryScheduleNowFromIsr(Executable& task,
                                      Priority priority) noexcept {
  {
    using Guard = roo_threads::internal::IsrLockGuard;
    Guard lock(isr_lock_, Guard::Context::kIsr);
    if (free_isr_slots_ == 0) return false;
    const unsigned index =
        __builtin_ctz(static_cast<unsigned>(free_isr_slots_));
    free_isr_slots_ &= ~(1u << index);
    Entry& slot = isr_slots_[index];
    slot.task_ = &task;
#if !ROO_SCHEDULER_IGNORE_PRIORITY
    slot.priority_ = priority;
#else
    (void)priority;
#endif
    posted_isr_slots_[posted_isr_count_++] = index;
  }
  work_event_.signal_from_isr();
  return true;
}

bool Scheduler::hasIsrWork() const {
  roo_threads::internal::IsrLockGuard lock(isr_lock_);
  return free_isr_slots_ != kAllIsrSlots;
}

roo_time::Uptime Scheduler::importIsrSlots() {
  uint8_t posted[kIsrCapacity];
  unsigned count;
  {
    roo_threads::internal::IsrLockGuard lock(isr_lock_);
    count = posted_isr_count_;
    std::copy_n(posted_isr_slots_, count, posted);
    posted_isr_count_ = 0;
  }

  const roo_time::Uptime now = roo_time::Uptime::Now();
  for (unsigned i = 0; i < count; ++i) {
    const unsigned index = posted[i];
    Entry& slot = isr_slots_[index];
    slot.id_ = nextExecutionId();
    slot.when_ = now;
    imported_isr_slots_ |= 1u << index;
  }
  return now;
}

Scheduler::Entry* Scheduler::findIsrCandidate(roo_time::Uptime cutoff) {
#if ROO_SCHEDULER_IGNORE_PRIORITY
  const TimeComparator compare;
#else
  const PriorityComparator compare;
#endif
  Entry* candidate = nullptr;
  for (unsigned i = 0; i < kIsrCapacity; ++i) {
    const unsigned bit = 1u << i;
    if ((imported_isr_slots_ & bit) == 0) continue;
    Entry& slot = isr_slots_[i];
#if ROO_SCHEDULER_IGNORE_PRIORITY
    if (slot.when() > cutoff) continue;
#else
    if (slot.when() <= cutoff) ready_isr_slots_ |= bit;
    if ((ready_isr_slots_ & bit) == 0) continue;
#endif
    if (candidate == nullptr || compare(*candidate, slot)) candidate = &slot;
  }
  return candidate;
}

void Scheduler::releaseIsrSlot(Entry& slot) {
  const unsigned bit = 1u << (&slot - isr_slots_);
  imported_isr_slots_ &= ~bit;
#if !ROO_SCHEDULER_IGNORE_PRIORITY
  ready_isr_slots_ &= ~bit;
#endif
  roo_threads::internal::IsrLockGuard lock(isr_lock_);
  free_isr_slots_ |= bit;
}

void Scheduler::removeIsrSlots(Executable& task) {
  roo_threads::internal::IsrLockGuard lock(isr_lock_);
  unsigned removed = 0;
  for (unsigned i = 0; i < kIsrCapacity; ++i) {
    const unsigned bit = 1u << i;
    if ((free_isr_slots_ & bit) == 0 && isr_slots_[i].task() == &task) {
      removed |= bit;
    }
  }
  unsigned kept = 0;
  for (unsigned i = 0; i < posted_isr_count_; ++i) {
    const unsigned index = posted_isr_slots_[i];
    if ((removed & (1u << index)) == 0) posted_isr_slots_[kept++] = index;
  }
  posted_isr_count_ = kept;
  imported_isr_slots_ &= ~removed;
#if !ROO_SCHEDULER_IGNORE_PRIORITY
  ready_isr_slots_ &= ~removed;
#endif
  free_isr_slots_ |= removed;
}
#endif

Scheduler::RetiredTasks::~RetiredTasks() {
  while (head_ != nullptr) {
    Executable* task = head_;
    head_ = task->retired_next_;
    delete task;
  }
}

void Scheduler::RetiredTasks::add(Entry&& entry) {
  Executable* task = entry.releaseOwnedTask();
  if (task == nullptr) return;
  task->retired_next_ = head_;
  head_ = task;
}

// The queue must be non-empty.
void Scheduler::pop(RetiredTasks& retired) {
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

bool SchedulingService::empty() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
#if ROO_THREADS_HAS_ISR_SUPPORT
  if (hasIsrWork()) return false;
#endif
  if (canceled_.empty()) {
    return queue_.empty()
#if !ROO_SCHEDULER_IGNORE_PRIORITY
           && ready_.empty()
#endif
        ;
  }
  for (const auto& entry : queue_) {
    if (!canceled_.contains(entry.id())) return false;
  }
#if !ROO_SCHEDULER_IGNORE_PRIORITY
  for (const auto& entry : ready_) {
    if (!canceled_.contains(entry.id())) return false;
  }
#endif
  return true;
}

bool SchedulingService::executeEligibleTasksUpToNow(Priority min_priority,
                                                    int max_tasks) {
  return Scheduler::executeEligibleTasks(
      {roo_time::Uptime::Max(), min_priority, DispatchMode::kCaptureCutoff},
      max_tasks);
}

bool SchedulingService::executeEligibleTasksUpTo(roo_time::Uptime deadline,
                                                 Priority min_priority,
                                                 int max_tasks) {
  return Scheduler::executeEligibleTasks(
      {deadline, min_priority, DispatchMode::kFixed}, max_tasks);
}

bool SchedulingService::executeEligibleTasks(Priority min_priority,
                                             int max_tasks) {
  return Scheduler::executeEligibleTasks(
      {roo_time::Uptime::Max(), min_priority, DispatchMode::kFixed}, max_tasks);
}

bool Scheduler::executeEligibleTasks(DispatchSelection selection,
                                     int max_tasks) {
  while (max_tasks < 0 || max_tasks-- > 0) {
    if (!runOneEligibleExecution(selection)) return true;
  }
  return false;
}

roo_time::Uptime SchedulingService::getNearestExecutionTime() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return getNearestExecutionTimeWithLockHeld();
}

roo_time::Uptime Scheduler::getNearestExecutionTimeWithLockHeld() const {
#if ROO_THREADS_HAS_ISR_SUPPORT
  if (hasIsrWork()) return roo_time::Uptime::Now();
#endif
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

roo_time::Duration SchedulingService::getNearestExecutionDelay() const {
  roo::lock_guard<roo::mutex> lock(mutex_);
  return getNearestExecutionDelayWithLockHeld();
}

roo_time::Duration Scheduler::getNearestExecutionDelayWithLockHeld() const {
#if ROO_THREADS_HAS_ISR_SUPPORT
  if (hasIsrWork()) return roo_time::Duration();
#endif
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

bool Scheduler::runOneEligibleExecution(DispatchSelection& selection) {
  RetiredTasks retired;
  Entry to_execute;
  InFlight flight(*this);
  {
    roo::lock_guard<roo::mutex> lock(mutex_);
#if ROO_THREADS_HAS_ISR_SUPPORT
    const roo_time::Uptime now = importIsrSlots();
#else
    const roo_time::Uptime now = roo_time::Uptime::Now();
#endif
    Priority min_priority = selection.min_priority;
    if (selection.mode == DispatchMode::kCaptureCutoff) {
      selection.cutoff = now;
      selection.mode = DispatchMode::kFixed;
    } else if (selection.mode == DispatchMode::kDelay) {
      if (now < selection.cutoff) {
        min_priority = Priority::kMinimum;
      } else {
        selection.mode = DispatchMode::kFixed;
      }
    }
    const roo_time::Uptime cutoff = std::min(selection.cutoff, now);

    Entry* candidate = nullptr;
#if !ROO_SCHEDULER_IGNORE_PRIORITY
    while (!queue_.empty() && queue_.front().when() <= cutoff) {
      ready_.push_back(std::move(queue_.front()));
      std::push_heap(ready_.begin(), ready_.end(), PriorityComparator());
      pop(retired);
    }
    // Previously admitted work remains eligible with an earlier cutoff.
    while (!ready_.empty() && canceled_.erase(ready_.front().id())) {
      std::pop_heap(ready_.begin(), ready_.end(), PriorityComparator());
      retired.add(std::move(ready_.back()));
      ready_.pop_back();
    }
    if (!ready_.empty()) candidate = &ready_.front();
#else
    while (!queue_.empty() && queue_.front().when() <= cutoff &&
           (canceled_.erase(queue_.front().id()) ||
            (min_priority <= Priority::kNormal &&
             queue_.front().task() == nullptr))) {
      pop(retired);
    }
    if (!queue_.empty() && queue_.front().when() <= cutoff) {
      candidate = &queue_.front();
    }
#endif

#if ROO_THREADS_HAS_ISR_SUPPORT
    Entry* isr_candidate = findIsrCandidate(cutoff);
#if ROO_SCHEDULER_IGNORE_PRIORITY
    const TimeComparator compare;
#else
    const PriorityComparator compare;
#endif
    if (isr_candidate != nullptr &&
        (candidate == nullptr || compare(*candidate, *isr_candidate))) {
      candidate = isr_candidate;
    }
#endif

    if (candidate != nullptr && candidate->priority() >= min_priority) {
      to_execute = std::move(*candidate);
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

#if ROO_THREADS_HAS_ISR_SUPPORT
      if (candidate == isr_candidate) {
        releaseIsrSlot(*isr_candidate);
      } else
#endif
      {
#if ROO_SCHEDULER_IGNORE_PRIORITY
        pop(retired);
#else
        std::pop_heap(ready_.begin(), ready_.end(), PriorityComparator());
        ready_.pop_back();
#endif
      }
    }

    if (queue_.empty()
#if !ROO_SCHEDULER_IGNORE_PRIORITY
        && ready_.empty()
#endif
    ) {
      canceled_.clear();
    }
  }
  if (to_execute.task() == nullptr) return false;
  to_execute.task()->execute(to_execute.id());
  return true;
}

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
                               Executable& task, Priority priority,
                               RetiredTasks& retired) {
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
  return push(when, &task, false, priority);
}

void SchedulingService::pruneCanceled() {
  RetiredTasks retired;
  roo::lock_guard<roo::mutex> lock(mutex_);
  if (canceled_.empty()) return;
  auto prune = [&](std::vector<Entry>& entries, auto comparator) {
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

bool Scheduler::cancelAndWait(Executable& task) {
  RetiredTasks retired;
  roo::unique_lock<roo::mutex> lock(mutex_);
  auto remove = [&](std::vector<Entry>& entries, auto comparator) {
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
#if ROO_THREADS_HAS_ISR_SUPPORT
  removeIsrSlots(task);
#endif
  while (true) {
    InFlight* active = in_flight_;
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

void SchedulingService::delay(roo_time::Duration delay, Priority min_priority) {
  delayUntil(roo_time::Uptime::Now() + delay, min_priority);
}

void SchedulingService::delayUntil(roo_time::Uptime deadline,
                                   Priority min_priority) {
  DispatchSelection selection{deadline, min_priority, DispatchMode::kDelay};
  while (true) {
    if (runOneEligibleExecution(selection)) continue;
    if (selection.mode == DispatchMode::kFixed) return;
    waitUntilNextWork(deadline);
  }
}

void SchedulingService::waitUntilNextWork(roo_time::Uptime deadline) {
#if ROO_THREADS_HAS_ISR_SUPPORT
  const roo_time::Uptime next = std::min(deadline, getNearestExecutionTime());
  if (next > roo_time::Uptime::Now()) work_event_.wait_until(next);
#else
  roo::unique_lock<roo::mutex> lock(mutex_);
  const auto next = std::min(deadline, getNearestExecutionTimeWithLockHeld());
  if (next > roo_time::Uptime::Now()) changed_.wait_until(lock, next);
#endif
}

void SchedulingService::waitForWork() {
#if ROO_THREADS_HAS_ISR_SUPPORT
  waitUntilNextWork(roo_time::Uptime::Max());
#elif defined(ROO_SCHEDULER_PICO_EVENT_WAIT)
  const roo_time::Uptime next = getNearestExecutionTime();
  if (next == roo_time::Uptime::Max()) {
    __wfe();
  } else if (next > roo_time::Uptime::Now()) {
    best_effort_wfe_or_timeout(
        from_us_since_boot(static_cast<uint64_t>(next.inMicros())));
  }
#else
  roo::unique_lock<roo::mutex> lock(mutex_);
  const roo_time::Duration delay = getNearestExecutionDelayWithLockHeld();
  if (delay <= roo_time::Duration()) return;
  if (delay == roo_time::Duration::Max()) {
#ifdef ROO_THREADS_SINGLETHREADED
    // Give the platform's cooperative event pump time to submit work.
    changed_.wait_for(lock, roo_time::Millis(1));
#else
    changed_.wait(lock);
#endif
  } else {
    changed_.wait_for(lock, delay);
  }
#endif
}

void SchedulingService::run() {
  while (true) {
    executeEligibleTasks();
    waitForWork();
  }
}

RepetitiveTask::RepetitiveTask(Scheduler& scheduler, roo_time::Duration delay,
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

PeriodicTask::PeriodicTask(Scheduler& scheduler, roo_time::Duration period,
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

SingletonTask::SingletonTask(Scheduler& scheduler, std::function<void()> task)
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

IteratingTask::IteratingTask(Scheduler& scheduler, Iterator& iterator,
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
