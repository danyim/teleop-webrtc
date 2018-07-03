package(default_visibility = ["//visibility:public"])

load("//protobuf_rules:bundle.bzl", "proto_library_bundle")

proto_library_bundle(
    name = "backend_message",
    languages = [
        "c++",
        "python",
    ],
    protos = ["backend_message.proto"],
    deps = [
        ":webrtc",
        "//packages/core/proto:timestamp",
        "//packages/hal/proto:camera_id",
    ],
)

proto_library_bundle(
    name = "vehicle_message",
    languages = [
        "c++",
        "python",
    ],
    protos = ["vehicle_message.proto"],
    deps = [
        ":camera",
        ":webrtc",
        "//packages/core/proto:timestamp",
        "//packages/hal/proto:device",
        "//packages/hal/proto:gps_telemetry",
        "//packages/hal/proto:network_telemetry",
        "//packages/perception/proto:detection",
    ],
)

proto_library_bundle(
    name = "webrtc",
    languages = [
        "c++",
        "python",
    ],
    protos = ["webrtc.proto"],
)

proto_library_bundle(
    name = "camera",
    languages = [
        "c++",
        "python",
    ],
    protos = ["camera.proto"],
    deps = [
        ":webrtc",
        "//packages/calibration/proto:system_calibration",
        "//packages/hal/proto:camera_id",
        "//packages/hal/proto:device",
        "//packages/panorama_stitcher/proto:panorama_calibration",
    ],
)

proto_library_bundle(
    name = "connection_options",
    languages = [
        "c++",
        "python",
    ],
    protos = ["connection_options.proto"],
    deps = [
        ":camera",
        "//packages/streamer/proto:signaler_options",
        "//packages/streamer/proto:stream",
    ],
)
