# Scheduling Tasks from Interrupt Handlers

## Objective

Allow an interrupt handler to request immediate execution of a borrowed task
on the existing scheduler dispatcher, on FreeRTOS and bare-metal RP2040.

## Motivation

A Pico alarm can wake the scheduler loop today, but the application has to
store the interrupt state, issue a processor event, and arrange for the loop to
submit the task. A FreeRTOS build needs a different handoff because its
dispatcher blocks as an RTOS task. A single scheduler entry point lets a
driver use the same submission code in both builds.
[Current Pico alarm handoff](../test/bare_metal_dispatch_test.cpp)

## Background

`Scheduler` exposes submission and cancellation to callers.
`SchedulingService` owns dispatch, including one dispatch context per service
and support for nested dispatch in that context. A borrowed `Executable`
stays owned by the caller. The scheduler holds a pointer and gives each
execution an ID. Dispatch admits due work using a time cutoff, then selects
among admitted entries by priority. Work already admitted stays eligible
across later calls with an earlier cutoff.

Ordinary submission acquires the scheduler mutex and inserts into a
vector-backed heap that can grow. Changing the wake operation alone would
leave an interrupt handler using that task-context submission path.
[Scheduler interfaces and dispatch rules](../README.md)
· [Heap insertion](../src/roo_scheduler.cpp)

## Requirements

- An interrupt handler can request an immediate callback without taking a
  task mutex, allocating, or invoking application code. Accepted requests
  are individually retained in bounded storage until claimed or canceled.
  When that storage is full, the caller can detect the rejected request.
- A request can wake an idle dispatcher or interrupt its wait for a later
  deadline. The same scheduling code can be used on both supported runtimes.
- Work runs in the dispatch context and follows the service's priority,
  cutoff, and dispatch count rules.
- A caller that has stopped submitting a borrowed task can cancel and wait
  for it even while an accepted request is pending import.
- Builds without the required ISR primitives retain ordinary scheduling.

## Design Overview

The scheduler reserves eight slots for ISR requests. Each occupied slot holds
a borrowed task pointer and priority. It stays occupied until dispatch claims
the request or cancellation removes it. The dispatcher **imports** posted
requests by assigning each a due time and execution ID in its existing slot.
Keeping them there gives the interrupt producer a fixed limit on outstanding
work, even when the dispatcher is busy.

One **work event** wakes the dispatcher for both ISR submissions and ordinary
scheduling changes. The event retains a wakeup while the slots retain the
individual requests. Dispatch completion uses the existing condition variable
for callers waiting in `cancelAndWait()`.
[Companion roo_threads design](../../roo_threads/docs/isr-queue-and-wake-event.md)

For example, housekeeping is due when the dispatcher imports two GPIO
requests. An elevated-priority GPIO callback runs first, then normal-priority
housekeeping, then a background-priority GPIO callback. A post takes effect
at a dispatch selection, including a nested selection requested by the
current callback.

## Design Details

### Submission and storage

On supported builds, the `Scheduler` base owns the slots and work event.
Producers use the `Scheduler` reference they already borrow from
`SchedulingService`. Construct the service before enabling its producers.

Under the `roo_threads` ISR-safe critical section, `tryScheduleNowFromIsr()`
claims a free slot, copies the task pointer and priority, and appends its
index to a list in publication order. The function signals the work event
after leaving the critical section. `true` accepts one execution, including
when the same object has other pending executions. `false` means all eight
slots were occupied at submission. A slot becomes available again as soon
as dispatch claims it or cancellation removes it. The callback receives its
execution ID when it runs.

The free-slot mask and posted indices let the ISR do a bounded amount of
work without reading the clock, assigning IDs, or accessing either scheduler
heap. Each slot also holds its due time, ID, and admission state, which only
task or loop code accesses under the scheduler mutex. The ISR writes the
task pointer and priority only after reserving the slot. Task or loop code
can read those fields after capturing the slot and before releasing it.

At about 24 bytes per slot, a typical 32-bit build reserves about 192 bytes
per service for slots, plus the mask, indices, and lock and event metadata.
This covers bursts of up to eight pending GPIO or timer requests with the
same memory budget under sustained load.

### Dispatch and queries

