#include <cctype>
#include <chrono>
#include <string>

#include <zmq.hpp>

#include "gflags/gflags.h"
#include "glog/logging.h"

#include "packages/filesystem/include/filesystem.h"
#include "packages/hal/proto/camera_sample.pb.h"
#include "packages/hal/proto/gps_telemetry.pb.h"
#include "packages/image_codec/include/jpeg/jpeg_decoder.h"
#include "packages/net/include/zmq_protobuf.h"
#include "packages/net/include/zmq_select.h"
#include "packages/serialization/include/proto.h"
#include "packages/streamer/proto/stream.pb.h"
#include "packages/teleop/include/connection.h"
#include "packages/teleop/include/context.h"
#include "packages/teleop/proto/backend_message.pb.h"
#include "packages/teleop/proto/connection_options.pb.h"
#include "packages/teleop/proto/vehicle_message.pb.h"

DEFINE_string(camera_name, "front", "Name for the camera device");
DEFINE_string(camera_addr, "test-pattern", "ZMQ socket for camera publisher, in format 'tcp://host:port'");
DEFINE_string(camera_topic, "camera", "topic for camera publisher");
DEFINE_string(gps_addr, "tcp://localhost:15557", "ZMQ socket for gps publisher, in format 'tcp://host:port'");
DEFINE_string(gps_topic, "gps", "topic for gps publisher");
DEFINE_string(backend, "ws://test.com", "URL for backend, in format 'ws://host:port'");
DEFINE_string(vehicle_id, "r01", "Vehicle ID for connection to backend");
DEFINE_string(auth_token, "", "Authentication token for connection to backend");

int pixelDepthFromFormat(hal::Format format) {
    if (format == hal::PB_LUMINANCE) {
        return 1;
    } else if (format == hal::PB_RGB) {
        return 3;
    } else if (format == hal::PB_RGBA) {
        return 4;
    } else {
        LOG(FATAL) << "invalid image format (only grayscale, rgb, and rgba are supported)";
        return 0;
    }
}

hal::Format parseImageFormat(const std::string& format) {
    if (format == "luminance") {
        return hal::PB_LUMINANCE;
    } else if (format == "rgb") {
        return hal::PB_RGB;
    } else if (format == "rgba") {
        return hal::PB_RGBA;
    } else {
        LOG(FATAL) << "invalid image format (only grayscale, rgb, and rgba are supported)";
        return hal::Format(-1);
    }
}

/// \brief Loop forever, publishing a test pattern on the given socket
void publishTestPattern(zmq::socket_t* socket, const hal::Image& image, const std::string& topic) {
    LOG(INFO) << "publishing still of size " << image.cols() << "x" << image.rows() << "...";

    int pixelDepth = pixelDepthFromFormat(image.format());
    int stride = pixelDepth * image.cols();
    CHECK_EQ(image.rows() * stride, image.data().size());

    hal::CameraSample sample;
    sample.set_id(123);
    sample.mutable_image()->CopyFrom(image);

    std::vector<char> buf(image.data().size());
    for (int row = 0; true; row = (row + 1) % image.rows()) {
        std::copy_n(image.data().data(), image.data().size(), buf.begin());
        std::fill_n(buf.begin() + stride * row, stride, '\255');
        sample.mutable_image()->set_data(buf.data(), buf.size());
        net::send(*socket, sample, topic);
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
    }
}

