#include <chrono>
#include <istream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "gflags/gflags.h"
#include "glog/logging.h"

#include "packages/hal/proto/gps_telemetry.pb.h"
#include "packages/net/include/zmq_topic_pub.h"

DEFINE_string(addr, "tcp://*:15557", "ZMQ socket for gps publisher");
DEFINE_string(topic, "gps", "topic for gps publisher");
DEFINE_double(lat, 33.677222, "initial latitude to publish");
DEFINE_double(lon, -106.475278, "initial longitude to publish");
DEFINE_double(alt, 0., "initial altitude to publish");

int main(int argc, char** argv) {
    gflags::SetUsageMessage("Publish frames over ZMQ");
    gflags::SetVersionString("0.0.1");
    gflags::ParseCommandLineFlags(&argc, &argv, false);

    FLAGS_logtostderr = true;

    zmq::context_t context = zmq::context_t(1);
    LOG(INFO) << "publishing on " << FLAGS_addr << ", topic:" << FLAGS_topic;
    net::ZMQProtobufPublisher<hal::GPSTelemetry> pub(context, FLAGS_addr, 1, 0);

    hal::GPSTelemetry tel;
    tel.set_latitude(FLAGS_lat);
    tel.set_longitude(FLAGS_lon);
    tel.set_altitude(FLAGS_alt);

    while (true) {
        // TODO: perturb lat/lon
        LOG(INFO) << "publishing";
        pub.send(tel, FLAGS_topic);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }

    return 0;
}
