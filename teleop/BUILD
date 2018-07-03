cc_library(
    name = "teleop",
    srcs = [
        "src/connection.cpp",
        "src/context.cpp",
        "src/encode.cpp",
        "src/receive.cpp",
    ],
    hdrs = [
        "include/connection.h",
        "include/context.h",
        "include/encode.h",
        "include/receive.h",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//applications/mercury/proto:connections",
        "//external:gflags",
        "//external:glog",
        "//packages/calibration/proto:system_calibration",
        "//packages/core",
        "//packages/hal/proto:camera_id",
        "//packages/hal/proto:camera_sample",
        "//packages/hal/proto:gps_telemetry",
        "//packages/hal/proto:network_telemetry",
        "//packages/image_codec",
        "//packages/net",
        "//packages/serialization",
        "//packages/streamer",
        "//packages/teleop/proto:backend_message",
        "//packages/teleop/proto:connection_options",
        "//packages/teleop/proto:vehicle_message",
        "@jpeg_archive//:jpeg",
        "@png_archive//:png",
        "@websocketpp//:websocketpp",
    ],
)

cc_binary(
    name = "mock-streamer",
    srcs = ["cmd/mock-streamer.cpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//external:gflags",
        "//external:glog",
        "//packages/filesystem",
        "//packages/hal/proto:camera_sample",
        "//packages/image_codec",
        "//packages/net",
        "//packages/teleop",
        "//packages/teleop/proto:backend_message",
        "//packages/teleop/proto:vehicle_message",
    ],
)

cc_binary(
    name = "image-to-proto",
    srcs = ["cmd/image-to-proto.cpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//external:gflags",
        "//external:glog",
        "//packages/filesystem",
        "//packages/hal/proto:camera_sample",
        "//packages/image_codec",
        "//packages/serialization",
    ],
)

cc_binary(
    name = "frame-publisher",
    srcs = ["cmd/frame-publisher.cpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//external:gflags",
        "//external:glog",
        "//packages/hal/proto:camera_sample",
        "//packages/net",
    ],
)

cc_binary(
    name = "gps-publisher",
    srcs = ["cmd/gps-publisher.cpp"],
    visibility = ["//visibility:public"],
    deps = [
        "//external:gflags",
        "//external:glog",
        "//packages/hal/proto:gps_telemetry",
        "//packages/net",
    ],
)
