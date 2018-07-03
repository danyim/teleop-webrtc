#include <zmq.hpp>

#include "packages/teleop/proto/backend_message.pb.h"
#include "packages/teleop/proto/vehicle_message.pb.h"

namespace teleop {

// ReceiveProtobuf waits for the next message on the given socket and then
// parses it as a protocol buffer of type PB. The intermediate message data is
// stored in MSG.
bool ReceiveProtobuf(zmq::socket_t& socket, zmq::message_t& msg, google::protobuf::Message& pb);

} // namespace teleop
