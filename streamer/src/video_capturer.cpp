#include "glog/logging.h"

#include "libyuv.h"
#include "webrtc/common_video/libyuv/include/webrtc_libyuv.h"

#include "packages/streamer/include/session.h"
#include "packages/streamer/include/video_capturer.h"
#include "packages/streamer/proto/stream.pb.h"

namespace streamer {

namespace {
    bool ConvertToYUV(const hal::Image& in, uint32_t fourcc, int depth, webrtc::I420Buffer* out) {
        CHECK_GT(in.cols(), 0u);
        CHECK_GT(in.rows(), 0u);
        CHECK_EQ(in.data().size(), size_t(in.cols() * in.rows() * depth));

        const uint8_t* src_frame = reinterpret_cast<const uint8_t*>(in.data().data());
        CHECK_NOTNULL(src_frame);

        // Convert frame from RGBA to YUV
        // Use libyuv directly since WebRTC wrappers don't support RGBA.
        if (libyuv::ConvertToI420( // params for conversion
                src_frame, // input frame
                in.data().size(), // input size
                out->MutableDataY(), // output Y plane
                out->StrideY(), // output Y stride
                out->MutableDataU(), // output U plane
                out->StrideU(), // output U stride
                out->MutableDataV(), // output V plane
                out->StrideV(), // output V stride
                0, // no cropping in x
                0, // no cropping in y
                in.cols(), // input width
                in.rows(), // input height
                in.cols(), // output width (we are not scaling here)
                in.rows(), // output height (we are not scaling here)
                libyuv::kRotate0, // no rotation
                fourcc)
            != 0) {
            LOG(ERROR) << "failed to convert frame to I420";
            return false;
        }

        return true;
    }

