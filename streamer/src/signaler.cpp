#include <iostream>
#include <memory>
#include <string>

#include "zmq.hpp"

#include "glog/logging.h"

#include "webrtc/api/audio_codecs/builtin_audio_decoder_factory.h"
#include "webrtc/api/peerconnectionfactoryproxy.h"
#include "webrtc/api/peerconnectioninterface.h"
#include "webrtc/api/peerconnectionproxy.h"
#include "webrtc/modules/audio_coding/codecs/builtin_audio_encoder_factory.h"
#include "webrtc/p2p/base/basicpacketsocketfactory.h"
#include "webrtc/p2p/client/basicportallocator.h"
#include "webrtc/pc/peerconnection.h"

#include "packages/streamer/include/session.h"
#include "packages/streamer/include/signaler.h"
#include "packages/streamer/include/video_capturer.h"
#include "packages/streamer/proto/signaler_options.pb.h"
#include "packages/streamer/proto/stream.pb.h"

namespace streamer {

// SignallerImpl exists to hide the signaller implementation and avoid
// leaking voluminous webrtc headers to other files.
class Signaler::Impl {
public:
    Impl(Signaler* signaler, SignalerOptions opts)
        : m_signaler(signaler)
        , m_ctx(1)
        , m_opts(opts) {
        CHECK_NOTNULL(signaler);

        // Add STUN servers to config
        for (const auto& item : m_opts.stun_servers()) {
            webrtc::PeerConnectionInterface::IceServer stunserver;
            stunserver.urls.push_back("stun:" + item.address());
            m_config.servers.push_back(stunserver);
        }

        // Add TURN servers to config
        for (const auto& item : m_opts.turn_servers()) {
            std::string url = "turn:" + item.address() + "?transport=tcp";
            LOG(INFO) << "adding turn server: " << url;

            webrtc::PeerConnectionInterface::IceServer turnserver;
            turnserver.uri = url;
            turnserver.username = item.username();
            turnserver.password = item.password();
            turnserver.urls.push_back(url);
            turnserver.tls_cert_policy = webrtc::PeerConnectionInterface::kTlsCertPolicyInsecureNoCheck;
            m_config.servers.push_back(turnserver);
        }

        // Setup threads
        m_network_thread = rtc::Thread::CreateWithSocketServer();
        m_worker_thread = rtc::Thread::Create();
        CHECK(m_network_thread->Start()) << "Failed to start webrtc network thread";
        CHECK(m_worker_thread->Start()) << "Failed to start webrtc worker thread";

        m_factory = webrtc::CreatePeerConnectionFactory( // factory params
            m_network_thread.get(), // webrtc networking
            m_worker_thread.get(), // webrtc worker
            rtc::Thread::Current(), // signalling thread
            nullptr, // audio device module (optional)
            nullptr, // video encoder factory (optional)
            nullptr // video decoder factory (optional)
            );
        CHECK_NOTNULL(m_factory.get());

        m_network_manager.reset(new rtc::BasicNetworkManager());
        m_socket_factory.reset(new rtc::BasicPacketSocketFactory(m_network_thread.get()));
    }

    void EmitMessage(const teleop::VehicleMessage& msg) {
        LOG(INFO) << "at SignallerImpl::EmitMessage";
        if (m_signaler->m_emit_handler) {
            m_signaler->m_emit_handler(msg);
        } else {
            LOG(WARNING) << "No emit handler registered with signaler, dropping message";
        }
    }

    void EmitSDPOffer(const teleop::SDPRequest& offer) {
        LOG(INFO) << "at SignallerImpl::EmitSDPOffer";
        teleop::VehicleMessage msg;
        msg.mutable_sdprequest()->CopyFrom(offer);
        EmitMessage(msg);
    }

    void EmitICECandidate(const teleop::ICECandidate& cand) {
        LOG(INFO) << "at SignallerImpl::EmitICECandidate";
        teleop::VehicleMessage msg;
        msg.mutable_icecandidate()->CopyFrom(cand);
        EmitMessage(msg);
    }

    void HandleVideoRequest(const std::string& conn_id, const Stream& source) {
        LOG(INFO) << "\n\nReceived VideoRequest for: " << conn_id << "\n\n\n";

        auto it = m_sessions.find(conn_id);
        if (it == m_sessions.end()) {
            LOG(INFO) << "no session for " << conn_id << " yet, creating new session";
            CreateSession(conn_id, source);
        } else {
            LOG(INFO) << "session for " << conn_id << " already exists, updating video source";
            it->second->Connect(source);
        }
    }

