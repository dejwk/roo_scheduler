cc_library(
    name = "roo_scheduler",
    visibility = ["//visibility:public"],
    srcs = glob(
        [
            "src/**/*.cpp",
            "src/**/*.h",
        ],
    ),
    hdrs = ["src/roo_scheduler.h"],
    includes = ["src"],
    deps = [
        "//lib/roo_collections",
        "//lib/roo_threads:testing",
        "//lib/roo_time:core"
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
        "@googletest//:gtest_main",
    ],
)
