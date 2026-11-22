#include <Arduino.h>
#include <roo_scheduler.h>

#if ROO_THREADS_HAS_ISR_SUPPORT
#if defined(ESP32) && CONFIG_ARDUINO_ISR_IRAM
#error This example requires Arduino GPIO handlers registered without ESP_INTR_FLAG_IRAM
#endif

namespace {

constexpr int kInputPin = 4;
roo_scheduler::SchedulingService scheduler;
roo_scheduler::Task announce_edge([] { Serial.println("GPIO falling edge"); });

void OnGpioEdge() {
  // This example drops an edge if all eight slots are occupied.
  scheduler.tryScheduleNowFromIsr(announce_edge);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  pinMode(kInputPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(kInputPin), OnGpioEdge, FALLING);
}

void loop() { scheduler.run(); }

#else

void setup() {}
void loop() {}

#endif