    void CreateSession(const std::string& conn_id, const Stream& source) {
        // Create the session
        auto session = std::make_shared<Session>(conn_id, &m_ctx);
        session->Connect(source);

        session->OnSDPCreated([conn_id, this](webrtc::SessionDescriptionInterface* desc) {
            LOG(INFO) << "created offer for " << conn_id;

            // Create the SDP request
            teleop::SDPRequest offer;
            offer.set_connection_id(conn_id);
            offer.set_status(teleop::Offered);

            // serialize the SDP
            if (!desc->ToString(offer.mutable_sdp())) {
                LOG(ERROR) << "failed to serialize offer SDP for " << conn_id;
                return;
            }

            EmitSDPOffer(offer);
        });

        session->OnIceCandidate([conn_id, this](const webrtc::IceCandidateInterface* candidate) {
            LOG(INFO) << "created ICE candidate for " << conn_id;

            // Create the ICE candidate
            teleop::ICECandidate cand;
            cand.set_connection_id(conn_id);
            cand.set_sdp_mid(candidate->sdp_mid());
            cand.set_sdp_mline_index(candidate->sdp_mline_index());

            // serialize the candidate
            if (!candidate->ToString(cand.mutable_candidate())) {
                LOG(ERROR) << "failed to serialize ICE candidate for " << conn_id;
                return;
            }

            EmitICECandidate(cand);
        });

        session->OnSignalingChange([conn_id, this](webrtc::PeerConnectionInterface::SignalingState new_state) {
            switch (new_state) {
            case webrtc::PeerConnectionInterface::kStable:
                LOG(INFO) << "connection " << conn_id << " is now stable";
                break;
            case webrtc::PeerConnectionInterface::kClosed:
                LOG(INFO) << "connection " << conn_id << " is now closed";
                break;
            case webrtc::PeerConnectionInterface::kHaveLocalOffer:
                LOG(INFO) << "connection " << conn_id << " now has a local offer";
                break;
            case webrtc::PeerConnectionInterface::kHaveRemoteOffer:
                LOG(INFO) << "connection " << conn_id << " now has a remote offer";
                break;
            case webrtc::PeerConnectionInterface::kHaveLocalPrAnswer:
                LOG(INFO) << "connection " << conn_id << " now has a local answer";
                break;
            case webrtc::PeerConnectionInterface::kHaveRemotePrAnswer:
                LOG(INFO) << "connection " << conn_id << " now has a remote answer";
                break;
            }
        });

        // Create video source. Note that CreateVideoSource below takes
        // ownership of the object allocated here.
        LOG(INFO) << "creating video source";
        cricket::VideoCapturer* capturer = new VideoCapturer(session.get());
        CHECK_NOTNULL(capturer);

        // Configure constraints
        session->constraints()->SetMandatoryReceiveAudio(false);
        session->constraints()->SetMandatoryReceiveVideo(false);
        session->constraints()->SetAllowDtlsSctpDataChannels();

        // Create the video track, which owns the capturer
        LOG(INFO) << "creating video track";
        rtc::scoped_refptr<webrtc::VideoTrackInterface> videoTrack(
            m_factory->CreateVideoTrack(conn_id, m_factory->CreateVideoSource(capturer, nullptr)));

        // Create the media stream and attach track
        LOG(INFO) << "creating media stream";
        auto stream = m_factory->CreateLocalMediaStream(conn_id);
        stream->AddTrack(videoTrack);

        // Set up STUN servers
        std::set<rtc::SocketAddress> stunservers;
        for (auto item : m_opts.stun_servers()) {
            rtc::SocketAddress addr;
            CHECK(addr.FromString(item.address())) << "error parsing STUN server address: " << item.address();
            stunservers.insert(addr);
        }

        // Create a port allocator, which we use to restrict the port range
        LOG(INFO) << "UDP port range: " << m_opts.min_udp_port() << "-" << m_opts.max_udp_port();
        auto port_allocator = std::make_unique<cricket::BasicPortAllocator>(m_network_manager.get(), m_socket_factory.get(), stunservers);
        port_allocator->SetPortRange(m_opts.min_udp_port(), m_opts.max_udp_port());

        // Set up TURN servers
        for (auto item : m_opts.turn_servers()) {
            rtc::SocketAddress turnaddr;
            CHECK(turnaddr.FromString(item.address())) << "error parsing TURN server address";
            cricket::RelayServerConfig turnserver(turnaddr, item.username(), item.password(), cricket::PROTO_TCP);
            port_allocator->AddTurnServer(turnserver);
            LOG(INFO) << "added turn server: " << item.address();
        }

        // Create the connection to the peer and add the stream
        LOG(INFO) << "creating connection";
        auto connection = m_factory->CreatePeerConnection( // peer connection
            m_config, // configuration for the connection
            session->constraints(), // constraints for the connection
            std::move(port_allocator), // the port allocator
            nullptr, // certificate generator
            session->observer());
        if (!connection->AddStream(stream)) {
            LOG(ERROR) << "failed to add stream to session";
        }

        // Assign the connection to the session
        session->SetConnection(connection);

        // Initiate the process of creating an offer
        LOG(INFO) << "creating offer";
        session->CreateOffer();

        LOG(INFO) << "adding the session";
        m_sessions[conn_id] = session;

        LOG(INFO) << "HandleVideoRequest done";
    }

