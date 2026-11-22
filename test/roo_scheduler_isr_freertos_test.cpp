#include <atomic>
#include <csignal>

#include "gtest/gtest.h"
#include "roo_scheduler.h"
#include "roo_testing/interrupts/interrupt_controller.h"
#include "roo_threads.h"
#include "roo_threads/semaphore.h"

namespace roo_scheduler {
namespace {

using roo_time::Millis;
using roo_time::Uptime;

struct InterruptPost {
  Scheduler& scheduler;
  Executable& task;
  volatile sig_atomic_t accepted = 0;
};

void PostFromInterrupt(void* argument) {
  auto& post = *static_cast<InterruptPost*>(argument);
  post.accepted = xPortInIsrContext() == pdTRUE &&
                          post.scheduler.tryScheduleNowFromIsr(post.task)
                      ? 1
                      : -1;
}

roo_testing::InterruptHandle Register(InterruptPost& post) {
  roo_testing::InterruptHandle handle;
  EXPECT_EQ(
      roo_testing::registerInterrupt(PostFromInterrupt, &post, true, &handle),
      roo_testing::InterruptRegistrationResult::kRegistered);
  return handle;
}

// Verifies ISR publication interrupts a blocked FreeRTOS dispatcher and the
// callback runs in that dispatcher's task context.
TEST(SchedulerIsrFreeRtos, WakesTimedDispatcher) {
  SchedulingService scheduler;
  Uptime ran;
  const auto dispatcher = roo::this_thread::get_id();
  Task task([&] {
    EXPECT_EQ(xPortInIsrContext(), pdFALSE);
    EXPECT_EQ(roo::this_thread::get_id(), dispatcher);
    ran = Uptime::Now();
  });
  InterruptPost post{scheduler, task};
  const auto handle = Register(post);
  ASSERT_TRUE(handle.isValid());
  const Uptime deadline = Uptime::Now() + Millis(500);
  scheduler.scheduleOn(deadline, [] {});
  roo::thread producer([handle] {
    roo::this_thread::sleep_for(Millis(20));
    EXPECT_TRUE(roo_testing::setInterruptPending(handle));
  });

  do {
    scheduler.waitForWork();
    scheduler.executeEligibleTasks();
  } while (ran == Uptime::Start() && Uptime::Now() < deadline);
  producer.join();
  EXPECT_TRUE(roo_testing::unregisterInterrupt(handle));
  EXPECT_EQ(post.accepted, 1);
  EXPECT_NE(ran, Uptime::Start());
  EXPECT_LT(ran, deadline);
}

// Verifies ISR work remains observable across the transition from a published
// slot to an in-flight execution while another thread cancels the task.
TEST(SchedulerIsrFreeRtos, CancellationWaitsForClaimedIsrCallback) {
  SchedulingService scheduler;
  roo::binary_semaphore entered(0), unblock(0), cancelling(0);
  std::atomic<bool> returned{false};
  Task task([&] {
    entered.release();
    unblock.acquire();
    returned = true;
  });
  InterruptPost post{scheduler, task};
  const auto handle = Register(post);
  ASSERT_TRUE(handle.isValid());
  EXPECT_TRUE(roo_testing::setInterruptPending(handle));
  const Uptime deadline = Uptime::Now() + Millis(500);
  while (scheduler.empty() && Uptime::Now() < deadline) {
    roo::this_thread::sleep_for(Millis(1));
  }
  EXPECT_TRUE(roo_testing::unregisterInterrupt(handle));
  ASSERT_EQ(post.accepted, 1);

  roo::thread dispatcher(
      [&] { EXPECT_FALSE(scheduler.executeEligibleTasks(1)); });
  entered.acquire();
  roo::thread canceller([&] {
    cancelling.release();
    EXPECT_TRUE(scheduler.cancelAndWait(task));
    EXPECT_TRUE(returned.load());
  });
  cancelling.acquire();
  EXPECT_FALSE(returned.load());
  unblock.release();
  canceller.join();
  dispatcher.join();
  EXPECT_TRUE(scheduler.empty());
}

}  // namespace
}  // namespace roo_scheduler