    bool ConvertGrayToYUV(const hal::Image& in, webrtc::I420Buffer* out) {
        CHECK_GT(in.cols(), 0);
        CHECK_GT(in.rows(), 0);
        CHECK_EQ(in.data().size(), size_t(in.cols() * in.rows()));

        const uint8_t* src_frame = reinterpret_cast<const uint8_t*>(in.data().data());
        CHECK_NOTNULL(src_frame);

        // Convert frame from RGBA to YUV
        // Use libyuv directly since WebRTC wrappers don't support RGBA.
        if (libyuv::I400ToI420( // params for conversion
                src_frame, // input Y plane
                in.cols(), // input Y stride
                out->MutableDataY(), // output Y plane
                out->StrideY(), // output Y stride
                out->MutableDataU(), // output U plane
                out->StrideU(), // output U stride
                out->MutableDataV(), // output V plane
                out->StrideV(), // output V stride
                in.cols(), // input width
                in.rows() // input height
                )
            != 0) {
            LOG(ERROR) << "failed to convert frame to I420";
            return false;
        }

        return true;
    }
} // namespace

VideoCapturer::VideoCapturer(Session* session)
    : session_(session) {}

VideoCapturer::~VideoCapturer() {}

cricket::CaptureState VideoCapturer::Start(const cricket::VideoFormat& format) {
    LOG(INFO) << "VideoCapturer starting";
    if (capture_state() == cricket::CS_RUNNING) {
        LOG(ERROR) << "Start called when it's already started.";
        return capture_state();
    }

    format_ = format;
    SetCaptureFormat(&format);

    // check that we're not already running
    if (thread_.joinable()) {
        LOG(FATAL) << "VideoCapturer::Start called when video capturer is already running";
    }

    // launch a new thread that polls for frames
    should_continue_ = true;
    thread_ = std::thread(&VideoCapturer::Loop, this);

    return cricket::CS_RUNNING;
}

void VideoCapturer::Stop() {
    LOG(INFO) << "VideoCapturer stopping";
    if (capture_state() == cricket::CS_STOPPED) {
        LOG(ERROR) << "Stop called when it's already stopped.";
        return;
    }

    // signal the thread to stop
    should_continue_ = false;

    // wait for the thread to terminate
    thread_.join();

    SetCaptureFormat(nullptr);
    SetCaptureState(cricket::CS_STOPPED);
}

void VideoCapturer::Loop() {
    while (should_continue_) {
        NextFrame();
    }
}

void VideoCapturer::NextFrame() {
    int output_width;
    int output_height;
    if (!session_->NextFrame(sample_, output_width, output_height)) {
        LOG(WARNING) << "no frame available";
        return;
    }

    if (sample_.image().type() != hal::PB_UNSIGNED_BYTE) {
        LOG(ERROR) << "expected camera sample with type unsigned byte, but got " << sample_.image().type();
        return;
    }

    const int src_width = sample_.image().cols();
    const int src_height = sample_.image().rows();

    // Allocate a new buffer if necessary
    if (!unscaled_ || src_width != unscaled_->width() || src_height != unscaled_->height()) {
        const int stride_y = src_width;
        const int stride_uv = (src_width + 1) / 2;
        unscaled_ = webrtc::I420Buffer::Create(src_width, src_height, stride_y, stride_uv, stride_uv);
        CHECK_NOTNULL(unscaled_.get());
    }

    // Allocate a new buffer if necessary
    if (!scaled_ || output_width != scaled_->width() || output_height != scaled_->height()) {
        const int stride_y = output_width;
        const int stride_uv = (output_width + 1) / 2;
        scaled_ = webrtc::I420Buffer::Create(output_width, output_height, stride_y, stride_uv, stride_uv);
        CHECK_NOTNULL(scaled_.get());
    }

    switch (sample_.image().format()) {
    case hal::PB_LUMINANCE:
        LOG_EVERY_N(INFO, 100) << "received a " << src_width << "x" << src_height << " grayscale frame";
        if (!ConvertGrayToYUV(sample_.image(), unscaled_)) {
            return;
        }
        break;
    case hal::PB_RGBA:
        LOG_EVERY_N(INFO, 100) << "received a " << src_width << "x" << src_height << " RGBA frame";
        if (!ConvertToYUV(sample_.image(), libyuv::FOURCC_RGBA, 4, unscaled_)) {
            return;
        }
        break;
    case hal::PB_RGB:
        LOG_EVERY_N(INFO, 100) << "received a " << src_width << "x" << src_height << " RGB frame";
        if (!ConvertToYUV(sample_.image(), libyuv::FOURCC_RAW, 3, unscaled_)) {
            return;
        }
        break;
    default:
        LOG(ERROR) << "expected camera sample with RGBA or luminance format, but got " << sample_.image().format();
        return;
    }

    // scale the frame if necessary
    auto frame = unscaled_;
    if (src_width != output_width || src_height != output_height) {
        // Use libyuv directly since WebRTC wrappers don't support RGBA
        LOG_EVERY_N(INFO, 100) << "scaling YUV image " << src_width << "x" << src_height << " -> " << output_width << "x" << output_height;
        if (libyuv::I420Scale( // params for scaling
                unscaled_->MutableDataY(), // input Y plane
                unscaled_->StrideY(), // input Y stride
                unscaled_->MutableDataU(), // input U plane
                unscaled_->StrideU(), // input U stride
                unscaled_->MutableDataV(), // input V plane
                unscaled_->StrideV(), // input V stride
                unscaled_->width(), // input width
                unscaled_->height(), // input height
                scaled_->MutableDataY(), // output Y plane
                scaled_->StrideY(), // output Y stride
                scaled_->MutableDataU(), // output U plane
                scaled_->StrideU(), // output U stride
                scaled_->MutableDataV(), // output V plane
                scaled_->StrideV(), // output V stride
                scaled_->width(), // output width
                scaled_->height(), // output height
                libyuv::kFilterBox)
            != 0) {

            LOG(ERROR) << "failed to scale I420 frame";
            return;
        }

        frame = scaled_;
    }

    LOG_EVERY_N(INFO, 100) << "converted image to I420, dispatching a " << frame->width() << "x" << frame->height() << " frame";
    OnFrame(webrtc::VideoFrame(frame, 0, rtc::TimeMillis(), webrtc::kVideoRotation_0), frame->width(), frame->height());
}

bool VideoCapturer::IsRunning() { return capture_state() == cricket::CS_RUNNING; }

bool VideoCapturer::GetPreferredFourccs(std::vector<uint32_t>* fourccs) {
    if (!fourccs) {
        return false;
    }

    // This class does not yet support multiple pixel formats.
    // fourccs->push_back(format_.fourcc);
    return true;
}

bool VideoCapturer::GetBestCaptureFormat(const cricket::VideoFormat& desired, cricket::VideoFormat* best_format) {
    if (!best_format) {
        return false;
    }

    // *best_format = format_;
    return true;
}

bool VideoCapturer::IsScreencast() const { return false; }

} // namespace scy
