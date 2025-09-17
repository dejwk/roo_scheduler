// This example demonstrates how to schedule one-off tasks (i.e. unique, usually
// stateful, executing only once).

#include <Arduino.h>

#include "roo_scheduler.h"
#include "roo_time.h"

using namespace roo_time;
using namespace roo_scheduler;

Scheduler scheduler;

void stateless_one_off_task() { Serial.printf("Stateless one-off\n"); }

void tic(int idx) {
  Serial.printf("Tic: %d\n", idx);
  scheduler.scheduleAfter(Seconds(1), [idx]() { tic(idx + 1); });
}

void setup() {
  Serial.begin(9600);
  const char* foo = "foo";
  // Schedule a simple lambda, capturing a local argument. (Note that this works
  // because string literals are global constants.)
  scheduler.scheduleAfter(
      Seconds(2), [foo] { Serial.printf("One-off lambda: %s\n", foo); });

  // Schedule a simple function that takes no arguments.
  scheduler.scheduleAfter(Seconds(3), stateless_one_off_task);

  // Schedule a parameterized function that will keep rescheduling itself every
  // second.
  scheduler.scheduleAfter(Seconds(4), []() { tic(0); });
}

void loop() { scheduler.run(); }
