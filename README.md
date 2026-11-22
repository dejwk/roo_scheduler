# roo_scheduler

Arduino-compliant microcontroller library for scheduling delayed and/or repetitive tasks. Works on boards that support C++ standard library (e.g. Espressif ESP32 family, and Raspberry Pi Pico with or without FreeRTOS). Specifically, it requires the following standard headers: `<memory>`, `<queue>`, and `<functional>`.

Uses vector-backed heaps for queued work and ready work:

* Maintains the queue in a flat vector, and thus, does not dynamically reallocate memory as long as the queue remains below its present capacity. (In most practical cases, the queue will stay within small bound capacity);
* Scheduling a task is O(log N); dispatch admits due tasks into a priority heap and selects its root. Cancellation pruning is linear in queue size.

The tasks can be defined as function pointers, but also as inline lambdas, or generally as arbitrary callables, so it is convenient and idiomatic to make them stateful.

## Scheduling interface and migration

Scheduling work and deciding when to execute it are separate responsibilities.
For example, a widget may need to schedule a timeout, while the application's
main loop controls when callbacks run.

`SchedulingService` owns the queues and provides the dispatch methods that run
callbacks. `Scheduler` is its restricted base interface: it lets consumers
schedule and cancel work without giving them access to dispatch or waiting
methods. A consumer can accept `Scheduler&` to express that limited need.
The reference borrows the application's existing scheduler.

Because `SchedulingService` derives from `Scheduler`, you can pass the
service directly to consumers and task adapters that accept `Scheduler&`:

```cpp
roo_scheduler::SchedulingService scheduler;
roo_scheduler::SingletonTask deferred(scheduler, [] { /* Deferred work. */ });

deferred.scheduleNow();
scheduler.executeEligibleTasks();
```

No separate client variable is needed. All task adapters accept the restricted
base reference. `Scheduler` exposes the `scheduleOn`, `scheduleAfter`,
`scheduleNow` overloads and `cancel`; dispatch, waiting, pruning, and queue-wide
queries remain on `SchedulingService`. This separation adds no queue, pointer
member, or virtual dispatch.

The service must outlive all borrowed references and adapters. Clients cannot
be copied, constructed independently, or deleted through the base interface.

Use `SchedulingService` for ownership and dispatch, and `Scheduler&` for
borrowers that only need scheduling and cancellation. `SchedulerClient` remains
a deprecated alias for `Scheduler`. The former owning `Scheduler` alias has been
removed: migrate owner declarations and forward declarations to
`SchedulingService`. Rebuild dependent binaries: this is not an ABI-compatible
change, and explicitly typed pointers to inherited members may also need updating.

The restricted interface does not establish a UI thread or forbid nested
dispatch through a separately retained service reference. Applications relying
on deferral until the current callback returns must dispatch on one thread and
prohibit nested dispatch. Cancellation can destroy owned callables synchronously;
objects captured by deferred work still need explicit lifetime management.
Adapter shutdown retains its existing waiting behavior, described below.

## Scheduling from an interrupt

When `ROO_THREADS_HAS_ISR_SUPPORT` is `1`, an interrupt can use
`Scheduler::tryScheduleNowFromIsr(task, priority)` to request a borrowed
execution. The scheduler holds up to eight pending ISR requests. A
successful call reserves one slot, including when the same task is
already pending. A full scheduler returns `false`. The callback runs in
dispatch context and receives its execution ID there.

An ISR request becomes due when the dispatcher imports it. In particular,
a post first imported after a `delayUntil()` deadline can remain pending
when that call returns. Before destroying a borrowed task, disable and
synchronize its interrupt producers and call `cancelAndWait(task)` from
ordinary context. On FreeRTOS with `ROO_THREADS_SINGLETHREADED`, one task
makes all ordinary scheduler calls for the service.

[GPIO example](examples/isr_gpio/isr_gpio.ino) ·
[ISR priority and memory requirements](docs/isr-task-scheduling.md#proposed-api)

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

For borrowed `Executable` objects, `SchedulingService::cancelAndWait(task)` cancels all
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

Host builds default to the roo_testing Arduino ESP32 profile. Bazelisk 1.21
or newer selects that profile when no frontend is specified.

    bazel test ...
    bazel test ... --config=asan
    bazel test ... --config=roo_testing_arduino_esp32

The files under `.roo_testing/bazelrc` are vendored from roo_testing;
follow their canonical-source headers when refreshing them.

Arduino examples are native runnable targets in their source packages. For
example:

    bazel run //examples/simple:simple

### Bare-metal Pico

On bare-metal RP2040 with Pico SDK 2.1 or newer and the default alarm pool
enabled, `SchedulingService::run()` waits for a scheduled deadline or a
processor event through `roo_threads`. Arduino-Pico selects this wait
automatically. For native SDK builds, configure
`ROO_THREADS_PICO_SDK_BARE_METAL=1` for `roo_threads` and its callers.
Put the SDK dependencies on the `roo_threads` target. Single-threaded
builds without an ISR work event poll an idle queue at 1 ms intervals.

An interrupt handler can record an application signal and call `__sev()`.
In the application loop, check for the signal and submit tasks, call
`executeEligibleTasks()`, then call `waitForWork()` before repeating.

The Arduino-Pico tests use the same SDK alarm and event APIs as the device.
Until a released `roo_testing` includes Pico IRQ and event-wait support, use a
local checkout containing that support.

    bazel test --config=roo_testing_arduino_rp2040 \
      --override_module=roo_testing=../roo_testing //...

Native thread creation and cross-thread shutdown require a threaded runtime.
