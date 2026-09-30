#include <type_traits>
#include <utility>

#include "gtest/gtest.h"
#include "roo_scheduler.h"
#include "roo_time.h"

namespace roo_scheduler {
namespace {

template <typename...>
using Void = void;

// Detect public access, including accidentally inherited dispatch methods.
#define CHECK_SERVICE_ONLY(method, ...)                                      \
  template <typename T, typename = void>                                     \
  struct Has_##method : std::false_type {};                                  \
  template <typename T>                                                      \
  struct Has_##                                                              \
      method<T, Void<decltype(std::declval<T&>().method(__VA_ARGS__))>>      \
      : std::true_type {};                                                   \
  static_assert(!Has_##method<SchedulerClient>::value, "Restricted client"); \
  static_assert(Has_##method<SchedulingService>::value, "Owner compatibility")

CHECK_SERVICE_ONLY(run);
CHECK_SERVICE_ONLY(delay, roo_time::Millis(1));
CHECK_SERVICE_ONLY(delayUntil, roo_time::Uptime::Now());
CHECK_SERVICE_ONLY(executeEligibleTasks);
CHECK_SERVICE_ONLY(executeEligibleTasksUpToNow);
CHECK_SERVICE_ONLY(executeEligibleTasksUpTo, roo_time::Uptime::Now());
CHECK_SERVICE_ONLY(cancelAndWait, std::declval<Executable&>());
CHECK_SERVICE_ONLY(pruneCanceled);
CHECK_SERVICE_ONLY(empty);
CHECK_SERVICE_ONLY(getNearestExecutionTime);
CHECK_SERVICE_ONLY(getNearestExecutionDelay);
#undef CHECK_SERVICE_ONLY

// Exercise the deprecated spelling without hiding warnings in other tests.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
static_assert(std::is_same<Scheduler, SchedulingService>::value,
              "Existing owner name remains valid");

// Verifies legacy owners still construct adapters and drive dispatch.
TEST(SchedulerClient, DeprecatedOwnerCompatibility) {
  Scheduler scheduler;
  int calls = 0;
  SingletonTask task(scheduler, [&] { ++calls; });
  task.scheduleNow();
  scheduler.executeEligibleTasks();
  EXPECT_EQ(1, calls);
}
#pragma GCC diagnostic pop

static_assert(std::is_convertible<SchedulingService&, SchedulerClient&>::value,
              "Services provide client references");
static_assert(!std::is_default_constructible<SchedulerClient>::value,
              "Clients cannot be constructed independently");
static_assert(!std::is_destructible<SchedulerClient>::value,
              "Clients cannot be deleted through the base interface");
static_assert(!std::is_copy_constructible<SchedulingService>::value,
              "Services cannot be copied");
static_assert(!std::is_move_constructible<SchedulingService>::value,
              "Services cannot be moved while borrowed");
static_assert(!std::is_polymorphic<SchedulerClient>::value,
              "The restricted interface needs no virtual dispatch");
static_assert(sizeof(SchedulingService) == sizeof(SchedulerClient),
              "The owner adds no per-instance storage");

// Verifies all submission forms share the owner's queue and remain deferred.
TEST(SchedulerClient, SubmitsBorrowedOwnedAndCallableWork) {
  SchedulingService scheduler;
  SchedulerClient& client = scheduler;
  int calls = 0;
  Task borrowed([&] { ++calls; });
  client.scheduleNow(borrowed);
  client.scheduleOn(roo_time::Uptime::Now(),
                    std::unique_ptr<Executable>(new Task([&] { ++calls; })));
  client.scheduleAfter(roo_time::Millis(10), [&] { ++calls; });
  EXPECT_EQ(0, calls);
  scheduler.executeEligibleTasks();
  EXPECT_EQ(2, calls);
  roo_time::Delay(roo_time::Millis(10));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(3, calls);
}

// Verifies cancellation through a client suppresses pending execution and
// owned tasks are eventually destroyed by the scheduler.
TEST(SchedulerClient, CancelsOwnedWork) {
  SchedulingService scheduler;
  SchedulerClient& client = scheduler;
  int calls = 0;
  int destroyed = 0;
  struct Owned : Executable {
    Owned(int& calls, int& destroyed) : calls(calls), destroyed(destroyed) {}
    ~Owned() override { ++destroyed; }
    void execute(ExecutionID) override { ++calls; }
    int& calls;
    int& destroyed;
  };
  ExecutionID id = client.scheduleNow(
      std::unique_ptr<Executable>(new Owned(calls, destroyed)));
  client.cancel(id);
  scheduler.executeEligibleTasks();
  EXPECT_EQ(0, calls);
  EXPECT_EQ(1, destroyed);
}

// Verifies callbacks can submit work without dispatching it on their own stack.
TEST(SchedulerClient, DefersSubmissionFromCallback) {
  SchedulingService scheduler;
  SchedulerClient& client = scheduler;
  bool returned = false;
  bool executed = false;
  client.scheduleNow([&] {
    client.scheduleNow([&] {
      EXPECT_TRUE(returned);
      executed = true;
    });
    EXPECT_FALSE(executed);
    returned = true;
  });
  scheduler.executeEligibleTasks();
  EXPECT_TRUE(returned);
  EXPECT_TRUE(executed);
}

// Verifies every adapter accepts the restricted base, including replacement,
// repeated execution and permanent shutdown through its private lifecycle
// hooks.
TEST(SchedulerClient, SupportsEveryAdapter) {
  SchedulingService scheduler;
  SchedulerClient& client = scheduler;
  int singleton_calls = 0;
  int repetitive_calls = 0;
  int periodic_calls = 0;
  int completions = 0;
  struct Finished : IteratingTask::Iterator {
    int64_t next() override { return -1; }
  } iterator;
  SingletonTask singleton(client, [&] { ++singleton_calls; });
  RepetitiveTask repetitive(client, roo_time::Millis(10),
                            [&] { ++repetitive_calls; });
  PeriodicTask periodic(client, roo_time::Millis(10),
                        [&] { ++periodic_calls; });
  IteratingTask iterating(client, iterator, [&] { ++completions; });
  singleton.scheduleAfter(roo_time::Millis(100));
  singleton.scheduleNow();
  ASSERT_TRUE(repetitive.startInstantly());
  ASSERT_TRUE(periodic.start());
  ASSERT_TRUE(iterating.start());
  scheduler.executeEligibleTasks();
  EXPECT_EQ(1, singleton_calls);
  EXPECT_EQ(1, repetitive_calls);
  EXPECT_EQ(1, periodic_calls);
  EXPECT_EQ(1, completions);
  roo_time::Delay(roo_time::Millis(10));
  scheduler.executeEligibleTasks();
  EXPECT_EQ(2, repetitive_calls);
  EXPECT_EQ(2, periodic_calls);
  EXPECT_TRUE(singleton.shutdown());
  EXPECT_TRUE(repetitive.shutdown());
  EXPECT_TRUE(periodic.shutdown());
  EXPECT_TRUE(iterating.shutdown());
  EXPECT_TRUE(scheduler.empty());
}

}  // namespace
}  // namespace roo_scheduler
