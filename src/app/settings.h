#pragma once

#include <windows.h>

#include "common/config.h"

namespace lk {

// Modal settings window. Returns true when the user saved changes.
class SettingsDialog {
public:
    static bool show(HWND parent, Config* cfg);
};

}  // namespace lk
