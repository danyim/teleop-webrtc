#include <cstdint>
#include <vector>

#include "packages/teleop/proto/backend_message.pb.h"
#include "packages/teleop/proto/vehicle_message.pb.h"

#include "packages/hal/proto/camera_sample.pb.h"

namespace teleop {

// Encode an image as a jpeg and populate the CompressedImage proto. QUALITY
// is in the range [1,100] where smaller quality means smaller output sizes.
bool EncodeFrame(teleop::CompressedImage* out, const hal::Image& in, int quality);

} // namespace teleop
