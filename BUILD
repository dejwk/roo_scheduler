# BUILD file for use with https://github.com/dejwk/roo_testing.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

TEST_MAIN_DEPS = select({
    "@roo_testing//roo_testing/platforms:is_arduino": ["@roo_testing//:arduino_gtest_main"],
    "@roo_testing//roo_testing/platforms:is_idf": ["@roo_testing//:esp_idf_gtest_main"],
    "@roo_testing//roo_testing/platforms:is_pico": ["@roo_testing//:pico_gtest_main"],
    "//conditions:default": [],
})

config_setting(
    name = "bare_metal_rp2040",
    constraint_values = [
        "@roo_testing//roo_testing/platforms:bare_metal",
        "@roo_testing//roo_testing/platforms:pico_sdk",
        "@roo_testing//roo_testing/platforms:rp2040",
    ],
)

SCHEDULER_DEPS = [
    "@roo_collections",
    "@roo_threads",
    "@roo_time",
] + select({
    ":bare_metal_rp2040": ["@roo_testing//:pico_sdk"],
    "//conditions:default": [],
})

cc_library(
    name = "roo_scheduler",
    srcs = glob(
        [
            "src/**/*.cpp",
            "src/**/*.h",
        ],
        exclude = ["test/**"],
    ),
    includes = [
        "src",
    ],
    visibility = ["//visibility:public"],
    deps = SCHEDULER_DEPS,
)

cc_test(
    name = "roo_scheduler_test",
    srcs = [
        "test/roo_scheduler_test.cpp",
        "test/scheduler_client_test.cpp",
    ],
    copts = ["-Iexternal/gtest/include"],
    includes = ["src"],
    linkstatic = 1,
    deps = [
        ":roo_scheduler",
        "@googletest//:gtest",
        "@roo_testing//roo_testing/system:manual_time_mode",
    ] + TEST_MAIN_DEPS,
)

cc_test(
    name = "roo_scheduler_auto_time_test",
    srcs = [
        "test/roo_scheduler_auto_time_test.cpp",
    ],
    copts = ["-Iexternal/gtest/include"],
    includes = ["src"],
    linkstatic = 1,
    deps = [
        ":roo_scheduler",
        "@googletest//:gtest",
    ] + TEST_MAIN_DEPS,
)

cc_test(
    name = "roo_scheduler_concurrency_test",
    size = "small",
    srcs = ["test/roo_scheduler_concurrency_test.cpp"],
    linkstatic = 1,
    target_compatible_with = select({
        "@roo_testing//roo_testing/platforms:is_bare_metal": ["@platforms//:incompatible"],
        "//conditions:default": [],
    }),
    deps = [
        ":roo_scheduler",
        "@googletest//:gtest",
    ] + TEST_MAIN_DEPS,
)

cc_test(
    name = "roo_scheduler_allocation_test",
    size = "small",
    srcs = ["test/roo_scheduler_allocation_test.cpp"],
    linkstatic = 1,
    deps = [
        ":roo_scheduler",
        "@googletest//:gtest",
        "@roo_testing//roo_testing/system:manual_time_mode",
    ] + TEST_MAIN_DEPS,
)

cc_library(
    name = "roo_scheduler_without_priority",
    srcs = [
        "src/roo_scheduler.cpp",
        "src/roo_scheduler.h",
    ],
    defines = ["ROO_SCHEDULER_IGNORE_PRIORITY=1"],
    includes = ["src"],
    deps = SCHEDULER_DEPS,
)

cc_test(
    name = "roo_scheduler_without_priority_test",
    size = "small",
    srcs = [
        "test/roo_scheduler_allocation_test.cpp",
        "test/scheduler_client_test.cpp",
    ],
    linkstatic = 1,
    deps = [
        ":roo_scheduler_without_priority",
        "@googletest//:gtest",
        "@roo_testing//roo_testing/system:manual_time_mode",
    ] + TEST_MAIN_DEPS,
)

cc_test(
    name = "bare_metal_dispatch_test",
    size = "small",
    srcs = ["test/bare_metal_dispatch_test.cpp"],
    linkstatic = True,
    target_compatible_with = [
        "@roo_testing//roo_testing/platforms:bare_metal",
        "@roo_testing//roo_testing/platforms:pico_sdk",
        "@roo_testing//roo_testing/platforms:rp2040",
    ],
    deps = [
        ":roo_scheduler",
        "@googletest//:gtest",
        "@roo_testing//:pico_sdk",
        "@roo_testing//roo_testing/system:manual_time_mode",
        "@roo_testing//roo_testing/system:timer",
    ] + TEST_MAIN_DEPS,
)

cc_test(
    name = "roo_scheduler_isr_freertos_test",
    size = "small",
    srcs = ["test/roo_scheduler_isr_freertos_test.cpp"],
    linkstatic = True,
    target_compatible_with = ["@roo_testing//roo_testing/platforms:esp_idf"],
    deps = [
        ":roo_scheduler",
        "@googletest//:gtest",
        "@roo_testing//roo_testing/interrupts:interrupt_controller",
    ] + TEST_MAIN_DEPS,
)

[
    cc_test(
        name = scheduler + "_isr_pico_test",
        size = "small",
        srcs = ["test/roo_scheduler_isr_pico_test.cpp"],
        linkstatic = True,
        target_compatible_with = [
            "@roo_testing//roo_testing/platforms:bare_metal",
            "@roo_testing//roo_testing/platforms:pico_sdk",
            "@roo_testing//roo_testing/platforms:rp2040",
        ],
        deps = [
            ":" + scheduler,
            "@googletest//:gtest",
            "@roo_testing//:pico_sdk",
            "@roo_testing//roo_testing/system:manual_time_mode",
        ] + TEST_MAIN_DEPS,
    )
    for scheduler in ["roo_scheduler", "roo_scheduler_without_priority"]
]

cc_library(
    name = "unsupported_isr_client_probe",
    testonly = True,
    srcs = ["test/unsupported_isr_client_probe.cpp"],
    deps = [":roo_scheduler"],
)
