#pragma once

#include <atomic>
#include <string>
#include <thread>

#include "packages/hal/proto/camera_sample.pb.h"
#include "packages/streamer/proto/stream.pb.h"

#include "webrtc/api/video/i420_buffer.h"
#include "webrtc/media/base/videocapturer.h"

namespace streamer {

// forward declarations
class Session;

/// VideoCapturer implements cricket::VideoCapturer by converting
/// hal::CameraSample to YUV.
class VideoCapturer : public cricket::VideoCapturer {
public:
    VideoCapturer(Session* session);
    virtual ~VideoCapturer();

    VideoCapturer(const VideoCapturer&) = delete;
    VideoCapturer& operator=(const VideoCapturer&) = delete;

    // cricket::VideoCapturer implementation.
    virtual cricket::CaptureState Start(const cricket::VideoFormat& capture_format) override;
    virtual void Stop() override;
    virtual bool GetPreferredFourccs(std::vector<uint32_t>* fourccs) override;
    virtual bool GetBestCaptureFormat(const cricket::VideoFormat& desired, cricket::VideoFormat* best_format) override;
    virtual bool IsRunning() override;
    virtual bool IsScreencast() const override;

protected:
    // polls for the next frame
    void NextFrame();

    // the poll loop that runs on a separate thread
    void Loop();

    // reference back to the session from which we draw frames
    Session* session_;

    // buffer for storing incoming frames
    hal::CameraSample sample_;

    // the video format
    cricket::VideoFormat format_;

    // the thread on which we read frames
    std::thread thread_;

    // the signal used to stop the thread
    std::atomic_bool should_continue_;

    // the buffer for storing YUV frames at the input size
    rtc::scoped_refptr<webrtc::I420Buffer> unscaled_;

    // the buffer for storing YUV frames after rescaling to the output size
    rtc::scoped_refptr<webrtc::I420Buffer> scaled_;
};

} // namespace streamer
