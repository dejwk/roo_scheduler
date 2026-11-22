#include <cstdlib>

#include "gtest/gtest.h"
#include "hardware/sync.h"
#include "pico/time.h"
#include "roo_scheduler.h"
#include "roo_testing/system/timer.h"

namespace {

struct AlarmSignal {
  volatile unsigned exception = 0;
};

/// Records the IRQ and signals the application loop without touching the queue.
int64_t SignalFromAlarm(alarm_id_t, void* arg) {
  static_cast<AlarmSignal*>(arg)->exception = __get_current_exception();
  __sev();
  return 0;
}

/// Verifies that IRQ work reaches the scheduler through application context.
void CheckAlarmHandoff(roo_scheduler::SchedulingService& scheduler) {
  const uint64_t start = time_us_64();
  AlarmSignal signal;
  const alarm_id_t alarm = add_alarm_in_ms(5, SignalFromAlarm, &signal, false);
  ASSERT_GT(alarm, 0);

  // Latched events can cause a wake without advancing manual time.
  do {
    scheduler.waitForWork();
  } while (signal.exception == 0 && time_us_64() == start);
  cancel_alarm(alarm);
  EXPECT_EQ(time_us_64() - start, 5000u);
  ASSERT_NE(signal.exception, 0u);
  EXPECT_EQ(__get_current_exception(), 0u);

  bool dispatched = false;
  scheduler.scheduleNow([&] { dispatched = true; });
  scheduler.executeEligibleTasks();
  EXPECT_TRUE(dispatched);
}

// Verifies that an idle scheduler waits for a Pico IRQ event.
TEST(BareMetalDispatch, IdleWaitWakesOnAlarmEvent) {
  roo_scheduler::SchedulingService scheduler;
  CheckAlarmHandoff(scheduler);
}

// Verifies that a cooperative callback can submit work to an idle run() loop.
TEST(BareMetalDispatch, IdleLoopPumpsCooperativeEvents) {
  EXPECT_EXIT(
      {
        roo_scheduler::SchedulingService scheduler;
        const uint64_t target = time_us_64() + 5000;
        ScheduleSystemTimeAlarm(target, [&scheduler, target] {
          scheduler.scheduleNow(
              [target] { std::_Exit(time_us_64() == target ? 0 : 1); });
        });

        // Wake the loop later if the earlier submission fails to signal it.
        const alarm_id_t watchdog = add_alarm_in_ms(
            100,
            [](alarm_id_t, void*) -> int64_t {
              __sev();
              return 0;
            },
            nullptr, false);
        if (watchdog <= 0) std::_Exit(2);
        scheduler.run();
      },
      testing::ExitedWithCode(0), "");
}

// Verifies earlier IRQ work can wake a scheduler waiting for a later deadline.
TEST(BareMetalDispatch, AlarmWakesWaitBeforeScheduledDeadline) {
  roo_scheduler::SchedulingService scheduler;
  bool later_task_ran = false;
  scheduler.scheduleAfter(roo_time::Millis(500),
                          [&] { later_task_ran = true; });
  CheckAlarmHandoff(scheduler);
  EXPECT_FALSE(later_task_ran);
}

// Verifies run() wakes for its own scheduled deadline without an application
// IRQ.
TEST(BareMetalDispatch, RunDispatchesAtScheduledDeadline) {
  EXPECT_EXIT(
      {
        roo_scheduler::SchedulingService scheduler;
        const roo_time::Uptime target =
            roo_time::Uptime::Now() + roo_time::Millis(20);
        scheduler.scheduleOn(target, [target] {
          std::_Exit(roo_time::Uptime::Now() == target ? 0 : 1);
        });
        scheduler.run();
      },
      testing::ExitedWithCode(0), "");
}

}  // namespace
