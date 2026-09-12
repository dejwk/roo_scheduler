# roo_scheduler

Arduino-compliant microcontroller library for scheduling delayed and/or repetitive tasks. Works on boards that support C++ standard library (e.g. Espressif ESP32 family, and Raspberry Pi Pico SMP). Specifically, it requires the following standard headers: `<memory>`, `<queue>`, and `<functional>`.

Uses vector-backed heaps for queued work and ready work:

* Maintains the queue in a flat vector, and thus, does not dynamically reallocate memory as long as the queue remains below its present capacity. (In most practical cases, the queue will stay within small bound capacity);
* Scheduling a task is O(log N); dispatch admits due tasks into a priority heap and selects its root. Cancellation pruning is linear in queue size.

The tasks can be defined as function pointers, but also as inline lambdas, or generally as arbitrary callables, so it is convenient and idiomatic to make them stateful.

## Dispatch cutoffs

`executeEligibleTasksUpTo(cutoff)` admits queued tasks due by
`min(cutoff, now)`, then dispatches ready tasks in priority order. Work admitted
by an earlier call remains ready even if a later call uses an earlier cutoff.
Nested dispatch can also admit work beyond an outer call's cutoff; that work
remains eligible when the outer call resumes. A cutoff does not withdraw work
from the ready heap. This keeps selection at the heap root without a scan.
Priority thresholds still apply to every dispatched task.

With `ROO_SCHEDULER_IGNORE_PRIORITY=1` there is no separate ready heap; dispatch
selects directly from the time heap using the current cutoff.

## Threads and lifetime

A scheduler accepts scheduling, cancellation and queries from multiple threads.
Use one dispatch thread per scheduler. Nested dispatch on that thread (including
`delay()` from a callback) is supported; simultaneous dispatch on multiple threads
is not. Callbacks and retired owned-task destructors run outside scheduler locks.

`RepetitiveTask`, `PeriodicTask`, `SingletonTask` and `IteratingTask` synchronize
control state. Scheduling can race with dispatch without exposing an unfinished
execution ID. Callback bodies must still synchronize application data accessed
by other threads. A stop/restart during a repetitive or periodic callback wins
over the old callback's attempt to schedule its next execution.

`cancel()` and `stop()` do not wait for callbacks. A callback already claimed for
execution may still finish. `is_scheduled()` reports pending singleton work, not
whether its callback is running. A sequence of separate control/query calls is
not one atomic operation; use application synchronization for compound changes.

For teardown, prevent other callers from using the object and call the adapter's
permanent `shutdown()` before destroying any state its callback references:

```cpp
// Controller thread; do not hold a mutex needed by the callback.
if (task.shutdown()) {
  // No pending or claimed callback remains. Callback-owned state can be torn down.
}
```

Shutdown disables later starts/rescheduling, cancels pending work, and waits for
claimed callbacks to return. Repeated shutdown is allowed. From the task's own
callback, or a nested callback inside it, shutdown returns false rather than
waiting on itself; it still disables future scheduling. Do not interpret false
as permission to destroy callback-owned state. No other caller may access the
object while it is being destroyed. The scheduler must outlive its adapters.

For borrowed `Executable` objects, `Scheduler::cancelAndWait(task)` cancels all
pending executions of that object and waits for claimed executions. Unlike an
adapter's shutdown it cannot disable application rescheduling: callers must do
that first. It returns false on self-wait just like adapter shutdown.

Singleton callbacks and iterator completion callbacks may destroy their own
adapter; their callable storage stays alive until invocation returns. The
iterator itself must remain alive through `next()`. Repetitive and periodic
callbacks must not destroy their adapter; their execution methods need to access
it after the callback. Their destructors terminate on that unsupported self-wait.

Stop dispatch and all producers before destroying the scheduler. There is no
implicit cross-thread termination of `run()`.

## Allocation behavior

Queue growth and owning callable submissions retain their existing allocation
costs. Deferred cancellation can grow the cancellation set. After capacity is
established, dispatch of borrowed tasks requires no additional heap storage.
Canceled owned tasks are linked through an intrusive retirement pointer and
destroyed after unlocking; there is no temporary retirement vector. In-flight
callback tracking uses the dispatch stack, including nested dispatch.

Singleton adapters allocate shared callable storage once during construction so
a callback can destroy its adapter safely. Iterating adapters do the same when
a completion callback is supplied. Dispatch copies only the shared reference,
preserving mutable callable state without allocating or copying the callable.
Repetitive and periodic adapters do not add shared callable storage. Adapter
lifecycle state uses reserved negative execution IDs instead of additional
flags or generation counters. Callable
captures and the selected threading backend can have their own allocation costs.

## Host emulation

Host builds use the roo_testing 2.0 Arduino ESP32 profile. With Bazelisk 1.21
or newer, a plain command defaults to that profile and prints a notice:

    bazel test ...
    bazel test ... --config=asan
    bazel test ... --config=roo_testing_arduino_esp32

The files under .roo_testing/bazelrc/esp32 are vendored from roo_testing;
follow their canonical-source headers when refreshing them.

Arduino examples are native runnable targets in their source packages. For
example:

    bazel run //examples/simple:simple
