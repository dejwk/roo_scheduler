#include <Arduino.h>

#include "roo_scheduler.h"
#include "roo_time.h"

using namespace roo_time;
using namespace roo_scheduler;

Scheduler scheduler;

PeriodicTask task1(
    scheduler, [] { Serial.printf("Tick: %d\n", millis() / 1000); },
    Seconds(2));

PeriodicTask task2(
    scheduler, [] { Serial.println("Tack: %d\n", millis() / 1000); },
    Seconds(5));

PeriodicTask task3(
    scheduler, [] { Serial.println("Toe: %d\n", millis() / 1000); },
    Seconds(3));

void setup() {
  Serial.begin(9600);
  task1.start();
  task2.start();
  task3.start();
}

void loop() { scheduler.executeEligibleTasks(); }
