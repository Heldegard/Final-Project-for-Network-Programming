#include "common/protocol.h"

#include <cstring>

namespace lk {
namespace {

void put_u16(std::vector<uint8_t>* b, uint16_t v) {
    b->push_back(static_cast<uint8_t>(v & 0xFF));
    b->push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void put_u32(std::vector<uint8_t>* b, uint32_t v) {
    for (int i = 0; i < 4; ++i) {
        b->push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

void put_u64(std::vector<uint8_t>* b, uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        b->push_back(static_cast<uint8_t>((v >> (8 * i)) & 0xFF));
    }
}

uint16_t read_u16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t read_u32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t read_u64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = (v << 8) | p[i];
    }
    return v;
}

}  // namespace

void Writer::u8(uint8_t v) { buf_.push_back(v); }

void Writer::u16(uint16_t v) { put_u16(&buf_, v); }

void Writer::u32(uint32_t v) { put_u32(&buf_, v); }

void Writer::u64(uint64_t v) { put_u64(&buf_, v); }

void Writer::raw(const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    buf_.insert(buf_.end(), b, b + n);
}

void Writer::str(const std::string& s) {
    u32(static_cast<uint32_t>(s.size()));
    raw(s.data(), s.size());
}

uint8_t Reader::u8() {
    if (!ok_ || off_ + 1 > n_) {
        ok_ = false;
        return 0;
    }
    return p_[off_++];
}

uint16_t Reader::u16() {
    if (!ok_ || off_ + 2 > n_) {
        ok_ = false;
        return 0;
    }
    uint16_t v = read_u16(p_ + off_);
    off_ += 2;
    return v;
}

uint32_t Reader::u32() {
    if (!ok_ || off_ + 4 > n_) {
        ok_ = false;
        return 0;
    }
    uint32_t v = read_u32(p_ + off_);
    off_ += 4;
    return v;
}

uint64_t Reader::u64() {
    if (!ok_ || off_ + 8 > n_) {
        ok_ = false;
        return 0;
    }
    uint64_t v = read_u64(p_ + off_);
    off_ += 8;
    return v;
}

bool Reader::raw(void* out, size_t n) {
    if (!ok_ || off_ + n > n_) {
        ok_ = false;
        return false;
    }
    if (n > 0) {
        std::memcpy(out, p_ + off_, n);
    }
    off_ += n;
    return true;
}

std::string Reader::str(size_t max_len) {
    uint32_t len = u32();
    if (!ok_ || len > max_len || off_ + len > n_) {
        ok_ = false;
        return {};
    }
    std::string s(reinterpret_cast<const char*>(p_ + off_), len);
    off_ += len;
    return s;
}

void encode_frame(const Frame& frame, std::vector<uint8_t>* out) {
    out->clear();
    out->reserve(kHeaderSize + frame.payload.size());
    put_u16(out, kMagic);
    out->push_back(kProtoVersion);
    out->push_back(static_cast<uint8_t>(frame.type));
    put_u16(out, frame.flags);
    put_u32(out, frame.seq);
    put_u32(out, static_cast<uint32_t>(frame.payload.size()));
    out->insert(out->end(), frame.payload.begin(), frame.payload.end());
}

void FrameDecoder::feed(const uint8_t* data, size_t len) {
    buf_.insert(buf_.end(), data, data + len);
}

bool FrameDecoder::next(Frame* out) {
    if (failed_) {
        return false;
    }
    if (buf_.size() - off_ < kHeaderSize) {
        return false;
    }
    const uint8_t* p = buf_.data() + off_;
    if (read_u16(p) != kMagic) {
        failed_ = true;
        return false;
    }
    uint8_t version = p[2];
    auto type = static_cast<MsgType>(p[3]);
    uint16_t flags = read_u16(p + 4);
    uint32_t seq = read_u32(p + 6);
    uint32_t len = read_u32(p + 10);
    if (version != kProtoVersion || len > kMaxPayload) {
        failed_ = true;
        return false;
    }
    if (buf_.size() - off_ < kHeaderSize + len) {
        return false;
    }
    out->type = type;
    out->flags = flags;
    out->seq = seq;
    out->payload.assign(p + kHeaderSize, p + kHeaderSize + len);
    off_ += kHeaderSize + len;
    if (off_ > 64 * 1024 && off_ * 2 > buf_.size()) {
        buf_.erase(buf_.begin(), buf_.begin() + static_cast<std::ptrdiff_t>(off_));
        off_ = 0;
    }
    return true;
}

void FrameDecoder::reset() {
    buf_.clear();
    off_ = 0;
    failed_ = false;
}

namespace msg {

std::vector<uint8_t> hello(uint32_t caps, const std::string& name) {
    Writer w;
    w.u16(kProtoVersion);
    w.u32(caps);
    w.str(name);
    return w.data();
}

bool parse_hello(const std::vector<uint8_t>& p, uint16_t* proto, uint32_t* caps, std::string* name) {
    Reader r(p.data(), p.size());
    *proto = r.u16();
    *caps = r.u32();
    *name = r.str(256);
    return r.ok();
}

std::vector<uint8_t> screen_info(const ScreenInfo& info) {
    Writer w;
    w.i32(info.rc.x);
    w.i32(info.rc.y);
    w.i32(info.rc.w);
    w.i32(info.rc.h);
    w.u32(info.dpi);
    w.str(info.name);
    return w.data();
}

bool parse_screen_info(const std::vector<uint8_t>& p, ScreenInfo* info) {
    Reader r(p.data(), p.size());
    info->rc.x = r.i32();
    info->rc.y = r.i32();
    info->rc.w = r.i32();
    info->rc.h = r.i32();
    info->dpi = r.u32();
    info->name = r.str(256);
    return r.ok();
}

std::vector<uint8_t> input_batch(uint64_t ts, const InputEvent* events, size_t count) {
    Writer w;
    w.u64(ts);
    w.u16(static_cast<uint16_t>(count));
    for (size_t i = 0; i < count; ++i) {
        const InputEvent& e = events[i];
        w.u8(static_cast<uint8_t>(e.kind));
        w.u8(e.flags);
        w.i16(e.dx);
        w.i16(e.dy);
        w.u32(e.data);
    }
    return w.data();
}

bool parse_input_batch(const std::vector<uint8_t>& p, uint64_t* ts, std::vector<InputEvent>* out) {
    Reader r(p.data(), p.size());
    *ts = r.u64();
    uint16_t count = r.u16();
    if (!r.ok()) {
        return false;
    }
    out->clear();
    out->reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        InputEvent e;
        e.kind = static_cast<InputKind>(r.u8());
        e.flags = r.u8();
        e.dx = r.i16();
        e.dy = r.i16();
        e.data = r.u32();
        if (!r.ok()) {
            return false;
        }
        out->push_back(e);
    }
    return true;
}

std::vector<uint8_t> clipboard_meta(const ClipboardMeta& meta) {
    Writer w;
    w.u8(meta.format);
    w.u32(meta.total);
    w.u32(meta.plain_size);
    w.raw(meta.hash, 32);
    w.u8(meta.encrypted ? 1 : 0);
    w.raw(meta.nonce, 12);
    w.raw(meta.tag, 16);
    return w.data();
}

bool parse_clipboard_meta(const std::vector<uint8_t>& p, ClipboardMeta* meta) {
    Reader r(p.data(), p.size());
    meta->format = r.u8();
    meta->total = r.u32();
    meta->plain_size = r.u32();
    r.raw(meta->hash, 32);
    meta->encrypted = r.u8() != 0;
    r.raw(meta->nonce, 12);
    r.raw(meta->tag, 16);
    return r.ok();
}

std::vector<uint8_t> clipboard_chunk(uint32_t offset, const uint8_t* data, size_t len) {
    Writer w;
    w.u32(offset);
    w.raw(data, len);
    return w.data();
}

bool parse_clipboard_chunk(const std::vector<uint8_t>& p, uint32_t* offset, std::vector<uint8_t>* data) {
    Reader r(p.data(), p.size());
    *offset = r.u32();
    if (!r.ok() || r.remaining() > kMaxPayload) {
        return false;
    }
    data->resize(r.remaining());
    if (!data->empty() && !r.raw(data->data(), data->size())) {
        return false;
    }
    return true;
}

std::vector<uint8_t> file_begin(uint32_t id, uint64_t size, const std::string& name) {
    Writer w;
    w.u32(id);
    w.u64(size);
    w.str(name);
    return w.data();
}

bool parse_file_begin(const std::vector<uint8_t>& p, uint32_t* id, uint64_t* size, std::string* name) {
    Reader r(p.data(), p.size());
    *id = r.u32();
    *size = r.u64();
    *name = r.str(1024);
    return r.ok();
}

std::vector<uint8_t> file_chunk(uint32_t id, uint64_t offset, const uint8_t* data, size_t len) {
    Writer w;
    w.u32(id);
    w.u64(offset);
    w.raw(data, len);
    return w.data();
}

bool parse_file_chunk(const std::vector<uint8_t>& p, uint32_t* id, uint64_t* offset,
                      std::vector<uint8_t>* data) {
    Reader r(p.data(), p.size());
    *id = r.u32();
    *offset = r.u64();
    if (!r.ok() || r.remaining() > kMaxPayload) {
        return false;
    }
    data->resize(r.remaining());
    if (!data->empty() && !r.raw(data->data(), data->size())) {
        return false;
    }
    return true;
}

std::vector<uint8_t> file_end(uint32_t id) {
    Writer w;
    w.u32(id);
    return w.data();
}

bool parse_file_end(const std::vector<uint8_t>& p, uint32_t* id) {
    Reader r(p.data(), p.size());
    *id = r.u32();
    return r.ok();
}

std::vector<uint8_t> file_ack(uint32_t id, uint8_t status, const std::string& message) {
    Writer w;
    w.u32(id);
    w.u8(status);
    w.str(message);
    return w.data();
}

bool parse_file_ack(const std::vector<uint8_t>& p, uint32_t* id, uint8_t* status, std::string* message) {
    Reader r(p.data(), p.size());
    *id = r.u32();
    *status = r.u8();
    *message = r.str(512);
    return r.ok();
}

std::vector<uint8_t> heartbeat(uint64_t ts) {
    Writer w;
    w.u64(ts);
    return w.data();
}

bool parse_heartbeat(const std::vector<uint8_t>& p, uint64_t* ts) {
    Reader r(p.data(), p.size());
    *ts = r.u64();
    return r.ok();
}

std::vector<uint8_t> echo(uint64_t ts) {
    Writer w;
    w.u64(ts);
    return w.data();
}

std::vector<uint8_t> auth_result(bool ok, const std::string& reason) {
    Writer w;
    w.u8(ok ? 1 : 0);
    w.str(reason);
    return w.data();
}

bool parse_auth_result(const std::vector<uint8_t>& p, bool* ok, std::string* reason) {
    Reader r(p.data(), p.size());
    *ok = r.u8() != 0;
    *reason = r.str(512);
    return r.ok();
}

std::vector<uint8_t> bye(uint8_t reason) {
    Writer w;
    w.u8(reason);
    return w.data();
}

}  // namespace msg

}  // namespace lk
