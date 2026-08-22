load("@roo_testing//roo_testing/emulation:arduino.bzl", "roo_arduino_example")

roo_arduino_example(
    name = "one_off",
    sketch = "one_off.ino",
    visibility = ["//visibility:public"],
    deps = ["//:roo_scheduler"],
)
