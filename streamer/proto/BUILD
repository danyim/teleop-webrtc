package(default_visibility = ["//visibility:public"])

load("//protobuf_rules:bundle.bzl", "proto_library_bundle")

proto_library_bundle(
    name = "stream",
    languages = [
        "c++",
        "python",
    ],
    protos = ["stream.proto"],
)

proto_library_bundle(
    name = "signaler_options",
    languages = [
        "c++",
        "python",
    ],
    protos = ["signaler_options.proto"],
)
