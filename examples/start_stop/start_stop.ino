// In this example, we demonstrate how a repetitive task (tick) can be toggled
// on and off, in this case by another repetitive task (toggle).
//
// To mix it up, the implementation of 'toggle' is in a separate function,
// whereas the implementation of the 'tick' is kept as a lambda.

#include <Arduino.h>

#include "roo_scheduler.h"
#include "roo_time.h"

using namespace roo_time;
using namespace roo_scheduler;

Scheduler scheduler;

RepetitiveTask tick(scheduler, Seconds(1),
                    [] { Serial.printf("Tick: %d\n", millis() / 1000); });

void toggle_fn() {
  static bool is_on = false;
  is_on = !is_on;
  if (is_on) {
    // On toggle, tick immediately.
    tick.start(Millis(0));
  } else {
    tick.stop();
  }
}

RepetitiveTask toggle(scheduler, Seconds(5), toggle_fn);

void setup() {
  Serial.begin(9600);
  toggle.start();
}

void loop() { scheduler.executeEligibleTasks(); }