/// \brief Convert a string to lowercase
std::string tolower(std::string s) {
    // this is recommended by http://en.cppreference.com/w/cpp/string/byte/tolower
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

/// \brief Determine whether one string has another string as a suffix.
bool hasSuffix(const std::string& s, const std::string& suffix) {
    if (s.size() < suffix.size()) {
        return false;
    }
    return std::equal(suffix.rbegin(), suffix.rend(), s.rbegin());
}

/// \brief Get the file extension for the given path
std::string extension(const std::string& path) {
    size_t pos = path.rfind('.');
    if (pos == std::string::npos) {
        return "";
    }
    return path.substr(pos);
}

int main(int argc, char** argv) {
    google::InstallFailureSignalHandler();
    google::InitGoogleLogging(argv[0]);

    gflags::SetUsageMessage("Receive frames over zmq and push to websocket");
    gflags::SetVersionString("0.0.1");
    gflags::ParseCommandLineFlags(&argc, &argv, false);

    FLAGS_logtostderr = true;

    teleop::Context ctx;

    // Start the test pattern publisher if requested
    hal::Image still;
    zmq::context_t context(1);
    zmq::socket_t framePub(context, ZMQ_PUB);
    std::thread testPatternThread;

    std::string cameraAddr = FLAGS_camera_addr;
    std::string ext = tolower(extension(cameraAddr));
    if (ext == ".protodat") {
        if (!serialization::loadProto(cameraAddr, &still)) {
            LOG(FATAL) << "unable to load image from " << cameraAddr;
        }
    } else if (ext == ".jpeg" || ext == ".jpg") {
        image_codec::JpegDecoder decoder;
        hal::Image compressed;
        compressed.set_format(hal::PB_COMPRESSED_JPEG);
        compressed.set_data(filesystem::readFileToString(cameraAddr));
        if (!decoder.decode(compressed, still)) {
            LOG(FATAL) << "unable to load image from " << cameraAddr;
        }
    } else if (cameraAddr == "test-pattern") {
        still.set_type(hal::PB_UNSIGNED_BYTE);
        still.set_format(hal::PB_LUMINANCE);
        still.set_rows(360);
        still.set_cols(640);
        still.set_stride(640);
        std::string img(still.stride() * still.rows(), '\0');
        still.set_data(img.data(), img.size());
    }

    if (!still.data().empty()) {
        cameraAddr = "tcp://127.0.0.1:19879";
        framePub.setsockopt(ZMQ_SNDHWM, 1);
        framePub.setsockopt(ZMQ_LINGER, 0);
        framePub.bind(cameraAddr);
        testPatternThread = std::thread(publishTestPattern, &framePub, still, FLAGS_camera_topic);
        LOG(INFO) << "starting still image thread";
    }

    // Create the connection object
    teleop::ConnectionOptions opts;
    opts.set_backend_address(FLAGS_backend);
    opts.set_vehicle_id(FLAGS_vehicle_id);
    opts.set_auth_token(FLAGS_auth_token);
    opts.set_jpeg_quality(80);
    opts.mutable_webrtc()->set_min_udp_port(52000);
    opts.mutable_webrtc()->set_max_udp_port(53000);

    // Add a front camera
    teleop::VideoSource* frontVideo = opts.add_video_sources();
    frontVideo->mutable_camera()->mutable_device()->set_name(FLAGS_camera_name);
    frontVideo->mutable_camera()->set_role(teleop::CameraRole::FrontFisheye);
    frontVideo->mutable_source()->set_address(cameraAddr);
    frontVideo->mutable_source()->set_topic(FLAGS_camera_topic);
    frontVideo->mutable_source()->set_output_width(640);
    frontVideo->mutable_source()->set_output_height(360);

    // Add a rear camera
    teleop::VideoSource* rearVideo = opts.add_video_sources();
    rearVideo->mutable_camera()->mutable_device()->set_name(FLAGS_camera_name + "2");
    rearVideo->mutable_camera()->set_role(teleop::CameraRole::RearFisheye);
    rearVideo->mutable_source()->set_address(cameraAddr);
    rearVideo->mutable_source()->set_topic(FLAGS_camera_topic);
    rearVideo->mutable_source()->set_output_width(640);
    rearVideo->mutable_source()->set_output_height(360);

    teleop::Connection conn(opts);

    // Open the websocket connection to the backend
    std::error_code err = conn.Dial();
    if (err) {
        LOG(FATAL) << "failed to connect to backend:" << err;
    }

    conn.OnJoystick([](const teleop::JoystickCommand& cmd) {
        LOG(INFO) << "router received a joystick command: " << cmd.linearvelocity() << ", " << cmd.curvature();
    });

    conn.OnPointAndGo([](const teleop::PointAndGoCommand& cmd) {
        LOG(INFO) << "router received a point-and-go command: " << cmd.imagex() << ", " << cmd.imagey();
    });

    // Run the webrtc loop
    LOG(INFO) << "setup done, entering webrtc loop...";
    while (true) {
        // process incoming messages on the webrtc thread.
        ctx.ProcessMessages(std::chrono::milliseconds(10));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    return 0;
}
