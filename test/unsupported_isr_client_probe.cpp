#undef ROO_TESTING
#undef ESP32
#undef ESP_PLATFORM
#undef PICO_ON_DEVICE
#undef ARDUINO_ARCH_RP2040

#include "roo_scheduler.h"

static_assert(ROO_THREADS_HAS_ISR_SUPPORT == 0);

namespace roo_scheduler {

// Verifies ordinary client calls compile when ISR support is unavailable.
void CompileOrdinaryClient(SchedulingService& service, Executable& task) {
  const ExecutionID id = service.scheduleNow(task);
  service.executeEligibleTasks();
  service.cancel(id);
}

}  // namespace roo_scheduler
