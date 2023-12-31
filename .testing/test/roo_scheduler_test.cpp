#include "roo_scheduler.h"

#include "gtest/gtest.h"
#include "roo_time.h"

static unsigned long int current_time_us = 0;

namespace roo_time {

const Uptime Uptime::Now() { return Uptime(current_time_us); }

}  // namespace roo_time

namespace roo_scheduler {

using namespace roo_time;

void delay(Interval interval) { current_time_us += interval.inMicros(); }

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
        delay(Millis(100));
      },
      Millis(1200));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  delay(Millis(1000));
  EXPECT_EQ(Uptime::Max(), scheduler.GetNearestExecutionTime());
  EXPECT_EQ(Interval::Max(), scheduler.GetNearestExecutionDelay());
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  task.start(Millis(200));
  EXPECT_EQ(Millis(200), scheduler.GetNearestExecutionDelay());
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  delay(Millis(200));
  EXPECT_EQ(Millis(0), scheduler.GetNearestExecutionDelay());
  scheduler.executeEligibleTasks();
  EXPECT_EQ(Millis(1200), scheduler.GetNearestExecutionDelay());
  EXPECT_EQ(1, counter);
  delay(Millis(1100));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(1, counter);
  delay(Millis(100));
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
        delay(Millis(100));
      },
      Millis(1200));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  delay(Millis(1000));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  task.start(Uptime::Now() + Millis(200));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  delay(Millis(200));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(1, counter);
  delay(Millis(1100));
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
          delay(Millis(100));
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
          delay(Millis(100));
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
      delay(Millis(100));
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
    delay(Millis(100));
  });
  {
    SingletonTask task2(scheduler, [&counter] {
      ++counter;
      delay(Millis(100));
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

struct Experiment {
  int64_t micros;
  ExecutionID id;
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
  delay(Seconds(2));
  int i = 0;
  while (!scheduler.executeEligibleTasks(1)) {
    EXPECT_EQ(observed[i], expected[i]) << "at " << i;
    ++i;
  }
  ASSERT_EQ(i, 100);
}

TEST(Scheduler, LargeRandomTest) {
  Scheduler scheduler;
  std::vector<ExecutionID> observed;

  TestTask test(observed);

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
  delay(Seconds(2));
  int i = 0;
  while (!scheduler.executeEligibleTasks(1)) {
    EXPECT_EQ(observed[i], expected[i].id)
        << "at " << i << "; expected time: " << expected[i].micros;
    ++i;
  }
  ASSERT_EQ(i, 10000);
}

}  // namespace roo_scheduler
