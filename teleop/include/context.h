#pragma once

#include <chrono>

namespace teleop {

/// Context initializes the webrtc system on construction and tears it down on
/// destruction. The lifetime of this instance must enclose the lifetime of
/// all teleop sessions.
class Context {
public:
    /// Initializes the crypto algs required for the webrtc system
    Context();

    /// Tears down the webrtc system
    ~Context();

    /// Process webrtc messages for up to the specified duration
    void ProcessMessages(std::chrono::milliseconds duration);
};
}
