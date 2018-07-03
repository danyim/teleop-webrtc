#include "scy/logger.h"

#include "webrtc/base/ssladapter.h"
#include "webrtc/base/thread.h"

#include "packages/teleop/include/context.h"

namespace teleop {

Context::Context() {
    // setup the sourcey logger
    scy::Logger::instance().add(new scy::ConsoleChannel("debug", scy::Level::Debug));

    // setup the webrtc environment
    rtc::LogMessage::LogToDebug(rtc::LS_INFO);
    rtc::InitializeSSL();
}

Context::~Context() {
    // tear down the webrtc environment
    rtc::CleanupSSL();
    scy::Logger::destroy();
}

void Context::ProcessMessages(std::chrono::milliseconds duration) { rtc::Thread::Current()->ProcessMessages(duration.count()); }
}
