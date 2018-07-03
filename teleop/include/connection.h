#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include "websocketpp/client.hpp"
#include "websocketpp/common/memory.hpp"
#include "websocketpp/common/thread.hpp"
#include "websocketpp/config/asio_no_tls_client.hpp"

#include "packages/calibration/proto/system_calibration.pb.h"
#include "packages/hal/proto/camera_id.pb.h"
#include "packages/hal/proto/camera_sample.pb.h"

#include "packages/streamer/include/signaler.h"
#include "packages/streamer/proto/stream.pb.h"
#include "packages/teleop/proto/backend_message.pb.h"
#include "packages/teleop/proto/connection_options.pb.h"
#include "packages/teleop/proto/vehicle_message.pb.h"

namespace teleop {

/// Load options from the default location
ConnectionOptions loadDefaultOptions();

/// PopulateCalibrationParameters populates the VideoSources within a
/// ConnectionOptions proto and populates the intrinsic and extrinsic
/// parameters from a SystemCalibration object. It returns true if all cameras
/// were present in the SystemCalibration.
bool populateCalibrationParameters(
    ConnectionOptions* opts, const calibration::SystemCalibration& calib, const panorama::PanoramaCalibration& panoCalib);

// Connection manages the websocket connection to the backend and is
// responsible for sending and receiving messages.
class Connection {
public:
    // the websocket client type
    typedef websocketpp::client<websocketpp::config::asio_client> client_t;

    // the joystick callback
    typedef std::function<void(const JoystickCommand&)> joystick_handler_t;

    // the point-and-go callback
    typedef std::function<void(const PointAndGoCommand&)> point_and_go_handler_t;

    // the docking command callback
    typedef std::function<void(const DockCommand&)> docking_handler_t;

    // the stop command callback
    typedef std::function<void(const StopCommand&)> stop_handler_t;

    // the turn-inplace command callback
    typedef std::function<void(const TurnInPlaceCommand&)> turn_in_place_handler_t;

    // the error-reset command callback
    typedef std::function<void(const ErrorStateResetCommand&)> error_reset_handler_t;

    // the exposure command callback
    typedef std::function<void(const ExposureCommand&)> exposure_handler_t;

    // the reset exposure command callback
    typedef std::function<void(const ResetExposureCommand&)> reset_exposure_handler_t;

    // Create a connection in the disconnected state.
    Connection(const ConnectionOptions& opts);

    // Open a connection to the given websocket URL
    std::error_code Dial();

    // Set the handler to be called when a joystick command arrives.
    inline void OnJoystick(joystick_handler_t handler) { joystick_handler_ = handler; }

    // Set the handler to be called when a point-and-go command arrives.
    inline void OnPointAndGo(point_and_go_handler_t handler) { point_and_go_handler_ = handler; }

    // Set the handler to be called when a point-and-go command arrives.
    inline void OnDockingRequested(docking_handler_t handler) { docking_handler_ = handler; }

    // Set the handler to be called when a stop command arrives.
    inline void OnStopRequested(stop_handler_t handler) { stop_handler_ = handler; }

    // Set the handler to be called when a stop command arrives.
    inline void OnTurnInPlaceRequested(turn_in_place_handler_t handler) { turn_in_place_handler_ = handler; }

    // Set the handler to be called when an error-clearing command is requested
    inline void OnErrorStateReset(error_reset_handler_t handler) { error_reset_handler_ = handler; }

    // Set the handler to be called when an exposure command arrives.
    inline void OnExposure(exposure_handler_t handler) { exposure_handler_ = handler; }

    // Set the handler to be called when an exposure reset command arrives.
    inline void OnResetExposure(reset_exposure_handler_t handler) { reset_exposure_handler_ = handler; }

    // Send a still image to the backend
    bool SendStillImage(const hal::CameraSample& sample);

    // Send template data to the backend, when T is already in protobuf format.
    template <typename T> bool Send(const T& data) {
        VehicleMessage vmsg;
        copyData(vmsg, data);
        return SendMessage(vmsg);
    }

    template <typename T> void copyData(VehicleMessage& vMsg, const T& data) {
        if (std::is_same<T, hal::GPSTelemetry>::value) {
            vMsg.mutable_gps()->CopyFrom(data);
        } else if (std::is_same<T, teleop::DockingObservation>::value) {
            vMsg.mutable_docking_observation()->CopyFrom(data);
        } else if (std::is_same<T, teleop::DockingStatus>::value) {
            vMsg.mutable_docking_status()->CopyFrom(data);
        } else if (std::is_same<T, perception::CameraAlignedBoxDetection>::value) {
            vMsg.mutable_detection()->CopyFrom(data);
        } else if (std::is_same<T, perception::CameraAligned3dBoxDetection>::value) {
            vMsg.mutable_detection3d()->CopyFrom(data);
        } else {
            throw std::runtime_error("Unsupported data type has been sent from vehicle.");
        }
    }

    // Send a confirmation to the backend
    bool SendConfirmation(const std::string& msg_id, Confirmation::Status status);

    // Send a vehicle message to the backend
    bool SendMessage(const VehicleMessage& vmsg);

private:
    // Delete copy constructor and assignment operator
    Connection(Connection&) = delete;
    Connection& operator=(Connection&) = delete;

    // Called by websocket client when connection is opened
    void HandleOpen(websocketpp::connection_hdl h);

    // Called by websocket client when connection fails
    void HandleFail(websocketpp::connection_hdl h);

    // Called by websocket client when connection is closed
    void HandleClose(websocketpp::connection_hdl h);

    // Called by websocket client when a message is received
    void HandleMessage(websocketpp::connection_hdl h, client_t::message_ptr buf);

    // Called when a video request arrives from the backend
    void HandleVideoRequest(const VideoRequest& msg);

    // Send a manifest to the backend
    bool SendManifest(const Manifest& manifest);

    // Find a camera by name, or return false if no camera found
    bool FindVideoSource(const std::string& name, VideoSource* video);

    // Find a camera by role, or return false if no camera found
    bool FindVideoSourceByRole(teleop::CameraRole name, VideoSource* video);

    // Options for this connection
    ConnectionOptions opts_;

    // The webtc signaler (TODO: move to private before merging)
    streamer::Signaler signaler_;

    // The client that owns the websocket resources
    client_t client_;

    // The handle to the connection currently in use
    websocketpp::connection_hdl handle_;

    // The thread running the internal websocket loop
    std::unique_ptr<websocketpp::lib::thread> thread_;

    // The manifest to be sent when the connection opens
    Manifest manifest_;

    // The handler for joystick commands
    joystick_handler_t joystick_handler_;

    // The handler for point-and-go commands
    point_and_go_handler_t point_and_go_handler_;

    // The handler for docking commands
    docking_handler_t docking_handler_;

    // The handler for stop commands
    stop_handler_t stop_handler_;

    // The handler for turn-in-place commands
    turn_in_place_handler_t turn_in_place_handler_;

    // The handler for error reset commands
    error_reset_handler_t error_reset_handler_;

    // The handler for exposure commands
    exposure_handler_t exposure_handler_;

    // The handler for reset exposure commands
    reset_exposure_handler_t reset_exposure_handler_;
};

} // namespace teleop
