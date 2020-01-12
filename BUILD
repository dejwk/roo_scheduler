cc_library(
    name = "roo_scheduler",
    visibility = ["//visibility:public"],
    srcs = glob(
        [
            "**/*.cpp",
            "**/*.h",
        ],
        exclude = ["test/**"],
    ),
    includes = [
        ".",
    ],
    deps = ["//lib/roo_time"],
)

cc_test(
    name = "roo_scheduler_test",
    srcs = [
        "test/roo_scheduler_test.cpp",
    ],
    copts = ["-Iexternal/gtest/include"],
    linkstatic = 1,
    deps = [
        "//lib/roo_scheduler",
        "@gtest//:gtest_main",
    ],
)
