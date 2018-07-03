cc_library(
    name = "streamer",
    srcs = [
        "src/session.cpp",
        "src/signaler.cpp",
        "src/video_capturer.cpp",
    ],
    hdrs = [
        "include/session.h",
        "include/signaler.h",
        "include/video_capturer.h",
    ],
    copts = [
        "-std=c++1y",
        "-pthread",
    ],
    defines = [
        "WEBRTC_POSIX",
    ],
    linkopts = [
        "-lrt",
        "-ldl",
        "-lX11",
        "-lGLU",
    ],
    visibility = ["//visibility:public"],
    deps = [
        "//external:cppzmq",
        "//external:gflags",
        "//external:glog",
        "//external:sourcey",
        "//external:webrtc",
        "//packages/hal/proto:camera_sample",
        "//packages/net",
        "//packages/streamer/proto:signaler_options",
        "//packages/streamer/proto:stream",
        "//packages/teleop/proto:backend_message",
        "//packages/teleop/proto:vehicle_message",
    ],
)
