#include <winsock2.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <windows.h>
#include <shellapi.h>

#include "agent/agent.h"
#include "app/settings.h"
#include "app/tray.h"
#include "common/clipboard.h"
#include "common/config.h"
#include "common/crypto.h"
#include "common/log.h"
#include "common/net.h"
#include "common/protocol.h"
#include "master/master.h"

namespace {

constexpr UINT kBalloonMessage = WM_APP + 21;
constexpr UINT_PTR kTimerId = 1;
constexpr wchar_t kMainClass[] = L"LanKVM.Main";

struct App {
    lk::Config cfg;
    std::wstring config_path;
    std::wstring log_path;
    std::string local_ip;
    HWND hwnd = nullptr;
    HANDLE instance_mutex = nullptr;
    lk::Tray tray;
    std::unique_ptr<lk::Master> master;
    std::unique_ptr<lk::Agent> agent;
};

App* g_app = nullptr;

void post_balloon(const std::string& title, const std::string& text) {
    if (!g_app || !g_app->hwnd) {
        return;
    }
    auto* wide_title = new std::wstring();
    auto* wide_text = new std::wstring();
    lk::utf8_to_wide(title, wide_title);
    lk::utf8_to_wide(text, wide_text);
    PostMessageW(g_app->hwnd, kBalloonMessage, reinterpret_cast<WPARAM>(wide_title),
                 reinterpret_cast<LPARAM>(wide_text));
}

void stop_role() {
    App* app = g_app;
    if (!app) {
        return;
    }
    if (app->master) {
        app->master->stop();
        app->master.reset();
    }
    if (app->agent) {
        app->agent->stop();
        app->agent.reset();
    }
}

bool start_role(std::string* err) {
    App* app = g_app;
    if (!app) {
        return false;
    }
    if (app->cfg.role == "agent") {
        app->agent = std::make_unique<lk::Agent>(app->cfg);
        app->agent->set_notify([](const std::string& title, const std::string& text) {
            post_balloon(title, text);
        });
        return app->agent->start(err);
    }
    app->master = std::make_unique<lk::Master>(app->cfg);
    app->master->set_notify([](const std::string& title, const std::string& text) {
        post_balloon(title, text);
    });
    return app->master->start(err);
}

void restart_role() {
    stop_role();
    std::string err;
    if (!start_role(&err)) {
        std::wstring wide;
        lk::utf8_to_wide(err, &wide);
        MessageBoxW(g_app->hwnd, wide.c_str(), L"LanKVM 启动失败", MB_OK | MB_ICONERROR);
    }
}

void update_tooltip() {
    App* app = g_app;
    if (!app) {
        return;
    }
    std::wstring text = L"LanKVM ";
    text += (app->cfg.role == "agent") ? L"[受控端] " : L"[主控端] ";
    if (!app->local_ip.empty()) {
        std::wstring wide_ip;
        lk::utf8_to_wide(app->local_ip, &wide_ip);
        text += wide_ip + L":" + std::to_wstring(app->cfg.port) + L"  ";
    }
    if (app->master) {
        text += app->master->connected() ? L"已连接" : L"等待连接";
        if (app->master->remote_active()) {
            text += L" / 远程控制中";
        }
        int64_t latency = app->master->latency_us();
        if (latency >= 0) {
            wchar_t buffer[64]{};
            std::swprintf(buffer, std::size(buffer), L" / %.1fms", latency / 1000.0);
            text += buffer;
        }
    } else if (app->agent) {
        text += app->agent->peer_connected() ? L"已连接" : L"监听中";
    } else {
        text += L"未运行";
    }
    app->tray.update_tooltip(text);
}

void handle_command(int command) {
    App* app = g_app;
    if (!app) {
        return;
    }
    switch (command) {
        case lk::Tray::kCmdConnect:
            if (app->master) {
                app->master->connect_now();
            } else {
                restart_role();
            }
            break;
        case lk::Tray::kCmdDisconnect:
            if (app->master) {
                app->master->disconnect();
            } else if (app->agent) {
                app->agent->stop();
            }
            break;
        case lk::Tray::kCmdSettings: {
            lk::Config updated = app->cfg;
            if (lk::SettingsDialog::show(app->hwnd, &updated)) {
                app->cfg = updated;
                std::string save_err;
                lk::config_save(app->config_path, app->cfg, &save_err);
                restart_role();
                update_tooltip();
            }
            break;
        }
        case lk::Tray::kCmdOpenLog:
            ShellExecuteW(nullptr, L"open", app->log_path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case lk::Tray::kCmdAbout: {
            std::wstring message = L"LanKVM 0.1.1\n\n";
            message += L"局域网键鼠共享 + 剪贴板同步 + 文件投递\n\n";
            message += L"角色：";
            message += (app->cfg.role == "agent") ? L"受控端" : L"主控端";
            message += L"\n端口：";
            message += std::to_wstring(app->cfg.port);
            message += L"\n本机地址：";
            std::wstring wide_ip;
            lk::utf8_to_wide(app->local_ip, &wide_ip);
            message += wide_ip.empty() ? L"未知" : wide_ip;
            message += L"\n配置：";
            message += app->config_path;
            MessageBoxW(app->hwnd, message.c_str(), L"关于 LanKVM", MB_OK | MB_ICONINFORMATION);
            break;
        }
        case lk::Tray::kCmdExit:
            DestroyWindow(app->hwnd);
            break;
        default:
            break;
    }
}

LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    App* app = g_app;
    switch (msg) {
        case lk::Tray::kCallbackMessage:
            if (app && (lparam == WM_RBUTTONUP || lparam == WM_LBUTTONUP || lparam == WM_CONTEXTMENU)) {
                int command = app->tray.show_menu();
                if (command != 0) {
                    handle_command(command);
                }
            }
            return 0;
        case kBalloonMessage: {
            auto* title = reinterpret_cast<std::wstring*>(wparam);
            auto* text = reinterpret_cast<std::wstring*>(lparam);
            if (app && title && text) {
                app->tray.balloon(*title, *text);
            }
            delete title;
            delete text;
            return 0;
        }
        case WM_TIMER:
            if (wparam == kTimerId) {
                update_tooltip();
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

void print_usage() {
    std::printf(
        "LanKVM 0.1.1\n"
        "usage: lankvm [master|agent] [--config <path>] [--netinfo] [--probe <ip> [port]]\n"
        "  master      capture local input and drive the peer (default)\n"
        "  agent       listen and inject input received from the master\n"
        "  --netinfo   list every network adapter and which address to use\n"
        "  --probe     test one TCP connect and explain the result\n");
}

int run_selftest() {
    int failures = 0;
    auto check = [&failures](bool ok, const char* name) {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) {
            ++failures;
        }
    };

    check(lk::crypto_init(), "crypto_init");

    std::vector<uint8_t> digest = lk::sha256("abc", 3);
    check(digest.size() == 32 && digest[0] == 0xba && digest[1] == 0x78, "sha256 known answer");

    std::vector<uint8_t> key(32, 0x0b);
    std::vector<uint8_t> mac = lk::hmac_sha256(key, "Hi There", 8);
    check(mac.size() == 32, "hmac produces 32 bytes");

    uint8_t aes_key[32]{};
    uint8_t nonce[12]{};
    for (int i = 0; i < 32; ++i) {
        aes_key[i] = static_cast<uint8_t>(i);
    }
    std::vector<uint8_t> plain{'h', 'e', 'l', 'l', 'o'};
    std::vector<uint8_t> cipher;
    uint8_t tag[16]{};
    bool encrypted = lk::aes256_gcm_encrypt(aes_key, nonce, plain.data(), plain.size(), &cipher, tag);
    std::vector<uint8_t> roundtrip;
    bool decrypted = encrypted && lk::aes256_gcm_decrypt(aes_key, nonce, cipher.data(), cipher.size(),
                                                         tag, &roundtrip);
    check(decrypted && roundtrip == plain, "aes-256-gcm round trip");

    tag[0] ^= 0xFF;
    std::vector<uint8_t> tampered;
    check(!lk::aes256_gcm_decrypt(aes_key, nonce, cipher.data(), cipher.size(), tag, &tampered),
          "aes-256-gcm rejects tampered tag");

    lk::Frame frame;
    frame.type = lk::MsgType::InputBatch;
    frame.seq = 42;
    frame.payload = {1, 2, 3, 4};
    std::vector<uint8_t> bytes;
    lk::encode_frame(frame, &bytes);
    check(bytes.size() == lk::kHeaderSize + 4, "frame encoding size");

    std::printf("selftest: %d failure(s)\n", failures);
    return failures;
}

}  // namespace

int main(int argc, char** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    SetConsoleOutputCP(CP_UTF8);

    std::string role;
    std::wstring config_path;
    bool selftest = false;
    bool netinfo = false;
    std::string probe_host;
    uint16_t probe_port = 24800;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        }
        if (arg == "--selftest") {
            selftest = true;
            continue;
        }
        if (arg == "--netinfo") {
            netinfo = true;
            continue;
        }
        if (arg == "--probe" && i + 1 < argc) {
            probe_host = argv[++i];
            if (i + 1 < argc) {
                int candidate = std::atoi(argv[i + 1]);
                if (candidate > 0 && candidate < 65536) {
                    probe_port = static_cast<uint16_t>(candidate);
                    ++i;
                }
            }
            continue;
        }
        if (arg == "--config" && i + 1 < argc) {
            std::string path = argv[++i];
            config_path.assign(path.begin(), path.end());
            continue;
        }
        if (arg == "master" || arg == "agent") {
            role = arg;
            continue;
        }
        std::printf("unknown argument: %s\n", arg.c_str());
        print_usage();
        return 2;
    }

