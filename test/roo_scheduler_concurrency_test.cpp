#include <atomic>
#include <memory>

#include "gtest/gtest.h"
#include "roo_scheduler.h"
#include "roo_threads/semaphore.h"

namespace roo_scheduler {
using namespace roo_time;

TEST(SchedulerConcurrency, ImmediateSingletonPublication) {
  Scheduler s;
  roo::binary_semaphore submitted(0), finished(0);
  std::atomic<int> calls{0};
  SingletonTask task(s, [&] {
    ++calls;
    finished.release();
  });
  constexpr int kRuns = 1000;
  roo::thread dispatcher([&] {
    for (int i = 0; i < kRuns; ++i) {
      submitted.acquire();
      // Scheduling and dispatch deliberately race; no adapter state is read
      // until scheduleNow has finished publishing it under the adapter mutex.
      while (!s.executeEligibleTasks()) {
      }
      while (calls.load() <= i) {
        s.executeEligibleTasks();
        roo::this_thread::yield();
      }
    }
  });
  for (int i = 0; i < kRuns; ++i) {
    submitted.release();
    task.scheduleNow();
    finished.acquire();
  }
  dispatcher.join();
  EXPECT_EQ(calls.load(), kRuns);
  EXPECT_FALSE(task.is_scheduled());
}

TEST(SchedulerConcurrency, RepetitiveRestartWinsOverRunningCallback) {
  Scheduler s;
  roo::binary_semaphore entered(0), release(0);
  int calls = 0;
  RepetitiveTask task(s, Millis(1), [&] {
    ++calls;
    entered.release();
    release.acquire();
  });
  task.startInstantly();
  roo::thread dispatcher([&] { s.executeEligibleTasks(1); });
  entered.acquire();
  EXPECT_TRUE(task.stop());
  EXPECT_TRUE(task.start(Seconds(60)));
  auto next = s.getNearestExecutionTime();
  release.release();
  dispatcher.join();
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(s.getNearestExecutionTime(), next);
  EXPECT_TRUE(task.shutdown());
  EXPECT_FALSE(task.startInstantly());
  EXPECT_TRUE(s.empty());
}

TEST(SchedulerConcurrency, PeriodicRestartKeepsNewTarget) {
  Scheduler s;
  roo::binary_semaphore entered(0), release(0);
  PeriodicTask task(s, Millis(1), [&] {
    entered.release();
    release.acquire();
  });
  task.start();
  roo::thread dispatcher([&] { s.executeEligibleTasks(1); });
  entered.acquire();
  EXPECT_TRUE(task.stop());
  auto next = Uptime::Now() + Seconds(60);
  EXPECT_TRUE(task.start(next));
  release.release();
  dispatcher.join();
  EXPECT_EQ(s.getNearestExecutionTime(), next);
}

TEST(SchedulerConcurrency, ShutdownWaitsWithoutHoldingAdapterMutex) {
  Scheduler s;
  roo::binary_semaphore entered(0), release(0);
  std::atomic<bool> finished{false};
  RepetitiveTask* ptr = nullptr;
  RepetitiveTask task(s, Millis(1), [&] {
    entered.release();
    release.acquire();
    // A shutdown holding the adapter mutex while waiting would deadlock here.
    EXPECT_FALSE(ptr->is_active());
  });
  ptr = &task;
  task.startInstantly();
  roo::thread dispatcher([&] { s.executeEligibleTasks(1); });
  entered.acquire();
  roo::thread stopper([&] {
    EXPECT_TRUE(task.shutdown());
    finished = true;
  });
  while (task.is_active()) roo::this_thread::yield();
  EXPECT_FALSE(finished.load());
  release.release();
  dispatcher.join();
  stopper.join();
  EXPECT_TRUE(finished.load());
  EXPECT_TRUE(s.empty());
}

TEST(SchedulerConcurrency,
     ClaimedSingletonIsQuiescentBeforeDestructionReturns) {
  Scheduler s;
  roo::binary_semaphore entered(0), release(0), deleting(0);
  std::atomic<bool> callback_finished{false};
  std::atomic<bool> destructor_finished{false};
  auto task = std::unique_ptr<SingletonTask>(new SingletonTask(s, [&] {
    entered.release();
    release.acquire();
    callback_finished = true;
  }));
  task->scheduleNow();
  roo::thread dispatcher([&] { s.executeEligibleTasks(1); });
  entered.acquire();
  roo::thread destroyer([&] {
    deleting.release();
    task.reset();
    EXPECT_TRUE(callback_finished.load());
    destructor_finished = true;
  });
  deleting.acquire();
  EXPECT_FALSE(destructor_finished.load());
  release.release();
  dispatcher.join();
  destroyer.join();
  EXPECT_TRUE(destructor_finished.load());
}

TEST(SchedulerConcurrency, SelfShutdownDoesNotWaitOnOuterNestedCallback) {
  Scheduler s;
  SingletonTask* outer_ptr = nullptr;
  SingletonTask outer(s, [&] {
    s.scheduleNow([&] { EXPECT_FALSE(outer_ptr->shutdown()); });
    s.executeEligibleTasks();
  });
  outer_ptr = &outer;
  outer.scheduleNow();
  s.executeEligibleTasks();
  EXPECT_TRUE(outer.shutdown());
  outer.scheduleNow();
  EXPECT_TRUE(s.empty());
}

TEST(SchedulerConcurrency, SingletonMayDeleteItselfAndKeepCaptureAlive) {
  Scheduler s;
  std::unique_ptr<SingletonTask> task;
  int observed = 0;
  task.reset(new SingletonTask(s, [&, value = std::make_shared<int>(42)] {
    task.reset();
    observed = *value;
  }));
  task->scheduleNow();
  s.executeEligibleTasks();
  EXPECT_EQ(observed, 42);
}

TEST(SchedulerConcurrency, SingletonRetainsMutableCallableState) {
  Scheduler s;
  int observed = 0;
  SingletonTask task(s, [&, count = 0]() mutable { observed = ++count; });
  task.scheduleNow();
  s.executeEligibleTasks();
  task.scheduleNow();
  s.executeEligibleTasks();
  EXPECT_EQ(observed, 2);
}

TEST(SchedulerConcurrency, IteratorCompletionCanDestroyTask) {
  struct Finished : IteratingTask::Iterator {
    int64_t next() override { return -1; }
  } iterator;
  Scheduler s;
  std::unique_ptr<IteratingTask> task;
  int observed = 0;
  task.reset(
      new IteratingTask(s, iterator, [&, value = std::make_shared<int>(42)] {
        task.reset();
        observed = *value;
      }));
  task->start();
  s.executeEligibleTasks();
  EXPECT_EQ(observed, 42);
}

TEST(SchedulerConcurrency, BorrowedCancellationWaitsForCallback) {
  Scheduler s;
  roo::binary_semaphore entered(0), release(0);
  std::atomic<bool> returned{false};
  Task task([&] {
    entered.release();
    release.acquire();
    returned = true;
  });
  s.scheduleNow(task);
  roo::thread dispatcher([&] { s.executeEligibleTasks(1); });
  entered.acquire();
  s.cancel(0);  // Non-waiting, even though this execution is already claimed.
  EXPECT_FALSE(returned.load());
  release.release();
  EXPECT_TRUE(s.cancelAndWait(task));
  EXPECT_TRUE(returned.load());
  dispatcher.join();
}
}  // namespace roo_scheduler

