#include "agent/agent.h"

#include <winsock2.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#include <windows.h>

#include "common/clipboard.h"
#include "common/clipxfer.h"
#include "common/crypto.h"
#include "common/inject.h"
#include "common/log.h"
#include "common/net.h"
#include "common/protocol.h"
#include "common/stream.h"

namespace lk {
namespace {

struct Conn : FrameSink {
    Socket sock;
    std::mutex send_mutex;
    std::atomic<uint32_t> seq{1};

    bool send(MsgType type, const std::vector<uint8_t>& payload) override {
        Frame frame;
        frame.type = type;
        frame.seq = seq.fetch_add(1);
        frame.payload = payload;
        std::vector<uint8_t> bytes;
        encode_frame(frame, &bytes);
        std::lock_guard<std::mutex> lock(send_mutex);
        return send_all(sock, bytes.data(), bytes.size());
    }
};

struct InjectJob {
    uint64_t ts = 0;
    std::vector<InputEvent> events;
};

struct FileRecv {
    std::ofstream out;
    std::wstring path;        // final name, only visible once complete
    std::wstring part_path;   // temporary .part file being written
    uint64_t size = 0;
    uint64_t written = 0;
    bool failed = false;
};

uint64_t now_ms() { return GetTickCount64(); }

Rect virtual_screen() {
    Rect rc;
    rc.x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    rc.y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    rc.w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    rc.h = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return rc;
}

std::string computer_name() {
    wchar_t name[256]{};
    DWORD size = static_cast<DWORD>(std::size(name));
    if (!GetComputerNameW(name, &size)) {
        return "agent";
    }
    std::string utf8;
    wide_to_utf8(std::wstring(name, size), &utf8);
    return utf8.empty() ? "agent" : utf8;
}

std::wstring utf8_to_path(const std::string& utf8) {
    std::wstring wide;
    utf8_to_wide(utf8, &wide);
    return wide;
}

std::string base_name(const std::string& utf8_path) {
    std::filesystem::path p(utf8_to_path(utf8_path));
    std::string name;
    wide_to_utf8(p.filename().wstring(), &name);
    if (name.empty() || name == "." || name == "..") {
        name = "received.bin";
    }
    for (char& c : name) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|') {
            c = '_';
        }
    }
    return name;
}

std::string clipboard_digest(const ClipBlob& blob) {
    std::vector<uint8_t> hash = sha256(blob.data);
    return to_hex(hash);
}

}  // namespace

struct Agent::Impl {
    Config cfg;
    Socket listener;
    std::atomic<bool> running{false};
    std::atomic<bool> authed{false};
    std::atomic<uint64_t> last_recv_ms{0};

    std::thread accept_thread;
    std::thread inject_thread;
    std::thread watchdog_thread;
    std::thread clip_thread;
    std::thread apply_thread;
    std::thread clip_send_thread;

    std::mutex conn_mutex;
    std::shared_ptr<Conn> conn;

    Injector injector;

    std::mutex job_mutex;
    std::condition_variable job_cv;
    std::deque<InjectJob> jobs;
    bool jobs_stop = false;

    std::mutex apply_mutex;
    std::condition_variable apply_cv;
    std::deque<ClipBlob> apply_queue;
    bool apply_stop = false;

    std::mutex send_mutex;
    std::condition_variable send_cv;
    std::deque<ClipBlob> send_queue;
    bool send_stop = false;

    std::mutex hash_mutex;
    std::string last_hash;

    std::mutex status_mutex;
    std::string status = "starting";
    NotifyFn notify;

    HWND clip_hwnd = nullptr;
    DWORD clip_thread_id = 0;

    std::shared_ptr<Conn> current_conn() {
        std::lock_guard<std::mutex> lock(conn_mutex);
        return conn;
    }

    void set_status(const std::string& text) {
        std::lock_guard<std::mutex> lock(status_mutex);
        status = text;
        LK_INFO("status: %s", text.c_str());
    }

    void notify_user(const std::string& title, const std::string& text) {
        NotifyFn fn;
        {
            std::lock_guard<std::mutex> lock(status_mutex);
            fn = notify;
        }
        if (fn) {
            fn(title, text);
        }
    }

