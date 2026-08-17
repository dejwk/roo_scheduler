load("@roo_testing//roo_testing/emulation:arduino.bzl", "roo_arduino_example")

roo_arduino_example(
    name = "simple",
    sketch = "simple.ino",
    deps = [
        "//:roo_scheduler",
        "@roo_time",
    ],
)
