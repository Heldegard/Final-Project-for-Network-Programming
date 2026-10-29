#include "master/master.h"

#include <winsock2.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <windows.h>

#include "common/clipboard.h"
#include "common/clipxfer.h"
#include "common/crypto.h"
#include "common/log.h"
#include "common/net.h"
#include "common/protocol.h"
#include "common/stream.h"
#include "master/dropzone.h"

namespace lk {
namespace {

std::atomic<void*> g_instance{nullptr};

uint64_t now_us() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f{};
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<uint64_t>(counter.QuadPart) * 1000000ull /
           static_cast<uint64_t>(freq.QuadPart);
}

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
        return "master";
    }
    std::string utf8;
    wide_to_utf8(std::wstring(name, size), &utf8);
    return utf8.empty() ? "master" : utf8;
}

std::string clipboard_digest(const ClipBlob& blob) { return to_hex(sha256(blob.data)); }

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

}  // namespace

struct Master::Impl {
    Config cfg;

    std::atomic<bool> running{false};
    std::atomic<bool> connected{false};
    std::atomic<bool> remote{false};
    std::atomic<bool> connect_enabled{false};
    std::atomic<int64_t> latency_us{-1};
    std::atomic<uint64_t> last_recv_ms{0};

    HWND hwnd_ = nullptr;
    DWORD ui_thread_id_ = 0;

    HHOOK keyboard_hook_ = nullptr;
    HHOOK mouse_hook_ = nullptr;

    Rect local_rect_{};
    Rect agent_rect_{};
    std::mutex rect_mutex_;
    POINT remote_pos_{};
    Edge edge_ = Edge::Right;
    std::set<uint32_t> pressed_keys_;

    std::thread ui_thread_;
    std::thread connect_thread_;
    std::thread sender_thread_;
    std::thread heartbeat_thread_;
    std::thread watchdog_thread_;
    std::thread clip_send_thread_;
    std::thread apply_thread_;

    std::mutex conn_mutex_;
    std::shared_ptr<Conn> conn_;

    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::deque<InputEvent> queue_;
    bool queue_stop_ = false;

    std::mutex clip_send_mutex_;
    std::condition_variable clip_send_cv_;
    std::deque<ClipBlob> clip_send_queue_;
    bool clip_send_stop_ = false;

    std::mutex clip_apply_mutex_;
    std::condition_variable clip_apply_cv_;
    std::deque<ClipBlob> clip_apply_queue_;
    bool clip_apply_stop_ = false;

    std::mutex hash_mutex_;
    std::string last_hash_;

    std::mutex status_mutex_;
    std::string status_ = "starting";
    NotifyFn notify_;

    DropZone dropzone_;
    std::atomic<uint32_t> next_file_id_{1};

    // ---------------------------------------------------------------- helpers

    std::shared_ptr<Conn> current_conn() {
        std::lock_guard<std::mutex> lock(conn_mutex_);
        return conn_;
    }

    void set_status(const std::string& text) {
        std::lock_guard<std::mutex> lock(status_mutex_);
        status_ = text;
        LK_INFO("status: %s", text.c_str());
    }

    std::string status_text() {
        std::lock_guard<std::mutex> lock(status_mutex_);
        return status_;
    }

    void notify_user(const std::string& title, const std::string& text) {
        NotifyFn fn;
        {
            std::lock_guard<std::mutex> lock(status_mutex_);
            fn = notify_;
        }
        if (fn) {
            fn(title, text);
        }
    }

    bool hash_seen(const std::string& digest) {
        std::lock_guard<std::mutex> lock(hash_mutex_);
        return digest == last_hash_;
    }

    void remember_hash(const std::string& digest) {
        std::lock_guard<std::mutex> lock(hash_mutex_);
        last_hash_ = digest;
    }