namespace roo_scheduler {
TEST(SchedulerConcurrency, ShutdownWaitsForClaimBeforeAdapterBodyStarts) {
  struct HeldTask : RepetitiveTask {
    HeldTask(Scheduler& scheduler, int& calls)
        : RepetitiveTask(scheduler, roo_time::Seconds(1), [&] { ++calls; }) {}
    roo::binary_semaphore claimed{0}, release{0};
    void execute(ExecutionID id) override {
      claimed.release();
      release.acquire();
      RepetitiveTask::execute(id);
    }
  };
  Scheduler scheduler;
  int calls = 0;
  HeldTask task(scheduler, calls);
  task.startInstantly();
  roo::thread dispatcher([&] { scheduler.executeEligibleTasks(1); });
  task.claimed.acquire();
  std::atomic<bool> finished{false};
  roo::thread stopper([&] {
    EXPECT_TRUE(task.shutdown());
    finished = true;
  });
  while (task.is_active()) roo::this_thread::yield();
  EXPECT_FALSE(finished.load());
  task.release.release();
  dispatcher.join();
  stopper.join();
  EXPECT_EQ(calls, 0);
}

TEST(SchedulerConcurrency, RetiredDestructorCanReenterAdapterControl) {
  Scheduler scheduler;
  SingletonTask singleton(scheduler, [] {});
  int destroyed = 0;
  struct Reentrant : Executable {
    Reentrant(SingletonTask& singleton, int& destroyed)
        : singleton(singleton), destroyed(destroyed) {}
    ~Reentrant() override {
      EXPECT_TRUE(singleton.is_scheduled());
      ++destroyed;
    }
    void execute(ExecutionID) override {}
    SingletonTask& singleton;
    int& destroyed;
  };
  singleton.scheduleNow();
  auto id = scheduler.scheduleAfter(
      roo_time::Seconds(1),
      std::unique_ptr<Executable>(new Reentrant(singleton, destroyed)));
  scheduler.cancel(id);
  EXPECT_EQ(destroyed, 0);
  // Canceling the old head also retires the canceled owned task behind it.
  singleton.scheduleAfter(roo_time::Seconds(2));
  EXPECT_EQ(destroyed, 1);
}
}  // namespace roo_scheduler