    bool hash_seen(const std::string& digest) {
        std::lock_guard<std::mutex> lock(hash_mutex);
        return digest == last_hash;
    }

    void remember_hash(const std::string& digest) {
        std::lock_guard<std::mutex> lock(hash_mutex);
        last_hash = digest;
    }

    bool start(std::string* err) {
        std::string listen_err;
        listener = tcp_listen(cfg.port, &listen_err);
        if (!listener.valid()) {
            if (err) {
                *err = listen_err;
            }
            return false;
        }
        running = true;
        set_status("listening on port " + std::to_string(cfg.port));
        accept_thread = std::thread([this] { accept_loop(); });
        inject_thread = std::thread([this] { inject_loop(); });
        watchdog_thread = std::thread([this] { watchdog_loop(); });
        apply_thread = std::thread([this] { apply_loop(); });
        clip_send_thread = std::thread([this] { clip_send_loop(); });
        clip_thread = std::thread([this] { clip_loop(); });
        return true;
    }

    void stop() {
        if (!running.exchange(false)) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(conn_mutex);
            if (conn) {
                conn->sock.shutdown_both();
            }
        }
        listener.close();
        if (clip_hwnd) {
            PostMessageW(clip_hwnd, WM_APP + 7, 0, 0);
        }
        {
            std::lock_guard<std::mutex> lock(job_mutex);
            jobs_stop = true;
        }
        job_cv.notify_all();
        {
            std::lock_guard<std::mutex> lock(apply_mutex);
            apply_stop = true;
        }
        apply_cv.notify_all();
        {
            std::lock_guard<std::mutex> lock(send_mutex);
            send_stop = true;
        }
        send_cv.notify_all();

