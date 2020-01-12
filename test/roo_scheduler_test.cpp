#include "gtest/gtest.h"

#include "roo_scheduler.h"

static unsigned long int current_time_us = 0;

unsigned long int micros() {
  return current_time_us;
}

namespace roo_scheduler {

using namespace roo_time;

void delay(Interval interval) {
  current_time_us += interval.ToMicros();
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
  RepetitiveTask task(&scheduler,
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
  PeriodicTask task(&scheduler,
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

}  // namespace roo_scheduler
