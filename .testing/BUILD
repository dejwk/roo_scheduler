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
        "@roo_collections//:roo_collections",
        "@roo_time//:core"
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
        "@gtest//:gtest_main",
    ],
)