    void HandleSDPRequest(const teleop::SDPRequest& msg) {
        LOG(INFO) << "\n\nReceived SDPRequest for: " << msg.connection_id() << "\n\n\n";
        auto it = m_sessions.find(msg.connection_id());
        if (it == m_sessions.end()) {
            LOG(WARNING) << "received SDP request with unknown connection ID: " << msg.connection_id();
            return;
        }

        if (msg.sdp().empty()) {
            LOG(ERROR) << "received SDPRequest with empty sdp";
            return;
        }

        it->second->SetRemoteDescription("answer", msg.sdp());
    }

    void HandleICECandidate(const teleop::ICECandidate& msg) {
        LOG(INFO) << "\n\nReceived ICECandidate for: " << msg.connection_id() << "\n\n\n";
        auto it = m_sessions.find(msg.connection_id());
        if (it == m_sessions.end()) {
            LOG(WARNING) << "received ICE candidate with unknown connection ID: " << msg.connection_id();
            return;
        }

        if (msg.sdp_mline_index() == -1) {
            LOG(WARNING) << "received ICE candidate with mlineindex=-1";
            return;
        }

        if (msg.sdp_mid().empty()) {
            LOG(ERROR) << "received ICE candidate with empty MID";
            return;
        }

        if (msg.candidate().empty()) {
            LOG(ERROR) << "received ICE candidate with empty candidate";
            return;
        }

        it->second->AddIceCandidate(msg.sdp_mid(), msg.sdp_mline_index(), msg.candidate());
    }

private:
    /// Pointer back to the facade
    Signaler* m_signaler;

    /// The zmq context for camera sample sockets
    zmq::context_t m_ctx;

    /// Options for the signaler
    SignalerOptions m_opts;

    /// Map from connection ID to session
    std::map<std::string, std::shared_ptr<Session> > m_sessions;

    /// The webrtc network thread
    std::unique_ptr<rtc::Thread> m_network_thread;

    /// The webrtc worker thread
    std::unique_ptr<rtc::Thread> m_worker_thread;

    /// The webrtc network manager
    std::unique_ptr<rtc::NetworkManager> m_network_manager;

    /// The webrtc socket factory
    std::unique_ptr<rtc::PacketSocketFactory> m_socket_factory;

    /// The webrtc peer factory
    rtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> m_factory;

    /// Configuration for Session connections
    webrtc::PeerConnectionInterface::RTCConfiguration m_config;
};

//
// Signaler
//

Signaler::Signaler(const SignalerOptions& opts)
    : m_impl(new Impl(this, opts)) {

    CHECK_NE(opts.min_udp_port(), 0);
    CHECK_NE(opts.max_udp_port(), 0);
}

Signaler::~Signaler() = default;

void Signaler::HandleVideoRequest(const std::string& conn_id, const Stream& source) {
    // defer to implementation
    m_impl->HandleVideoRequest(conn_id, source);
}

void Signaler::HandleSDPRequest(const teleop::SDPRequest& msg) {
    // defer to implementation
    m_impl->HandleSDPRequest(msg);
}

void Signaler::HandleICECandidate(const teleop::ICECandidate& msg) {
    // defer to implementation
    m_impl->HandleICECandidate(msg);
}

} // namespace scy