    void enqueue(const InputEvent& event) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (queue_.size() > 4096) {
                return;
            }
            queue_.push_back(event);
        }
        queue_cv_.notify_one();
    }

    void enqueue_move() {
        Rect agent;
        {
            std::lock_guard<std::mutex> lock(rect_mutex_);
            agent = agent_rect_;
        }
        if (agent.empty()) {
            return;
        }
        InputEvent event;
        event.kind = InputKind::MouseMove;
        event.data = (static_cast<uint32_t>(norm_x(remote_pos_.x, agent)) << 16) |
                     static_cast<uint32_t>(norm_y(remote_pos_.y, agent));
        enqueue(event);
    }

    void enqueue_button(MouseButton button, bool down) {
        InputEvent event;
        event.kind = InputKind::MouseButton;
        event.flags = static_cast<uint8_t>(button);
        event.data = down ? 1u : 0u;
        enqueue(event);
    }

    void enqueue_wheel(int32_t delta) {
        InputEvent event;
        event.kind = InputKind::MouseWheel;
        event.data = static_cast<uint32_t>(delta);
        enqueue(event);
    }

    void enqueue_key(uint32_t packed, bool down) {
        InputEvent event;
        event.kind = down ? InputKind::KeyDown : InputKind::KeyUp;
        event.data = packed;
        enqueue(event);
        if (down) {
            pressed_keys_.insert(packed);
        } else {
            pressed_keys_.erase(packed);
        }
    }

    // ---------------------------------------------------------- edge switching

    void install_hooks() {
        if (!keyboard_hook_) {
            keyboard_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &Impl::keyboard_proc,
                                               GetModuleHandleW(nullptr), 0);
            if (!keyboard_hook_) {
                LK_WARN("cannot install keyboard hook (err=%lu)", GetLastError());
            }
        }
        if (!mouse_hook_) {
            mouse_hook_ = SetWindowsHookExW(WH_MOUSE_LL, &Impl::mouse_proc,
                                            GetModuleHandleW(nullptr), 0);
            if (!mouse_hook_) {
                LK_WARN("cannot install mouse hook (err=%lu)", GetLastError());
            }
        }
    }

    void uninstall_hooks() {
        if (keyboard_hook_) {
            UnhookWindowsHookEx(keyboard_hook_);
            keyboard_hook_ = nullptr;
        }
        if (mouse_hook_) {
            UnhookWindowsHookEx(mouse_hook_);
            mouse_hook_ = nullptr;
        }
    }

    void try_enter(POINT cursor, LONG dx, LONG dy) {
        if (!connected) {
            return;
        }
        bool at_edge = false;
        switch (edge_) {
            case Edge::Right:
                at_edge = cursor.x >= local_rect_.right() - 1 && dx > 0;
                break;
            case Edge::Left:
                at_edge = cursor.x <= local_rect_.x && dx < 0;
                break;
            case Edge::Top:
                at_edge = cursor.y <= local_rect_.y && dy < 0;
                break;
            case Edge::Bottom:
                at_edge = cursor.y >= local_rect_.bottom() - 1 && dy > 0;
                break;
        }
        if (at_edge) {
            enter_remote(cursor);
        }
    }

    void enter_remote(POINT cursor) {
        Rect agent;
        {
            std::lock_guard<std::mutex> lock(rect_mutex_);
            agent = agent_rect_;
        }
        if (agent.empty()) {
            return;
        }
        double t = 0.0;
        switch (edge_) {
            case Edge::Right:
            case Edge::Left:
                t = static_cast<double>(cursor.y - local_rect_.y) /
                    std::max(1, local_rect_.h);
                break;
            case Edge::Top:
            case Edge::Bottom:
                t = static_cast<double>(cursor.x - local_rect_.x) /
                    std::max(1, local_rect_.w);
                break;
        }
        t = std::clamp(t, 0.0, 1.0);

        switch (edge_) {
            case Edge::Right:
                remote_pos_.x = agent.x;
                remote_pos_.y = agent.y + static_cast<LONG>(t * (agent.h - 1));
                break;
            case Edge::Left:
                remote_pos_.x = agent.right() - 1;
                remote_pos_.y = agent.y + static_cast<LONG>(t * (agent.h - 1));
                break;
            case Edge::Top:
                remote_pos_.y = agent.y;
                remote_pos_.x = agent.x + static_cast<LONG>(t * (agent.w - 1));
                break;
            case Edge::Bottom:
                remote_pos_.y = agent.bottom() - 1;
                remote_pos_.x = agent.x + static_cast<LONG>(t * (agent.w - 1));
                break;
        }

        remote = true;
        pressed_keys_.clear();
        install_hooks();
        enqueue_move();
        set_status("remote control active");
    }

    void exit_remote() {
        if (!remote.exchange(false)) {
            return;
        }
        uninstall_hooks();

        for (uint32_t packed : pressed_keys_) {
            InputEvent event;
            event.kind = InputKind::KeyUp;
            event.data = packed;
            enqueue(event);
        }
        pressed_keys_.clear();

        Rect agent;
        {
            std::lock_guard<std::mutex> lock(rect_mutex_);
            agent = agent_rect_;
        }
        if (agent.empty()) {
            set_status("local");
            return;
        }

        POINT p{};
        switch (edge_) {
            case Edge::Right:
            case Edge::Left: {
                double t = static_cast<double>(remote_pos_.y - agent.y) /
                           std::max(1, agent.h);
                p.x = (edge_ == Edge::Right) ? local_rect_.right() - 1 : local_rect_.x;
                p.y = local_rect_.y + static_cast<LONG>(std::clamp(t, 0.0, 1.0) * (local_rect_.h - 1));
                break;
            }
            case Edge::Top:
            case Edge::Bottom: {
                double t = static_cast<double>(remote_pos_.x - agent.x) /
                           std::max(1, agent.w);
                p.y = (edge_ == Edge::Top) ? local_rect_.y : local_rect_.bottom() - 1;
                p.x = local_rect_.x + static_cast<LONG>(std::clamp(t, 0.0, 1.0) * (local_rect_.w - 1));
                break;
            }
        }
        SetCursorPos(p.x, p.y);
        set_status("local");
    }

    void on_remote_move(LONG dx, LONG dy) {
        Rect agent;
        {
            std::lock_guard<std::mutex> lock(rect_mutex_);
            agent = agent_rect_;
        }
        if (agent.empty()) {
            return;
        }
        remote_pos_.x += dx;
        remote_pos_.y += dy;

        bool returned = false;
        switch (edge_) {
            case Edge::Right:
                returned = remote_pos_.x < agent.x;
                break;
            case Edge::Left:
                returned = remote_pos_.x > agent.right() - 1;
                break;
            case Edge::Top:
                returned = remote_pos_.y < agent.y;
                break;
            case Edge::Bottom:
                returned = remote_pos_.y > agent.bottom() - 1;
                break;
        }
        if (returned) {
            exit_remote();
            return;
        }
        remote_pos_.x = std::clamp<LONG>(remote_pos_.x, agent.x, agent.right() - 1);
        remote_pos_.y = std::clamp<LONG>(remote_pos_.y, agent.y, agent.bottom() - 1);
        enqueue_move();
    }

    // ------------------------------------------------------------- raw input

    void on_raw(HRAWINPUT raw_handle) {
        UINT size = sizeof(RAWINPUT);
        RAWINPUT raw{};
        if (GetRawInputData(raw_handle, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != size) {
            return;
        }
        if (raw.header.dwType != RIM_TYPEMOUSE) {
            return;
        }
        const RAWMOUSE& mouse = raw.data.mouse;
        if (mouse.usFlags & MOUSE_MOVE_ABSOLUTE) {
            return;  // RDP/tablet style input: not supported in v1
        }
        LONG dx = mouse.lLastX;
        LONG dy = mouse.lLastY;
        if (dx == 0 && dy == 0) {
            return;
        }
        if (remote) {
            on_remote_move(dx, dy);
        } else {
            POINT cursor{};
            GetCursorPos(&cursor);
            try_enter(cursor, dx, dy);
        }
    }

    void on_clipboard() {
        if (!cfg.clipboard || !connected) {
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
            std::lock_guard<std::mutex> lock(clip_send_mutex_);
            clip_send_queue_.push_back(std::move(blob));
        }
        clip_send_cv_.notify_one();
    }

    static LRESULT CALLBACK keyboard_proc(int code, WPARAM wparam, LPARAM lparam) {
        if (code < 0) {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }
        auto* self = static_cast<Impl*>(g_instance.load());
        if (!self || !self->remote) {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }
        auto* info = reinterpret_cast<KBDLLHOOKSTRUCT*>(lparam);
        bool down = (wparam == WM_KEYDOWN || wparam == WM_SYSKEYDOWN);
        uint32_t packed = static_cast<uint32_t>(info->scanCode);
        if (info->flags & LLKHF_EXTENDED) {
            packed |= 0x10000u;
        }
        self->enqueue_key(packed, down);
        return 1;
    }

    static LRESULT CALLBACK mouse_proc(int code, WPARAM wparam, LPARAM lparam) {
        if (code < 0) {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }
        auto* self = static_cast<Impl*>(g_instance.load());
        if (!self || !self->remote) {
            return CallNextHookEx(nullptr, code, wparam, lparam);
        }
        auto* info = reinterpret_cast<MSLLHOOKSTRUCT*>(lparam);
        switch (wparam) {
            case WM_LBUTTONDOWN: self->enqueue_button(MouseButton::Left, true); break;
            case WM_LBUTTONUP: self->enqueue_button(MouseButton::Left, false); break;
            case WM_RBUTTONDOWN: self->enqueue_button(MouseButton::Right, true); break;
            case WM_RBUTTONUP: self->enqueue_button(MouseButton::Right, false); break;
            case WM_MBUTTONDOWN: self->enqueue_button(MouseButton::Middle, true); break;
            case WM_MBUTTONUP: self->enqueue_button(MouseButton::Middle, false); break;
            case WM_XBUTTONDOWN:
                self->enqueue_button((HIWORD(info->mouseData) == XBUTTON1) ? MouseButton::X1
                                                                          : MouseButton::X2,
                                     true);
                break;
            case WM_XBUTTONUP:
                self->enqueue_button((HIWORD(info->mouseData) == XBUTTON1) ? MouseButton::X1
                                                                          : MouseButton::X2,
                                     false);
                break;
            case WM_MOUSEWHEEL:
                self->enqueue_wheel(static_cast<int16_t>(HIWORD(info->mouseData)));
                break;
            default:
                break;
        }
        return 1;
    }

    static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        switch (msg) {
            case WM_INPUT:
                if (self) {
                    self->on_raw(reinterpret_cast<HRAWINPUT>(lparam));
                }
                return DefWindowProcW(hwnd, msg, wparam, lparam);
            case WM_CLIPBOARDUPDATE:
                if (self) {
                    self->on_clipboard();
                }
                return 0;
            case WM_APP + 1:
                if (self) {
                    self->exit_remote();
                    self->connected = false;
                }
                return 0;
            case WM_APP + 9:
                PostQuitMessage(0);
                return 0;
            default:
                break;
        }
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }

    void ui_main(std::promise<bool>* ready) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &Impl::wnd_proc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"LanKVM.Master";
        RegisterClassExW(&wc);

        hwnd_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                wc.hInstance, nullptr);
        if (!hwnd_) {
            LK_ERROR("cannot create master window");
            ready->set_value(false);
            return;
        }
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        ui_thread_id_ = GetCurrentThreadId();

        RAWINPUTDEVICE devices[2]{};
        devices[0].usUsagePage = 0x01;
        devices[0].usUsage = 0x02;
        devices[0].dwFlags = RIDEV_INPUTSINK;
        devices[0].hwndTarget = hwnd_;
        devices[1].usUsagePage = 0x01;
        devices[1].usUsage = 0x06;
        devices[1].dwFlags = RIDEV_INPUTSINK;
        devices[1].hwndTarget = hwnd_;
        if (!RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) {
            LK_ERROR("RegisterRawInputDevices failed (err=%lu)", GetLastError());
            DestroyWindow(hwnd_);
            hwnd_ = nullptr;
            ready->set_value(false);
            return;
        }

        AddClipboardFormatListener(hwnd_);
        g_instance.store(this);

        local_rect_ = virtual_screen();
        LK_INFO("local virtual screen: %dx%d at (%d,%d)", local_rect_.w, local_rect_.h,
                local_rect_.x, local_rect_.y);

        if (cfg.dropzone) {
            if (!dropzone_.create(GetModuleHandleW(nullptr),
                                  [this](const std::vector<std::string>& paths) {
                                      send_files(paths);
                                  })) {
                LK_WARN("drop zone unavailable");
            }
        }

        ready->set_value(true);

        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        uninstall_hooks();
        RemoveClipboardFormatListener(hwnd_);
        dropzone_.destroy();
        g_instance.store(nullptr);
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }

    // ---------------------------------------------------------------- network

    bool handshake(Conn& connection) {
        FrameStream stream(connection.sock);
        Frame frame;

        DWORD timeout = 5000;
        setsockopt(connection.sock.get(), SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));

        uint32_t caps = kCapClipboard | kCapFiles;
        if (cfg.encrypt_clipboard) {
            caps |= kCapEncryption;
        }
        if (!connection.send(MsgType::Hello, msg::hello(caps, computer_name()))) {
            return false;
        }
        if (!stream.read(&frame) || frame.type != MsgType::AuthChallenge ||
            frame.payload.size() != 16) {
            LK_WARN("handshake: no challenge");
            return false;
        }
        std::vector<uint8_t> response =
            hmac_sha256(psk_key(cfg), frame.payload.data(), frame.payload.size());
        if (!connection.send(MsgType::AuthResponse, response)) {
            return false;
        }
        if (!stream.read(&frame) || frame.type != MsgType::AuthResult) {
            LK_WARN("handshake: no auth result");
            return false;
        }
        bool ok = false;
        std::string reason;
        msg::parse_auth_result(frame.payload, &ok, &reason);
        if (!ok) {
            set_status("authentication rejected: " + reason);
            return false;
        }
        if (!stream.read(&frame) || frame.type != MsgType::ScreenInfo) {
            LK_WARN("handshake: no screen info");
            return false;
        }
        ScreenInfo info;
        if (!msg::parse_screen_info(frame.payload, &info)) {
            return false;
        }
        {
            std::lock_guard<std::mutex> lock(rect_mutex_);
            agent_rect_ = info.rc;
        }
        LK_INFO("peer screen: %dx%d at (%d,%d) dpi=%u", info.rc.w, info.rc.h, info.rc.x, info.rc.y,
                info.dpi);

        timeout = 0;
        setsockopt(connection.sock.get(), SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        return true;
    }

    void connect_loop() {
        while (running) {
            if (!connect_enabled) {
                Sleep(200);
                continue;
            }
            if (connected) {
                Sleep(200);
                continue;
            }
            std::string err;
            Socket sock = tcp_connect(cfg.peer_ip, cfg.port, 4000, &err);
            if (!sock.valid()) {
                set_status("cannot reach " + cfg.peer_ip + ": " + err);
                Sleep(1000);
                continue;
            }
            sock.set_nodelay(true);
            sock.set_keepalive(true);

            auto connection = std::make_shared<Conn>();
            connection->sock = std::move(sock);
            {
                std::lock_guard<std::mutex> lock(conn_mutex_);
                conn_ = connection;
            }
            if (!handshake(*connection)) {
                connection->sock.shutdown_both();
                {
                    std::lock_guard<std::mutex> lock(conn_mutex_);
                    if (conn_ == connection) {
                        conn_.reset();
                    }
                }
                Sleep(1000);
                continue;
            }

            connected = true;
            last_recv_ms = now_ms();
            set_status("connected to " + cfg.peer_ip);
            receive_loop(connection);

            connected = false;
            remote = false;
            {
                std::lock_guard<std::mutex> lock(conn_mutex_);
                if (conn_ == connection) {
                    conn_.reset();
                }
            }
            if (hwnd_) {
                PostMessageW(hwnd_, WM_APP + 1, 0, 0);
            }
            Sleep(500);
        }
    }

    void receive_loop(const std::shared_ptr<Conn>& connection) {
        ClipboardReceiver receiver;
        FrameStream stream(connection->sock);
        Frame frame;
        while (running && stream.read(&frame)) {
            last_recv_ms = now_ms();
            switch (frame.type) {
                case MsgType::Echo: {
                    uint64_t ts = 0;
                    if (msg::parse_heartbeat(frame.payload, &ts) && ts != 0) {
                        latency_us = static_cast<int64_t>(now_us() - ts);
                    }
                    break;
                }
                case MsgType::ScreenInfo: {
                    ScreenInfo info;
                    if (msg::parse_screen_info(frame.payload, &info)) {
                        std::lock_guard<std::mutex> lock(rect_mutex_);
                        agent_rect_ = info.rc;
                    }
                    break;
                }
                case MsgType::ClipboardMeta: {
                    std::string clip_err;
                    if (!receiver.on_meta(frame.payload, &clip_err)) {
                        LK_WARN("clipboard meta rejected: %s", clip_err.c_str());
                    }
                    break;
                }
                case MsgType::ClipboardChunk: {
                    std::string clip_err;
                    if (!receiver.on_chunk(frame.payload, &clip_err)) {
                        LK_WARN("clipboard chunk rejected: %s", clip_err.c_str());
                        receiver.reset();
                        break;
                    }
                    if (receiver.complete()) {
                        ClipBlob blob;
                        if (receiver.finish(cfg, &blob, &clip_err)) {
                            remember_hash(clipboard_digest(blob));
                            {
                                std::lock_guard<std::mutex> lock(clip_apply_mutex_);
                                clip_apply_queue_.push_back(std::move(blob));
                            }
                            clip_apply_cv_.notify_one();
                        } else {
                            LK_WARN("clipboard receive failed: %s", clip_err.c_str());
                        }
                    }
                    break;
                }
                case MsgType::FileAck: {
                    uint32_t id = 0;
                    uint8_t status = 0;
                    std::string message;
                    if (msg::parse_file_ack(frame.payload, &id, &status, &message)) {
                        LK_INFO("file #%u ack status=%u (%s)", id, status, message.c_str());
                    }
                    break;
                }
                case MsgType::Bye:
                    LK_INFO("peer closed the session");
                    break;
                default:
                    break;
            }
        }
    }

    void sender_loop() {
        while (true) {
            std::vector<InputEvent> batch;
            {
                std::unique_lock<std::mutex> lock(queue_mutex_);
                queue_cv_.wait_for(lock, std::chrono::milliseconds(4),
                                   [this] { return queue_stop_ || !queue_.empty(); });
                if (queue_stop_) {
                    return;
                }
                while (!queue_.empty() && batch.size() < 512) {
                    batch.push_back(queue_.front());
                    queue_.pop_front();
                }
            }
            if (batch.empty()) {
                continue;
            }
            if (!connected) {
                continue;
            }
            std::shared_ptr<Conn> connection = current_conn();
            if (!connection) {
                continue;
            }
            connection->send(MsgType::InputBatch, msg::input_batch(now_us(), batch.data(), batch.size()));
        }
    }

    void heartbeat_loop() {
        int tick = 0;
        while (running) {
            if (connected) {
                std::shared_ptr<Conn> connection = current_conn();
                if (connection) {
                    connection->send(MsgType::Heartbeat, msg::heartbeat(now_us()));
                }
                if (++tick % 5 == 0) {
                    int64_t us = latency_us.load();
                    if (us >= 0) {
                        LK_DEBUG("round trip latency: %lld us", static_cast<long long>(us));
                    }
                }
            }
            Sleep(static_cast<DWORD>(std::max(200, cfg.heartbeat_ms)));
        }
    }

    void watchdog_loop() {
        while (running) {
            Sleep(500);
            if (!connected) {
                continue;
            }
            uint64_t last = last_recv_ms.load();
            if (last != 0 && now_ms() - last > 6000) {
                LK_WARN("peer timeout");
                std::shared_ptr<Conn> connection = current_conn();
                if (connection) {
                    connection->sock.shutdown_both();
                }
            }
        }
    }

    void clip_send_loop() {
        while (true) {
            ClipBlob blob;
            {
                std::unique_lock<std::mutex> lock(clip_send_mutex_);
                clip_send_cv_.wait(lock, [this] { return clip_send_stop_ || !clip_send_queue_.empty(); });
                if (clip_send_stop_ && clip_send_queue_.empty()) {
                    return;
                }
                blob = std::move(clip_send_queue_.front());
                clip_send_queue_.pop_front();
            }
            std::shared_ptr<Conn> connection = current_conn();
            if (!connection || !connected) {
                continue;
            }
            std::string err;
            if (!send_clipboard(*connection, cfg, blob, &err)) {
                LK_WARN("clipboard send failed: %s", err.c_str());
            }
        }
    }

    void clip_apply_loop() {
        while (true) {
            ClipBlob blob;
            {
                std::unique_lock<std::mutex> lock(clip_apply_mutex_);
                clip_apply_cv_.wait(lock,
                                    [this] { return clip_apply_stop_ || !clip_apply_queue_.empty(); });
                if (clip_apply_stop_ && clip_apply_queue_.empty()) {
                    return;
                }
                blob = std::move(clip_apply_queue_.front());
                clip_apply_queue_.pop_front();
            }
            std::string err;
            if (!clipboard_write(blob, &err)) {
                LK_WARN("cannot apply clipboard: %s", err.c_str());
            } else {
                LK_INFO("clipboard applied: format=%u bytes=%zu", blob.format, blob.data.size());
            }
        }
    }

    void send_files(std::vector<std::string> paths) {
        std::shared_ptr<Conn> connection = current_conn();
        if (!connection || !connected) {
            notify_user("LanKVM", "尚未连接对端，无法发送文件");
            return;
        }
        std::thread([this, connection, paths = std::move(paths)]() {
            for (const std::string& path : paths) {
                std::wstring wide;
                if (!utf8_to_wide(path, &wide)) {
                    continue;
                }
                std::error_code ec;
                std::filesystem::path fs_path(wide);
                if (!std::filesystem::is_regular_file(fs_path, ec)) {
                    LK_WARN("skip non-file: %s", path.c_str());
                    continue;
                }
                uint64_t size = std::filesystem::file_size(fs_path, ec);
                if (ec) {
                    size = 0;
                }
                std::ifstream in(fs_path, std::ios::binary);
                if (!in.is_open()) {
                    LK_WARN("cannot open %s", path.c_str());
                    continue;
                }
                uint32_t id = next_file_id_.fetch_add(1);
                connection->send(MsgType::FileBegin, msg::file_begin(id, size, path));

                std::vector<char> buffer(64 * 1024);
                uint64_t offset = 0;
                while (in) {
                    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
                    std::streamsize got = in.gcount();
                    if (got <= 0) {
                        break;
                    }
                    if (!connection->send(MsgType::FileChunk,
                                          msg::file_chunk(id, offset,
                                                          reinterpret_cast<const uint8_t*>(buffer.data()),
                                                          static_cast<size_t>(got)))) {
                        break;
                    }
                    offset += static_cast<uint64_t>(got);
                }
                connection->send(MsgType::FileEnd, msg::file_end(id));
                LK_INFO("sent file '%s' (%llu bytes)", path.c_str(),
                        static_cast<unsigned long long>(offset));
            }
        }).detach();
    }

    // ------------------------------------------------------------------ life

    bool start(std::string* err) {
        if (running) {
            return true;
        }
        if (!edge_from_name(cfg.edge, &edge_)) {
            edge_ = Edge::Right;
        }
        connect_enabled = cfg.auto_connect;
        running = true;

        std::promise<bool> ready;
        std::future<bool> future = ready.get_future();
        ui_thread_ = std::thread([this, &ready] { ui_main(&ready); });
        if (!future.get()) {
            running = false;
            if (ui_thread_.joinable()) {
                ui_thread_.join();
            }
            if (err) {
                *err = "cannot initialise input capture";
            }
            return false;
        }

        sender_thread_ = std::thread([this] { sender_loop(); });
        heartbeat_thread_ = std::thread([this] { heartbeat_loop(); });
        watchdog_thread_ = std::thread([this] { watchdog_loop(); });
        clip_send_thread_ = std::thread([this] { clip_send_loop(); });
        apply_thread_ = std::thread([this] { clip_apply_loop(); });
        connect_thread_ = std::thread([this] { connect_loop(); });
        return true;
    }

    void stop() {
        if (!running.exchange(false)) {
            return;
        }
        if (hwnd_) {
            PostMessageW(hwnd_, WM_APP + 9, 0, 0);
        }
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_stop_ = true;
        }
        queue_cv_.notify_all();
        {
            std::lock_guard<std::mutex> lock(clip_send_mutex_);
            clip_send_stop_ = true;
        }
        clip_send_cv_.notify_all();
        {
            std::lock_guard<std::mutex> lock(clip_apply_mutex_);
            clip_apply_stop_ = true;
        }
        clip_apply_cv_.notify_all();

        {
            std::lock_guard<std::mutex> lock(conn_mutex_);
            if (conn_) {
                conn_->sock.shutdown_both();
            }
        }

        if (connect_thread_.joinable()) {
            connect_thread_.join();
        }
        if (sender_thread_.joinable()) {
            sender_thread_.join();
        }
        if (heartbeat_thread_.joinable()) {
            heartbeat_thread_.join();
        }
        if (watchdog_thread_.joinable()) {
            watchdog_thread_.join();
        }
        if (clip_send_thread_.joinable()) {
            clip_send_thread_.join();
        }
        if (apply_thread_.joinable()) {
            apply_thread_.join();
        }
        if (ui_thread_.joinable()) {
            ui_thread_.join();
        }
    }
};

