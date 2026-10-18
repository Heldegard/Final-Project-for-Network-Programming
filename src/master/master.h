#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "common/config.h"

namespace lk {

// Sending side: captures local input, decides when the cursor crosses the
// configured screen edge, forwards input to the agent, mirrors the clipboard and
// offers a drop zone for files.
class Master {
public:
    using NotifyFn = std::function<void(const std::string& title, const std::string& text)>;

    explicit Master(const Config& cfg);
    ~Master();

    bool start(std::string* err);
    void stop();
    void disconnect();
    void connect_now();

    bool running() const;
    bool connected() const;
    bool remote_active() const;
    int64_t latency_us() const;
    std::string status_text() const;

    void set_notify(NotifyFn fn);
    void send_files(const std::vector<std::string>& paths);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lk
