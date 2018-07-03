#include "packages/teleop/include/encode.h"

#include "glog/logging.h"

#include "packages/hal/proto/camera_sample.pb.h"
#include "packages/image_codec/include/jpeg.h"
#include "packages/teleop/proto/backend_message.pb.h"
#include "packages/teleop/proto/vehicle_message.pb.h"

namespace teleop {

bool EncodeFrame(teleop::CompressedImage* out, const hal::Image& in, int quality) {
    int depth;
    core::ImageType type;
    switch (in.format()) {
    case hal::PB_LUMINANCE:
        depth = 1;
        type = core::ImageType::uint8;
        break;
    case hal::PB_RGB:
        depth = 3;
        type = core::ImageType::rgb8;
        break;
    case hal::PB_RGBA:
        depth = 4;
        type = core::ImageType::rgba8;
        break;
    default:
        LOG(INFO) << "EncodeFrame cannot process image with format " << in.format();
        return false;
    }

    std::vector<uint8_t> buf;
    if (!image_codec::encodeJPEG((const uint8_t*)in.data().data(), in.cols(), in.rows(), in.cols() * depth, type, quality, &buf)) {
        return false;
    }

    out->set_width(in.cols());
    out->set_height(in.rows());
    out->set_content(buf.data(), buf.size());
    out->set_encoding(teleop::JPEG);

    return true;
}

} // namespace teleop
