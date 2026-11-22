#include <hardware/sync.h>
#include <pico/time.h>

#include <cstdint>
#include <vector>

#include "gtest/gtest.h"
#include "roo_scheduler.h"

namespace roo_scheduler {
namespace {

using roo_time::Millis;
using roo_time::Uptime;

struct IsrPost {
  Executable& task;
  Priority priority = Priority::kNormal;
};

struct AlarmPosts {
  Scheduler& scheduler;
  const IsrPost* posts;
  unsigned count;
  unsigned accepted = 0;
  unsigned exception = 0;
};

int64_t PostFromAlarm(alarm_id_t, void* argument) {
  auto& batch = *static_cast<AlarmPosts*>(argument);
  batch.exception = __get_current_exception();
  for (unsigned i = 0; i < batch.count; ++i) {
    const IsrPost& post = batch.posts[i];
    if (batch.scheduler.tryScheduleNowFromIsr(post.task, post.priority)) {
      ++batch.accepted;
    }
  }
  return 0;
}

void PostAndAdvance(AlarmPosts& batch) {
  const alarm_id_t alarm = add_alarm_in_ms(1, PostFromAlarm, &batch, false);
  ASSERT_GT(alarm, 0);
  roo_time::Delay(Millis(1));
  cancel_alarm(alarm);
  EXPECT_NE(batch.exception, 0u);
}

// Verifies duplicate posts occupy individual slots and import preserves the
// storage limit until dispatch or cancellation actually releases a slot.
TEST(SchedulerIsrPico, CapacitySurvivesImportAndSupportsReuse) {
  SchedulingService scheduler;
  unsigned executions = 0;
  Task task([&] {
    EXPECT_EQ(__get_current_exception(), 0u);
    ++executions;
  });
  IsrPost duplicates[] = {{task}, {task}, {task}, {task}, {task},
                          {task}, {task}, {task}, {task}};
  AlarmPosts first{scheduler, duplicates, 9};
  ASSERT_NO_FATAL_FAILURE(PostAndAdvance(first));
  EXPECT_EQ(first.accepted, 8u);
  EXPECT_FALSE(scheduler.empty());
  EXPECT_EQ(scheduler.getNearestExecutionTime(), Uptime::Now());
  EXPECT_EQ(scheduler.getNearestExecutionDelay(), roo_time::Duration());

  EXPECT_TRUE(scheduler.executeEligibleTasksUpTo(Uptime::Start()));
  EXPECT_EQ(executions, 0u);
  AlarmPosts full{scheduler, duplicates, 1};
  ASSERT_NO_FATAL_FAILURE(PostAndAdvance(full));
  EXPECT_EQ(full.accepted, 0u);

  EXPECT_FALSE(scheduler.executeEligibleTasks(1));
  EXPECT_EQ(executions, 1u);
  AlarmPosts reused{scheduler, duplicates, 2};
  ASSERT_NO_FATAL_FAILURE(PostAndAdvance(reused));
  EXPECT_EQ(reused.accepted, 1u);
  EXPECT_TRUE(scheduler.cancelAndWait(task));
  EXPECT_TRUE(scheduler.empty());

  AlarmPosts freed{scheduler, duplicates, 9};
  ASSERT_NO_FATAL_FAILURE(PostAndAdvance(freed));
  EXPECT_EQ(freed.accepted, 8u);
  EXPECT_TRUE(scheduler.executeEligibleTasks());
  EXPECT_EQ(executions, 9u);
}

// Verifies cancellation removes matching posts before import while restoring
// capacity and keeping the publication order of other tasks.
TEST(SchedulerIsrPico, CancelBeforeImportPreservesOtherPosts) {
  SchedulingService scheduler;
  std::vector<int> order;
  Task cancel([&] { order.push_back(0); });
  Task first([&] { order.push_back(1); });
  Task second([&] { order.push_back(2); });
  Task third([&] { order.push_back(3); });
  IsrPost posts[] = {{cancel}, {first}, {cancel}, {second}};
  AlarmPosts batch{scheduler, posts, 4};
  ASSERT_NO_FATAL_FAILURE(PostAndAdvance(batch));
  ASSERT_TRUE(scheduler.cancelAndWait(cancel));
  IsrPost trailing[] = {{third}};
  AlarmPosts next{scheduler, trailing, 1};
  ASSERT_NO_FATAL_FAILURE(PostAndAdvance(next));
  EXPECT_TRUE(scheduler.executeEligibleTasksUpToNow());
  EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

#if !ROO_SCHEDULER_IGNORE_PRIORITY
// Verifies ordinary work and imported work use one ordering, including an
// equal-priority, equal-time tie and ready work with an earlier cutoff.
TEST(SchedulerIsrPico, MergesReadinessAndPriorityWithOrdinaryWork) {
  SchedulingService scheduler;
  std::vector<int> order;
  Task normal([&] { order.push_back(2); });
  Task high([&] { order.push_back(0); });
  Task low([&] { order.push_back(3); });
  IsrPost posts[] = {
      {normal}, {high, Priority::kElevated}, {low, Priority::kBackground}};
  AlarmPosts batch{scheduler, posts, 3};
  ASSERT_NO_FATAL_FAILURE(PostAndAdvance(batch));
  scheduler.scheduleNow([&] { order.push_back(1); });
  EXPECT_FALSE(scheduler.executeEligibleTasksUpToNow(Priority::kMinimum, 1));
  EXPECT_EQ(order, (std::vector<int>{0}));
  EXPECT_TRUE(scheduler.executeEligibleTasksUpTo(Uptime::Start()));
  EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 3}));
}
#else
// Verifies ISR and ordinary candidates share time ordering and the existing
// effective normal priority when priority ordering is compiled out.
TEST(SchedulerIsrPico, UsesTimeAndEffectiveNormalPriority) {
  SchedulingService scheduler;
  std::vector<int> order;
  Task isr([&] { order.push_back(2); });
  IsrPost posts[] = {{isr, Priority::kMaximum}};
  AlarmPosts batch{scheduler, posts, 1};
  ASSERT_NO_FATAL_FAILURE(PostAndAdvance(batch));
  scheduler.scheduleNow([&] { order.push_back(1); });
  EXPECT_TRUE(scheduler.executeEligibleTasks(Priority::kElevated));
  EXPECT_TRUE(order.empty());
  EXPECT_TRUE(scheduler.executeEligibleTasks());
  EXPECT_EQ(order, (std::vector<int>{1, 2}));
}
#endif

