#include <vector>

#include "gtest/gtest.h"
#include "roo_scheduler.h"
#include "roo_time.h"

namespace roo_scheduler {

using namespace roo_time;

struct TestTask : public Executable {
  TestTask(std::vector<ExecutionID>& observed) : observed(observed) {}
  void execute(ExecutionID id) override { observed.push_back(id); }
  std::vector<ExecutionID>& observed;
};

// Verifies an expired delay drains normal and higher priorities, leaving lower
// priorities pending until they are explicitly requested.
TEST(Scheduler, DelayWithNormalPriority) {
  Scheduler scheduler;
  std::vector<ExecutionID> observed;
  TestTask test(observed);

  // An expired deadline exercises the guaranteed final drain without relying
  // on how much opportunistic work fits into a host scheduling interval.
  Uptime trigger = Uptime::Now();
  ExecutionID id1 = scheduler.scheduleOn(trigger, test, PRIORITY_BACKGROUND);
  ExecutionID id2 = scheduler.scheduleOn(trigger, test, PRIORITY_REDUCED);
  ExecutionID id3 = scheduler.scheduleOn(trigger, test, PRIORITY_NORMAL);
  ExecutionID id4 = scheduler.scheduleOn(trigger, test, PRIORITY_ELEVATED);
  ExecutionID id5 = scheduler.scheduleOn(trigger, test, PRIORITY_SENSITIVE);
  ExecutionID id6 = scheduler.scheduleOn(trigger, test, PRIORITY_CRITICAL);

  scheduler.delayUntil(trigger);
  std::vector<ExecutionID> expected = {id6, id5, id4, id3};
  EXPECT_EQ(observed, expected);

  scheduler.delayUntil(trigger, PRIORITY_BACKGROUND);
  expected.push_back(id2);
  expected.push_back(id1);
  EXPECT_EQ(observed, expected);
}

// Verifies an expired delay honors a heightened priority threshold and a later
// background-priority drain executes all remaining tasks in priority order.
TEST(Scheduler, DelayWithHeightenedPriority) {
  Scheduler scheduler;
  std::vector<ExecutionID> observed;
  TestTask test(observed);

  Uptime trigger = Uptime::Now();
  ExecutionID id1 = scheduler.scheduleOn(trigger, test, PRIORITY_BACKGROUND);
  ExecutionID id2 = scheduler.scheduleOn(trigger, test, PRIORITY_REDUCED);
  ExecutionID id3 = scheduler.scheduleOn(trigger, test, PRIORITY_NORMAL);
  ExecutionID id4 = scheduler.scheduleOn(trigger, test, PRIORITY_ELEVATED);
  ExecutionID id5 = scheduler.scheduleOn(trigger, test, PRIORITY_SENSITIVE);
  ExecutionID id6 = scheduler.scheduleOn(trigger, test, PRIORITY_CRITICAL);

  scheduler.delayUntil(trigger, PRIORITY_SENSITIVE);
  std::vector<ExecutionID> expected = {id6, id5};
  EXPECT_EQ(observed, expected);

  scheduler.delayUntil(trigger, PRIORITY_BACKGROUND);
  expected.push_back(id4);
  expected.push_back(id3);
  expected.push_back(id2);
  expected.push_back(id1);
  EXPECT_EQ(observed, expected);
}

// Verifies automatic time reaches the deadline and executes work due at that
// deadline, including background tasks when the caller requests them.
TEST(Scheduler, DelayUntilFutureDeadline) {
  Scheduler scheduler;
  std::vector<ExecutionID> observed;
  TestTask test(observed);

  Uptime deadline = Uptime::Now() + Millis(10);
  ExecutionID id = scheduler.scheduleOn(deadline, test, PRIORITY_BACKGROUND);
  scheduler.delayUntil(deadline, PRIORITY_BACKGROUND);

  EXPECT_GE(Uptime::Now(), deadline);
  std::vector<ExecutionID> expected = {id};
  EXPECT_EQ(observed, expected);
}

}  // namespace roo_scheduler
