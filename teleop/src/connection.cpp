#include "packages/teleop/include/connection.h"

#include <chrono>
#include <istream>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "gflags/gflags.h"
#include "glog/logging.h"

#include "websocketpp/client.hpp"
#include "websocketpp/common/memory.hpp"
#include "websocketpp/common/thread.hpp"
#include "websocketpp/config/asio_no_tls_client.hpp"

#include "packages/calibration/proto/system_calibration.pb.h"
#include "packages/hal/proto/camera_sample.pb.h"
#include "packages/serialization/include/proto.h"

#include "packages/perception/proto/detection.pb.h"
#include "packages/teleop/include/encode.h"
#include "packages/teleop/include/receive.h"
#include "packages/teleop/proto/backend_message.pb.h"
#include "packages/teleop/proto/connection_options.pb.h"
#include "packages/teleop/proto/vehicle_message.pb.h"

#include "applications/mercury/proto/connections.pb.h"

namespace teleop {

static const char* kDefaultOptions = "config/global/teleop.pbtxt";

ConnectionOptions loadDefaultOptions() {
    ConnectionOptions opts;
    CHECK(serialization::loadProtoText(kDefaultOptions, &opts));
    return opts;
}

bool populateCalibrationParameters(
    ConnectionOptions* opts, const calibration::SystemCalibration& calib, const panorama::PanoramaCalibration& panoCalib) {
    bool allFound = true;

    // iterate over all sources
    for (uint32_t i = 0; i < (uint32_t)opts->video_sources_size(); i++) {
        auto videoSource = opts->mutable_video_sources(i);
        std::string name = videoSource->camera().device().name();

        bool foundIntrinsics = false;
        bool foundExtrinsics = false;

        // see if it is a panorama camera
        if (mercury::topic_Name(mercury::topic::panorama) == name) {
            videoSource->mutable_camera()->mutable_panorama_intrinsics()->CopyFrom(panoCalib.intrinsics());
            foundIntrinsics = true;

            if (panoCalib.devicetodevicecoordinatetransformation().targetcoordinateframe().device().name() == name) {
                videoSource->mutable_camera()->mutable_extrinsics()->CopyFrom(panoCalib.devicetodevicecoordinatetransformation());
                foundExtrinsics = true;
            }
        } else {
            // see if the camera is in the system calibration file
            for (auto intrinsics : calib.cameraintrinsiccalibration()) {
                if (intrinsics.cameraundercalibration().name() == name) {
                    videoSource->mutable_camera()->mutable_camera_intrinsics()->CopyFrom(intrinsics);
                    foundIntrinsics = true;
                }
            }

            for (auto extrinsics : calib.devicetodevicecoordinatetransformation()) {
                if (extrinsics.targetcoordinateframe().device().name() == name) {
                    videoSource->mutable_camera()->mutable_extrinsics()->CopyFrom(extrinsics);
                    foundExtrinsics = true;
                }
            }
        }

        if (!foundIntrinsics) {
            LOG(ERROR) << "system calibration does not contain intrinsics for " << name;
            allFound = false;
        }

        if (!foundExtrinsics) {
            LOG(ERROR) << "system calibration does not contain extrinsics for " << name;
            allFound = false;
        }
    }

    return allFound;
}

Connection::Connection(const ConnectionOptions& opts)
    : opts_(opts)
    , signaler_(opts.webrtc()) {

    // Sanity-check the options
    CHECK(!opts.backend_address().empty());
    CHECK(!opts.vehicle_id().empty());
    CHECK(!opts.video_sources().empty());
    CHECK_NE(opts.jpeg_quality(), 0);

    // Initialize websocket
    client_.init_asio();
    client_.start_perpetual();
    client_.clear_access_channels(websocketpp::log::alevel::all);

    // Start background thread to service websocket
    thread_.reset(new websocketpp::lib::thread(&client_t::run, &client_));

    // send messages emitted by the signaler over the websocket
    signaler_.OnEmit([&](const VehicleMessage& msg) {
        LOG(INFO) << "sending out message from signaller";
        SendMessage(msg);
    });
}

bool Connection::FindVideoSource(const std::string& name, VideoSource* video) {
    for (const VideoSource& item : opts_.video_sources()) {
        if (item.camera().device().name() == name) {
            video->CopyFrom(item);
            return true;
        }
    }

    // provide backwards compatibility with old frontends
    // TODO: remove this once the frontend is no longer hard-coding camera names
    if (name == "front") {
        return FindVideoSourceByRole(teleop::CameraRole::FrontFisheye, video);
    } else if (name == "rear") {
        return FindVideoSourceByRole(teleop::CameraRole::RearFisheye, video);
    }

    return false;
}

bool Connection::FindVideoSourceByRole(teleop::CameraRole role, VideoSource* video) {
    for (const VideoSource& item : opts_.video_sources()) {
        if (item.camera().role() == role) {
            video->CopyFrom(item);
            return true;
        }
    }

    return false;
}

void Connection::HandleOpen(websocketpp::connection_hdl h) {
    LOG(INFO) << "at HandleOpen";
    Manifest manifest;
    for (auto item : opts_.video_sources()) {
        manifest.add_cameras()->CopyFrom(item.camera());
    }
    SendManifest(manifest);
}

void Connection::HandleFail(websocketpp::connection_hdl h) { LOG(FATAL) << "unable to open websocket connection to backend"; }

void Connection::HandleClose(websocketpp::connection_hdl h) {
    LOG(INFO) << "at HandleClose, reconnecting...";
    Dial();
}

void Connection::HandleMessage(websocketpp::connection_hdl h, client_t::message_ptr buf) {
    BackendMessage msg;
    if (!msg.ParseFromString(buf->get_payload())) {
        LOG(WARNING) << "could not parse message";
        return;
    }

    if (msg.has_joystick() && joystick_handler_) {
        joystick_handler_(msg.joystick());
    }
    if (msg.has_pointandgo() && point_and_go_handler_) {
        point_and_go_handler_(msg.pointandgo());
    }
    if (msg.has_dock_command() && docking_handler_) {
        docking_handler_(msg.dock_command());
    }
    if (msg.has_stop_command() && stop_handler_) {
        stop_handler_(msg.stop_command());
    }
    if (msg.has_errorstateresetcommand() && error_reset_handler_) {
        error_reset_handler_(msg.errorstateresetcommand());
    }
    if (msg.has_turninplace() && turn_in_place_handler_) {
        turn_in_place_handler_(msg.turninplace());
    }
    if (msg.has_reset_exposure() && reset_exposure_handler_) {
        reset_exposure_handler_(msg.reset_exposure());
    }
    if (msg.has_videorequest()) {
        HandleVideoRequest(msg.videorequest());
    }
    if (msg.has_sdprequest()) {
        signaler_.HandleSDPRequest(msg.sdprequest());
    }
    if (msg.has_icecandidate()) {
        signaler_.HandleICECandidate(msg.icecandidate());
    }

    // for now just acknowledge all commands immediately
    if (!msg.id().empty()) {
        LOG(INFO) << "confirming message " << msg.id() << "...";
        SendConfirmation(msg.id(), Confirmation::SUCCESS);
    }
}

void Connection::HandleVideoRequest(const VideoRequest& msg) {
    LOG(INFO) << "\n\nReceived video request for camera " << msg.camera() << "\n\n\n";

    if (opts_.video_sources().empty()) {
        LOG(ERROR) << "ignoring video request because no cameras are registered";
        return;
    }

    // Look up info for the requested camera
    VideoSource video;
    if (!FindVideoSource(msg.camera(), &video)) {
        // If the camera is not found then fall back to using the first camera
        LOG(WARNING) << "camera " << msg.camera() << " not found, falling back to default";
        video.CopyFrom(*opts_.video_sources().begin());
    }

    video.mutable_source()->set_output_width(msg.width());
    video.mutable_source()->set_output_height(msg.height());

    signaler_.HandleVideoRequest(msg.connection_id(), video.source());
}

std::error_code Connection::Dial() {
    using websocketpp::lib::placeholders::_1;
    using websocketpp::lib::placeholders::_2;

    std::string wsurl = opts_.backend_address() + "/api/v1/ws/vehicle/" + opts_.vehicle_id() + "/register";
    LOG(INFO) << "Dialing " << wsurl;

    // Set up connection
    std::error_code err;
    auto conn = client_.get_connection(wsurl, err);
    if (err) {
        LOG(FATAL) << "websocket initialization error: " << err.message();
        return err;
    }

    handle_ = conn->get_handle();

    conn->set_open_handler(websocketpp::lib::bind(&Connection::HandleOpen, this, _1));
    conn->set_fail_handler(websocketpp::lib::bind(&Connection::HandleFail, this, _1));
    conn->set_close_handler(websocketpp::lib::bind(&Connection::HandleClose, this, _1));
    conn->set_message_handler(websocketpp::lib::bind(&Connection::HandleMessage, this, _1, _2));

    // Set up authentication
    if (opts_.auth_token().empty()) {
        LOG(WARNING) << "not sending any auth token to backend";
    } else {
        conn->append_header("Cookie", "auth=" + opts_.auth_token());
    }

    // Connect
    client_.connect(conn);

    return std::error_code();
}

bool Connection::SendMessage(const VehicleMessage& vmsg) {
    // serialize the message
    std::string s;
    vmsg.SerializeToString(&s);
    LOG(INFO) << "serialized VehicleMessage to " << s.size() << " bytes";

    // send the message over the websocket
    std::error_code err;
    client_.send(handle_, s, websocketpp::frame::opcode::binary, err);
    if (err) {
        LOG(WARNING) << "error sending message to websocket: " << err.message();
        return false;
    }
    return true;
}

bool Connection::SendConfirmation(const std::string& msg_id, Confirmation::Status status) {
    VehicleMessage vmsg;
    Confirmation* conf = vmsg.mutable_confirmation();
    conf->set_message_id(msg_id);
    conf->set_status(status);
    return SendMessage(vmsg);
}

bool Connection::SendStillImage(const hal::CameraSample& sample) {
    VehicleMessage vmsg;
    if (!EncodeFrame(vmsg.mutable_frame(), sample.image(), opts_.jpeg_quality())) {
        LOG(WARNING) << "failed to encode frame, discarding";
        return false;
    }
    return SendMessage(vmsg);
}

bool Connection::SendManifest(const Manifest& manifest) {
    VehicleMessage vmsg;
    vmsg.mutable_manifest()->CopyFrom(manifest);
    return SendMessage(vmsg);
}

} // namespace teleop