    if (selftest) {
        return run_selftest();
    }

    if (netinfo) {
        if (!lk::net_init()) {
            std::printf("WSAStartup failed\n");
            return 1;
        }
        std::string report = lk::describe_adapters();
        std::printf("%s", report.c_str());
        lk::net_shutdown();
        return 0;
    }

    if (!probe_host.empty()) {
        if (!lk::net_init()) {
            std::printf("WSAStartup failed\n");
            return 1;
        }
        int elapsed = -1;
        std::string detail;
        lk::ProbeResult result = lk::probe_tcp(probe_host, probe_port, 4000, &elapsed, &detail);
        const char* verdict = "ERROR";
        const char* advice = "";
        switch (result) {
            case lk::ProbeResult::Ok:
                verdict = "OK";
                advice = "端口开放：对端程序在监听，网络可达。";
                break;
            case lk::ProbeResult::Refused:
                verdict = "REFUSED";
                advice = "主机可达，但端口没人监听：对端 LanKVM 没启动，或端口填错。";
                break;
            case lk::ProbeResult::Timeout:
                verdict = "TIMEOUT";
                advice = "完全没有回应：对端关机 / 防火墙丢包 / 两机不在同一网络（校园网客户端隔离）。";
                break;
            case lk::ProbeResult::Unreachable:
                verdict = "UNREACHABLE";
                advice = "路由不可达：不在同一网段，检查 IP 或换网络。";
                break;
            case lk::ProbeResult::Error:
                verdict = "ERROR";
                advice = "本地错误，见 detail。";
                break;
        }
        std::printf("probe %s:%u -> %s (%d ms)\n", probe_host.c_str(),
                    static_cast<unsigned>(probe_port), verdict, elapsed);
        std::printf("detail : %s\n", detail.c_str());
        std::printf("local  : %s\n", lk::local_ipv4_for(probe_host).c_str());
        std::printf("%s\n", advice);
        lk::net_shutdown();
        return (result == lk::ProbeResult::Ok || result == lk::ProbeResult::Refused) ? 0 : 1;
    }

