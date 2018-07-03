#include <mutex>

#include "glog/logging.h"

#include "packages/hal/proto/camera_sample.pb.h"
#include "packages/net/include/zmq_protobuf.h"
#include "packages/streamer/include/session.h"

namespace streamer {

namespace {
    class DummySetSessionDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
    public:
        static DummySetSessionDescriptionObserver* Create() { return new rtc::RefCountedObject<DummySetSessionDescriptionObserver>(); }

        virtual void OnSuccess() override;
        virtual void OnFailure(const std::string& error) override;

    protected:
        DummySetSessionDescriptionObserver() = default;
        ~DummySetSessionDescriptionObserver() = default;
    };
} // anonymous namespace

/// Observer routes events to their handlers
class Session::Observer : public webrtc::PeerConnectionObserver, public webrtc::CreateSessionDescriptionObserver {
public:
    Observer(Session* session)
        : m_session(session) {}

    void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState new_state) {
        if (m_session->m_signaling_change_handler) {
            m_session->m_signaling_change_handler(new_state);
        }
    }

    void OnIceConnectionChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) {
        if (m_session->m_ice_connection_change_handler) {
            m_session->m_ice_connection_change_handler(new_state);
        }
    }

    void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState new_state) {
        if (m_session->m_ice_gathering_change_handler) {
            m_session->m_ice_gathering_change_handler(new_state);
        }
    }

    void OnRenegotiationNeeded() {
        if (m_session->m_renegotiation_needed_handler) {
            m_session->m_renegotiation_needed_handler();
        }
    }

    void OnAddStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
        if (m_session->m_add_stream_handler) {
            m_session->m_add_stream_handler(stream);
        }
    }

    void OnRemoveStream(rtc::scoped_refptr<webrtc::MediaStreamInterface> stream) {
        if (m_session->m_remove_stream_handler) {
            m_session->m_remove_stream_handler(stream);
        }
    }

    void OnDataChannel(rtc::scoped_refptr<webrtc::DataChannelInterface> stream) {
        if (m_session->m_data_channel_handler) {
            m_session->m_data_channel_handler(stream);
        }
    }

    void OnAddStream(webrtc::MediaStreamInterface* stream) { LOG(INFO) << "ignoring deprecated OnAddStream event"; }

    void OnRemoveStream(webrtc::MediaStreamInterface* stream) { LOG(INFO) << "ignoring deprecated OnRemoveStream event"; }

    void OnIceCandidate(const webrtc::IceCandidateInterface* candidate) {
        if (m_session->m_ice_candidate_handler) {
            m_session->m_ice_candidate_handler(candidate);
        }
    }

    void OnSuccess(webrtc::SessionDescriptionInterface* desc) {
        LOG(INFO) << m_session->m_label << ": Set local description";
        m_session->m_connection->SetLocalDescription(DummySetSessionDescriptionObserver::Create(), desc);

        if (m_session->m_sdp_created_handler) {
            m_session->m_sdp_created_handler(desc);
        }
    }

    void OnFailure(const std::string& error) {
        LOG(ERROR) << m_session->m_label << ": On failure: " << error;

        if (m_session->m_sdp_failure_handler) {
            m_session->m_sdp_failure_handler(error);
        }
    }

    int AddRef() const override { return 1; }
    int Release() const override { return 0; }

private:
    Session* m_session;
};

Session::Session(const std::string& label, zmq::context_t* ctx)
    : m_ctx(ctx)
    , m_observer(new Session::Observer(this))
    , m_label(label)
    , m_connection(nullptr) {}

Session::~Session() {
    LOG(INFO) << m_label << ": Destroying";
    if (m_connection) {
        m_connection->Close();
    }
}

void Session::CloseConnection() {
    LOG(INFO) << m_label << ": Closing";

    if (m_connection) {
        m_connection->Close();
        if (m_closed_handler) {
            m_closed_handler();
        }
    }
}

void Session::CreateOffer() {
    CHECK_NOTNULL(m_connection);
    m_connection->CreateOffer(m_observer.get(), &m_constraints);
}

void Session::SetRemoteDescription(const std::string& type, const std::string& sdp) {
    webrtc::SdpParseError error;
    webrtc::SessionDescriptionInterface* desc(webrtc::CreateSessionDescription(type, sdp, &error));
    if (!desc) {
        LOG(ERROR) << "error parsing remote SDP: " << error.description;
        return;
    }

    m_connection->SetRemoteDescription(DummySetSessionDescriptionObserver::Create(), desc);
    if (type == "offer") {
        m_connection->CreateAnswer(m_observer.get(), &m_constraints);
    }
}

void Session::AddIceCandidate(const std::string& mid, int mlineindex, const std::string& sdp) {
    webrtc::SdpParseError error;
    std::unique_ptr<webrtc::IceCandidateInterface> candidate(webrtc::CreateIceCandidate(mid, mlineindex, sdp, &error));
    if (!candidate) {
        LOG(ERROR) << "error parsing remote candidate: " << error.description;
        return;
    }
    m_connection->AddIceCandidate(candidate.get());
}

webrtc::PeerConnectionObserver* Session::observer() { return m_observer.get(); }

void Session::Connect(const Stream& source) {
    // Create the zmq subscriber. This can block on at least one TCP roundtrip,
    // so do not hold the lock while this is happening.
    auto subscriber = std::make_unique<zmq::socket_t>(*m_ctx, ZMQ_SUB);
    subscriber->setsockopt(ZMQ_RCVHWM, 1);
    subscriber->connect(source.address());
    subscriber->setsockopt(ZMQ_SUBSCRIBE, source.topic().c_str(), source.topic().size());

    // Take the socket guard because we are going to overwrite the socket
    std::lock_guard<std::mutex> lock(m_socket_guard);

    // This will be moved to the m_frame_socket when the current frame read is done
    m_next_frame_socket = std::move(subscriber);
    m_output_width = source.output_width();
    m_output_height = source.output_height();
}

bool Session::NextFrame(hal::CameraSample& sample, int& output_width, int& output_height) {
    // If there is a new socket waiting then overwrite the current one with
    // that one. We do things this way to minimize the time that the lock
    // needs to be held. This allows us to update the frame socket without
    // ever blocking on a long operation such as polling a socket or
    // connecting to a socket.
    {
        std::lock_guard<std::mutex> lock(m_socket_guard);
        if (m_next_frame_socket) {
            m_frame_socket = std::move(m_next_frame_socket);
        }
    }

    if (!net::receive(*m_frame_socket, sample, std::chrono::milliseconds(100))) {
        LOG(WARNING) << "timed out while waiting for frame";
        return false;
    }

    output_width = m_output_width;
    output_height = m_output_height;

    return true;
}

//
// Dummy Set Session Description Observer
//

void DummySetSessionDescriptionObserver::OnSuccess() { LOG(INFO) << "SDP parse success"; }

void DummySetSessionDescriptionObserver::OnFailure(const std::string& error) { LOG(ERROR) << "SDP parse error: " << error; }

} // namespace streamer
