#include "gtest/gtest.h"

#include "roo_time.h"
#include "roo_scheduler.h"

static unsigned long int current_time_us = 0;

namespace roo_time {

const Uptime Uptime::Now() { return Uptime(current_time_us); }

}  // namespace roo_time

namespace roo_scheduler {

using namespace roo_time;

void delay(Interval interval) {
  current_time_us += interval.inMicros();
}

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
  RepetitiveTask task(scheduler,
                      [&counter] {
                        ++counter;
                        delay(Millis(100));
                      },
                      Millis(1200));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  delay(Millis(1000));
  EXPECT_EQ(Uptime::Max(), scheduler.GetNextTaskTime());
  EXPECT_EQ(Interval::Max(), scheduler.GetNextTaskDelay());
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  task.start(Millis(200));
  EXPECT_EQ(Millis(200), scheduler.GetNextTaskDelay());
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, counter);
  delay(Millis(200));
  EXPECT_EQ(Millis(0), scheduler.GetNextTaskDelay());
  scheduler.executeEligibleTasks();
  EXPECT_EQ(Millis(1200), scheduler.GetNextTaskDelay());
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
  PeriodicTask task(scheduler,
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
    RepetitiveTask task(scheduler,
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
    PeriodicTask task(scheduler,
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
    SingletonTask task(scheduler,
                        [&counter] {
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
  SingletonTask task1(scheduler,
                      [&counter] {
                        ++counter;
                        delay(Millis(100));
                      });
  {
    SingletonTask task2(scheduler,
                        [&counter] {
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

}  // namespace roo_scheduler
