#include "roo_scheduler.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "gtest/gtest.h"
#include "roo_time.h"

static std::atomic<long> current_time_us = 0;

namespace roo_time {

const Uptime Uptime::Now() { return Uptime(current_time_us); }

void Delay(Interval interval) { current_time_us += interval.inMicros(); }

void DelayUntil(roo_time::Uptime when) {
  if (when.inMicros() > current_time_us) {
    current_time_us = when.inMicros();
  }
}

}  // namespace roo_time

namespace roo_scheduler {

static std::atomic<bool> going = true;

void real_time() {
  const auto start = std::chrono::system_clock::now();
  long arduino_start = current_time_us;
  while (going) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    const auto elapsed = std::chrono::system_clock::now() - start;
    current_time_us =
        (arduino_start +
         (std::chrono::duration_cast<std::chrono::microseconds>(elapsed))
             .count());
  }
}

using namespace roo_time;

TEST(Scheduler, Now) {
  int counter = 0;
  Scheduler scheduler;
  Task task([&counter] { ++counter; });
  scheduler.scheduleNow(&task);
  scheduler.executeEligibleTasks();
  EXPECT_EQ(1, counter);
}

TEST(Scheduler, Now3x) {
  int counter = 0;
  Scheduler scheduler;
  Task task([&counter] { ++counter; });
  scheduler.scheduleNow(&task);
  scheduler.scheduleNow(&task);
  scheduler.scheduleNow(&task);
  scheduler.executeEligibleTasks();
  EXPECT_EQ(3, counter);
}

TEST(Scheduler, Repetitive) {
  int counter = 0;
  Scheduler scheduler;
  RepetitiveTask task(
      scheduler,
      [&counter] {
        ++counter;
        Delay(Millis(100));
      },
      Millis(1200));
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
  int counter = 0;
  Scheduler scheduler;
  PeriodicTask task(
      scheduler,
      [&counter] {
        ++counter;
        Delay(Millis(100));
      },
      Millis(1200));
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
  Scheduler scheduler;
  int counter = 0;
  {
    RepetitiveTask task(
        scheduler,
        [&counter] {
          ++counter;
          Delay(Millis(100));
        },
        Millis(1200));
    task.startInstantly();
    // Now, destroy the task.
  }
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
}

TEST(Scheduler, PeriodicImmediateDestruction) {
  Scheduler scheduler;
  int counter = 0;
  {
    PeriodicTask task(
        scheduler,
        [&counter] {
          ++counter;
          Delay(Millis(100));
        },
        Millis(1200));
    task.start();
    // Now, destroy the task.
  }
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
}

TEST(Scheduler, SingletonImmediateDestruction) {
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
  Scheduler scheduler;
  std::vector<ExecutionID> observed;
  std::vector<ExecutionID> expected;
  TestTask test(observed);

  Uptime now = Uptime::Now();
  for (int i = 0; i < 100; ++i) {
    ExecutionID id = scheduler.scheduleOn(&test, now + Micros(100));
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
  Scheduler scheduler;
  std::vector<ExecutionID> observed;
  std::vector<ExecutionID> expected;
  TestTask test(observed);

  Uptime now = Uptime::Now();
  ExecutionID id1 =
      scheduler.scheduleOn(&test, now + Micros(100), PRIORITY_BACKGROUND);
  ExecutionID id2 =
      scheduler.scheduleOn(&test, now + Micros(200), PRIORITY_REDUCED);
  ExecutionID id3 =
      scheduler.scheduleOn(&test, now + Micros(300), PRIORITY_NORMAL);
  ExecutionID id4 =
      scheduler.scheduleOn(&test, now + Micros(400), PRIORITY_ELEVATED);
  ExecutionID id5 =
      scheduler.scheduleOn(&test, now + Micros(500), PRIORITY_SENSITIVE);
  ExecutionID id6 =
      scheduler.scheduleOn(&test, now + Micros(600), PRIORITY_CRITICAL);
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
  Scheduler scheduler;
  std::vector<ExecutionID> observed;
  std::vector<ExecutionID> expected;
  TestTask test(observed);

  Uptime now = Uptime::Now();
  ExecutionID id1 =
      scheduler.scheduleOn(&test, now + Micros(100), PRIORITY_BACKGROUND);
  ExecutionID id2 =
      scheduler.scheduleOn(&test, now + Micros(200), PRIORITY_REDUCED);
  ExecutionID id3 =
      scheduler.scheduleOn(&test, now + Micros(300), PRIORITY_NORMAL);
  ExecutionID id4 =
      scheduler.scheduleOn(&test, now + Micros(400), PRIORITY_ELEVATED);
  ExecutionID id5 =
      scheduler.scheduleOn(&test, now + Micros(500), PRIORITY_SENSITIVE);
  ExecutionID id6 =
      scheduler.scheduleOn(&test, now + Micros(600), PRIORITY_CRITICAL);
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
  going = true;
  std::thread rt(&real_time);

  Scheduler scheduler;
  std::vector<ExecutionID> observed;
  std::vector<ExecutionID> expected;
  TestTask test(observed);

  Uptime now = Uptime::Now();
  Uptime trigger = now + Micros(100);
  ExecutionID id1 = scheduler.scheduleOn(&test, trigger, PRIORITY_BACKGROUND);
  ExecutionID id2 = scheduler.scheduleOn(&test, trigger, PRIORITY_REDUCED);
  ExecutionID id3 = scheduler.scheduleOn(&test, trigger, PRIORITY_NORMAL);
  ExecutionID id4 = scheduler.scheduleOn(&test, trigger, PRIORITY_ELEVATED);
  ExecutionID id5 = scheduler.scheduleOn(&test, trigger, PRIORITY_SENSITIVE);
  ExecutionID id6 = scheduler.scheduleOn(&test, trigger, PRIORITY_CRITICAL);

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

  going = false;
  rt.join();
}

TEST(Scheduler, DelayWithHeightenedPriority) {
  going = true;
  std::thread rt(&real_time);

  Scheduler scheduler;
  std::vector<ExecutionID> observed;
  std::vector<ExecutionID> expected;
  TestTask test(observed);

  Uptime now = Uptime::Now();
  Uptime trigger = now + Micros(100);
  ExecutionID id1 = scheduler.scheduleOn(&test, trigger, PRIORITY_BACKGROUND);
  ExecutionID id2 = scheduler.scheduleOn(&test, trigger, PRIORITY_REDUCED);
  ExecutionID id3 = scheduler.scheduleOn(&test, trigger, PRIORITY_NORMAL);
  ExecutionID id4 = scheduler.scheduleOn(&test, trigger, PRIORITY_ELEVATED);
  ExecutionID id5 = scheduler.scheduleOn(&test, trigger, PRIORITY_SENSITIVE);
  ExecutionID id6 = scheduler.scheduleOn(&test, trigger, PRIORITY_CRITICAL);
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

  going = false;
  rt.join();
}

TEST(Scheduler, LargeRandomTest) {
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
    ExecutionID id = scheduler.scheduleAfter(&test, Micros(micros));
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
    ExecutionID id = scheduler.scheduleAfter(&test, Micros(micros));
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
    ExecutionID id = scheduler.scheduleAfter(&test, Micros(micros));
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
