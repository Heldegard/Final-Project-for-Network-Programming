#pragma once

#include <functional>
#include <memory>
#include <string>

#include "common/config.h"

namespace lk {

// Receiving side: listens, authenticates the master, injects input, applies
// clipboard updates and stores dropped files.
class Agent {
public:
    using NotifyFn = std::function<void(const std::string& title, const std::string& text)>;

    explicit Agent(const Config& cfg);
    ~Agent();

    bool start(std::string* err);
    void stop();

    bool running() const;
    bool peer_connected() const;
    std::string status_text() const;
    void set_notify(NotifyFn fn);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace lk
