#include <chrono>
#include <istream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <zmq.hpp>

#include "gflags/gflags.h"
#include "glog/logging.h"

#include "packages/filesystem/include/filesystem.h"
#include "packages/hal/proto/camera_sample.pb.h"
#include "packages/image_codec/include/jpeg/jpeg_decoder.h"
#include "packages/serialization/include/proto.h"

int main(int argc, char** argv) {
    gflags::SetUsageMessage("Publish frames over ZMQ");
    gflags::SetVersionString("0.0.1");
    gflags::ParseCommandLineFlags(&argc, &argv, false);

    FLAGS_logtostderr = true;

    if (argc != 3) {
        printf("Usage: image-to-proto INPUT.JPG OUTPUT.PROTODAT\n");
        exit(-1);
    }

    std::string in(argv[1]);
    std::string out(argv[2]);

    // Load image
    hal::Image raw;
    raw.set_data(filesystem::readFileToString(in));
    raw.set_format(hal::PB_COMPRESSED_JPEG);

    hal::Image image;
    image_codec::JpegDecoder decoder;
    if (!decoder.decode(raw, image)) {
        printf("unable to load %s\n", in.c_str());
        exit(-1);
    }

    printf("loaded %s: %dx%d\n", in.c_str(), image.cols(), image.rows());

    // Write to file
    if (!serialization::writeProto(out, image)) {
        printf("unable to write protobuf to %s\n", out.c_str());
        exit(-1);
    }

    printf("wrote protobuf to %s\n", out.c_str());
    return 0;
}
