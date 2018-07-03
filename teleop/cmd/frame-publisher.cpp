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

#include "packages/net/include/zmq_topic_pub.h"

#include "packages/hal/proto/camera_sample.pb.h"

DEFINE_string(camera_addr, "tcp://*:5556", "ZMQ socket for camera publisher");
DEFINE_string(camera_topic, "camera", "topic for camera publisher");
DEFINE_int32(image_width, 640, "width of image to generate");
DEFINE_int32(image_height, 360, "height of image to generate");
DEFINE_string(format, "rgba", "image format to generate: rgba or luminance");

int main(int argc, char** argv) {
    gflags::SetUsageMessage("Publish frames over ZMQ");
    gflags::SetVersionString("0.0.1");
    gflags::ParseCommandLineFlags(&argc, &argv, false);

    FLAGS_logtostderr = true;

    zmq::context_t context = zmq::context_t(1);
    LOG(INFO) << "publishing on " << FLAGS_camera_addr << ", topic:" << FLAGS_camera_topic;
    net::ZMQProtobufPublisher<hal::CameraSample> pub(context, FLAGS_camera_addr, 1, 0);

    hal::CameraSample sample;
    sample.set_id(123);

    int pixel_depth;
    if (FLAGS_format == "luminance") {
        LOG(INFO) << "generating luminance images";
        pixel_depth = 1;
        sample.mutable_image()->set_format(hal::PB_LUMINANCE);
    } else if (FLAGS_format == "rgb") {
        LOG(INFO) << "generating RGB images";
        pixel_depth = 3;
        sample.mutable_image()->set_format(hal::PB_RGB);
    } else if (FLAGS_format == "rgba") {
        LOG(INFO) << "generating RGBA images";
        pixel_depth = 4;
        sample.mutable_image()->set_format(hal::PB_RGBA);
    } else {
        LOG(FATAL) << "invalid format: " << FLAGS_format;
    }

    int stride = FLAGS_image_width * pixel_depth;

    sample.mutable_image()->set_type(hal::PB_UNSIGNED_BYTE);
    sample.mutable_image()->set_rows(FLAGS_image_height);
    sample.mutable_image()->set_cols(FLAGS_image_width);
    sample.mutable_image()->set_stride(stride);

    std::vector<char> buf(FLAGS_image_width * FLAGS_image_height * pixel_depth);

    LOG(INFO) << "publishing frames of size " << FLAGS_image_width << "x" << FLAGS_image_height << "...";

    int row = 0;
    while (true) {
        std::fill(buf.begin(), buf.end(), '\255');
        std::fill_n(buf.begin() + stride * row, stride, '\0');

        sample.mutable_image()->set_data(buf.data(), buf.size());
        pub.send(sample, FLAGS_camera_topic);

        row = (row + 1) % FLAGS_image_height;
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    return 0;
}