Before selecting an execution, take the scheduler mutex and capture the
posted indices in publication order under the ISR-safe critical section.
Clear the posted list while keeping the captured slots occupied. Release
the critical section before reading the clock or examining the scheduler
heaps. Import the captured slots in place, assigning one sampled uptime to
the batch and assigning IDs from the scheduler's existing sequence. A post
after the capture is considered at the next selection.

The first selection pass of `executeEligibleTasksUpToNow()` uses that same
sample for its fixed cutoff. `executeEligibleTasks()` takes a fresh sample at
each selection pass. An explicit cutoff remains the caller's time, clamped
to the current sample. A newly imported request is due at import time, so it
remains pending when that time is later than the cutoff.
[Dispatch APIs](../src/roo_scheduler.h)

In priority-enabled builds, mark imported slots due by the cutoff as ready.
Keep them ready across later selections with an earlier cutoff, just as for
ordinary work. Scan the imported slots for the best ready candidate and
compare it with the ordinary ready-heap head after discarding canceled
heads. Use the same ordering for both candidates.
Higher priority wins. Equal priorities are ordered by due time, then by
execution ID. Apply the existing minimum-priority rule to the combined
selection. In builds with `ROO_SCHEDULER_IGNORE_PRIORITY`, use due time and
execution ID to compare the best eligible slot and the time-heap head,
following the existing cutoff and minimum-priority behavior.

After choosing an ISR entry, record its task and ID for dispatch under the
scheduler mutex, register it as in flight, and release its slot. The callback
runs outside the mutex and counts toward the caller's execution limit.
Import and candidate selection each examine at most eight ISR slots. Neither
operation grows a heap to store ISR requests.

`empty()` and both nearest-work queries check for occupied slots under the
scheduler mutex and the ISR-safe critical section. Any occupied slot makes
the scheduler nonempty and represents immediate work for the queries.
The queries leave the slots untouched.

### Waiting

On the supported builds, `waitForWork()` checks for pending or already-due
work and computes the next deadline. It releases the scheduler mutex
before waiting on the work event. Ordinary insertions and replacements
signal after publishing work. An ISR that posts between the check and the
wait leaves a signal for the dispatcher. A wake returns control to the
caller, which rechecks work before the next wait. This supports `run()`
and explicit loops without executing callbacks inside `waitForWork()`.

Use the same event in `delayUntil()`, bounded by the earlier of its return
deadline and the next scheduled deadline. Keep its opportunistic dispatch
before the return deadline and its priority-filtered final dispatch through
that deadline. Document in the public API that a request can still be
pending when `delayUntil(deadline)` returns if it was first imported after
`deadline`, even when the ISR posted before it.

On builds without ISR support, retain the existing wait paths, including
the raw Pico event path where already available. On supported Pico builds,
the work event also lets a raw processor event wake `waitForWork()`.
[Current wait and completion paths](../src/roo_scheduler.cpp)

### Cancellation and lifetime

Before calling `cancelAndWait(task)`, the caller disables further submissions
of that task, including disabling the relevant interrupt source and
synchronizing with an ISR already in progress. Once the ISR can no longer
post, the task's executions are in its ISR slots, in the ordinary heaps,
or in flight.

Under the scheduler mutex, `cancelAndWait()` removes the task's ordinary
entries and clears its ISR slots. Hold the ISR-safe critical section while
removing matching indices from the posted list and making the slots
available. Preserve the publication order and due times of other requests.
Canceling ISR requests requires no import or heap allocation. The same
scheduler mutex serializes cancellation with import and claiming, and the
existing completion condition variable covers claimed executions.
A successful call permits destruction of the borrowed task. The service
must outlive every producer and dispatch call.

Ordinary submission, queries, cancellation, and dispatch must run on one
core or be externally serialized when the ordinary thread backend uses
a no-op mutex. The ISR-safe critical section permits supported ISR producers
on either core under that same configuration.
[Borrowed task cancellation contract](../src/roo_scheduler.h)

## Proposed API

Expose the method on `Scheduler` when `ROO_THREADS_HAS_ISR_SUPPORT` is `1`.
The method becomes available together with working import, wake, and
cancellation support.

