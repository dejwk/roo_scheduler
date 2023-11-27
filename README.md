# roo_scheduler

Arduino-compliant microcontroller library for scheduling delayed and/or repetitive tasks. Works on boards that support C++ standard library (e.g. Espressif ESP32 family). Specifically, it requires the following standard headers: `<memory>`, `<queue>`, and `<functional>`.

Uses `std::priority_queue` for storage, and therefore:

* Maintains the queue in a flat vector, and thus, does not dynamically reallocate memory as long as the queue remains below its present capacity. (In most practical cases, the queue will stay within small bound capacity);
* Scheduling a task, as well as looking up the nearest scheduled task, is O(log N), where N is the queue length, so it remains fast even in those rare cases when the queue gets longer.

The tasks can be defined as function pointers, but also as inline lambdas, or generally as arbitrary callables, so it is convenient and idiomatic to make them stateful.
