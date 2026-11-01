#include <cstdlib>

#include "gtest/gtest.h"
#include "roo_scheduler.h"
#include "roo_testing/system/timer.h"

// Verifies an initially idle single-threaded event loop pumps platform time,
// allowing a cooperative alarm callback to enqueue work without another thread.
TEST(BareMetalDispatch, IdleLoopPumpsCooperativeEvents) {
  EXPECT_EXIT(
      {
        roo_scheduler::SchedulingService scheduler;
        ScheduleSystemTimeAlarm(system_time_get_micros() + 5000, [&scheduler] {
          scheduler.scheduleNow([] { std::_Exit(0); });
        });
        scheduler.run();
      },
      testing::ExitedWithCode(0), "");
}