        if (accept_thread.joinable()) {
            accept_thread.join();
        }
        if (inject_thread.joinable()) {
            inject_thread.join();
        }
        if (watchdog_thread.joinable()) {
            watchdog_thread.join();
        }
        if (apply_thread.joinable()) {
            apply_thread.join();
        }
        if (clip_send_thread.joinable()) {
            clip_send_thread.join();
        }
        if (clip_thread.joinable()) {
            clip_thread.join();
        }
        injector.release_all();
    }

    void accept_loop() {
        while (running) {
            std::string err;
            Socket sock = tcp_accept(listener, &err);
            if (!sock.valid()) {
                if (running) {
                    LK_WARN("accept failed: %s", err.c_str());
                    Sleep(200);
                }
                continue;
            }
            sock.set_nodelay(true);
            sock.set_keepalive(true);
            auto connection = std::make_shared<Conn>();
            connection->sock = std::move(sock);
            {
                std::lock_guard<std::mutex> lock(conn_mutex);
                if (conn) {
                    LK_INFO("replacing previous connection");
                    conn->sock.shutdown_both();
                }
                conn = connection;
            }
            std::thread([this, connection] { serve(connection); }).detach();
        }
    }

    bool handshake(Conn& connection) {
        FrameStream stream(connection.sock);
        Frame frame;

        // Handshake must not hang forever when the peer is not speaking our protocol.
        DWORD timeout = 5000;
        setsockopt(connection.sock.get(), SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));

        if (!stream.read(&frame) || frame.type != MsgType::Hello) {
            LK_WARN("handshake: expected Hello");
            return false;
        }
        uint16_t proto = 0;
        uint32_t caps = 0;
        std::string name;
        if (!msg::parse_hello(frame.payload, &proto, &caps, &name)) {
            return false;
        }
        LK_INFO("hello from '%s' proto=%u caps=0x%x", name.c_str(), proto, caps);

        uint8_t nonce[16]{};
        random_bytes(nonce, sizeof(nonce));
        if (!connection.send(MsgType::AuthChallenge,
                             std::vector<uint8_t>(nonce, nonce + sizeof(nonce)))) {
            return false;
        }
        if (!stream.read(&frame) || frame.type != MsgType::AuthResponse) {
            LK_WARN("handshake: expected AuthResponse");
            return false;
        }
        std::vector<uint8_t> expected = hmac_sha256(psk_key(cfg), nonce, sizeof(nonce));
        if (frame.payload.size() != expected.size() ||
            !constant_time_equal(frame.payload.data(), expected.data(), expected.size())) {
            LK_WARN("handshake: bad pre-shared key from '%s'", name.c_str());
            connection.send(MsgType::AuthResult, msg::auth_result(false, "invalid key"));
            return false;
        }
        connection.send(MsgType::AuthResult, msg::auth_result(true, "ok"));

        ScreenInfo info;
        info.rc = virtual_screen();
        info.dpi = GetDpiForSystem();
        info.name = computer_name();
        connection.send(MsgType::ScreenInfo, msg::screen_info(info));
        LK_INFO("screen: %dx%d at (%d,%d)", info.rc.w, info.rc.h, info.rc.x, info.rc.y);

        timeout = 0;
        setsockopt(connection.sock.get(), SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        authed = true;
        last_recv_ms = now_ms();
        set_status("connected to " + name);
        return true;
    }

    void serve(std::shared_ptr<Conn> connection) {
        if (!handshake(*connection)) {
            connection->sock.shutdown_both();
            {
                std::lock_guard<std::mutex> lock(conn_mutex);
                if (conn == connection) {
                    conn.reset();
                }
            }
            return;
        }

        injector.set_screen(virtual_screen());

        ClipboardReceiver receiver;
        std::map<uint32_t, FileRecv> files;
        FrameStream stream(connection->sock);
        Frame frame;
        while (running && stream.read(&frame)) {
            last_recv_ms = now_ms();
            switch (frame.type) {
                case MsgType::Heartbeat: {
                    uint64_t ts = 0;
                    if (msg::parse_heartbeat(frame.payload, &ts)) {
                        connection->send(MsgType::HeartbeatAck, msg::heartbeat(ts));
                    }
                    break;
                }
                case MsgType::InputBatch: {
                    uint64_t ts = 0;
                    std::vector<InputEvent> events;
                    if (!msg::parse_input_batch(frame.payload, &ts, &events)) {
                        break;
                    }
                    {
                        std::lock_guard<std::mutex> lock(job_mutex);
                        jobs.push_back(InjectJob{ts, std::move(events)});
                    }
                    job_cv.notify_one();
                    break;
                }
                case MsgType::ClipboardMeta: {
                    std::string err;
                    if (!receiver.on_meta(frame.payload, &err)) {
                        LK_WARN("clipboard meta rejected: %s", err.c_str());
                    }
                    break;
                }
                case MsgType::ClipboardChunk: {
                    std::string err;
                    if (!receiver.on_chunk(frame.payload, &err)) {
                        LK_WARN("clipboard chunk rejected: %s", err.c_str());
                        receiver.reset();
                        break;
                    }
                    if (receiver.complete()) {
                        ClipBlob blob;
                        if (receiver.finish(cfg, &blob, &err)) {
                            remember_hash(clipboard_digest(blob));
                            {
                                std::lock_guard<std::mutex> lock(apply_mutex);
                                apply_queue.push_back(std::move(blob));
                            }
                            apply_cv.notify_one();
                        } else {
                            LK_WARN("clipboard receive failed: %s", err.c_str());
                        }
                    }
                    break;
                }
                case MsgType::FileBegin: {
                    uint32_t id = 0;
                    uint64_t size = 0;
                    std::string name;
                    if (!msg::parse_file_begin(frame.payload, &id, &size, &name)) {
                        break;
                    }
                    receive_file_begin(files, id, size, name, *connection);
                    break;
                }
                case MsgType::FileChunk: {
                    uint32_t id = 0;
                    uint64_t offset = 0;
                    std::vector<uint8_t> data;
                    if (!msg::parse_file_chunk(frame.payload, &id, &offset, &data)) {
                        break;
                    }
                    receive_file_chunk(files, id, offset, data);
                    break;
                }
                case MsgType::FileEnd: {
                    uint32_t id = 0;
                    if (!msg::parse_file_end(frame.payload, &id)) {
                        break;
                    }
                    receive_file_end(files, id, *connection);
                    break;
                }
                case MsgType::Bye:
                    LK_INFO("peer said goodbye");
                    running = false;
                    break;
                default:
                    break;
            }
        }

        LK_INFO("connection closed");
        authed = false;
        injector.release_all();
        connection->sock.shutdown_both();
        {
            std::lock_guard<std::mutex> lock(conn_mutex);
            if (conn == connection) {
                conn.reset();
            }
        }
        set_status("waiting for master");
    }

    void receive_file_begin(std::map<uint32_t, FileRecv>& files, uint32_t id, uint64_t size,
                            const std::string& name, Conn& connection) {
        if (files.count(id)) {
            files.erase(id);
        }
        std::filesystem::path dir(utf8_to_path(config_download_dir(cfg)));
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);

        std::string leaf = base_name(name);
        std::filesystem::path target = dir / utf8_to_path(leaf);
        int suffix = 1;
        while (std::filesystem::exists(target)) {
            std::filesystem::path candidate = dir / utf8_to_path(leaf);
            std::wstring stem = candidate.stem().wstring();
            std::wstring ext = candidate.extension().wstring();
            wchar_t buf[64]{};
            std::swprintf(buf, std::size(buf), L" (%d)", suffix++);
            candidate = dir / (stem + buf + ext);
            target = candidate;
        }

        FileRecv recv;
        recv.path = target.wstring();
        recv.part_path = target.wstring() + L".part";
        recv.size = size;
        recv.out.open(std::filesystem::path(recv.part_path),
                      std::ios::binary | std::ios::trunc);
        if (!recv.out.is_open()) {
            LK_ERROR("cannot create %ls", recv.part_path.c_str());
            connection.send(MsgType::FileAck, msg::file_ack(id, 1, "cannot create file"));
            return;
        }
        files[id] = std::move(recv);
        LK_INFO("receiving file #%u '%s' (%llu bytes) into .part", id, leaf.c_str(),
                static_cast<unsigned long long>(size));
    }

    void receive_file_chunk(std::map<uint32_t, FileRecv>& files, uint32_t id, uint64_t offset,
                            const std::vector<uint8_t>& data) {
        auto it = files.find(id);
        if (it == files.end() || it->second.failed) {
            return;
        }
        it->second.out.seekp(static_cast<std::streamoff>(offset));
        it->second.out.write(reinterpret_cast<const char*>(data.data()),
                             static_cast<std::streamsize>(data.size()));
        if (!it->second.out) {
            // Disk full, device removed, permission change: remember the failure
            // so the transfer is not reported as successful.
            LK_ERROR("write failed for file #%u at offset %llu", id,
                     static_cast<unsigned long long>(offset));
            it->second.failed = true;
            return;
        }
        it->second.written += data.size();
    }

    void receive_file_end(std::map<uint32_t, FileRecv>& files, uint32_t id, Conn& connection) {
        auto it = files.find(id);
        if (it == files.end()) {
            connection.send(MsgType::FileAck, msg::file_ack(id, 1, "unknown file id"));
            return;
        }
        it->second.out.close();
        const bool size_ok = it->second.written == it->second.size;
        const bool ok = size_ok && !it->second.failed;
        std::wstring final_path = it->second.path;
        std::wstring part_path = it->second.part_path;
        uint64_t size = it->second.size;
        uint64_t written = it->second.written;
        files.erase(it);

        std::error_code ec;
        if (ok) {
            std::filesystem::remove(std::filesystem::path(final_path), ec);
            ec.clear();
            std::filesystem::rename(std::filesystem::path(part_path),
                                    std::filesystem::path(final_path), ec);
            if (ec) {
                LK_ERROR("cannot rename .part to final name: %s", ec.message().c_str());
            }
        } else {
            std::filesystem::remove(std::filesystem::path(part_path), ec);
        }

        const bool committed = ok && !ec;
        connection.send(MsgType::FileAck,
                        msg::file_ack(id, committed ? 0 : 2,
                                      committed ? "ok" : (size_ok ? "write failed" : "size mismatch")));

        if (committed) {
            char text[512]{};
            std::snprintf(text, sizeof(text), "已保存 %llu KB 文件到下载目录",
                          static_cast<unsigned long long>(size / 1024));
            std::string utf8;
            wide_to_utf8(final_path, &utf8);
            LK_INFO("file saved: %s (%llu bytes)", utf8.c_str(),
                    static_cast<unsigned long long>(size));
            notify_user("LanKVM 收到文件", text);
        } else {
            LK_WARN("file #%u incomplete: %llu of %llu bytes, partial file removed", id,
                    static_cast<unsigned long long>(written),
                    static_cast<unsigned long long>(size));
            notify_user("LanKVM 文件接收失败", "传输中断，未完成的文件已删除，请重试");
        }
    }

    void inject_loop() {
        while (true) {
            InjectJob job;
            {
                std::unique_lock<std::mutex> lock(job_mutex);
                job_cv.wait(lock, [this] { return jobs_stop || !jobs.empty(); });
                if (jobs_stop && jobs.empty()) {
                    return;
                }
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            injector.inject(job.events);
            std::shared_ptr<Conn> connection = current_conn();
            if (connection && job.ts != 0) {
                connection->send(MsgType::Echo, msg::echo(job.ts));
            }
        }
    }

    void watchdog_loop() {
        while (running) {
            Sleep(500);
            if (authed) {
                uint64_t last = last_recv_ms.load();
                if (last != 0 && now_ms() - last > 5000) {
                    LK_WARN("peer timeout, closing connection");
                    std::shared_ptr<Conn> connection = current_conn();
                    if (connection) {
                        connection->sock.shutdown_both();
                    }
                }
            }
        }
    }

    void apply_loop() {
        while (true) {
            ClipBlob blob;
            {
                std::unique_lock<std::mutex> lock(apply_mutex);
                apply_cv.wait(lock, [this] { return apply_stop || !apply_queue.empty(); });
                if (apply_stop && apply_queue.empty()) {
                    return;
                }
                blob = std::move(apply_queue.front());
                apply_queue.pop_front();
            }
            std::string err;
            if (!clipboard_write(blob, &err)) {
                LK_WARN("cannot apply clipboard: %s", err.c_str());
            } else {
                LK_INFO("clipboard applied: format=%u bytes=%zu", blob.format, blob.data.size());
            }
        }
    }

    void clip_send_loop() {
        while (true) {
            ClipBlob blob;
            {
                std::unique_lock<std::mutex> lock(send_mutex);
                send_cv.wait(lock, [this] { return send_stop || !send_queue.empty(); });
                if (send_stop && send_queue.empty()) {
                    return;
                }
                blob = std::move(send_queue.front());
                send_queue.pop_front();
            }
            std::shared_ptr<Conn> connection = current_conn();
            if (!connection) {
                continue;
            }
            std::string err;
            if (!send_clipboard(*connection, cfg, blob, &err)) {
                LK_WARN("clipboard send failed: %s", err.c_str());
            }
        }
    }

    static LRESULT CALLBACK ClipWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        if (msg == WM_CLIPBOARDUPDATE) {
            auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self) {
                self->on_clipboard_changed();
            }
            return 0;
        }
        if (msg == WM_APP + 7) {
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    void on_clipboard_changed() {
        if (!cfg.clipboard || !authed) {
            return;
        }
        ClipBlob blob;
        std::string err;
        if (!clipboard_read(&blob, &err)) {
            return;
        }
        std::string digest = clipboard_digest(blob);
        if (hash_seen(digest)) {
            return;
        }
        remember_hash(digest);
        {
            std::lock_guard<std::mutex> lock(send_mutex);
            send_queue.push_back(std::move(blob));
        }
        send_cv.notify_one();
    }

    void clip_loop() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ClipWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"LanKVM.AgentClipboard";
        RegisterClassExW(&wc);

        HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                    wc.hInstance, nullptr);
        if (!hwnd) {
            LK_ERROR("cannot create clipboard window");
            return;
        }
        clip_hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        AddClipboardFormatListener(hwnd);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        RemoveClipboardFormatListener(hwnd);
        DestroyWindow(hwnd);
        clip_hwnd = nullptr;
    }
};

Agent::Agent(const Config& cfg) : impl_(std::make_unique<Impl>()) { impl_->cfg = cfg; }

Agent::~Agent() { stop(); }

bool Agent::start(std::string* err) { return impl_->start(err); }

void Agent::stop() {
    if (impl_) {
        impl_->stop();
    }
}

bool Agent::running() const { return impl_->running.load(); }

bool Agent::peer_connected() const { return impl_->authed.load(); }

std::string Agent::status_text() const {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    return impl_->status;
}

void Agent::set_notify(NotifyFn fn) {
    std::lock_guard<std::mutex> lock(impl_->status_mutex);
    impl_->notify = std::move(fn);
}

}  // namespace lk
