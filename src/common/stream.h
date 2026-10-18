#pragma once

#include "common/net.h"
#include "common/protocol.h"

namespace lk {

// Blocking frame reader on top of a stream socket.
class FrameStream {
public:
    explicit FrameStream(const Socket& sock) : sock_(sock) {}
    bool read(Frame* out);

private:
    const Socket& sock_;
};

}  // namespace lk