// Verifies the final delay pass keeps its fixed cutoff when a callback spans
// the deadline and the ISR request is imported only after that callback.
TEST(SchedulerIsrPico, DelayImportUsesTheFinalCutoff) {
  SchedulingService scheduler;
  bool posted_ran = false;
  Task posted([&] { posted_ran = true; });
  IsrPost posts[] = {{posted, Priority::kElevated}};
  AlarmPosts batch{scheduler, posts, 1};
  const alarm_id_t alarm = add_alarm_in_ms(5, PostFromAlarm, &batch, false);
  ASSERT_GT(alarm, 0);
  scheduler.scheduleNow([] { roo_time::Delay(Millis(20)); });
  scheduler.delayUntil(Uptime::Now() + Millis(10));
  cancel_alarm(alarm);
  EXPECT_EQ(batch.accepted, 1u);
  EXPECT_FALSE(posted_ran);
  EXPECT_FALSE(scheduler.empty());
  EXPECT_TRUE(scheduler.executeEligibleTasks());
  EXPECT_TRUE(posted_ran);
}

// Verifies an alarm can interrupt a scheduler work wait for a later deadline.
TEST(SchedulerIsrPico, InterruptWakesTimedWorkWait) {
  SchedulingService scheduler;
  bool ran = false;
  Task task([&] { ran = true; });
  IsrPost posts[] = {{task}};
  AlarmPosts batch{scheduler, posts, 1};
  const Uptime deadline = Uptime::Now() + Millis(100);
  scheduler.scheduleOn(deadline, [] {});
  const alarm_id_t alarm = add_alarm_in_ms(5, PostFromAlarm, &batch, false);
  ASSERT_GT(alarm, 0);
  do {
    scheduler.waitForWork();
    scheduler.executeEligibleTasks();
  } while (!ran && Uptime::Now() < deadline);
  cancel_alarm(alarm);
  EXPECT_TRUE(ran);
  EXPECT_LT(Uptime::Now(), deadline);
}

}  // namespace
}  // namespace roo_scheduler