    if (!lk::net_init()) {
        std::printf("WSAStartup failed\n");
        return 1;
    }
    if (!lk::crypto_init()) {
        std::printf("crypto init failed\n");
        lk::net_shutdown();
        return 1;
    }

    App app;
    g_app = &app;
    app.config_path = config_path.empty() ? lk::config_default_path() : config_path;

    std::string err;
    bool loaded = lk::config_load(app.config_path, &app.cfg, &err);
    bool first_run = !loaded;
    if (!loaded) {
        LK_INFO("config not found (%s), creating defaults", err.c_str());
        lk::config_ensure_psk(&app.cfg);
        lk::config_save(app.config_path, app.cfg, nullptr);
    } else if (!lk::config_ensure_psk(&app.cfg)) {
        LK_ERROR("cannot prepare pre-shared key");
    }
    if (!role.empty()) {
        app.cfg.role = role;
    }
    if (app.cfg.role != "agent") {
        app.cfg.role = "master";
    }
    if (!role.empty()) {
        // Keep the ini in sync with the effective role. Otherwise the file says
        // "master" while the process runs as an agent, and the next plain
        // double-click silently starts the wrong mode.
        lk::config_save(app.config_path, app.cfg, nullptr);
    }

    // Only one instance per role+port, so a stale master icon cannot be mistaken
    // for the agent that was just launched.
    std::wstring mutex_name = L"Local\\LanKVM." +
                              std::wstring(app.cfg.role.begin(), app.cfg.role.end()) + L"." +
                              std::to_wstring(app.cfg.port);
    app.instance_mutex = CreateMutexW(nullptr, TRUE, mutex_name.c_str());
    if (app.instance_mutex != nullptr && GetLastError() == ERROR_ALREADY_EXISTS) {
        std::printf("another %s instance is already running on port %u; exiting.\n",
                    app.cfg.role.c_str(), static_cast<unsigned>(app.cfg.port));
        MessageBoxW(nullptr,
                    L"已经有一个相同角色的 LanKVM 在运行。\n\n"
                    L"请查看系统托盘（图标可能在 ^ 折叠区）。\n"
                    L"如果托盘里的角色和你预期的不一样，那是旧实例，先退出它再启动。",
                    L"LanKVM", MB_OK | MB_ICONINFORMATION);
        CloseHandle(app.instance_mutex);
        return 0;
    }