Master::Master(const Config& cfg) : impl_(std::make_unique<Impl>()) { impl_->cfg = cfg; }

Master::~Master() { stop(); }

bool Master::start(std::string* err) { return impl_->start(err); }

void Master::stop() {
    if (impl_) {
        impl_->stop();
    }
}

void Master::disconnect() {
    std::lock_guard<std::mutex> lock(impl_->conn_mutex_);
    if (impl_->conn_) {
        impl_->conn_->sock.shutdown_both();
    }
}

void Master::connect_now() {
    impl_->connect_enabled = true;
    std::lock_guard<std::mutex> lock(impl_->conn_mutex_);
    if (impl_->conn_) {
        impl_->conn_->sock.shutdown_both();
    }
}

bool Master::running() const { return impl_->running.load(); }

bool Master::connected() const { return impl_->connected.load(); }

bool Master::remote_active() const { return impl_->remote.load(); }

int64_t Master::latency_us() const { return impl_->latency_us.load(); }

std::string Master::status_text() const { return impl_->status_text(); }

void Master::set_notify(NotifyFn fn) {
    std::lock_guard<std::mutex> lock(impl_->status_mutex_);
    impl_->notify_ = std::move(fn);
}

void Master::send_files(const std::vector<std::string>& paths) { impl_->send_files(paths); }

}  // namespace lk
