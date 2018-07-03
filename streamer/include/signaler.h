#pragma once

#include <functional>
#include <memory>

#include "packages/streamer/proto/signaler_options.pb.h"
#include "packages/streamer/proto/stream.pb.h"
#include "packages/teleop/proto/backend_message.pb.h"
#include "packages/teleop/proto/vehicle_message.pb.h"

namespace streamer {

/// Signaler negotiates video streams by communicating with the signaling backend
class Signaler {
public:
    /// The docking command callback
    typedef std::function<void(const teleop::VehicleMessage&)> emit_handler;

    /// Construct an empty signaler
    Signaler(const SignalerOptions& opts);
    virtual ~Signaler();

    /// Set the handler to be called when the signaler emits a message.
    inline void OnEmit(emit_handler handler) { m_emit_handler = handler; }

    /// Called when a VideoRequest message arrives over the websocket
    void HandleVideoRequest(const std::string& conn_id, const Stream& source);

    /// Called when an SDPRequest message arrives over the websocket
    void HandleSDPRequest(const teleop::SDPRequest& msg);

    /// Called when an ICECandidate message arrives over the websocket
    void HandleICECandidate(const teleop::ICECandidate& msg);

private:
    /// Forward declaration of SignallerImpl, which hides the implementation using the pimpl idiom
    class Impl;

    /// The handler for emitted messages
    emit_handler m_emit_handler;

    /// Pointer to implementation (to avoid leaking voluminous webrtc headers)
    std::unique_ptr<Impl> m_impl;
};

} // namespace streamer
