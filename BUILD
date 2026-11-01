# BUILD file for use with https://github.com/dejwk/roo_testing.

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")

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
    deps = [
        "@roo_collections",
        "@roo_threads",
        "@roo_time",
    ],
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
        "@roo_testing//:arduino_gtest_main",
        "@roo_testing//roo_testing/system:manual_time_mode",
    ],
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
        "@roo_testing//:arduino_gtest_main",
    ],
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
        "@roo_testing//:arduino_gtest_main",
    ],
)

cc_test(
    name = "roo_scheduler_allocation_test",
    size = "small",
    srcs = ["test/roo_scheduler_allocation_test.cpp"],
    linkstatic = 1,
    deps = [
        ":roo_scheduler",
        "@googletest//:gtest",
        "@roo_testing//:arduino_gtest_main",
        "@roo_testing//roo_testing/system:manual_time_mode",
    ],
)

cc_library(
    name = "roo_scheduler_without_priority",
    srcs = [
        "src/roo_scheduler.cpp",
        "src/roo_scheduler.h",
    ],
    defines = ["ROO_SCHEDULER_IGNORE_PRIORITY=1"],
    includes = ["src"],
    deps = [
        "@roo_collections",
        "@roo_threads",
        "@roo_time",
    ],
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
        "@roo_testing//:arduino_gtest_main",
        "@roo_testing//roo_testing/system:manual_time_mode",
    ],
)

cc_test(
    name = "bare_metal_dispatch_test",
    size = "small",
    srcs = ["test/bare_metal_dispatch_test.cpp"],
    linkstatic = True,
    target_compatible_with = [
        "@roo_testing//roo_testing/platforms:bare_metal",
        "@roo_testing//roo_testing/platforms:arduino",
    ],
    deps = [
        ":roo_scheduler",
        "@roo_testing//:arduino_gtest_main",
        "@roo_testing//roo_testing/system:manual_time_mode",
        "@roo_testing//roo_testing/system:timer",
    ],
)
