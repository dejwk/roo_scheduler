load("@roo_testing//roo_testing/emulation:arduino.bzl", "roo_arduino_example")

roo_arduino_example(
    name = "multithreaded",
    sketch = "multithreaded.ino",
    visibility = ["//visibility:public"],
    deps = ["//:roo_scheduler"],
)
