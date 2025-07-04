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
        "//lib/roo_collections",
        "//lib/roo_time",
        "//lib/roo_threads",
    ],
)
