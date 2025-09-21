// This example demonstrates how to schedule one-off tasks (i.e. unique, usually
// stateful, executing only once).

#include <Arduino.h>

#include "roo_scheduler.h"
#include "roo_threads.h"
#include "roo_time.h"

using namespace roo_time;
using namespace roo_scheduler;

Scheduler scheduler;
roo::thread scheduler_thread;

// Note: the counter is incremented from the scheduler thread, and read from the
// loop thread. Therefore, data access must be synchronized. In this case,
// std::atomic<int> is sufficient.
std::atomic<int> counter{0};

roo_scheduler::RepetitiveTask incrementer(scheduler, Seconds(0.4), []() {
  ++counter;
  Serial.printf("Counter: %d\n", counter.load());
});

void setup() {
  Serial.begin(9600);

  roo::thread::attributes attrs;
  // Optional, but useful for debugging.
  attrs.set_name("scheduler");
  // We set priority slightly higher than the loop task, so that the scheduled
  // tasks are able to preempt the loop task if necessary. See
  // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/freertos.html#background-tasks
  attrs.set_priority(2);
  // We set a very small stack size, which is OK for this example because tasks
  // are really simple.
  attrs.set_stack_size(2048);

  // Start the scheduler loop in a new thread.
  scheduler_thread = roo::thread(attrs, []() { scheduler.run(); });

  scheduler.scheduleAfter(Seconds(1), []() { Serial.printf("At 1 second\n"); });
  scheduler.scheduleAfter(Seconds(2),
                          []() { Serial.printf("At 2 seconds\n"); });

  incrementer.start();
}

void loop() {
  // The loop is free to do whatever it wants to do, and it does not need to
  // worry about calling the scheduler to execute pending tasks. They will be
  // asynchronously executed in a separate thread.
  if (incrementer.is_active() && counter >= 10) {
    incrementer.stop();
    Serial.printf("Incrementer stopped at counter=%d\n", counter.load());
  }
}
