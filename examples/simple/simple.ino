// This example demonstrates how to use tasks that execute periodically. It
// creates and keeps running 3 such tasks, with different periods.

#include <Arduino.h>

#include "roo_scheduler.h"
#include "roo_time.h"

using namespace roo_time;
using namespace roo_scheduler;

// At any given time, the scheduler queue will contain up to 3 elements, and it
// will never get reallocated. Its total memory footprint is ~120 bytes
// allocated on startup (to back a queue with capacity of 8 elements).
Scheduler scheduler;

PeriodicTask task1(
    scheduler, [] { Serial.printf("Tick: %d\n", millis() / 1000); },
    Seconds(2));

PeriodicTask task2(
    scheduler, [] { Serial.printf("Tack: %d\n", millis() / 1000); },
    Seconds(5));

PeriodicTask task3(
    scheduler, [] { Serial.printf("Toe: %d\n", millis() / 1000); }, Seconds(3));

void setup() {
  Serial.begin(9600);
  task1.start();
  task2.start();
  task3.start();
}

void loop() { scheduler.executeEligibleTasks(); }
