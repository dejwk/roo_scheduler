#include <cstdlib>
#include <new>

#include "gtest/gtest.h"
#include "roo_scheduler.h"

namespace {
thread_local bool count_allocations = false;
thread_local size_t allocations = 0;

struct CountAllocations {
  CountAllocations() {
    allocations = 0;
    count_allocations = true;
  }
  ~CountAllocations() { count_allocations = false; }
};
}  // namespace

void* operator new(std::size_t size) {
  if (count_allocations) ++allocations;
  void* ptr = std::malloc(size == 0 ? 1 : size);
  if (!ptr) throw std::bad_alloc();
  return ptr;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete[](void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { std::free(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { std::free(ptr); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  if (count_allocations) ++allocations;
  return std::malloc(size == 0 ? 1 : size);
}
void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept {
  return ::operator new(size, tag);
}
void operator delete(void* ptr, const std::nothrow_t&) noexcept {
  std::free(ptr);
}
void operator delete[](void* ptr, const std::nothrow_t&) noexcept {
  std::free(ptr);
}

namespace roo_scheduler {
using namespace roo_time;

TEST(SchedulerAllocation, WarmBorrowedDispatchDoesNotAllocate) {
  Scheduler scheduler;
  int calls = 0;
  Task task([&] { ++calls; });
  for (int i = 0; i < 8; ++i) scheduler.scheduleNow(task);
  scheduler.executeEligibleTasks();
  size_t observed;
  {
    CountAllocations count;
    for (int i = 0; i < 8; ++i) scheduler.scheduleNow(task);
    scheduler.executeEligibleTasks();
    observed = allocations;
  }
  EXPECT_EQ(observed, 0u);
  EXPECT_EQ(calls, 16);
}

TEST(SchedulerAllocation, ImmediateOwnedRetirementDoesNotAllocate) {
  Scheduler scheduler;
  auto id = scheduler.scheduleNow([] {});
  size_t observed;
  {
    CountAllocations count;
    scheduler.cancel(id);
    observed = allocations;
  }
  EXPECT_EQ(observed, 0u);
  EXPECT_TRUE(scheduler.empty());
}

TEST(SchedulerAllocation, PruningOwnedTasksDoesNotAllocate) {
  Scheduler scheduler;
  scheduler.scheduleNow([] {});
  auto id = scheduler.scheduleNow([] {});
  scheduler.cancel(id);
  size_t observed;
  {
    CountAllocations count;
    scheduler.pruneCanceled();
    observed = allocations;
  }
  EXPECT_EQ(observed, 0u);
  scheduler.executeEligibleTasks();
}

TEST(SchedulerAllocation, SingletonDispatchDoesNotAllocateAfterWarmup) {
  Scheduler scheduler;
  int calls = 0;
  SingletonTask task(scheduler, [&] { ++calls; });
  task.scheduleNow();
  scheduler.executeEligibleTasks();
  size_t observed;
  {
    CountAllocations count;
    task.scheduleNow();
    scheduler.executeEligibleTasks();
    observed = allocations;
  }
  EXPECT_EQ(observed, 0u);
  EXPECT_EQ(calls, 2);
}

TEST(SchedulerAllocation, IteratorDispatchDoesNotAllocateAfterWarmup) {
  struct Finished : IteratingTask::Iterator {
    int64_t next() override { return -1; }
  } iterator;
  Scheduler scheduler;
  int calls = 0;
  IteratingTask task(scheduler, iterator, [&] { ++calls; });
  task.start();
  scheduler.executeEligibleTasks();
  size_t observed;
  {
    CountAllocations count;
    task.start();
    scheduler.executeEligibleTasks();
    observed = allocations;
  }
  EXPECT_EQ(observed, 0u);
  EXPECT_EQ(calls, 2);
}
}  // namespace roo_scheduler

namespace roo_scheduler {
TEST(SchedulerAllocation, SingletonReplacementReusesPendingQueueSlot) {
  Scheduler scheduler;
  SingletonTask task(scheduler, [] {});
  task.scheduleAfter(roo_time::Seconds(1));
  size_t observed;
  {
    CountAllocations count;
    task.scheduleAfter(roo_time::Seconds(2));
    observed = allocations;
  }
  EXPECT_EQ(observed, 0u);
}
}  // namespace roo_scheduler
