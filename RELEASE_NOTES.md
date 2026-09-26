# roo_scheduler 2.2.1

- Upgrade `roo_collections` to 1.4.8, `roo_threads` to 1.2.9, and `roo_time` to 2.0.1; update PlatformIO minimum versions accordingly.
- Upgrade `roo_testing` to 2.3.0.
- Improve ESP32 test tooling with automatic ESP-IDF profile selection for example runs and a helper to test both Arduino and ESP-IDF profiles.

---

# roo_scheduler 2.2.0

- Fixed ready-queue cancellation, pending-work queries, singleton rescheduling, and owned-task cleanup.
- Added thread-safe adapter controls, permanent `shutdown()` methods, and `Scheduler::cancelAndWait()` for safe teardown. Dispatch remains limited to one thread, with nested dispatch supported.
- Defined dispatch timestamps as admission cutoffs: previously admitted tasks remain eligible when later calls use earlier cutoffs.
- Removed temporary allocations when retiring canceled owned tasks and moved their destruction outside scheduler locks.
- Expanded regression, concurrency, and allocation tests; documented threading and lifetime requirements.
- Updated dependencies to `roo_collections` ≥ 1.4.7, `roo_threads` ≥ 1.2.8, and `roo_time` ≥ 2.0.0; upgraded `roo_testing` to 2.1.2, `rules_cc` to 0.2.25, GoogleTest to 1.18.0.bcr.1, and CI dependencies.

---

# [roo_scheduler 2.1.10](https://github.com/dejwk/roo_scheduler/releases/tag/2.1.10)

Published 2026-08-29.

## Added

- Runnable host-emulation targets for all Arduino examples, including `simple`, `one_off`, `start_stop`, and `multithreaded`.
- Documentation for running examples locally with Bazel, e.g. `bazel run //examples/simple`.
- Separate automatic-time scheduler test coverage.

### Changed

- Migrated host testing to `roo_testing` 2.0 ESP32 Arduino profiles.
- Updated CI and centralized AddressSanitizer configuration.
- Updated dependencies:
  - `roo_collections` ≥ 1.4.6
  - `roo_threads` ≥ 1.2.7
  - `roo_time` ≥ 1.4.7
- Corrected `millis()` format specifiers in the simple example for portability.

### Compatibility

- No public scheduler API changes. This release primarily modernizes the test, emulation, and dependency tooling.

---

# [roo_scheduler 2.1.9](https://github.com/dejwk/roo_scheduler/releases/tag/2.1.9)

Published 2026-02-26.

Adopting enum class for better safety. (Compatibility shims left behind).

---

# [roo_scheduler 2.1.8](https://github.com/dejwk/roo_scheduler/releases/tag/2.1.8)

Published 2026-02-25.

* Updated public documentation, converting to the doxygen format.
* Updated dependencies.

**Full Changelog**: https://github.com/dejwk/roo_scheduler/compare/2.1.7...2.1.8

---

# [roo_scheduler 2.1.7](https://github.com/dejwk/roo_scheduler/releases/tag/2.1.7)

Published 2026-01-26.

Fixed tests after changes in Bazel behavior.

**Full Changelog**: https://github.com/dejwk/roo_scheduler/compare/2.1.6...2.1.7

---

# [roo_scheduler 2.1.6](https://github.com/dejwk/roo_scheduler/releases/tag/2.1.6)

Published 2026-01-06.

* Updated dependencies.

**Full Changelog**: https://github.com/dejwk/roo_scheduler/compare/2.1.5...2.1.6

---

# [roo_scheduler 2.1.5](https://github.com/dejwk/roo_scheduler/releases/tag/2.1.5)

Published 2025-11-12.

Updated dependencies.

**Full Changelog**: https://github.com/dejwk/roo_scheduler/compare/2.1.4...2.1.5

---

# [roo_scheduler 2.1.4](https://github.com/dejwk/roo_scheduler/releases/tag/2.1.4)

Published 2025-10-30.

* Better CI, .gitignore.
* Picking up updated dependencies.

**Full Changelog**: https://github.com/dejwk/roo_scheduler/compare/2.1.3...2.1.4

---

# [roo_scheduler 2.1.3](https://github.com/dejwk/roo_scheduler/releases/tag/2.1.3)

Published 2025-10-05.

* Updated dependencies.
* Switching to the new roo_time API (replacing Interval with Duration).

**Full Changelog**: https://github.com/dejwk/roo_scheduler/compare/2.1.2...2.1.3

---

# [roo_scheduler 2.1.2](https://github.com/dejwk/roo_scheduler/releases/tag/2.1.2)

Published 2025-09-27.

* Minor compilation fix: make it possible to call executeEligibleTasksUpTo*() without specifying priority.
* Adding a compile flag to make it possible to disable deprecated functions completely.


---

# [roo_scheduler 2.1.1](https://github.com/dejwk/roo_scheduler/releases/tag/2.1.1)

Published 2025-09-26.

Updated roo_threads dependency to 1.1.2.

---

# [roo_scheduler 2.1.0](https://github.com/dejwk/roo_scheduler/releases/tag/2.1.0)

Published 2025-09-23.

* Added better support for one-off tasks,
* Refactored and extended unit tests,
* Documented the priority better,
* Added new examples,
* Changed the API slightly to improve readability.

**Full Changelog**: https://github.com/dejwk/roo_scheduler/compare/2.0.2...2.1.0

---

# [roo_scheduler 2.0.2](https://github.com/dejwk/roo_scheduler/releases/tag/2.0.2)

Published 2025-07-04.

Refactored threading to roo_threads.

---

# [roo_scheduler 2.0.1](https://github.com/dejwk/roo_scheduler/releases/tag/2.0.1)

Published 2025-03-24.

Fixed test workflows; no new functionality.

---

# [roo_scheduler 2.0.0](https://github.com/dejwk/roo_scheduler/releases/tag/2.0.0)

Published 2024-12-20.

Some major new features:
1. make the implementation thread-safe, so that tasks can be scheduled from background activities / callbacks;
2. added support for task priorities, which dictate the order of execution of tasks that are due or past-due.

---

# [roo_scheduler 1.2.1](https://github.com/dejwk/roo_scheduler/releases/tag/1.2.1)

Published 2024-08-08.

* Added the ability to execute tasks eligible up to the specified deadline.
* Adding the ability to delay (similar to Arduino delay()) but while keeping to execute scheduled work.
* Added Scheduler::run(), which can be used to implement purely event-driven apps.

**Full Changelog**: https://github.com/dejwk/roo_scheduler/compare/1.2.0...1.2.1

---

# [roo_scheduler 1.2.0](https://github.com/dejwk/roo_scheduler/releases/tag/1.2.0)

Published 2024-01-03.

The most important change is the addition of proper cancellation, which allows tasks to be destroyed even if they have pending scheduled executions. To accommodate this, the library now depends on a hashtable provided by roo_collections. (That hashtable has near-zero memory cost if your code does not actually use cancellation).

Additionally, the order of executions is now stable; that is, if you add multiple executions with the exact same timestamp, they will be executed in the order of addition, rather than unspecified order.


---

# [roo_scheduler 1.1.1](https://github.com/dejwk/roo_scheduler/releases/tag/1.1.1)

Published 2023-12-23.

Minor update.

---

# [roo_scheduler 1.1.0](https://github.com/dejwk/roo_scheduler/releases/tag/1.1.0)

Published 2023-11-27.

* Made compliant with Arduino IDE;
* Added examples.

---

# [roo_scheduler 1.0.1](https://github.com/dejwk/roo_scheduler/releases/tag/1.0.1)

Published 2023-06-16.

Initial release.

---

