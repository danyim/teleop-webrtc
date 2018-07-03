#pragma once

#include <memory>
#include <mutex>

#include "webrtc/api/jsep.h"
#include "webrtc/api/peerconnectioninterface.h"
#include "webrtc/api/test/fakeconstraints.h"
#include "webrtc/p2p/client/basicportallocator.h"

#include "packages/hal/proto/camera_sample.pb.h"
#include "packages/net/include/zmq_topic_sub.h"
#include "packages/streamer/proto/stream.pb.h"

namespace streamer {

class Session {
public:
    /// Events related to the connection
    typedef std::function<void(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream)> AddStreamHandler; ///< since 7f0676
    typedef std::function<void(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream)> RemoveStreamHandler; ///< since 7f0676
    typedef std::function<void(rtc::scoped_refptr<webrtc::DataChannelInterface> stream)> DataChannelHandler; ///< since 7f0676
    typedef std::function<void(const webrtc::IceCandidateInterface* candidate)> IceCandidateHandler;
    typedef std::function<void(webrtc::PeerConnectionInterface::SignalingState new_state)> SignalingChangeHandler;
    typedef std::function<void(webrtc::PeerConnectionInterface::IceConnectionState new_state)> IceConnectionChangeHandler;
    typedef std::function<void(webrtc::PeerConnectionInterface::IceGatheringState new_state)> IceGatheringChangeHandler;
    typedef std::function<void()> RenegotiationNeededHandler;
    typedef std::function<void()> ClosedHandler;

    /// CreateSessionDescriptionObserver
    typedef std::function<void(webrtc::SessionDescriptionInterface* desc)> SDPCreatedHandler;
    typedef std::function<void(const std::string& error)> SDPFailureHandler;

    /// Accessors for event handlers
    inline void OnAddStream(AddStreamHandler h) { m_add_stream_handler = h; }
    inline void OnRemoveStream(RemoveStreamHandler h) { m_remove_stream_handler = h; }
    inline void OnDataChannel(DataChannelHandler h) { m_data_channel_handler = h; }
    inline void OnIceCandidate(IceCandidateHandler h) { m_ice_candidate_handler = h; }
    inline void OnSignalingChange(SignalingChangeHandler h) { m_signaling_change_handler = h; }
    inline void OnIceConnectionChange(IceConnectionChangeHandler h) { m_ice_connection_change_handler = h; }
    inline void OnIceGatheringChange(IceGatheringChangeHandler h) { m_ice_gathering_change_handler = h; }
    inline void OnRenegotiationNeeded(RenegotiationNeededHandler h) { m_renegotiation_needed_handler = h; }
    inline void OnSDPCreated(SDPCreatedHandler h) { m_sdp_created_handler = h; }
    inline void OnSDPFailure(SDPFailureHandler h) { m_sdp_failure_handler = h; }
    inline void OnClosed(ClosedHandler h) { m_closed_handler = h; }

    /// Construct a session with a label (used for logging only)
    Session(const std::string& label, zmq::context_t* ctx);

    /// Default descructor
    virtual ~Session();

    /// Close the Session connection.
    virtual void CloseConnection();

    /// Create the offer SDP tos end to the Session.
    /// No offer should be received after creating the offer.
    /// A call to `recvSDP` with answer is expected in order to initiate the session.
    virtual void CreateOffer();

    /// Receive a remote offer or answer.
    virtual void SetRemoteDescription(const std::string& type, const std::string& sdp);

    /// Receive a remote candidate.
    virtual void AddIceCandidate(const std::string& mid, int mlineindex, const std::string& sdp);

    /// Get the label for this Session
    inline std::string label() const { return m_label; }

    /// Get the constraints for this Session
    webrtc::PeerConnectionObserver* observer();

    /// Get the constraints for this Session
    inline webrtc::FakeConstraints* constraints() { return &m_constraints; }

    /// Set the connection
    inline void SetConnection(rtc::scoped_refptr<webrtc::PeerConnectionInterface> conn) { m_connection = conn; }

    /// Connect changes the video source for this session
    void Connect(const Stream& source);

    /// NextFrame gets the next video frame for this session, or returns false if no frame is available
    bool NextFrame(hal::CameraSample& sample, int& outputWidth, int& outputHeight);

private:
    /// Observer receives webrtc events and routes them to handlers
    class Observer;
    friend class Observer;

    /// The zmq context
    zmq::context_t* m_ctx;

    /// The event observer
    std::unique_ptr<Observer> m_observer;

    /// The connection ID for this Session
    std::string m_label;

    /// The Session connection
    rtc::scoped_refptr<webrtc::PeerConnectionInterface> m_connection;

    /// Constraints on the Session connection
    webrtc::FakeConstraints m_constraints;

    /// Handler for add stream event
    AddStreamHandler m_add_stream_handler;

    /// Handler for remove stream event
    RemoveStreamHandler m_remove_stream_handler;

    /// Handler for data channel event
    DataChannelHandler m_data_channel_handler;

    /// Handler for ICE candidate event
    IceCandidateHandler m_ice_candidate_handler;

    /// Handler for signaling change event
    SignalingChangeHandler m_signaling_change_handler;

    /// Handler for ICE connection change event
    IceConnectionChangeHandler m_ice_connection_change_handler;

    /// Handler for ICE gathering change event
    IceGatheringChangeHandler m_ice_gathering_change_handler;

    /// Handler for renegotiation needed event
    RenegotiationNeededHandler m_renegotiation_needed_handler;

    /// Handler for connection closed event
    ClosedHandler m_closed_handler;

    /// Handler for SDP success event
    SDPCreatedHandler m_sdp_created_handler;

    /// Handler for SDP failure event
    SDPFailureHandler m_sdp_failure_handler;

    /// The socket from which we are currently reading frames
    std::unique_ptr<zmq::socket_t> m_frame_socket;

    /// The next socket that will replace the socket above when we are done
    /// reading the current frame
    std::unique_ptr<zmq::socket_t> m_next_frame_socket;

    /// The mutex protecting access to m_next_frame_socket
    std::mutex m_socket_guard;

    /// Desired output width
    int m_output_width;

    /// Desired output height
    int m_output_height;
};

} // namespace streamer
