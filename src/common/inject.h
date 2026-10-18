#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include "common/protocol.h"
#include "common/types.h"

namespace lk {

class Injector {
public:
    void set_screen(const Rect& screen) { screen_ = screen; }
    const Rect& screen() const { return screen_; }

    void inject(const std::vector<InputEvent>& events);
    void release_all();

private:
    void move_absolute(uint16_t nx, uint16_t ny);
    void mouse_button(uint8_t button, bool down);
    void wheel(int32_t delta);
    void key(uint32_t packed_scan, bool down);

    Rect screen_;
    std::set<uint32_t> pressed_keys_;
    std::set<uint8_t> pressed_buttons_;
};

}  // namespace lk
