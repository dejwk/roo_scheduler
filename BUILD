# BUILD file for use with https://github.com/dejwk/roo_testing.

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