namespace roo_scheduler {
TEST(SchedulerConcurrency, StopAndCancelCannotUndoPermanentShutdown) {
  Scheduler scheduler;
  RepetitiveTask repetitive(scheduler, roo_time::Seconds(1), [] {});
  EXPECT_TRUE(repetitive.shutdown());
  EXPECT_FALSE(repetitive.stop());
  EXPECT_FALSE(repetitive.startInstantly());
  EXPECT_TRUE(repetitive.is_shutdown());

  PeriodicTask periodic(scheduler, roo_time::Seconds(1), [] {});
  EXPECT_TRUE(periodic.shutdown());
  EXPECT_FALSE(periodic.stop());
  EXPECT_FALSE(periodic.start());
  EXPECT_TRUE(periodic.is_shutdown());

  SingletonTask singleton(scheduler, [] {});
  EXPECT_TRUE(singleton.shutdown());
  singleton.cancel();
  singleton.scheduleNow();
  EXPECT_TRUE(singleton.is_shutdown());
  EXPECT_FALSE(singleton.is_scheduled());

  struct Finished : IteratingTask::Iterator {
    int64_t next() override { return -1; }
  } iterator;
  IteratingTask iterating(scheduler, iterator);
  EXPECT_TRUE(iterating.shutdown());
  EXPECT_FALSE(iterating.start());
  EXPECT_TRUE(iterating.is_shutdown());
  EXPECT_TRUE(scheduler.empty());
}
}  // namespace roo_scheduler

namespace roo_scheduler {
TEST(SchedulerConcurrency, SchedulerDestructionKeepsSynchronizationAlive) {
  int retired = 0;
  struct Reentrant : Executable {
    Reentrant(Scheduler& scheduler, int& retired)
        : scheduler(scheduler), retired(retired) {}
    ~Reentrant() override {
      EXPECT_TRUE(scheduler.empty());
      // The scheduler must also reclaim work submitted during teardown.
      scheduler.scheduleNow([] {});
      ++retired;
    }
    void execute(ExecutionID) override {}
    Scheduler& scheduler;
    int& retired;
  };
  {
    Scheduler scheduler;
    scheduler.scheduleNow(
        std::unique_ptr<Executable>(new Reentrant(scheduler, retired)));
  }
  EXPECT_EQ(retired, 1);
}
}  // namespace roo_scheduler
