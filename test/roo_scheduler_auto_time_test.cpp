#include "roo_scheduler.h"

#include <vector>

#include "gtest/gtest.h"
#include "roo_time.h"

namespace roo_scheduler {

using namespace roo_time;

struct TestTask : public Executable {
  TestTask(std::vector<ExecutionID>& observed) : observed(observed) {}
  void execute(ExecutionID id) override { observed.push_back(id); }
  std::vector<ExecutionID>& observed;
};

TEST(Scheduler, DelayWithNormalPriority) {
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

}  // namespace roo_scheduler
