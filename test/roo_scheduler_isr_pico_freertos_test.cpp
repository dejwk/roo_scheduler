#include <atomic>

#include "gtest/gtest.h"
#include "roo_scheduler.h"
#include "roo_testing/system/timer.h"
#include "runtime.h"

extern "C" long __real_RooFreeRtosPollWaiters();

namespace roo_scheduler {
namespace {

using roo_time::Millis;
using roo_time::Uptime;

struct InterruptPost {
  SchedulingService& scheduler;
  Executable& task;
  Uptime when;
};

std::atomic<InterruptPost*> pending_post{nullptr};
static_assert(decltype(pending_post)::is_always_lock_free);

// Verifies a tick ISR wakes a blocked Pico FreeRTOS scheduler and the callback
// runs in the dispatcher task before the next scheduled deadline.
TEST(SchedulerIsrPicoFreeRtos, WakesTimedDispatcher) {
  SchedulingService scheduler;
  const roo::thread::id dispatcher = roo::this_thread::get_id();
  Uptime ran = Uptime::Start();
  Task task([&] {
    EXPECT_FALSE(RooFreeRtosPortIsIsr());
    EXPECT_EQ(roo::this_thread::get_id(), dispatcher);
    ran = Uptime::Now();
  });
  const Uptime start = Uptime::Now();
  const Uptime deadline = start + Millis(500);
  scheduler.scheduleOn(deadline, [] {});

  InterruptPost post{scheduler, task, start + Millis(20)};
  // Manual time advances once the dispatcher blocks. The alarm makes the
  // kernel tick at the posting time before it advances to the later deadline.
  const SystemTimeAlarmId alarm =
      ScheduleSystemTimeAlarm(post.when.inMicros(), [] {});
  pending_post.store(&post, std::memory_order_release);
  do {
    scheduler.waitForWork();
    scheduler.executeEligibleTasks();
  } while (ran == Uptime::Start() && Uptime::Now() < deadline);
  pending_post.store(nullptr, std::memory_order_release);
  CancelSystemTimeAlarm(alarm);

  EXPECT_GE(ran, post.when);
  EXPECT_LT(ran, deadline);
}

}  // namespace

// Injects a one-shot post from the Pico port's native tick ISR and preserves
// its normal handling of timed waits.
extern "C" long __wrap_RooFreeRtosPollWaiters() {
  const long reschedule = __real_RooFreeRtosPollWaiters();
  InterruptPost* post = pending_post.load(std::memory_order_acquire);
  if (post != nullptr && RooFreeRtosPortIsIsr() &&
      Uptime::Now() >= post->when) {
    pending_post.store(nullptr, std::memory_order_relaxed);
    post->scheduler.tryScheduleNowFromIsr(post->task);
  }
  return reschedule;
}

}  // namespace roo_scheduler
