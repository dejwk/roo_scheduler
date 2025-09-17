#include "roo_scheduler.h"

#include <atomic>
#include <chrono>

#include "gtest/gtest.h"
#include "roo_testing/system/timer.h"
#include "roo_time.h"

namespace roo_scheduler {

using namespace roo_time;

TEST(Scheduler, Now) {
  system_time_set_auto_sync(false);
  int counter = 0;
  Scheduler scheduler;
  Task task([&counter] { ++counter; });
  scheduler.scheduleNow(task);
  scheduler.executeEligibleTasks();
  EXPECT_EQ(1, counter);
}

TEST(Scheduler, Now3x) {
  system_time_set_auto_sync(false);
  int counter = 0;
  Scheduler scheduler;
  Task task([&counter] { ++counter; });
  scheduler.scheduleNow(task);
  scheduler.scheduleNow(task);
  scheduler.scheduleNow(task);
  scheduler.executeEligibleTasks();
  EXPECT_EQ(3, counter);
}

TEST(Scheduler, Repetitive) {
  system_time_set_auto_sync(false);
  int counter = 0;
  Scheduler scheduler;
  RepetitiveTask task(scheduler, Millis(1200), [&counter] {
    ++counter;
    Delay(Millis(100));
  });
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  Delay(Millis(1000));
  EXPECT_EQ(Uptime::Max(), scheduler.getNearestExecutionTime());
  EXPECT_EQ(Interval::Max(), scheduler.getNearestExecutionDelay());
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  task.start(Millis(200));
  EXPECT_EQ(Millis(200), scheduler.getNearestExecutionDelay());
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  Delay(Millis(200));
  EXPECT_EQ(Millis(0), scheduler.getNearestExecutionDelay());
  scheduler.executeEligibleTasks();
  EXPECT_EQ(Millis(1200), scheduler.getNearestExecutionDelay());
  EXPECT_EQ(1, counter);
  Delay(Millis(1100));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(1, counter);
  Delay(Millis(100));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(2, counter);
}

TEST(Scheduler, Periodic) {
  system_time_set_auto_sync(false);
  int counter = 0;
  Scheduler scheduler;
  PeriodicTask task(scheduler, Millis(1200), [&counter] {
    ++counter;
    Delay(Millis(100));
  });
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  Delay(Millis(1000));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  task.start(Uptime::Now() + Millis(200));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  Delay(Millis(200));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(1, counter);
  Delay(Millis(1100));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(2, counter);
}

TEST(Scheduler, RepetitiveImmediateDestruction) {
  system_time_set_auto_sync(false);
  Scheduler scheduler;
  int counter = 0;
  {
    RepetitiveTask task(scheduler, Millis(1200), [&counter] {
      ++counter;
      Delay(Millis(100));
    });
    task.startInstantly();
    // Now, destroy the task.
  }
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
}

TEST(Scheduler, PeriodicImmediateDestruction) {
  system_time_set_auto_sync(false);
  Scheduler scheduler;
  int counter = 0;
  {
    PeriodicTask task(scheduler, Millis(1200), [&counter] {
      ++counter;
      Delay(Millis(100));
    });
    task.start();
    // Now, destroy the task.
  }
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
}

TEST(Scheduler, SingletonImmediateDestruction) {
  system_time_set_auto_sync(false);
  Scheduler scheduler;
  int counter = 0;
  {
    SingletonTask task(scheduler, [&counter] {
      ++counter;
      Delay(Millis(100));
    });
    task.scheduleNow();
    // Now, destroy the task.
  }
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
}

TEST(Scheduler, SingletonNonImmediateDestruction) {
  system_time_set_auto_sync(false);
  Scheduler scheduler;
  int counter = 0;
  SingletonTask task1(scheduler, [&counter] {
    ++counter;
    Delay(Millis(100));
  });
  {
    SingletonTask task2(scheduler, [&counter] {
      ++counter;
      Delay(Millis(100));
    });
    task1.scheduleNow();
    task2.scheduleNow();
    // Now, destroy task1.
  }
  scheduler.executeEligibleTasks();
  EXPECT_EQ(1, counter);
}

struct TestTask : public Executable {
  TestTask(std::vector<ExecutionID>& observed) : observed(observed) {}
  void execute(ExecutionID id) { observed.push_back(id); }
  std::vector<ExecutionID>& observed;
};

TEST(Scheduler, StableScheduleOrder) {
  system_time_set_auto_sync(false);
  Scheduler scheduler;
  std::vector<ExecutionID> observed;
  std::vector<ExecutionID> expected;
  TestTask test(observed);

  Uptime now = Uptime::Now();
  for (int i = 0; i < 100; ++i) {
    ExecutionID id = scheduler.scheduleOn(now + Micros(100), test);
    expected.push_back(id);
  }
  Delay(Seconds(2));
  int i = 0;
  while (!scheduler.executeEligibleTasks(1)) {
    EXPECT_EQ(observed[i], expected[i]) << "at " << i;
    ++i;
  }
  ASSERT_EQ(i, 100);
}

TEST(Scheduler, PriorityNoEffectWhenNotBackedUp) {
  system_time_set_auto_sync(false);
  Scheduler scheduler;
  std::vector<ExecutionID> observed;
  std::vector<ExecutionID> expected;
  TestTask test(observed);

  Uptime now = Uptime::Now();
  ExecutionID id1 =
      scheduler.scheduleOn(now + Micros(100), test, PRIORITY_BACKGROUND);
  ExecutionID id2 =
      scheduler.scheduleOn(now + Micros(200), test, PRIORITY_REDUCED);
  ExecutionID id3 =
      scheduler.scheduleOn(now + Micros(300), test, PRIORITY_NORMAL);
  ExecutionID id4 =
      scheduler.scheduleOn(now + Micros(400), test, PRIORITY_ELEVATED);
  ExecutionID id5 =
      scheduler.scheduleOn(now + Micros(500), test, PRIORITY_SENSITIVE);
  ExecutionID id6 =
      scheduler.scheduleOn(now + Micros(600), test, PRIORITY_CRITICAL);
  expected.push_back(id1);
  expected.push_back(id2);
  expected.push_back(id3);
  expected.push_back(id4);
  expected.push_back(id5);
  expected.push_back(id6);
  Delay(Micros(100));
  EXPECT_TRUE(scheduler.executeEligibleTasks());
  Delay(Micros(100));
  EXPECT_TRUE(scheduler.executeEligibleTasks());
  Delay(Micros(100));
  EXPECT_TRUE(scheduler.executeEligibleTasks());
  Delay(Micros(100));
  EXPECT_TRUE(scheduler.executeEligibleTasks());
  Delay(Micros(100));
  EXPECT_TRUE(scheduler.executeEligibleTasks());
  Delay(Micros(100));
  EXPECT_TRUE(scheduler.executeEligibleTasks());

  EXPECT_EQ(observed, expected);
}

TEST(Scheduler, PriorityAppliedWhenBackedUp) {
  system_time_set_auto_sync(false);
  Scheduler scheduler;
  std::vector<ExecutionID> observed;
  std::vector<ExecutionID> expected;
  TestTask test(observed);

  Uptime now = Uptime::Now();
  ExecutionID id1 =
      scheduler.scheduleOn(now + Micros(100), test, PRIORITY_BACKGROUND);
  ExecutionID id2 =
      scheduler.scheduleOn(now + Micros(200), test, PRIORITY_REDUCED);
  ExecutionID id3 =
      scheduler.scheduleOn(now + Micros(300), test, PRIORITY_NORMAL);
  ExecutionID id4 =
      scheduler.scheduleOn(now + Micros(400), test, PRIORITY_ELEVATED);
  ExecutionID id5 =
      scheduler.scheduleOn(now + Micros(500), test, PRIORITY_SENSITIVE);
  ExecutionID id6 =
      scheduler.scheduleOn(now + Micros(600), test, PRIORITY_CRITICAL);
  expected.push_back(id6);
  expected.push_back(id5);
  expected.push_back(id4);
  expected.push_back(id3);
  expected.push_back(id2);
  expected.push_back(id1);
  Delay(Micros(1000));
  EXPECT_TRUE(scheduler.executeEligibleTasks());
  EXPECT_EQ(observed, expected);
}

TEST(Scheduler, DelayWithNormalPriority) {
  system_time_set_auto_sync(true);
  Scheduler scheduler;
  std::vector<ExecutionID> observed;
  std::vector<ExecutionID> expected;
  TestTask test(observed);

  Uptime now = Uptime::Now();
  Uptime trigger = now + Micros(100);
  ExecutionID id1 = scheduler.scheduleOn(trigger, test, PRIORITY_BACKGROUND);
  ExecutionID id2 = scheduler.scheduleOn(trigger, test, PRIORITY_REDUCED);
  ExecutionID id3 = scheduler.scheduleOn(trigger, test, PRIORITY_NORMAL);
  ExecutionID id4 = scheduler.scheduleOn(trigger, test, PRIORITY_ELEVATED);
  ExecutionID id5 = scheduler.scheduleOn(trigger, test, PRIORITY_SENSITIVE);
  ExecutionID id6 = scheduler.scheduleOn(trigger, test, PRIORITY_CRITICAL);

  scheduler.delayUntil(now + Micros(50));
  EXPECT_EQ(observed, expected);
  expected.push_back(id6);
  expected.push_back(id5);
  expected.push_back(id4);
  expected.push_back(id3);
  scheduler.delayUntil(now + Micros(100));
  EXPECT_EQ(observed, expected);
  expected.push_back(id2);
  expected.push_back(id1);
  scheduler.delayUntil(now + Micros(2000));
  EXPECT_EQ(observed, expected);
}

TEST(Scheduler, DelayWithHeightenedPriority) {
  system_time_set_auto_sync(true);
  Scheduler scheduler;
  std::vector<ExecutionID> observed;
  std::vector<ExecutionID> expected;
  TestTask test(observed);

  Uptime now = Uptime::Now();
  Uptime trigger = now + Micros(100);
  ExecutionID id1 = scheduler.scheduleOn(trigger, test, PRIORITY_BACKGROUND);
  ExecutionID id2 = scheduler.scheduleOn(trigger, test, PRIORITY_REDUCED);
  ExecutionID id3 = scheduler.scheduleOn(trigger, test, PRIORITY_NORMAL);
  ExecutionID id4 = scheduler.scheduleOn(trigger, test, PRIORITY_ELEVATED);
  ExecutionID id5 = scheduler.scheduleOn(trigger, test, PRIORITY_SENSITIVE);
  ExecutionID id6 = scheduler.scheduleOn(trigger, test, PRIORITY_CRITICAL);
  scheduler.delayUntil(now + Micros(50), PRIORITY_SENSITIVE);
  EXPECT_EQ(observed, expected);
  expected.push_back(id6);
  expected.push_back(id5);
  scheduler.delayUntil(now + Micros(100), PRIORITY_SENSITIVE);
  EXPECT_EQ(observed, expected);
  expected.push_back(id4);
  expected.push_back(id3);
  expected.push_back(id2);
  expected.push_back(id1);
  scheduler.delayUntil(now + Micros(2000));
  EXPECT_EQ(observed, expected);
}

TEST(Scheduler, LargeRandomTest) {
  system_time_set_auto_sync(false);
  Scheduler scheduler;
  std::vector<ExecutionID> observed;

  TestTask test(observed);

  struct Experiment {
    int64_t micros;
    ExecutionID id;
  };
  std::vector<Experiment> expected;

  for (int i = 0; i < 10000; ++i) {
    int64_t micros = rand() % 2000000;
    ExecutionID id = scheduler.scheduleAfter(Micros(micros), test);
    expected.push_back(Experiment{.micros = micros, .id = id});
  }
  std::sort(expected.begin(), expected.end(),
            [](const Experiment& a, const Experiment& b) {
              return a.micros < b.micros ||
                     (a.micros == b.micros && a.id < b.id);
            });
  Delay(Seconds(2));
  int i = 0;
  while (!scheduler.executeEligibleTasks(1)) {
    EXPECT_EQ(observed[i], expected[i].id)
        << "at " << i << "; expected time: " << expected[i].micros;
    ++i;
  }
  ASSERT_EQ(i, 10000);
}

TEST(Scheduler, LargeRandomCancellationTest) {
  system_time_set_auto_sync(false);
  Scheduler scheduler;
  std::vector<ExecutionID> observed;

  TestTask test(observed);

  struct Experiment {
    int64_t micros;
    ExecutionID id;
    bool cancelled;
  };
  std::vector<Experiment> expected;

  for (int i = 0; i < 10000; ++i) {
    int64_t micros = rand() % 2000000;
    ExecutionID id = scheduler.scheduleAfter(Micros(micros), test);
    expected.push_back(
        Experiment{.micros = micros, .id = id, .cancelled = false});
  }
  for (int i = 0; i < 2000; ++i) {
    int64_t pos = rand() % 10000;
    scheduler.cancel(expected[pos].id);
    expected[pos].cancelled = true;
  }
  expected.erase(
      std::remove_if(expected.begin(), expected.end(),
                     [](const Experiment& e) { return e.cancelled; }),
      expected.end());

  std::sort(expected.begin(), expected.end(),
            [](const Experiment& a, const Experiment& b) {
              return a.micros < b.micros ||
                     (a.micros == b.micros && a.id < b.id);
            });
  Delay(Seconds(2));
  int i = 0;
  while (!scheduler.executeEligibleTasks(1)) {
    EXPECT_EQ(observed[i], expected[i].id)
        << "at " << i << "; expected time: " << expected[i].micros;
    ++i;
  }
  ASSERT_EQ(i, expected.size());
}

TEST(Scheduler, LargeRandomCancellationTestWithPruning) {
  system_time_set_auto_sync(false);
  Scheduler scheduler;
  std::vector<ExecutionID> observed;

  TestTask test(observed);

  struct Experiment {
    int64_t micros;
    ExecutionID id;
    bool cancelled;
  };
  std::vector<Experiment> expected;

  for (int i = 0; i < 10000; ++i) {
    int64_t micros = rand() % 2000000;
    ExecutionID id = scheduler.scheduleAfter(Micros(micros), test);
    expected.push_back(
        Experiment{.micros = micros, .id = id, .cancelled = false});
  }
  for (int i = 0; i < 1000; ++i) {
    int64_t pos = rand() % 10000;
    scheduler.cancel(expected[pos].id);
    expected[pos].cancelled = true;
  }
  scheduler.pruneCanceled();
  for (int i = 0; i < 1000; ++i) {
    int64_t pos = rand() % 10000;
    scheduler.cancel(expected[pos].id);
    expected[pos].cancelled = true;
  }
  expected.erase(
      std::remove_if(expected.begin(), expected.end(),
                     [](const Experiment& e) { return e.cancelled; }),
      expected.end());

  std::sort(expected.begin(), expected.end(),
            [](const Experiment& a, const Experiment& b) {
              return a.micros < b.micros ||
                     (a.micros == b.micros && a.id < b.id);
            });
  Delay(Seconds(2));
  int i = 0;
  while (!scheduler.executeEligibleTasks(1)) {
    EXPECT_EQ(observed[i], expected[i].id)
        << "at " << i << "; expected time: " << expected[i].micros;
    ++i;
  }
  ASSERT_EQ(i, expected.size());
}

}  // namespace roo_scheduler
