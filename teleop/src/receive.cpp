#include "packages/teleop/include/receive.h"

#include <istream>

#include <zmq.hpp>

#include "packages/teleop/proto/backend_message.pb.h"
#include "packages/teleop/proto/vehicle_message.pb.h"

namespace teleop {

// zmqbuf implements istream by reading bytes from a ZMQ message. It makes it
// possible to parse protobufs from ZMQ messages without copying the
// underlying data.
class zmqbuf : public std::streambuf {
public:
    zmqbuf(zmq::message_t& msg) {
        char* data = reinterpret_cast<char*>(msg.data());
        this->setg(data, data, data + msg.size());
    }
};

// ReceiveProtobuf waits for the next message on the given socket and then
// parses it as a protocol buffer of type PB. The intermediate message data is
// stored in MSG.
bool ReceiveProtobuf(zmq::socket_t& socket, zmq::message_t& msg, google::protobuf::Message& pb) {
    // receive into externally allocated buffer
    if (!socket.recv(&msg, 0)) {
        return false;
    }

    // parse without copying the data
    zmqbuf buf(msg);
    std::istream in(&buf);
    return pb.ParseFromIstream(&in);
}

} // namespace teleop