    lk::LogLevel level = lk::LogLevel::Info;
    lk::log_parse_level(app.cfg.log_level, &level);
    app.log_path = app.config_path;
    size_t dot = app.log_path.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        app.log_path = app.log_path.substr(0, dot);
    }
    app.log_path += L".log";
    lk::log_open(app.log_path, level);
    app.local_ip = lk::local_ipv4();
    LK_INFO("LanKVM starting as %s (local %s:%u)", app.cfg.role.c_str(), app.local_ip.c_str(),
            static_cast<unsigned>(app.cfg.port));

    {
        std::wstring console_title =
            (app.cfg.role == "agent") ? L"LanKVM [受控端] " : L"LanKVM [主控端] ";
        console_title += std::to_wstring(app.cfg.port);
        SetConsoleTitleW(console_title.c_str());
    }

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kMainClass;
    RegisterClassExW(&wc);
    app.hwnd = CreateWindowExW(0, kMainClass, L"LanKVM", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                               wc.hInstance, nullptr);
    if (!app.hwnd) {
        std::printf("cannot create message window\n");
        return 1;
    }

    app.tray.create(app.hwnd, L"LanKVM");
    update_tooltip();

    if (!start_role(&err)) {
        std::wstring wide;
        lk::utf8_to_wide(err, &wide);
        MessageBoxW(nullptr, wide.c_str(), L"LanKVM 启动失败", MB_OK | MB_ICONERROR);
        return 1;
    }

    SetTimer(app.hwnd, kTimerId, 1000, nullptr);

    if (first_run) {
        std::printf("first run: config written to %ls\n", app.config_path.c_str());
        std::printf("pre-shared key: %s\n", app.cfg.psk_hex.c_str());
    }
    std::printf("\n==================================================\n");
    std::printf(" LanKVM 0.1.1\n");
    std::printf(" role   : %s\n", app.cfg.role.c_str());
    std::printf(" config : %ls\n", app.config_path.c_str());
    std::printf(" local  : %s:%u\n", app.local_ip.c_str(), static_cast<unsigned>(app.cfg.port));
    if (app.cfg.role == "agent") {
        std::printf(" listen : 0.0.0.0:%u\n", static_cast<unsigned>(app.cfg.port));
        std::printf(" -> 在主控端的 \"对端 IP\" 里填 %s\n", app.local_ip.c_str());
    } else {
        std::printf(" peer   : %s:%u\n", app.cfg.peer_ip.c_str(),
                    static_cast<unsigned>(app.cfg.port));
    }
    std::printf("==================================================\n\n");

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(app.hwnd, kTimerId);
    stop_role();
    app.tray.destroy();
    if (app.instance_mutex) {
        CloseHandle(app.instance_mutex);
        app.instance_mutex = nullptr;
    }
    LK_INFO("LanKVM exiting");
    lk::log_close();
    lk::net_shutdown();
    g_app = nullptr;
    return 0;
}
