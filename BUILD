# BUILD file for use with https://github.com/dejwk/roo_testing.

cc_library(
    name = "roo_scheduler",
    visibility = ["//visibility:public"],
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
    deps = [
        "@roo_collections",
        "@roo_time",
        "@roo_threads",
    ],
)

cc_test(
    name = "roo_scheduler_test",
    srcs = [
        "test/roo_scheduler_test.cpp",
    ],
    includes = ["src"],
    copts = ["-Iexternal/gtest/include"],
    linkstatic = 1,
    deps = [
        ":roo_scheduler",
        "@roo_testing//:arduino_gtest_main",
        "@googletest//:gtest",
    ],
)