```cpp
/// Requests one borrowed task execution from an ISR.
/// Returns false when all eight ISR request slots are occupied.
/// See SchedulingService::cancelAndWait() for teardown.
bool tryScheduleNowFromIsr(Executable& task,
                           Priority priority = Priority::kNormal) noexcept;
```

A GPIO handler can submit the same task object repeatedly. Construct the
scheduler and task before registering or enabling the ISR. The example
uses peripheral-specific hooks for handling the work and saturation.

```cpp
void ServiceGpio();
void RecordGpioOverflowFromIsr();

class GpioTask final : public roo_scheduler::Executable {
 public:
  /// Services the peripheral in scheduler dispatch context.
  void execute(roo_scheduler::ExecutionID) override { ServiceGpio(); }
};

roo_scheduler::SchedulingService scheduler;
GpioTask gpio_task;

void OnGpioInterrupt() {
  if (!scheduler.tryScheduleNowFromIsr(gpio_task,
                                      roo_scheduler::Priority::kElevated)) {
    RecordGpioOverflowFromIsr();
  }
}
```

## Implementation Plan

Follow the repository's [C++ authoring instructions](../.github/instructions/general-cpp-code-authoring-instructions.md).

### Phase 1. Dispatch wake events

**Proposed commit message** `Use roo_threads wake events for scheduler dispatch waits`

Require the `roo_threads` release that includes both the RP2040 and
FreeRTOS backends in the Bazel, Arduino, and PlatformIO dependency metadata.
Integrate the work event into ordinary scheduling, replacement,
`waitForWork()`, and `delayUntil()`. Keep completion notifications on the
condition variable. Use the `roo_threads` Pico platform dependency for the
event path and keep the platform dependency needed by the existing fallback.
Update the wait documentation. Validate earlier task-context submissions,
idle and timed waits, and Pico raw-event compatibility.

### Phase 2. ISR submission and cancellation

**Proposed commit message** `Schedule borrowed tasks from interrupts using fixed slots`

Add the slots and submission method. Integrate selection, queries, and
cancellation. Document overflow, cutoff, and lifetime semantics and add a
GPIO or timer example. Validate duplicate posts, saturation after import,
capacity reuse after claiming or cancellation, and allocation-free
handling of ISR work. Test mixed ISR and ordinary priorities, including
equal-priority ties and an earlier cutoff after work is already ready.
Cover the no-priority build, dispatch limits, cancellation before import
and during a callback, and a post before `delayUntil()`'s deadline that is
first imported after it. Exercise a simulated FreeRTOS ISR and a Pico alarm
posting just before a wait and during a wait for a later deadline.

## Testing Plan

Cover the existing scheduler, concurrency, allocation, and no-priority
targets. Use manual-time tests for deadlines and the Pico alarm handoff,
and simulated ISR tests for FreeRTOS task wake and dispatch context.
Include an unsupported-backend compile probe that builds ordinary
scheduler clients.
[Scheduler test targets](../BUILD)

## Caveats

The slots absorb a burst while the dispatcher's execution rate governs
sustained throughput. Continued higher-priority work can postpone
lower-priority requests. A caller with a hard interrupt-time deadline
needs a mechanism suited to that deadline.

### Rejected Alternatives

#### Insert directly into the scheduling heap

Direct insertion would give the caller an execution ID at submission time
and reuse a single container. The heap can allocate, and inserting takes
logarithmic time while synchronized with other operations. The fixed
slots keep those costs out of the interrupt handler.

#### Drain an ISR FIFO into the ordinary heaps

A FIFO can reuse the `roo_threads` queue and the existing heap admission
path. If dispatch imports eight entries and claims one, producers can
refill the FIFO while the heap holds seven extra requests. Keeping each
request in its slot gives the service one capacity limit and removes
heap growth from the ISR request path. Scanning eight slots also lets
cancellation remove matching requests in place.

#### Register a pending bit per task

A per-task bit is compact and suits an application that drains device work
after any number of signals. Repeated submissions merge into one callback,
and registration adds a lifetime protocol. The fixed slots give each
accepted post its own execution and use the existing borrowed task
lifetime model.

## Future Work

A subsequent API can carry an explicit interrupt-time timestamp when a
consumer needs an absolute deadline tied to the physical event.
