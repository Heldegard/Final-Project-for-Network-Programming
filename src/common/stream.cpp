#include "common/stream.h"

#include <cstring>

namespace lk {

bool FrameStream::read(Frame* out) {
    uint8_t header[kHeaderSize]{};
    if (!recv_all(sock_, header, sizeof(header))) {
        return false;
    }
    uint16_t magic = static_cast<uint16_t>(header[0] | (header[1] << 8));
    if (magic != kMagic || header[2] != kProtoVersion) {
        return false;
    }
    out->type = static_cast<MsgType>(header[3]);
    out->flags = static_cast<uint16_t>(header[4] | (header[5] << 8));
    out->seq = static_cast<uint32_t>(header[6]) | (static_cast<uint32_t>(header[7]) << 8) |
               (static_cast<uint32_t>(header[8]) << 16) | (static_cast<uint32_t>(header[9]) << 24);
    uint32_t len = static_cast<uint32_t>(header[10]) | (static_cast<uint32_t>(header[11]) << 8) |
                   (static_cast<uint32_t>(header[12]) << 16) |
                   (static_cast<uint32_t>(header[13]) << 24);
    if (len > kMaxPayload) {
        return false;
    }
    out->payload.resize(len);
    if (len > 0 && !recv_all(sock_, out->payload.data(), len)) {
        return false;
    }
    return true;
}

}  // namespace lk
